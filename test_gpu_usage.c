#include <stdio.h>
#include <stdlib.h>
#include <CL/cl.h>

const char *kernel_source =
"__kernel void simple_test(__global float* data) {\n"
"    int i = get_global_id(0);\n"
"    data[i] = data[i] * 2.0f;\n"
"}\n";

int main() {
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  ТЕСТ: OpenCL использует GPU после исправления local_mem_size\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    cl_platform_id platform;
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;
    cl_int err;
    
    // Получить Clover платформу (работает стабильно)
    cl_uint num_platforms;
    clGetPlatformIDs(0, NULL, &num_platforms);
    cl_platform_id *platforms = malloc(sizeof(cl_platform_id) * num_platforms);
    clGetPlatformIDs(num_platforms, platforms, NULL);
    
    platform = NULL;
    for (cl_uint i = 0; i < num_platforms; i++) {
        char name[256];
        clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(name), name, NULL);
        if (strstr(name, "Clover")) {
            platform = platforms[i];
            printf("✅ Используется платформа: %s\n", name);
            break;
        }
    }
    
    if (!platform) {
        fprintf(stderr, "❌ Clover платформа не найдена\n");
        return 1;
    }
    
    // Получить GPU устройство
    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "❌ GPU устройство не найдено\n");
        return 1;
    }
    
    char dev_name[256];
    cl_uint compute_units;
    cl_ulong global_mem, local_mem;
    
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(dev_name), dev_name, NULL);
    clGetDeviceInfo(device, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(compute_units), &compute_units, NULL);
    clGetDeviceInfo(device, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(global_mem), &global_mem, NULL);
    clGetDeviceInfo(device, CL_DEVICE_LOCAL_MEM_SIZE, sizeof(local_mem), &local_mem, NULL);
    
    printf("✅ Устройство: %s\n", dev_name);
    printf("   Compute Units: %u\n", compute_units);
    printf("   Global Memory: %lu MB\n", global_mem / (1024*1024));
    printf("   Local Memory:  %lu KB\n\n", local_mem / 1024);
    
    // Создать контекст и очередь
    context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "❌ Ошибка создания контекста: %d\n", err);
        return 1;
    }
    
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    queue = clCreateCommandQueue(context, device, 0, &err);
    #pragma GCC diagnostic pop
    
    if (err != CL_SUCCESS) {
        fprintf(stderr, "❌ Ошибка создания очереди: %d\n", err);
        return 1;
    }
    
    printf("✅ OpenCL контекст и очередь созданы\n\n");
    
    // Простой тест: создать буфер и запустить kernel
    #define N 1000000
    size_t bytes = N * sizeof(float);
    float *host_data = (float*)malloc(bytes);
    
    for (int i = 0; i < N; i++) {
        host_data[i] = (float)i;
    }
    
    cl_mem device_data = clCreateBuffer(context, CL_MEM_READ_WRITE, bytes, NULL, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "❌ Ошибка создания буфера: %d\n", err);
        return 1;
    }
    
    // Копировать на GPU
    clEnqueueWriteBuffer(queue, device_data, CL_TRUE, 0, bytes, host_data, 0, NULL, NULL);
    
    // Компилировать kernel
    cl_program program = clCreateProgramWithSource(context, 1, &kernel_source, NULL, &err);
    err = clBuildProgram(program, 1, &device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "❌ Ошибка компиляции kernel\n");
        return 1;
    }
    
    cl_kernel kernel = clCreateKernel(program, "simple_test", &err);
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &device_data);
    
    printf("Запуск kernel на GPU...\n");
    printf("\n⏱️  ВАЖНО: Запустите в отдельных терминалах:\n");
    printf("   Терминал 1: radeontop    (должна быть активность GPU)\n");
    printf("   Терминал 2: htop         (CPU НЕ должен быть на 100%%)\n\n");
    printf("Выполнение 10 итераций...\n");
    
    for (int iter = 0; iter < 10; iter++) {
        size_t global_size = N;
        err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global_size, NULL, 0, NULL, NULL);
        if (err != CL_SUCCESS) {
            fprintf(stderr, "❌ Ошибка запуска kernel: %d\n", err);
            return 1;
        }
        clFinish(queue);
        printf("  Итерация %d/10 завершена\n", iter + 1);
        usleep(500000); // 0.5 сек задержка для наблюдения в radeontop
    }
    
    // Копировать обратно
    clEnqueueReadBuffer(queue, device_data, CL_TRUE, 0, bytes, host_data, 0, NULL, NULL);
    
    // Проверить результат
    int errors = 0;
    for (int i = 0; i < 100; i++) {
        float expected = (float)i * 1024.0f; // 2^10 = 1024
        if (host_data[i] != expected) {
            errors++;
        }
    }
    
    printf("\n");
    if (errors == 0) {
        printf("✅ УСПЕХ! Все вычисления корректны\n");
        printf("✅ OpenCL использует GPU!\n");
    } else {
        printf("⚠️  Найдено %d ошибок в вычислениях\n", errors);
    }
    
    // Очистка
    clReleaseMemObject(device_data);
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);
    free(host_data);
    free(platforms);
    
    printf("\n═══════════════════════════════════════════════════════════════\n");
    return 0;
}
