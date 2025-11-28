#include <CL/cl.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
    cl_uint num_platforms;
    clGetPlatformIDs(0, NULL, &num_platforms);
    
    cl_platform_id *platforms = malloc(sizeof(cl_platform_id) * num_platforms);
    clGetPlatformIDs(num_platforms, platforms, NULL);
    
    for (cl_uint i = 0; i < num_platforms; i++) {
        char name[256];
        clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(name), name, NULL);
        printf("\n=== Platform %u: %s ===\n", i, name);
        
        cl_uint num_devices;
        printf("Getting device count...\n");
        fflush(stdout);
        
        cl_int err = clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, 0, NULL, &num_devices);
        if (err != CL_SUCCESS) {
            printf("  clGetDeviceIDs failed: %d\n", err);
            continue;
        }
        
        printf("  Found %u devices\n", num_devices);
        
        cl_device_id *devices = malloc(sizeof(cl_device_id) * num_devices);
        clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, num_devices, devices, NULL);
        
        for (cl_uint j = 0; j < num_devices; j++) {
            char dev_name[256];
            cl_device_type dev_type;
            cl_uint compute_units;
            
            printf("  Device %u:\n", j);
            fflush(stdout);
            
            clGetDeviceInfo(devices[j], CL_DEVICE_NAME, sizeof(dev_name), dev_name, NULL);
            printf("    Name: %s\n", dev_name);
            
            clGetDeviceInfo(devices[j], CL_DEVICE_TYPE, sizeof(dev_type), &dev_type, NULL);
            printf("    Type: %s\n", (dev_type == CL_DEVICE_TYPE_GPU) ? "GPU" : "CPU");
            
            clGetDeviceInfo(devices[j], CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(compute_units), &compute_units, NULL);
            printf("    Compute Units: %u\n", compute_units);
        }
        
        free(devices);
    }
    
    free(platforms);
    return 0;
}
