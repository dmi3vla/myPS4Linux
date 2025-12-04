__kernel void flops_test(__global float* out, int iter) {
    int gid = get_global_id(0);
    float a = gid;
    float b = 1.5f;
    float c = 2.0f;
    
    // Heavy compute loop
    for(int i=0; i<iter; i++) {
        a = a * b + c;
        a = a * b + c;
        a = a * b + c;
        a = a * b + c;
        a = a * b + c;
        a = a * b + c;
        a = a * b + c;
        a = a * b + c; // 16 FLOPS per loop (8 FMAs)
    }
    
    out[gid] = a;
}
