# Пересборка ядра с обновлённой конфигурацией Gladius

## Изменения

**Файл**: `drivers/gpu/drm/amd/amdgpu/gfx_v7_0.c`

```c
case CHIP_GLADIUS:
    adev->gfx.config.max_cu_per_sh = 7;  // 28 CU (было 8)
```

**Причина**: OpenCL детектирует 28 активных CU, а не 32. Это harvesting (4 CU отключены).

---

## Команды для пересборки

```bash
cd ~/Documents/myPS4Linux

# Сборка ядра
make -j$(nproc)

# Установка модулей
sudo make modules_install

# Установка ядра
sudo make install

# Обновление GRUB
sudo update-grub

# Перезагрузка
sudo reboot
```

---

## Проверка после перезагрузки

```bash
# Проверить количество CU
cat /sys/class/kfd/kfd/topology/nodes/1/properties | grep simd_count
# Должно быть: simd_count 112 (28 CU × 4 SIMD)

# Проверить OpenCL
cd ~/Documents/myPS4Linux
./test_opencl_devices
# Должно показать: Compute Units: 28
```

---

## Готово!

После перезагрузки OpenCL будет работать с правильной конфигурацией 28 CU.
