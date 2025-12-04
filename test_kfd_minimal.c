/*
 * Минимальный тест KFD - проверка доступа к GPU Gladius
 * Использует прямой ioctl доступ к /dev/kfd без ROCm
 * 
 * Компиляция:
 *   gcc -o test_kfd_minimal test_kfd_minimal.c
 * 
 * Запуск:
 *   ./test_kfd_minimal
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <errno.h>

/* KFD ioctl definitions - минимальный набор без kernel headers */
#define AMDKFD_IOCTL_BASE 'K'

/* Версия KFD */
struct kfd_ioctl_get_version_args {
    uint32_t major_version;
    uint32_t minor_version;
};

/* Структура для получения apertures */
struct kfd_process_device_apertures {
    uint64_t lds_base;
    uint64_t lds_limit;
    uint64_t scratch_base;
    uint64_t scratch_limit;
    uint64_t gpuvm_base;
    uint64_t gpuvm_limit;
    uint32_t gpu_id;
    uint32_t pad;
};

#define NUM_OF_SUPPORTED_GPUS 7
struct kfd_ioctl_get_process_apertures_args {
    struct kfd_process_device_apertures process_apertures[NUM_OF_SUPPORTED_GPUS];
    uint32_t num_of_nodes;
    uint32_t pad;
};

/* Структура для новой версии apertures */
struct kfd_ioctl_get_process_apertures_new_args {
    uint64_t kfd_process_device_apertures_ptr;
    uint32_t num_of_nodes;
    uint32_t pad;
};

/* IOCTL команды */
#define AMDKFD_IOC_GET_VERSION \
    _IOR(AMDKFD_IOCTL_BASE, 0x01, struct kfd_ioctl_get_version_args)

#define AMDKFD_IOC_GET_PROCESS_APERTURES \
    _IOR(AMDKFD_IOCTL_BASE, 0x06, struct kfd_ioctl_get_process_apertures_args)

#define AMDKFD_IOC_GET_PROCESS_APERTURES_NEW \
    _IOWR(AMDKFD_IOCTL_BASE, 0x14, struct kfd_ioctl_get_process_apertures_new_args)

int main() {
    int kfd_fd;
    int ret;
    
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  ТЕСТ KFD - ПРЯМОЙ ДОСТУП К GPU GLADIUS (PS4 Pro)\n");
    printf("═══════════════════════════════════════════════════════════\n\n");
    
    /* Открываем KFD устройство */
    printf("[1] Открытие /dev/kfd...\n");
    kfd_fd = open("/dev/kfd", O_RDWR);
    if (kfd_fd < 0) {
        perror("    ❌ Ошибка открытия /dev/kfd");
        printf("    Убедитесь что пользователь в группе 'render'\n");
        printf("    Выполните: sudo usermod -aG render $USER\n");
        return 1;
    }
    printf("    ✅ /dev/kfd открыт (fd=%d)\n\n", kfd_fd);
    
    /* Получаем версию KFD */
    printf("[2] Получение версии KFD...\n");
    struct kfd_ioctl_get_version_args version = {0};
    ret = ioctl(kfd_fd, AMDKFD_IOC_GET_VERSION, &version);
    if (ret < 0) {
        perror("    ❌ Ошибка ioctl GET_VERSION");
    } else {
        printf("    ✅ KFD версия: %u.%u\n", version.major_version, version.minor_version);
    }
    printf("\n");
    
    /* Получаем apertures (адресные пространства GPU) */
    printf("[3] Получение GPU apertures...\n");
    struct kfd_ioctl_get_process_apertures_args apertures = {0};
    ret = ioctl(kfd_fd, AMDKFD_IOC_GET_PROCESS_APERTURES, &apertures);
    if (ret < 0) {
        perror("    ❌ Ошибка ioctl GET_PROCESS_APERTURES");
    } else {
        printf("    ✅ Найдено GPU узлов: %u\n", apertures.num_of_nodes);
        for (uint32_t i = 0; i < apertures.num_of_nodes; i++) {
            struct kfd_process_device_apertures *ap = &apertures.process_apertures[i];
            printf("\n    GPU #%u (gpu_id=%u):\n", i, ap->gpu_id);
            printf("        LDS:     0x%016llx - 0x%016llx\n", 
                   (unsigned long long)ap->lds_base, (unsigned long long)ap->lds_limit);
            printf("        Scratch: 0x%016llx - 0x%016llx\n", 
                   (unsigned long long)ap->scratch_base, (unsigned long long)ap->scratch_limit);
            printf("        GPUVM:   0x%016llx - 0x%016llx\n", 
                   (unsigned long long)ap->gpuvm_base, (unsigned long long)ap->gpuvm_limit);
        }
    }
    printf("\n");
    
    /* Читаем информацию из sysfs */
    printf("[4] Информация о GPU из sysfs...\n");
    const char* node_path = "/sys/class/kfd/kfd/topology/nodes/1/properties";
    FILE *props = fopen(node_path, "r");
    if (props) {
        char line[256];
        printf("    Свойства GPU (node 1):\n");
        while (fgets(line, sizeof(line), props)) {
            /* Показываем только важные свойства */
            if (strstr(line, "simd_count") ||
                strstr(line, "cu_per") ||
                strstr(line, "local_mem_size") ||
                strstr(line, "gfx_target") ||
                strstr(line, "device_id") ||
                strstr(line, "vendor_id")) {
                printf("        %s", line);
            }
        }
        fclose(props);
    } else {
        printf("    ⚠️  Не удалось прочитать %s\n", node_path);
    }
    printf("\n");
    
    /* Закрываем KFD */
    close(kfd_fd);
    
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  ТЕСТ ЗАВЕРШЁН\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("KFD доступ работает! GPU Gladius готов к compute.\n");
    printf("\n");
    printf("Следующий шаг: создание compute queue для умножения массивов\n");
    printf("Требуется:\n");
    printf("  1. Выделить память GPU (AMDKFD_IOC_ALLOC_MEMORY_OF_GPU)\n");
    printf("  2. Создать compute queue (AMDKFD_IOC_CREATE_QUEUE)\n");
    printf("  3. Отправить PM4 пакеты с шейдером\n");
    printf("  4. Дождаться завершения\n");
    
    return 0;
}
