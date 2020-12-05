#ifndef AL_MATH_DEFS_H
#define AL_MATH_DEFS_H

constexpr float Deg2Rad(float x) noexcept { return x * 1.74532925199432955e-02f/*pi/180*/; }
constexpr float Rad2Deg(float x) noexcept { return x * 5.72957795130823229e+01f/*180/pi*/; }

namespace al {

template<typename Real>
struct MathDefs { };

template<>
struct MathDefs<float> {
    static constexpr float Pi() noexcept { return 3.14159265358979323846e+00f; }
    static constexpr float Tau() noexcept { return 6.28318530717958647692e+00f; }
};

template<>
struct MathDefs<double> {
    static constexpr double Pi() noexcept { return 3.14159265358979323846e+00; }
    static constexpr double Tau() noexcept { return 6.28318530717958647692e+00; }
};

} // namespace al

#endif /* AL_MATH_DEFS_H */
