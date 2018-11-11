#ifndef CPU_CAPS_H
#define CPU_CAPS_H

#ifdef __cplusplus
extern "C" {
#endif

extern int CPUCapFlags;
enum {
    CPU_CAP_SSE    = 1<<0,
    CPU_CAP_SSE2   = 1<<1,
    CPU_CAP_SSE3   = 1<<2,
    CPU_CAP_SSE4_1 = 1<<3,
    CPU_CAP_NEON   = 1<<4,
};

void FillCPUCaps(int capfilter);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* CPU_CAPS_H */
