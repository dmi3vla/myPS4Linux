#!/bin/bash

# ═══════════════════════════════════════════════════════════════
# СКРИПТ ДИАГНОСТИКИ ДРАЙВЕРА AMDKFD ДЛЯ ПРОВЕРКИ ФЛАГОВ
# ═══════════════════════════════════════════════════════════════

echo "═══════════════════════════════════════════════════════════════"
echo "  ДИАГНОСТИКА ДРАЙВЕРА AMDKFD - ПРОВЕРКА ФЛАГОВ ОБОРУДОВАНИЯ"
echo "═══════════════════════════════════════════════════════════════"
echo ""

KERNEL_SRC="/home/noob404/Documents/dev/myPS4Linux"
KFD_DIR="$KERNEL_SRC/drivers/gpu/drm/amd/amdkfd"
AMDGPU_DIR="$KERNEL_SRC/drivers/gpu/drm/amd/amdgpu"
INCLUDE_DIR="$KERNEL_SRC/drivers/gpu/drm/amd/include"

# Проверка существования основных директорий
if [ ! -d "$KERNEL_SRC" ]; then
    echo "❌ Ошибка: Директория ядра не найдена: $KERNEL_SRC"
    exit 1
fi

if [ ! -d "$KFD_DIR" ]; then
    echo "❌ Ошибка: KFD директория не найдена: $KFD_DIR"
    exit 1
fi

if [ ! -d "$AMDGPU_DIR" ]; then
    echo "❌ Ошибка: AMDGPU директория не найдена: $AMDGPU_DIR"
    exit 1
fi

# Функция для поиска определений в коде
search_definition() {
    local term="$1"
    local dir="$2"
    echo "[Поиск: $term]"
    grep -rn "$term" "$dir" --include="*.c" --include="*.h" | head -20
    echo ""
}

# Функция для вывода секции файла
# Использование: show_code_section файл паттерн контекст
# Паттерн поддерживает расширенные regex (через -E)
show_code_section() {
    local file="$1"
    local pattern="$2"
    local context="${3:-10}"  # По умолчанию 10 строк контекста
    
    if [ ! -f "$file" ]; then
        echo "⚠️  Файл не найден: $file"
        return
    fi
    
    echo "[Файл: $(basename "$file")]"
    # Используем -E для расширенных regex (для паттернов с |)
    grep -E -A "$context" -B 2 "$pattern" "$file" 2>/dev/null || echo "Паттерн не найден"
    echo ""
}

echo "═══════════════════════════════════════════════════════════════"
echo "[1] ПРОВЕРКА СТРУКТУР DEVICE INFO"
echo "═══════════════════════════════════════════════════════════════"
echo ""

# Поиск структуры kfd_local_mem_info
echo "→ Структура kfd_local_mem_info:"
# Используем find для корректной обработки wildcard
find "$INCLUDE_DIR" -maxdepth 1 -name "*.h" -exec grep -A 10 "struct kfd_local_mem_info" {} + 2>/dev/null | head -20
echo ""

# Поиск как заполняется local_mem_info
echo "→ Вызовы amdgpu_amdkfd_get_local_mem_info:"
grep -n "amdgpu_amdkfd_get_local_mem_info" "$KFD_DIR"/*.c | head -10
echo ""

echo "═══════════════════════════════════════════════════════════════"
echo "[2] ПРОВЕРКА ИНИЦИАЛИЗАЦИИ ДЛЯ GFX7 (Kaveri/Liverpool)"
echo "═══════════════════════════════════════════════════════════════"
echo ""

# Поиск кода для получения информации о памяти
# Примечание: get_local_mem_info находится в общем файле amdgpu_amdkfd.c
echo "→ Функция get_local_mem_info (общий драйвер):"
show_code_section "$AMDGPU_DIR/amdgpu_amdkfd.c" "amdgpu_amdkfd_get_local_mem_info" 20

echo "→ Инициализация GFX7/CIK устройств:"
show_code_section "$AMDGPU_DIR/amdgpu_amdkfd_gfx_v7.c" "kfd2kgd_funcs" 15

echo "→ Конфигурация памяти в GFX7:"
show_code_section "$AMDGPU_DIR/amdgpu_amdkfd_gfx_v7.c" "vram|local_mem" 10

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
    # Читаем и выводим свойства узла KFD
    if [ -f "$NODE_PATH/properties" ]; then
        grep -E "(local_mem_size|max_allocatable|mem_banks_count|array_count|simd_count|cu_per)" "$NODE_PATH/properties" | while read -r line; do
            echo "  $line"
        done
    else
        echo "⚠️  Файл properties не найден"
    fi
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
echo '   → printk(KERN_INFO "KFD: local_mem_size_public=%llu\n", ...)'
echo "   → Перекомпилируйте модуль и проверьте dmesg | grep KFD"
echo ""

echo "═══════════════════════════════════════════════════════════════"
echo "  ДИАГНОСТИКА ЗАВЕРШЕНА"
echo "═══════════════════════════════════════════════════════════════"
