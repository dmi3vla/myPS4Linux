# Детальный анализ clCreateCommandQueueWithProperties

## Обзор функции

`clCreateCommandQueueWithProperties` - это функция OpenCL 2.0+ для создания очереди команд с расширенными свойствами.

### Сигнатура функции

```c
cl_command_queue clCreateCommandQueueWithProperties(
    cl_context context,                          // Контекст OpenCL
    cl_device_id device,                         // Устройство для очереди
    const cl_queue_properties *properties,       // Массив свойств (пары ключ-значение)
    cl_int *errcode_ret                         // Код ошибки
);
```

## Типы данных

### cl_command_queue
- **Описание**: Непрозрачный указатель на объект очереди команд
- **Используется для**: Постановки команд OpenCL в очередь выполнения на устройстве
- **Освобождение**: `clReleaseCommandQueue()`

### cl_queue_properties
- **Тип**: `cl_ulong` (64-битное беззнаковое целое)
- **Формат**: Массив пар [ключ, значение, ..., 0]
- **Завершение**: Должен заканчиваться нулем (NULL-terminated)

## Свойства очереди (cl_queue_properties)

### CL_QUEUE_PROPERTIES (0x1093)
Основное свойство, определяющее поведение очереди. Значение - битовая маска из:

#### CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE (1 << 0)
- **Значение**: 0x1
- **Описание**: Разрешает выполнение команд вне порядка
- **По умолчанию**: Отключено (in-order execution)
- **Использование**: Для параллельного выполнения независимых команд

#### CL_QUEUE_PROFILING_ENABLE (1 << 1)
- **Значение**: 0x2
- **Описание**: Включает профилирование команд
- **По умолчанию**: Отключено
- **Использование**: Для измерения времени выполнения команд
- **Функции**: `clGetEventProfilingInfo()` для получения времени

#### CL_QUEUE_ON_DEVICE (1 << 2) - OpenCL 2.0+
- **Значение**: 0x4
- **Описание**: Очередь для выполнения на устройстве (device-side queue)
- **Требования**: OpenCL 2.0+, устройство должно поддерживать

#### CL_QUEUE_ON_DEVICE_DEFAULT (1 << 3) - OpenCL 2.0+
- **Значение**: 0x8
- **Описание**: Очередь по умолчанию на устройстве

### CL_QUEUE_SIZE (0x1094) - OpenCL 2.0+
- **Описание**: Размер очереди устройства в байтах
- **Применимо**: Только для device-side queues
- **Тип значения**: cl_uint

## Deprecated API (OpenCL 1.x)

### clCreateCommandQueue (deprecated в OpenCL 2.0)

```c
cl_command_queue clCreateCommandQueue(
    cl_context context,
    cl_device_id device,
    cl_command_queue_properties properties,  // Битовая маска, НЕ массив
    cl_int *errcode_ret
);
```

**Отличия от новой функции:**
- Свойства передаются как битовая маска (cl_command_queue_properties), а не массив
- Поддерживает только CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE и CL_QUEUE_PROFILING_ENABLE
- Более простой интерфейс
- **Работает стабильнее на старых драйверах** (например, AMD APP для gfx701)

## Зависимые функции

### Создание и управление

#### clRetainCommandQueue
```c
cl_int clRetainCommandQueue(cl_command_queue command_queue);
```
- Увеличивает счетчик ссылок на очередь
- Используется для shared ownership

#### clReleaseCommandQueue
```c
cl_int clReleaseCommandQueue(cl_command_queue command_queue);
```
- Уменьшает счетчик ссылок
- Освобождает очередь когда счетчик достигает 0

#### clFlush
```c
cl_int clFlush(cl_command_queue command_queue);
```
- Гарантирует отправку всех команд устройству
- Возвращается немедленно, не ждет завершения

#### clFinish
```c
cl_int clFinish(cl_command_queue command_queue);
```
- Блокируется до завершения всех команд в очереди
- Используется для синхронизации

### Запрос информации

#### clGetCommandQueueInfo
```c
cl_int clGetCommandQueueInfo(
    cl_command_queue command_queue,
    cl_command_queue_info param_name,
    size_t param_value_size,
    void *param_value,
    size_t *param_value_size_ret
);
```

**Параметры запроса:**
- `CL_QUEUE_CONTEXT` - получить контекст очереди
- `CL_QUEUE_DEVICE` - получить устройство очереди
- `CL_QUEUE_REFERENCE_COUNT` - счетчик ссылок
- `CL_QUEUE_PROPERTIES` - свойства очереди
- `CL_QUEUE_SIZE` - размер (для device queues)
- `CL_QUEUE_DEVICE_DEFAULT` - является ли default queue

## Команды очереди

Все эти функции используют command_queue как параметр:

### Память
- `clEnqueueReadBuffer` - чтение из буфера
- `clEnqueueWriteBuffer` - запись в буфер
- `clEnqueueCopyBuffer` - копирование буфера
- `clEnqueueFillBuffer` - заполнение буфера
- `clEnqueueMapBuffer` - отображение буфера в память хоста

### Kernel
- `clEnqueueNDRangeKernel` - запуск kernel
- `clEnqueueTask` - запуск single work-item kernel
- `clEnqueueNativeKernel` - запуск native C функции

### Синхронизация
- `clEnqueueMarker` - маркер в очереди
- `clEnqueueBarrier` - барьер в очереди
- `clEnqueueWaitForEvents` - ожидание событий

## Profiling (при CL_QUEUE_PROFILING_ENABLE)

### clGetEventProfilingInfo
```c
cl_int clGetEventProfilingInfo(
    cl_event event,
    cl_profiling_info param_name,
    size_t param_value_size,
    void *param_value,
    size_t *param_value_size_ret
);
```

**Параметры:**
- `CL_PROFILING_COMMAND_QUEUED` - время постановки в очередь
- `CL_PROFILING_COMMAND_SUBMIT` - время отправки на устройство
- `CL_PROFILING_COMMAND_START` - время начала выполнения
- `CL_PROFILING_COMMAND_END` - время окончания выполнения
- `CL_PROFILING_COMMAND_COMPLETE` - время полного завершения

**Тип времени**: cl_ulong (наносекунды от произвольной точки отсчета)

## События (cl_event)

События создаются при вызове команд очереди:

```c
cl_int clEnqueueNDRangeKernel(
    ...,
    cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list,
    cl_event *event  // Возвращаемое событие
);
```

### Функции для событий
- `clWaitForEvents` - ожидать завершения событий
- `clGetEventInfo` - получить информацию о событии
- `clRetainEvent` / `clReleaseEvent` - управление временем жизни

## Коды ошибок

### При создании очереди
- `CL_SUCCESS` (0) - успешно
- `CL_INVALID_CONTEXT` (-34) - неверный контекст
- `CL_INVALID_DEVICE` (-33) - устройство не в контексте
- `CL_INVALID_VALUE` (-30) - неверные значения свойств
- `CL_INVALID_QUEUE_PROPERTIES` (-35) - неподдерживаемые свойства
- `CL_OUT_OF_RESOURCES` (-5) - недостаточно ресурсов устройства
- `CL_OUT_OF_HOST_MEMORY` (-6) - недостаточно памяти хоста

## Примеры использования

### С новым API (OpenCL 2.0+)

```c
// Пример 1: С профилированием
cl_queue_properties props[] = {
    CL_QUEUE_PROPERTIES, CL_QUEUE_PROFILING_ENABLE,
    0
};
cl_int err;
cl_command_queue queue = clCreateCommandQueueWithProperties(context, device, props, &err);

// Пример 2: Out-of-order + profiling
cl_queue_properties props[] = {
    CL_QUEUE_PROPERTIES, 
    CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_PROFILING_ENABLE,
    0
};

// Пример 3: Без дополнительных свойств
cl_queue_properties props[] = { 0 };  // или NULL
cl_command_queue queue = clCreateCommandQueueWithProperties(context, device, props, &err);
```

### Со старым API (OpenCL 1.x, deprecated но стабильнее)

```c
// С профилированием
cl_int err;
cl_command_queue queue = clCreateCommandQueue(
    context, 
    device, 
    CL_QUEUE_PROFILING_ENABLE,
    &err
);

// Без свойств
cl_command_queue queue = clCreateCommandQueue(context, device, 0, &err);

// Out-of-order + profiling
cl_command_queue queue = clCreateCommandQueue(
    context,
    device,
    CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_PROFILING_ENABLE,
    &err
);
```

## Проблема на gfx701 (PS4 Pro GPU)

### Симптомы
- `clCreateCommandQueueWithProperties` зависает
- Драйвер AMD Accelerated Parallel Processing не полностью поддерживает OpenCL 2.0 API
- `clCreateCommandQueue` (deprecated) работает стабильно

### Решение
Использовать wrapper функцию, которая пробует новый API, а при неудаче fallback на старый:

```c
cl_command_queue safe_create_command_queue(
    cl_context context,
    cl_device_id device,
    cl_bool enable_profiling,
    cl_int *err_code
) {
    cl_command_queue_properties props = 0;
    if (enable_profiling) {
        props = CL_QUEUE_PROFILING_ENABLE;
    }
    
    // Используем старый стабильный API для gfx701
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    cl_int err;
    cl_command_queue queue = clCreateCommandQueue(context, device, props, &err);
    #pragma GCC diagnostic pop
    
    if (err_code) {
        *err_code = err;
    }
    
    return queue;
}
```

## Версии OpenCL и совместимость

| Функция | OpenCL версия | Статус | Поддержка gfx701 |
|---------|--------------|--------|------------------|
| clCreateCommandQueue | 1.0 | Deprecated в 2.0 | ✅ Стабильно |
| clCreateCommandQueueWithProperties | 2.0 | Актуально | ⚠️ Зависает |

## Рекомендации для PS4 Pro (gfx701)

1. ✅ Использовать `clCreateCommandQueue` (deprecated но стабильный)
2. ✅ Включать профилирование через битовую маску
3. ❌ Избегать `clCreateCommandQueueWithProperties`
4. ✅ Всегда проверять коды ошибок
5. ✅ Использовать `clFinish()` для синхронизации
