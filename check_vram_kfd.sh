#!/bin/bash

# Скрипт для проверки значений VRAM в системе

echo "═══════════════════════════════════════════════════════════════"
echo "  ПРОВЕРКА VRAM И KFD LOCAL MEMORY"
echo "═══════════════════════════════════════════════════════════════"
echo ""

echo "[1] VRAM из dmesg (инициализация драйвера):"
echo "───────────────────────────────────────────────────────────────"
dmesg | grep -i "VRAM:" | tail -5
echo ""

echo "[2] VRAM aperture (BAR mapping):"
echo "───────────────────────────────────────────────────────────────"
dmesg | grep -i "vram apper" | tail -2
echo ""

echo "[3] Detected VRAM конфигурация:"
echo "───────────────────────────────────────────────────────────────"
dmesg | grep -i "Detected VRAM" | tail -2
echo ""

echo "[4] KFD инициализация:"
echo "───────────────────────────────────────────────────────────────"
dmesg | grep -i "kfd" | grep -i "init\|probe\|add" | tail -10
echo ""

echo "[5] KFD sysfs - local_mem_size:"
echo "───────────────────────────────────────────────────────────────"
if [ -f "/sys/class/kfd/kfd/topology/nodes/1/properties" ]; then
    grep "local_mem_size" /sys/class/kfd/kfd/topology/nodes/1/properties
else
    echo "⚠️  KFD node 1 properties не найдены"
fi
echo ""

echo "[6] Debugfs VRAM info (если доступен):"
echo "───────────────────────────────────────────────────────────────"
if [ -f "/sys/kernel/debug/dri/0/amdgpu_vram_mm" ]; then
    head -20 /sys/kernel/debug/dri/0/amdgpu_vram_mm
else
    echo "⚠️  debugfs недоступен (требуются root права или CONFIG_DEBUG_FS)"
fi
echo ""

echo "═══════════════════════════════════════════════════════════════"
echo "  ДИАГНОСТИКА"
echo "═══════════════════════════════════════════════════════════════"
echo ""

VRAM_SIZE=$(dmesg | grep -i "VRAM:" | tail -1 | grep -oP '1024M|2048M|4096M|512M' | head -1)
KFD_LOCAL_MEM=$(grep "local_mem_size" /sys/class/kfd/kfd/topology/nodes/1/properties 2>/dev/null | awk '{print $2}')

if [ -n "$VRAM_SIZE" ]; then
    echo "✅ VRAM инициализирован: $VRAM_SIZE"
else
    echo "❌ VRAM не обнаружен в dmesg"
fi

if [ "$KFD_LOCAL_MEM" = "0" ]; then
    echo "❌ KFD local_mem_size = 0 (ПРОБЛЕМА!)"
    echo ""
    echo "Возможные причины:"
    echo "  1. amdgpu_amdkfd_get_local_mem_info() не передает данные в KFD"
    echo "  2. kfd_topology.c неправильно обрабатывает local_mem_info для APU"
    echo "  3. Проблема в kfd_crat.c - принудительное обнуление для APU"
elif [ -n "$KFD_LOCAL_MEM" ] && [ "$KFD_LOCAL_MEM" != "0" ]; then
    echo "✅ KFD local_mem_size = $KFD_LOCAL_MEM bytes"
else
    echo "⚠️  Не удалось получить KFD local_mem_size"
fi

echo ""
echo "═══════════════════════════════════════════════════════════════"
