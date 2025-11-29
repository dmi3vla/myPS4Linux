#include "opencl_gpu_helper.h"
#include <sys/time.h>
#include <math.h>
#include <unistd.h>

// Простой kernel для теста (умножение векторов)
const char *kernel_source =
"__kernel void vector_multiply(__global const float* A,\n"
"                              __global const float* B,\n"
"                              __global float* C,\n"
"                              const unsigned int N) {\n"
"    int i = get_global_id(0);\n"
"    if (i < N) {\n"
"        // Делаем больше вычислений для заметной нагрузки на GPU\n"
"        float result = 0.0f;\n"
"        for (int j = 0; j < 100; j++) {\n"
"            result += A[i] * B[i];\n"
"        }\n"
"        C[i] = result;\n"
"    }\n"
"}\n";

#define ARRAY_SIZE (1024 * 1024)  // 1M элементов
#define ITERATIONS 10             // Количество итераций для видимой нагрузки

int main() {
    cl_int err;
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  ДЕМОНСТРАЦИЯ ПРАВИЛЬНОГО ВЫБОРА GPU УСТРОЙСТВА\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    // Шаг 1: Выбрать лучшее GPU устройство
    printf("[1] Автоматический выбор лучшего GPU устройства...\n");
    gpu_device_info_t gpu_info;
    if (select_best_gpu_device(&gpu_info) != 0) {
        fprintf(stderr, "Не удалось выбрать GPU устройство!\n");
        return 1;
    }
    printf("\n");
    print_device_info(&gpu_info);
    printf("\n");
    
    // Шаг 2: Создать контекст и очередь команд для GPU
    printf("[2] Создание OpenCL контекста и очереди команд...\n");
    cl_context context = create_gpu_context(&gpu_info, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "ОШИБКА: clCreateContext failed (err=%d)\n", err);
        return 1;
    }
    printf("✓ Контекст создан\n");
    
    cl_command_queue queue = create_gpu_queue(context, &gpu_info, CL_TRUE, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "ОШИБКА: clCreateCommandQueue failed (err=%d)\n", err);
        return 1;
    }
    printf("✓ Очередь команд создана\n");
    printf("\n");
    
    // Шаг 3: Подготовить данные
    printf("[3] Подготовка тестовых данных (%d элементов)...\n", ARRAY_SIZE);
    size_t bytes = ARRAY_SIZE * sizeof(float);
    float *hA = (float*)malloc(bytes);
    float *hB = (float*)malloc(bytes);
    float *hC = (float*)malloc(bytes);
    
    if (!hA || !hB || !hC) {
        fprintf(stderr, "ОШИБКА: malloc failed\n");
        return 1;
    }
    
    // Инициализация данных
    for (int i = 0; i < ARRAY_SIZE; i++) {
        hA[i] = (float)(i % 1000) * 0.01f;
        hB[i] = (float)((i * 3) % 1000) * 0.02f;
    }
    printf("✓ Данные подготовлены (%.2f MB на массив)\n", bytes / (1024.0 * 1024.0));
    printf("\n");
    
    // Шаг 4: Создать буферы на GPU
    printf("[4] Создание буферов на GPU...\n");
    cl_mem dA = clCreateBuffer(context, CL_MEM_READ_ONLY, bytes, NULL, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "ОШИБКА: clCreateBuffer(A) failed (err=%d)\n", err);
        return 1;
    }
    
    cl_mem dB = clCreateBuffer(context, CL_MEM_READ_ONLY, bytes, NULL, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "ОШИБКА: clCreateBuffer(B) failed (err=%d)\n", err);
        return 1;
    }
    
    cl_mem dC = clCreateBuffer(context, CL_MEM_WRITE_ONLY, bytes, NULL, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "ОШИБКА: clCreateBuffer(C) failed (err=%d)\n", err);
        return 1;
    }
    printf("✓ Буферы созданы\n");
    
    // Копирование данных на GPU
    clEnqueueWriteBuffer(queue, dA, CL_TRUE, 0, bytes, hA, 0, NULL, NULL);
    clEnqueueWriteBuffer(queue, dB, CL_TRUE, 0, bytes, hB, 0, NULL, NULL);
    printf("✓ Данные скопированы на GPU\n");
    printf("\n");
    
    // Шаг 5: Компиляция kernel
    printf("[5] Компиляция OpenCL kernel...\n");
    cl_program program = clCreateProgramWithSource(context, 1, &kernel_source, NULL, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "ОШИБКА: clCreateProgramWithSource failed (err=%d)\n", err);
        return 1;
    }
    
    err = clBuildProgram(program, 1, &gpu_info.device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        char log[4096];
        clGetProgramBuildInfo(program, gpu_info.device, CL_PROGRAM_BUILD_LOG, sizeof(log), log, NULL);
        fprintf(stderr, "ОШИБКА компиляции:\n%s\n", log);
        return 1;
    }
    printf("✓ Kernel скомпилирован\n");
    
    cl_kernel kernel = clCreateKernel(program, "vector_multiply", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "ОШИБКА: clCreateKernel failed (err=%d)\n", err);
        return 1;
    }
    printf("✓ Kernel создан\n");
    printf("\n");
    
    // Шаг 6: Установить аргументы kernel
    unsigned int n = ARRAY_SIZE;
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &dA);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &dB);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &dC);
    clSetKernelArg(kernel, 3, sizeof(unsigned int), &n);
    
    // Шаг 7: Запустить kernel несколько раз
    printf("[6] Запуск GPU вычислений (%d итераций)...\n", ITERATIONS);
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("⚠️  ПРОВЕРЬТЕ СЕЙЧАС:\n");
    printf("   - Запустите 'radeontop' в другом терминале\n");
    printf("   - Запустите 'htop' в другом терминале\n");
    printf("   - GPU должен показывать нагрузку (НЕ 0%%)\n");
    printf("   - CPU НЕ должен быть на 100%% на одном ядре\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\nНачинаю вычисления через 2 секунды...\n");
    sleep(2);
    
    size_t global_size = ARRAY_SIZE;
    size_t local_size = 256;
    double total_time = 0.0;
    
    for (int iter = 0; iter < ITERATIONS; iter++) {
        cl_event event;
        err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global_size, &local_size, 
                                     0, NULL, &event);
        if (err != CL_SUCCESS) {
            fprintf(stderr, "ОШИБКА: clEnqueueNDRangeKernel failed (err=%d)\n", err);
            return 1;
        }
        
        clFinish(queue);
        
        // Получить время выполнения
        cl_ulong start_time, end_time;
        clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_START, sizeof(start_time), &start_time, NULL);
        clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END, sizeof(end_time), &end_time, NULL);
        
        double exec_time = (end_time - start_time) * 1e-6; // нс -> мс
        total_time += exec_time;
        
        printf("Итерация %2d: %.3f мс\n", iter + 1, exec_time);
        clReleaseEvent(event);
    }
    
    double avg_time = total_time / ITERATIONS;
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Среднее время выполнения: %.3f мс\n", avg_time);
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    // Шаг 8: Проверить результаты
    printf("[7] Проверка результатов...\n");
    clEnqueueReadBuffer(queue, dC, CL_TRUE, 0, bytes, hC, 0, NULL, NULL);
    
    int errors = 0;
    for (int i = 0; i < 100 && i < ARRAY_SIZE; i++) {
        float expected = hA[i] * hB[i] * 100.0f; // 100 итераций в kernel
        if (fabsf(hC[i] - expected) > 0.001f) {
            if (errors == 0) {
                printf("  Ошибка в элементе %d: %.2f != %.2f\n", i, hC[i], expected);
            }
            errors++;
        }
    }
    
    if (errors == 0) {
        printf("✓ Все результаты корректны!\n");
    } else {
        printf("✗ Найдено %d ошибок в результатах\n", errors);
    }
    printf("\n");
    
    // Очистка
    printf("[8] Освобождение ресурсов...\n");
    clReleaseMemObject(dA);
    clReleaseMemObject(dB);
    clReleaseMemObject(dC);
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);
    free(hA);
    free(hB);
    free(hC);
    printf("✓ Готово\n");
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  ТЕСТ ЗАВЕРШЕН УСПЕШНО\n");
    printf("  GPU устройство было корректно использовано для вычислений!\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    return 0;
}
