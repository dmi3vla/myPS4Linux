#include <CL/cl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>

#define MATRIX_SIZE 1024
#define TILE_SIZE 16

const char *kernel_source = 
"#define TS 16\n"
"__kernel void sgemm_naive(const int M, const int N, const int K,\n"
"                          const __global float* A,\n"
"                          const __global float* B,\n"
"                          __global float* C) {\n"
"    int row = get_global_id(1);\n"
"    int col = get_global_id(0);\n"
"    if (row < M && col < N) {\n"
"        float sum = 0.0f;\n"
"        for (int i = 0; i < K; i++) {\n"
"            sum += A[row * K + i] * B[i * N + col];\n"
"        }\n"
"        C[row * N + col] = sum;\n"
"    }\n"
"}\n"
"\n"
"__kernel void sgemm_tiled(const int M, const int N, const int K,\n"
"                          const __global float* A,\n"
"                          const __global float* B,\n"
"                          __global float* C) {\n"
"    const int row = get_local_id(1);\n"
"    const int col = get_local_id(0);\n"
"    const int globalRow = TS * get_group_id(1) + row;\n"
"    const int globalCol = TS * get_group_id(0) + col;\n"
"\n"
"    __local float Asub[TS][TS];\n"
"    __local float Bsub[TS][TS];\n"
"\n"
"    float acc = 0.0f;\n"
"\n"
"    const int numTiles = K / TS;\n"
"    for (int t = 0; t < numTiles; t++) {\n"
"        const int tiledCol = TS * t + col;\n"
"        const int tiledRow = TS * t + row;\n"
"        \n"
"        Asub[row][col] = A[globalRow * K + tiledCol];\n"
"        Bsub[row][col] = B[tiledRow * N + globalCol];\n"
"\n"
"        barrier(CLK_LOCAL_MEM_FENCE);\n"
"\n"
"        for (int k = 0; k < TS; k++) {\n"
"            acc += Asub[row][k] * Bsub[k][col];\n"
"        }\n"
"\n"
"        barrier(CLK_LOCAL_MEM_FENCE);\n"
"    }\n"
"\n"
"    C[globalRow * N + globalCol] = acc;\n"
"}\n";

double get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void cpu_sgemm(int M, int N, int K, float *A, float *B, float *C) {
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            float sum = 0.0f;
            for (int k = 0; k < K; k++) {
                sum += A[i * K + k] * B[k * N + j];
            }
            C[i * N + j] = sum;
        }
    }
}

int main() {
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  OpenCL Matrix Multiplication Benchmark (Gladius PS4 Pro)\n");
    printf("  Matrix Size: %dx%d\n", MATRIX_SIZE, MATRIX_SIZE);
    printf("═══════════════════════════════════════════════════════════════\n\n");

    // 1. Setup OpenCL
    cl_uint num_platforms;
    clGetPlatformIDs(0, NULL, &num_platforms);
    cl_platform_id *platforms = malloc(sizeof(cl_platform_id) * num_platforms);
    clGetPlatformIDs(num_platforms, platforms, NULL);
    
    cl_platform_id platform = platforms[0];
    for (cl_uint i = 0; i < num_platforms; i++) {
        char name[256];
        clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(name), name, NULL);
        if (strstr(name, "Clover") || strstr(name, "AMD")) {
            platform = platforms[i];
            printf("Using Platform: %s\n", name);
            break;
        }
    }
    
    cl_device_id device;
    clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);
    char dev_name[256];
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(dev_name), dev_name, NULL);
    cl_uint cus;
    clGetDeviceInfo(device, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cus), &cus, NULL);
    printf("Using Device:   %s (%u CUs)\n\n", dev_name, cus);

    cl_context context = clCreateContext(NULL, 1, &device, NULL, NULL, NULL);
    cl_command_queue queue = clCreateCommandQueue(context, device, 0, NULL);
    
    cl_program program = clCreateProgramWithSource(context, 1, &kernel_source, NULL, NULL);
    cl_int err = clBuildProgram(program, 1, &device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        char log[4096];
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, sizeof(log), log, NULL);
        printf("Build Error:\n%s\n", log);
        return 1;
    }

    cl_kernel kernel_naive = clCreateKernel(program, "sgemm_naive", NULL);
    cl_kernel kernel_tiled = clCreateKernel(program, "sgemm_tiled", NULL);

    // 2. Prepare Data
    size_t size = MATRIX_SIZE * MATRIX_SIZE * sizeof(float);
    float *h_A = malloc(size);
    float *h_B = malloc(size);
    float *h_C_gpu = malloc(size);
    float *h_C_cpu = malloc(size);

    srand(time(NULL));
    for (int i = 0; i < MATRIX_SIZE * MATRIX_SIZE; i++) {
        h_A[i] = (float)(rand() % 100) / 10.0f;
        h_B[i] = (float)(rand() % 100) / 10.0f;
    }

    cl_mem d_A = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, size, h_A, NULL);
    cl_mem d_B = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, size, h_B, NULL);
    cl_mem d_C = clCreateBuffer(context, CL_MEM_WRITE_ONLY, size, NULL, NULL);

    int M = MATRIX_SIZE;
    int N = MATRIX_SIZE;
    int K = MATRIX_SIZE;

    // 3. Run Naive Kernel
    printf("[1] Running Naive Kernel...\n");
    clSetKernelArg(kernel_naive, 0, sizeof(int), &M);
    clSetKernelArg(kernel_naive, 1, sizeof(int), &N);
    clSetKernelArg(kernel_naive, 2, sizeof(int), &K);
    clSetKernelArg(kernel_naive, 3, sizeof(cl_mem), &d_A);
    clSetKernelArg(kernel_naive, 4, sizeof(cl_mem), &d_B);
    clSetKernelArg(kernel_naive, 5, sizeof(cl_mem), &d_C);

    size_t global[2] = {MATRIX_SIZE, MATRIX_SIZE};
    
    double start = get_time();
    clEnqueueNDRangeKernel(queue, kernel_naive, 2, NULL, global, NULL, 0, NULL, NULL);
    clFinish(queue);
    double end = get_time();
    double time_naive = end - start;
    double gflops_naive = (2.0 * M * N * K) / (time_naive * 1e9);
    printf("    Time: %.4f s | Performance: %.2f GFLOPS\n", time_naive, gflops_naive);

    // 4. Run Tiled Kernel
    printf("[2] Running Tiled Kernel (LDS optimized)...\n");
    clSetKernelArg(kernel_tiled, 0, sizeof(int), &M);
    clSetKernelArg(kernel_tiled, 1, sizeof(int), &N);
    clSetKernelArg(kernel_tiled, 2, sizeof(int), &K);
    clSetKernelArg(kernel_tiled, 3, sizeof(cl_mem), &d_A);
    clSetKernelArg(kernel_tiled, 4, sizeof(cl_mem), &d_B);
    clSetKernelArg(kernel_tiled, 5, sizeof(cl_mem), &d_C);

    size_t local[2] = {TILE_SIZE, TILE_SIZE};
    
    start = get_time();
    clEnqueueNDRangeKernel(queue, kernel_tiled, 2, NULL, global, local, 0, NULL, NULL);
    clFinish(queue);
    end = get_time();
    double time_tiled = end - start;
    double gflops_tiled = (2.0 * M * N * K) / (time_tiled * 1e9);
    printf("    Time: %.4f s | Performance: %.2f GFLOPS\n", time_tiled, gflops_tiled);
    
    // Read back result
    clEnqueueReadBuffer(queue, d_C, CL_TRUE, 0, size, h_C_gpu, 0, NULL, NULL);

    // 5. Verify (CPU check on small subset to save time)
    printf("[3] Verifying results (checking top-left 16x16 block)...\n");
    int errors = 0;
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            float sum = 0.0f;
            for (int k = 0; k < MATRIX_SIZE; k++) {
                sum += h_A[i * MATRIX_SIZE + k] * h_B[k * MATRIX_SIZE + j];
            }
            if (fabs(sum - h_C_gpu[i * MATRIX_SIZE + j]) > 0.1f) {
                errors++;
                if (errors < 5) printf("Error at [%d][%d]: GPU=%.2f, CPU=%.2f\n", i, j, h_C_gpu[i * MATRIX_SIZE + j], sum);
            }
        }
    }

    if (errors == 0) {
        printf("    ✓ SUCCESS: Results match CPU reference!\n");
    } else {
        printf("    ✗ FAILED: %d errors found.\n", errors);
    }

    printf("\nSpeedup (Tiled vs Naive): %.2fx\n", time_naive / time_tiled);

    // Cleanup
    free(h_A); free(h_B); free(h_C_gpu); free(h_C_cpu); free(platforms);
    clReleaseMemObject(d_A); clReleaseMemObject(d_B); clReleaseMemObject(d_C);
    clReleaseKernel(kernel_naive); clReleaseKernel(kernel_tiled);
    clReleaseProgram(program); clReleaseCommandQueue(queue); clReleaseContext(context);
    
    return 0;
}
