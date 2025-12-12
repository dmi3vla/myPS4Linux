#!/bin/bash
# Компиляция теста параллельного умножения матриц на CPU

echo "Компиляция test_cpu_matrix_parallel..."

gcc -o test_cpu_matrix_parallel test_cpu_matrix_parallel.c \
    -pthread -lm -O3 -Wall -march=native

if [ $? -eq 0 ]; then
    echo "✓ Компиляция успешна!"
    echo ""
    echo "Запуск теста:"
    echo "  ./test_cpu_matrix_parallel"
    echo ""
    echo "Примечание: Программа автоматически определит количество CPU ядер"
else
    echo "✗ Ошибка компиляции"
    exit 1
fi
