#include <CL/cl.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("Starting OpenCL minimal test...\n");
    fflush(stdout);
    
    cl_uint num_platforms = 0;
    printf("Calling clGetPlatformIDs (count)...\n");
    fflush(stdout);
    
    cl_int err = clGetPlatformIDs(0, NULL, &num_platforms);
    printf("clGetPlatformIDs returned: %d, num_platforms: %u\n", err, num_platforms);
    fflush(stdout);
    
    if (err != CL_SUCCESS || num_platforms == 0) {
        printf("No OpenCL platforms found\n");
        return 1;
    }
    
    cl_platform_id *platforms = malloc(sizeof(cl_platform_id) * num_platforms);
    printf("Calling clGetPlatformIDs (get)...\n");
    fflush(stdout);
    
    err = clGetPlatformIDs(num_platforms, platforms, NULL);
    printf("Got platforms: %d\n", err);
    fflush(stdout);
    
    for (cl_uint i = 0; i < num_platforms; i++) {
        char name[256];
        printf("Getting platform %u name...\n", i);
        fflush(stdout);
        
        clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(name), name, NULL);
        printf("Platform %u: %s\n", i, name);
        fflush(stdout);
    }
    
    printf("Test completed successfully!\n");
    free(platforms);
    return 0;
}
