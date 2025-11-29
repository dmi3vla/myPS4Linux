#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <CL/cl.h>

/*
 * ═══════════════════════════════════════════════════════════════
 * УТИЛИТА ДЛЯ ПРОВЕРКИ СООТВЕТСТВИЯ МЕТРИК KFD И OPENCL
 * ═══════════════════════════════════════════════════════════════
 * 
 * Сравнивает топологию GPU из KFD (kernel-level) с данными OpenCL
 */

// Структура для хранения метрик KFD
typedef struct {
    int node_id;
    unsigned int simd_count;
    unsigned int max_waves_per_simd;
    unsigned int max_engine_clk_fcompute;
    unsigned int max_engine_clk_ccompute;
    unsigned int local_mem_size;
    unsigned long long max_allocatable_memory;
    unsigned int cu_per_simd_array;
    unsigned int simd_arrays_per_engine;
    unsigned int num_shader_banks;
    unsigned int num_shader_arrays;
    unsigned int gpu_id;
} kfd_metrics_t;

// Структура для хранения метрик OpenCL
typedef struct {
    char platform_name[256];
    char device_name[256];
    cl_uint max_compute_units;
    cl_ulong global_mem_size;
    cl_ulong local_mem_size;
    cl_uint max_clock_frequency;
    cl_uint max_work_item_dimensions;
    size_t max_work_group_size;
    cl_uint mem_base_addr_align;
    cl_device_type device_type;
} opencl_metrics_t;

// Чтение метрик KFD из sysfs
int read_kfd_metrics(int node_id, kfd_metrics_t *metrics) {
    char path[512];
    FILE *fp;
    
    memset(metrics, 0, sizeof(kfd_metrics_t));
    metrics->node_id = node_id;
    
    // Базовый путь к узлу KFD
    snprintf(path, sizeof(path), "/sys/class/kfd/kfd/topology/nodes/%d/properties", node_id);
    
    fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        // Парсим каждое свойство
        if (sscanf(line, "simd_count %u", &metrics->simd_count) == 1) continue;
        if (sscanf(line, "max_waves_per_simd %u", &metrics->max_waves_per_simd) == 1) continue;
        if (sscanf(line, "max_engine_clk_fcompute %u", &metrics->max_engine_clk_fcompute) == 1) continue;
        if (sscanf(line, "max_engine_clk_ccompute %u", &metrics->max_engine_clk_ccompute) == 1) continue;
        if (sscanf(line, "local_mem_size %u", &metrics->local_mem_size) == 1) continue;
        if (sscanf(line, "max_allocatable_memory %llu", &metrics->max_allocatable_memory) == 1) continue;
        if (sscanf(line, "cu_per_simd_array %u", &metrics->cu_per_simd_array) == 1) continue;
        if (sscanf(line, "simd_arrays_per_engine %u", &metrics->simd_arrays_per_engine) == 1) continue;
        if (sscanf(line, "num_shader_banks %u", &metrics->num_shader_banks) == 1) continue;
        if (sscanf(line, "num_shader_arrays %u", &metrics->num_shader_arrays) == 1) continue;
        if (sscanf(line, "gpu_id %u", &metrics->gpu_id) == 1) continue;
    }
    
    fclose(fp);
    return 0;
}

// Вычисление числа CU из метрик KFD
unsigned int calculate_kfd_compute_units(const kfd_metrics_t *kfd) {
    // CU = число SIMD массивов × CU на массив
    if (kfd->num_shader_arrays > 0 && kfd->cu_per_simd_array > 0) {
        return kfd->num_shader_arrays * kfd->cu_per_simd_array;
    }
    
    // Альтернативный метод: SIMD count / 4 (для GCN архитектуры)
    if (kfd->simd_count > 0) {
        return kfd->simd_count / 4;
    }
    
    return 0;
}

// Получение метрик OpenCL для устройства
int read_opencl_metrics(cl_device_id device, cl_platform_id platform, opencl_metrics_t *metrics) {
    memset(metrics, 0, sizeof(opencl_metrics_t));
    
    // Получить имя платформы
    clGetPlatformInfo(platform, CL_PLATFORM_NAME, sizeof(metrics->platform_name), 
                     metrics->platform_name, NULL);
    
    // Получить имя устройства
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(metrics->device_name), 
                   metrics->device_name, NULL);
    
    // Compute Units
    clGetDeviceInfo(device, CL_DEVICE_MAX_COMPUTE_UNITS, 
                   sizeof(metrics->max_compute_units), &metrics->max_compute_units, NULL);
    
    // Память
    clGetDeviceInfo(device, CL_DEVICE_GLOBAL_MEM_SIZE, 
                   sizeof(metrics->global_mem_size), &metrics->global_mem_size, NULL);
    clGetDeviceInfo(device, CL_DEVICE_LOCAL_MEM_SIZE, 
                   sizeof(metrics->local_mem_size), &metrics->local_mem_size, NULL);
    
    // Частота
    clGetDeviceInfo(device, CL_DEVICE_MAX_CLOCK_FREQUENCY, 
                   sizeof(metrics->max_clock_frequency), &metrics->max_clock_frequency, NULL);
    
    // Work group размеры
    clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, 
                   sizeof(metrics->max_work_item_dimensions), &metrics->max_work_item_dimensions, NULL);
    clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_GROUP_SIZE, 
                   sizeof(metrics->max_work_group_size), &metrics->max_work_group_size, NULL);
    
    // Выравнивание памяти
    clGetDeviceInfo(device, CL_DEVICE_MEM_BASE_ADDR_ALIGN, 
                   sizeof(metrics->mem_base_addr_align), &metrics->mem_base_addr_align, NULL);
    
    // Тип устройства
    clGetDeviceInfo(device, CL_DEVICE_TYPE, 
                   sizeof(metrics->device_type), &metrics->device_type, NULL);
    
    return 0;
}

// Вывод метрик KFD
void print_kfd_metrics(const kfd_metrics_t *kfd) {
    printf("\n═══════════════════════════════════════════════════════════════\n");
    printf("  KFD МЕТРИКИ (NODE %d)\n", kfd->node_id);
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("GPU ID:                   0x%X\n", kfd->gpu_id);
    printf("SIMD Count:               %u\n", kfd->simd_count);
    printf("Shader Arrays:            %u\n", kfd->num_shader_arrays);
    printf("CU per Array:             %u\n", kfd->cu_per_simd_array);
    printf("→ Calculated CUs:         %u\n", calculate_kfd_compute_units(kfd));
    printf("\nЧастоты (MHz):\n");
    printf("  FCompute:               %u MHz\n", kfd->max_engine_clk_fcompute);
    printf("  CCompute:               %u MHz\n", kfd->max_engine_clk_ccompute);
    printf("\nПамять:\n");
    printf("  Local Mem:              %u KB\n", kfd->local_mem_size / 1024);
    printf("  Max Allocatable:        %llu MB\n", kfd->max_allocatable_memory / (1024*1024));
    printf("\nДругие параметры:\n");
    printf("  Max Waves/SIMD:         %u\n", kfd->max_waves_per_simd);
    printf("  SIMD Arrays/Engine:     %u\n", kfd->simd_arrays_per_engine);
    printf("  Shader Banks:           %u\n", kfd->num_shader_banks);
    printf("═══════════════════════════════════════════════════════════════\n");
}

// Вывод метрик OpenCL
void print_opencl_metrics(const opencl_metrics_t *ocl) {
    printf("\n═══════════════════════════════════════════════════════════════\n");
    printf("  OPENCL МЕТРИКИ\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Platform:                 %s\n", ocl->platform_name);
    printf("Device:                   %s\n", ocl->device_name);
    printf("Device Type:              %s\n", 
           (ocl->device_type == CL_DEVICE_TYPE_GPU) ? "GPU" : "Other");
    printf("\nCompute:\n");
    printf("  Compute Units:          %u\n", ocl->max_compute_units);
    printf("  Max Clock:              %u MHz\n", ocl->max_clock_frequency);
    printf("  Max Work Group Size:    %zu\n", ocl->max_work_group_size);
    printf("\nПамять:\n");
    printf("  Global Mem:             %llu MB\n", ocl->global_mem_size / (1024*1024));
    printf("  Local Mem:              %llu KB\n", ocl->local_mem_size / 1024);
    printf("  Mem Align:              %u bits\n", ocl->mem_base_addr_align);
    printf("═══════════════════════════════════════════════════════════════\n");
}

// Сравнение метрик и вывод результата
void compare_metrics(const kfd_metrics_t *kfd, const opencl_metrics_t *ocl) {
    unsigned int kfd_cu = calculate_kfd_compute_units(kfd);
    
    printf("\n═══════════════════════════════════════════════════════════════\n");
    printf("  СРАВНЕНИЕ МЕТРИК\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    
    // Сравнение Compute Units
    printf("\n[1] Compute Units:\n");
    printf("    KFD:    %u CUs\n", kfd_cu);
    printf("    OpenCL: %u CUs\n", ocl->max_compute_units);
    if (kfd_cu == ocl->max_compute_units) {
        printf("    ✅ СОВПАДАЮТ\n");
    } else {
        printf("    ⚠️  РАЗЛИЧАЮТСЯ (разница: %d)\n", 
               (int)ocl->max_compute_units - (int)kfd_cu);
    }
    
    // Сравнение частоты
    printf("\n[2] Частота:\n");
    printf("    KFD FCompute: %u MHz\n", kfd->max_engine_clk_fcompute);
    printf("    KFD CCompute: %u MHz\n", kfd->max_engine_clk_ccompute);
    printf("    OpenCL:       %u MHz\n", ocl->max_clock_frequency);
    
    // OpenCL может показывать либо FCompute, либо CCompute
    if (ocl->max_clock_frequency == kfd->max_engine_clk_fcompute ||
        ocl->max_clock_frequency == kfd->max_engine_clk_ccompute) {
        printf("    ✅ СОВПАДАЮТ\n");
    } else {
        printf("    ⚠️  РАЗЛИЧАЮТСЯ\n");
    }
    
    // Сравнение Local Memory
    printf("\n[3] Local Memory:\n");
    printf("    KFD:    %u KB\n", kfd->local_mem_size / 1024);
    printf("    OpenCL: %llu KB\n", ocl->local_mem_size / 1024);
    if (kfd->local_mem_size == ocl->local_mem_size) {
        printf("    ✅ СОВПАДАЮТ\n");
    } else {
        printf("    ⚠️  РАЗЛИЧАЮТСЯ\n");
    }
    
    // Общая память (KFD max_allocatable vs OpenCL global_mem)
    printf("\n[4] Глобальная память:\n");
    printf("    KFD Max Alloc: %llu MB\n", kfd->max_allocatable_memory / (1024*1024));
    printf("    OpenCL Global: %llu MB\n", ocl->global_mem_size / (1024*1024));
    
    // Могут различаться из-за резервирования системой
    unsigned long long diff_mb = llabs((long long)ocl->global_mem_size - 
                                       (long long)kfd->max_allocatable_memory) / (1024*1024);
    if (diff_mb < 100) {  // Допустимое расхождение < 100 MB
        printf("    ✅ ПРИМЕРНО СОВПАДАЮТ (разница: %llu MB)\n", diff_mb);
    } else {
        printf("    ⚠️  ЗНАЧИТЕЛЬНО РАЗЛИЧАЮТСЯ (разница: %llu MB)\n", diff_mb);
    }
    
    printf("\n═══════════════════════════════════════════════════════════════\n");
}

// Поиск GPU узлов KFD
int find_kfd_gpu_nodes(int *node_ids, int max_nodes) {
    int count = 0;
    
    for (int i = 0; i < 16 && count < max_nodes; i++) {
        char path[512];
        snprintf(path, sizeof(path), "/sys/class/kfd/kfd/topology/nodes/%d/gpu_id", i);
        
        FILE *fp = fopen(path, "r");
        if (!fp) continue;
        
        unsigned int gpu_id;
        if (fscanf(fp, "%u", &gpu_id) == 1 && gpu_id != 0) {
            node_ids[count++] = i;
        }
        fclose(fp);
    }
    
    return count;
}

int main() {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  ПРОВЕРКА СООТВЕТСТВИЯ МЕТРИК KFD И OPENCL\n");
    printf("  PS4 Pro GPU (gfx701/Gladius)\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    
    // Шаг 1: Найти GPU узлы в KFD
    printf("\n[Шаг 1] Поиск GPU узлов в KFD...\n");
    int kfd_nodes[4];
    int kfd_node_count = find_kfd_gpu_nodes(kfd_nodes, 4);
    
    if (kfd_node_count == 0) {
        fprintf(stderr, "❌ GPU узлы KFD не найдены!\n");
        fprintf(stderr, "Проверьте: ls /sys/class/kfd/kfd/topology/nodes/\n");
        return 1;
    }
    
    printf("✅ Найдено GPU узлов: %d\n", kfd_node_count);
    for (int i = 0; i < kfd_node_count; i++) {
        printf("   - Node %d\n", kfd_nodes[i]);
    }
    
    // Шаг 2: Прочитать метрики KFD для первого GPU
    printf("\n[Шаг 2] Чтение метрик KFD для Node %d...\n", kfd_nodes[0]);
    kfd_metrics_t kfd_metrics;
    if (read_kfd_metrics(kfd_nodes[0], &kfd_metrics) != 0) {
        fprintf(stderr, "❌ Не удалось прочитать метрики KFD!\n");
        return 1;
    }
    print_kfd_metrics(&kfd_metrics);
    
    // Шаг 3: Получить все OpenCL платформы и устройства
    printf("\n[Шаг 3] Поиск OpenCL GPU устройств...\n");
    
    cl_uint num_platforms;
    clGetPlatformIDs(0, NULL, &num_platforms);
    cl_platform_id *platforms = malloc(sizeof(cl_platform_id) * num_platforms);
    clGetPlatformIDs(num_platforms, platforms, NULL);
    
    int found_match = 0;
    
    for (cl_uint i = 0; i < num_platforms; i++) {
        char platform_name[256];
        clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(platform_name), 
                         platform_name, NULL);
        
        cl_uint num_devices;
        cl_int err = clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, 0, NULL, &num_devices);
        if (err != CL_SUCCESS || num_devices == 0) continue;
        
        cl_device_id *devices = malloc(sizeof(cl_device_id) * num_devices);
        clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, num_devices, devices, NULL);
        
        for (cl_uint j = 0; j < num_devices; j++) {
            opencl_metrics_t ocl_metrics;
            read_opencl_metrics(devices[j], platforms[i], &ocl_metrics);
            
            print_opencl_metrics(&ocl_metrics);
            compare_metrics(&kfd_metrics, &ocl_metrics);
            found_match = 1;
        }
        
        free(devices);
    }
    
    free(platforms);
    
    if (!found_match) {
        fprintf(stderr, "\n❌ OpenCL GPU устройства не найдены!\n");
        return 1;
    }
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  ПРОВЕРКА ЗАВЕРШЕНА\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    return 0;
}
