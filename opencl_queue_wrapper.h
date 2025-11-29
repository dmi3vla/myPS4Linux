#ifndef OPENCL_QUEUE_WRAPPER_H
#define OPENCL_QUEUE_WRAPPER_H

#include <CL/cl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * ═══════════════════════════════════════════════════════════════
 * WRAPPER ДЛЯ СОЗДАНИЯ OPENCL ОЧЕРЕДИ КОМАНД
 * ═══════════════════════════════════════════════════════════════
 * 
 * Этот файл содержит wrapper функции для создания OpenCL очереди команд
 * с автоматическим fallback на deprecated API для совместимости.
 *
 * Проблема: clCreateCommandQueueWithProperties (OpenCL 2.0) зависает
 *           на некоторых драйверах (например, AMD APP для gfx701)
 * 
 * Решение: Использовать стабильный deprecated API clCreateCommandQueue
 */

// ═══════════════════════════════════════════════════════════════
// КОНСТАНТЫ И ОПРЕДЕЛЕНИЯ
// ═══════════════════════════════════════════════════════════════

// Свойства очереди (битовая маска для старого API)
typedef enum {
    QUEUE_PROPS_NONE = 0,                                      // Без свойств
    QUEUE_PROPS_OUT_OF_ORDER = CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE,  // 0x1
    QUEUE_PROPS_PROFILING = CL_QUEUE_PROFILING_ENABLE,         // 0x2
    QUEUE_PROPS_OOO_AND_PROF = CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | 
                               CL_QUEUE_PROFILING_ENABLE       // 0x3
} queue_properties_enum;

// Структура для расширенной информации об очереди
typedef struct {
    cl_command_queue queue;          // Сама очередь
    cl_context context;              // Контекст
    cl_device_id device;             // Устройство
    cl_command_queue_properties props;  // Свойства
    cl_bool is_out_of_order;         // Поддержка out-of-order
    cl_bool is_profiling_enabled;    // Включено ли профилирование
} queue_info_t;

// ═══════════════════════════════════════════════════════════════
// БАЗОВЫЕ ФУНКЦИИ СОЗДАНИЯ ОЧЕРЕДИ
// ═══════════════════════════════════════════════════════════════

/**
 * Создать очередь команд используя DEPRECATED API (OpenCL 1.x)
 * Этот API стабильно работает на всех драйверах, включая AMD APP для gfx701
 * 
 * @param context - контекст OpenCL
 * @param device - устройство для очереди
 * @param properties - битовая маска свойств
 * @param err_code - возвращаемый код ошибки (может быть NULL)
 * @return очередь команд или NULL при ошибке
 */
static inline cl_command_queue create_queue_legacy(
    cl_context context,
    cl_device_id device,
    cl_command_queue_properties properties,
    cl_int *err_code
) {
    cl_int err;
    
    // Подавляем warning о deprecated функции
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    cl_command_queue queue = clCreateCommandQueue(context, device, properties, &err);
    #pragma GCC diagnostic pop
    
    if (err_code) {
        *err_code = err;
    }
    
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[QUEUE] clCreateCommandQueue failed: %d\n", err);
        return NULL;
    }
    
    return queue;
}

/**
 * Создать очередь команд используя НОВЫЙ API (OpenCL 2.0+)
 * ВНИМАНИЕ: Может зависнуть на некоторых драйверах!
 * 
 * @param context - контекст OpenCL
 * @param device - устройство для очереди
 * @param properties_array - массив свойств (пары ключ-значение, завершается 0)
 * @param err_code - возвращаемый код ошибки (может быть NULL)
 * @return очередь команд или NULL при ошибке
 */
static inline cl_command_queue create_queue_modern(
    cl_context context,
    cl_device_id device,
    const cl_queue_properties *properties_array,
    cl_int *err_code
) {
    cl_int err;
    cl_command_queue queue = clCreateCommandQueueWithProperties(
        context, device, properties_array, &err
    );
    
    if (err_code) {
        *err_code = err;
    }
    
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[QUEUE] clCreateCommandQueueWithProperties failed: %d\n", err);
        return NULL;
    }
    
    return queue;
}

// ═══════════════════════════════════════════════════════════════
// ВЫСОКОУРОВНЕВЫЕ WRAPPER ФУНКЦИИ
// ═══════════════════════════════════════════════════════════════

/**
 * РЕКОМЕНДУЕМАЯ ФУНКЦИЯ: Создать очередь с простыми параметрами
 * Автоматически использует стабильный deprecated API
 * 
 * @param context - контекст OpenCL
 * @param device - устройство для очереди
 * @param enable_profiling - включить профилирование (CL_TRUE/CL_FALSE)
 * @param enable_out_of_order - включить out-of-order execution (CL_TRUE/CL_FALSE)
 * @param err_code - возвращаемый код ошибки (может быть NULL)
 * @return очередь команд или NULL при ошибке
 */
static inline cl_command_queue create_command_queue_simple(
    cl_context context,
    cl_device_id device,
    cl_bool enable_profiling,
    cl_bool enable_out_of_order,
    cl_int *err_code
) {
    cl_command_queue_properties props = 0;
    
    if (enable_profiling) {
        props |= CL_QUEUE_PROFILING_ENABLE;
    }
    
    if (enable_out_of_order) {
        props |= CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE;
    }
    
    return create_queue_legacy(context, device, props, err_code);
}

/**
 * Создать очередь с профилированием (самый частый случай)
 * 
 * @param context - контекст OpenCL
 * @param device - устройство для очереди
 * @param err_code - возвращаемый код ошибки (может быть NULL)
 * @return очередь команд или NULL при ошибке
 */
static inline cl_command_queue create_profiling_queue(
    cl_context context,
    cl_device_id device,
    cl_int *err_code
) {
    return create_queue_legacy(
        context, device, CL_QUEUE_PROFILING_ENABLE, err_code
    );
}

/**
 * Создать обычную очередь без дополнительных свойств
 * 
 * @param context - контекст OpenCL
 * @param device - устройство для очереди
 * @param err_code - возвращаемый код ошибки (может быть NULL)  
 * @return очередь команд или NULL при ошибке
 */
static inline cl_command_queue create_default_queue(
    cl_context context,
    cl_device_id device,
    cl_int *err_code
) {
    return create_queue_legacy(context, device, 0, err_code);
}

// ═══════════════════════════════════════════════════════════════
// ФУНКЦИИ ДЛЯ ПОЛУЧЕНИЯ ИНФОРМАЦИИ ОБ ОЧЕРЕДИ
// ═══════════════════════════════════════════════════════════════

/**
 * Получить полную информацию об очереди команд
 * 
 * @param queue - очередь команд
 * @param info - структура для заполнения информации
 * @return CL_SUCCESS при успехе, иначе код ошибки
 */
static inline cl_int get_queue_info(cl_command_queue queue, queue_info_t *info) {
    if (!queue || !info) {
        return CL_INVALID_VALUE;
    }
    
    info->queue = queue;
    
    // Получить контекст
    cl_int err = clGetCommandQueueInfo(
        queue, CL_QUEUE_CONTEXT, sizeof(cl_context), &info->context, NULL
    );
    if (err != CL_SUCCESS) return err;
    
    // Получить устройство
    err = clGetCommandQueueInfo(
        queue, CL_QUEUE_DEVICE, sizeof(cl_device_id), &info->device, NULL
    );
    if (err != CL_SUCCESS) return err;
    
    // Получить свойства
    err = clGetCommandQueueInfo(
        queue, CL_QUEUE_PROPERTIES, 
        sizeof(cl_command_queue_properties), &info->props, NULL
    );
    if (err != CL_SUCCESS) return err;
    
    // Разобрать свойства
    info->is_out_of_order = (info->props & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE) != 0;
    info->is_profiling_enabled = (info->props & CL_QUEUE_PROFILING_ENABLE) != 0;
    
    return CL_SUCCESS;
}

/**
 * Вывести информацию об очереди в консоль
 * 
 * @param queue - очередь команд
 */
static inline void print_queue_info(cl_command_queue queue) {
    queue_info_t info;
    cl_int err = get_queue_info(queue, &info);
    
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[QUEUE] Не удалось получить информацию: %d\n", err);
        return;
    }
    
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  ИНФОРМАЦИЯ ОБ ОЧЕРЕДИ КОМАНД\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Очередь:              %p\n", (void*)info.queue);
    printf("Контекст:             %p\n", (void*)info.context);
    printf("Устройство:           %p\n", (void*)info.device);
    printf("Свойства (маска):     0x%llx\n", (unsigned long long)info.props);
    printf("Out-of-order:         %s\n", info.is_out_of_order ? "ДА" : "НЕТ");
    printf("Профилирование:       %s\n", info.is_profiling_enabled ? "ДА" : "НЕТ");
    printf("═══════════════════════════════════════════════════════════════\n");
}

// ═══════════════════════════════════════════════════════════════
// ФУНКЦИИ ДЛЯ ПРОФИЛИРОВАНИЯ
// ═══════════════════════════════════════════════════════════════

/**
 * Получить время выполнения команды из события
 * ТРЕБУЕТ: очередь создана с CL_QUEUE_PROFILING_ENABLE
 * 
 * @param event - событие от выполненной команды
 * @param exec_time_ns - указатель для сохранения времени в наносекундах
 * @return CL_SUCCESS при успехе, иначе код ошибки
 */
static inline cl_int get_event_execution_time(cl_event event, cl_ulong *exec_time_ns) {
    if (!event || !exec_time_ns) {
        return CL_INVALID_VALUE;
    }
    
    cl_ulong start_time, end_time;
    cl_int err;
    
    // Получить время начала
    err = clGetEventProfilingInfo(
        event, CL_PROFILING_COMMAND_START, sizeof(cl_ulong), &start_time, NULL
    );
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[PROFILING] Не удалось получить start time: %d\n", err);
        return err;
    }
    
    // Получить время окончания
    err = clGetEventProfilingInfo(
        event, CL_PROFILING_COMMAND_END, sizeof(cl_ulong), &end_time, NULL
    );
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[PROFILING] Не удалось получить end time: %d\n", err);
        return err;
    }
    
    *exec_time_ns = end_time - start_time;
    return CL_SUCCESS;
}

/**
 * Получить детальную информацию о профилировании события
 * 
 * @param event - событие от выполненной команды
 * @param queued - время постановки в очередь (ns)
 * @param submit - время отправки на устройство (ns)
 * @param start - время начала выполнения (ns)
 * @param end - время окончания выполнения (ns)
 * @return CL_SUCCESS при успехе, иначе код ошибки
 */
static inline cl_int get_event_profiling_details(
    cl_event event,
    cl_ulong *queued,
    cl_ulong *submit,
    cl_ulong *start,
    cl_ulong *end
) {
    cl_int err;
    
    if (queued) {
        err = clGetEventProfilingInfo(
            event, CL_PROFILING_COMMAND_QUEUED, sizeof(cl_ulong), queued, NULL
        );
        if (err != CL_SUCCESS) return err;
    }
    
    if (submit) {
        err = clGetEventProfilingInfo(
            event, CL_PROFILING_COMMAND_SUBMIT, sizeof(cl_ulong), submit, NULL
        );
        if (err != CL_SUCCESS) return err;
    }
    
    if (start) {
        err = clGetEventProfilingInfo(
            event, CL_PROFILING_COMMAND_START, sizeof(cl_ulong), start, NULL
        );
        if (err != CL_SUCCESS) return err;
    }
    
    if (end) {
        err = clGetEventProfilingInfo(
            event, CL_PROFILING_COMMAND_END, sizeof(cl_ulong), end, NULL
        );
        if (err != CL_SUCCESS) return err;
    }
    
    return CL_SUCCESS;
}

/**
 * Вывести детальную информацию о профилировании
 */
static inline void print_event_profiling(cl_event event) {
    cl_ulong queued, submit, start, end;
    
    cl_int err = get_event_profiling_details(event, &queued, &submit, &start, &end);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[PROFILING] Ошибка получения данных: %d\n", err);
        return;
    }
    
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  ПРОФИЛИРОВАНИЕ СОБЫТИЯ\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Queued -> Submit:  %10.3f мкс\n", (submit - queued) / 1000.0);
    printf("Submit -> Start:   %10.3f мкс\n", (start - submit) / 1000.0);
    printf("Start -> End:      %10.3f мкс (время выполнения)\n", (end - start) / 1000.0);
    printf("Total (Q -> End):  %10.3f мкс\n", (end - queued) / 1000.0);
    printf("═══════════════════════════════════════════════════════════════\n");
}

#endif // OPENCL_QUEUE_WRAPPER_H
