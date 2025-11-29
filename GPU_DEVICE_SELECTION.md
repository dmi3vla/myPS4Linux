# Руководство по выбору GPU устройства в OpenCL

## Проблема

При использовании стандартных методов OpenCL без явного указания типа устройства, OpenCL может выбрать CPU вместо GPU для вычислений. Это приводит к:

- GPU показывает 0% нагрузки в `radeontop`
- CPU загружен на 100% на одном ядре в `htop`
- Намного более медленные вычисления

## Причина

В системе может быть несколько OpenCL платформ и устройств:

1. **AMD Accelerated Parallel Processing** - аппаратный драйвер AMD (использует GPU)
2. **Clover** - программный драйвер Mesa (также может использовать GPU)
3. **CPU устройства** - вычисления на процессоре

Если не указать явно `CL_DEVICE_TYPE_GPU` при вызове `clGetDeviceIDs()`, OpenCL может выбрать устройство по умолчанию, которое может быть CPU.

## Решение

### Вариант 1: Использовать helper библиотеку (Рекомендуется)

```c
#include "opencl_gpu_helper.h"

int main() {
    // Автоматически выбрать лучшее GPU устройство
    gpu_device_info_t gpu_info;
    if (select_best_gpu_device(&gpu_info) != 0) {
        fprintf(stderr, "Ошибка: GPU не найден!\n");
        return 1;
    }
    
    // Вывести информацию об устройстве
    print_device_info(&gpu_info);
    
    // Создать контекст для GPU
    cl_int err;
    cl_context context = create_gpu_context(&gpu_info, &err);
    
    // Создать очередь команд для GPU
    cl_command_queue queue = create_gpu_queue(context, &gpu_info, CL_TRUE, &err);
    
    // Теперь все вычисления будут выполняться на GPU!
    // ...
}
```

### Вариант 2: Ручной выбор платформы и устройства

```c
#include <CL/cl.h>
#include <string.h>

int main() {
    cl_int err;
    
    // 1. Получить все платформы
    cl_uint num_platforms;
    clGetPlatformIDs(0, NULL, &num_platforms);
    cl_platform_id *platforms = malloc(sizeof(cl_platform_id) * num_platforms);
    clGetPlatformIDs(num_platforms, platforms, NULL);
    
    // 2. Найти платформу AMD Accelerated Parallel Processing
    cl_platform_id target_platform = NULL;
    for (cl_uint i = 0; i < num_platforms; i++) {
        char name[256];
        clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(name), name, NULL);
        
        if (strstr(name, "AMD Accelerated Parallel Processing")) {
            target_platform = platforms[i];
            break;
        }
    }
    
    if (!target_platform) {
        fprintf(stderr, "AMD APP платформа не найдена!\n");
        return 1;
    }
    
    // 3. ВАЖНО: Указать CL_DEVICE_TYPE_GPU явно!
    cl_device_id device;
    err = clGetDeviceIDs(target_platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "GPU устройство не найдено!\n");
        return 1;
    }
    
    // 4. Создать контекст с привязкой к платформе
    cl_context_properties props[] = {
        CL_CONTEXT_PLATFORM, (cl_context_properties)target_platform,
        0
    };
    
    cl_context context = clCreateContext(props, 1, &device, NULL, NULL, &err);
    cl_command_queue queue = clCreateCommandQueue(context, device, 0, &err);
    
    // Теперь вычисления будут на GPU!
    // ...
}
```

## Что делать НЕПРАВИЛЬНО

### ❌ НЕПРАВИЛЬНО: Использовать CL_DEVICE_TYPE_DEFAULT или CL_DEVICE_TYPE_ALL

```c
// ЭТО МОЖЕТ ВЫБРАТЬ CPU!
clGetDeviceIDs(platform, CL_DEVICE_TYPE_DEFAULT, 1, &device, NULL);

// ЭТО ТОЖЕ МОЖЕТ ВЫБРАТЬ CPU!
clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 1, &device, NULL);
```

### ❌ НЕПРАВИЛЬНО: Не указывать платформу в контексте

```c
// Без указания платформы контекст может быть создан для другой платформы
cl_context context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
```

## Проверка правильности выбора

После запуска программы проверьте:

1. **radeontop** - должна быть видна нагрузка на GPU (например, 50-90%)
2. **htop** - CPU не должен быть загружен на 100% на одном ядре
3. Программа должна выводить название устройства "gfx701" или похожее

## Пример вывода правильной программы

```
═══════════════════════════════════════════════════════════════
  ВЫБРАННОЕ GPU УСТРОЙСТВО
═══════════════════════════════════════════════════════════════
Платформа:      AMD Accelerated Parallel Processing
Устройство:     gfx701
Вычисл. блоки:  28 CUs
Память:         1024 MB
Частота:        100 MHz
═══════════════════════════════════════════════════════════════
```

## Особенности PS4 Pro GPU (gfx701 - Liverpool / Gladius)

- **Архитектура**: GCN 1.1 (Graphics Core Next)
- **Compute Units**: 28 активных (из 36 физических, 8 отключены harvesting)
- **Платформа**: Используйте "AMD Accelerated Parallel Processing" для лучшей производительности
- **Память**: Может показывать 1024 MB или 3072 MB в зависимости от драйвера

## Тестовая программа

Используйте `test_proper_gpu_selection.c` для проверки:

```bash
gcc -o test_proper_gpu_selection test_proper_gpu_selection.c -lOpenCL
./test_proper_gpu_selection
```

Программа автоматически:
- Выберет правильное GPU устройство
- Выведет информацию об устройстве
- Выполнит тестовые вычисления
- Подскажет проверить radeontop и htop

## Дополнительные ресурсы

- Используйте `audit_gpu` для проверки доступных устройств
- Используйте `test_opencl_devices` для просмотра всех платформ и устройств
