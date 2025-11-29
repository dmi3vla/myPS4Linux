#ifndef OPENCL_GPU_HELPER_H
#define OPENCL_GPU_HELPER_H

#include <CL/cl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Структура для хранения выбранного GPU устройства
typedef struct {
    cl_platform_id platform;
    cl_device_id device;
    char platform_name[256];
    char device_name[256];
    cl_uint compute_units;
    cl_ulong global_mem_size;
    cl_uint max_clock_freq;
} gpu_device_info_t;

// Вывод информации об устройстве
void print_device_info(const gpu_device_info_t *info) {
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  ВЫБРАННОЕ GPU УСТРОЙСТВО\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Платформа:      %s\n", info->platform_name);
    printf("Устройство:     %s\n", info->device_name);
    printf("Вычисл. блоки:  %u CUs\n", info->compute_units);
    printf("Память:         %lu MB\n", info->global_mem_size / (1024 * 1024));
    printf("Частота:        %u MHz\n", info->max_clock_freq);
    printf("═══════════════════════════════════════════════════════════════\n");
}

// Главная функция для выбора лучшего GPU устройства
// Возвращает 0 при успехе, -1 при ошибке
int select_best_gpu_device(gpu_device_info_t *info) {
    cl_int err;
    cl_uint num_platforms;
    
    // Шаг 1: Получить количество платформ
    err = clGetPlatformIDs(0, NULL, &num_platforms);
    if (err != CL_SUCCESS || num_platforms == 0) {
        fprintf(stderr, "ОШИБКА: Не найдено OpenCL платформ (err=%d)\n", err);
        return -1;
    }
    
    // Шаг 2: Получить все платформы
    cl_platform_id *platforms = (cl_platform_id*)malloc(sizeof(cl_platform_id) * num_platforms);
    err = clGetPlatformIDs(num_platforms, platforms, NULL);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "ОШИБКА: clGetPlatformIDs failed (err=%d)\n", err);
        free(platforms);
        return -1;
    }
    
    // Шаг 3: Поиск лучшего GPU устройства
    // ВАЖНО: Для gfx701 платформа Clover работает стабильнее чем AMD APP!
    // Приоритет 1: Clover (стабильная Mesa реализация)
    // Приоритет 2: AMD Accelerated Parallel Processing (зависает при создании очереди)
    // Приоритет 3: Любое другое GPU устройство
    cl_platform_id best_platform = NULL;
    cl_device_id best_device = NULL;
    int priority = 999;
    
    for (cl_uint i = 0; i < num_platforms; i++) {
        char platform_name[256];
        clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(platform_name), platform_name, NULL);
        
        // Получить GPU устройства на этой платформе
        cl_uint num_devices;
        err = clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, 0, NULL, &num_devices);
        if (err != CL_SUCCESS || num_devices == 0) {
            // На этой платформе нет GPU, пропускаем
            continue;
        }
        
        cl_device_id *devices = (cl_device_id*)malloc(sizeof(cl_device_id) * num_devices);
        clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, num_devices, devices, NULL);
        
        // Проверяем приоритет платформы
        int current_priority = 3; // По умолчанию низкий приоритет
        if (strstr(platform_name, "Clover")) {
            current_priority = 1; // Высший приоритет для Clover (стабильнее для gfx701)
        } else if (strstr(platform_name, "AMD Accelerated Parallel Processing")) {
            current_priority = 2; // AMD APP зависает при создании очереди
        }
        
        // Если нашли платформу с более высоким приоритетом, выбираем первое GPU
        if (current_priority < priority) {
            priority = current_priority;
            best_platform = platforms[i];
            best_device = devices[0];
        }
        
        free(devices);
    }
    
    free(platforms);
    
    if (!best_device) {
        fprintf(stderr, "ОШИБКА: Не найдено ни одного GPU устройства!\n");
        return -1;
    }
    
    // Шаг 4: Заполнить информацию об устройстве
    info->platform = best_platform;
    info->device = best_device;
    
    clGetPlatformInfo(best_platform, CL_PLATFORM_NAME, 
                      sizeof(info->platform_name), info->platform_name, NULL);
    clGetDeviceInfo(best_device, CL_DEVICE_NAME, 
                    sizeof(info->device_name), info->device_name, NULL);
    clGetDeviceInfo(best_device, CL_DEVICE_MAX_COMPUTE_UNITS, 
                    sizeof(info->compute_units), &info->compute_units, NULL);
    clGetDeviceInfo(best_device, CL_DEVICE_GLOBAL_MEM_SIZE, 
                    sizeof(info->global_mem_size), &info->global_mem_size, NULL);
    clGetDeviceInfo(best_device, CL_DEVICE_MAX_CLOCK_FREQUENCY, 
                    sizeof(info->max_clock_freq), &info->max_clock_freq, NULL);
    
    return 0;
}

// Создать контекст для выбранного устройства
cl_context create_gpu_context(const gpu_device_info_t *info, cl_int *err_code) {
    cl_context_properties props[] = {
        CL_CONTEXT_PLATFORM, (cl_context_properties)info->platform,
        0
    };
    
    cl_int err;
    cl_context context = clCreateContext(props, 1, &info->device, NULL, NULL, &err);
    
    if (err_code) {
        *err_code = err;
    }
    
    return context;
}

// Создать очередь команд для выбранного устройства
// ВАЖНО: используем старый API clCreateCommandQueue вместо clCreateCommandQueueWithProperties
// так как новый API зависает на драйвере AMD APP для gfx701
cl_command_queue create_gpu_queue(cl_context context, const gpu_device_info_t *info, 
                                   cl_bool enable_profiling, cl_int *err_code) {
    cl_int err;
    cl_command_queue_properties props = 0;
    
    if (enable_profiling) {
        props = CL_QUEUE_PROFILING_ENABLE;
    }
    
    // Используем deprecated API - он стабильно работает на gfx701
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    cl_command_queue queue = clCreateCommandQueue(context, info->device, props, &err);
    #pragma GCC diagnostic pop
    
    if (err_code) {
        *err_code = err;
    }
    
    return queue;
}

#endif // OPENCL_GPU_HELPER_H
