#!/bin/bash

cat << 'EOF'
═══════════════════════════════════════════════════════════════
  ПЕРЕСБОРКА bzimage ДЛЯ PS4 PRO (KEXEC) С ИСПРАВЛЕНИЕМ KFD
═══════════════════════════════════════════════════════════════

ВАЖНО: У вас kexec загрузка с интегрированными драйверами в bzimage!
НЕ нужно собирать loadable modules - нужно пересобрать ядро целиком.

СТАТУС ПАТЧА: ✅ Применен в kfd_topology.c (строка 525)
ИЗМЕНЕНИЕ: local_mem_size теперь динамический вместо 0

═══════════════════════════════════════════════════════════════
  ШАГ 1: УБЕДИТЬСЯ ЧТО ДРАЙВЕРЫ BUILT-IN
═══════════════════════════════════════════════════════════════

# Проверить текущую конфигурацию
cd /home/noob404/Documents/myPS4Linux
grep -E "CONFIG_DRM_AMDGPU|CONFIG_HSA_AMD" .config

Должно быть:
  CONFIG_DRM_AMDGPU=y          # НЕ =m (module)!
  CONFIG_HSA_AMD=y             # НЕ =m!

Если =m - изменить на =y в .config или через menuconfig

═══════════════════════════════════════════════════════════════
  ШАГ 2: ПОЛНАЯ ПЕРЕСБОРКА ЯДРА
═══════════════════════════════════════════════════════════════

cd /home/noob404/Documents/myPS4Linux

# Очистить предыдущие сборки (опционально, но рекомендуется)
make clean

# Убедиться что патч применен
grep -A 5 "Get actual VRAM size" drivers/gpu/drm/amd/amdkfd/kfd_topology.c

# Пересобрать ядро с новым кодом
make -j$(nproc) bzImage

# Ожидайте ~10-30 минут в зависимости от CPU

═══════════════════════════════════════════════════════════════
  ШАГ 3: УСТАНОВКА НОВОГО bzImage
═══════════════════════════════════════════════════════════════

# После успешной сборки bzImage будет тут:
ls -lh arch/x86/boot/bzImage

# РЕЗЕРВНАЯ КОПИЯ ТЕКУЩЕГО ЯДРА (ВАЖНО!)
sudo cp /boot/bzImage /boot/bzImage.backup

# Установить новое ядро
sudo cp arch/x86/boot/bzImage /boot/bzImage

# Или если у вас другой путь для kexec:
# sudo cp arch/x86/boot/bzImage /путь/к/вашему/kexec/bzImage

═══════════════════════════════════════════════════════════════
  ШАГ 4: ПЕРЕЗАГРУЗКА
═══════════════════════════════════════════════════════════════

# Перезагрузиться с новым ядром
sudo reboot

# Или если используете kexec напрямую:
# sudo kexec -l /boot/bzImage --append="$(cat /proc/cmdline)"
# sudo kexec -e

═══════════════════════════════════════════════════════════════
  ШАГ 5: ПРОВЕРКА ПОСЛЕ ЗАГРУЗКИ
═══════════════════════════════════════════════════════════════

# Проверить версию ядра
uname -r

# Проверить что VRAM инициализирован
dmesg | grep -i "VRAM:"

# ГЛАВНАЯ ПРОВЕРКА: local_mem_size теперь НЕ 0!
cat /sys/class/kfd/kfd/topology/nodes/1/properties | grep local_mem_size

ОЖИДАЕМЫЙ РЕЗУЛЬТАТ:
  local_mem_size 1073741824    # 1024 MB!

БЫЛО (до патча):
  local_mem_size 0             # ← проблема!

# Запустить полную диагностику
cd /home/noob404/Documents/myPS4Linux
./check_vram_kfd.sh

# Проверить OpenCL
./compare_kfd_opencl

═══════════════════════════════════════════════════════════════
  ШАГ 6: ТЕСТИРОВАНИЕ OPENCL
═══════════════════════════════════════════════════════════════

# Запустить OpenCL тест
./test_queue_wrapper

# В отдельном терминале - мониторинг GPU
radeontop

# В отдельном терминале - мониторинг CPU
htop

ОЖИДАЕМОЕ ПОВЕДЕНИЕ:
  ✅ radeontop показывает активность GPU
  ✅ htop НЕ показывает 100% на одном ядре
  ✅ OpenCL программы работают быстрее

═══════════════════════════════════════════════════════════════
  ОТКАТ (если что-то не так)
═══════════════════════════════════════════════════════════════

# Загрузиться с резервным ядром
sudo cp /boot/bzImage.backup /boot/bzImage
sudo reboot

# Или указать kexec на старый bzImage
# sudo kexec -l /boot/bzImage.backup --append="$(cat /proc/cmdline)"
# sudo kexec -e

═══════════════════════════════════════════════════════════════
  БЫСТРАЯ ПРОВЕРКА СБОРКИ
═══════════════════════════════════════════════════════════════

EOF

echo ""
echo "ТЕКУЩИЙ СТАТУС:"
echo "─────────────────────────────────────────"

# Проверка что патч применен
if grep -q "Get actual VRAM size" drivers/gpu/drm/amd/amdkfd/kfd_topology.c 2>/dev/null; then
    echo "✅ Патч применен в исходном коде"
else
    echo "❌ Патч НЕ применен"
    exit 1
fi

# Проверка конфигурации built-in
if [ -f .config ]; then
    AMDGPU_CFG=$(grep "^CONFIG_DRM_AMDGPU=" .config)
    HSA_CFG=$(grep "^CONFIG_HSA_AMD=" .config)
    
    echo ""
    echo "Конфигурация драйверов:"
    echo "  $AMDGPU_CFG"
    echo "  $HSA_CFG"
    
    if echo "$AMDGPU_CFG" | grep -q "=y"; then
        echo "  ✅ AMDGPU built-in (правильно для kexec)"
    else
        echo "  ⚠️  AMDGPU не built-in - измените CONFIG_DRM_AMDGPU=y"
    fi
else
    echo "⚠️  .config не найден - сконфигурируйте ядро: make menuconfig"
fi

echo ""
echo "СЛЕДУЮЩИЙ ШАГ:"
echo "  make -j$(nproc) bzImage"
echo ""
