#!/bin/bash
# Компиляция теста параллельного умножения матриц на OpenCL

echo "Компиляция test_kfd_matrix_parallel..."

gcc -o test_kfd_matrix_parallel test_kfd_matrix_parallel.c \
    -lOpenCL -lm -O2 -Wall

if [ $? -eq 0 ]; then
    echo "✓ Компиляция успешна!"
    echo ""
    echo "Запуск теста:"
    echo "  ./test_kfd_matrix_parallel"
    echo ""
    echo "Примечание: Убедитесь что файл matrix_kernels.cl находится в той же директории"
else
    echo "✗ Ошибка компиляции"
    exit 1
fi
