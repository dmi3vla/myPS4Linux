#!/bin/bash

# ═══════════════════════════════════════════════════════════════
# СКРИПТ ДИАГНОСТИКИ ДРАЙВЕРА AMDKFD ДЛЯ ПРОВЕРКИ ФЛАГОВ
# ═══════════════════════════════════════════════════════════════

echo "═══════════════════════════════════════════════════════════════"
echo "  ДИАГНОСТИКА ДРАЙВЕРА AMDKFD - ПРОВЕРКА ФЛАГОВ ОБОРУДОВАНИЯ"
echo "═══════════════════════════════════════════════════════════════"
echo ""

KERNEL_SRC="/home/noob404/Documents/myPS4Linux"
KFD_DIR="$KERNEL_SRC/drivers/gpu/drm/amd/amdkfd"
AMDGPU_DIR="$KERNEL_SRC/drivers/gpu/drm/amd/amdgpu"

# Функция для поиска определений в коде
search_definition() {
    local term="$1"
    local dir="$2"
    echo "[Поиск: $term]"
    grep -rn "$term" "$dir" --include="*.c" --include="*.h" | head -20
    echo ""
}

# Функция для вывода секции файла
show_code_section() {
    local file="$1"
    local pattern="$2"
    local context="$3"
    
    if [ ! -f "$file" ]; then
        echo "⚠️  Файл не найден: $file"
        return
    fi
    
    echo "[Файл: $(basename $file)]"
    grep -A $context -B 2 "$pattern" "$file" 2>/dev/null || echo "Паттерн не найден"
    echo ""
}

echo "═══════════════════════════════════════════════════════════════"
echo "[1] ПРОВЕРКА СТРУКТУР DEVICE INFO"
echo "═══════════════════════════════════════════════════════════════"
echo ""

# Поиск структуры kfd_local_mem_info
echo "→ Структура kfd_local_mem_info:"
grep -A 10 "struct kfd_local_mem_info" "$KERNEL_SRC/drivers/gpu/drm/amd/include"/*.h 2>/dev/null | head -20
echo ""

# Поиск как заполняется local_mem_info
echo "→ Вызовы amdgpu_amdkfd_get_local_mem_info:"
grep -n "amdgpu_amdkfd_get_local_mem_info" "$KFD_DIR"/*.c | head -10
echo ""

echo "═══════════════════════════════════════════════════════════════"
echo "[2] ПРОВЕРКА ИНИЦИАЛИЗАЦИИ ДЛЯ GFX7 (Kaveri/Liverpool)"
echo "═══════════════════════════════════════════════════════════════"
echo ""

# Поиск кода для GFX7
echo "→ Инициализация GFX7/CIK:"
show_code_section "$AMDGPU_DIR/amdgpu_amdkfd_gfx_v7.c" "get_local_mem_info" 15

echo "→ Конфигурация памяти GFX7:"
show_code_section "$AMDGPU_DIR/amdgpu_amdkfd_gfx_v7.c" "mem_info" 15

echo "═══════════════════════════════════════════════════════════════"
echo "[3] ПРОВЕРКА DEVICE PROPERTIES В TOPOLOGY"
echo "═══════════════════════════════════════════════════════════════"
echo ""

echo "→ Заполнение свойств в kfd_topology.c:"
show_code_section "$KFD_DIR/kfd_topology.c" "local_mem_size" 10

echo "→ Публичная vs приватная память:"
show_code_section "$KFD_DIR/kfd_topology.c" "local_mem_size_private" 5
show_code_section "$KFD_DIR/kfd_topology.c" "local_mem_size_public" 5

echo "═══════════════════════════════════════════════════════════════"
echo "[4] ПРОВЕРКА РЕАЛЬНЫХ ЗНАЧЕНИЙ ИЗ SYSFS"
echo "═══════════════════════════════════════════════════════════════"
echo ""

NODE_PATH="/sys/class/kfd/kfd/topology/nodes/1"

if [ -d "$NODE_PATH" ]; then
    echo "→ Актуальные значения из KFD Node 1:"
    echo ""
    grep -E "(local_mem_size|max_allocatable|mem_banks_count|array_count|simd_count|cu_per)" "$NODE_PATH/properties" | while read line; do
        echo "  $line"
    done
    echo ""
else
    echo "⚠️  KFD Node 1 не найден в sysfs"
fi

echo "═══════════════════════════════════════════════════════════════"
echo "[5] ПОИСК ПРОБЛЕМ В КОДЕ"
echo "═══════════════════════════════════════════════════════════════"
echo ""

echo "→ Проверка обнуления local_mem в CRAT:"
grep -n "local_mem_size_private = 0" "$KFD_DIR/kfd_crat.c"
echo ""

echo "→ Проверка условий для APU (integrated graphics):"
grep -n "integrated" "$AMDGPU_DIR"/amdgpu_amdkfd*.c | head -10
echo ""

echo "═══════════════════════════════════════════════════════════════"
echo "[6] РЕКОМЕНДАЦИИ ПО ИСПРАВЛЕНИЮ"
echo "═══════════════════════════════════════════════════════════════"
echo ""

echo "Если local_mem_size = 0, проверьте:"
echo ""
echo "1. Файл amdgpu_amdkfd_gfx_v7.c - функция get_local_mem_info()"
echo "   → Должна заполнять local_mem_size_public и local_mem_size_private"
echo ""
echo "2. Проверьте определение APU vs дискретной карты"
echo "   → PS4 Pro - это APU, но имеет выделенную VRAM"
echo "   → Может быть некорректно определен тип устройства"
echo ""
echo "3. Проверьте kfd_crat.c строка ~2156"
echo "   → Там может быть принудительное обнуление для APU"
echo ""
echo "4. Добавьте debug вывод в dmesg:"
echo "   → printk(KERN_INFO \"KFD: local_mem_size_public=%llu\\n\", ...)"
echo "   → Перекомпилируйте модуль и проверьте dmesg | grep KFD"
echo ""

echo "═══════════════════════════════════════════════════════════════"
echo "  ДИАГНОСТИКА ЗАВЕРШЕНА"
echo "═══════════════════════════════════════════════════════════════"
