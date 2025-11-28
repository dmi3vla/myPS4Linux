#!/bin/bash
# Компиляция OpenCL kernel в GCN ISA для Gladius (gfx701)

CLANG="/opt/rocm/llvm/bin/clang"
OBJDUMP="/opt/rocm/llvm/bin/llvm-objdump"
OBJCOPY="/opt/rocm/llvm/bin/llvm-objcopy"
READELF="/opt/rocm/llvm/bin/llvm-readelf"

KERNEL_FILE="gpu_kernels.cl"
OUTPUT_BASE="gpu_kernel"

echo "═══════════════════════════════════════════════════════════"
echo "  Компиляция GPU Kernel для Gladius (gfx701)"
echo "═══════════════════════════════════════════════════════════"
echo ""

# Проверка наличия clang
if [ ! -f "$CLANG" ]; then
    echo "❌ Ошибка: ROCm clang не найден: $CLANG"
    exit 1
fi

echo "Шаг 1: Компиляция OpenCL → LLVM IR..."
$CLANG -x cl \
    -target amdgcn-amd-amdhsa \
    -mcpu=gfx701 \
    -O3 \
    -S -emit-llvm \
    $KERNEL_FILE \
    -o ${OUTPUT_BASE}.ll

if [ $? -eq 0 ]; then
    echo "✓ LLVM IR создан: ${OUTPUT_BASE}.ll"
    echo "  Размер: $(wc -l < ${OUTPUT_BASE}.ll) строк"
else
    echo "❌ Ошибка компиляции в LLVM IR"
    exit 1
fi

echo ""
echo "Шаг 2: Компиляция LLVM IR → GCN Object..."
$CLANG -x cl \
    -target amdgcn-amd-amdhsa \
    -mcpu=gfx701 \
    -O3 \
    -c $KERNEL_FILE \
    -o ${OUTPUT_BASE}.o

if [ $? -eq 0 ]; then
    echo "✓ Object file создан: ${OUTPUT_BASE}.o"
    echo "  Размер: $(stat -f%z ${OUTPUT_BASE}.o 2>/dev/null || stat -c%s ${OUTPUT_BASE}.o) байт"
else
    echo "❌ Ошибка компиляции в object"
    exit 1
fi

echo ""
echo "Шаг 3: Анализ ELF структуры..."
if [ -f "$READELF" ]; then
    $READELF -S ${OUTPUT_BASE}.o | head -20
fi

echo ""
echo "Шаг 4: Извлечение .text секции (ISA binary)..."
$OBJCOPY --dump-section=.text=${OUTPUT_BASE}.bin ${OUTPUT_BASE}.o

if [ -f "${OUTPUT_BASE}.bin" ]; then
    BIN_SIZE=$(stat -f%z ${OUTPUT_BASE}.bin 2>/dev/null || stat -c%s ${OUTPUT_BASE}.bin)
    echo "✓ ISA binary извлечен: ${OUTPUT_BASE}.bin"
    echo "  Размер: $BIN_SIZE байт"
    
    # Показать первые байты
    echo ""
    echo "Первые 64 байта (hex):"
    hexdump -C ${OUTPUT_BASE}.bin | head -5
else
    echo "❌ Ошибка извлечения .text секции"
    exit 1
fi

echo ""
echo "Шаг 5: Создание полного HSA code object..."
cp ${OUTPUT_BASE}.o ${OUTPUT_BASE}.co

echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  ✓ Компиляция завершена успешно!"
echo "═══════════════════════════════════════════════════════════"
echo ""
echo "Созданные файлы:"
echo "  • ${OUTPUT_BASE}.ll   - LLVM IR (человекочитаемый)"
echo "  • ${OUTPUT_BASE}.o    - ELF object file"
echo "  • ${OUTPUT_BASE}.bin  - Raw ISA binary"
echo "  • ${OUTPUT_BASE}.co   - HSA code object"
echo ""
echo "Следующий шаг: Загрузить ${OUTPUT_BASE}.co в GPU через KFD"
