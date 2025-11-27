#include <CL/cl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Простое ядро для умножения векторов
const char *kernel_source = 
"__kernel void vector_mult(__global const float *a, __global const float *b, __global float *result) {\n"
"    int gid = get_global_id(0);\n"
"    result[gid] = a[gid] * b[gid];\n"
"}\n";

double get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char *argv[]) {
    cl_platform_id platforms[10];
    cl_device_id device;
    cl_uint num_platforms;
    cl_int err;
    
    // Получить платформы
    clGetPlatformIDs(10, platforms, &num_platforms);
    
    printf("=== Выбор платформы OpenCL ===\n");
    for (cl_uint i = 0; i < num_platforms; i++) {
        char name[256];
        clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(name), name, NULL);
        printf("%u: %s\n", i, name);
    }
    
    // Выбор платформы
    int platform_idx = 0;
    if (argc > 1) {
        platform_idx = atoi(argv[1]);
    }
    
    if (platform_idx >= num_platforms) {
        printf("Ошибка: неверный индекс платформы %d (всего %u)\n", platform_idx, num_platforms);
        return 1;
    }

    cl_platform_id selected_platform = platforms[platform_idx];
    char selected_name[256];
    clGetPlatformInfo(selected_platform, CL_PLATFORM_NAME, sizeof(selected_name), selected_name, NULL);
    printf("\n✓ Выбрана платформа [%d]: %s\n", platform_idx, selected_name);
    
    // Получить GPU
    err = clGetDeviceIDs(selected_platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);
    if (err != CL_SUCCESS) {
        printf("Ошибка: GPU не найден (код ошибки: %d)\n", err);
        printf("Попытка найти любое устройство (CL_DEVICE_TYPE_ALL)...\n");
        err = clGetDeviceIDs(selected_platform, CL_DEVICE_TYPE_ALL, 1, &device, NULL);
        if (err != CL_SUCCESS) {
             printf("Ошибка: Устройства не найдены даже с CL_DEVICE_TYPE_ALL (код: %d)\n", err);
             return 1;
        }
        printf("✓ Найдено альтернативное устройство\n");
    }
    
    char device_name[256];
    cl_uint compute_units;
    cl_device_type dev_type;
    
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(device_name), device_name, NULL);
    clGetDeviceInfo(device, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(compute_units), &compute_units, NULL);
    clGetDeviceInfo(device, CL_DEVICE_TYPE, sizeof(dev_type), &dev_type, NULL);
    
    printf("\n=== Устройство ===\n");
    printf("Название: %s\n", device_name);
    printf("Compute Units: %u\n", compute_units);
    printf("Тип: %s\n", (dev_type & CL_DEVICE_TYPE_GPU) ? "GPU" : (dev_type & CL_DEVICE_TYPE_CPU) ? "CPU" : "OTHER");
    
    // Создать контекст и очередь
    cl_context context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    if (err != CL_SUCCESS) {
        printf("Ошибка создания контекста: %d\n", err);
        return 1;
    }
    
    cl_command_queue queue = clCreateCommandQueue(context, device, 0, &err);
    if (err != CL_SUCCESS) {
        printf("Ошибка создания очереди: %d\n", err);
        return 1;
    }
    
    // Создать программу
    cl_program program = clCreateProgramWithSource(context, 1, &kernel_source, NULL, &err);
    if (err != CL_SUCCESS) {
        printf("Ошибка создания программы: %d\n", err);
        return 1;
    }
    
    // Скомпилировать
    printf("\n=== Компиляция OpenCL ядра ===\n");
    err = clBuildProgram(program, 1, &device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        printf("Ошибка компиляции: %d\n", err);
        char build_log[4096];
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, sizeof(build_log), build_log, NULL);
        printf("Build log:\n%s\n", build_log);
        return 1;
    }
    printf("✓ Ядро скомпилировано успешно\n");
    
    // Создать ядро
    cl_kernel kernel = clCreateKernel(program, "vector_mult", &err);
    if (err != CL_SUCCESS) {
        printf("Ошибка создания ядра: %d\n", err);
        return 1;
    }
    
    // Тест производительности
    printf("\n=== Тест производительности ===\n");
    const size_t N = 10000000; // 10 миллионов элементов
    size_t bytes = N * sizeof(float);
    
    float *a = (float*)malloc(bytes);
    float *b = (float*)malloc(bytes);
    float *result = (float*)malloc(bytes);
    
    // Инициализация
    for (size_t i = 0; i < N; i++) {
        a[i] = (float)i;
        b[i] = 2.0f;
    }
    
    // Создать буферы
    cl_mem buf_a = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, bytes, a, &err);
    cl_mem buf_b = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, bytes, b, &err);
    cl_mem buf_result = clCreateBuffer(context, CL_MEM_WRITE_ONLY, bytes, NULL, &err);
    
    // Установить аргументы
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &buf_a);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &buf_b);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &buf_result);
    
    // Запуск на GPU
    printf("Запуск %zu элементов на %s...\n", N, device_name);
    double start = get_time();
    
    err = clEnqueueNDRangeKernel(queue,  kernel, 1, NULL, &N, NULL, 0, NULL, NULL);
    if (err != CL_SUCCESS) {
        printf("Ошибка запуска ядра: %d\n", err);
        return 1;
    }
    
    clFinish(queue);
    double end = get_time();
    
    // Получить результат
    clEnqueueReadBuffer(queue, buf_result, CL_TRUE, 0, bytes, result, 0, NULL, NULL);
    
    double gpu_time = end - start;
    double gflops = (N / gpu_time) / 1e9;
    
    printf("\n=== Результаты ===\n");
    printf("Время выполнения: %.4f сек\n", gpu_time);
    printf("Производительность: %.2f GFLOPS\n", gflops);
    printf("Пропускная способность: %.2f GB/s\n", (bytes * 3 / gpu_time) / 1e9);
    
    // Проверка корректности
    int errors = 0;
    for (size_t i = 0; i < 100 && errors < 5; i++) {
        float expected = a[i] * b[i];
        if (result[i] != expected) {
            printf("Ошибка at %zu: expected %.2f, got %.2f\n", i, expected, result[i]);
            errors++;
        }
    }
    
    if (errors == 0) {
        printf("✓ Все проверки пройдены!\n");
    }
    
    // Очистка
    free(a);
    free(b);
    free(result);
    clReleaseMemObject(buf_a);
    clReleaseMemObject(buf_b);
    clReleaseMemObject(buf_result);
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);
    
    return 0;
}
