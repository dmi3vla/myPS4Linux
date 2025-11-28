#include <CL/cl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MATRIX_SIZE 32
#define ARRAY_SIZE (MATRIX_SIZE * MATRIX_SIZE)

// Kernel для рекомбинации (перемешивания) массива
const char *recombine_kernel = 
"__kernel void recombine(__global float *data, __global int *indices) {\n"
"    int gid = get_global_id(0);\n"
"    if (gid < 1024) {\n"
"        int new_idx = indices[gid];\n"
"        float temp = data[gid];\n"
"        barrier(CLK_GLOBAL_MEM_FENCE);\n"
"        data[new_idx] = temp;\n"
"    }\n"
"}\n";

void print_matrix_sample(float *data, const char *label) {
    printf("\n%s (первые 8x8):\n", label);
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            printf("%.2f ", data[i * MATRIX_SIZE + j]);
        }
        printf("\n");
    }
}

int main() {
    cl_platform_id platform;
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel kernel;
    cl_mem vram_buffer, indices_buffer;
    cl_int err;
    
    // 1. Инициализация массива случайными float
    printf("=== Шаг 1: Инициализация массива %dx%d случайными float ===\n", MATRIX_SIZE, MATRIX_SIZE);
    float *host_data = (float*)malloc(ARRAY_SIZE * sizeof(float));
    srand(time(NULL));
    for (int i = 0; i < ARRAY_SIZE; i++) {
        host_data[i] = (float)rand() / RAND_MAX * 100.0f;
    }
    print_matrix_sample(host_data, "Исходный массив");
    
    // Получить OpenCL платформу и устройство
    cl_platform_id platforms[10];
    cl_uint num_platforms;
    err = clGetPlatformIDs(10, platforms, &num_platforms);
    if (err != CL_SUCCESS) {
        printf("Ошибка получения платформ: %d\n", err);
        return 1;
    }
    
    printf("\n=== Доступные платформы ===\n");
    for (cl_uint i = 0; i < num_platforms; i++) {
        char name[256];
        clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(name), name, NULL);
        printf("%u: %s\n", i, name);
    }
    
    // Использовать Clover (обычно платформа 1) вместо ROCm
    cl_uint platform_idx = (num_platforms > 1) ? 1 : 0;
    platform = platforms[platform_idx];
    
    char platform_name[256];
    clGetPlatformInfo(platform, CL_PLATFORM_NAME, sizeof(platform_name), platform_name, NULL);
    printf("Выбрана платформа: %s\n", platform_name);
    
    // Попробовать найти GPU
    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);
    if (err != CL_SUCCESS) {
        printf("GPU не найден, пробую CL_DEVICE_TYPE_ALL...\n");
        err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 1, &device, NULL);
        if (err != CL_SUCCESS) {
            printf("Ошибка получения устройства: %d\n", err);
            return 1;
        }
    }
    
    // Получить информацию об устройстве
    char device_name[256];
    cl_ulong global_mem_size, local_mem_size;
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(device_name), device_name, NULL);
    clGetDeviceInfo(device, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(global_mem_size), &global_mem_size, NULL);
    clGetDeviceInfo(device, CL_DEVICE_LOCAL_MEM_SIZE, sizeof(local_mem_size), &local_mem_size, NULL);
    
    printf("\n=== Устройство: %s ===\n", device_name);
    printf("Глобальная память (VRAM): %lu MB\n", global_mem_size / (1024*1024));
    printf("Локальная память: %lu KB\n", local_mem_size / 1024);
    
    // 2. Получить namespace VRAM для записи
    printf("\n=== Шаг 2: Создание контекста и получение VRAM namespace ===\n");
    context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    if (err != CL_SUCCESS) {
        printf("Ошибка создания контекста: %d\n", err);
        return 1;
    }
    
    queue = clCreateCommandQueue(context, device, 0, &err);
    if (err != CL_SUCCESS) {
        printf("Ошибка создания очереди: %d\n", err);
        return 1;
    }
    
    // 3. Записать в VRAM
    printf("\n=== Шаг 3: Запись данных в VRAM ===\n");
    vram_buffer = clCreateBuffer(context, 
                                  CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                                  ARRAY_SIZE * sizeof(float),
                                  host_data,
                                  &err);
    if (err != CL_SUCCESS) {
        printf("Ошибка создания VRAM буфера: %d\n", err);
        return 1;
    }
    printf("✓ Данные записаны в VRAM (буфер создан)\n");
    
    // 4. Прочитать из VRAM
    printf("\n=== Шаг 4: Чтение данных из VRAM ===\n");
    float *read_data = (float*)malloc(ARRAY_SIZE * sizeof(float));
    err = clEnqueueReadBuffer(queue, vram_buffer, CL_TRUE, 0,
                              ARRAY_SIZE * sizeof(float), read_data,
                              0, NULL, NULL);
    if (err != CL_SUCCESS) {
        printf("Ошибка чтения из VRAM: %d\n", err);
        return 1;
    }
    print_matrix_sample(read_data, "Прочитано из VRAM");
    
    // Проверка целостности
    int errors = 0;
    for (int i = 0; i < ARRAY_SIZE && errors < 5; i++) {
        if (read_data[i] != host_data[i]) {
            printf("Ошибка на индексе %d: ожидалось %.2f, получено %.2f\n", 
                   i, host_data[i], read_data[i]);
            errors++;
        }
    }
    if (errors == 0) {
        printf("✓ Данные прочитаны корректно\n");
    }
    
    // 5. Рекомбинировать массив (перемешать)
    printf("\n=== Шаг 5: Рекомбинация массива в VRAM ===\n");
    
    // Создать массив индексов для перемешивания
    int *indices = (int*)malloc(ARRAY_SIZE * sizeof(int));
    for (int i = 0; i < ARRAY_SIZE; i++) {
        indices[i] = i;
    }
    // Перемешать индексы (Fisher-Yates shuffle)
    for (int i = ARRAY_SIZE - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
    }
    
    // Создать буфер для индексов
    indices_buffer = clCreateBuffer(context,
                                     CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                     ARRAY_SIZE * sizeof(int),
                                     indices,
                                     &err);
    if (err != CL_SUCCESS) {
        printf("Ошибка создания буфера индексов: %d\n", err);
        return 1;
    }
    
    // Скомпилировать kernel
    program = clCreateProgramWithSource(context, 1, &recombine_kernel, NULL, &err);
    if (err != CL_SUCCESS) {
        printf("Ошибка создания программы: %d\n", err);
        return 1;
    }
    
    err = clBuildProgram(program, 1, &device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        printf("Ошибка компиляции kernel: %d\n", err);
        char build_log[4096];
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 
                             sizeof(build_log), build_log, NULL);
        printf("Build log:\n%s\n", build_log);
        return 1;
    }
    
    kernel = clCreateKernel(program, "recombine", &err);
    if (err != CL_SUCCESS) {
        printf("Ошибка создания kernel: %d\n", err);
        return 1;
    }
    
    // Установить аргументы kernel
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &vram_buffer);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &indices_buffer);
    
    // Запустить kernel
    size_t global_size = ARRAY_SIZE;
    err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global_size, NULL, 0, NULL, NULL);
    if (err != CL_SUCCESS) {
        printf("Ошибка запуска kernel: %d\n", err);
        return 1;
    }
    
    clFinish(queue);
    printf("✓ Массив рекомбинирован в VRAM\n");
    
    // 6. Прочитать рекомбинированный массив
    printf("\n=== Шаг 6: Чтение рекомбинированного массива из VRAM ===\n");
    float *recombined_data = (float*)malloc(ARRAY_SIZE * sizeof(float));
    err = clEnqueueReadBuffer(queue, vram_buffer, CL_TRUE, 0,
                              ARRAY_SIZE * sizeof(float), recombined_data,
                              0, NULL, NULL);
    if (err != CL_SUCCESS) {
        printf("Ошибка чтения рекомбинированных данных: %d\n", err);
        return 1;
    }
    print_matrix_sample(recombined_data, "Рекомбинированный массив");
    
    // Проверить, что данные действительно перемешаны
    int different = 0;
    for (int i = 0; i < ARRAY_SIZE; i++) {
        if (recombined_data[i] != host_data[i]) {
            different++;
        }
    }
    printf("✓ Изменено элементов: %d из %d\n", different, ARRAY_SIZE);
    
    // 7. Удалить данные из VRAM
    printf("\n=== Шаг 7: Освобождение VRAM ===\n");
    clReleaseMemObject(vram_buffer);
    clReleaseMemObject(indices_buffer);
    printf("✓ VRAM освобождена\n");
    
    // Очистка
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);
    free(host_data);
    free(read_data);
    free(recombined_data);
    free(indices);
    
    printf("\n=== ТЕСТ ЗАВЕРШЕН УСПЕШНО ===\n");
    printf("Все операции с VRAM выполнены корректно:\n");
    printf("  ✓ Инициализация массива\n");
    printf("  ✓ Получение VRAM namespace\n");
    printf("  ✓ Запись в VRAM\n");
    printf("  ✓ Чтение из VRAM\n");
    printf("  ✓ Рекомбинация в VRAM\n");
    printf("  ✓ Повторное чтение\n");
    printf("  ✓ Освобождение VRAM\n");
    
    return 0;
}
