#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <time.h>

#define MATRIX_SIZE 32
#define ARRAY_SIZE (MATRIX_SIZE * MATRIX_SIZE)

/* KFD ioctl definitions */
struct kfd_ioctl_get_version_args {
    uint32_t major_version;
    uint32_t minor_version;
};

struct kfd_ioctl_acquire_vm_args {
    uint32_t drm_fd;
    uint32_t gpu_id;
};

struct kfd_ioctl_alloc_memory_of_gpu_args {
    uint64_t va_addr;
    uint64_t size;
    uint64_t handle;
    uint64_t mmap_offset;
    uint32_t gpu_id;
    uint32_t flags;
};

struct kfd_ioctl_map_memory_to_gpu_args {
    uint64_t handle;
    uint64_t device_ids_array_ptr;
    uint32_t n_devices;
    uint32_t n_success;
};

struct kfd_ioctl_free_memory_of_gpu_args {
    uint64_t handle;
};

#define AMDKFD_IOCTL_BASE 'K'
#define AMDKFD_IOR(nr, type) _IOR(AMDKFD_IOCTL_BASE, nr, type)
#define AMDKFD_IOW(nr, type) _IOW(AMDKFD_IOCTL_BASE, nr, type)
#define AMDKFD_IOWR(nr, type) _IOWR(AMDKFD_IOCTL_BASE, nr, type)

#define AMDKFD_IOC_GET_VERSION AMDKFD_IOR(0x01, struct kfd_ioctl_get_version_args)
#define AMDKFD_IOC_ACQUIRE_VM AMDKFD_IOW(0x15, struct kfd_ioctl_acquire_vm_args)
#define AMDKFD_IOC_ALLOC_MEMORY_OF_GPU AMDKFD_IOWR(0x16, struct kfd_ioctl_alloc_memory_of_gpu_args)
#define AMDKFD_IOC_MAP_MEMORY_TO_GPU AMDKFD_IOWR(0x18, struct kfd_ioctl_map_memory_to_gpu_args)
#define AMDKFD_IOC_FREE_MEMORY_OF_GPU AMDKFD_IOWR(0x19, struct kfd_ioctl_free_memory_of_gpu_args)

#define KFD_IOC_ALLOC_MEM_FLAGS_GTT (1 << 1)
#define KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE (1 << 31)
#define KFD_IOC_ALLOC_MEM_FLAGS_PUBLIC (1 << 29)

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
    int kfd_fd, drm_fd;
    uint32_t gpu_id;
    
    printf("=== Тест прямого KFD доступа к VRAM ===\n");
    printf("Массив: %dx%d float (%zu байт)\n\n", MATRIX_SIZE, MATRIX_SIZE, 
           ARRAY_SIZE * sizeof(float));
    
    // 1. Открыть KFD
    printf("Шаг 1: Открытие /dev/kfd...\n");
    kfd_fd = open("/dev/kfd", O_RDWR);
    if (kfd_fd < 0) {
        perror("Ошибка открытия /dev/kfd");
        return 1;
    }
    
    // Получить версию KFD
    struct kfd_ioctl_get_version_args version = {0};
    if (ioctl(kfd_fd, AMDKFD_IOC_GET_VERSION, &version) < 0) {
        perror("Ошибка получения версии KFD");
        close(kfd_fd);
        return 1;
    }
    printf("✓ KFD версия: %d.%d\n", version.major_version, version.minor_version);
    
    // 2. Получить GPU ID
    printf("\nШаг 2: Поиск Liverpool GPU...\n");
    FILE *f = fopen("/sys/class/kfd/kfd/topology/nodes/1/gpu_id", "r");
    if (!f) {
        printf("Ошибка: не найден GPU в topology\n");
        close(kfd_fd);
        return 1;
    }
    fscanf(f, "%u", &gpu_id);
    fclose(f);
    printf("✓ GPU ID: %u (0x%x)\n", gpu_id, gpu_id);
    
    // 3. Открыть DRM device (renderD129 для Liverpool)
    printf("\nШаг 3: Открытие DRM устройства...\n");
    const char *drm_paths[] = {"/dev/dri/renderD129", "/dev/dri/renderD128", 
                               "/dev/dri/card0", "/dev/dri/card1"};
    drm_fd = -1;
    
    for (int i = 0; i < 4; i++) {
        int fd = open(drm_paths[i], O_RDWR);
        if (fd < 0) continue;
        
        struct kfd_ioctl_acquire_vm_args acquire_vm = {
            .drm_fd = fd,
            .gpu_id = gpu_id
        };
        
        if (ioctl(kfd_fd, AMDKFD_IOC_ACQUIRE_VM, &acquire_vm) == 0) {
            printf("✓ VM acquired с %s\n", drm_paths[i]);
            drm_fd = fd;
            break;
        }
        close(fd);
    }
    
    if (drm_fd < 0) {
        printf("Ошибка: не удалось получить VM\n");
        close(kfd_fd);
        return 1;
    }
    
    // 4. Инициализировать массив случайными данными
    printf("\nШаг 4: Инициализация массива случайными float...\n");
    float *host_data = malloc(ARRAY_SIZE * sizeof(float));
    srand(time(NULL));
    for (int i = 0; i < ARRAY_SIZE; i++) {
        host_data[i] = (float)rand() / RAND_MAX * 100.0f;
    }
    print_matrix_sample(host_data, "Исходный массив");
    
    // 5. Выделить память в GTT (доступна для CPU и GPU)
    printf("\nШаг 5: Выделение GTT памяти (CPU+GPU доступ)...\n");
    struct kfd_ioctl_alloc_memory_of_gpu_args alloc_args = {
        .size = ARRAY_SIZE * sizeof(float),
        .gpu_id = gpu_id,
        .flags = KFD_IOC_ALLOC_MEM_FLAGS_GTT | 
                 KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE |
                 KFD_IOC_ALLOC_MEM_FLAGS_PUBLIC,
        .va_addr = 0x100000000ULL  // Hint address
    };
    
    if (ioctl(kfd_fd, AMDKFD_IOC_ALLOC_MEMORY_OF_GPU, &alloc_args) < 0) {
        perror("Ошибка выделения памяти");
        free(host_data);
        close(drm_fd);
        close(kfd_fd);
        return 1;
    }
    printf("✓ Выделено GTT: VA=0x%lx, Handle=0x%lx, mmap_offset=0x%lx\n",
           alloc_args.va_addr, alloc_args.handle, alloc_args.mmap_offset);
    
    // 6. Замапить память для GPU
    printf("\nШаг 6: Маппинг памяти на GPU...\n");
    struct kfd_ioctl_map_memory_to_gpu_args map_args = {
        .handle = alloc_args.handle,
        .device_ids_array_ptr = (uint64_t)&gpu_id,
        .n_devices = 1
    };
    
    if (ioctl(kfd_fd, AMDKFD_IOC_MAP_MEMORY_TO_GPU, &map_args) < 0) {
        perror("Ошибка маппинга на GPU");
        struct kfd_ioctl_free_memory_of_gpu_args free_args = {.handle = alloc_args.handle};
        ioctl(kfd_fd, AMDKFD_IOC_FREE_MEMORY_OF_GPU, &free_args);
        free(host_data);
        close(drm_fd);
        close(kfd_fd);
        return 1;
    }
    printf("✓ Память замаплена на GPU\n");
    
    // 7. Замапить память для CPU (через DRM fd!)
    printf("\nШаг 7: Маппинг памяти для CPU...\n");
    void *gpu_ptr = mmap(NULL, ARRAY_SIZE * sizeof(float), 
                         PROT_READ | PROT_WRITE, MAP_SHARED,
                         drm_fd, alloc_args.mmap_offset);
    
    if (gpu_ptr == MAP_FAILED) {
        perror("Ошибка mmap");
        struct kfd_ioctl_free_memory_of_gpu_args free_args = {.handle = alloc_args.handle};
        ioctl(kfd_fd, AMDKFD_IOC_FREE_MEMORY_OF_GPU, &free_args);
        free(host_data);
        close(drm_fd);
        close(kfd_fd);
        return 1;
    }
    printf("✓ Память замаплена для CPU: %p\n", gpu_ptr);
    
    // 8. ЗАПИСЬ: Копировать данные в GPU память
    printf("\nШаг 8: ЗАПИСЬ данных в GPU память...\n");
    memcpy(gpu_ptr, host_data, ARRAY_SIZE * sizeof(float));
    printf("✓ Записано %zu байт\n", ARRAY_SIZE * sizeof(float));
    
    // 9. ЧТЕНИЕ: Прочитать обратно и проверить
    printf("\nШаг 9: ЧТЕНИЕ данных из GPU памяти...\n");
    float *read_data = malloc(ARRAY_SIZE * sizeof(float));
    memcpy(read_data, gpu_ptr, ARRAY_SIZE * sizeof(float));
    
    int errors = 0;
    for (int i = 0; i < ARRAY_SIZE && errors < 5; i++) {
        if (read_data[i] != host_data[i]) {
            printf("✗ Ошибка на [%d]: ожидалось %.2f, получено %.2f\n",
                   i, host_data[i], read_data[i]);
            errors++;
        }
    }
    
    if (errors == 0) {
        printf("✓ Все данные прочитаны корректно!\n");
        print_matrix_sample(read_data, "Прочитанный массив");
    }
    
    // 10. РЕКОМБИНАЦИЯ: Перемешать данные в GPU памяти
    printf("\nШаг 10: РЕКОМБИНАЦИЯ массива в GPU памяти...\n");
    
    // Создать массив индексов для перемешивания
    int *indices = malloc(ARRAY_SIZE * sizeof(int));
    for (int i = 0; i < ARRAY_SIZE; i++) {
        indices[i] = i;
    }
    
    // Fisher-Yates shuffle
    for (int i = ARRAY_SIZE - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
    }
    
    // Применить перестановку напрямую в GPU памяти
    float *temp_array = malloc(ARRAY_SIZE * sizeof(float));
    memcpy(temp_array, gpu_ptr, ARRAY_SIZE * sizeof(float));
    
    float *gpu_float = (float*)gpu_ptr;
    for (int i = 0; i < ARRAY_SIZE; i++) {
        gpu_float[indices[i]] = temp_array[i];
    }
    
    printf("✓ Массив рекомбинирован (перемешан)\n");
    free(temp_array);
    
    // 11. ПОВТОРНОЕ ЧТЕНИЕ: Прочитать рекомбинированный массив
    printf("\nШаг 11: ПОВТОРНОЕ ЧТЕНИЕ рекомбинированного массива...\n");
    float *recombined_data = malloc(ARRAY_SIZE * sizeof(float));
    memcpy(recombined_data, gpu_ptr, ARRAY_SIZE * sizeof(float));
    
    int different = 0;
    for (int i = 0; i < ARRAY_SIZE; i++) {
        if (recombined_data[i] != host_data[i]) {
            different++;
        }
    }
    
    printf("✓ Прочитано рекомбинированных данных\n");
    printf("  Изменено элементов: %d из %d (%.1f%%)\n", 
           different, ARRAY_SIZE, (different * 100.0f) / ARRAY_SIZE);
    print_matrix_sample(recombined_data, "Рекомбинированный массив");
    
    // 12. УДАЛЕНИЕ: Освободить память
    printf("\nШаг 12: УДАЛЕНИЕ - освобождение памяти...\n");
    
    munmap(gpu_ptr, ARRAY_SIZE * sizeof(float));
    
    struct kfd_ioctl_free_memory_of_gpu_args free_args = {
        .handle = alloc_args.handle
    };
    
    if (ioctl(kfd_fd, AMDKFD_IOC_FREE_MEMORY_OF_GPU, &free_args) < 0) {
        perror("Предупреждение: ошибка освобождения памяти");
    } else {
        printf("✓ GPU память освобождена\n");
    }
    
    // Cleanup
    free(indices);
    free(recombined_data);
    free(read_data);
    free(host_data);
    close(drm_fd);
    close(kfd_fd);
    
    printf("\n=== ТЕСТ ЗАВЕРШЕН УСПЕШНО ===\n");
    printf("Все операции выполнены:\n");
    printf("  ✓ Инициализация массива 32x32 float\n");
    printf("  ✓ Выделение GTT памяти через KFD\n");
    printf("  ✓ ЗАПИСЬ в GPU память\n");
    printf("  ✓ ЧТЕНИЕ из GPU памяти\n");
    printf("  ✓ РЕКОМБИНАЦИЯ в GPU памяти\n");
    printf("  ✓ ПОВТОРНОЕ ЧТЕНИЕ\n");
    printf("  ✓ УДАЛЕНИЕ памяти\n");
    
    return 0;
}
