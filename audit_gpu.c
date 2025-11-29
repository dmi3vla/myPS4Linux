#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <CL/cl.h>

// Function to read KFD topology property
int read_kfd_property(const char *prop_name) {
    char path[256];
    snprintf(path, sizeof(path), "/sys/class/kfd/kfd/topology/nodes/1/properties");
    
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    char line[256];
    int value = -1;
    
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, prop_name)) {
            // Format is usually "name value"
            char *ptr = strstr(line, " ");
            if (ptr) {
                value = atoi(ptr + 1);
                break;
            }
        }
    }
    
    fclose(f);
    return value;
}

int main() {
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  GPU COUNTER AUDIT: KFD vs OpenCL (Gladius PS4 Pro)\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    // 1. KFD AUDIT
    printf("[1] KFD Low-Level Audit (Sysfs)\n");
    int simd_count = read_kfd_property("simd_count");
    int cu_per_simd_array = read_kfd_property("cu_per_simd_array");
    int array_count = read_kfd_property("array_count");
    int device_id = read_kfd_property("device_id");
    
    if (simd_count == -1) {
        printf("  ERROR: Could not read KFD topology!\n");
    } else {
        int kfd_cus = simd_count / 4; // 4 SIMDs per CU
        printf("  Device ID:         %d (0x%X)\n", device_id, device_id);
        printf("  SIMD Count:        %d\n", simd_count);
        printf("  Shader Arrays:     %d\n", array_count);
        printf("  CU per Array:      %d\n", cu_per_simd_array);
        printf("  Calculated CUs:    %d\n", kfd_cus);
        
        printf("  -> KFD sees:       %d active CUs\n", kfd_cus);
    }
    printf("\n");

    // 2. OpenCL AUDIT
    printf("[2] OpenCL Userspace Audit\n");
    
    cl_uint num_platforms;
    clGetPlatformIDs(0, NULL, &num_platforms);
    if (num_platforms == 0) {
        printf("  ERROR: No OpenCL platforms found!\n");
        return 1;
    }

    cl_platform_id *platforms = malloc(sizeof(cl_platform_id) * num_platforms);
    clGetPlatformIDs(num_platforms, platforms, NULL);
    
    int opencl_cus = 0;
    
    for (cl_uint i = 0; i < num_platforms; i++) {
        char p_name[256];
        clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(p_name), p_name, NULL);
        
        // Only interested in Clover or AMD APP
        if (strstr(p_name, "Clover") || strstr(p_name, "AMD")) {
            printf("  Platform:          %s\n", p_name);
            
            cl_uint num_devices;
            clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, 0, NULL, &num_devices);
            
            if (num_devices > 0) {
                cl_device_id *devices = malloc(sizeof(cl_device_id) * num_devices);
                clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, num_devices, devices, NULL);
                
                for (cl_uint j = 0; j < num_devices; j++) {
                    char d_name[256];
                    cl_uint cus;
                    cl_ulong mem_size;
                    cl_uint max_freq;
                    
                    clGetDeviceInfo(devices[j], CL_DEVICE_NAME, sizeof(d_name), d_name, NULL);
                    clGetDeviceInfo(devices[j], CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cus), &cus, NULL);
                    clGetDeviceInfo(devices[j], CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(mem_size), &mem_size, NULL);
                    clGetDeviceInfo(devices[j], CL_DEVICE_MAX_CLOCK_FREQUENCY, sizeof(max_freq), &max_freq, NULL);
                    
                    printf("    Device:          %s\n", d_name);
                    printf("    Compute Units:   %u\n", cus);
                    printf("    Global Mem:      %lu MB\n", mem_size / (1024*1024));
                    printf("    Max Freq:        %u MHz\n", max_freq);
                    
                    if (cus > opencl_cus) opencl_cus = cus;
                }
                free(devices);
            }
        }
    }
    printf("\n");

    // 3. COMPARISON & CONCLUSION
    printf("[3] AUDIT CONCLUSION\n");
    printf("  Physical CUs (Spec): 36\n");
    printf("  KFD Active CUs:      %d\n", simd_count / 4);
    printf("  OpenCL Active CUs:   %d\n", opencl_cus);
    
    int kfd_cus = simd_count / 4;
    
    if (kfd_cus == opencl_cus) {
        if (kfd_cus == 36) {
            printf("  ✅ PERFECT! Full 36 CUs enabled.\n");
        } else if (kfd_cus == 24 || kfd_cus == 28) {
            printf("  ⚠️ HARVESTING DETECTED. Hardware or Kernel limits active CUs to %d.\n", kfd_cus);
            printf("     Possible causes:\n");
            printf("     1. Hardware fuses (permanent harvesting)\n");
            printf("     2. Kernel config 'max_cu_per_sh' is too low\n");
        }
    } else {
        printf("  ❌ DISCREPANCY! KFD (%d) != OpenCL (%d)\n", kfd_cus, opencl_cus);
        printf("     Userspace (Mesa) is hiding CUs that KFD sees.\n");
    }

    free(platforms);
    return 0;
}
