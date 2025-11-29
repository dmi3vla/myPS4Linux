# Проверка флагов оборудования в драйвере AMDKFD

## Обнаруженная проблема

`local_mem_size` показывает 0 в KFD sysfs (`/sys/class/kfd/kfd/topology/nodes/1/properties`).

## Причина

В драйвере `/drivers/gpu/drm/amd/amdgpu/amdgpu_amdkfd.c` функция `amdgpu_amdkfd_get_local_mem_info()` **не заполняет** поля `local_mem_size_public` и `local_mem_size_private` для APU устройств.

### Код в amdgpu_amdkfd.c (строка ~401)

```c
void amdgpu_amdkfd_get_local_mem_info(struct kgd_dev *kgd,
                                     struct kfd_local_mem_info *mem_info)
{
    struct amdgpu_device *adev = (struct amdgpu_device *)kgd;
    
    memset(mem_info, 0, sizeof(*mem_info));  // ← ВСЁ ОБНУЛЯЕТСЯ!
    
    if (adev->gmc.real_vram_size) {
        mem_info->local_mem_size_public = adev->gmc.visible_vram_size;
        mem_info->local_mem_size_private = adev->gmc.real_vram_size -
                                           adev->gmc.visible_vram_size;
    }
    // ← Если real_vram_size == 0, то поля остаются нулевыми!
    
    mem_info->vram_width = adev->gmc.vram_width;
    mem_info->mem_clk_max = amdgpu_dpm_get_mclk(adev, false) / 1000;
}
```

### Проблема для PS4 Pro (APU)

PS4 Pro - это APU (integrated graphics), но с **выделенной GDDR5 памятью** (1GB VRAM). Однако драйвер amdgpu может некорректно определять `real_vram_size` для этого типа устройств, считая их обычным APU без VRAM.

## Диагностические скрипты

### 1. diagnose_kfd_driver.sh

Скрипт автоматически проверяет:
- Структуры kfd_local_mem_info в коде
- Вызовы amdgpu_amdkfd_get_local_mem_info
- Инициализацию для GFX7 (Kaveri/Liverpool/Gladius)
- Актуальные значения из sysfs
- Потенциальные проблемы в коде

```bash
./diagnose_kfd_driver.sh
```

**Ключевые находки:**
```
→ Актуальные значения из KFD Node 1:
  simd_count 112               ✅ Корректно
  array_count 4                ✅ Корректно
  cu_per_simd_array 8          ✅ Корректно
  local_mem_size 0             ❌ ПРОБЛЕМА!
  
→ Проверка обнуления local_mem в CRAT:
2156:  local_mem_info.local_mem_size_private = 0;
```

## Проверка реальных значений VRAM

### Проверить через dmesg

```bash
dmesg | grep -i vram
dmesg | grep -i "gmc"
```

Ожидаемые значения для PS4 Pro:
- `real_vram_size`: ~1GB (1073741824 bytes)
- `visible_vram_size`: ~256MB или больше
- `vram_type`: GDDR5

### Проверить через amdgpu debugfs

```bash
cat /sys/kernel/debug/dri/0/amdgpu_vram_mm
cat /sys/kernel/debug/dri/0/amdgpu_gtt_mm
```

## Где искать проблему

### 1. Инициализация VRAM для GFX7 (gfx701)

Файл: `drivers/gpu/drm/amd/amdgpu/gmc_v7_0.c`

Проверить функцию `gmc_v7_0_mc_init()` - правильно ли определяется VRAM для Liverpool/Gladius.

### 2. Определение типа устройства

Файл: `drivers/gpu/drm/amd/amdgpu/amdgpu_device.c`

PS4 Pro может быть некорректно определен как "pure APU" вместо "APU with discrete VRAM".

### 3. KFD CRAT генерация

Файл: `drivers/gpu/drm/amd/amdkfd/kfd_crat.c` строка 2156

```c
local_mem_info.local_mem_size_private = 0;  // ← Принудительное обнуление для APU?
```

Эта строка принудительно обнуляет private memory для APU. Нужно проверить условие.

## Исправление

### Вариант 1: Патч для amdgpu_amdkfd.c

Добавить special case для Liverpool/Gladius:

```c
void amdgpu_amdkfd_get_local_mem_info(struct kgd_dev *kgd,
                                     struct kfd_local_mem_info *mem_info)
{
    struct amdgpu_device *adev = (struct amdgpu_device *)kgd;
    
    memset(mem_info, 0, sizeof(*mem_info));
    
    // Special handling for PS4/PS4 Pro APU with dedicated VRAM
    if (adev->asic_type == CHIP_LIVERPOOL || adev->asic_type == CHIP_GLADIUS) {
        // PS4 Pro has 1GB GDDR5, treat как discrete VRAM
        mem_info->local_mem_size_public = adev->gmc.visible_vram_size;
        mem_info->local_mem_size_private = adev->gmc.real_vram_size -
                                           adev->gmc.visible_vram_size;
    } else if (adev->gmc.real_vram_size) {
        mem_info->local_mem_size_public = adev->gmc.visible_vram_size;
        mem_info->local_mem_size_private = adev->gmc.real_vram_size -
                                           adev->gmc.visible_vram_size;
    }
    
    mem_info->vram_width = adev->gmc.vram_width;
    mem_info->mem_clk_max = amdgpu_dpm_get_mclk(adev, false) / 1000;
}
```

### Вариант 2: Исправить gmc_v7_0.c

Убедиться что `real_vram_size` правильно инициализируется для Liverpool/Gladius с учетом 1GB GDDR5.

### Вариант 3: Debug вывод

Добавить printk для диагностики:

```c
void amdgpu_amdkfd_get_local_mem_info(struct kgd_dev *kgd,
                                     struct kfd_local_mem_info *mem_info)
{
    struct amdgpu_device *adev = (struct amdgpu_device *)kgd;
    
    printk(KERN_INFO "KFD: asic_type=%d, real_vram=%llu, visible_vram=%llu\n",
           adev->asic_type, adev->gmc.real_vram_size, adev->gmc.visible_vram_size);
    
    // ... rest of function
}
```

Затем проверить:
```bash
dmesg | grep "KFD: asic_type"
```

## Утилиты для проверки

- `./compare_kfd_opencl` - сравнение метрик KFD vs OpenCL
- `./diagnose_kfd_driver.sh` - диагностика драйвера
- `./audit_gpu` - базовая проверка GPU

## Следующие шаги

1. Проверить `dmesg | grep -i vram` - определяется ли VRAM вообще
2. Если VRAM = 0, проблема в `gmc_v7_0.c`
3. Если VRAM > 0, проблема в `amdgpu_amdkfd_get_local_mem_info()`
4. Применить соответствующий патч
5. Пересобрать модуль `amdgpu.ko`
6. Перезагрузить модуль: `rmmod amdgpu && modprobe amdgpu`
7. Проверить результат: `cat /sys/class/kfd/kfd/topology/nodes/1/properties | grep local_mem`
