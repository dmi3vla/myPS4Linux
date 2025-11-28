#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <CL/cl.h>
#include <sys/time.h>

// Минимальный kernel для теста
const char *test_kernel = 
"__kernel void test_write(__global float *out) {\n"
"    int gid = get_global_id(0);\n"
"    out[gid] = (float)gid * 2.0f;\n"
"}\n";

void print_separator() {
    printf("═══════════════════════════════════════════════════════════════\n");
}

void check_cl_error(cl_int err, const char *operation) {
    if (err != CL_SUCCESS) {
        printf("✗ ОШИБКА в %s: %d\n", operation, err);
        exit(1);
    }
    printf("✓ %s: OK\n", operation);
}

int main() {
    print_separator();
    printf("  ДИАГНОСТИКА clEnqueueNDRangeKernel для Gladius GPU\n");
    print_separator();
    
    // 1. Получить Clover платформу
    printf("\n[1] Поиск OpenCL платформ...\n");
    cl_platform_id platform = NULL;
    cl_uint num_platforms;
    clGetPlatformIDs(0, NULL, &num_platforms);
    cl_platform_id *platforms = malloc(sizeof(cl_platform_id) * num_platforms);
    clGetPlatformIDs(num_platforms, platforms, NULL);
    
    for (cl_uint i = 0; i < num_platforms; i++) {
        char name[256];
        clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(name), name, NULL);
        printf("  Platform %u: %s\n", i, name);
        if (strstr(name, "Clover")) {
            platform = platforms[i];
        }
    }
    
    if (!platform) {
        printf("✗ Clover платформа не найдена!\n");
        return 1;
    }
    printf("✓ Используем Clover\n");
    
    // 2. Получить GPU устройство
    printf("\n[2] Получение GPU устройства...\n");
    cl_device_id device;
    cl_int err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);
    check_cl_error(err, "clGetDeviceIDs");
    
    char dev_name[256];
    cl_uint compute_units;
    size_t max_work_group_size;
    cl_ulong global_mem_size;
    
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(dev_name), dev_name, NULL);
    clGetDeviceInfo(device, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(compute_units), &compute_units, NULL);
    clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(max_work_group_size), &max_work_group_size, NULL);
    clGetDeviceInfo(device, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(global_mem_size), &global_mem_size, NULL);
    
    printf("  Device: %s\n", dev_name);
    printf("  Compute Units: %u\n", compute_units);
    printf("  Max Work Group Size: %zu\n", max_work_group_size);
    printf("  Global Memory: %lu MB\n", global_mem_size / (1024*1024));
    
    // 3. Создать контекст
    printf("\n[3] Создание OpenCL контекста...\n");
    cl_context context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    check_cl_error(err, "clCreateContext");
    
    // 4. Создать очередь команд
    printf("\n[4] Создание очереди команд...\n");
    cl_command_queue queue = clCreateCommandQueue(context, device, 0, &err);
    check_cl_error(err, "clCreateCommandQueue");
    
    // 5. Компиляция kernel
    printf("\n[5] Компиляция kernel...\n");
    cl_program program = clCreateProgramWithSource(context, 1, &test_kernel, NULL, &err);
    check_cl_error(err, "clCreateProgramWithSource");
    
    err = clBuildProgram(program, 1, &device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        char log[4096];
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, sizeof(log), log, NULL);
        printf("✗ Ошибка компиляции:\n%s\n", log);
        return 1;
    }
    printf("✓ Kernel скомпилирован\n");
    
    cl_kernel kernel = clCreateKernel(program, "test_write", &err);
    check_cl_error(err, "clCreateKernel");
    
    // 6. Тест с разными размерами work-group
    printf("\n[6] ТЕСТИРОВАНИЕ clEnqueueNDRangeKernel\n");
    print_separator();
    
    size_t test_sizes[] = {1, 64, 256, 1024, 4096};
    int num_tests = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    for (int i = 0; i < num_tests; i++) {
        size_t global_size = test_sizes[i];
        printf("\n  Тест %d: global_size = %zu\n", i+1, global_size);
        
        // Создать буфер
        cl_mem buffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY, 
                                       global_size * sizeof(float), NULL, &err);
        if (err != CL_SUCCESS) {
            printf("  ✗ clCreateBuffer failed: %d\n", err);
            continue;
        }
        printf("  ✓ Буфер создан (%zu bytes)\n", global_size * sizeof(float));
        
        // Установить аргументы
        err = clSetKernelArg(kernel, 0, sizeof(cl_mem), &buffer);
        if (err != CL_SUCCESS) {
            printf("  ✗ clSetKernelArg failed: %d\n", err);
            clReleaseMemObject(buffer);
            continue;
        }
        printf("  ✓ Аргументы установлены\n");
        
        // КРИТИЧЕСКИЙ МОМЕНТ: Запуск kernel
        printf("  → Вызов clEnqueueNDRangeKernel...\n");
        fflush(stdout);
        
        struct timeval start, end;
        gettimeofday(&start, NULL);
        
        err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global_size, NULL, 0, NULL, NULL);
        
        gettimeofday(&end, NULL);
        long elapsed_us = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
        
        if (err != CL_SUCCESS) {
            printf("  ✗ clEnqueueNDRangeKernel FAILED: %d (время: %ld мкс)\n", err, elapsed_us);
            clReleaseMemObject(buffer);
            continue;
        }
        printf("  ✓ clEnqueueNDRangeKernel вернулся (время: %ld мкс)\n", elapsed_us);
        
        // Попытка дождаться завершения
        printf("  → Ожидание завершения (clFinish)...\n");
        fflush(stdout);
        
        gettimeofday(&start, NULL);
        err = clFinish(queue);
        gettimeofday(&end, NULL);
        elapsed_us = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
        
        if (err != CL_SUCCESS) {
            printf("  ✗ clFinish FAILED: %d (время: %ld мкс)\n", err, elapsed_us);
        } else {
            printf("  ✓ clFinish SUCCESS (время: %ld мкс)\n", elapsed_us);
            
            // Проверить результаты
            float *results = malloc(global_size * sizeof(float));
            err = clEnqueueReadBuffer(queue, buffer, CL_TRUE, 0, 
                                      global_size * sizeof(float), results, 0, NULL, NULL);
            if (err == CL_SUCCESS) {
                int errors = 0;
                for (size_t j = 0; j < global_size && j < 10; j++) {
                    float expected = j * 2.0f;
                    if (results[j] != expected) {
                        printf("    Ошибка[%zu]: %.2f != %.2f\n", j, results[j], expected);
                        errors++;
                    }
                }
                if (errors == 0) {
                    printf("  ✓ Результаты КОРРЕКТНЫ!\n");
                } else {
                    printf("  ✗ Обнаружены ошибки в результатах\n");
                }
            }
            free(results);
        }
        
        clReleaseMemObject(buffer);
    }
    
    // Cleanup
    printf("\n");
    print_separator();
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);
    free(platforms);
    
    printf("Диагностика завершена\n");
    print_separator();
    
    return 0;
}
