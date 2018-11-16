#ifndef AL_MATH_DEFS_H
#define AL_MATH_DEFS_H

#include <math.h>
#ifdef HAVE_FLOAT_H
#include <float.h>
#endif

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

#define F_PI    (3.14159265358979323846f)
#define F_PI_2  (1.57079632679489661923f)
#define F_TAU   (6.28318530717958647692f)

#ifndef FLT_EPSILON
#define FLT_EPSILON (1.19209290e-07f)
#endif

#define SQRT_2 1.41421356237309504880
#define SQRT_3 1.73205080756887719318

#define SQRTF_2 1.41421356237309504880f
#define SQRTF_3 1.73205080756887719318f

#ifndef HUGE_VALF
static const union msvc_inf_hack {
    unsigned char b[4];
    float f;
} msvc_inf_union = {{ 0x00, 0x00, 0x80, 0x7F }};
#define HUGE_VALF (msvc_inf_union.f)
#endif

#define DEG2RAD(x)  ((float)(x) * (float)(M_PI/180.0))
#define RAD2DEG(x)  ((float)(x) * (float)(180.0/M_PI))

#endif /* AL_MATH_DEFS_H */
