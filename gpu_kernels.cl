// Simple vector addition kernel for Gladius GPU (gfx701)
// This will be compiled to GCN ISA and executed directly on GPU

__kernel void vector_add(__global const float *a,
                         __global const float *b,
                         __global float *c,
                         const unsigned int n)
{
    int gid = get_global_id(0);
    
    if (gid < n) {
        c[gid] = a[gid] + b[gid];
    }
}

// FMA benchmark kernel - intensive compute (simplified for Clover)
__kernel void fma_benchmark(__global float *a,
                            __global float *b,
                            __global float *c,
                            const unsigned int n,
                            const int iterations)
{
    int gid = get_global_id(0);
    
    if (gid < n) {
        float va = a[gid];
        float vb = b[gid];
        float vc = c[gid];
        
        // Manual FMA: c = a * b + c
        for (int i = 0; i < iterations; i++) {
            vc = va * vb + vc;
            vc = va * vb + vc;
            vc = va * vb + vc;
            vc = va * vb + vc;
        }
        
        c[gid] = vc;
    }
}
