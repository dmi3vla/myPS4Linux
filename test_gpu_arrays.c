#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#include <CL/cl.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define ARRAY_SIZE      (1 * 1024 * 1024)  // 1M элементов
#define RUNS            3                  // число прогонов для усреднения
#define MAX_PLATFORMS   8
#define MAX_DEVICES     8

const char *kernelSource =
"__kernel void vec_mul(__global const float* A,\n"
"                      __global const float* B,\n"
"                      __global float* C,\n"
"                      const unsigned int N) {\n"
"    size_t i = get_global_id(0);\n"
"    if (i < N) {\n"
"        C[i] = A[i] * B[i];\n"
"    }\n"
"}\n";

static void check_status(cl_int err, const char *msg) {
    if (err != CL_SUCCESS) {
        fprintf(stderr, "ERROR: %s (err = %d)\n", msg, err);
        exit(1);
    }
}

int main(void) {
    cl_int err;

    printf("=== GPU array multiplication OpenCL test ===\n");
    printf("Target platform: \"AMD Accelerated Parallel Processing\"\n");
    printf("Target device:   first GPU (gfx701)\n\n");

    // --- 1. Инициализация платформ и устройств ---
    cl_platform_id platforms[MAX_PLATFORMS];
    cl_uint num_platforms = 0;
    check_status(clGetPlatformIDs(MAX_PLATFORMS, platforms, &num_platforms),
                 "clGetPlatformIDs");

    cl_platform_id chosen_platform = NULL;
    char pname[256];

    for (cl_uint i = 0; i < num_platforms; i++) {
        size_t len = 0;
        clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME,
                          sizeof(pname), pname, &len);
        printf("Platform %u: %s\n", i, pname);
        if (strstr(pname, "AMD Accelerated Parallel Processing") != NULL) {
            chosen_platform = platforms[i];
        }
    }

    if (!chosen_platform) {
        fprintf(stderr, "ERROR: AMD Accelerated Parallel Processing platform not found\n");
        return 1;
    }

    cl_device_id devices[MAX_DEVICES];
    cl_uint num_devices = 0;
    check_status(clGetDeviceIDs(chosen_platform, CL_DEVICE_TYPE_GPU,
                                MAX_DEVICES, devices, &num_devices),
                 "clGetDeviceIDs(GPU)");

    if (num_devices == 0) {
        fprintf(stderr, "ERROR: no GPU devices on chosen platform\n");
        return 1;
    }

    cl_device_id device = devices[0];

    char dname[256];
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(dname), dname, NULL);
    printf("\nUsing device: %s\n", dname);

    // --- 2. Контекст и очередь команд ---

    // Свойства контекста: привязываем его к выбранной платформе
    cl_context_properties ctx_props[] = {
        CL_CONTEXT_PLATFORM, (cl_context_properties)chosen_platform,
        0
    };

    cl_context context = clCreateContext(
        ctx_props,   // свойства контекста (платформа AMD APP)
        1,           // одно устройство
        &device,     // указатель на наше устройство gfx701
        NULL,        // без callback-функции
        NULL,        // без user_data
        &err
    );
    check_status(err, "clCreateContext");

    // Свойства очереди: включаем профилирование
    cl_queue_properties qprops[] = {
        CL_QUEUE_PROPERTIES, CL_QUEUE_PROFILING_ENABLE,
        0
    };

    cl_command_queue queue = clCreateCommandQueueWithProperties(
        context, device, qprops, &err
    );
    check_status(err, "clCreateCommandQueueWithProperties");

    // --- 3. Подготовка данных ---
    size_t N = ARRAY_SIZE;
    size_t bytes = N * sizeof(float);

    printf("\nArray size: %zu elements (%.2f MB per array)\n",
           N, bytes / (1024.0 * 1024.0));

    float *hA = (float *)malloc(bytes);
    float *hB = (float *)malloc(bytes);
    float *hC = (float *)malloc(bytes);
    if (!hA || !hB || !hC) {
        fprintf(stderr, "ERROR: host malloc failed\n");
        return 1;
    }

    for (size_t i = 0; i < N; i++) {
        hA[i] = (float)(i % 1000) * 0.001f;
        hB[i] = (float)((i * 7) % 1000) * 0.002f;
    }

    // --- 4. Буферы ---
    cl_mem dA = clCreateBuffer(context, CL_MEM_READ_ONLY,  bytes, NULL, &err);
    check_status(err, "clCreateBuffer(A)");
    cl_mem dB = clCreateBuffer(context, CL_MEM_READ_ONLY,  bytes, NULL, &err);
    check_status(err, "clCreateBuffer(B)");
    cl_mem dC = clCreateBuffer(context, CL_MEM_WRITE_ONLY, bytes, NULL, &err);
    check_status(err, "clCreateBuffer(C)");

    check_status(clEnqueueWriteBuffer(queue, dA, CL_TRUE, 0, bytes, hA, 0, NULL, NULL),
                 "clEnqueueWriteBuffer(A)");
    check_status(clEnqueueWriteBuffer(queue, dB, CL_TRUE, 0, bytes, hB, 0, NULL, NULL),
                 "clEnqueueWriteBuffer(B)");

    // --- 5. Программа и ядро ---
    const char *src = kernelSource;
    size_t srclen = strlen(kernelSource);
    cl_program program = clCreateProgramWithSource(context, 1, &src, &srclen, &err);
    check_status(err, "clCreateProgramWithSource");

    err = clBuildProgram(program, 1, &device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        size_t log_size = 0;
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
        char *log = (char *)malloc(log_size);
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, log_size, log, NULL);
        fprintf(stderr, "Build log:\n%s\n", log);
        free(log);
        check_status(err, "clBuildProgram");
    }

    cl_kernel kernel = clCreateKernel(program, "vec_mul", &err);
    check_status(err, "clCreateKernel");

    // --- 6. Установка аргументов ядра ---
    check_status(clSetKernelArg(kernel, 0, sizeof(cl_mem), &dA),
                 "clSetKernelArg(0)");
    check_status(clSetKernelArg(kernel, 1, sizeof(cl_mem), &dB),
                 "clSetKernelArg(1)");
    check_status(clSetKernelArg(kernel, 2, sizeof(cl_mem), &dC),
                 "clSetKernelArg(2)");
    check_status(clSetKernelArg(kernel, 3, sizeof(unsigned int), &N),
                 "clSetKernelArg(3)");

    // --- 7. Запуск ядра многократно и измерение времени ---
    size_t global = ((N + 255) / 256) * 256; // округление до кратного 256
    size_t local  = 256;

    printf("\nRunning %d GPU passes...\n", RUNS);

    double total_ms = 0.0;
    for (int r = 0; r < RUNS; r++) {
        cl_event evt;
        err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL,
                                     &global, &local, 0, NULL, &evt);
        check_status(err, "clEnqueueNDRangeKernel");

        clFinish(queue);

        cl_ulong tstart = 0, tend = 0;
        clGetEventProfilingInfo(evt, CL_PROFILING_COMMAND_START,
                                sizeof(tstart), &tstart, NULL);
        clGetEventProfilingInfo(evt, CL_PROFILING_COMMAND_END,
                                sizeof(tend), &tend, NULL);
        double ms = (tend - tstart) * 1e-6; // нс -> мс
        total_ms += ms;
        printf("Run %2d: %.3f ms\n", r + 1, ms);
        clReleaseEvent(evt);
    }

    double avg_ms = total_ms / RUNS;
    double gflops = (double)N / 1e9 / (avg_ms / 1000.0); // 1 умножение = 1 FLOP

    printf("\nAverage GPU time: %.3f ms\n", avg_ms);
    printf("Throughput:       %.3f GFLOPS\n", gflops);

    // --- 8. Чтение результата и быстрая проверка корректности ---
    check_status(clEnqueueReadBuffer(queue, dC, CL_TRUE, 0, bytes, hC, 0, NULL, NULL),
                 "clEnqueueReadBuffer(C)");

    printf("\nChecking correctness on first 1M elements...\n");
    size_t checkN = (N < 1024 * 1024) ? N : 1024 * 1024;
    float max_err = 0.0f;

    for (size_t i = 0; i < checkN; i++) {
        float ref = hA[i] * hB[i]; // лёгкий CPU-расчёт только для проверки
        float errv = fabsf(hC[i] - ref);
        if (errv > max_err) max_err = errv;
    }

    printf("Max abs error on %zu elems: %e\n", checkN, max_err);

    // --- 9. Очистка ---
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseMemObject(dA);
    clReleaseMemObject(dB);
    clReleaseMemObject(dC);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);

    free(hA);
    free(hB);
    free(hC);

    printf("\nTest finished.\n");
    return 0;
}