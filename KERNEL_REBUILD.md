# Пересборка ядра с поддержкой Gladius (PS4 Pro)

## Внесённые изменения

1. **`drivers/gpu/drm/amd/amdgpu/gfx_v7_0.c`**:
   - Установлено `max_cu_per_sh = 9`.
   - Конфигурация: 4 SE × 1 SH × 9 CU = **36 Total CU**.
   - Активных: **32 CU** (4 отключены/harvested).
   - Это позволит использовать все доступные вычислительные блоки.

2. **`drivers/gpu/drm/amd/amdgpu/cik.c`**:
   - Включён **PowerPlay (DPM)** для Gladius.
   - Добавлен `kv_smu_ip_block` (как для Kaveri APU).
   - Это позволит динамически менять частоту GPU (SCLK) и памяти (MCLK).

---

## Инструкция по пересборке

Выполните следующие команды в терминале:

```bash
cd ~/Documents/dev/myPS4Linux

# 1. Сборка ядра (используя все ядра CPU)
make -j$(nproc)

# 2. Установка модулей
sudo make modules_install

# 3. Установка ядра
sudo make install

# 4. Обновление initramfs и GRUB (обычно делается автоматически make install, но для надежности)
sudo update-initramfs -u
sudo update-grub

# 5. Перезагрузка
sudo reboot
```

---

## Проверка после перезагрузки

### 1. Проверка частоты (DPM)
Запустите скрипт мониторинга:
```bash
cd ~/Documents/dev/myPS4Linux
chmod +x monitor_gpu.sh
./monitor_gpu.sh
```
Должны отображаться текущие частоты SCLK/MCLK и загрузка.

### 2. Проверка количества CU
```bash
cat /sys/class/kfd/kfd/topology/nodes/1/properties | grep simd_count
```
Ожидается: `simd_count 128` (32 CU × 4 SIMD) или `144` (36 CU × 4 SIMD).
Ранее было 112 (28 CU).

### 3. Тест производительности (FLOPS)
Скомпилируйте и запустите тест (требуется доработка загрузчика):
```bash
./compile_flops.sh
# Запуск через KFD (потребуется дополнительная утилита)
```
