#ifndef AL_TYPES_HPP
#define AL_TYPES_HPP

#include <bit>
#include <compare>
#include <concepts>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#include "opthelpers.h"


struct i8;
struct u8;
struct i16;
struct u16;
struct i32;
struct u32;
struct i64;
struct u64;
struct f32;
struct f64;
struct isize;
struct usize;

namespace al {

struct narrowing_error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]]
auto throw_narrowing_error(std::string_view prefix, long long value, std::string_view type)
    -> void;
[[noreturn]]
auto throw_narrowing_error(std::string_view prefix, unsigned long long value,
    std::string_view type) -> void;
[[noreturn]]
auto throw_narrowing_error(std::string_view prefix, long double value, std::string_view type)
    -> void;


template<typename>
concept dependent_false = false;

template<typename T> [[nodiscard]] consteval
auto get_type_name() -> std::string_view
{
    using namespace std::string_view_literals;
    if constexpr(std::same_as<T, int8_t>)
        return "int8_t"sv;
    else if constexpr(std::same_as<T, uint8_t>)
        return "uint8_t"sv;
    else if constexpr(std::same_as<T, int16_t>)
        return "int16_t"sv;
    else if constexpr(std::same_as<T, uint16_t>)
        return "uint16_t"sv;
    else if constexpr(std::same_as<T, int32_t>)
        return "int32_t"sv;
    else if constexpr(std::same_as<T, uint32_t>)
        return "uint32_t"sv;
    else if constexpr(std::same_as<T, int64_t>)
        return "int64_t"sv;
    else if constexpr(std::same_as<T, uint64_t>)
        return "uint64_t"sv;
    else if constexpr(std::same_as<T, float>)
        return "float"sv;
    else if constexpr(std::same_as<T, double>)
        return "double"sv;
    /* In case some built-in types aren't aliased to the above: */
    else if constexpr(std::same_as<T, short>)
        return "short"sv;
    else if constexpr(std::same_as<T, unsigned short>)
        return "unsigned short"sv;
    else if constexpr(std::same_as<T, int>)
        return "int"sv;
    else if constexpr(std::same_as<T, unsigned int>)
        return "unsigned int"sv;
    else if constexpr(std::same_as<T, long>)
        return "long"sv;
    else if constexpr(std::same_as<T, unsigned long>)
        return "unsigned long"sv;
    else if constexpr(std::same_as<T, long long>)
        return "long long"sv;
    else if constexpr(std::same_as<T, unsigned long long>)
        return "unsigned long long"sv;
    else
    {
        static_assert(dependent_false<T>, "Unexpected type");
        return ""sv;
    }
}

template<typename>
struct make_strict { };

template<> struct make_strict<std::int8_t> { using type = i8; };
template<> struct make_strict<std::uint8_t> { using type = u8; };
template<> struct make_strict<std::int16_t> { using type = i16; };
template<> struct make_strict<std::uint16_t> { using type = u16; };
template<> struct make_strict<std::int32_t> { using type = i32; };
template<> struct make_strict<std::uint32_t> { using type = u32; };
template<> struct make_strict<std::int64_t> { using type = i64; };
template<> struct make_strict<std::uint64_t> { using type = u64; };
template<> struct make_strict<float> { using type = f32; };
template<> struct make_strict<double> { using type = f64; };

template<typename T>
using make_strict_t = typename make_strict<T>::type;

} /* namespace al */

using sys_int = al::make_strict_t<int>;
using sys_uint = al::make_strict_t<unsigned>;

namespace al {

/* A "weak number" is a standard number type. They are prone to implicit
 * conversions, unexpected type promotions, and signedness mismatches,
 * producing unexpected results.
 */
template<typename T>
concept weak_number = std::integral<T> or std::floating_point<T>;

/* Unlike standard C++, this considers integers to larger floating point types
 * to be non-narrowing, e.g. int16 -> float(32) and int32 -> double(64) are
 * fine since all platforms we currently care about won't lose precision.
 */
template<typename To, typename From>
concept might_narrow = sizeof(To) < sizeof(From)
    or (std::unsigned_integral<To> and std::signed_integral<From>)
    or (std::integral<To> and std::floating_point<From>)
    or (sizeof(To) == sizeof(From)
        and ((std::signed_integral<To> and std::unsigned_integral<From>)
            or (std::floating_point<To> and std::integral<From>)));

template<typename T, typename U>
concept has_common = not might_narrow<T, U> or not might_narrow<U, T>;


template<weak_number To, weak_number From> [[nodiscard]] constexpr
auto convert_to(From const &value) noexcept(not might_narrow<To, From>) -> To
{
    if constexpr(not might_narrow<To, From>)
        return static_cast<To>(value);
    else
    {
        using cast_type = std::conditional_t<std::floating_point<From>, long double,
            std::conditional_t<std::unsigned_integral<From>, unsigned long long, long long>>;

        if constexpr(std::signed_integral<To> and std::unsigned_integral<From>)
        {
            if(From{std::numeric_limits<To>::max()} < value)
                throw_narrowing_error("convert_to", cast_type{value}, get_type_name<To>());
        }
        else if constexpr(std::unsigned_integral<To> and std::signed_integral<From>)
        {
            if(value < From{0})
                throw_narrowing_error("convert_to", cast_type{value}, get_type_name<To>());
        }

        auto const ret = static_cast<To>(value);
        if(static_cast<From>(ret) != value)
            throw_narrowing_error("convert_to", cast_type{value}, get_type_name<To>());
        return ret;
    }
}


/* A "strict number" is a numeric type derived from the number class below. It
 * has stronger protections from implicit conversions and automatic type
 * promotions.
 */
template<typename T>
concept strict_number = requires { T::is_strict_number_type; }
    and std::derived_from<T, typename T::self_t>;

template<typename T>
concept strict_integral = strict_number<T> and std::integral<typename T::value_t>;

template<typename T>
concept strict_signed_integral = strict_number<T> and std::signed_integral<typename T::value_t>;

template<typename T>
concept strict_unsigned_integral = strict_number<T>
    and std::unsigned_integral<typename T::value_t>;

template<typename T>
concept strict_floating_point = strict_number<T> and std::floating_point<typename T::value_t>;


/* Determines whether a weak number of type T can be used for an operation on a
 * strict number's underlying type U. This is intended to restrict otherwise
 * valid numeric constants that could be confusing. For example:
 *
 * auto var = u8{...};
 * auto var_doubled = var * 2;
 *
 * Here 2 is a "weak" int, which is required to convert to a "strict" u8. This
 * would be a narrowing conversion (usually), but 2 is a constant value that
 * can be represented in an u8. This effectively results in:
 *
 * auto var_doubled = var * 2_u8;
 *
 * which is valid and reasonable. In contrast, however:
 *
 * auto var = u32{...};
 * auto rcp = 1.0f / var;
 *
 * Here 1.0f is a "weak" float, which is required to convert to a "strict" u32.
 * Normally this would be a narrowing conversion, but 1.0f is a constant value
 * that can be represented as an u32 type, so this would convert and compile.
 * The result being:
 *
 * auto rcp = 1_u32 / var;
 *
 * which is a completely valid computation, but likely not what was expected.
 * This kind of conversion should be rejected, and instead require explicit
 * types:
 *
 * auto rcp = 1.0f / var.cast_to<f32>();
 *
 * will compile and work as expected.
 */
template<typename ConstType, typename VarType>
concept compatible_constant = (std::integral<ConstType> and std::integral<VarType>)
    or (std::floating_point<VarType>
        and (std::integral<ConstType> or sizeof(ConstType) <= sizeof(VarType)));


/* Models a weak number type that can be converted to the given strict number
 * type without narrowing.
 */
template<typename WeakType, typename StrictType>
concept compatible_weak_number = weak_number<WeakType> and strict_number<StrictType>
    and not might_narrow<typename StrictType::value_t, WeakType>;


/* This ConstantNum class is a wrapper to handle various operations with
 * numeric constants (literals, constexpr values). It can be initialized with
 * weak numeric constant types, and will fail to compile if the provided value
 * doesn't fit the type it's paired against.
 */
template<weak_number T>
struct ConstantNum {
    T const c_val;

    /* NOLINTBEGIN(*-explicit-constructor) */
    template<compatible_constant<T> U>
    consteval ConstantNum(U const &value) noexcept : c_val{convert_to<T>(value)} { }
    /* NOLINTEND(*-explicit-constructor) */
};


namespace detail_ {

template<typename>
struct signed_difference { using type = void; };

template<std::integral T>
struct signed_difference<T> { using type = std::make_signed_t<T>; };

}

/* Strict numbers are implemented using CRTP to act as a mixin of sorts. */
template<weak_number ValueType, typename SelfType>
class number_base {
    static_assert(not std::is_const_v<ValueType> and not std::is_volatile_v<ValueType>);

    friend SelfType;

    /* Defaulted constructor/destructor/copy assignment functions, which will be
     * inherited by the parent type. Allows the type to be trivial.
     */
    constexpr number_base() noexcept = default;
    constexpr number_base(number_base const&) noexcept = default;
    constexpr ~number_base() noexcept = default;
    constexpr
    auto operator=(number_base const &rhs) & noexcept LIFETIMEBOUND -> number_base& = default;

public:
    static constexpr auto is_strict_number_type = true;

    using value_t = ValueType;
    using self_t = SelfType;
    /* HACK: Annoyingly, iota_view has some strict requirements for what only
     * needs to be incrementable. It needs std::iter_difference_t<T> to be
     * "signed-integer-like", which is defined as an alias to either
     * std::iterator_traits<std::remove_cvref_t<T>>::difference_type or
     * std::incrementable_traits<std::remove_cvref_t<T>>::difference_type.
     * As this is not an iterator, that means working with
     * incrementable_traits::difference_type, which itself is defined in one of
     * two ways:
     *
     * T::difference_type is declared as a "signed-integer-like" type, or
     * the result of T() - T() being a std::integral type (converted to signed
     * via std::make_signed_t).
     *
     * In this case, T() - T() results in T, not a standard integral, leaving
     * T::difference_type. To be "signed-integer-like" in turn means to be a
     * standard signed integral type, or a type that behaves like a signed
     * integer *with a width larger than any standard integer type*.
     *
     * Here, we define a difference_type type that is the standard signed
     * integral type this strict number models, which satisfies the definition.
     * Although subtracting two strict numbers does not result in this type,
     * and I don't know of any standard mechanism to generate or apply this
     * difference type from/to the incrementable object(s). That makes it feel
     * like we follow the technical requirements but not the intended
     * requirements, which could be prone to breaking.
     *
     * For its part, cppreference does not specify how this difference_type may
     * be used with weakly_incrementable (or plain incrementable) objects, so I
     * have no idea what the purpose of having this type is. Perhaps it may be
     * enough to have an explicit conversion operator to this type, and support
     * operator+ and operator- with this type (templated to require this type
     * specifically, to help prevent implicit conversions to/from other integer
     * types; so x = difference_type(a - b) and b = a + difference_type(x) will
     * work). But if it expects a - b to result in a difference_type type
     * implicitly, or to add anything that can be implicitly converted to
     * difference_type, that will obviously not work.
     *
     * Floating point types define this to void, making this not incrementable.
     * Which is perfectly fine, since standard fp types aren't either.
     */
    using difference_type = typename detail_::signed_difference<ValueType>::type;

    /* Force printing smaller types as (unsigned) int. Always treat these as
     * numeric values even when backed by character types.
     */
    using fmttype_t = std::conditional_t<not might_narrow<unsigned, ValueType>, unsigned,
        std::conditional_t<not might_narrow<int, ValueType>, int,
        ValueType>>;


    ValueType c_val;

    /* Implicit constructor from non-narrowing weak number types. */
    template<weak_number U> requires(not might_narrow<ValueType, U>) force_inline constexpr
    explicit(false) number_base(U const &value) noexcept : c_val{static_cast<ValueType>(value)} { }

    /* Implicit constructor from narrowing weak number types. Required to be
     * compile-time so the provided value can be checked for narrowing.
     */
    constexpr explicit(false)
    number_base(ConstantNum<ValueType> const &value) noexcept : c_val{value.c_val} { }

    template<weak_number U> force_inline static constexpr
    auto from(U const &value)
        noexcept(not might_narrow<ValueType, U> or std::floating_point<ValueType>)
        -> SelfType
    {
        /* Converting to a floating point type isn't checked here because it's
         * nearly impossible to otherwise ensure a large enough integer or
         * precise enough double will correctly fit in the target type. Since
         * converting to floating point results in the nearest value instead of
         * being modulo wrapped, this should be considered fine.
         */
        if constexpr(std::floating_point<ValueType>)
            return SelfType{static_cast<ValueType>(value)};
        else
            return SelfType{convert_to<ValueType>(value)};
    }

    [[nodiscard]] force_inline static constexpr
    auto bit_pack(std::byte const value) noexcept -> SelfType requires(sizeof(value_t) == 1)
    {
        return std::bit_cast<SelfType>(value);
    }

    [[nodiscard]] force_inline static constexpr
    auto bit_pack(std::byte const hi, std::byte const lo) noexcept -> SelfType
        requires(sizeof(value_t) == 2)
    {
        auto const ret = static_cast<std::uint16_t>((to_integer<std::uint16_t>(hi)<<8)
            | to_integer<std::uint16_t>(lo));
        return std::bit_cast<SelfType>(ret);
    }

    [[nodiscard]] force_inline static constexpr
    auto bit_pack(std::byte const hi, std::byte const midhi, std::byte const midlo,
        std::byte const lo) noexcept -> SelfType requires(sizeof(value_t) == 4)
    {
        auto const ret = (to_integer<std::uint32_t>(hi)<<24)
            | (to_integer<std::uint32_t>(midhi)<<16) | (to_integer<std::uint32_t>(midlo)<<8)
            | to_integer<std::uint32_t>(lo);
        return std::bit_cast<SelfType>(ret);
    }

    [[nodiscard]] force_inline static constexpr
    auto bit_pack(std::byte const hi, std::byte const mid6, std::byte const mid5,
        std::byte const mid4, std::byte const mid3, std::byte const mid2, std::byte const mid1,
        std::byte const lo) noexcept -> SelfType requires(sizeof(value_t) == 8)
    {
        auto const ret = (to_integer<std::uint64_t>(hi)<<56)
            | (to_integer<std::uint64_t>(mid6)<<48) | (to_integer<std::uint64_t>(mid5)<<40)
            | (to_integer<std::uint64_t>(mid4)<<32) | (to_integer<std::uint64_t>(mid3)<<24)
            | (to_integer<std::uint64_t>(mid2)<<16) | (to_integer<std::uint64_t>(mid1)<<8)
            | to_integer<std::uint64_t>(lo);
        return std::bit_cast<SelfType>(ret);
    }

    /* Conversion operator to other strict number types. Only valid for
     * non-narrowing conversions.
     */
    template<strict_number U> requires(not might_narrow<typename U::value_t, ValueType>)
        force_inline constexpr explicit
    operator U() noexcept { return U{static_cast<typename U::value_t>(c_val)}; }

    template<std::same_as<difference_type> U> [[nodiscard]] force_inline constexpr explicit
    operator U() noexcept { return static_cast<U>(c_val); }

    [[nodiscard]] force_inline constexpr explicit
    operator bool() noexcept requires(std::integral<ValueType>) { return c_val != ValueType{0}; }

    /* .as<T>() is a non-narrowing conversion method. Non-narrowing type
     * conversions are simply cast to the target type, while potentially
     * narrowing conversions are checked at compile-time and will cause a
     * compilation failure if the value is either not known at compile-time, or
     * can't fit the target type.
     */
    template<strict_number U> requires(not might_narrow<typename U::value_t, ValueType>)
    [[nodiscard]] force_inline constexpr
    auto as() const noexcept -> U { return U{static_cast<typename U::value_t>(c_val)}; }

    template<strict_number U> [[nodiscard]] consteval
    auto as() const noexcept -> U { return U{convert_to<typename U::value_t>(c_val)}; }

    /* Potentially narrowing conversion method. Throws a narrowing_error
     * exception if the converted value narrows.
     */
    template<strict_number U> [[nodiscard]] force_inline constexpr
    auto cast_to() const
        noexcept(not might_narrow<typename U::value_t, ValueType>
            or std::floating_point<typename U::value_t>)
        -> U
    {
        /* Like from(), converting to a floating point type isn't checked here. */
        if constexpr(std::floating_point<typename U::value_t>)
            return U{static_cast<typename U::value_t>(c_val)};
        else
            return U{convert_to<typename U::value_t>(c_val)};
    }

    /* "Raw" conversion method, essentially applying a static_cast to the
     * underlying type without checking for narrowing.
     */
    template<strict_number U> [[nodiscard]] force_inline constexpr
    auto reinterpret_as() const noexcept -> U
    { return U{static_cast<typename U::value_t>(c_val)}; }

    /* Saturating cast, applying a standard cast except out of range source
     * values are clamped to the output range.
     */
    template<strict_number U> [[nodiscard]] constexpr
    auto saturate_as() const noexcept -> U
    {
        if constexpr(strict_integral<U>)
        {
            if constexpr(std::floating_point<ValueType>)
            {
                if constexpr(strict_unsigned_integral<U>)
                {
                    /* fp -> unsigned integral */
                    if(signbit())
                        return U{0};
                    if(c_val >= U::template fplimit<SelfType>().c_val or isnan())
                        return U::max();
                    return U{static_cast<typename U::value_t>(c_val)};
                }
                else
                {
                    /* fp -> signed integral */
                    if(abs() >= U::template fplimit<SelfType>() or isnan())
                        return signbit() ? U::min() : U::max();
                    return U{static_cast<typename U::value_t>(c_val)};
                }
            }
            else
            {
                /* integral -> integral */
                if constexpr(U::digits < std::numeric_limits<ValueType>::digits)
                {
                    if constexpr(strict_signed_integral<U> and std::signed_integral<ValueType>)
                    {
                        if(c_val < U::min().template as<SelfType>().c_val)
                            return U::min();
                    }
                    if(c_val > U::max().template as<SelfType>().c_val)
                        return U::max();
                }
                if constexpr(strict_unsigned_integral<U> and std::signed_integral<ValueType>)
                {
                    if(c_val < 0)
                        return U{0};
                }
                return U{static_cast<typename U::value_t>(c_val)};
            }
        }
        else
        {
            /* fp/integral -> fp */
            return U{static_cast<typename U::value_t>(c_val)};
        }
    }

    [[nodiscard]] force_inline constexpr
    auto popcount() const noexcept -> sys_uint requires(std::integral<ValueType>);
    [[nodiscard]] force_inline constexpr
    auto countl_zero() const noexcept -> sys_uint requires(std::integral<ValueType>);
    [[nodiscard]] force_inline constexpr
    auto countr_zero() const noexcept -> sys_uint requires(std::integral<ValueType>);

    [[nodiscard]] force_inline constexpr
    auto abs() const noexcept -> SelfType { return SelfType{std::abs(c_val)}; }

    [[nodiscard]] force_inline constexpr
    auto signbit() const noexcept -> bool
    {
        if constexpr(std::floating_point<ValueType>)
            return std::signbit(c_val);
        else if constexpr(std::signed_integral<ValueType>)
            return c_val < 0;
        else
            return false;
    }

    [[nodiscard]] force_inline constexpr
    auto ceil() const noexcept -> SelfType requires(std::floating_point<ValueType>)
    { return SelfType{std::ceil(c_val)}; }

    [[nodiscard]] force_inline constexpr
    auto floor() const noexcept -> SelfType requires(std::floating_point<ValueType>)
    { return SelfType{std::floor(c_val)}; }

    [[nodiscard]] force_inline constexpr
    auto sqrt() const noexcept -> SelfType requires(std::floating_point<ValueType>)
    { return SelfType{std::sqrt(c_val)}; }

    [[nodiscard]] force_inline constexpr
    auto cbrt() const noexcept -> SelfType requires(std::floating_point<ValueType>)
    { return SelfType{std::cbrt(c_val)}; }

    [[nodiscard]] force_inline constexpr
    auto sin() const noexcept -> SelfType requires(std::floating_point<ValueType>)
    { return SelfType{std::sin(c_val)}; }

    [[nodiscard]] force_inline constexpr
    auto asin() const noexcept -> SelfType requires(std::floating_point<ValueType>)
    { return SelfType{std::asin(c_val)}; }

    [[nodiscard]] force_inline constexpr
    auto cos() const noexcept -> SelfType requires(std::floating_point<ValueType>)
    { return SelfType{std::cos(c_val)}; }

    [[nodiscard]] force_inline constexpr
    auto acos() const noexcept -> SelfType requires(std::floating_point<ValueType>)
    { return SelfType{std::acos(c_val)}; }

    [[nodiscard]] force_inline constexpr
    auto log() const noexcept -> SelfType requires(std::floating_point<ValueType>)
    { return SelfType{std::log(c_val)}; }

    [[nodiscard]] force_inline constexpr
    auto log2() const noexcept -> SelfType requires(std::floating_point<ValueType>)
    { return SelfType{std::log2(c_val)}; }

    [[nodiscard]] force_inline constexpr
    auto log10() const noexcept -> SelfType requires(std::floating_point<ValueType>)
    { return SelfType{std::log10(c_val)}; }

    [[nodiscard]] force_inline constexpr
    auto exp() const noexcept -> SelfType requires(std::floating_point<ValueType>)
    { return SelfType{std::exp(c_val)}; }

    [[nodiscard]] force_inline constexpr
    auto exp2() const noexcept -> SelfType requires(std::floating_point<ValueType>)
    { return SelfType{std::exp2(c_val)}; }

    [[nodiscard]] force_inline constexpr
    auto round() const noexcept -> SelfType requires(std::floating_point<ValueType>)
    { return SelfType{std::round(c_val)}; }

    [[nodiscard]] force_inline constexpr
    auto modf(SelfType &ires) const noexcept -> SelfType requires(std::floating_point<ValueType>)
    { return SelfType{std::modf(c_val, &ires.c_val)}; }

    [[nodiscard]] force_inline constexpr
    auto isfinite() const noexcept -> bool requires(std::floating_point<ValueType>)
    { return std::isfinite(c_val); }

    [[nodiscard]] force_inline constexpr
    auto isnan() const noexcept -> bool requires(std::floating_point<ValueType>)
    { return std::isnan(c_val); }


    /* Relevant values for the given type. Offered here as static methods
     * instead of through a separate templated structure.
     */
    static consteval
    auto min() noexcept { return SelfType{std::numeric_limits<ValueType>::min()}; }
    static consteval
    auto max() noexcept { return SelfType{std::numeric_limits<ValueType>::max()}; }
    static consteval
    auto lowest() noexcept { return SelfType{std::numeric_limits<ValueType>::lowest()}; }
    static consteval auto epsilon() noexcept requires std::floating_point<ValueType>
    { return SelfType{std::numeric_limits<ValueType>::epsilon()}; }
    static consteval auto infinity() noexcept requires std::numeric_limits<ValueType>::has_infinity
    { return SelfType{std::numeric_limits<ValueType>::infinity()}; }
    static consteval
    auto quiet_NaN() noexcept requires std::numeric_limits<ValueType>::has_quiet_NaN
    { return SelfType{std::numeric_limits<ValueType>::quiet_NaN()}; }
    static consteval
    auto signaling_NaN() noexcept requires std::numeric_limits<ValueType>::has_signaling_NaN
    { return SelfType{std::numeric_limits<ValueType>::signaling_NaN()}; }
    static constexpr auto digits = std::numeric_limits<ValueType>::digits;

    /* This returns the numeric limit of the type as the given floating point
     * type. That is, 2**digits; for integral types, this is one greater than
     * max, and for floating point types, the greatest integral value before
     * skipping whole numbers (where nexttoward(x, 0) == x-1 and
     * nexttoward(x, inf) == x+2).
     */
    template<strict_floating_point R> [[nodiscard]] static consteval
    auto fplimit() noexcept -> R
    {
        constexpr auto halflimit = std::uint64_t{1} << (digits-1);
        constexpr auto res = typename R::value_t{halflimit} * 2;
        static_assert(res < R::infinity().c_val);
        return R{res};
    }
};

} /* namespace al */

/* Prefix and postfix increment and decrement operators. Only valid for
 * integral types.
 */
template<al::strict_integral T> force_inline constexpr
auto operator++(T &self LIFETIMEBOUND) noexcept -> T&
{ ++self.c_val; return self; }
template<al::strict_integral T> [[nodiscard]] force_inline constexpr
auto operator++(T &self, int) noexcept -> T
{
    auto const old = self;
    ++self.c_val;
    return old;
}

template<al::strict_integral T> force_inline constexpr
auto operator--(T &self LIFETIMEBOUND) noexcept -> T&
{ --self.c_val; return self; }
template<al::strict_integral T> [[nodiscard]] force_inline constexpr
auto operator--(T &self, int) noexcept -> T
{
    auto const old = self;
    --self.c_val;
    return old;
}

/* No automatic type promotion for our unary ops. Unary - is only valid for
 * signed types.
 */
template<al::strict_number T> [[nodiscard]] force_inline constexpr
auto operator+(T const &value) noexcept -> T { return value; }
template<al::strict_number T> [[nodiscard]] force_inline constexpr
auto operator-(T const &value) noexcept -> T
{
    static_assert(std::is_signed_v<typename T::value_t>,
        "Unary operator- is only valid for signed types");
    return T{static_cast<typename T::value_t>(-value.c_val)};
}
template<al::strict_number T> [[nodiscard]] force_inline constexpr
auto operator~(T const &value) noexcept -> T
{ return T{static_cast<typename T::value_t>(~value.c_val)}; }

/* Our binary ops only promote to the larger of the two operands, when the
 * conversion can't narrow (e.g. i8 + i16 = i16, while u32 + i16 = error).
 * If one operand is a weak number type, it must be convertible to the other
 * operand's type without narrowing. If one operand is a constant, the value
 * must fit the other operand's type, and the result won't be promoted.
 */

#define DECL_BINARY(op)                                                       \
template<al::strict_number T, al::strict_number U> [[nodiscard]] force_inline \
constexpr auto operator op(T const &lhs, U const &rhs) noexcept               \
{                                                                             \
    static_assert(al::has_common<typename T::value_t, typename U::value_t>,   \
        "Incompatible operands");                                             \
    if constexpr(not al::might_narrow<typename T::value_t, typename U::value_t>) \
        return T{static_cast<typename T::value_t>(lhs.c_val op rhs.c_val)};   \
    else if constexpr(not al::might_narrow<typename U::value_t, typename T::value_t>) \
        return U{static_cast<typename U::value_t>(lhs.c_val op rhs.c_val)};   \
    else                                                                      \
        return T{};                                                           \
}                                                                             \
                                                                              \
template<al::strict_number T> [[nodiscard]] force_inline constexpr            \
auto operator op(T const &lhs, al::compatible_weak_number<T> auto const &rhs) \
    noexcept -> T                                                             \
{ return T{static_cast<typename T::value_t>(lhs.c_val op rhs)}; }             \
template<al::strict_number T> [[nodiscard]] force_inline constexpr            \
auto operator op(al::compatible_weak_number<T> auto const &lhs, T const &rhs) \
    noexcept -> T                                                             \
{ return T{static_cast<typename T::value_t>(lhs op rhs.c_val)}; }             \
                                                                              \
template<al::strict_number T> [[nodiscard]] force_inline constexpr            \
auto operator op(T const &lhs, al::ConstantNum<typename T::value_t> const &rhs) noexcept -> T \
{ return T{static_cast<typename T::value_t>(lhs.c_val op rhs.c_val)}; }       \
template<al::strict_number T> [[nodiscard]] force_inline constexpr            \
auto operator op(al::ConstantNum<typename T::value_t> const &lhs, T const &rhs) noexcept -> T \
{ return T{static_cast<typename T::value_t>(lhs.c_val op rhs.c_val)}; }

DECL_BINARY(+)
DECL_BINARY(-)
DECL_BINARY(*)
DECL_BINARY(/)
DECL_BINARY(%)
DECL_BINARY(|)
DECL_BINARY(&)
DECL_BINARY(^)
#undef DECL_BINARY

/* Binary ops >> and << between strict number types. Note that the right-side
 * operand type doesn't influence the return type, as this is only modifying
 * the left-side operand value (e.g. 1_u8 << 1_u32 == 2_u8).
 */
template<al::strict_number T> [[nodiscard]] force_inline constexpr
auto operator>>(T const &lhs, al::strict_number auto const &rhs) noexcept -> T
{ return T{static_cast<typename T::value_t>(lhs.c_val >> rhs.c_val)}; }

template<al::strict_number T> [[nodiscard]] force_inline constexpr
auto operator<<(T const &lhs, al::strict_number auto const &rhs) noexcept -> T
{ return T{static_cast<typename T::value_t>(lhs.c_val << rhs.c_val)}; }

/* Binary ops >> and << between a strict number type and weak integer.
 * Unlike the other operations, these don't require the weak integer to be
 * constant because the result type is always the same as the left-side
 * operand.
 */
template<al::strict_number T> [[nodiscard]] force_inline constexpr
auto operator>>(T const &lhs, std::integral auto const &rhs) noexcept -> T
{ return T{static_cast<typename T::value_t>(lhs.c_val >> rhs)}; }

template<al::strict_number T> [[nodiscard]] force_inline constexpr
auto operator<<(T const &lhs, std::integral auto const &rhs) noexcept -> T
{ return T{static_cast<typename T::value_t>(lhs.c_val << rhs)}; }

/* Increment/decrement a strict unsigned integral using its signed difference
 * type.
 */
template<al::strict_unsigned_integral T> [[nodiscard]] force_inline constexpr
auto operator+(T const &lhs, std::same_as<typename T::difference_type> auto const &rhs) noexcept
    -> T
{ return T{static_cast<typename T::value_t>(lhs.c_val + static_cast<typename T::value_t>(rhs))}; }

template<al::strict_unsigned_integral T> [[nodiscard]] force_inline constexpr
auto operator+(std::same_as<typename T::difference_type> auto const &lhs, T const &rhs) noexcept
    -> T
{ return T{static_cast<typename T::value_t>(static_cast<typename T::value_t>(lhs) + rhs.c_val)}; }

template<al::strict_unsigned_integral T> [[nodiscard]] force_inline constexpr
auto operator-(T const &lhs, std::same_as<typename T::difference_type> auto const &rhs) noexcept
    -> T
{ return T{static_cast<typename T::value_t>(lhs.c_val - static_cast<typename T::value_t>(rhs))}; }

/* Our binary assignment ops only promote the rhs value to the lhs type when
 * the conversion can't narrow, and produces an error otherwise.
 */

#define DECL_BINASSIGN(op)                                                    \
template<al::strict_number T, al::strict_number U> force_inline constexpr     \
auto operator op(T &lhs LIFETIMEBOUND, U const &rhs) noexcept -> T&           \
{                                                                             \
    static_assert(not al::might_narrow<typename T::value_t, typename U::value_t>, \
        "Incompatible right side operand");                                   \
    lhs.c_val op static_cast<typename T::value_t>(rhs.c_val);                 \
    return lhs;                                                               \
}                                                                             \
template<al::strict_number T> force_inline constexpr                          \
auto operator op(T &lhs LIFETIMEBOUND,                                        \
    al::compatible_weak_number<T> auto const &rhs) noexcept -> T&             \
{ lhs.c_val op rhs; return lhs; }                                             \
template<al::strict_number T> force_inline constexpr                          \
auto operator op(T &lhs LIFETIMEBOUND,                                        \
    al::ConstantNum<typename T::value_t> const &rhs) noexcept -> T&           \
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

/* Binary assignment ops >>= and <<=. Integer constants for the right side
 * operand must fit an uint8 type.
 */
#define DECL_BINASSIGN(op)                                                    \
template<al::strict_number T> force_inline constexpr                          \
auto operator op(T &lhs LIFETIMEBOUND,                                        \
    al::strict_number auto const &rhs) noexcept -> T&                         \
{ lhs.c_val op rhs.c_val; return lhs; }                                       \
template<al::strict_number T> force_inline constexpr                          \
auto operator op(T &lhs LIFETIMEBOUND,                                        \
    al::ConstantNum<std::uint8_t> const &rhs) noexcept -> T&                  \
{ lhs.c_val op static_cast<typename T::value_t>(rhs.c_val); return lhs; }
DECL_BINASSIGN(>>=)
DECL_BINASSIGN(<<=)
#undef DECL_BINASSIGN

/* Offset a strict unsigned integral using its signed difference type. */
template<al::strict_unsigned_integral T> force_inline constexpr
auto operator+=(T &lhs LIFETIMEBOUND, std::same_as<typename T::difference_type> auto const &rhs)
    noexcept -> T&
{ lhs.c_val += static_cast<typename T::value_t>(rhs); return lhs; }

template<al::strict_unsigned_integral T> force_inline constexpr
auto operator-=(T &lhs LIFETIMEBOUND, std::same_as<typename T::difference_type> auto const &rhs)
    noexcept -> T&
{ lhs.c_val -= static_cast<typename T::value_t>(rhs); return lhs; }


/* Three-way comparison operator between strict number types, from which other
 * comparison operators are synthesized. Implicitly handles signedness
 * differences and floating point precision.
 */
template<al::strict_number T, al::strict_number U> [[nodiscard]] force_inline constexpr
auto operator<=>(T const &lhs, U const &rhs) noexcept
{
    if constexpr(not al::might_narrow<typename T::value_t, typename U::value_t>)
        return lhs.c_val <=> static_cast<typename T::value_t>(rhs.c_val);
    else if constexpr(not al::might_narrow<typename U::value_t, typename T::value_t>)
        return static_cast<typename U::value_t>(lhs.c_val) <=> rhs.c_val;
    else if constexpr(std::signed_integral<typename T::value_t>
        and std::unsigned_integral<typename U::value_t>)
    {
        if(lhs.c_val < typename T::value_t{0})
            return std::strong_ordering::less;
        return static_cast<std::make_unsigned_t<typename T::value_t>>(lhs.c_val) <=> rhs.c_val;
    }
    else if constexpr(std::unsigned_integral<typename T::value_t>
        and std::signed_integral<typename U::value_t>)
    {
        if(typename U::value_t{0} > rhs.c_val)
            return std::strong_ordering::greater;
        using unsigned_t = std::make_unsigned_t<typename U::value_t>;
        return lhs.c_val <=> static_cast<unsigned_t>(rhs.c_val);
    }
    /* All integer<=>integer and fp<=>fp comparisons should be handled above,
     * as well as some integer<=>fp/fp<=>integer where the integer type can fit
     * in the fp type. So this only needs to handle the remaining integer<=>fp
     * and fp<=>integer comparisons where the integer type can handle values
     * the fp type can't.
     */
    else if constexpr(std::floating_point<typename T::value_t>
        and std::integral<typename U::value_t>)
    {
        if(std::isnan(lhs.c_val))
            return std::partial_ordering::unordered;
        if(lhs.c_val < U::min().template as<T>().c_val)
            return std::partial_ordering::less;
        if(lhs.c_val >= U::template fplimit<T>().c_val)
            return std::partial_ordering::greater;
        if(std::abs(lhs.c_val) < T::template fplimit<T>().c_val)
            return lhs.c_val <=> static_cast<typename T::value_t>(rhs.c_val);
        return std::partial_ordering{static_cast<typename U::value_t>(lhs.c_val) <=> rhs.c_val};
    }
    else if constexpr(std::integral<typename T::value_t>
        and std::floating_point<typename U::value_t>)
    {
        if(std::isnan(rhs.c_val))
            return std::partial_ordering::unordered;
        if(T::min().template as<U>().c_val > rhs.c_val)
            return std::partial_ordering::greater;
        if(T::template fplimit<U>().c_val <= rhs.c_val)
            return std::partial_ordering::less;
        if(std::abs(rhs.c_val) < U::template fplimit<U>().c_val)
            return static_cast<typename U::value_t>(lhs.c_val) <=> rhs.c_val;
        return std::partial_ordering{lhs.c_val <=> static_cast<typename T::value_t>(rhs.c_val)};
    }
}

/* Three-way comparison operator between a strict number type and weak number
 * type, from which other comparison operators are synthesized. Only valid when
 * one is compatible with the other.
 */
template<al::strict_number T, al::weak_number U> requires(al::has_common<typename T::value_t, U>)
[[nodiscard]] force_inline constexpr auto operator<=>(T const &lhs, U const &rhs) noexcept
{
    if constexpr(not al::might_narrow<typename T::value_t, U>)
        return lhs.c_val <=> static_cast<typename T::value_t>(rhs);
    else if constexpr(not al::might_narrow<U, typename T::value_t>)
        return static_cast<U>(lhs.c_val) <=> rhs;
}

/* Three-way comparison operator between a strict number type and numeric
 * constant, from which other comparison operators are synthesized. Only valid
 * when the numeric constant fits the strict number type.
 */
template<al::strict_number T> [[nodiscard]] force_inline constexpr
auto operator<=>(T const &lhs, al::ConstantNum<typename T::value_t> const &rhs) noexcept
{ return lhs.c_val <=> rhs.c_val; }

/* FIXME: Why do I have to define these manually instead of the compiler
 * implicitly generating them from operator<=> like the others???
 */
template<al::strict_number T, al::strict_number U> [[nodiscard]] force_inline constexpr
auto operator==(T const &lhs, U const &rhs) noexcept -> bool
{ return (lhs <=> rhs) == 0; }

template<al::strict_number T, al::weak_number U> requires(al::has_common<typename T::value_t, U>)
[[nodiscard]] force_inline constexpr auto operator==(T const &lhs, U const &rhs) noexcept -> bool
{ return (lhs <=> rhs) == 0; }

template<al::strict_number T> [[nodiscard]] force_inline constexpr
auto operator==(T const &lhs, al::ConstantNum<typename T::value_t> const &rhs) noexcept -> bool
{ return (lhs <=> rhs) == 0; }


#define DECL_NUMBERTYPE(SelfType, ValueType)                                  \
struct [[nodiscard]] SelfType : al::number_base<ValueType, SelfType> {        \
    using number_base::number_base;                                           \
                                                                              \
    constexpr SelfType() noexcept = default;                                  \
    constexpr SelfType(SelfType const&) noexcept = default;                   \
    constexpr ~SelfType() noexcept = default;                                 \
                                                                              \
    constexpr                                                                 \
    auto operator=(SelfType const &rhs) & noexcept LIFETIMEBOUND              \
        -> SelfType& = default;                                               \
                                                                              \
    template<al::strict_number U>                                             \
        requires(not std::is_base_of_v<SelfType, U>                           \
            and not al::might_narrow<value_t, typename U::value_t>)           \
    force_inline constexpr                                                    \
    auto operator=(U const &rhs) & noexcept LIFETIMEBOUND -> SelfType&        \
    {                                                                         \
        c_val = static_cast<value_t>(rhs.c_val);                              \
        return *this;                                                         \
    }                                                                         \
                                                                              \
    template<al::weak_number U> requires(not al::might_narrow<value_t, U>)    \
    force_inline constexpr                                                    \
    auto operator=(U const &rhs) & noexcept LIFETIMEBOUND -> SelfType&        \
    {                                                                         \
        c_val = static_cast<value_t>(rhs);                                    \
        return *this;                                                         \
    }                                                                         \
                                                                              \
    force_inline constexpr                                                    \
    auto operator=(al::ConstantNum<ValueType> const &rhs) & noexcept          \
        LIFETIMEBOUND -> SelfType&                                            \
    {                                                                         \
        c_val = rhs.c_val;                                                    \
        return *this;                                                         \
    }                                                                         \
};

DECL_NUMBERTYPE(i8, std::int8_t)
DECL_NUMBERTYPE(u8, std::uint8_t)
DECL_NUMBERTYPE(i16, std::int16_t)
DECL_NUMBERTYPE(u16, std::uint16_t)
DECL_NUMBERTYPE(i32, std::int32_t)
DECL_NUMBERTYPE(u32, std::uint32_t)
DECL_NUMBERTYPE(i64, std::int64_t)
DECL_NUMBERTYPE(u64, std::uint64_t)
DECL_NUMBERTYPE(f32, float)
DECL_NUMBERTYPE(f64, double)

DECL_NUMBERTYPE(isize, std::make_signed_t<std::size_t>);
DECL_NUMBERTYPE(usize, std::size_t);
#undef DECL_NUMBERTYPE

namespace al {

template<weak_number ValueType, typename SelfType> [[nodiscard]] force_inline
constexpr auto number_base<ValueType, SelfType>::popcount() const noexcept -> sys_uint
    requires(std::integral<ValueType>)
{
    using unsigned_t = std::make_unsigned_t<ValueType>;
    return sys_uint{static_cast<unsigned>(std::popcount(static_cast<unsigned_t>(c_val)))};
}

template<weak_number ValueType, typename SelfType> [[nodiscard]] force_inline
constexpr auto number_base<ValueType, SelfType>::countl_zero() const noexcept -> sys_uint
    requires(std::integral<ValueType>)
{
    using unsigned_t = std::make_unsigned_t<ValueType>;
    return sys_uint{static_cast<unsigned>(std::countl_zero(static_cast<unsigned_t>(c_val)))};
}

template<weak_number ValueType, typename SelfType> [[nodiscard]] force_inline
constexpr auto number_base<ValueType, SelfType>::countr_zero() const noexcept -> sys_uint
    requires(std::integral<ValueType>)
{
    using unsigned_t = std::make_unsigned_t<ValueType>;
    return sys_uint{static_cast<unsigned>(std::countr_zero(static_cast<unsigned_t>(c_val)))};
}

} /* namespace al */


[[nodiscard]] consteval
auto operator ""_i8(unsigned long long const n) noexcept { return i8::from(n); }
[[nodiscard]] consteval
auto operator ""_u8(unsigned long long const n) noexcept { return u8::from(n); }

[[nodiscard]] consteval
auto operator ""_i16(unsigned long long const n) noexcept { return i16::from(n); }
[[nodiscard]] consteval
auto operator ""_u16(unsigned long long const n) noexcept { return u16::from(n); }

[[nodiscard]] consteval
auto operator ""_i32(unsigned long long const n) noexcept { return i32::from(n); }
[[nodiscard]] consteval
auto operator ""_u32(unsigned long long const n) noexcept { return u32::from(n); }

[[nodiscard]] consteval
auto operator ""_i64(unsigned long long const n) noexcept { return i64::from(n); }
[[nodiscard]] consteval
auto operator ""_u64(unsigned long long const n) noexcept { return u64::from(n); }

[[nodiscard]] consteval
auto operator ""_f32(long double const n) noexcept { return f32::from(n); }
[[nodiscard]] consteval
auto operator ""_f64(long double const n) noexcept { return f64::from(n); }

[[nodiscard]] consteval
auto operator ""_isize(unsigned long long const n) noexcept { return isize::from(n); }
[[nodiscard]] consteval
auto operator ""_usize(unsigned long long const n) noexcept { return usize::from(n); }

[[nodiscard]] consteval
auto operator ""_z(unsigned long long const n) noexcept
{ return al::convert_to<isize::value_t>(n); }
[[nodiscard]] consteval
auto operator ""_uz(unsigned long long const n) noexcept { return al::convert_to<std::size_t>(n); }
[[nodiscard]] consteval
auto operator ""_zu(unsigned long long const n) noexcept { return al::convert_to<std::size_t>(n); }


namespace std {

/* Declare the common type between strict number types, where one fits the
 * other. Note that the result must be order invariant, that is, if
 * common_type_t<A, B> results in A, then common_type_t<B, A> must also result
 * in A. Similarly, if common_type_t<A, B> results in C, then
 * common_type_t<B, A> must also result in C. Consequently, since isize and
 * usize may be interconvertible with other types, these base specializations
 * will never result in isize or usize.
 */
template<al::strict_number T, al::strict_number U>
    requires(not std::derived_from<T, isize> and not std::derived_from<T, usize>
        and not al::might_narrow<typename T::value_t, typename U::value_t>)
struct common_type<T, U> { using type = T; };

template<al::strict_number T, al::strict_number U>
    requires(not std::derived_from<U, isize> and not std::derived_from<U, usize>
        and not al::might_narrow<typename U::value_t, typename T::value_t>
        and al::might_narrow<typename T::value_t, typename U::value_t>)
struct common_type<T, U> { using type = U; };

/* Declare the common type between signed and unsigned strict number types,
 * where a larger signed type is needed.
 */
template<> struct common_type<i8, u8> { using type = i16; };
template<> struct common_type<i8, u16> { using type = i32; };
template<> struct common_type<i8, u32> { using type = i64; };
template<> struct common_type<i16, u16> { using type = i32; };
template<> struct common_type<i16, u32> { using type = i64; };
template<> struct common_type<i32, u32> { using type = i64; };

template<> struct common_type<u8, i8> { using type = i16; };
template<> struct common_type<u16, i8> { using type = i32; };
template<> struct common_type<u32, i8> { using type = i64; };
template<> struct common_type<u16, i16> { using type = i32; };
template<> struct common_type<u32, i16> { using type = i64; };
template<> struct common_type<u32, i32> { using type = i64; };

/* Declare the common type between equal-sized strict integer and floating
 * point types, where a larger floating point type is needed.
 */
template<> struct common_type<f32, i32> { using type = f64; };
template<> struct common_type<f32, u32> { using type = f64; };
template<> struct common_type<i32, f32> { using type = f64; };
template<> struct common_type<u32, f32> { using type = f64; };

/* Declare the common type between strict integer types where isize or usize is
 * the appropriate result type.
 */
template<al::strict_integral T> requires(sizeof(T) < sizeof(isize) or std::same_as<T, isize>)
struct common_type<isize, T> { using type = isize; };
template<al::strict_integral T> requires(sizeof(T) < sizeof(isize))
struct common_type<T, isize> { using type = isize; };

template<al::strict_integral T> requires(sizeof(T) < sizeof(usize) or std::same_as<T, usize>)
struct common_type<usize, T> { using type = usize; };
template<al::strict_integral T> requires(sizeof(T) < sizeof(usize))
struct common_type<T, usize> { using type = usize; };

/* Declare the common type between a strict and weak number type, ensuring the
 * appropriate strict number type is provided.
 */
template<al::strict_number T, al::weak_number U>
struct common_type<T, U> : common_type<T, al::make_strict_t<U>> { };

template<al::weak_number T, al::strict_number U>
struct common_type<T, U> : common_type<al::make_strict_t<T>, U> { };

} /* namespace std */


template<al::strict_integral T> [[nodiscard]] force_inline constexpr
auto popcount(T const &x) noexcept -> sys_uint { return x.popcount(); }

template<al::strict_integral T> [[nodiscard]] force_inline constexpr
auto countl_zero(T const &x) noexcept -> sys_uint { return x.countl_zero(); }

template<al::strict_integral T> [[nodiscard]] force_inline constexpr
auto countr_zero(T const &x) noexcept -> sys_uint { return x.countr_zero(); }

template<al::strict_number T> [[nodiscard]] force_inline constexpr
auto abs(T const &x) noexcept -> T { return x.abs(); }

template<al::strict_floating_point T> [[nodiscard]] force_inline constexpr
auto ceil(T const &x) noexcept -> T { return x.ceil(); }

template<al::strict_floating_point T> [[nodiscard]] force_inline constexpr
auto floor(T const &x) noexcept -> T { return x.floor(); }

template<al::strict_floating_point T> [[nodiscard]] force_inline constexpr
auto sqrt(T const &x) noexcept -> T { return x.sqrt(); }

template<al::strict_floating_point T> [[nodiscard]] force_inline constexpr
auto cbrt(T const &x) noexcept -> T { return x.cbrt(); }

template<al::strict_floating_point T> [[nodiscard]] force_inline constexpr
auto sin(T const &x) noexcept -> T { return x.sin(); }

template<al::strict_floating_point T> [[nodiscard]] force_inline constexpr
auto asin(T const &x) noexcept -> T { return x.asin(); }

template<al::strict_floating_point T> [[nodiscard]] force_inline constexpr
auto cos(T const &x) noexcept -> T { return x.cos(); }

template<al::strict_floating_point T> [[nodiscard]] force_inline constexpr
auto acos(T const &x) noexcept -> T { return x.acos(); }

template<al::strict_floating_point T> [[nodiscard]] force_inline constexpr
auto atan2(T const &y, T const &x) noexcept -> T { return T{std::atan2(y.c_val, x.c_val)}; }

template<al::strict_floating_point T> [[nodiscard]] force_inline constexpr
auto pow(T const &x, T const &y) noexcept -> T { return T{std::pow(x.c_val, y.c_val)}; }

template<al::strict_floating_point T> [[nodiscard]] force_inline constexpr
auto log(T const &x) noexcept -> T { return x.log(); }

template<al::strict_floating_point T> [[nodiscard]] force_inline constexpr
auto log2(T const &x) noexcept -> T { return x.log2(); }

template<al::strict_floating_point T> [[nodiscard]] force_inline constexpr
auto log10(T const &x) noexcept -> T { return x.log10(); }

template<al::strict_floating_point T> [[nodiscard]] force_inline constexpr
auto exp(T const &x) noexcept -> T { return x.exp(); }

template<al::strict_floating_point T> [[nodiscard]] force_inline constexpr
auto exp2(T const &x) noexcept -> T { return x.exp2(); }

template<al::strict_floating_point T> [[nodiscard]] force_inline constexpr
auto round(T const &x) noexcept -> T { return x.round(); }

template<al::strict_floating_point T> [[nodiscard]] force_inline constexpr
auto lerp(T const &a, T const &b, T const &t) noexcept -> T
{ return T{std::lerp(a.c_val, b.c_val, t.c_val)}; }

[[nodiscard]] constexpr
auto lerpf(f32 const val1, f32 const val2, f32 const mu) noexcept -> f32
{ return val1 + (val2-val1)*mu; }

#endif /* AL_TYPES_HPP */
