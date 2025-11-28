#!/bin/bash
CLANG="/opt/rocm/llvm/bin/clang"
OBJCOPY="/opt/rocm/llvm/bin/llvm-objcopy"

$CLANG -x cl -target amdgcn-amd-amdhsa -mcpu=gfx701 -O3 -c simple_write.cl -o simple_write.o
$OBJCOPY --dump-section=.text=simple_write.bin simple_write.o
