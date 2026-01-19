#ifndef AL_TYPES_HPP
#define AL_TYPES_HPP

#include <concepts>
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <type_traits>

#include "alformat.hpp"
#include "opthelpers.h"
#include "gsl/gsl"


namespace al {

/* A "weak number" is a standard number type. They are prone to implicit
 * conversions, unexpected type promotions, and signedness mismatches
 * producing unexpected results.
 */
template<typename T>
concept weak_number = std::integral<T> or std::floating_point<T>;

/* Unlike standard C++, this considers integers to larger floating point types
 * to be non-narrowing, e.g. int16 -> float(32) and int32 -> double(64) are
 * fine since all platforms we currently care about won't lose precision.
 */
template<typename T, typename U>
concept can_narrow = sizeof(T) < sizeof(U)
    or (std::unsigned_integral<T> and std::signed_integral<U>)
    or (std::integral<T> and std::floating_point<U>)
    or (sizeof(T) == sizeof(U)
        and ((std::signed_integral<T> and std::unsigned_integral<U>)
            or (std::floating_point<T> and std::integral<U>)));

template<typename T, typename U>
concept has_common = not can_narrow<T, U> or not can_narrow<U, T>;


template<weak_number T, weak_number U> [[nodiscard]] constexpr
auto convert_to(U const &value) noexcept(not can_narrow<T, U>) -> T
{
    if constexpr(not can_narrow<T, U>)
        return static_cast<T>(value);
    else
    {
        if constexpr(std::signed_integral<T> and std::unsigned_integral<U>)
        {
            if(U{std::numeric_limits<T>::max()} < value)
                throw std::out_of_range{"Too large unsigned to signed"};
        }
        else if constexpr(std::unsigned_integral<T> and std::signed_integral<U>)
        {
            if(value < U{0})
                throw std::out_of_range{"Negative signed to unsigned"};
        }

        auto const ret = static_cast<T>(value);
        if(static_cast<U>(ret) != value)
            throw std::out_of_range{"Conversion narrowed"};
        return ret;
    }
}


/* This ConstantNum class is a wrapper to handle various operations with
 * numeric constants (literals, constexpr values). It can only be initialized
 * with weak numeric constant types, and will fail to compile if the provided
 * value doesn't fit the type it's paired against.
 */
template<weak_number T>
struct ConstantNum {
    T const c_val;

    /* NOLINTNEXTLINE(*-explicit-constructor) */
    consteval ConstantNum(weak_number auto const &value) noexcept : c_val{convert_to<T>(value)} { }
};


/* A "strong number" is a numeric type derived from the number class below. It
 * has stronger protections from implicit conversions and automatic type
 * promotions.
 */
template<typename T>
concept strong_number = requires { T::is_strong_number_type; };

template<typename T>
concept strong_integral = strong_number<T> and std::integral<typename T::value_t>;

template<typename T>
concept strong_signed_integral = strong_number<T> and std::signed_integral<typename T::value_t>;

template<typename T>
concept strong_unsigned_integral = strong_number<T>
    and std::unsigned_integral<typename T::value_t>;

template<typename T>
concept strong_floating_point = strong_number<T> and std::floating_point<typename T::value_t>;


/* Strong numbers are implemented using CRTP to act as a mixin of sorts. To
 * define a strong numeric type:
 *
 * struct name : al::number_base<real_type, name> {
 *     using number_base::number_base;
 *     using number_base::operator=;
 * };
 *
 * And to define its formatter:
 *
 * template<typename CharT>
 * struct al::formatter<name, CharT> : name::formatter<CharT> { };
 */
template<weak_number T, typename SelfType>
    requires(not std::is_const_v<T> and not std::is_volatile_v<T>)
class number_base {
    static constexpr auto is_strong_number_type = true;

    friend SelfType;

    /* Defaulted constructor/destructor/copy assignment functions, which will be
     * inherited by the parent type. Allows the type to be trivial.
     */
    constexpr number_base() noexcept = default;
    constexpr number_base(number_base const&) noexcept = default;
    constexpr ~number_base() noexcept = default;
    constexpr auto operator=(number_base const &rhs) & noexcept -> number_base& = default;

public:
    using value_t = T;
    T c_val;

    template<weak_number U> force_inline constexpr explicit
    number_base(U const &value) noexcept(not can_narrow<T, U>) : c_val{convert_to<T>(value)} { }

    /* Copy assignment from another strong number type, only for types that
     * won't narrow.
     */
    template<strong_number U> requires(not std::is_base_of_v<number_base, U>)
    constexpr auto operator=(U const &rhs) & noexcept LIFETIMEBOUND -> number_base&
    {
        static_assert(not can_narrow<T, typename U::value_t>,
            "Invalid narrowing assignment; use .cast_to<U>() or .reinterpret_as<U>() to convert");
        c_val = static_cast<T>(rhs.c_val);
        return *this;
    }

    /* Conversion operator to other strong number types. Will throw if the
     * conversion narrows.
     */
    template<strong_number U> force_inline constexpr explicit
    operator U() noexcept(not can_narrow<typename U::value_t, T>)
    { return U{convert_to<typename U::value_t>(c_val)}; }

    /* Non-narrowing conversion method. */
    template<strong_number U> requires(not can_narrow<typename U::value_t, T>) [[nodiscard]]
    constexpr auto as() const noexcept -> U { return U{convert_to<typename U::value_t>(c_val)}; }

    /* Potentially narrowing conversion method. Throws if the converted value
     * narrows.
     */
    template<strong_number U> [[nodiscard]] constexpr
    auto cast_to() const
        noexcept(not can_narrow<typename U::value_t,T> or std::floating_point<typename U::value_t>)
        -> U
    {
        /* Converting to a floating point type isn't checked here because it's
         * nearly impossible to otherwise ensure a large enough integer or
         * precise enough double will correctly fit in the target type. Since
         * converting to floating point results in the nearest value instead of
         * being modulo wrapped, this should be considered fine.
         */
        if constexpr(std::floating_point<typename U::value_t>)
            return U{static_cast<U::value_t>(c_val)};
        else
            return U{convert_to<typename U::value_t>(c_val)};
    }

    /* "Raw" conversion method, essentially applying a static_cast to the
     * underlying type.
     */
    template<strong_number U> [[nodiscard]] constexpr
    auto reinterpret_as() const noexcept -> U { return U{static_cast<U::value_t>(c_val)}; }

    /* Relevant values for the given type. Offered here as static methods
     * instead of through a separate templated structure.
     */
    static consteval auto min() noexcept { return SelfType{std::numeric_limits<T>::min()}; }
    static consteval auto max() noexcept { return SelfType{std::numeric_limits<T>::max()}; }
    static consteval auto lowest() noexcept { return SelfType{std::numeric_limits<T>::lowest()}; }
    static consteval auto epsilon() noexcept requires std::floating_point<T>
    { return SelfType{std::numeric_limits<T>::epsilon()}; }
    static consteval auto infinity() noexcept requires std::numeric_limits<T>::has_infinity
    { return SelfType{std::numeric_limits<T>::infinity()}; }
    static consteval auto quiet_NaN() noexcept requires std::numeric_limits<T>::has_quiet_NaN
    { return SelfType{std::numeric_limits<T>::quiet_NaN()}; }
    static consteval auto signaling_NaN() noexcept
        requires std::numeric_limits<T>::has_signaling_NaN
    { return SelfType{std::numeric_limits<T>::signaling_NaN()}; }

    /* Three-way comparison operator between strong number types, from which
     * other comparison operators are synthesized. Implicitly handles
     * signedness differences.
     */
    template<strong_number U> [[nodiscard]] force_inline friend constexpr
    auto operator<=>(SelfType const &lhs, U const &rhs) noexcept
    {
        if constexpr(not can_narrow<T, typename U::value_t>)
            return lhs.c_val <=> convert_to<T>(rhs.c_val);
        else if constexpr(not can_narrow<typename U::value_t, T>)
            return convert_to<typename U::value_t>(lhs.c_val) <=> rhs.c_val;
        else if constexpr(std::signed_integral<T> and std::unsigned_integral<typename U::value_t>)
        {
            if(lhs.c_val < T{0})
                return std::strong_ordering::less;
            return static_cast<std::make_unsigned_t<T>>(lhs.c_val) <=> rhs.c_val;
        }
        else if constexpr(std::unsigned_integral<T> and std::signed_integral<typename U::value_t>)
        {
            if(typename U::value_t{0} > rhs.c_val)
                return std::strong_ordering::greater;
            using unsigned_t = std::make_unsigned_t<typename U::value_t>;
            return lhs.c_val <=> static_cast<unsigned_t>(rhs.c_val);
        }
        /* FIXME: Allow comparing more "incompatible" types. This is difficult
         * with floating point because, e.g. 2147483647_i32 < 2147483648.0_f32
         * should be true, except converting 2147483647 to float to directly
         * compare them results in 2147483648.0f, making the result false.
         * Converting 2147483648.0f to int32_t is additionally invalid since an
         * int32_t can't hold 2147483648 (at best you may get -2147483648,
         * which also makes it false). While a double would fix this, the
         * problem remains when comparing i64 and f64.
         */
    }

    /* Three-way comparison operator between a strong number type and numeric
     * constant, from which other comparison operators are synthesized. Only
     * valid when the numeric constant fits the strong number type.
     */
    [[nodiscard]] force_inline friend constexpr
    auto operator<=>(SelfType const &lhs, ConstantNum<T> const &rhs) noexcept
    { return lhs.c_val <=> rhs.c_val; }

    /* Prefix and postfix increment and decrement operators. Only valid for
     * integral types.
     */
    force_inline friend constexpr
    auto operator++(SelfType &self LIFETIMEBOUND) noexcept -> SelfType& requires std::integral<T>
    { ++self.c_val; return self; }
    [[nodiscard]] force_inline friend constexpr
    auto operator++(SelfType &self, int) noexcept -> SelfType requires std::integral<T>
    {
        auto const old = self;
        ++self.c_val;
        return old;
    }
    force_inline friend constexpr
    auto operator--(SelfType &self LIFETIMEBOUND) noexcept -> SelfType& requires std::integral<T>
    { --self.c_val; return self; }
    [[nodiscard]] force_inline friend constexpr
    auto operator--(SelfType &self, int) noexcept -> SelfType requires std::integral<T>
    {
        auto const old = self;
        --self.c_val;
        return old;
    }

    /* No automatic type promotion for our unary ops. Unary - is only valid for
     * signed types.
     */
    [[nodiscard]] force_inline friend constexpr
    auto operator-(SelfType const &value) noexcept -> SelfType
    {
        static_assert(std::is_signed_v<T>, "operator- is only valid for signed types");
        return SelfType{static_cast<T>(-value.c_val)};
    }
    [[nodiscard]] force_inline friend constexpr
    auto operator~(SelfType const &value) noexcept -> SelfType
    { return SelfType{static_cast<T>(~value.c_val)}; }

    /* Our binary ops only promote to the larger of the two operands, when the
     * conversion can't narrow (e.g. signed char + short = short, while
     * unsigned + short = error).
     */

    /* Binary ops +, -, *, /, %, |, &, and ^ between strong number types. */
#define DECL_BINARY(op)                                                       \
    template<strong_number U> [[nodiscard]] force_inline friend constexpr     \
    auto operator op(SelfType const &lhs, U const &rhs) noexcept              \
    {                                                                         \
        static_assert(has_common<T, typename U::value_t>,                     \
            "Incompatible operands");                                         \
        if constexpr(not can_narrow<T, typename U::value_t>)                  \
            return SelfType{static_cast<T>(lhs.c_val op rhs.c_val)};          \
        else if constexpr(not can_narrow<typename U::value_t, T>)             \
            return U{static_cast<typename U::value_t>(lhs.c_val op rhs.c_val)}; \
        else                                                                  \
            return SelfType{};                                                \
    }
    DECL_BINARY(+)
    DECL_BINARY(-)
    DECL_BINARY(*)
    DECL_BINARY(/)
    DECL_BINARY(%)
    DECL_BINARY(|)
    DECL_BINARY(&)
    DECL_BINARY(^)
#undef DECL_BINARY
    /* Binary ops >> and << between strong number types. Only available for
     * integer types, and the right-side operand must be an unsigned type.
     */
#define DECL_BINARY(op)                                                       \
    template<strong_number U> [[nodiscard]] force_inline friend constexpr     \
    auto operator op(SelfType const &lhs, U const &rhs) noexcept              \
    {                                                                         \
        static_assert(std::unsigned_integral<typename U::value_t>,            \
            "Right-side operand must be an unsigned integer");                \
        return SelfType{static_cast<T>(lhs.c_val op rhs.c_val)};              \
    }
    DECL_BINARY(>>)
    DECL_BINARY(<<)
#undef DECL_BINARY

    /* Binary ops +, -, *, /, %, |, &, and ^ between a strong number type and
     * numeric constant.
     */
#define DECL_BINARY(op)                                                       \
    [[nodiscard]] force_inline friend constexpr                               \
    auto operator op(SelfType const &lhs, ConstantNum<T> const &rhs) noexcept \
    { return SelfType{static_cast<T>(lhs.c_val op rhs.c_val)}; }              \
    [[nodiscard]] force_inline friend constexpr                               \
    auto operator op(ConstantNum<T> const &lhs, SelfType const &rhs) noexcept \
    { return SelfType{static_cast<T>(lhs.c_val op rhs.c_val)}; }
    DECL_BINARY(+)
    DECL_BINARY(-)
    DECL_BINARY(*)
    DECL_BINARY(/)
    DECL_BINARY(%)
    DECL_BINARY(|)
    DECL_BINARY(&)
    DECL_BINARY(^)
#undef DECL_BINARY
    /* Binary ops >> and << between a strong number type and integer constant.
     * The shift amount must fit into an u8 regardless of the left-side type.
     */
#define DECL_BINARY(op)                                                       \
    [[nodiscard]] force_inline friend constexpr                               \
    auto operator op(SelfType const &lhs, ConstantNum<std::uint8_t> const &rhs) noexcept \
    { return SelfType{static_cast<T>(lhs.c_val op rhs.c_val)}; }
    DECL_BINARY(>>)
    DECL_BINARY(<<)
#undef DECL_BINARY

    /* Our binary assignment ops only promote the rhs value to the lhs type
     * when the conversion can't lose information, and produces an error
     * otherwise.
     */

    /* Binary assignment ops +=, -=, *=, /=, %=, |=, &=, and ^= between strong
     * number types. %=, |=, &=, and ^= are only available for integer types.
     */
#define DECL_BINASSIGN(op)                                                    \
    template<strong_number U> force_inline friend constexpr                   \
    auto operator op(SelfType &lhs LIFETIMEBOUND, U const &rhs) noexcept      \
        -> SelfType&                                                          \
    {                                                                         \
        static_assert(not can_narrow<T, typename U::value_t>,                 \
            "Incompatible right side operand");                               \
        lhs.c_val op rhs.c_val;                                               \
        return lhs;                                                           \
    }
    DECL_BINASSIGN(+=)
    DECL_BINASSIGN(-=)
    DECL_BINASSIGN(*=)
    DECL_BINASSIGN(/=)
    DECL_BINASSIGN(%=)
    DECL_BINASSIGN(|=)
    DECL_BINASSIGN(&=)
    DECL_BINASSIGN(^=)
#undef DECL_BINASSIGN
    /* Binary assignment ops >>= and <<= between strong number types. Only
     * available for integer types, and the right operand must be an unsigned
     * type.
     */
#define DECL_BINASSIGN(op)                                                    \
    template<strong_number U> force_inline friend constexpr                   \
    auto operator op(SelfType &lhs LIFETIMEBOUND, U const &rhs) noexcept      \
        -> SelfType&                                                          \
    {                                                                         \
        static_assert(std::unsigned_integral<typename U::value_t>,            \
            "Right side operand must be unsigned");                           \
        lhs.c_val op rhs.c_val;                                               \
        return lhs;                                                           \
    }
    DECL_BINASSIGN(>>=)
    DECL_BINASSIGN(<<=)
#undef DECL_BINASSIGN

    /* Binary assignment ops +=, -=, *=, /=, %=, |=, &=, and ^= between a
     * strong number type and numeric constant.
     */
#define DECL_BINASSIGN(op)                                                    \
    force_inline friend constexpr                                             \
    auto operator op(SelfType &lhs LIFETIMEBOUND, ConstantNum<T> const &rhs)  \
        noexcept -> SelfType&                                                 \
    { lhs.c_val op rhs.c_val; return lhs; }
    DECL_BINASSIGN(+=)
    DECL_BINASSIGN(-=)
    DECL_BINASSIGN(*=)
    DECL_BINASSIGN(/=)
    DECL_BINASSIGN(%=)
    DECL_BINASSIGN(|=)
    DECL_BINASSIGN(&=)
    DECL_BINASSIGN(^=)
#undef DECL_BINASSIGN
    /* Binary assignment ops >>= and <<= between a strong number type and
     * integer constant. The shift amount must fit into an u8 regardless of the
     * left operand type.
     */
#define DECL_BINASSIGN(op)                                                    \
    force_inline friend constexpr                                             \
    auto operator op(SelfType &lhs LIFETIMEBOUND, ConstantNum<std::uint8_t> const &rhs)  \
        noexcept -> SelfType&                                                 \
    { lhs.c_val op rhs.c_val; return lhs; }
    DECL_BINASSIGN(>>=)
    DECL_BINASSIGN(<<=)
#undef DECL_BINASSIGN

    /* Force printing smaller types as int. Always treat these as numeric
     * values even when backed by character types.
     */
    using fmttype_t = std::conditional_t<not can_narrow<int, T>, int, T>;
    template<typename CharT>
    struct formatter : al::formatter<fmttype_t, CharT> {
        auto format(SelfType const &obj, auto& ctx) const
        {
            return al::formatter<fmttype_t,CharT>::format(al::convert_to<fmttype_t>(obj.c_val),
                ctx);
        }
    };
};

}


struct i8 : al::number_base<std::int8_t, i8> { using number_base::number_base; using number_base::operator=; };
template<typename CharT> struct al::formatter<i8, CharT> : i8::formatter<CharT> { };

using u8 = std::uint8_t;
using i16 = std::int16_t;
using u16 = std::uint16_t;
using i32 = std::int32_t;
using u32 = std::uint32_t;
using i64 = std::int64_t;
using u64 = std::uint64_t;
using isize = std::make_signed_t<std::size_t>;
using usize = std::size_t;
using f32 = float;
using f64 = double;


[[nodiscard]] consteval
auto operator ""_i8(unsigned long long const n) noexcept { return i8{n}; }
[[nodiscard]] consteval
auto operator ""_u8(unsigned long long const n) noexcept { return gsl::narrow<u8>(n); }

[[nodiscard]] consteval
auto operator ""_i16(unsigned long long const n) noexcept { return gsl::narrow<i16>(n); }
[[nodiscard]] consteval
auto operator ""_u16(unsigned long long const n) noexcept { return gsl::narrow<u16>(n); }

[[nodiscard]] consteval
auto operator ""_i32(unsigned long long const n) noexcept { return gsl::narrow<i32>(n); }
[[nodiscard]] consteval
auto operator ""_u32(unsigned long long const n) noexcept { return gsl::narrow<u32>(n); }

[[nodiscard]] consteval
auto operator ""_i64(unsigned long long const n) noexcept { return gsl::narrow<i64>(n); }
[[nodiscard]] consteval
auto operator ""_u64(unsigned long long const n) noexcept { return gsl::narrow<u64>(n); }

[[nodiscard]] consteval
auto operator ""_z(unsigned long long const n) noexcept { return gsl::narrow<isize>(n); }
[[nodiscard]] consteval
auto operator ""_uz(unsigned long long const n) noexcept { return gsl::narrow<usize>(n); }
[[nodiscard]] consteval
auto operator ""_zu(unsigned long long const n) noexcept { return gsl::narrow<usize>(n); }

[[nodiscard]] consteval
auto operator ""_f32(long double const n) noexcept { return static_cast<f32>(n); }
[[nodiscard]] consteval
auto operator ""_f64(long double const n) noexcept { return static_cast<f64>(n); }

#endif /* AL_TYPES_HPP */
