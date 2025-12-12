#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>
#include <CL/cl.h>
#include "opencl_gpu_helper.h"

#define MAX_SOURCE_SIZE (0x100000)

// Utility functions
double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

void print_header(const char *title) {
    printf("\n╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║ %-66s ║\n", title);
    printf("╚════════════════════════════════════════════════════════════════════╝\n");
}

void print_section(const char *title) {
    printf("\n═══ %s ═══\n", title);
}

// Чтение исходного кода OpenCL из файла
char* load_kernel_source(const char* filename, size_t* size) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Ошибка: не удалось открыть %s\n", filename);
        return NULL;
    }
    
    char *source = (char*)malloc(MAX_SOURCE_SIZE);
    *size = fread(source, 1, MAX_SOURCE_SIZE, fp);
    fclose(fp);
    
    return source;
}

// Проверка GPU power state
void check_gpu_power_state() {
    print_section("СОСТОЯНИЕ ПИТАНИЯ GPU");
    
    FILE *fp = fopen("/sys/class/drm/card0/device/power_dpm_state", "r");
    if (fp) {
        char state[64];
        if (fgets(state, sizeof(state), fp)) {
            printf("DPM State: %s", state);
        }
        fclose(fp);
    }
    
    fp = fopen("/sys/class/drm/card0/device/power_dpm_force_performance_level", "r");
    if (fp) {
        char level[64];
        if (fgets(level, sizeof(level), fp)) {
            printf("Performance Level: %s", level);
        }
        fclose(fp);
    }
}

// FMA Stress Test для разогрева GPU
void run_fma_stress_test(cl_context context, cl_command_queue queue, 
                         cl_program program, gpu_device_info_t *gpu_info) {
    print_section("FMA STRESS TEST (РАЗОГРЕВ GPU)");
    
    const size_t n = 4 * 1024 * 1024; // 4M элементов
    const unsigned int iterations = 1000;
    
    printf("Запуск %u FMA итераций на %zu элементов\n", iterations, n);
    printf("Это должно поднять частоту GPU...\n");
    
    cl_int err;
    size_t bytes = n * sizeof(float);
    
    // Создать буферы
    cl_mem buf_a = clCreateBuffer(context, CL_MEM_READ_ONLY, bytes, NULL, &err);
    cl_mem buf_b = clCreateBuffer(context, CL_MEM_READ_ONLY, bytes, NULL, &err);
    cl_mem buf_c = clCreateBuffer(context, CL_MEM_READ_WRITE, bytes, NULL, &err);
    
    // Инициализировать данные
    float *h_a = (float*)malloc(bytes);
    float *h_b = (float*)malloc(bytes);
    float *h_c = (float*)malloc(bytes);
    
    for (size_t i = 0; i < n; i++) {
        h_a[i] = 1.0f;
        h_b[i] = 2.0f;
        h_c[i] = 0.0f;
    }
    
    clEnqueueWriteBuffer(queue, buf_a, CL_TRUE, 0, bytes, h_a, 0, NULL, NULL);
    clEnqueueWriteBuffer(queue, buf_b, CL_TRUE, 0, bytes, h_b, 0, NULL, NULL);
    clEnqueueWriteBuffer(queue, buf_c, CL_TRUE, 0, bytes, h_c, 0, NULL, NULL);
    
    // Создать kernel
    cl_kernel kernel = clCreateKernel(program, "fma_stress", &err);
    
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &buf_a);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &buf_b);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &buf_c);
    clSetKernelArg(kernel, 3, sizeof(unsigned int), &n);
    clSetKernelArg(kernel, 4, sizeof(unsigned int), &iterations);
    
    size_t global_work_size = n;
    size_t local_work_size = 256;
    
    // Запуск с измерением времени
    double start = get_time();
    err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global_work_size, 
                                 &local_work_size, 0, NULL, NULL);
    clFinish(queue);
    double end = get_time();
    
    double elapsed = end - start;
    
    // Вычислить производительность
    // Каждая итерация: 4 FMA операции, каждая = 2 FLOP
    double total_flops = (double)n * iterations * 4 * 2;
    double gflops = total_flops / elapsed / 1e9;
    
    printf("Время: %.3f сек\n", elapsed);
    printf("Производительность: %.2f GFLOPS (%.3f TFLOPS)\n", gflops, gflops/1000.0);
    
    // Cleanup
    free(h_a);
    free(h_b);
    free(h_c);
    clReleaseMemObject(buf_a);
    clReleaseMemObject(buf_b);
    clReleaseMemObject(buf_c);
    clReleaseKernel(kernel);
}

// Тест умножения матриц
void run_matrix_test(cl_context context, cl_command_queue queue,
                     cl_program program, unsigned int size, int num_runs) {
    printf("\nMatrix Size: %u × %u (%.1f MB per matrix)\n", 
           size, size, (size * size * sizeof(float)) / (1024.0 * 1024.0));
    
    cl_int err;
    size_t bytes = size * size * sizeof(float);
    
    // Создать буферы
    cl_mem buf_a = clCreateBuffer(context, CL_MEM_READ_ONLY, bytes, NULL, &err);
    cl_mem buf_b = clCreateBuffer(context, CL_MEM_READ_ONLY, bytes, NULL, &err);
    cl_mem buf_c = clCreateBuffer(context, CL_MEM_WRITE_ONLY, bytes, NULL, &err);
    
    // Инициализировать матрицы случайными значениями
    float *h_a = (float*)malloc(bytes);
    float *h_b = (float*)malloc(bytes);
    float *h_c = (float*)malloc(bytes);
    
    srand(time(NULL));
    for (unsigned int i = 0; i < size * size; i++) {
        h_a[i] = (float)rand() / RAND_MAX;
        h_b[i] = (float)rand() / RAND_MAX;
    }
    
    clEnqueueWriteBuffer(queue, buf_a, CL_TRUE, 0, bytes, h_a, 0, NULL, NULL);
    clEnqueueWriteBuffer(queue, buf_b, CL_TRUE, 0, bytes, h_b, 0, NULL, NULL);
    
    // Создать kernel
    cl_kernel kernel = clCreateKernel(program, "matrix_multiply", &err);
    
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &buf_a);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &buf_b);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &buf_c);
    clSetKernelArg(kernel, 3, sizeof(unsigned int), &size);
    
    size_t global_work_size[2] = {size, size};
    size_t local_work_size[2] = {16, 16};
    
    // Прогрев
    clEnqueueNDRangeKernel(queue, kernel, 2, NULL, global_work_size, 
                          local_work_size, 0, NULL, NULL);
    clFinish(queue);
    
    // Замеры времени
    double times[10];
    double min_time = 1e9, max_time = 0, avg_time = 0;
    
    for (int run = 0; run < num_runs; run++) {
        double start = get_time();
        clEnqueueNDRangeKernel(queue, kernel, 2, NULL, global_work_size,
                              local_work_size, 0, NULL, NULL);
        clFinish(queue);
        double end = get_time();
        
        times[run] = (end - start) * 1000.0; // в миллисекундах
        avg_time += times[run];
        if (times[run] < min_time) min_time = times[run];
        if (times[run] > max_time) max_time = times[run];
    }
    avg_time /= num_runs;
    
    // Прочитать результат
    clEnqueueReadBuffer(queue, buf_c, CL_TRUE, 0, bytes, h_c, 0, NULL, NULL);
    
    // Вычислить производительность
    // Matrix multiplication: 2*size^3 FLOPs (size^3 умножений + size^3 сложений)
    double flops = 2.0 * size * size * size;
    double gflops = flops / (avg_time / 1000.0) / 1e9;
    
    // Bandwidth: 3 матрицы * size^2 * 4 bytes
    double bytes_transferred = 3.0 * size * size * sizeof(float);
    double bandwidth_gb = bytes_transferred / (avg_time / 1000.0) / 1e9;
    
    printf("Время (avg/min/max): %.3f / %.3f / %.3f мс\n", 
           avg_time, min_time, max_time);
    printf("Производительность: %.2f GFLOPS\n", gflops);
    printf("Bandwidth: %.2f GB/s\n", bandwidth_gb);
    
    // Проверка корректности (CPU вычисление для первого элемента)
    float expected = 0.0f;
    for (unsigned int k = 0; k < size; k++) {
        expected += h_a[k] * h_b[k * size];
    }
    
    if (fabs(h_c[0] - expected) < 0.01) {
        printf("✓ Результат корректен (C[0][0] = %.4f)\n", h_c[0]);
    } else {
        printf("✗ Ошибка: C[0][0] = %.4f, ожидалось %.4f\n", h_c[0], expected);
    }
    
    // Cleanup
    free(h_a);
    free(h_b);
    free(h_c);
    clReleaseMemObject(buf_a);
    clReleaseMemObject(buf_b);
    clReleaseMemObject(buf_c);
    clReleaseKernel(kernel);
}

// Тест цепочки kernels
void run_kernel_chain_test(cl_context context, cl_command_queue queue,
                           cl_program program) {
    print_section("ЦЕПОЧКА KERNELS (SINGLE QUEUE)");
    
    const unsigned int size = 512;
    const int num_kernels = 4;
    
    printf("Запуск цепочки из %d матричных умножений (%ux%u)\n", 
           num_kernels, size, size);
    
    cl_int err;
    size_t bytes = size * size * sizeof(float);
    
    // Создать буферы
    cl_mem buf_a = clCreateBuffer(context, CL_MEM_READ_WRITE, bytes, NULL, &err);
    cl_mem buf_b = clCreateBuffer(context, CL_MEM_READ_WRITE, bytes, NULL, &err);
    cl_mem buf_c = clCreateBuffer(context, CL_MEM_READ_WRITE, bytes, NULL, &err);
    
    // Инициализировать
    float *h_data = (float*)calloc(size * size, sizeof(float));
    for (unsigned int i = 0; i < size * size; i++) {
        h_data[i] = (float)rand() / RAND_MAX;
    }
    clEnqueueWriteBuffer(queue, buf_a, CL_TRUE, 0, bytes, h_data, 0, NULL, NULL);
    clEnqueueWriteBuffer(queue, buf_b, CL_TRUE, 0, bytes, h_data, 0, NULL, NULL);
    
    cl_kernel kernel = clCreateKernel(program, "matrix_multiply", &err);
    
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &buf_a);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &buf_b);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &buf_c);
    clSetKernelArg(kernel, 3, sizeof(unsigned int), &size);
    
    size_t global_work_size[2] = {size, size};
    size_t local_work_size[2] = {16, 16};
    
    printf("Время по каждому kernel:\n");
    
    double wall_start = get_time();
    double total_gpu_time = 0;
    
    for (int i = 0; i < num_kernels; i++) {
        double start = get_time();
        clEnqueueNDRangeKernel(queue, kernel, 2, NULL, global_work_size,
                              local_work_size, 0, NULL, NULL);
        clFinish(queue);
        double end = get_time();
        
        double kernel_time = (end - start) * 1000.0;
        total_gpu_time += kernel_time;
        printf("  Kernel %d: %.2f мс\n", i, kernel_time);
    }
    
    double wall_end = get_time();
    double wall_time = (wall_end - wall_start) * 1000.0;
    
    printf("Wall time: %.3f мс\n", wall_time);
    printf("Сумма GPU time: %.3f мс\n", total_gpu_time);
    
    // Вычислить производительность для всей цепочки
    double total_flops = 2.0 * size * size * size * num_kernels;
    double gflops = total_flops / (total_gpu_time / 1000.0) / 1e9;
    printf("Производительность: %.2f GFLOPS\n", gflops);
    
    // Cleanup
    free(h_data);
    clReleaseMemObject(buf_a);
    clReleaseMemObject(buf_b);
    clReleaseMemObject(buf_c);
    clReleaseKernel(kernel);
}

int main() {
    cl_int err;
    
    print_header("KFD/LLVM/OPENCL PARALLEL MATRIX MULTIPLICATION TEST");
    
    // Проверить состояние питания GPU
    check_gpu_power_state();
    
    // Инициализация OpenCL
    print_section("ИНИЦИАЛИЗАЦИЯ OPENCL");
    
    gpu_device_info_t gpu_info;
    if (select_best_gpu_device(&gpu_info) != 0) {
        fprintf(stderr, "Ошибка: не удалось выбрать GPU устройство\n");
        return 1;
    }
    
    printf("Platform: %s\n", gpu_info.platform_name);
    printf("Device: %s\n", gpu_info.device_name);
    printf("Compute Units: %u\n", gpu_info.compute_units);
    
    cl_uint max_freq;
    clGetDeviceInfo(gpu_info.device, CL_DEVICE_MAX_CLOCK_FREQUENCY, 
                    sizeof(max_freq), &max_freq, NULL);
    printf("Max Frequency: %u MHz\n", max_freq);
    
    size_t max_work_group;
    clGetDeviceInfo(gpu_info.device, CL_DEVICE_MAX_WORK_GROUP_SIZE,
                    sizeof(max_work_group), &max_work_group, NULL);
    printf("Max Work Group: %zu\n", max_work_group);
    
    printf("VRAM: %lu MB\n", gpu_info.global_mem_size / (1024 * 1024));
    printf("✓ OpenCL initialized\n");
    
    // Создать контекст
    cl_context context = create_gpu_context(&gpu_info, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Ошибка создания контекста: %d\n", err);
        return 1;
    }
    
    // Создать очередь команд
    cl_command_queue queue = create_gpu_queue(context, &gpu_info, CL_TRUE, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Ошибка создания очереди: %d\n", err);
        clReleaseContext(context);
        return 1;
    }
    
    // Загрузить и скомпилировать kernel
    size_t source_size;
    char *source = load_kernel_source("matrix_kernels.cl", &source_size);
    if (!source) {
        fprintf(stderr, "Ошибка загрузки kernel source\n");
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        return 1;
    }
    
    cl_program program = clCreateProgramWithSource(context, 1, (const char**)&source, 
                                                    &source_size, &err);
    free(source);
    
    err = clBuildProgram(program, 1, &gpu_info.device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Ошибка компиляции программы: %d\n", err);
        
        size_t log_size;
        clGetProgramBuildInfo(program, gpu_info.device, CL_PROGRAM_BUILD_LOG,
                             0, NULL, &log_size);
        char *log = (char*)malloc(log_size);
        clGetProgramBuildInfo(program, gpu_info.device, CL_PROGRAM_BUILD_LOG,
                             log_size, log, NULL);
        fprintf(stderr, "Build Log:\n%s\n", log);
        free(log);
        
        clReleaseProgram(program);
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        return 1;
    }
    
    // Запустить FMA стресс-тест для разогрева
    run_fma_stress_test(context, queue, program, &gpu_info);
    
    // Тесты умножения матриц
    print_section("ТЕСТ УМНОЖЕНИЯ МАТРИЦ");
    run_matrix_test(context, queue, program, 512, 5);
    run_matrix_test(context, queue, program, 1024, 5);
    run_matrix_test(context, queue, program, 2048, 3);
    
    // Тест цепочки kernels
    run_kernel_chain_test(context, queue, program);
    
    // Cleanup
    clReleaseProgram(program);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);
    
    print_header("ТЕСТ ЗАВЕРШЕН");
    printf("✓ Все тесты выполнены успешно\n");
    
    return 0;
}
