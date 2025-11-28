#include <CL/cl.h>
#include <stdio.h>
#include <stdlib.h>

const char *kernel_source = 
"__kernel void vector_add(__global const float *a, __global const float *b, __global float *c) {\n"
"    int gid = get_global_id(0);\n"
"    c[gid] = a[gid] + b[gid];\n"
"}\n";

int main() {
    // Get Clover platform
    cl_platform_id platform;
    cl_uint num_platforms;
    clGetPlatformIDs(0, NULL, &num_platforms);
    cl_platform_id *platforms = malloc(sizeof(cl_platform_id) * num_platforms);
    clGetPlatformIDs(num_platforms, platforms, NULL);
    
    // Find Clover
    for (cl_uint i = 0; i < num_platforms; i++) {
        char name[256];
        clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(name), name, NULL);
        if (strstr(name, "Clover")) {
            platform = platforms[i];
            printf("Using platform: %s\n", name);
            break;
        }
    }
    
    // Get GPU device
    cl_device_id device;
    clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);
    
    char dev_name[256];
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(dev_name), dev_name, NULL);
    printf("Using device: %s\n", dev_name);
    
    // Create context and queue
    cl_context context = clCreateContext(NULL, 1, &device, NULL, NULL, NULL);
    cl_command_queue queue = clCreateCommandQueue(context, device, 0, NULL);
    
    // Create and build program
    printf("Compiling kernel...\n");
    cl_program program = clCreateProgramWithSource(context, 1, &kernel_source, NULL, NULL);
    cl_int err = clBuildProgram(program, 1, &device, NULL, NULL, NULL);
    
    if (err != CL_SUCCESS) {
        char log[4096];
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, sizeof(log), log, NULL);
        printf("Build failed:\n%s\n", log);
        return 1;
    }
    printf("Kernel compiled successfully!\n");
    
    // Create kernel
    cl_kernel kernel = clCreateKernel(program, "vector_add", NULL);
    
    // Prepare data
    const int N = 1024;
    float *a = malloc(N * sizeof(float));
    float *b = malloc(N * sizeof(float));
    float *c = malloc(N * sizeof(float));
    
    for (int i = 0; i < N; i++) {
        a[i] = i;
        b[i] = i * 2;
    }
    
    // Create buffers
    cl_mem buf_a = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, N * sizeof(float), a, NULL);
    cl_mem buf_b = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, N * sizeof(float), b, NULL);
    cl_mem buf_c = clCreateBuffer(context, CL_MEM_WRITE_ONLY, N * sizeof(float), NULL, NULL);
    
    // Set kernel arguments
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &buf_a);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &buf_b);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &buf_c);
    
    // Execute kernel
    printf("Executing kernel on GPU...\n");
    size_t global_size = N;
    clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global_size, NULL, 0, NULL, NULL);
    
    // Read results
    clEnqueueReadBuffer(queue, buf_c, CL_TRUE, 0, N * sizeof(float), c, 0, NULL, NULL);
    
    // Verify
    int errors = 0;
    for (int i = 0; i < N; i++) {
        float expected = a[i] + b[i];
        if (c[i] != expected) {
            if (errors < 10) printf("Error at %d: got %.2f, expected %.2f\n", i, c[i], expected);
            errors++;
        }
    }
    
    if (errors == 0) {
        printf("✓ SUCCESS! All %d elements computed correctly on GPU!\n", N);
    } else {
        printf("✗ FAILED: %d errors\n", errors);
    }
    
    // Cleanup
    free(a); free(b); free(c); free(platforms);
    clReleaseMemObject(buf_a);
    clReleaseMemObject(buf_b);
    clReleaseMemObject(buf_c);
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);
    
    return errors == 0 ? 0 : 1;
}
