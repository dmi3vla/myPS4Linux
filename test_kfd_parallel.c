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
#include <pthread.h>

#define MATRIX_SIZE 32
#define ARRAY_SIZE (MATRIX_SIZE * MATRIX_SIZE)
#define NUM_COMPUTE_UNITS 32  // Gladius (PS4 Pro) has 32 CUs (4 SE × 8 CU)
#define WORK_PER_CU (ARRAY_SIZE / NUM_COMPUTE_UNITS)

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

// Структура для передачи данных в поток
typedef struct {
    int cu_id;              // ID compute unit (0-17)
    float *gpu_data;        // Указатель на GPU память
    int start_idx;          // Начальный индекс для этого CU
    int end_idx;            // Конечный индекс
    int *indices;           // Массив индексов для рекомбинации
    pthread_barrier_t *barrier;  // Барьер для синхронизации
} cu_work_t;

// Функция потока, имитирующая работу Compute Unit
void* cu_worker(void *arg) {
    cu_work_t *work = (cu_work_t*)arg;
    
    // Фаза 1: Инициализация (каждый CU обрабатывает свою часть)
    pthread_barrier_wait(work->barrier);
    
    // Фаза 2: Рекомбинация (перемешивание)
    // Каждый CU применяет перестановку к своему участку
    float *temp = malloc((work->end_idx - work->start_idx) * sizeof(float));
    
    for (int i = work->start_idx; i < work->end_idx; i++) {
        temp[i - work->start_idx] = work->gpu_data[i];
    }
    
    pthread_barrier_wait(work->barrier);
    
    // Применяем перестановку
    for (int i = work->start_idx; i < work->end_idx; i++) {
        int new_idx = work->indices[i];
        work->gpu_data[new_idx] = temp[i - work->start_idx];
    }
    
    free(temp);
    pthread_barrier_wait(work->barrier);
    
    return NULL;
}

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
    pthread_t threads[NUM_COMPUTE_UNITS];
    cu_work_t work_items[NUM_COMPUTE_UNITS];
    pthread_barrier_t barrier;
    
    printf("=== Многопоточный тест KFD для Gladius (%d Compute Units) ===\n", NUM_COMPUTE_UNITS);
    printf("Массив: %dx%d float (%zu байт)\n", MATRIX_SIZE, MATRIX_SIZE, 
           ARRAY_SIZE * sizeof(float));
    printf("Работа на CU: %d элементов\n\n", WORK_PER_CU);
    
    // 1. Открыть KFD
    printf("Шаг 1: Открытие /dev/kfd...\n");
    kfd_fd = open("/dev/kfd", O_RDWR);
    if (kfd_fd < 0) {
        perror("Ошибка открытия /dev/kfd");
        return 1;
    }
    
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
    printf("✓ Gladius (PS4 Pro): 32 Compute Units (4 Shader Engines × 8 CU)\n");
    
    // 3. Открыть DRM и получить VM
    printf("\nШаг 3: Открытие DRM устройства...\n");
    const char *drm_paths[] = {"/dev/dri/renderD129", "/dev/dri/renderD128"};
    drm_fd = -1;
    
    for (int i = 0; i < 2; i++) {
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
    
    // 4. Инициализировать массив
    printf("\nШаг 4: Инициализация массива случайными float...\n");
    float *host_data = malloc(ARRAY_SIZE * sizeof(float));
    srand(time(NULL));
    for (int i = 0; i < ARRAY_SIZE; i++) {
        host_data[i] = (float)rand() / RAND_MAX * 100.0f;
    }
    print_matrix_sample(host_data, "Исходный массив");
    
    // 5. Выделить GTT память
    printf("\nШаг 5: Выделение GTT памяти...\n");
    struct kfd_ioctl_alloc_memory_of_gpu_args alloc_args = {
        .size = ARRAY_SIZE * sizeof(float),
        .gpu_id = gpu_id,
        .flags = KFD_IOC_ALLOC_MEM_FLAGS_GTT | 
                 KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE |
                 KFD_IOC_ALLOC_MEM_FLAGS_PUBLIC,
        .va_addr = 0x100000000ULL
    };
    
    if (ioctl(kfd_fd, AMDKFD_IOC_ALLOC_MEMORY_OF_GPU, &alloc_args) < 0) {
        perror("Ошибка выделения памяти");
        free(host_data);
        close(drm_fd);
        close(kfd_fd);
        return 1;
    }
    printf("✓ Выделено GTT: VA=0x%lx, Handle=0x%lx\n",
           alloc_args.va_addr, alloc_args.handle);
    
    // 6. Замапить на GPU
    struct kfd_ioctl_map_memory_to_gpu_args map_args = {
        .handle = alloc_args.handle,
        .device_ids_array_ptr = (uint64_t)&gpu_id,
        .n_devices = 1
    };
    
    if (ioctl(kfd_fd, AMDKFD_IOC_MAP_MEMORY_TO_GPU, &map_args) < 0) {
        perror("Ошибка маппинга на GPU");
        close(drm_fd);
        close(kfd_fd);
        return 1;
    }
    printf("✓ Память замаплена на GPU\n");
    
    // 7. Замапить для CPU
    void *gpu_ptr = mmap(NULL, ARRAY_SIZE * sizeof(float), 
                         PROT_READ | PROT_WRITE, MAP_SHARED,
                         drm_fd, alloc_args.mmap_offset);
    
    if (gpu_ptr == MAP_FAILED) {
        perror("Ошибка mmap");
        close(drm_fd);
        close(kfd_fd);
        return 1;
    }
    printf("✓ Память замаплена для CPU: %p\n", gpu_ptr);
    
    // 8. ЗАПИСЬ в GPU память
    printf("\nШаг 6: ЗАПИСЬ данных в GPU память...\n");
    memcpy(gpu_ptr, host_data, ARRAY_SIZE * sizeof(float));
    printf("✓ Записано %zu байт\n", ARRAY_SIZE * sizeof(float));
    
    // 9. ЧТЕНИЕ и проверка
    printf("\nШаг 7: ЧТЕНИЕ данных из GPU памяти...\n");
    float *read_data = malloc(ARRAY_SIZE * sizeof(float));
    memcpy(read_data, gpu_ptr, ARRAY_SIZE * sizeof(float));
    
    int errors = 0;
    for (int i = 0; i < ARRAY_SIZE && errors < 5; i++) {
        if (read_data[i] != host_data[i]) {
            errors++;
        }
    }
    
    if (errors == 0) {
        printf("✓ Все данные прочитаны корректно!\n");
    }
    free(read_data);
    
    // 10. МНОГОПОТОЧНАЯ РЕКОМБИНАЦИЯ
    printf("\nШаг 8: МНОГОПОТОЧНАЯ РЕКОМБИНАЦИЯ (%d потоков)...\n", NUM_COMPUTE_UNITS);
    
    // Создать массив индексов
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
    
    // Инициализировать барьер
    pthread_barrier_init(&barrier, NULL, NUM_COMPUTE_UNITS);
    
    // Создать рабочие элементы для каждого CU
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < NUM_COMPUTE_UNITS; i++) {
        work_items[i].cu_id = i;
        work_items[i].gpu_data = (float*)gpu_ptr;
        work_items[i].start_idx = i * WORK_PER_CU;
        work_items[i].end_idx = (i == NUM_COMPUTE_UNITS - 1) ? 
                                 ARRAY_SIZE : (i + 1) * WORK_PER_CU;
        work_items[i].indices = indices;
        work_items[i].barrier = &barrier;
        
        pthread_create(&threads[i], NULL, cu_worker, &work_items[i]);
    }
    
    // Ждать завершения всех потоков
    for (int i = 0; i < NUM_COMPUTE_UNITS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + 
                     (end.tv_nsec - start.tv_nsec) / 1e9;
    
    printf("✓ Рекомбинация завершена за %.6f секунд\n", elapsed);
    printf("  Производительность: %.2f GB/s\n", 
           (ARRAY_SIZE * sizeof(float) / 1e9) / elapsed);
    
    pthread_barrier_destroy(&barrier);
    
    // 11. ПОВТОРНОЕ ЧТЕНИЕ
    printf("\nШаг 9: ПОВТОРНОЕ ЧТЕНИЕ рекомбинированного массива...\n");
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
    
    // 12. Cleanup
    printf("\nШаг 10: Освобождение ресурсов...\n");
    
    munmap(gpu_ptr, ARRAY_SIZE * sizeof(float));
    
    struct kfd_ioctl_free_memory_of_gpu_args free_args = {
        .handle = alloc_args.handle
    };
    ioctl(kfd_fd, AMDKFD_IOC_FREE_MEMORY_OF_GPU, &free_args);
    
    free(indices);
    free(recombined_data);
    free(host_data);
    close(drm_fd);
    close(kfd_fd);
    
    printf("✓ Все ресурсы освобождены\n");
    
    printf("\n=== МНОГОПОТОЧНЫЙ ТЕСТ ЗАВЕРШЕН ===\n");
    printf("Статистика:\n");
    printf("  • Compute Units: %d\n", NUM_COMPUTE_UNITS);
    printf("  • Работа на CU: %d элементов\n", WORK_PER_CU);
    printf("  • Время рекомбинации: %.6f сек\n", elapsed);
    printf("  • Пропускная способность: %.2f GB/s\n", 
           (ARRAY_SIZE * sizeof(float) / 1e9) / elapsed);
    
    return 0;
}
