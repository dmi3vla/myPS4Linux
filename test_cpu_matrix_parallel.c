#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

// Utility functions
double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

void print_header(const char *title) {
    printf("\n╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║ %-66s ║\n", title);
    printf("╚════════════════════════════════════════════════════════════════════╝\n");
}

void print_section(const char *title) {
    printf("\n═══ %s ═══\n", title);
}

// Получить количество CPU ядер
int get_cpu_cores() {
    return sysconf(_SC_NPROCESSORS_ONLN);
}

// Структура для передачи данных в потоки
typedef struct {
    float *a;
    float *b;
    float *c;
    size_t start;
    size_t end;
    unsigned int iterations;
} fma_thread_data_t;

typedef struct {
    const float *A;
    const float *B;
    float *C;
    unsigned int size;
    unsigned int start_row;
    unsigned int end_row;
} matrix_thread_data_t;

// FMA kernel для CPU (один поток)
void* fma_stress_worker(void *arg) {
    fma_thread_data_t *data = (fma_thread_data_t*)arg;
    
    for (size_t i = data->start; i < data->end; i++) {
        float val_a = data->a[i];
        float val_b = data->b[i];
        float val_c = data->c[i];
        
        // Выполняем iterations операций (аналог OpenCL версии)
        for (unsigned int iter = 0; iter < data->iterations; iter++) {
            val_c = val_a * val_b + val_c;
            val_c = val_a * val_b + val_c;
            val_c = val_a * val_b + val_c;
            val_c = val_a * val_b + val_c;
        }
        
        data->c[i] = val_c;
    }
    
    return NULL;
}

// Matrix multiply kernel для CPU (один поток)
void* matrix_multiply_worker(void *arg) {
    matrix_thread_data_t *data = (matrix_thread_data_t*)arg;
    
    for (unsigned int row = data->start_row; row < data->end_row; row++) {
        for (unsigned int col = 0; col < data->size; col++) {
            float sum = 0.0f;
            for (unsigned int k = 0; k < data->size; k++) {
                sum += data->A[row * data->size + k] * data->B[k * data->size + col];
            }
            data->C[row * data->size + col] = sum;
        }
    }
    
    return NULL;
}

// Проверка CPU информации
void check_cpu_info() {
    print_section("ИНФОРМАЦИЯ О ПРОЦЕССОРЕ");
    
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        char line[256];
        int core_count = 0;
        char model_name[256] = "Unknown";
        
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "processor", 9) == 0) {
                core_count++;
            }
            if (strncmp(line, "model name", 10) == 0) {
                char *colon = strchr(line, ':');
                if (colon) {
                    sscanf(colon + 2, "%[^\n]", model_name);
                }
            }
        }
        fclose(fp);
        
        printf("CPU Model: %s\n", model_name);
        printf("CPU Cores: %d\n", core_count);
    }
    
    // Проверка CPU governor
    fp = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "r");
    if (fp) {
        char governor[64];
        if (fgets(governor, sizeof(governor), fp)) {
            printf("CPU Governor: %s", governor);
        }
        fclose(fp);
    }
}

// FMA Stress Test для разогрева CPU
void run_fma_stress_test(int num_threads) {
    print_section("FMA STRESS TEST (РАЗОГРЕВ CPU)");
    
    const size_t n = 4 * 1024 * 1024; // 4M элементов
    const unsigned int iterations = 1000;
    
    printf("Запуск %u итераций на %zu элементов\n", iterations, n);
    printf("Используется %d потоков...\n", num_threads);
    
    // Выделить память
    float *a = (float*)malloc(n * sizeof(float));
    float *b = (float*)malloc(n * sizeof(float));
    float *c = (float*)malloc(n * sizeof(float));
    
    // Инициализировать данные
    for (size_t i = 0; i < n; i++) {
        a[i] = 1.0f;
        b[i] = 2.0f;
        c[i] = 0.0f;
    }
    
    // Создать потоки
    pthread_t *threads = (pthread_t*)malloc(num_threads * sizeof(pthread_t));
    fma_thread_data_t *thread_data = (fma_thread_data_t*)malloc(num_threads * sizeof(fma_thread_data_t));
    
    size_t chunk_size = n / num_threads;
    
    // Запуск с измерением времени
    double start = get_time();
    
    for (int t = 0; t < num_threads; t++) {
        thread_data[t].a = a;
        thread_data[t].b = b;
        thread_data[t].c = c;
        thread_data[t].start = t * chunk_size;
        thread_data[t].end = (t == num_threads - 1) ? n : (t + 1) * chunk_size;
        thread_data[t].iterations = iterations;
        
        pthread_create(&threads[t], NULL, fma_stress_worker, &thread_data[t]);
    }
    
    // Ждать завершения всех потоков
    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
    }
    
    double end = get_time();
    double elapsed = end - start;
    
    // Вычислить производительность
    // Каждая итерация: 4 операции, каждая = 2 FLOP (умножение + сложение)
    double total_flops = (double)n * iterations * 4 * 2;
    double gflops = total_flops / elapsed / 1e9;
    
    printf("Время: %.3f сек\n", elapsed);
    printf("Производительность: %.2f GFLOPS (%.3f TFLOPS)\n", gflops, gflops/1000.0);
    
    // Cleanup
    free(a);
    free(b);
    free(c);
    free(threads);
    free(thread_data);
}

// Тест умножения матриц
void run_matrix_test(unsigned int size, int num_runs, int num_threads) {
    printf("\nMatrix Size: %u × %u (%.1f MB per matrix)\n", 
           size, size, (size * size * sizeof(float)) / (1024.0 * 1024.0));
    
    size_t bytes = size * size * sizeof(float);
    
    // Выделить память
    float *A = (float*)malloc(bytes);
    float *B = (float*)malloc(bytes);
    float *C = (float*)malloc(bytes);
    
    // Инициализировать матрицы случайными значениями
    srand(time(NULL));
    for (unsigned int i = 0; i < size * size; i++) {
        A[i] = (float)rand() / RAND_MAX;
        B[i] = (float)rand() / RAND_MAX;
    }
    
    // Создать потоки
    pthread_t *threads = (pthread_t*)malloc(num_threads * sizeof(pthread_t));
    matrix_thread_data_t *thread_data = (matrix_thread_data_t*)malloc(num_threads * sizeof(matrix_thread_data_t));
    
    unsigned int rows_per_thread = size / num_threads;
    
    // Прогрев
    for (int t = 0; t < num_threads; t++) {
        thread_data[t].A = A;
        thread_data[t].B = B;
        thread_data[t].C = C;
        thread_data[t].size = size;
        thread_data[t].start_row = t * rows_per_thread;
        thread_data[t].end_row = (t == num_threads - 1) ? size : (t + 1) * rows_per_thread;
        
        pthread_create(&threads[t], NULL, matrix_multiply_worker, &thread_data[t]);
    }
    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
    }
    
    // Замеры времени
    double times[10];
    double min_time = 1e9, max_time = 0, avg_time = 0;
    
    for (int run = 0; run < num_runs; run++) {
        double start = get_time();
        
        for (int t = 0; t < num_threads; t++) {
            pthread_create(&threads[t], NULL, matrix_multiply_worker, &thread_data[t]);
        }
        for (int t = 0; t < num_threads; t++) {
            pthread_join(threads[t], NULL);
        }
        
        double end = get_time();
        
        times[run] = (end - start) * 1000.0; // в миллисекундах
        avg_time += times[run];
        if (times[run] < min_time) min_time = times[run];
        if (times[run] > max_time) max_time = times[run];
    }
    avg_time /= num_runs;
    
    // Вычислить производительность
    double flops = 2.0 * size * size * size;
    double gflops = flops / (avg_time / 1000.0) / 1e9;
    
    // Bandwidth: 3 матрицы * size^2 * 4 bytes
    double bytes_transferred = 3.0 * size * size * sizeof(float);
    double bandwidth_gb = bytes_transferred / (avg_time / 1000.0) / 1e9;
    
    printf("Время (avg/min/max): %.3f / %.3f / %.3f мс\n", 
           avg_time, min_time, max_time);
    printf("Производительность: %.2f GFLOPS\n", gflops);
    printf("Bandwidth: %.2f GB/s\n", bandwidth_gb);
    
    // Проверка корректности (CPU вычисление для первого элемента)
    float expected = 0.0f;
    for (unsigned int k = 0; k < size; k++) {
        expected += A[k] * B[k * size];
    }
    
    if (fabs(C[0] - expected) < 0.01) {
        printf("✓ Результат корректен (C[0][0] = %.4f)\n", C[0]);
    } else {
        printf("✗ Ошибка: C[0][0] = %.4f, ожидалось %.4f\n", C[0], expected);
    }
    
    // Cleanup
    free(A);
    free(B);
    free(C);
    free(threads);
    free(thread_data);
}

// Тест цепочки умножений матриц
void run_kernel_chain_test(int num_threads) {
    print_section("ЦЕПОЧКА ОПЕРАЦИЙ (MULTI-THREADED)");
    
    const unsigned int size = 512;
    const int num_kernels = 4;
    
    printf("Запуск цепочки из %d матричных умножений (%ux%u)\n", 
           num_kernels, size, size);
    printf("Используется %d потоков...\n", num_threads);
    
    size_t bytes = size * size * sizeof(float);
    
    // Создать буферы
    float *A = (float*)malloc(bytes);
    float *B = (float*)malloc(bytes);
    float *C = (float*)malloc(bytes);
    
    // Инициализировать
    for (unsigned int i = 0; i < size * size; i++) {
        A[i] = (float)rand() / RAND_MAX;
        B[i] = (float)rand() / RAND_MAX;
    }
    
    // Создать потоки
    pthread_t *threads = (pthread_t*)malloc(num_threads * sizeof(pthread_t));
    matrix_thread_data_t *thread_data = (matrix_thread_data_t*)malloc(num_threads * sizeof(matrix_thread_data_t));
    
    unsigned int rows_per_thread = size / num_threads;
    
    for (int t = 0; t < num_threads; t++) {
        thread_data[t].A = A;
        thread_data[t].B = B;
        thread_data[t].C = C;
        thread_data[t].size = size;
        thread_data[t].start_row = t * rows_per_thread;
        thread_data[t].end_row = (t == num_threads - 1) ? size : (t + 1) * rows_per_thread;
    }
    
    printf("Время по каждой операции:\n");
    
    double wall_start = get_time();
    double total_cpu_time = 0;
    
    for (int i = 0; i < num_kernels; i++) {
        double start = get_time();
        
        for (int t = 0; t < num_threads; t++) {
            pthread_create(&threads[t], NULL, matrix_multiply_worker, &thread_data[t]);
        }
        for (int t = 0; t < num_threads; t++) {
            pthread_join(threads[t], NULL);
        }
        
        double end = get_time();
        
        double kernel_time = (end - start) * 1000.0;
        total_cpu_time += kernel_time;
        printf("  Operation %d: %.2f мс\n", i, kernel_time);
    }
    
    double wall_end = get_time();
    double wall_time = (wall_end - wall_start) * 1000.0;
    
    printf("Wall time: %.3f мс\n", wall_time);
    printf("Сумма CPU time: %.3f мс\n", total_cpu_time);
    
    // Вычислить производительность для всей цепочки
    double total_flops = 2.0 * size * size * size * num_kernels;
    double gflops = total_flops / (total_cpu_time / 1000.0) / 1e9;
    printf("Производительность: %.2f GFLOPS\n", gflops);
    
    // Cleanup
    free(A);
    free(B);
    free(C);
    free(threads);
    free(thread_data);
}

int main() {
    print_header("CPU PARALLEL MATRIX MULTIPLICATION TEST");
    
    // Получить информацию о CPU
    check_cpu_info();
    
    int num_threads = get_cpu_cores();
    printf("\n✓ Используется %d потоков (CPU cores)\n", num_threads);
    
    // Запустить FMA стресс-тест для разогрева
    run_fma_stress_test(num_threads);
    
    // Тесты умножения матриц
    print_section("ТЕСТ УМНОЖЕНИЯ МАТРИЦ");
    run_matrix_test(512, 5, num_threads);
    run_matrix_test(1024, 5, num_threads);
    run_matrix_test(2048, 3, num_threads);
    
    // Тест цепочки операций
    run_kernel_chain_test(num_threads);
    
    print_header("ТЕСТ ЗАВЕРШЕН");
    printf("✓ Все тесты выполнены успешно\n");
    
    return 0;
}
