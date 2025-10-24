#ifndef AL_NUMERIC_H
#define AL_NUMERIC_H

#include "config_simd.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>
#ifdef HAVE_INTRIN_H
#include <intrin.h>
#endif
#if HAVE_SSE_INTRINSICS
#include <emmintrin.h>
#endif

#include "altypes.hpp"
#include "gsl/gsl"
#include "opthelpers.h"


namespace al {

#if HAS_BUILTIN(__builtin_add_overflow)
template<std::integral T> [[nodiscard]]
constexpr auto add_sat(T const lhs, T const rhs) noexcept -> T
{
    T res;
    if(!__builtin_add_overflow(lhs, rhs, &res))
        return res;
    if constexpr(std::is_signed_v<T>)
    {
        if(rhs < 0)
            return std::numeric_limits<T>::min();
    }
    return std::numeric_limits<T>::max();
}

#else

template<std::integral T> [[nodiscard]]
constexpr auto add_sat(T lhs, T rhs) noexcept -> T
{
    if constexpr(std::is_signed_v<T>)
    {
        if(rhs < 0)
        {
            if(lhs < std::numeric_limits<T>::min()-rhs)
                return std::numeric_limits<T>::min();
            return lhs + rhs;
        }
        if(lhs > std::numeric_limits<T>::max()-rhs)
            return std::numeric_limits<T>::max();
        return lhs + rhs;
    }
    else
    {
        const auto res = static_cast<T>(lhs + rhs);
        if(res < lhs)
            return std::numeric_limits<T>::max();
        return res;
    }
}
#endif

template<std::integral R, std::integral T> [[nodiscard]]
constexpr auto saturate_cast(T val) noexcept -> R
{
    if constexpr(std::numeric_limits<R>::digits < std::numeric_limits<T>::digits)
    {
        if constexpr(std::is_signed_v<R> && std::is_signed_v<T>)
        {
            if(val < std::numeric_limits<R>::min())
                return std::numeric_limits<R>::min();
        }
        if(val > T{std::numeric_limits<R>::max()})
            return std::numeric_limits<R>::max();
    }
    if constexpr(std::is_unsigned_v<R> && std::is_signed_v<T>)
    {
        if(val < 0)
            return R{0};
    }
    return gsl::narrow_cast<R>(val);
}

} /* namespace al */

template<std::integral T> [[nodiscard]]
constexpr auto as_unsigned(T value) noexcept
{ return static_cast<std::make_unsigned_t<T>>(value); }

template<std::integral T> [[nodiscard]]
constexpr auto as_signed(T value) noexcept
{ return static_cast<std::make_signed_t<T>>(value); }


[[nodiscard]]
constexpr auto GetCounterSuffix(usize const count) noexcept -> std::string_view
{
    using namespace std::string_view_literals;
    return (((count%100)/10) == 1) ? "th"sv :
        ((count%10) == 1) ? "st"sv :
        ((count%10) == 2) ? "nd"sv :
        ((count%10) == 3) ? "rd"sv : "th"sv;
}


[[nodiscard]]
constexpr auto lerpf(f32 const val1, f32 const val2, f32 const mu) noexcept -> f32
{ return val1 + (val2-val1)*mu; }


/** Find the next power-of-2 for non-power-of-2 numbers. */
[[nodiscard]]
constexpr auto NextPowerOf2(u32 value) noexcept -> u32
{
    if(value > 0)
    {
        value--;
        value |= value>>1;
        value |= value>>2;
        value |= value>>4;
        value |= value>>8;
        value |= value>>16;
    }
    return value+1;
}

/**
 * If the value is not already a multiple of r, round toward zero to the next
 * multiple.
 */
template<std::integral T> [[nodiscard]]
constexpr auto RoundToZero(T value, std::type_identity_t<T> r) noexcept -> T
{ return value - (value%r); }

/**
 * If the value is not already a multiple of r, round away from zero to the
 * next multiple.
 */
template<std::integral T> [[nodiscard]]
constexpr auto RoundFromZero(T value, std::type_identity_t<T> r) noexcept -> T
{
    if(value >= 0)
        return RoundToZero(value + r-1, r);
    return RoundToZero(value - r+1, r);
}


/**
 * Fast float-to-int conversion. No particular rounding mode is assumed; the
 * IEEE-754 default is round-to-nearest with ties-to-even, though an app could
 * change it on its own threads. On some systems, a truncating conversion may
 * always be the fastest method.
 */
[[nodiscard]]
inline auto fastf2i(f32 const f) noexcept -> i32
{
#if HAVE_SSE_INTRINSICS
    return _mm_cvt_ss2si(_mm_set_ss(f));

#elif defined(_MSC_VER) && defined(_M_IX86_FP) && _M_IX86_FP == 0

    int i;
    __asm fld f
    __asm fistp i
    return i;

#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)) \
    && !defined(__SSE_MATH__)

    int i;
    __asm__ __volatile__("fistpl %0" : "=m"(i) : "t"(f) : "st");
    return i;

#else

    return gsl::narrow_cast<int>(f);
#endif
}
[[nodiscard]]
inline auto fastf2u(f32 const f) noexcept -> u32
{ return gsl::narrow_cast<u32>(fastf2i(f)); }

/**
 * Converts float-to-int using standard behavior (truncation). Out of range
 * values are clamped.
 */
[[nodiscard]]
inline auto float2int(f32 const f) noexcept -> i32
{
    /* We can't rely on SSE or the compiler generated conversion if we want
     * clamping behavior with overflow and underflow.
     */
    const auto conv_i = std::bit_cast<i32>(f);

    const auto sign = (conv_i>>31) | 1;
    const auto shift = ((conv_i>>23)&0xff) - (127+23);

    /* Too small. */
    if(shift < -23) [[unlikely]]
        return 0;
    /* Too large (or NaN). */
    if(shift > 7) [[unlikely]]
        return (sign > 0) ? std::numeric_limits<i32>::max() : std::numeric_limits<i32>::min();

    const auto mant = (conv_i&0x7f'ff'ff) | 0x80'00'00;
    if(shift < 0) [[likely]]
        return (mant >> -shift) * sign;
    return (mant << shift) * sign;
}
/**
 * Converts float-to-uint using standard behavior (truncation). Out of range
 * values are clamped.
 */
[[nodiscard]]
inline auto float2uint(f32 const f) noexcept -> u32
{
    const auto conv_i = std::bit_cast<i32>(f);

    /* A 0 mask for negative values creates a 0 result. */
    const auto mask = static_cast<u32>(conv_i>>31) ^ 0xff'ff'ff'ff_u32;
    const auto shift = ((conv_i>>23)&0xff) - (127+23);

    if(shift < -23) [[unlikely]]
        return 0;
    if(shift > 8) [[unlikely]]
        return std::numeric_limits<u32>::max() & mask;

    const auto mant = gsl::narrow_cast<u32>(conv_i&0x7f'ff'ff) | 0x80'00'00_u32;
    if(shift < 0) [[likely]]
        return (mant >> -shift) & mask;
    return (mant << shift) & mask;
}

/**
 * Rounds a float to the nearest integral value, according to the current
 * rounding mode. This is essentially an inlined version of rintf, although
 * makes fewer promises (e.g. -0 or -0.25 rounded to 0 may result in +0).
 */
[[nodiscard]]
inline auto fast_roundf(f32 f) noexcept -> f32
{
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)) \
    && !defined(__SSE_MATH__)

    float out;
    __asm__ __volatile__("frndint" : "=t"(out) : "0"(f));
    return out;

#elif (defined(__GNUC__) || defined(__clang__)) && defined(__aarch64__)

    float out;
    __asm__ volatile("frintx %s0, %s1" : "=w"(out) : "w"(f));
    return out;

#else

    /* Integral limit, where sub-integral precision is not available for
     * floats.
     */
    static constexpr auto ilim = std::array{
         8388608.0f /*  0x1.0p+23 */,
        -8388608.0f /* -0x1.0p+23 */
    };
    const auto conv_u = std::bit_cast<u32>(f);

    const auto sign = (conv_u>>31u)&0x01u;
    const auto expo = (conv_u>>23u)&0xffu;

    if(expo >= 150/*+23*/) [[unlikely]]
    {
        /* An exponent (base-2) of 23 or higher is incapable of sub-integral
         * precision, so it's already an integral value. We don't need to worry
         * about infinity or NaN here.
         */
        return f;
    }
    /* Adding the integral limit to the value (with a matching sign) forces a
     * result that has no sub-integral precision, and is consequently forced to
     * round to an integral value. Removing the integral limit then restores
     * the initial value rounded to the integral. The compiler should not
     * optimize this out because of non-associative rules on floating-point
     * math (as long as you don't use -fassociative-math,
     * -funsafe-math-optimizations, -ffast-math, or -Ofast, in which case this
     * may break without __builtin_assoc_barrier support).
     */
#if HAS_BUILTIN(__builtin_assoc_barrier)
    return __builtin_assoc_barrier(f + ilim[sign]) - ilim[sign];
#else
    f += ilim[sign];
    return f - ilim[sign];
#endif
#endif
}


// Converts level (mB) to gain.
[[nodiscard]]
inline auto level_mb_to_gain(f32 const x) -> f32
{
    if(x <= -10'000.0f)
        return 0.0f;
    return std::pow(10.0f, x / 2'000.0f);
}

// Converts gain to level (mB).
[[nodiscard]]
inline auto gain_to_level_mb(f32 const x) -> f32
{
    if(x <= 1e-05f)
        return -10'000.0f;
    return std::max(std::log10(x) * 2'000.0f, -10'000.0f);
}

#endif /* AL_NUMERIC_H */
