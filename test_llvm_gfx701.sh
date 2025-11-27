#!/bin/bash
CLANG="/opt/rocm/llvm/bin/clang"
TARGET="amdgcn-amd-amdhsa"
MCPU="gfx701"

echo "__kernel void test_kernel(__global int *a) { *a = 1; }" > test_kernel.cl

echo "Compiling OpenCL kernel for $MCPU..."
$CLANG -x cl -target $TARGET -mcpu=$MCPU -c test_kernel.cl -o test_kernel.o

if [ $? -eq 0 ]; then
    echo "Compilation successful!"
    if [ -f "test_kernel.o" ]; then
        echo "Object file created: test_kernel.o"
    fi
else
    echo "Compilation failed!"
fi
