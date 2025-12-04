#!/bin/bash
CLANG="/opt/rocm/llvm/bin/clang"
OBJCOPY="/opt/rocm/llvm/bin/llvm-objcopy"

echo "Compiling flops_test.cl for GFX701..."
$CLANG -x cl -target amdgcn-amd-amdhsa -mcpu=gfx701 -O3 -c flops_test.cl -o flops_test.o
if [ $? -ne 0 ]; then
    echo "Compilation failed!"
    exit 1
fi

echo "Extracting .text section..."
$OBJCOPY --dump-section=.text=flops_test.bin flops_test.o
if [ $? -ne 0 ]; then
    echo "Objcopy failed!"
    exit 1
fi

echo "Done! flops_test.bin created."
ls -l flops_test.bin
