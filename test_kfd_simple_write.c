#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <time.h>

#define KERNEL_OFFSET 0x0  // Offset of simple_write in binary

// KFD IOCTLs
struct kfd_ioctl_acquire_vm_args { uint32_t drm_fd; uint32_t gpu_id; };
struct kfd_ioctl_alloc_memory_of_gpu_args { uint64_t va_addr; uint64_t size; uint64_t handle; uint64_t mmap_offset; uint32_t gpu_id; uint32_t flags; };
struct kfd_ioctl_map_memory_to_gpu_args { uint64_t handle; uint64_t device_ids_array_ptr; uint32_t n_devices; uint32_t n_success; };
struct kfd_ioctl_create_queue_args { uint64_t ring_base_address; uint64_t write_pointer_address; uint64_t read_pointer_address; uint64_t doorbell_offset; uint32_t ring_size; uint32_t gpu_id; uint32_t queue_type; uint32_t queue_percentage; uint32_t queue_priority; uint32_t queue_id; uint64_t eop_buffer_address; uint64_t eop_buffer_size; uint64_t ctx_save_restore_address; uint32_t ctx_save_restore_size; uint32_t ctl_stack_size; };

#define AMDKFD_IOCTL_BASE 'K'
#define AMDKFD_IOC_ACQUIRE_VM _IOW(AMDKFD_IOCTL_BASE, 0x15, struct kfd_ioctl_acquire_vm_args)
#define AMDKFD_IOC_ALLOC_MEMORY_OF_GPU _IOWR(AMDKFD_IOCTL_BASE, 0x16, struct kfd_ioctl_alloc_memory_of_gpu_args)
#define AMDKFD_IOC_MAP_MEMORY_TO_GPU _IOWR(AMDKFD_IOCTL_BASE, 0x18, struct kfd_ioctl_map_memory_to_gpu_args)
#define AMDKFD_IOC_CREATE_QUEUE _IOWR(AMDKFD_IOCTL_BASE, 0x02, struct kfd_ioctl_create_queue_args)

#define KFD_IOC_ALLOC_MEM_FLAGS_GTT (1 << 1)
#define KFD_IOC_ALLOC_MEM_FLAGS_VRAM (1 << 0)
#define KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE (1 << 31)
#define KFD_IOC_ALLOC_MEM_FLAGS_EXECUTABLE (1 << 30)
#define KFD_IOC_QUEUE_TYPE_COMPUTE 0

// PM4 Definitions for CIK (GFX7)
#define PACKET3(op, n)	((1 << 31) | ((op) << 8) | (n))
#define PACKET3_SET_SH_REG 0x23
#define PACKET3_DISPATCH_DIRECT 0x15

// Register Offsets (CIK)
#define mmCOMPUTE_PGM_LO 0x2e0c
#define mmCOMPUTE_PGM_HI 0x2e0d
#define mmCOMPUTE_PGM_RSRC1 0x2e12
#define mmCOMPUTE_PGM_RSRC2 0x2e13
#define mmCOMPUTE_USER_DATA_0 0x2e40 
#define mmCOMPUTE_START_X 0x2e04

int main() {
    int kfd_fd = open("/dev/kfd", O_RDWR);
    int drm_fd = open("/dev/dri/renderD129", O_RDWR);
    uint32_t gpu_id;
    
    FILE *f = fopen("/sys/class/kfd/kfd/topology/nodes/1/gpu_id", "r");
    fscanf(f, "%u", &gpu_id);
    fclose(f);
    
    struct kfd_ioctl_acquire_vm_args acquire_vm = { .drm_fd = drm_fd, .gpu_id = gpu_id };
    ioctl(kfd_fd, AMDKFD_IOC_ACQUIRE_VM, &acquire_vm);
    
    printf("=== Direct KFD Simple Write Test ===\n");
    printf("GPU ID: %u\n", gpu_id);
    
    // 1. Load Kernel Binary
    FILE *kb = fopen("simple_write.bin", "rb");
    fseek(kb, 0, SEEK_END);
    size_t kernel_size = ftell(kb);
    fseek(kb, 0, SEEK_SET);
    void *kernel_bin = malloc(kernel_size);
    fread(kernel_bin, 1, kernel_size, kb);
    fclose(kb);
    
    struct kfd_ioctl_alloc_memory_of_gpu_args alloc_code = {
        .size = (kernel_size + 4095) & ~4095,
        .gpu_id = gpu_id,
        .flags = KFD_IOC_ALLOC_MEM_FLAGS_VRAM | KFD_IOC_ALLOC_MEM_FLAGS_EXECUTABLE | KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE,
        .va_addr = 0x100000000ULL
    };
    ioctl(kfd_fd, AMDKFD_IOC_ALLOC_MEMORY_OF_GPU, &alloc_code);
    
    struct kfd_ioctl_map_memory_to_gpu_args map_code = { .handle = alloc_code.handle, .device_ids_array_ptr = (uint64_t)&gpu_id, .n_devices = 1 };
    ioctl(kfd_fd, AMDKFD_IOC_MAP_MEMORY_TO_GPU, &map_code);
    
    void *code_ptr = mmap(NULL, alloc_code.size, PROT_READ|PROT_WRITE, MAP_SHARED, drm_fd, alloc_code.mmap_offset);
    memcpy(code_ptr, kernel_bin, kernel_size);
    munmap(code_ptr, alloc_code.size);
    printf("Kernel loaded at VA 0x%lx\n", alloc_code.va_addr);
    
    // 2. Allocate Data Buffer
    struct kfd_ioctl_alloc_memory_of_gpu_args alloc_data = {
        .size = 4096,
        .gpu_id = gpu_id,
        .flags = KFD_IOC_ALLOC_MEM_FLAGS_GTT | KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE,
        .va_addr = 0x200000000ULL
    };
    ioctl(kfd_fd, AMDKFD_IOC_ALLOC_MEMORY_OF_GPU, &alloc_data);
    
    struct kfd_ioctl_map_memory_to_gpu_args map_data = { .handle = alloc_data.handle, .device_ids_array_ptr = (uint64_t)&gpu_id, .n_devices = 1 };
    ioctl(kfd_fd, AMDKFD_IOC_MAP_MEMORY_TO_GPU, &map_data);
    
    float *host_data = mmap(NULL, alloc_data.size, PROT_READ|PROT_WRITE, MAP_SHARED, drm_fd, alloc_data.mmap_offset);
    host_data[0] = 0.0f;
    
    // 3. Allocate Kernarg Buffer
    struct kfd_ioctl_alloc_memory_of_gpu_args alloc_args = {
        .size = 4096,
        .gpu_id = gpu_id,
        .flags = KFD_IOC_ALLOC_MEM_FLAGS_GTT | KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE,
        .va_addr = 0x300000000ULL
    };
    ioctl(kfd_fd, AMDKFD_IOC_ALLOC_MEMORY_OF_GPU, &alloc_args);
    
    struct kfd_ioctl_map_memory_to_gpu_args map_args = { .handle = alloc_args.handle, .device_ids_array_ptr = (uint64_t)&gpu_id, .n_devices = 1 };
    ioctl(kfd_fd, AMDKFD_IOC_MAP_MEMORY_TO_GPU, &map_args);
    
    uint64_t *args = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, drm_fd, alloc_args.mmap_offset);
    args[0] = alloc_data.va_addr; // Pointer to data buffer
    
    // 4. Create Queue
    struct kfd_ioctl_alloc_memory_of_gpu_args alloc_ring = {
        .size = 8192,
        .gpu_id = gpu_id,
        .flags = KFD_IOC_ALLOC_MEM_FLAGS_GTT | KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE,
        .va_addr = 0x400000000ULL
    };
    ioctl(kfd_fd, AMDKFD_IOC_ALLOC_MEMORY_OF_GPU, &alloc_ring);
    struct kfd_ioctl_map_memory_to_gpu_args map_ring = { .handle = alloc_ring.handle, .device_ids_array_ptr = (uint64_t)&gpu_id, .n_devices = 1 };
    ioctl(kfd_fd, AMDKFD_IOC_MAP_MEMORY_TO_GPU, &map_ring);
    
    uint32_t *ring_ptr = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, drm_fd, alloc_ring.mmap_offset);
    
    struct kfd_ioctl_create_queue_args queue_args = {
        .ring_base_address = 0x400000000ULL,
        .ring_size = 8192,
        .gpu_id = gpu_id,
        .queue_type = KFD_IOC_QUEUE_TYPE_COMPUTE,
        .queue_percentage = 100,
        .queue_priority = 7,
        .write_pointer_address = 0x400000000ULL + 8192 - 16,
        .read_pointer_address = 0x400000000ULL + 8192 - 8,
    };
    
    if (ioctl(kfd_fd, AMDKFD_IOC_CREATE_QUEUE, &queue_args) < 0) {
        perror("Create queue failed");
        return 1;
    }
    
    volatile uint32_t *doorbell = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, kfd_fd, queue_args.doorbell_offset & ~4095);
    if (doorbell == MAP_FAILED) {
        perror("Doorbell mmap failed");
        return 1;
    }
    doorbell += (queue_args.doorbell_offset & 4095) / 4;
    
    // 5. Build PM4 Packets
    uint32_t *packet = ring_ptr;
    int idx = 0;
    
    // CONTEXT_CONTROL
    packet[idx++] = PACKET3(0x28, 1);
    packet[idx++] = 0x80000000;
    packet[idx++] = 0;
    
    // ACQUIRE_MEM
    packet[idx++] = PACKET3(0x24, 6);
    packet[idx++] = 0; 
    packet[idx++] = 0xFFFFFFFF; 
    packet[idx++] = 0xFFFFFFFF;
    packet[idx++] = 0; 
    packet[idx++] = 0;
    packet[idx++] = 0; 
    packet[idx++] = 0;
    
    // SET_SH_REG: COMPUTE_PGM_LO/HI
    uint64_t kernel_addr = alloc_code.va_addr + KERNEL_OFFSET;
    packet[idx++] = PACKET3(PACKET3_SET_SH_REG, 2);
    packet[idx++] = (mmCOMPUTE_PGM_LO - 0x2c00) >> 2;
    packet[idx++] = kernel_addr & 0xFFFFFFFF;
    packet[idx++] = kernel_addr >> 32;
    
    // SET_SH_REG: COMPUTE_PGM_RSRC1/2
    // VGPRs: 1. SGPRs: 1. (Minimal)
    // FLOAT_MODE: 0xC0 (IEEE) -> Shift 12. DX10_CLAMP: 1 -> Shift 21.
    uint32_t rsrc1 = (0 << 0) | (0 << 6) | (0xC0 << 12) | (1 << 21);
    packet[idx++] = PACKET3(PACKET3_SET_SH_REG, 2);
    packet[idx++] = (mmCOMPUTE_PGM_RSRC1 - 0x2c00) >> 2;
    packet[idx++] = rsrc1;
    
    // RSRC2: USER_SGPR=16 (s0-s15), TGID_X/Y/Z_EN=1 (s16-s18)
    // Flood strategy: s0-s15 = Kernarg Ptr.
    uint32_t rsrc2 = (16 << 1) | (1 << 7) | (1 << 8) | (1 << 9);
    packet[idx++] = rsrc2;
    
    // SET_SH_REG: COMPUTE_USER_DATA_0..15 (Kernarg Ptr)
    packet[idx++] = PACKET3(PACKET3_SET_SH_REG, 16);
    packet[idx++] = (mmCOMPUTE_USER_DATA_0 - 0x2c00) >> 2;
    for (int i = 0; i < 8; i++) {
        packet[idx++] = alloc_args.va_addr & 0xFFFFFFFF;
        packet[idx++] = alloc_args.va_addr >> 32;
    }
    
    // SET_SH_REG: COMPUTE_START_X/Y/Z (0,0,0)
    packet[idx++] = PACKET3(PACKET3_SET_SH_REG, 3);
    packet[idx++] = (mmCOMPUTE_START_X - 0x2c00) >> 2;
    packet[idx++] = 0;
    packet[idx++] = 0;
    packet[idx++] = 0;
    
    // DISPATCH_DIRECT
    packet[idx++] = PACKET3(PACKET3_DISPATCH_DIRECT, 3);
    packet[idx++] = 1; // X dim
    packet[idx++] = 1; // Y dim
    packet[idx++] = 1; // Z dim
    packet[idx++] = 1; // Initiator
    
    // RELEASE_MEM
    packet[idx++] = PACKET3(0x46, 0);
    packet[idx++] = 0x14;
    
    // Submit
    printf("Submitting %d dwords...\n", idx);
    *doorbell = idx * 4;
    
    sleep(1);
    
    printf("Result: %.2f (Expected: 123.00)\n", host_data[0]);
    if (host_data[0] == 123.0f) {
        printf("✓ SUCCESS!\n");
    } else {
        printf("✗ FAILURE\n");
    }
    
    return 0;
}
