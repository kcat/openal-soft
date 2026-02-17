#ifndef AL_TYPES_HPP
#define AL_TYPES_HPP

#include <compare>
#include <concepts>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <type_traits>

#include "alformat.hpp"
#include "altypes.hpp"
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
concept might_narrow = sizeof(T) < sizeof(U)
    or (std::unsigned_integral<T> and std::signed_integral<U>)
    or (std::integral<T> and std::floating_point<U>)
    or (sizeof(T) == sizeof(U)
        and ((std::signed_integral<T> and std::unsigned_integral<U>)
            or (std::floating_point<T> and std::integral<U>)));

template<typename T, typename U>
concept has_common = not might_narrow<T, U> or not might_narrow<U, T>;


template<weak_number T, weak_number U> [[nodiscard]] constexpr
auto convert_to(U const &value) noexcept(not might_narrow<T, U>) -> T
{
    if constexpr(not might_narrow<T, U>)
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


/* A "strong number" is a numeric type derived from the number class below. It
 * has stronger protections from implicit conversions and automatic type
 * promotions.
 */
template<typename T>
concept strong_number = requires { T::is_strong_number_type; }
    and std::derived_from<T, typename T::self_t>;

template<typename T>
concept strong_integral = strong_number<T> and std::integral<typename T::value_t>;

template<typename T>
concept strong_signed_integral = strong_number<T> and std::signed_integral<typename T::value_t>;

template<typename T>
concept strong_unsigned_integral = strong_number<T>
    and std::unsigned_integral<typename T::value_t>;

template<typename T>
concept strong_floating_point = strong_number<T> and std::floating_point<typename T::value_t>;


/* Determines whether a weak number of type T can be used for an operation on a
 * strong number's underlying type U. This is intended to restrict otherwise
 * valid numeric constants that could be confusing. For example:
 *
 * auto var = u8{...};
 * auto var_doubled = var * 2;
 *
 * Here 2 is a "weak" int, which is required to convert to a "strong" u8. This
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
 * Here 1.0f is a "weak" float, which is required to convert to a "strong" u32.
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
template<typename T, typename U>
concept compatible_constant = (std::integral<U> and std::integral<T>)
    or (std::floating_point<U> and (std::integral<T> or sizeof(U) >= sizeof(T)));


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


struct UInt;


namespace detail_ {

template<typename>
struct signed_difference { using type = void; };

template<typename T> requires(std::integral<T>)
struct signed_difference<T> { using type = std::make_signed_t<T>; };

}

/* Strong numbers are implemented using CRTP to act as a mixin of sorts. */
template<weak_number ValueType, typename SelfType>
    requires(not std::is_const_v<ValueType> and not std::is_volatile_v<ValueType>)
class number_base {
    friend SelfType;

    /* Force printing smaller types as (unsigned) int. Always treat these as
     * numeric values even when backed by character types.
     */
    using fmttype_t = std::conditional_t<not might_narrow<unsigned, ValueType>, unsigned,
        std::conditional_t<not might_narrow<int, ValueType>, int,
        ValueType>>;

    /* Defaulted constructor/destructor/copy assignment functions, which will be
     * inherited by the parent type. Allows the type to be trivial.
     */
    constexpr number_base() noexcept = default;
    constexpr number_base(number_base const&) noexcept = default;
    constexpr ~number_base() noexcept = default;
    constexpr
    auto operator=(number_base const &rhs) & noexcept LIFETIMEBOUND -> number_base& = default;

public:
    static constexpr auto is_strong_number_type = true;

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
     * integral type this strong number models, which satisfies the definition.
     * Although subtracting two strong numbers does not result in this type,
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

    ValueType c_val;

    /* Implicit constructor from non-narrowing weak number types. */
    template<weak_number U> requires(not might_narrow<ValueType, U>) force_inline constexpr
    explicit(false) number_base(U const &value) noexcept : c_val{convert_to<ValueType>(value)} { }

    /* Implicit constructor from narrowing weak number types. Required to be
     * compile-time so the provided value can be checked for narrowing.
     */
    constexpr explicit(false)
    number_base(ConstantNum<ValueType> const &value) noexcept : c_val{value.c_val} { }

    template<weak_number U> force_inline static constexpr
    auto make_from(U const &value) noexcept(not might_narrow<ValueType, U>) -> SelfType
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
    auto bit_pack(std::byte const hi, std::byte const lo) noexcept -> SelfType
        requires(sizeof(value_t) == 2)
    {
        auto const ret = static_cast<std::uint16_t>((to_integer<std::uint16_t>(hi)<<8)
            | to_integer<std::uint16_t>(lo));
        return std::bit_cast<SelfType>(ret);
    }

    /* Conversion operator to other strong number types. Only valid for
     * non-narrowing conversions.
     */
    template<strong_number U> requires(not might_narrow<typename U::value_t, ValueType>)
        force_inline constexpr explicit
    operator U() noexcept { return U{convert_to<typename U::value_t>(c_val)}; }

    template<std::same_as<difference_type> U> [[nodiscard]] force_inline constexpr explicit
    operator U() noexcept { return static_cast<U>(c_val); }

    [[nodiscard]] force_inline constexpr explicit
    operator bool() noexcept requires(std::integral<ValueType>) { return c_val != ValueType{0}; }

    /* Non-narrowing conversion method. */
    template<strong_number U> requires(not might_narrow<typename U::value_t, ValueType>)
    [[nodiscard]] force_inline constexpr
    auto as() const noexcept -> U { return U{convert_to<typename U::value_t>(c_val)}; }

    template<strong_number U> [[nodiscard]] consteval
    auto as() const noexcept -> U { return U{convert_to<typename U::value_t>(c_val)}; }

    /* Potentially narrowing conversion method. Throws if the converted value
     * narrows.
     */
    template<strong_number U> [[nodiscard]] force_inline constexpr
    auto cast_to() const
        noexcept(not might_narrow<typename U::value_t,ValueType>
            or std::floating_point<typename U::value_t>)
        -> U
    {
        /* Like make_from, converting to a floating point type isn't checked
         * here.
         */
        if constexpr(std::floating_point<typename U::value_t>)
            return U{static_cast<typename U::value_t>(c_val)};
        else
            return U{convert_to<typename U::value_t>(c_val)};
    }

    /* "Raw" conversion method, essentially applying a static_cast to the
     * underlying type.
     */
    template<strong_number U> [[nodiscard]] force_inline constexpr
    auto reinterpret_as() const noexcept -> U
    { return U{static_cast<typename U::value_t>(c_val)}; }

    /* Saturating cast, applying a standard cast except out of range source
     * values are clamped to the output range.
     */
    template<strong_number U> [[nodiscard]] constexpr
    auto saturate_as() const noexcept -> U
    {
        if constexpr(strong_integral<U>)
        {
            if constexpr(std::floating_point<ValueType>)
            {
                if constexpr(strong_signed_integral<U>)
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
                    if constexpr(strong_signed_integral<U> and std::signed_integral<ValueType>)
                    {
                        if(c_val < U::min().template as<SelfType>().c_val)
                            return U::min();
                    }
                    if(c_val > U::max().template as<SelfType>().c_val)
                        return U::max();
                }
                if constexpr(strong_signed_integral<U> and std::signed_integral<ValueType>)
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
    auto popcount() const noexcept -> UInt requires(std::integral<ValueType>);
    [[nodiscard]] force_inline constexpr
    auto countr_zero() const noexcept -> UInt requires(std::integral<ValueType>);

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
    template<strong_floating_point R> [[nodiscard]] static consteval
    auto fplimit() noexcept -> R
    {
        constexpr auto halflimit = std::uint64_t{1} << (digits-1);
        constexpr auto res = typename R::value_t{halflimit} * 2;
        static_assert(res < R::infinity().c_val);
        return R{res};
    }

    template<typename CharT>
    struct formatter : al::formatter<fmttype_t, CharT> {
        auto format(SelfType const &obj, auto& ctx) const
        {
            return al::formatter<fmttype_t,CharT>::format(al::convert_to<fmttype_t>(obj.c_val),
                ctx);
        }
    };
};

} /* namespace al */

/* Prefix and postfix increment and decrement operators. Only valid for
 * integral types.
 */
template<al::strong_integral T> force_inline constexpr
auto operator++(T &self LIFETIMEBOUND) noexcept -> T&
{ ++self.c_val; return self; }
template<al::strong_integral T> [[nodiscard]] force_inline constexpr
auto operator++(T &self, int) noexcept -> T
{
    auto const old = self;
    ++self.c_val;
    return old;
}

template<al::strong_integral T> force_inline constexpr
auto operator--(T &self LIFETIMEBOUND) noexcept -> T&
{ --self.c_val; return self; }
template<al::strong_integral T> [[nodiscard]] force_inline constexpr
auto operator--(T &self, int) noexcept -> T
{
    auto const old = self;
    --self.c_val;
    return old;
}

/* No automatic type promotion for our unary ops. Unary - is only valid for
 * signed types.
 */
template<al::strong_number T> [[nodiscard]] force_inline constexpr
auto operator+(T const &value) noexcept -> T { return value; }
template<al::strong_number T> [[nodiscard]] force_inline constexpr
auto operator-(T const &value) noexcept -> T
{
    static_assert(std::is_signed_v<typename T::value_t>,
        "Unary operator- is only valid for signed types");
    return T{static_cast<typename T::value_t>(-value.c_val)};
}
template<al::strong_number T> [[nodiscard]] force_inline constexpr
auto operator~(T const &value) noexcept -> T
{ return T{static_cast<typename T::value_t>(~value.c_val)}; }

/* Our binary ops only promote to the larger of the two operands, when the
 * conversion can't narrow (e.g. i8 + i16 = i16, while u32 + i16 = error).
 * If one operand is a constant, it must fit the type of the other operand, and
 * the result won't be promoted.
 */

#define DECL_BINARY(op)                                                       \
template<al::strong_number T, al::strong_number U> [[nodiscard]] force_inline \
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
template<al::strong_number T> [[nodiscard]] force_inline constexpr            \
auto operator op(T const &lhs, al::ConstantNum<typename T::value_t> const &rhs) noexcept -> T \
{ return T{static_cast<typename T::value_t>(lhs.c_val op rhs.c_val)}; }       \
template<al::strong_number T> [[nodiscard]] force_inline constexpr            \
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
/* Binary ops >> and << between strong number types. Note that the right-side
 * operand type doesn't influence the return type, as this is only modifying
 * the left-side operand value.
 */
#define DECL_BINARY(op)                                                       \
template<al::strong_number T, al::strong_number U> [[nodiscard]] force_inline \
constexpr auto operator op(T const &lhs, U const &rhs) noexcept -> T          \
{ return T{static_cast<typename T::value_t>(lhs.c_val op rhs.c_val)}; }
DECL_BINARY(>>)
DECL_BINARY(<<)
#undef DECL_BINARY

/* Binary ops >> and << between a strong number type and weak integer.
 * Unlike the other operations, these don't require the weak integer to be
 * constant because the result type is always the same as the left-side
 * operand.
 */
#define DECL_BINARY(op)                                                       \
template<al::strong_number T, std::integral U> [[nodiscard]] force_inline     \
constexpr auto operator op(T const &lhs, U const &rhs) noexcept -> T          \
{ return T{static_cast<typename T::value_t>(lhs.c_val op rhs)}; }
DECL_BINARY(>>)
DECL_BINARY(<<)
#undef DECL_BINARY

/* Increment/decrement a strong integral using its difference type. */
template<al::strong_integral T, std::same_as<typename T::difference_type> U> [[nodiscard]]
    force_inline constexpr
auto operator+(T const &lhs, U const &rhs) noexcept -> T
{ return T{static_cast<typename T::value_t>(lhs.c_val + static_cast<typename T::value_t>(rhs))}; }

template<al::strong_integral T, std::same_as<typename T::difference_type> U> [[nodiscard]]
    force_inline constexpr
auto operator+(U const &lhs, T const &rhs) noexcept -> T
{ return T{static_cast<typename T::value_t>(static_cast<typename T::value_t>(lhs) + rhs.c_val)}; }

template<al::strong_integral T, std::same_as<typename T::difference_type> U> [[nodiscard]]
    force_inline constexpr
auto operator-(T const &lhs, U const &rhs) noexcept -> T
{ return T{static_cast<typename T::value_t>(lhs.c_val - static_cast<typename T::value_t>(rhs))}; }

/* Our binary assignment ops only promote the rhs value to the lhs type when
 * the conversion can't lose information, and produces an error otherwise.
 */

#define DECL_BINASSIGN(op)                                                    \
template<al::strong_number T, al::strong_number U> force_inline constexpr     \
auto operator op(T &lhs LIFETIMEBOUND, U const &rhs) noexcept -> T&           \
{                                                                             \
    static_assert(not al::might_narrow<typename T::value_t, typename U::value_t>, \
        "Incompatible right side operand");                                   \
    lhs.c_val op static_cast<typename T::value_t>(rhs.c_val);                 \
    return lhs;                                                               \
}                                                                             \
template<al::strong_number T> force_inline constexpr                          \
auto operator op(T &lhs LIFETIMEBOUND, al::ConstantNum<typename T::value_t> const &rhs) \
    noexcept -> T&                                                            \
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
template<al::strong_number T, al::strong_number U> force_inline constexpr     \
auto operator op(T &lhs LIFETIMEBOUND, U const &rhs) noexcept -> T&           \
{ lhs.c_val op rhs.c_val; return lhs; }                                       \
template<al::strong_number T> force_inline constexpr                          \
auto operator op(T &lhs LIFETIMEBOUND, al::ConstantNum<std::uint8_t> const &rhs) \
    noexcept -> T&                                                            \
{ lhs.c_val op static_cast<typename T::value_t>(rhs.c_val); return lhs; }
DECL_BINASSIGN(>>=)
DECL_BINASSIGN(<<=)
#undef DECL_BINASSIGN

/* Offset a strong integral using its difference type. */
#define DECL_BINASSIGN(op)                                                    \
template<al::strong_integral T, std::same_as<typename T::difference_type> U>  \
    force_inline constexpr                                                    \
auto operator op(T &lhs LIFETIMEBOUND, U const &rhs) noexcept -> T&           \
{ lhs.c_val op static_cast<typename T::value_t>(rhs); return lhs; }
DECL_BINASSIGN(+=)
DECL_BINASSIGN(-=)
#undef DECL_BINASSIGN

/* Three-way comparison operator between strong number types, from which other
 * comparison operators are synthesized. Implicitly handles signedness
 * differences and floating point precision.
 */
template<al::strong_number T, al::strong_number U> [[nodiscard]] force_inline constexpr
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

/* Three-way comparison operator between a strong number type and weak number
 * type, from which other comparison operators are synthesized. Only valid when
 * one is compatible with the other.
 */
template<al::strong_number T, al::weak_number U> requires(al::has_common<typename T::value_t, U>)
[[nodiscard]] force_inline constexpr auto operator<=>(T const &lhs, U const &rhs) noexcept
{
    if constexpr(not al::might_narrow<typename T::value_t, U>)
        return lhs.c_val <=> static_cast<typename T::value_t>(rhs);
    else if constexpr(not al::might_narrow<U, typename T::value_t>)
        return static_cast<U>(lhs.c_val) <=> rhs;
}

/* Three-way comparison operator between a strong number type and numeric
 * constant, from which other comparison operators are synthesized. Only valid
 * when the numeric constant fits the strong number type.
 */
template<al::strong_number T> [[nodiscard]] force_inline constexpr
auto operator<=>(T const &lhs, al::ConstantNum<typename T::value_t> const &rhs) noexcept
{ return lhs.c_val <=> rhs.c_val; }

/* FIXME: Why do I have to define these manually instead of the compiler
 * implicitly generating them from operator<=> like the others???
 */
template<al::strong_number T, al::strong_number U> [[nodiscard]] force_inline constexpr
auto operator==(T const &lhs, U const &rhs) noexcept -> bool
{ return (lhs <=> rhs) == 0; }

template<al::strong_number T, al::weak_number U> requires(al::has_common<typename T::value_t, U>)
[[nodiscard]] force_inline constexpr auto operator==(T const &lhs, U const &rhs) noexcept -> bool
{ return (lhs <=> rhs) == 0; }

template<al::strong_number T> [[nodiscard]] force_inline constexpr
auto operator==(T const &lhs, al::ConstantNum<typename T::value_t> const &rhs) noexcept -> bool
{ return (lhs <=> rhs) == 0; }


#define DECL_NUMBERTYPE(SelfType, ValueType)                                  \
struct SelfType : al::number_base<ValueType, SelfType> {                      \
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
    template<al::strong_number U>                                             \
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

using isize = std::make_signed_t<std::size_t>;
using usize = std::size_t;

namespace al {

DECL_NUMBERTYPE(UInt, unsigned)
#undef DECL_NUMBERTYPE

template<weak_number T, typename SelfType>
    requires(not std::is_const_v<T> and not std::is_volatile_v<T>) [[nodiscard]] force_inline
constexpr auto number_base<T,SelfType>::popcount() const noexcept -> UInt
    requires(std::integral<T>)
{
    using unsigned_t = std::make_unsigned_t<T>;
    return UInt{static_cast<unsigned>(std::popcount(static_cast<unsigned_t>(c_val)))};
}

template<weak_number T, typename SelfType>
    requires(not std::is_const_v<T> and not std::is_volatile_v<T>) [[nodiscard]] force_inline
constexpr auto number_base<T,SelfType>::countr_zero() const noexcept -> UInt
    requires(std::integral<T>)
{
    using unsigned_t = std::make_unsigned_t<T>;
    return UInt{static_cast<unsigned>(std::countr_zero(static_cast<unsigned_t>(c_val)))};
}

} /* namespace al */

template<typename CharT> struct al::formatter<i8, CharT> : i8::formatter<CharT> { };
template<typename CharT> struct al::formatter<u8, CharT> : u8::formatter<CharT> { };
template<typename CharT> struct al::formatter<i16, CharT> : i16::formatter<CharT> { };
template<typename CharT> struct al::formatter<u16, CharT> : u16::formatter<CharT> { };
template<typename CharT> struct al::formatter<i32, CharT> : i32::formatter<CharT> { };
template<typename CharT> struct al::formatter<u32, CharT> : u32::formatter<CharT> { };
template<typename CharT> struct al::formatter<i64, CharT> : i64::formatter<CharT> { };
template<typename CharT> struct al::formatter<u64, CharT> : u64::formatter<CharT> { };
template<typename CharT> struct al::formatter<f32, CharT> : f32::formatter<CharT> { };
template<typename CharT> struct al::formatter<f64, CharT> : f64::formatter<CharT> { };
template<typename CharT> struct al::formatter<al::UInt, CharT> : al::UInt::formatter<CharT> { };

[[nodiscard]] consteval
auto operator ""_i8(unsigned long long const n) noexcept { return i8::make_from(n); }
[[nodiscard]] consteval
auto operator ""_u8(unsigned long long const n) noexcept { return u8::make_from(n); }

[[nodiscard]] consteval
auto operator ""_i16(unsigned long long const n) noexcept { return i16::make_from(n); }
[[nodiscard]] consteval
auto operator ""_u16(unsigned long long const n) noexcept { return u16::make_from(n); }

[[nodiscard]] consteval
auto operator ""_i32(unsigned long long const n) noexcept { return i32::make_from(n); }
[[nodiscard]] consteval
auto operator ""_u32(unsigned long long const n) noexcept { return u32::make_from(n); }

[[nodiscard]] consteval
auto operator ""_i64(unsigned long long const n) noexcept { return i64::make_from(n); }
[[nodiscard]] consteval
auto operator ""_u64(unsigned long long const n) noexcept { return u64::make_from(n); }

[[nodiscard]] consteval
auto operator ""_f32(long double const n) noexcept { return f32::make_from(n); }
[[nodiscard]] consteval
auto operator ""_f64(long double const n) noexcept { return f64::make_from(n); }

[[nodiscard]] consteval
auto operator ""_z(unsigned long long const n) noexcept { return gsl::narrow<isize>(n); }
[[nodiscard]] consteval
auto operator ""_uz(unsigned long long const n) noexcept { return gsl::narrow<usize>(n); }
[[nodiscard]] consteval
auto operator ""_zu(unsigned long long const n) noexcept { return gsl::narrow<usize>(n); }


template<al::strong_number T> [[nodiscard]] force_inline constexpr
auto abs(T const &x) noexcept -> T { return x.abs(); }

template<al::strong_floating_point T> [[nodiscard]] force_inline constexpr
auto ceil(T const &x) noexcept -> T { return x.ceil(); }

template<al::strong_floating_point T> [[nodiscard]] force_inline constexpr
auto floor(T const &x) noexcept -> T { return x.floor(); }

template<al::strong_floating_point T> [[nodiscard]] force_inline constexpr
auto sqrt(T const &x) noexcept -> T { return x.sqrt(); }

template<al::strong_floating_point T> [[nodiscard]] force_inline constexpr
auto cbrt(T const &x) noexcept -> T { return x.cbrt(); }

template<al::strong_floating_point T> [[nodiscard]] force_inline constexpr
auto sin(T const &x) noexcept -> T { return x.sin(); }

template<al::strong_floating_point T> [[nodiscard]] force_inline constexpr
auto asin(T const &x) noexcept -> T { return x.asin(); }

template<al::strong_floating_point T> [[nodiscard]] force_inline constexpr
auto cos(T const &x) noexcept -> T { return x.cos(); }

template<al::strong_floating_point T> [[nodiscard]] force_inline constexpr
auto acos(T const &x) noexcept -> T { return x.acos(); }

template<al::strong_floating_point T> [[nodiscard]] force_inline constexpr
auto atan2(T const &y, T const &x) noexcept -> T { return T{std::atan2(y.c_val, x.c_val)}; }

template<al::strong_floating_point T> [[nodiscard]] force_inline constexpr
auto pow(T const &x, T const &y) noexcept -> T { return T{std::pow(x.c_val, y.c_val)}; }

template<al::strong_floating_point T> [[nodiscard]] force_inline constexpr
auto log(T const &x) noexcept -> T { return x.log(); }

template<al::strong_floating_point T> [[nodiscard]] force_inline constexpr
auto log2(T const &x) noexcept -> T { return x.log2(); }

template<al::strong_floating_point T> [[nodiscard]] force_inline constexpr
auto log10(T const &x) noexcept -> T { return x.log10(); }

template<al::strong_floating_point T> [[nodiscard]] force_inline constexpr
auto exp(T const &x) noexcept -> T { return x.exp(); }

template<al::strong_floating_point T> [[nodiscard]] force_inline constexpr
auto exp2(T const &x) noexcept -> T { return x.exp2(); }

template<al::strong_floating_point T> [[nodiscard]] force_inline constexpr
auto round(T const &x) noexcept -> T { return x.round(); }

template<al::strong_floating_point T> [[nodiscard]] force_inline constexpr
auto lerp(T const &a, T const &b, T const &t) noexcept -> T
{ return T{std::lerp(a.c_val, b.c_val, t.c_val)}; }

#endif /* AL_TYPES_HPP */
