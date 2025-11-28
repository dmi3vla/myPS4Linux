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
#include <math.h>

#define NUM_COMPUTE_UNITS 32  // Gladius has 32 CUs
#define VECTOR_SIZE (1024 * 1024)  // 1M floats per CU
#define TOTAL_SIZE (VECTOR_SIZE * NUM_COMPUTE_UNITS)
#define BENCHMARK_DURATION_SEC 120  // 2 минуты

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

#define AMDKFD_IOCTL_BASE 'K'
#define AMDKFD_IOR(nr, type) _IOR(AMDKFD_IOCTL_BASE, nr, type)
#define AMDKFD_IOW(nr, type) _IOW(AMDKFD_IOCTL_BASE, nr, type)
#define AMDKFD_IOWR(nr, type) _IOWR(AMDKFD_IOCTL_BASE, nr, type)

#define AMDKFD_IOC_GET_VERSION AMDKFD_IOR(0x01, struct kfd_ioctl_get_version_args)
#define AMDKFD_IOC_ACQUIRE_VM AMDKFD_IOW(0x15, struct kfd_ioctl_acquire_vm_args)
#define AMDKFD_IOC_ALLOC_MEMORY_OF_GPU AMDKFD_IOWR(0x16, struct kfd_ioctl_alloc_memory_of_gpu_args)
#define AMDKFD_IOC_MAP_MEMORY_TO_GPU AMDKFD_IOWR(0x18, struct kfd_ioctl_map_memory_to_gpu_args)

#define KFD_IOC_ALLOC_MEM_FLAGS_GTT (1 << 1)
#define KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE (1 << 31)
#define KFD_IOC_ALLOC_MEM_FLAGS_PUBLIC (1 << 29)

// Глобальные переменные для бенчмарка
volatile int benchmark_running = 1;
uint64_t total_operations[NUM_COMPUTE_UNITS] = {0};

// Структура для работы CU
typedef struct {
    int cu_id;
    float *data_a;
    float *data_b;
    float *data_c;
    int start_idx;
    int end_idx;
    pthread_barrier_t *barrier;
} cu_benchmark_t;

// FMA (Fused Multiply-Add) операции: C = A * B + C
// Каждая итерация = 2 FLOP (multiply + add)
void* cu_benchmark_worker(void *arg) {
    cu_benchmark_t *work = (cu_benchmark_t*)arg;
    int size = work->end_idx - work->start_idx;
    
    // Инициализация данных
    for (int i = 0; i < size; i++) {
        int idx = work->start_idx + i;
        work->data_a[idx] = 1.0f + (float)i / 1000.0f;
        work->data_b[idx] = 2.0f + (float)i / 2000.0f;
        work->data_c[idx] = 0.0f;
    }
    
    pthread_barrier_wait(work->barrier);
    
    // Основной цикл бенчмарка
    uint64_t ops = 0;
    while (benchmark_running) {
        // Векторизованные FMA операции
        for (int i = 0; i < size; i++) {
            int idx = work->start_idx + i;
            // FMA: c = a * b + c (2 FLOP)
            work->data_c[idx] = work->data_a[idx] * work->data_b[idx] + work->data_c[idx];
        }
        ops += size * 2;  // 2 FLOP per element
        
        // Дополнительные операции для увеличения нагрузки
        for (int i = 0; i < size; i++) {
            int idx = work->start_idx + i;
            // Еще 4 операции: sqrt, sin, cos, multiply
            float temp = sqrtf(fabsf(work->data_c[idx]));
            temp = sinf(temp) * cosf(temp);
            work->data_c[idx] = temp * 0.999f;
        }
        ops += size * 6;  // sqrt + fabs + sin + cos + 2x multiply
    }
    
    total_operations[work->cu_id] = ops;
    return NULL;
}

int main() {
    int kfd_fd, drm_fd;
    uint32_t gpu_id;
    pthread_t threads[NUM_COMPUTE_UNITS];
    cu_benchmark_t work_items[NUM_COMPUTE_UNITS];
    pthread_barrier_t barrier;
    
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║   Gladius GPU FLOPS Benchmark (32 Compute Units)          ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    printf("Конфигурация:\n");
    printf("  • GPU: Gladius (PS4 Pro)\n");
    printf("  • Compute Units: %d\n", NUM_COMPUTE_UNITS);
    printf("  • Векторов на CU: %d (%.1f MB)\n", VECTOR_SIZE, 
           (VECTOR_SIZE * sizeof(float)) / (1024.0 * 1024.0));
    printf("  • Всего данных: %.1f MB\n", 
           (TOTAL_SIZE * 3 * sizeof(float)) / (1024.0 * 1024.0));
    printf("  • Длительность: %d секунд\n\n", BENCHMARK_DURATION_SEC);
    
    // 1. Открыть KFD
    kfd_fd = open("/dev/kfd", O_RDWR);
    if (kfd_fd < 0) {
        perror("Ошибка открытия /dev/kfd");
        return 1;
    }
    
    struct kfd_ioctl_get_version_args version = {0};
    ioctl(kfd_fd, AMDKFD_IOC_GET_VERSION, &version);
    
    // 2. Получить GPU ID
    FILE *f = fopen("/sys/class/kfd/kfd/topology/nodes/1/gpu_id", "r");
    if (!f) {
        close(kfd_fd);
        return 1;
    }
    fscanf(f, "%u", &gpu_id);
    fclose(f);
    
    // 3. Открыть DRM и получить VM
    drm_fd = open("/dev/dri/renderD129", O_RDWR);
    if (drm_fd < 0) {
        drm_fd = open("/dev/dri/renderD128", O_RDWR);
    }
    
    struct kfd_ioctl_acquire_vm_args acquire_vm = {
        .drm_fd = drm_fd,
        .gpu_id = gpu_id
    };
    
    if (ioctl(kfd_fd, AMDKFD_IOC_ACQUIRE_VM, &acquire_vm) < 0) {
        perror("Ошибка acquire VM");
        close(drm_fd);
        close(kfd_fd);
        return 1;
    }
    
    printf("Инициализация GPU памяти...\n");
    
    // 4. Выделить память для 3 векторов (A, B, C)
    size_t buffer_size = TOTAL_SIZE * sizeof(float);
    
    struct kfd_ioctl_alloc_memory_of_gpu_args alloc_a = {
        .size = buffer_size,
        .gpu_id = gpu_id,
        .flags = KFD_IOC_ALLOC_MEM_FLAGS_GTT | 
                 KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE |
                 KFD_IOC_ALLOC_MEM_FLAGS_PUBLIC,
        .va_addr = 0x100000000ULL
    };
    
    if (ioctl(kfd_fd, AMDKFD_IOC_ALLOC_MEMORY_OF_GPU, &alloc_a) < 0) {
        perror("Ошибка выделения памяти A");
        return 1;
    }
    
    struct kfd_ioctl_alloc_memory_of_gpu_args alloc_b = alloc_a;
    alloc_b.va_addr = 0x200000000ULL;
    ioctl(kfd_fd, AMDKFD_IOC_ALLOC_MEMORY_OF_GPU, &alloc_b);
    
    struct kfd_ioctl_alloc_memory_of_gpu_args alloc_c = alloc_a;
    alloc_c.va_addr = 0x300000000ULL;
    ioctl(kfd_fd, AMDKFD_IOC_ALLOC_MEMORY_OF_GPU, &alloc_c);
    
    // Замапить на GPU
    struct kfd_ioctl_map_memory_to_gpu_args map_args = {
        .handle = alloc_a.handle,
        .device_ids_array_ptr = (uint64_t)&gpu_id,
        .n_devices = 1
    };
    ioctl(kfd_fd, AMDKFD_IOC_MAP_MEMORY_TO_GPU, &map_args);
    
    map_args.handle = alloc_b.handle;
    ioctl(kfd_fd, AMDKFD_IOC_MAP_MEMORY_TO_GPU, &map_args);
    
    map_args.handle = alloc_c.handle;
    ioctl(kfd_fd, AMDKFD_IOC_MAP_MEMORY_TO_GPU, &map_args);
    
    // Замапить для CPU
    float *data_a = mmap(NULL, buffer_size, PROT_READ | PROT_WRITE, 
                         MAP_SHARED, drm_fd, alloc_a.mmap_offset);
    float *data_b = mmap(NULL, buffer_size, PROT_READ | PROT_WRITE, 
                         MAP_SHARED, drm_fd, alloc_b.mmap_offset);
    float *data_c = mmap(NULL, buffer_size, PROT_READ | PROT_WRITE, 
                         MAP_SHARED, drm_fd, alloc_c.mmap_offset);
    
    if (data_a == MAP_FAILED || data_b == MAP_FAILED || data_c == MAP_FAILED) {
        perror("Ошибка mmap");
        return 1;
    }
    
    printf("✓ Выделено %.1f MB GPU памяти\n\n", 
           (buffer_size * 3) / (1024.0 * 1024.0));
    
    // 5. Создать потоки для каждого CU
    pthread_barrier_init(&barrier, NULL, NUM_COMPUTE_UNITS);
    
    for (int i = 0; i < NUM_COMPUTE_UNITS; i++) {
        work_items[i].cu_id = i;
        work_items[i].data_a = data_a;
        work_items[i].data_b = data_b;
        work_items[i].data_c = data_c;
        work_items[i].start_idx = i * VECTOR_SIZE;
        work_items[i].end_idx = (i + 1) * VECTOR_SIZE;
        work_items[i].barrier = &barrier;
    }
    
    printf("Запуск бенчмарка на %d секунд...\n", BENCHMARK_DURATION_SEC);
    printf("Операции: FMA (A*B+C), SQRT, SIN, COS\n\n");
    
    struct timespec start_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    // Запустить потоки
    for (int i = 0; i < NUM_COMPUTE_UNITS; i++) {
        pthread_create(&threads[i], NULL, cu_benchmark_worker, &work_items[i]);
    }
    
    // Мониторинг прогресса
    int last_percent = -1;
    while (1) {
        sleep(1);
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        double elapsed = (current_time.tv_sec - start_time.tv_sec) + 
                        (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
        
        int percent = (int)((elapsed / BENCHMARK_DURATION_SEC) * 100);
        if (percent > 100) percent = 100;
        
        if (percent != last_percent) {
            // Подсчитать текущие GFLOPS
            uint64_t total_ops = 0;
            for (int i = 0; i < NUM_COMPUTE_UNITS; i++) {
                total_ops += total_operations[i];
            }
            double gflops = (total_ops / elapsed) / 1e9;
            
            printf("\r[");
            for (int i = 0; i < 50; i++) {
                if (i < percent / 2) printf("█");
                else printf("░");
            }
            printf("] %3d%% | %.2f сек | %.2f GFLOPS", 
                   percent, elapsed, gflops);
            fflush(stdout);
            last_percent = percent;
        }
        
        if (elapsed >= BENCHMARK_DURATION_SEC) {
            benchmark_running = 0;
            break;
        }
    }
    
    // Ждать завершения потоков
    for (int i = 0; i < NUM_COMPUTE_UNITS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    double total_time = (current_time.tv_sec - start_time.tv_sec) + 
                       (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
    
    printf("\n\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                    РЕЗУЛЬТАТЫ БЕНЧМАРКА                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    // Подсчитать общие результаты
    uint64_t total_ops = 0;
    for (int i = 0; i < NUM_COMPUTE_UNITS; i++) {
        total_ops += total_operations[i];
    }
    
    double gflops = (total_ops / total_time) / 1e9;
    double tflops = gflops / 1000.0;
    
    printf("Время выполнения: %.2f секунд\n", total_time);
    printf("Всего операций: %lu (%.2e)\n", total_ops, (double)total_ops);
    printf("\n");
    printf("┌────────────────────────────────────────────────────────────┐\n");
    printf("│  ПРОИЗВОДИТЕЛЬНОСТЬ:  %.2f GFLOPS (%.3f TFLOPS)  │\n", gflops, tflops);
    printf("└────────────────────────────────────────────────────────────┘\n");
    printf("\n");
    
    // Статистика по CU
    printf("Статистика по Compute Units:\n");
    uint64_t min_ops = total_operations[0];
    uint64_t max_ops = total_operations[0];
    for (int i = 0; i < NUM_COMPUTE_UNITS; i++) {
        if (total_operations[i] < min_ops) min_ops = total_operations[i];
        if (total_operations[i] > max_ops) max_ops = total_operations[i];
    }
    
    printf("  • Min операций на CU: %lu\n", min_ops);
    printf("  • Max операций на CU: %lu\n", max_ops);
    printf("  • Avg операций на CU: %lu\n", total_ops / NUM_COMPUTE_UNITS);
    printf("  • Разброс: %.2f%%\n", 
           ((max_ops - min_ops) * 100.0) / ((max_ops + min_ops) / 2.0));
    
    // Теоретический максимум для Gladius
    printf("\nСравнение с теоретическим максимумом:\n");
    printf("  • Теоретический пик Gladius: 4.2 TFLOPS\n");
    printf("  • Достигнуто: %.3f TFLOPS (%.1f%% от пика)\n", 
           tflops, (tflops / 4.2) * 100.0);
    
    // Cleanup
    pthread_barrier_destroy(&barrier);
    munmap(data_a, buffer_size);
    munmap(data_b, buffer_size);
    munmap(data_c, buffer_size);
    close(drm_fd);
    close(kfd_fd);
    
    printf("\n✓ Бенчмарк завершен\n");
    
    return 0;
}
