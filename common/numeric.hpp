#ifndef COMMON_NUMERIC_HPP
#define COMMON_NUMERIC_HPP

#include "config_simd.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string_view>
#include <type_traits>
#ifdef HAVE_INTRIN_H
#include <intrin.h>
#endif
#if HAVE_SSE_INTRINSICS
#include <emmintrin.h>
#endif

#include "opthelpers.h"


namespace al {

#if HAS_BUILTIN(__builtin_add_overflow)
template<std::integral T> [[nodiscard]] constexpr
auto saturating_add(T const lhs, T const rhs) noexcept -> T
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
constexpr auto saturating_add(T const lhs, T const rhs) noexcept -> T
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

namespace detail {

    template<typename T>
    concept number = std::integral<T> or std::floating_point<T>;

    template<std::floating_point T> [[nodiscard]] constexpr
    auto unsigned_from_fp(T const& val) noexcept
    {
        if constexpr(sizeof(T) == sizeof(std::uint32_t))
            return std::bit_cast<std::uint32_t>(val);
        else if constexpr(sizeof(T) == sizeof(std::uint64_t))
            return std::bit_cast<std::uint64_t>(val);
        else if constexpr(sizeof(T) == sizeof(std::uint16_t))
            return std::bit_cast<std::uint16_t>(val);
        else if constexpr(sizeof(T) == sizeof(std::uint8_t))
            return std::bit_cast<std::uint8_t>(val);
        else
        {
            static_assert(al::dependent_false<T>, "Unexpected floating point type");
            return 0u;
        }
    }

}

/* Our saturating_cast supports floating point (currently requires radix == 2,
 * and doesn't support larger than 64-bit types).
 */
template<detail::number R, detail::number T> [[nodiscard]] constexpr
auto saturating_cast(T const val) noexcept -> R
{
    if constexpr(std::floating_point<T>)
    {
        if constexpr(std::integral<R>) /* fp -> integer */
        {
            /* mantbits is the number of mantissa bits physically stored in the
             * variable. 32- and 64-bit fp formats (float and double) have an
             * implied 1 bit that is included in the digits count but not
             * actually stored.
             * TODO: The 80-bit extended precision format (long double in GCC,
             * Clang, etc.) makes the extra 1 bit explicit in the mantissa.
             */
            auto constexpr mantbits = unsigned{std::numeric_limits<T>::digits - 1};
            auto constexpr max_exponent = unsigned{std::numeric_limits<T>::max_exponent};
            auto constexpr expbits = unsigned{std::bit_width(max_exponent)};
            /* expbias is the exponent value that encodes for 1.xxxx. This
             * corresponds to a left-shift of 1-fpdigits (or a right-shift of
             * fpdigits-1) for the mantissa bits. This is also the minimum
             * viable exponent, as anything less will shift the mantissa to 0.
             */
            auto constexpr expbias = unsigned{(1<<(expbits-1)) - 1};
            /* expoffset is the exponent value that results in no shift to the
             * mantissa bits.
             */
            auto constexpr expoffset = unsigned{int{expbias} + std::numeric_limits<T>::digits - 1};
            /* expmax is the maximum viable exponent value. Anything greater
             * will overflow (for positive values), or either underflow or
             * result in exactly the integer minimum (for negative values).
             */
            auto constexpr expmax = unsigned{std::numeric_limits<R>::digits
                - std::numeric_limits<T>::digits + int{expoffset}};

            /* Values for extracting and building the result. The type needs to
             * be large enough to handle the fp value as an integer and the
             * result type (unsigned int is included to avoid automatic type
             * promotions).
             */
            using tmpint_t = std::common_type_t<unsigned, std::make_unsigned_t<R>,
                decltype(detail::unsigned_from_fp(val))>;
            auto constexpr expmask = ((tmpint_t{1}<<expbits) - 1) << mantbits;
            auto constexpr signmask = tmpint_t{1} << (mantbits+expbits);
            auto constexpr impliedbit = tmpint_t{1} << mantbits;
            auto constexpr mantissamask = tmpint_t{impliedbit - 1};

            auto const conv_u = detail::unsigned_from_fp(val);
            auto const exponent = (conv_u&expmask) >> mantbits;
            if(exponent < expbias)
                return R{0};

            if constexpr(std::signed_integral<R>) /* fp -> signed integer */
            {
                if(exponent > expmax) [[unlikely]]
                {
                    /* Convert NaN to 0. */
                    if(exponent == (expmask>>mantbits) and (conv_u&mantissamask) != 0)
                        return R{0};
                    return (conv_u&signmask) ? std::numeric_limits<R>::min()
                        : std::numeric_limits<R>::max();
                }

                auto const sign = (conv_u&signmask) ? R{-1} : R{1};
                auto const mant = tmpint_t{(conv_u&mantissamask) | impliedbit};
                if(exponent < expoffset) [[likely]]
                    return static_cast<R>(static_cast<R>(mant >> (expoffset-exponent)) * sign);
                return static_cast<R>(static_cast<R>(mant << (exponent-expoffset)) * sign);
            }
            else if constexpr(std::unsigned_integral<R>) /* fp -> unsigned integer */
            {
                if(exponent > expmax) [[unlikely]]
                {
                    /* Convert NaN to 0. */
                    if(exponent == (expmask>>mantbits) and (conv_u&mantissamask) != 0)
                        return R{0};
                    return (conv_u&signmask) ? R{0} : std::numeric_limits<R>::max();
                }

                /* A 0 mask for negative values creates a 0 result. */
                auto const mask = (conv_u&signmask) ? R{0} : std::numeric_limits<R>::max();
                auto const mant = tmpint_t{(conv_u&mantissamask) | impliedbit};
                if(exponent < expoffset) [[likely]]
                    return static_cast<R>((mant >> (expoffset-exponent)) & mask);
                return static_cast<R>((mant << (exponent-expoffset)) & mask);
            }
            else
            {
                static_assert(al::dependent_false<R>, "Unexpected numeric type conversion");
                return R{};
            }
        }
        else
        {
            /* fp -> fp automatically saturates. */
            static_assert(std::floating_point<R>, "Unexpected numeric type conversion");
            return static_cast<R>(val);
        }
    }
    else if constexpr(std::integral<R>) /* integer -> integer */
    {
        if constexpr(std::numeric_limits<R>::digits < std::numeric_limits<T>::digits)
        {
            if constexpr(std::signed_integral<R> && std::signed_integral<T>)
            {
                if(val < std::numeric_limits<R>::min())
                    return std::numeric_limits<R>::min();
            }
            if(val > T{std::numeric_limits<R>::max()})
                return std::numeric_limits<R>::max();
        }
        if constexpr(std::unsigned_integral<R> && std::signed_integral<T>)
        {
            if(val < 0)
                return R{0};
        }
        return static_cast<R>(val);
    }
    else
    {
        /* integer -> fp automatically saturates. */
        static_assert(std::integral<T> and std::floating_point<R>,
            "Unexpected numeric type conversion");
        return static_cast<R>(val);
    }
}

} /* namespace al */


template<std::integral T> [[nodiscard]] constexpr
auto as_unsigned(T const value) noexcept
{ return static_cast<std::make_unsigned_t<T>>(value); }

template<std::integral T> [[nodiscard]] constexpr
auto as_signed(T const value) noexcept
{ return static_cast<std::make_signed_t<T>>(value); }


[[nodiscard]] constexpr
auto GetCounterSuffix(std::size_t const count) noexcept -> std::string_view
{
    using namespace std::string_view_literals;
    return (((count%100)/10) == 1) ? "th"sv :
        ((count%10) == 1) ? "st"sv :
        ((count%10) == 2) ? "nd"sv :
        ((count%10) == 3) ? "rd"sv : "th"sv;
}


[[nodiscard]] constexpr
auto lerpf(float const val1, float const val2, float const mu) noexcept -> float
{ return val1 + (val2-val1)*mu; }


/**
 * If the value is not already a multiple of r, round toward zero to the next
 * multiple.
 */
template<std::integral T> [[nodiscard]] constexpr
auto RoundToZero(T value, std::type_identity_t<T> r) noexcept -> T
{ return value - (value%r); }

/**
 * If the value is not already a multiple of r, round away from zero to the
 * next multiple.
 */
template<std::integral T> [[nodiscard]] constexpr
auto RoundFromZero(T value, std::type_identity_t<T> r) noexcept -> T
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
[[nodiscard]] inline
auto fastf2i(float const f) noexcept -> int
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

    return static_cast<int>(f);
#endif
}
[[nodiscard]] inline
auto fastf2u(float const f) noexcept -> unsigned
{ return static_cast<unsigned>(fastf2i(f)); }


namespace detail_ {

/* Integral limit, where sub-integral precision is not available for floats. */
inline constexpr auto ilim = std::array{
    8388608.0f /*  0x1.0p+23 */,
   -8388608.0f /* -0x1.0p+23 */
};

}

/**
 * Rounds a float to the nearest integral value, according to the current
 * rounding mode. This is essentially an inlined version of rintf, although
 * makes fewer promises (e.g. -0 or -0.25 rounded to 0 may result in +0).
 */
[[nodiscard]] inline
auto fast_roundf(float f) noexcept -> float
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

    const auto conv_u = std::bit_cast<unsigned>(f);
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
    return __builtin_assoc_barrier(f + detail_::ilim[sign]) - detail_::ilim[sign];
#else
    f += detail_::ilim[sign];
    return f - detail_::ilim[sign];
#endif
#endif
}


// Converts level (mB) to gain.
[[nodiscard]] constexpr
auto level_mb_to_gain(float const x) -> float
{
    if(x <= -10'000.0f)
        return 0.0f;
    return std::pow(10.0f, x / 2'000.0f);
}

// Converts gain to level (mB).
[[nodiscard]] constexpr
auto gain_to_level_mb(float const x) -> float
{
    if(x <= 1e-05f)
        return -10'000.0f;
    return std::max(std::log10(x) * 2'000.0f, -10'000.0f);
}

#endif /* COMMON_NUMERIC_HPP */
