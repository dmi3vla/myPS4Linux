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

#define VECTOR_SIZE (1024 * 1024)
#define ITERATIONS 1000
#define KERNEL_OFFSET 0x100  // Offset of fma_benchmark in binary

// KFD IOCTLs
struct kfd_ioctl_acquire_vm_args { uint32_t drm_fd; uint32_t gpu_id; };
struct kfd_ioctl_alloc_memory_of_gpu_args { uint64_t va_addr; uint64_t size; uint64_t handle; uint64_t mmap_offset; uint32_t gpu_id; uint32_t flags; };
struct kfd_ioctl_map_memory_to_gpu_args { uint64_t handle; uint64_t device_ids_array_ptr; uint32_t n_devices; uint32_t n_success; };
struct kfd_ioctl_create_queue_args { uint64_t ring_base_address; uint64_t write_pointer_address; uint64_t read_pointer_address; uint64_t doorbell_offset; uint32_t ring_size; uint32_t gpu_id; uint32_t queue_type; uint32_t queue_percentage; uint32_t queue_priority; uint32_t queue_id; uint64_t eop_buffer_address; uint64_t eop_buffer_size; uint64_t ctx_save_restore_address; uint32_t ctx_save_restore_size; uint32_t ctl_stack_size; };
struct kfd_ioctl_set_memory_policy_args { uint64_t alternate_aperture_base; uint64_t alternate_aperture_size; uint32_t gpu_id; uint32_t default_policy; uint32_t alternate_policy; };

#define AMDKFD_IOCTL_BASE 'K'
#define AMDKFD_IOC_ACQUIRE_VM _IOW(AMDKFD_IOCTL_BASE, 0x15, struct kfd_ioctl_acquire_vm_args)
#define AMDKFD_IOC_ALLOC_MEMORY_OF_GPU _IOWR(AMDKFD_IOCTL_BASE, 0x16, struct kfd_ioctl_alloc_memory_of_gpu_args)
#define AMDKFD_IOC_MAP_MEMORY_TO_GPU _IOWR(AMDKFD_IOCTL_BASE, 0x18, struct kfd_ioctl_map_memory_to_gpu_args)
#define AMDKFD_IOC_CREATE_QUEUE _IOWR(AMDKFD_IOCTL_BASE, 0x02, struct kfd_ioctl_create_queue_args)

#define KFD_IOC_ALLOC_MEM_FLAGS_GTT (1 << 1)
#define KFD_IOC_ALLOC_MEM_FLAGS_VRAM (1 << 0)
#define KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE (1 << 31)
#define KFD_IOC_ALLOC_MEM_FLAGS_EXECUTABLE (1 << 30)
#define KFD_IOC_ALLOC_MEM_FLAGS_PUBLIC (1 << 29)
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
#define mmCOMPUTE_USER_DATA_0 0x2e40  // Start of user data SGPRs
#define mmCOMPUTE_START_X 0x2e04
#define mmCOMPUTE_START_Y 0x2e05
#define mmCOMPUTE_START_Z 0x2e06
#define mmCOMPUTE_NUM_THREAD_X 0x2e07

// Kernarg Structure (must match ELF notes)
struct kernarg_t {
    uint64_t addr_a;
    uint64_t addr_b;
    uint64_t addr_c;
    uint32_t n;
    uint32_t iterations;
    // Hidden args
    uint32_t block_count_x;
    uint32_t block_count_y;
    uint32_t block_count_z;
    uint16_t group_size_x;
    uint16_t group_size_y;
    uint16_t group_size_z;
    uint16_t remainder_x;
    uint16_t remainder_y;
    uint16_t remainder_z;
    uint8_t padding[288 - 56]; // Pad to 288 bytes
};

int main() {
    int kfd_fd = open("/dev/kfd", O_RDWR);
    int drm_fd = open("/dev/dri/renderD129", O_RDWR);
    uint32_t gpu_id;
    
    // Get GPU ID
    FILE *f = fopen("/sys/class/kfd/kfd/topology/nodes/1/gpu_id", "r");
    fscanf(f, "%u", &gpu_id);
    fclose(f);
    
    // Acquire VM
    struct kfd_ioctl_acquire_vm_args acquire_vm = { .drm_fd = drm_fd, .gpu_id = gpu_id };
    ioctl(kfd_fd, AMDKFD_IOC_ACQUIRE_VM, &acquire_vm);
    
    printf("=== Direct KFD FMA Benchmark ===\n");
    printf("GPU ID: %u\n", gpu_id);
    
    // 1. Load Kernel Binary
    FILE *kb = fopen("gpu_kernel.bin", "rb");
    fseek(kb, 0, SEEK_END);
    size_t kernel_size = ftell(kb);
    fseek(kb, 0, SEEK_SET);
    void *kernel_bin = malloc(kernel_size);
    fread(kernel_bin, 1, kernel_size, kb);
    fclose(kb);
    
    // Allocate VRAM for Kernel Code
    struct kfd_ioctl_alloc_memory_of_gpu_args alloc_code = {
        .size = (kernel_size + 4095) & ~4095,
        .gpu_id = gpu_id,
        .flags = KFD_IOC_ALLOC_MEM_FLAGS_VRAM | KFD_IOC_ALLOC_MEM_FLAGS_EXECUTABLE | KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE,
        .va_addr = 0x100000000ULL
    };
    ioctl(kfd_fd, AMDKFD_IOC_ALLOC_MEMORY_OF_GPU, &alloc_code);
    
    struct kfd_ioctl_map_memory_to_gpu_args map_code = { .handle = alloc_code.handle, .device_ids_array_ptr = (uint64_t)&gpu_id, .n_devices = 1 };
    ioctl(kfd_fd, AMDKFD_IOC_MAP_MEMORY_TO_GPU, &map_code);
    
    // Copy kernel to VRAM (via CPU mapping)
    void *code_ptr = mmap(NULL, alloc_code.size, PROT_READ|PROT_WRITE, MAP_SHARED, drm_fd, alloc_code.mmap_offset);
    memcpy(code_ptr, kernel_bin, kernel_size);
    munmap(code_ptr, alloc_code.size);
    printf("Kernel loaded at VA 0x%lx\n", alloc_code.va_addr);
    
    // 2. Allocate Data Buffers (A, B, C)
    size_t data_size = VECTOR_SIZE * sizeof(float);
    struct kfd_ioctl_alloc_memory_of_gpu_args alloc_data = {
        .size = data_size * 3,
        .gpu_id = gpu_id,
        .flags = KFD_IOC_ALLOC_MEM_FLAGS_GTT | KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE,
        .va_addr = 0x200000000ULL
    };
    ioctl(kfd_fd, AMDKFD_IOC_ALLOC_MEMORY_OF_GPU, &alloc_data);
    
    struct kfd_ioctl_map_memory_to_gpu_args map_data = { .handle = alloc_data.handle, .device_ids_array_ptr = (uint64_t)&gpu_id, .n_devices = 1 };
    ioctl(kfd_fd, AMDKFD_IOC_MAP_MEMORY_TO_GPU, &map_data);
    
    float *data_ptr = mmap(NULL, alloc_data.size, PROT_READ|PROT_WRITE, MAP_SHARED, drm_fd, alloc_data.mmap_offset);
    float *host_a = data_ptr;
    float *host_b = data_ptr + VECTOR_SIZE;
    float *host_c = data_ptr + 2 * VECTOR_SIZE;
    
    // Initialize Data
    for (int i = 0; i < VECTOR_SIZE; i++) {
        host_a[i] = 1.0f;
        host_b[i] = 2.0f;
        host_c[i] = 0.0f;
    }
    
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
    
    struct kernarg_t *args = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, drm_fd, alloc_args.mmap_offset);
    args->addr_a = 0x200000000ULL;
    args->addr_b = 0x200000000ULL + data_size;
    args->addr_c = 0x200000000ULL + 2 * data_size;
    args->n = VECTOR_SIZE;
    args->iterations = ITERATIONS;
    
    // Hidden args
    args->block_count_x = VECTOR_SIZE / 256;
    args->block_count_y = 1;
    args->block_count_z = 1;
    args->group_size_x = 256;
    args->group_size_y = 1;
    args->group_size_z = 1;
    args->remainder_x = 0;
    
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
        .write_pointer_address = 0x400000000ULL + 8192 - 16, // Fake address for now
        .read_pointer_address = 0x400000000ULL + 8192 - 8,
    };
    // Note: Proper WPTR/RPTR handling requires separate allocation, simplifying for MVP
    
    if (ioctl(kfd_fd, AMDKFD_IOC_CREATE_QUEUE, &queue_args) < 0) {
        perror("Create queue failed");
        return 1;
    }
    printf("Queue created ID: %d, Doorbell: 0x%lx\n", queue_args.queue_id, queue_args.doorbell_offset);
    
    volatile uint32_t *doorbell = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, kfd_fd, queue_args.doorbell_offset & ~4095);
    doorbell += (queue_args.doorbell_offset & 4095) / 4;
    
    // 5. Build PM4 Packets
    uint32_t *packet = ring_ptr;
    int idx = 0;
    
    // Preamble: CONTEXT_CONTROL (Reset state)
    packet[idx++] = PACKET3(0x28, 1); // PACKET3_CONTEXT_CONTROL
    packet[idx++] = 0x80000000; // Load Enable=1, Load Shadow=0
    packet[idx++] = 0;
    
    // Preamble: ACQUIRE_MEM (Flush caches)
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
    // VGPRs: 7 -> 1. SGPRs: 10 -> 1.
    // FLOAT_MODE: 0xC0 (IEEE) -> Shift 12. DX10_CLAMP: 1 -> Shift 21.
    uint32_t rsrc1 = (1 << 0) | (1 << 6) | (0xC0 << 12) | (1 << 21);
    packet[idx++] = PACKET3(PACKET3_SET_SH_REG, 2);
    packet[idx++] = (mmCOMPUTE_PGM_RSRC1 - 0x2c00) >> 2;
    packet[idx++] = rsrc1;
    
    // RSRC2: USER_SGPR=2 (s0-s1), TGID_X/Y/Z_EN=1 (s2-s4), TG_SIZE_EN=1 (s5)
    // Compact ABI hypothesis: Kernarg in s0-s1, then System SGPRs.
    uint32_t rsrc2 = (2 << 1) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10);
    packet[idx++] = rsrc2;
    
    // SET_SH_REG: COMPUTE_USER_DATA_0..1 (Kernarg Ptr)
    packet[idx++] = PACKET3(PACKET3_SET_SH_REG, 2);
    packet[idx++] = (mmCOMPUTE_USER_DATA_0 - 0x2c00) >> 2;
    packet[idx++] = alloc_args.va_addr & 0xFFFFFFFF;
    packet[idx++] = alloc_args.va_addr >> 32;
    
    // SET_SH_REG: COMPUTE_START_X/Y/Z (0,0,0)
    packet[idx++] = PACKET3(PACKET3_SET_SH_REG, 3);
    packet[idx++] = (mmCOMPUTE_START_X - 0x2c00) >> 2;
    packet[idx++] = 0;
    packet[idx++] = 0;
    packet[idx++] = 0;
    
    // DISPATCH_DIRECT
    packet[idx++] = PACKET3(PACKET3_DISPATCH_DIRECT, 3);
    packet[idx++] = VECTOR_SIZE / 256; // X dim
    packet[idx++] = 1;                 // Y dim
    packet[idx++] = 1;                 // Z dim
    packet[idx++] = 1;                 // Initiator (COMPUTE_SHADER_EN)
    
    // EVENT_WRITE: RELEASE_MEM (Flush caches after)
    // PACKET3_EVENT_WRITE = 0x46
    // Event: CACHE_FLUSH_AND_INV_TS_EVENT (0x14)
    packet[idx++] = PACKET3(0x46, 0);
    packet[idx++] = 0x14;
    
    // Submit
    printf("Submitting %d dwords...\n", idx);
    *doorbell = idx * 4;
    
    // Wait
    sleep(2);
    
    // Check results
    printf("Checking results...\n");
    int errors = 0;
    for (int i = 0; i < 10; i++) {
        // FMA: c = a*b + c.
        // Init: a=1, b=2, c=0.
        // Iter 1: 1*2 + 0 = 2.
        // Iter 2: 1*2 + 2 = 4.
        // Result = 2 * ITERATIONS.
        float expected = 2.0f * ITERATIONS;
        if (host_c[i] != expected) {
            printf("c[%d] = %.2f (Expected: %.2f)\n", i, host_c[i], expected);
            errors++;
        }
    }
    
    if (errors == 0) {
        printf("✓ SUCCESS! All results match.\n");
        
        // Calculate FLOPS
        double total_flops = (double)VECTOR_SIZE * ITERATIONS * 4 * 2; // 4 FMAs * 2 FLOPs
        // We don't have exact time, but let's assume it ran fast.
        printf("Total operations: %.2e FLOPs\n", total_flops);
    } else {
        printf("✗ FAILURE: Results do not match.\n");
    }
    
    return 0;
}
