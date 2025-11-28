#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#include <CL/cl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

// Размеры матриц
#define N 1024
#define M 1024
#define K 1024

const char *kernelSource = 
"__kernel void matmul(__global float* A, __global float* B, __global float* C, int N, int M, int K) {\n"
"    int i = get_global_id(0);\n"
"    int j = get_global_id(1);\n"
"    \n" 
"    if (i < N && j < K) {\n"
"        float sum = 0.0f;\n"
"        for (int k = 0; k < M; k++) {\n"
"            sum += A[i * M + k] * B[k * K + j];\n"
"        }\n"
"        C[i * K + j] = sum;\n"
"    }\n"
"}\n";

void fill_matrix(float *matrix, int rows, int cols) {
    for (int i = 0; i < rows * cols; i++) {
        matrix[i] = (float)rand() / RAND_MAX;
    }
}

int main() {
    cl_platform_id platform_id = NULL;
    cl_device_id device_id = NULL;
    cl_uint num_devices, num_platforms;
    cl_int ret;
    cl_context context;
    cl_command_queue command_queue;
    cl_program program;
    cl_kernel kernel;
    cl_mem a_mem_obj, b_mem_obj, c_mem_obj;
    size_t global_work_size[2] = {N, K};
    size_t local_work_size[2] = {16, 16}; // Оптимальный размер рабочей группы

    // Создаем временные переменные для размеров
    int n = N;
    int m = M;
    int k = K;

    // Выделение памяти для матриц
    size_t a_size = N * M * sizeof(float);
    size_t b_size = M * K * sizeof(float);
    size_t c_size = N * K * sizeof(float);
    
    float *A = (float *)malloc(a_size);
    float *B = (float *)malloc(b_size);
    float *C = (float *)malloc(c_size);
    float *C_ref = (float *)malloc(c_size);

    // Инициализация матриц случайными значениями
    srand(time(NULL));
    fill_matrix(A, N, M);
    fill_matrix(B, M, K);

    // Эталонное вычисление на CPU
    printf("Выполнение эталонного вычисления на CPU...\n");
    clock_t cpu_start = clock();
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < K; j++) {
            float sum = 0.0f;
            for (int k = 0; k < M; k++) {
                sum += A[i * M + k] * B[k * K + j];
            }
            C_ref[i * K + j] = sum;
        }
    }
    double cpu_time = (double)(clock() - cpu_start) / CLOCKS_PER_SEC;
    printf("Время выполнения на CPU: %.3f секунд\n", cpu_time);

    // Получение платформы и устройства
    ret = clGetPlatformIDs(1, &platform_id, &num_platforms);
    ret = clGetDeviceIDs(platform_id, CL_DEVICE_TYPE_GPU, 1, &device_id, &num_devices);

    // Создание контекста
    context = clCreateContext(NULL, 1, &device_id, NULL, NULL, &ret);

    // Создание очереди команд (современный способ)
    cl_command_queue_properties props = CL_QUEUE_PROFILING_ENABLE;
    command_queue = clCreateCommandQueueWithProperties(context, device_id, &props, &ret);

    // Выделение буферов в памяти устройства
    a_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY, a_size, NULL, &ret);
    b_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY, b_size, NULL, &ret);
    c_mem_obj = clCreateBuffer(context, CL_MEM_WRITE_ONLY, c_size, NULL, &ret);

    // Копирование данных на устройство
    ret = clEnqueueWriteBuffer(command_queue, a_mem_obj, CL_TRUE, 0, a_size, A, 0, NULL, NULL);
    ret = clEnqueueWriteBuffer(command_queue, b_mem_obj, CL_TRUE, 0, b_size, B, 0, NULL, NULL);

    // Создание программы из исходного кода ядра
    program = clCreateProgramWithSource(context, 1, (const char **)&kernelSource, NULL, &ret);
    ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL);

    // Проверка ошибок компиляции ядра
    if (ret != CL_SUCCESS) {
        size_t log_size;
        clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
        char *log = (char *)malloc(log_size);
        clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, log_size, log, NULL);
        printf("Ошибка компиляции ядра:\n%s\n", log);
        free(log);
        return 1;
    }

    // Создание ядра
    kernel = clCreateKernel(program, "matmul", &ret);

    // Установка аргументов ядра
    ret = clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *)&a_mem_obj);
    ret = clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *)&b_mem_obj);
    ret = clSetKernelArg(kernel, 2, sizeof(cl_mem), (void *)&c_mem_obj);
    ret = clSetKernelArg(kernel, 3, sizeof(int), (void *)&n);
    ret = clSetKernelArg(kernel, 4, sizeof(int), (void *)&m);
    ret = clSetKernelArg(kernel, 5, sizeof(int), (void *)&k);

    // Выполнение ядра
    cl_event event;
    printf("Запуск вычислений на GPU...\n");
    ret = clEnqueueNDRangeKernel(command_queue, kernel, 2, NULL, global_work_size, local_work_size, 0, NULL, &event);
    clFinish(command_queue);

    // Измерение времени выполнения
    cl_ulong time_start, time_end;
    clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_START, sizeof(time_start), &time_start, NULL);
    clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END, sizeof(time_end), &time_end, NULL);
    double gpu_time = (time_end - time_start) * 1.0e-6; // в миллисекундах
    printf("Время выполнения на GPU: %.3f мс\n", gpu_time);
    printf("Ускорение: %.2fx\n", cpu_time * 1000 / gpu_time);

    // Чтение результата
    ret = clEnqueueReadBuffer(command_queue, c_mem_obj, CL_TRUE, 0, c_size, C, 0, NULL, NULL);

    // Проверка точности
    printf("Проверка точности...\n");
    float max_error = 0.0f;
    for (int i = 0; i < N * K; i++) {
        float error = fabs(C[i] - C_ref[i]);
        if (error > max_error) {
            max_error = error;
        }
    }
    printf("Максимальная ошибка: %e\n", max_error);

    // Освобождение ресурсов
    ret = clFlush(command_queue);
    ret = clFinish(command_queue);
    ret = clReleaseKernel(kernel);
    ret = clReleaseProgram(program);
    ret = clReleaseMemObject(a_mem_obj);
    ret = clReleaseMemObject(b_mem_obj);
    ret = clReleaseMemObject(c_mem_obj);
    ret = clReleaseCommandQueue(command_queue);
    ret = clReleaseContext(context);

    free(A);
    free(B);
    free(C);
    free(C_ref);

    return 0;
}