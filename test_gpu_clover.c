#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <CL/cl.h>
#include <time.h>

#define VECTOR_SIZE (32 * 1024 * 1024)  // 32M floats = 128MB
#define BENCHMARK_ITERATIONS 1000

// Загрузить kernel из файла
char* load_kernel_source(const char *filename, size_t *size) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("Ошибка открытия kernel файла");
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *source = malloc(*size + 1);
    fread(source, 1, *size, f);
    source[*size] = '\0';
    fclose(f);
    
    return source;
}

int main() {
    cl_platform_id platforms[10];
    cl_uint num_platforms;
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel kernel;
    cl_int err;
    
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║   GPU Kernel Test - Mesa Clover (Gladius gfx701)          ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    // 1. Получить платформы
    err = clGetPlatformIDs(10, platforms, &num_platforms);
    if (err != CL_SUCCESS) {
        printf("Ошибка получения платформ: %d\n", err);
        return 1;
    }
    
    printf("Найдено платформ: %u\n", num_platforms);
    for (cl_uint i = 0; i < num_platforms; i++) {
        char name[256];
        clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(name), name, NULL);
        printf("  [%u] %s\n", i, name);
    }
    
    // Выбрать Clover (обычно платформа 1)
    cl_uint platform_idx = (num_platforms > 1) ? 1 : 0;
    cl_platform_id platform = platforms[platform_idx];
    
    char platform_name[256];
    clGetPlatformInfo(platform, CL_PLATFORM_NAME, sizeof(platform_name), platform_name, NULL);
    printf("\nВыбрана платформа: %s\n", platform_name);
    
    // 2. Получить GPU device
    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);
    if (err != CL_SUCCESS) {
        printf("GPU не найден, пробую CL_DEVICE_TYPE_ALL...\n");
        err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 1, &device, NULL);
        if (err != CL_SUCCESS) {
            printf("Ошибка получения устройства: %d\n", err);
            return 1;
        }
    }
    
    char device_name[256];
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(device_name), device_name, NULL);
    printf("Устройство: %s\n", device_name);
    
    cl_uint compute_units;
    clGetDeviceInfo(device, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(compute_units), &compute_units, NULL);
    printf("Compute Units: %u\n", compute_units);
    
    // 3. Создать контекст
    context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    if (err != CL_SUCCESS) {
        printf("Ошибка создания контекста: %d\n", err);
        return 1;
    }
    printf("✓ Контекст создан\n");
    
    // 4. Создать command queue
    queue = clCreateCommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &err);
    if (err != CL_SUCCESS) {
        printf("Ошибка создания queue: %d\n", err);
        return 1;
    }
    printf("✓ Command queue создан\n\n");
    
    // 5. Загрузить kernel source
    size_t source_size;
    char *kernel_source = load_kernel_source("gpu_kernels.cl", &source_size);
    if (!kernel_source) {
        return 1;
    }
    printf("Загружен kernel source: %zu байт\n", source_size);
    
    // 6. Создать и скомпилировать программу
    program = clCreateProgramWithSource(context, 1, (const char**)&kernel_source, &source_size, &err);
    if (err != CL_SUCCESS) {
        printf("Ошибка создания программы: %d\n", err);
        return 1;
    }
    
    printf("Компиляция kernel для gfx701...\n");
    err = clBuildProgram(program, 1, &device, "-cl-std=CL1.2", NULL, NULL);
    if (err != CL_SUCCESS) {
        printf("Ошибка компиляции: %d\n", err);
        
        size_t log_size;
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
        char *log = malloc(log_size);
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, log_size, log, NULL);
        printf("Build log:\n%s\n", log);
        free(log);
        return 1;
    }
    printf("✓ Kernel скомпилирован\n\n");
    
    // 7. Создать kernel
    kernel = clCreateKernel(program, "fma_benchmark", &err);
    if (err != CL_SUCCESS) {
        printf("Ошибка создания kernel: %d\n", err);
        return 1;
    }
    printf("✓ Kernel 'fma_benchmark' создан\n");
    
    // 8. Выделить память
    printf("\nВыделение памяти: %d MB × 3 буфера...\n", 
           (int)(VECTOR_SIZE * sizeof(float) / (1024 * 1024)));
    
    cl_mem buf_a = clCreateBuffer(context, CL_MEM_READ_ONLY, 
                                   VECTOR_SIZE * sizeof(float), NULL, &err);
    cl_mem buf_b = clCreateBuffer(context, CL_MEM_READ_ONLY, 
                                   VECTOR_SIZE * sizeof(float), NULL, &err);
    cl_mem buf_c = clCreateBuffer(context, CL_MEM_READ_WRITE, 
                                   VECTOR_SIZE * sizeof(float), NULL, &err);
    
    if (!buf_a || !buf_b || !buf_c) {
        printf("Ошибка выделения GPU памяти\n");
        return 1;
    }
    printf("✓ Выделено GPU памяти\n");
    
    // 9. Инициализировать данные
    float *host_a = malloc(VECTOR_SIZE * sizeof(float));
    float *host_b = malloc(VECTOR_SIZE * sizeof(float));
    float *host_c = malloc(VECTOR_SIZE * sizeof(float));
    
    for (int i = 0; i < VECTOR_SIZE; i++) {
        host_a[i] = 1.0f + (float)i / 1000000.0f;
        host_b[i] = 2.0f + (float)i / 2000000.0f;
        host_c[i] = 0.0f;
    }
    
    // Копировать на GPU
    clEnqueueWriteBuffer(queue, buf_a, CL_TRUE, 0, VECTOR_SIZE * sizeof(float), host_a, 0, NULL, NULL);
    clEnqueueWriteBuffer(queue, buf_b, CL_TRUE, 0, VECTOR_SIZE * sizeof(float), host_b, 0, NULL, NULL);
    clEnqueueWriteBuffer(queue, buf_c, CL_TRUE, 0, VECTOR_SIZE * sizeof(float), host_c, 0, NULL, NULL);
    printf("✓ Данные скопированы на GPU\n\n");
    
    // 10. Установить аргументы kernel
    unsigned int n = VECTOR_SIZE;
    int iterations = BENCHMARK_ITERATIONS;
    
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &buf_a);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &buf_b);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &buf_c);
    clSetKernelArg(kernel, 3, sizeof(unsigned int), &n);
    clSetKernelArg(kernel, 4, sizeof(int), &iterations);
    
    // 11. Запустить kernel
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  Запуск GPU Kernel\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  • Элементов: %u (%.1f MB)\n", n, (n * sizeof(float)) / (1024.0 * 1024.0));
    printf("  • Итераций FMA: %d\n", iterations);
    printf("  • Операций на элемент: %d (4 FMA × %d iterations)\n", iterations * 4 * 2, iterations);
    printf("  • Всего FLOP: %.2e\n\n", (double)n * iterations * 4 * 2);
    
    size_t global_work_size = VECTOR_SIZE;
    size_t local_work_size = 256;  // Workgroup size
    
    cl_event event;
    struct timespec start, end;
    
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, 
                                 &global_work_size, &local_work_size, 
                                 0, NULL, &event);
    if (err != CL_SUCCESS) {
        printf("Ошибка запуска kernel: %d\n", err);
        return 1;
    }
    
    // Ждать завершения
    clWaitForEvents(1, &event);
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    
    // Получить профилирование
    cl_ulong time_start, time_end;
    clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_START, sizeof(time_start), &time_start, NULL);
    clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END, sizeof(time_end), &time_end, NULL);
    double gpu_time = (time_end - time_start) / 1e9;
    
    printf("✓ Kernel выполнен!\n\n");
    
    // 12. Результаты
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                    РЕЗУЛЬТАТЫ                              ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    double total_flops = (double)n * iterations * 4 * 2;
    double gflops = total_flops / gpu_time / 1e9;
    double tflops = gflops / 1000.0;
    
    printf("Время выполнения:\n");
    printf("  • Wall time: %.3f сек\n", elapsed);
    printf("  • GPU time:  %.3f сек\n", gpu_time);
    printf("\n");
    printf("Производительность:\n");
    printf("  • GFLOPS: %.2f\n", gflops);
    printf("  • TFLOPS: %.3f\n", tflops);
    printf("  • Процент от пика (4.2 TFLOPS): %.1f%%\n", (tflops / 4.2) * 100.0);
    printf("\n");
    
    // Проверить результаты
    clEnqueueReadBuffer(queue, buf_c, CL_TRUE, 0, VECTOR_SIZE * sizeof(float), host_c, 0, NULL, NULL);
    
    printf("Проверка результатов (первые 10 элементов):\n");
    for (int i = 0; i < 10; i++) {
        printf("  c[%d] = %.6f\n", i, host_c[i]);
    }
    
    // Cleanup
    clReleaseMemObject(buf_a);
    clReleaseMemObject(buf_b);
    clReleaseMemObject(buf_c);
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);
    free(host_a);
    free(host_b);
    free(host_c);
    free(kernel_source);
    
    printf("\n✓ Тест завершен\n");
    
    return 0;
}
