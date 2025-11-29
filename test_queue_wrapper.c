#include "opencl_gpu_helper.h"
#include "opencl_queue_wrapper.h"
#include <sys/time.h>
#include <math.h>
#include <unistd.h>

// Простой kernel для демонстрации профилирования
const char *vector_add_kernel =
"__kernel void vector_add(__global const float* A,\n"
"                         __global const float* B,\n"
"                         __global float* C,\n"
"                         const unsigned int N) {\n"
"    int i = get_global_id(0);\n"
"    if (i < N) {\n"
"        C[i] = A[i] + B[i];\n"
"    }\n"
"}\n";

#define ARRAY_SIZE (2 * 1024 * 1024)  // 2M элементов

int main() {
    cl_int err;
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  ДЕМОНСТРАЦИЯ OPENCL QUEUE WRAPPER\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    // Шаг 1: Выбрать GPU устройство
    printf("[1] Выбор GPU устройства...\n");
    gpu_device_info_t gpu_info;
    if (select_best_gpu_device(&gpu_info) != 0) {
        fprintf(stderr, "Ошибка: не удалось выбрать GPU!\n");
        return 1;
    }
    printf("\n");
    print_device_info(&gpu_info);
    printf("\n");
    
    // Шаг 2: Создать контекст
    printf("[2] Создание контекста...\n");
    cl_context context = create_gpu_context(&gpu_info, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Ошибка создания контекста: %d\n", err);
        return 1;
    }
    printf("✓ Контекст создан\n\n");
    
    // Шаг 3: Демонстрация разных вариантов создания очереди
    printf("[3] Создание очередей команд (разные варианты)...\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    
    // Вариант 1: Обычная очередь без свойств
    printf("\nВариант 1: Обычная очередь (без свойств)\n");
    cl_command_queue queue_default = create_default_queue(context, gpu_info.device, &err);
    if (err == CL_SUCCESS) {
        printf("✓ Очередь создана\n");
        print_queue_info(queue_default);
        clReleaseCommandQueue(queue_default);
    } else {
        fprintf(stderr, "✗ Ошибка: %d\n", err);
    }
    
    // Вариант 2: Очередь с профилированием (рекомендуется)
    printf("\nВариант 2: Очередь с профилированием\n");
    cl_command_queue queue_profiling = create_profiling_queue(context, gpu_info.device, &err);
    if (err == CL_SUCCESS) {
        printf("✓ Очередь с профилированием создана\n");
        print_queue_info(queue_profiling);
    } else {
        fprintf(stderr, "✗ Ошибка: %d\n", err);
        return 1;
    }
    
    // Вариант 3: Очередь с пользовательскими настройками
    printf("\nВариант 3: Очередь с настройками (profiling + out-of-order)\n");
    cl_command_queue queue_custom = create_command_queue_simple(
        context, gpu_info.device, 
        CL_TRUE,  // профилирование
        CL_FALSE, // НЕ out-of-order (для стабильности)
        &err
    );
    if (err == CL_SUCCESS) {
        printf("✓ Кастомная очередь создана\n");
        print_queue_info(queue_custom);
        clReleaseCommandQueue(queue_custom);
    } else {
        fprintf(stderr, "✗ Ошибка: %d\n", err);
    }
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    
    // Шаг 4: Использовать очередь с профилированием для реальных вычислений
    printf("\n[4] Запуск тестовых вычислений с профилированием...\n");
    
    // Подготовка данных
    size_t bytes = ARRAY_SIZE * sizeof(float);
    float *hA = (float*)malloc(bytes);
    float *hB = (float*)malloc(bytes);
    float *hC = (float*)malloc(bytes);
    
    for (int i = 0; i < ARRAY_SIZE; i++) {
        hA[i] = (float)i;
        hB[i] = (float)(i * 2);
    }
    
    // Создать буферы
    cl_mem dA = clCreateBuffer(context, CL_MEM_READ_ONLY, bytes, NULL, &err);
    cl_mem dB = clCreateBuffer(context, CL_MEM_READ_ONLY, bytes, NULL, &err);
    cl_mem dC = clCreateBuffer(context, CL_MEM_WRITE_ONLY, bytes, NULL, &err);
    
    // Копировать данные
    clEnqueueWriteBuffer(queue_profiling, dA, CL_TRUE, 0, bytes, hA, 0, NULL, NULL);
    clEnqueueWriteBuffer(queue_profiling, dB, CL_TRUE, 0, bytes, hB, 0, NULL, NULL);
    
    // Компилировать kernel
    cl_program program = clCreateProgramWithSource(context, 1, &vector_add_kernel, NULL, &err);
    err = clBuildProgram(program, 1, &gpu_info.device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        char log[4096];
        clGetProgramBuildInfo(program, gpu_info.device, CL_PROGRAM_BUILD_LOG, sizeof(log), log, NULL);
        fprintf(stderr, "Ошибка компиляции:\n%s\n", log);
        return 1;
    }
    
    cl_kernel kernel = clCreateKernel(program, "vector_add", &err);
    
    // Установить аргументы
    unsigned int n = ARRAY_SIZE;
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &dA);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &dB);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &dC);
    clSetKernelArg(kernel, 3, sizeof(unsigned int), &n);
    
    // Запустить kernel с профилированием
    printf("\nЗапуск kernel на GPU...\n");
    size_t global_size = ARRAY_SIZE;
    size_t local_size = 256;
    
    cl_event event;
    err = clEnqueueNDRangeKernel(
        queue_profiling, kernel, 1, NULL, &global_size, &local_size,
        0, NULL, &event
    );
    
    if (err != CL_SUCCESS) {
        fprintf(stderr, "✗ Ошибка запуска kernel: %d\n", err);
        return 1;
    }
    
    // Дождаться завершения
    clFinish(queue_profiling);
    printf("✓ Kernel выполнен\n\n");
    
    // Показать информацию о профилировании
    print_event_profiling(event);
    
    // Получить только время выполнения
    cl_ulong exec_time_ns;
    if (get_event_execution_time(event, &exec_time_ns) == CL_SUCCESS) {
        double exec_time_ms = exec_time_ns / 1e6;
        double throughput_gflops = (ARRAY_SIZE / 1e9) / (exec_time_ns / 1e9);
        
        printf("\n");
        printf("Производительность:\n");
        printf("  Время выполнения: %.3f мс\n", exec_time_ms);
        printf("  Throughput:       %.3f GFLOPS\n", throughput_gflops);
        printf("  Элементов/сек:    %.2f млн\n", (ARRAY_SIZE / 1e6) / (exec_time_ms / 1000.0));
    }
    
    // Проверить результаты
    clEnqueueReadBuffer(queue_profiling, dC, CL_TRUE, 0, bytes, hC, 0, NULL, NULL);
    
    int errors = 0;
    for (int i = 0; i < 100 && i < ARRAY_SIZE; i++) {
        float expected = hA[i] + hB[i];
        if (fabsf(hC[i] - expected) > 0.001f) {
            errors++;
        }
    }
    
    printf("\nПроверка корректности: ");
    if (errors == 0) {
        printf("✓ Все результаты корректны!\n");
    } else {
        printf("✗ Найдено %d ошибок\n", errors);
    }
    
    // Очистка
    clReleaseEvent(event);
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseMemObject(dA);
    clReleaseMemObject(dB);
    clReleaseMemObject(dC);
    clReleaseCommandQueue(queue_profiling);
    clReleaseContext(context);
    free(hA);
    free(hB);
    free(hC);
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  ДЕМОНСТРАЦИЯ ЗАВЕРШЕНА УСПЕШНО\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    return 0;
}
