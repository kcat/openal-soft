#ifndef AL_MATH_DEFS_H
#define AL_MATH_DEFS_H

#include <math.h>

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

#define F_PI    (3.14159265358979323846f)
#define F_PI_2  (1.57079632679489661923f)
#define F_TAU   (6.28318530717958647692f)

#define SQRTF_3 1.73205080756887719318f

constexpr inline float Deg2Rad(float x) noexcept { return x * static_cast<float>(M_PI/180.0); }
constexpr inline float Rad2Deg(float x) noexcept { return x * static_cast<float>(180.0/M_PI); }

#endif /* AL_MATH_DEFS_H */
