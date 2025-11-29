# Проверка соответствия метрик KFD и OpenCL

## Описание

Утилита `compare_kfd_opencl` сравнивает метрики топологии GPU из KFD (kernel-level) с данными OpenCL (userspace) чтобы убедиться, что они соответствуют друг другу.

## Компиляция и запуск

```bash
gcc -o compare_kfd_opencl compare_kfd_opencl.c -lOpenCL
./compare_kfd_opencl
```

## Что проверяется

### 1. Compute Units (CUs)
- **KFD**: Читает из `/sys/class/kfd/kfd/topology/nodes/*/properties`
- **Расчет**: `simd_count / 4` (для GCN архитектуры)
- **OpenCL**: `CL_DEVICE_MAX_COMPUTE_UNITS`
- **Ожидается**: Совпадение (28 CUs для gfx701)

### 2. Частота (Clock Frequency)
- **KFD**: `max_engine_clk_fcompute` и `max_engine_clk_ccompute`
- **OpenCL**: `CL_DEVICE_MAX_CLOCK_FREQUENCY`
- **Особенность**: OpenCL может показывать либо FCompute, либо CCompute
- **AMD APP**: 100 MHz (FCompute)
- **Clover**: 914 MHz (реальная частота GPU)

### 3. Local Memory
- **KFD**: `local_mem_size` (часто не заполняется корректно)
- **OpenCL**: `CL_DEVICE_LOCAL_MEM_SIZE`
- **AMD APP**: 64 KB
- **Clover**: 32 KB

### 4. Global Memory
- **KFD**: `max_allocatable_memory`
- **OpenCL**: `CL_DEVICE_GLOBAL_MEM_SIZE`
- **AMD APP**: 1024 MB (VRAM)
- **Clover**: 3072 MB (system RAM)

## Результаты для PS4 Pro (gfx701)

### ✅ Совпадают
- **Compute Units**: 28 CUs (обе платформы)
- **Частота**: В зависимости от платформы

### ⚠️ Различаются
- **Local Memory**: KFD часто показывает 0 KB
- **Global Memory**: KFD показывает 0 MB (не заполняется)
- **Частота Clover**: Показывает 914 MHz вместо 100 MHz из KFD

## Важные метрики

| Параметр | KFD | AMD APP | Clover |
|----------|-----|---------|--------|
| Compute Units | 28 | 28 ✅ | 28 ✅ |
| SIMD Count | 112 | - | - |
| Частота | 100 MHz | 100 MHz ✅ | 914 MHz ⚠️ |
| Local Mem | 0 KB | 64 KB | 32 KB |
| Global Mem | 0 MB | 1024 MB | 3072 MB |

## Интерпретация результатов

### ✅ Нормально
- **CU совпадают**: OpenCL правильно определяет количество вычислительных блоков
- **Частота AMD APP = KFD FCompute**: Корректное определение частоты

### ⚠️ Ожидаемые расхождения
- **KFD Local/Global Memory = 0**: Эти поля не всегда заполняются драйвером KFD
- **Clover частота выше**: Mesa OpenCL может использовать другой источник информации
- **Разная Global Memory между платформами**: 
  - AMD APP → VRAM (1024 MB)
  - Clover → System RAM (3072 MB)

### ❌ Проблемы
- **CU не совпадают**: OpenCL видит меньше CU чем KFD
- **Частота сильно отличается**: Возможна проблема с драйвером

## Дополнительные параметры KFD

Утилита также читает:
- `max_waves_per_simd` - максимум волн на SIMD (10 для gfx701)
- `simd_arrays_per_engine` - SIMD массивов на движок
- `num_shader_arrays` - количество shader массивов
- `cu_per_simd_array` - CU на массив (8 для gfx701)

## Полезные команды

### Вручную проверить KFD топологию
```bash
cat /sys/class/kfd/kfd/topology/nodes/1/properties
```

### Проверить GPU ID
```bash
cat /sys/class/kfd/kfd/topology/nodes/1/gpu_id
```

### Посмотреть все узлы
```bash
ls /sys/class/kfd/kfd/topology/nodes/
```

## Связанные утилиты

- `audit_gpu` - базовая проверка KFD vs OpenCL
- `test_opencl_devices` - список всех OpenCL устройств
- `compare_kfd_opencl` - детальное сравнение метрик (эта утилита)
