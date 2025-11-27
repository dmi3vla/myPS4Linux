#include <CL/cl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_device_info(cl_device_id device) {
    char device_name[256];
    char device_vendor[256];
    cl_device_type device_type;
    cl_uint compute_units;
    cl_ulong global_mem_size;
    cl_ulong local_mem_size;
    size_t max_work_group_size;
    
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(device_name), device_name, NULL);
    clGetDeviceInfo(device, CL_DEVICE_VENDOR, sizeof(device_vendor), device_vendor, NULL);
    clGetDeviceInfo(device, CL_DEVICE_TYPE, sizeof(device_type), &device_type, NULL);
    clGetDeviceInfo(device, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(compute_units), &compute_units, NULL);
    clGetDeviceInfo(device, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(global_mem_size), &global_mem_size, NULL);
    clGetDeviceInfo(device, CL_DEVICE_LOCAL_MEM_SIZE, sizeof(local_mem_size), &local_mem_size, NULL);
    clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(max_work_group_size), &max_work_group_size, NULL);
    
    printf("  Устройство: %s\n", device_name);
    printf("  Производитель: %s\n", device_vendor);
    printf("  Тип: ");
    if (device_type & CL_DEVICE_TYPE_CPU) printf("CPU ");
    if (device_type & CL_DEVICE_TYPE_GPU) printf("GPU ");
    if (device_type & CL_DEVICE_TYPE_ACCELERATOR) printf("ACCELERATOR ");
    printf("\n");
    printf("  Compute Units: %u\n", compute_units);
    printf("  Глобальная память: %lu MB\n", global_mem_size / (1024*1024));
    printf("  Локальная память: %lu KB\n", local_mem_size / 1024);
    printf("  Макс. размер рабочей группы: %zu\n", max_work_group_size);
}

int main() {
    cl_platform_id platforms[10];
    cl_device_id devices[10];
    cl_uint num_platforms = 0, num_devices = 0;
    cl_int err;
    
    // Получить все платформы OpenCL
    err = clGetPlatformIDs(10, platforms, &num_platforms);
    if (err != CL_SUCCESS || num_platforms == 0) {
        printf("Ошибка: OpenCL платформы не найдены (код: %d)\n", err);
        return 1;
    }
    
    printf("=== Найдено OpenCL платформ: %u ===\n\n", num_platforms);
    
    // Перебрать все платформы
    for (cl_uint p = 0; p < num_platforms; p++) {
        char platform_name[256];
        char platform_vendor[256];
        clGetPlatformInfo(platforms[p], CL_PLATFORM_NAME, sizeof(platform_name), platform_name, NULL);
        clGetPlatformInfo(platforms[p], CL_PLATFORM_VENDOR, sizeof(platform_vendor), platform_vendor, NULL);
        
        printf("Платформа %u: %s (%s)\n", p, platform_name, platform_vendor);
        
        // Получить все устройства на платформе
        err = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_ALL, 10, devices, &num_devices);
        if (err == CL_SUCCESS && num_devices > 0) {
            printf("  Найдено устройств: %u\n", num_devices);
            for (cl_uint d = 0; d < num_devices; d++) {
                printf("\n  --- Устройство %u ---\n", d);
                print_device_info(devices[d]);
            }
        } else {
            printf("  Нет устройств (код: %d)\n", err);
        }
        printf("\n");
    }
    
    // Теперь попробовать выбрать GPU устройство
    printf("=== Попытка выбрать GPU устройство ===\n");
    cl_device_id gpu_device = NULL;
    cl_platform_id gpu_platform = NULL;
    
    for (cl_uint p = 0; p < num_platforms; p++) {
        err = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_GPU, 1, &gpu_device, NULL);
        if (err == CL_SUCCESS) {
            gpu_platform = platforms[p];
            char platform_name[256];
            clGetPlatformInfo(gpu_platform, CL_PLATFORM_NAME, sizeof(platform_name), platform_name, NULL);
            printf("✓ GPU найден на платформе: %s\n\n", platform_name);
            print_device_info(gpu_device);
            break;
        }
    }
    
    if (gpu_device == NULL) {
        printf("✗ GPU устройство не найдено!\n");
        printf("Попробую использовать первое доступное устройство...\n");
        
        // Взять первое устройство с первой платформы
        err = clGetDeviceIDs(platforms[0], CL_DEVICE_TYPE_ALL, 1, &gpu_device, NULL);
        if (err != CL_SUCCESS) {
            printf("Ошибка: не удалось получить ни одно устройство\n");
            return 1;
        }
        gpu_platform = platforms[0];
    }
    
    // Создать контекст
    printf("\n=== Создание OpenCL контекста ===\n");
    cl_context context = clCreateContext(NULL, 1, &gpu_device, NULL, NULL, &err);
    if (err != CL_SUCCESS) {
        printf("✗ Ошибка создания контекста OpenCL (код: %d)\n", err);
        return 1;
    }
    printf("✓ OpenCL контекст создан успешно\n");
    
    // Создать очередь команд
    cl_command_queue queue = clCreateCommandQueue(context, gpu_device, 0, &err);
    if (err != CL_SUCCESS) {
        printf("✗ Ошибка создания очереди команд (код: %d)\n", err);
        clReleaseContext(context);
        return 1;
    }
    printf("✓ Очередь команд создана успешно\n");
    
    printf("\n=== УСПЕХ! ===\n");
    printf("OpenCL готов для вычислений\n");
    
    // Очистка
    clReleaseCommandQueue(queue);
    clReleaseContext(context);
    
    return 0;
}
