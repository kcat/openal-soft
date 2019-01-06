#ifndef AL_MATH_DEFS_H
#define AL_MATH_DEFS_H

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

constexpr inline float Deg2Rad(float x) noexcept { return x * static_cast<float>(M_PI/180.0); }
constexpr inline float Rad2Deg(float x) noexcept { return x * static_cast<float>(180.0/M_PI); }

namespace al {

template<typename Real>
struct MathDefs { };

template<>
struct MathDefs<float> {
    static constexpr inline float Pi() noexcept { return 3.14159265358979323846f; }
    static constexpr inline float Tau() noexcept { return 3.14159265358979323846f * 2.0f; }
    static constexpr inline float Sqrt3() noexcept { return 1.73205080756887719318f; }
};

template<>
struct MathDefs<double> {
    static constexpr inline double Pi() noexcept { return 3.14159265358979323846; }
    static constexpr inline double Tau() noexcept { return 3.14159265358979323846 * 2.0; }
    static constexpr inline double Sqrt3() noexcept { return 1.73205080756887719318; }
};

} // namespace al

#endif /* AL_MATH_DEFS_H */
