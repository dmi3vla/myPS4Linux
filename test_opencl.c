#include <CL/cl.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
    cl_platform_id platform;
    cl_device_id device;
    cl_uint num_platforms = 0, num_devices = 0;
    cl_int err;
    
    // Получить платформы OpenCL
    err = clGetPlatformIDs(1, &platform, &num_platforms);
    if (err != CL_SUCCESS || num_platforms == 0) {
        printf("Ошибка: OpenCL платформы не найдены (код: %d)\n", err);
        return 1;
    }
    
    printf("✓ Найдено OpenCL платформ: %u\n", num_platforms);
    
    // Получить имя платформы
    char platform_name[256];
    clGetPlatformInfo(platform, CL_PLATFORM_NAME, sizeof(platform_name), platform_name, NULL);
    printf("  Платформа: %s\n", platform_name);
    
    // Получить GPU устройства
    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, &num_devices);
    if (err != CL_SUCCESS || num_devices == 0) {
        printf("Ошибка: GPU устройства не найдены (код: %d)\n", err);
        return 1;
    }
    
    printf("✓ Найдено GPU устройств: %u\n", num_devices);
    
    // Получить информацию о GPU
    char device_name[256];
    cl_uint compute_units;
    cl_ulong global_mem_size;
    cl_ulong local_mem_size;
    size_t max_work_group_size;
    
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(device_name), device_name, NULL);
    clGetDeviceInfo(device, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(compute_units), &compute_units, NULL);
    clGetDeviceInfo(device, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(global_mem_size), &global_mem_size, NULL);
    clGetDeviceInfo(device, CL_DEVICE_LOCAL_MEM_SIZE, sizeof(local_mem_size), &local_mem_size, NULL);
    clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(max_work_group_size), &max_work_group_size, NULL);
    
    printf("\n=== Информация о GPU ===\n");
    printf("Устройство: %s\n", device_name);
    printf("Compute Units: %u\n", compute_units);
    printf("Глобальная память: %lu MB\n", global_mem_size / (1024*1024));
    printf("Локальная память: %lu KB\n", local_mem_size / 1024);
    printf("Макс. размер рабочей группы: %zu\n", max_work_group_size);
    
    // Создать контекст
    cl_context context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    if (err != CL_SUCCESS) {
        printf("Ошибка создания контекста OpenCL (код: %d)\n", err);
        return 1;
    }
    printf("\n✓ OpenCL контекст создан успешно\n");
    
    // Создать очередь команд
    cl_command_queue queue = clCreateCommandQueue(context, device, 0, &err);
    if (err != CL_SUCCESS) {
        printf("Ошибка создания очереди команд (код: %d)\n", err);
        clReleaseContext(context);
        return 1;
    }
    printf("✓ Очередь команд создана успешно\n");
    
    printf("\n=== УСПЕХ! ===\n");
    printf("GPU готов для вычислений через OpenCL/LLVM\n");
    printf("Устройство /dev/kfd работает корректно\n");
    
    // Очистка
    clReleaseCommandQueue(queue);
    clReleaseContext(context);
    
    return 0;
}
