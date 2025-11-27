#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <drm/drm.h>

/* Copied from kfd_ioctl.h */
#define KFD_IOCTL_MAJOR_VERSION 1
#define KFD_IOCTL_MINOR_VERSION 6

struct kfd_ioctl_get_version_args {
	uint32_t major_version;
	uint32_t minor_version;
};

struct kfd_ioctl_acquire_vm_args {
	uint32_t drm_fd;
	uint32_t gpu_id;
};

struct kfd_ioctl_alloc_memory_of_gpu_args {
	uint64_t va_addr;
	uint64_t size;
	uint64_t handle;
	uint64_t mmap_offset;
	uint32_t gpu_id;
	uint32_t flags;
};

struct kfd_ioctl_map_memory_to_gpu_args {
	uint64_t handle;
	uint64_t device_ids_array_ptr;
	uint32_t n_devices;
	uint32_t n_success;
};

struct kfd_ioctl_create_queue_args {
	uint64_t ring_base_address;
	uint64_t write_pointer_address;
	uint64_t read_pointer_address;
	uint64_t doorbell_offset;
	uint32_t ring_size;
	uint32_t gpu_id;
	uint32_t queue_type;
	uint32_t queue_percentage;
	uint32_t queue_priority;
	uint32_t queue_id;
	uint64_t eop_buffer_address;
	uint64_t eop_buffer_size;
	uint64_t ctx_save_restore_address;
	uint32_t ctx_save_restore_size;
	uint32_t ctl_stack_size;
};

struct kfd_ioctl_destroy_queue_args {
	uint32_t queue_id;
	uint32_t pad;
};

#define NUM_OF_SUPPORTED_GPUS 7
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

struct kfd_ioctl_get_process_apertures_args {
	struct kfd_process_device_apertures process_apertures[NUM_OF_SUPPORTED_GPUS];
	uint32_t num_of_nodes;
	uint32_t pad;
};

#define AMDKFD_IOCTL_BASE 'K'
#define AMDKFD_IOR(nr, type)		_IOR(AMDKFD_IOCTL_BASE, nr, type)
#define AMDKFD_IOW(nr, type)		_IOW(AMDKFD_IOCTL_BASE, nr, type)
#define AMDKFD_IOWR(nr, type)		_IOWR(AMDKFD_IOCTL_BASE, nr, type)

#define AMDKFD_IOC_GET_VERSION			AMDKFD_IOR(0x01, struct kfd_ioctl_get_version_args)
#define AMDKFD_IOC_ACQUIRE_VM			AMDKFD_IOW(0x15, struct kfd_ioctl_acquire_vm_args)
#define AMDKFD_IOC_ALLOC_MEMORY_OF_GPU		AMDKFD_IOWR(0x16, struct kfd_ioctl_alloc_memory_of_gpu_args)
#define AMDKFD_IOC_MAP_MEMORY_TO_GPU		AMDKFD_IOWR(0x18, struct kfd_ioctl_map_memory_to_gpu_args)
#define AMDKFD_IOC_CREATE_QUEUE			AMDKFD_IOWR(0x02, struct kfd_ioctl_create_queue_args)
#define AMDKFD_IOC_DESTROY_QUEUE		AMDKFD_IOWR(0x03, struct kfd_ioctl_destroy_queue_args)
#define AMDKFD_IOC_GET_PROCESS_APERTURES	AMDKFD_IOR(0x06, struct kfd_ioctl_get_process_apertures_args)

#define KFD_IOC_ALLOC_MEM_FLAGS_VRAM		(1 << 0)
#define KFD_IOC_ALLOC_MEM_FLAGS_GTT		(1 << 1)
#define KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE	(1 << 31)
#define KFD_IOC_ALLOC_MEM_FLAGS_EXECUTABLE	(1 << 30)
#define KFD_IOC_ALLOC_MEM_FLAGS_PUBLIC		(1 << 29)

#define KFD_IOC_QUEUE_TYPE_COMPUTE		0x0

int main() {
	int kfd_fd = open("/dev/kfd", O_RDWR);
	if (kfd_fd < 0) {
		perror("Failed to open /dev/kfd");
		return 1;
	}

	struct kfd_ioctl_get_version_args version_args = {0};
	if (ioctl(kfd_fd, AMDKFD_IOC_GET_VERSION, &version_args) < 0) {
		perror("Failed to get KFD version");
		close(kfd_fd);
		return 1;
	}
	printf("KFD Version: %d.%d\n", version_args.major_version, version_args.minor_version);

	// Need to find GPU ID. For now, assume the first one found in topology (or hardcoded if we knew it).
	// Since we don't have topology parsing here, we might need to guess or parse sysfs.
	// Let's try to find it from /sys/class/kfd/kfd/topology/nodes/
	// But for simplicity, let's assume a typical ID or try to find it.
	// Actually, without topology, we can't easily get the GPU ID required for other ioctls.
	// We can try to read /sys/class/kfd/kfd/topology/nodes/1/gpu_id (node 0 is usually CPU).
	
	FILE *f = fopen("/sys/class/kfd/kfd/topology/nodes/1/gpu_id", "r");
	if (!f) {
		// Try node 0 just in case
		f = fopen("/sys/class/kfd/kfd/topology/nodes/0/gpu_id", "r");
	}
	
	if (!f) {
		printf("Failed to find GPU ID in sysfs\n");
		return 1;
	}
	
	uint32_t gpu_id;
	fscanf(f, "%u", &gpu_id);
	fclose(f);
	printf("Found GPU ID: %u\n", gpu_id);

	// Try to get process apertures to find valid GPU IDs
	struct kfd_ioctl_get_process_apertures_args apertures_args = {0};
	apertures_args.num_of_nodes = 1; // Try to get 1
	if (ioctl(kfd_fd, AMDKFD_IOC_GET_PROCESS_APERTURES, &apertures_args) == 0) {
		printf("GET_PROCESS_APERTURES success. Num nodes: %d\n", apertures_args.num_of_nodes);
		for (int i = 0; i < NUM_OF_SUPPORTED_GPUS; i++) {
			if (apertures_args.process_apertures[i].gpu_id != 0) {
				printf("Aperture %d: GPU ID %u (0x%x)\n", i, apertures_args.process_apertures[i].gpu_id, apertures_args.process_apertures[i].gpu_id);
				// Use the first valid GPU ID found from apertures if the sysfs one failed
				if (gpu_id == 0 || gpu_id != apertures_args.process_apertures[i].gpu_id) {
					printf("Switching to GPU ID from apertures: %u\n", apertures_args.process_apertures[i].gpu_id);
					gpu_id = apertures_args.process_apertures[i].gpu_id;
				}
			}
		}
	} else {
		perror("GET_PROCESS_APERTURES failed");
	}

	printf("Using GPU ID: %u (0x%x)\n", gpu_id, gpu_id);

	int drm_fds[] = { -1, -1, -1, -1 };
	const char *drm_paths[] = { "/dev/dri/renderD128", "/dev/dri/renderD129", "/dev/dri/card0", "/dev/dri/card1" };
	int success_fd = -1;

	for (int i = 0; i < 4; i++) {
		printf("Trying DRM device: %s\n", drm_paths[i]);
		int fd = open(drm_paths[i], O_RDWR);
		if (fd < 0) {
			perror("Failed to open DRM device");
			continue;
		}
		
		struct kfd_ioctl_acquire_vm_args acquire_vm = {
			.drm_fd = fd,
			.gpu_id = gpu_id
		};
		
		if (ioctl(kfd_fd, AMDKFD_IOC_ACQUIRE_VM, &acquire_vm) == 0) {
			printf("VM Acquired successfully with %s\n", drm_paths[i]);
			success_fd = fd;
			break;
		} else {
			perror("Failed to acquire VM");
			close(fd);
		}
	}

	if (success_fd < 0) {
		printf("Failed to acquire VM with any DRM device\n");
		// We can't proceed
		close(kfd_fd);
		return 1;
	}

	// Allocate Ring Buffer (4KB)
	struct kfd_ioctl_alloc_memory_of_gpu_args alloc_ring = {
		.size = 4096,
		.gpu_id = gpu_id,
		.flags = KFD_IOC_ALLOC_MEM_FLAGS_GTT | KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE | KFD_IOC_ALLOC_MEM_FLAGS_PUBLIC
	};
	
	// Note: va_addr usually needs to be provided by user for KFD, or 0 to let KFD decide?
	// KFD usually expects the user to manage VA. We'll pick a random high address.
	alloc_ring.va_addr = 0x100000000ULL; 
	
	if (ioctl(kfd_fd, AMDKFD_IOC_ALLOC_MEMORY_OF_GPU, &alloc_ring) < 0) {
		perror("Failed to allocate ring buffer");
		return 1;
	}
	printf("Allocated Ring Buffer at VA: 0x%lx, Handle: 0x%lx\n", alloc_ring.va_addr, alloc_ring.handle);

	// Map Ring Buffer
	struct kfd_ioctl_map_memory_to_gpu_args map_ring = {
		.handle = alloc_ring.handle,
		.device_ids_array_ptr = (uint64_t)&gpu_id,
		.n_devices = 1
	};
	if (ioctl(kfd_fd, AMDKFD_IOC_MAP_MEMORY_TO_GPU, &map_ring) < 0) {
		perror("Failed to map ring buffer");
		return 1;
	}
	printf("Mapped Ring Buffer\n");

	// We also need to mmap it to CPU to write commands
	// According to kfd_ioctl.h, mmap_offset is for mmapping the render node
	printf("Mmapping Ring Buffer with offset: 0x%lx on DRM FD: %d\n", alloc_ring.mmap_offset, success_fd);
	void *ring_ptr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, success_fd, alloc_ring.mmap_offset);
	if (ring_ptr == MAP_FAILED) {
		perror("Failed to mmap ring buffer");
		return 1;
	}
	memset(ring_ptr, 0, 4096);

	// Allocate Read/Write Pointers (can be in same buffer for simplicity, but usually separate)
	// Let's use the first 16 bytes of ring for pointers? No, KFD writes to them.
	// We'll allocate another small buffer for pointers.
	
	struct kfd_ioctl_alloc_memory_of_gpu_args alloc_mqd = {
		.size = 4096,
		.gpu_id = gpu_id,
		.flags = KFD_IOC_ALLOC_MEM_FLAGS_GTT | KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE | KFD_IOC_ALLOC_MEM_FLAGS_PUBLIC,
		.va_addr = 0x200000000ULL
	};
	if (ioctl(kfd_fd, AMDKFD_IOC_ALLOC_MEMORY_OF_GPU, &alloc_mqd) < 0) {
		perror("Failed to allocate MQD/Pointers");
		return 1;
	}
	
	struct kfd_ioctl_map_memory_to_gpu_args map_mqd = {
		.handle = alloc_mqd.handle,
		.device_ids_array_ptr = (uint64_t)&gpu_id,
		.n_devices = 1
	};
	ioctl(kfd_fd, AMDKFD_IOC_MAP_MEMORY_TO_GPU, &map_mqd);
	
	// Create Queue
	struct kfd_ioctl_create_queue_args create_queue = {0};
	create_queue.gpu_id = gpu_id;
	create_queue.queue_type = KFD_IOC_QUEUE_TYPE_COMPUTE;
	create_queue.queue_percentage = 100;
	create_queue.queue_priority = 7;
	create_queue.ring_base_address = alloc_ring.va_addr;
	create_queue.ring_size = 4096;
	create_queue.read_pointer_address = alloc_mqd.va_addr;
	create_queue.write_pointer_address = alloc_mqd.va_addr + 8;
	
	if (ioctl(kfd_fd, AMDKFD_IOC_CREATE_QUEUE, &create_queue) < 0) {
		perror("Failed to create queue");
		return 1;
	}
	printf("Queue Created! ID: %d, Doorbell Offset: 0x%lx\n", create_queue.queue_id, create_queue.doorbell_offset);

	// Map Doorbell
	// Doorbell mapping is usually done on the KFD FD (or DRM FD?)
	// KFD doorbells are usually on the KFD device.
	// The offset is create_queue.doorbell_offset.
	// Let's try mmapping KFD FD for doorbell.
	// Note: Doorbell offset needs to be page aligned?
	// Usually doorbell_offset is the offset *within* the doorbell page, or the global offset?
	// KFD returns doorbell_offset as the offset to use for mmap?
	// Actually, for KFD, doorbells are mmapped via KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL?
	// No, the queue creation returns a doorbell_offset.
	// In libhsakmt, it mmaps the doorbell aperture.
	
	// Let's try to mmap the doorbell using KFD FD and the returned offset.
	// But first, let's verify queue creation worked.
	
	printf("Test passed so far (Queue Creation). KFD is functional.\n");

	close(success_fd);
	close(kfd_fd);
	return 0;
}
