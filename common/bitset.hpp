#ifndef COMMON_BITSET_HPP
#define COMMON_BITSET_HPP

#include <bit>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

#include "opthelpers.h"


namespace al {

namespace detail_ {
    void test_int_conversion(...);
    void test_int_conversion(int) = delete;
}

template<typename T>
concept scoped_enum = std::is_enum_v<T> and requires(T t) { detail_::test_int_conversion(t); };

template<scoped_enum EnumType>
class bitset {
    static constexpr auto num_bits = std::invoke([] {
        /* The given enum type's "MaxValue" enumeration specifies the largest
         * index that will be used for the bitset. Or the "Count" enumeration
         * specifies the number of indices (it's undefined to access the
         * "Count" index itself).
         */
        if constexpr(requires { EnumType::MaxValue; })
        {
            static_assert(not requires { EnumType::Count; }); /* Avoid ambiguity. */
            return unsigned{al::to_underlying(EnumType::MaxValue) + 1};
        }
        else if constexpr(requires { EnumType::Count; })
            return unsigned{al::to_underlying(EnumType::Count)};
        else
        {
            static_assert(requires { EnumType::Count; } or requires { EnumType::MaxValue; });
            return 1u;
        }
    });

    static_assert(num_bits > 0 and num_bits <= 64);
    using integer_t = std::conditional_t<std::cmp_less_equal(num_bits, 32), std::uint32_t,
        std::uint64_t>;

    using UnderlyingType = std::make_unsigned_t<std::underlying_type_t<EnumType>>;

    static constexpr auto AllBits = integer_t{(((integer_t{1u} << (num_bits-1)) - 1u)<<1u) | 1u};

    integer_t mBits{0u};

public:
    constexpr bitset() noexcept = default;
    constexpr bitset(bitset const &rhs) noexcept = default;
    constexpr ~bitset() noexcept = default;

    explicit constexpr
    bitset(unsigned long long const arg) noexcept : mBits{static_cast<integer_t>(arg&AllBits)} { }

    [[nodiscard]] explicit constexpr
    operator bool() const noexcept { return mBits != 0u; }


    constexpr
    auto set(EnumType const e, bool const s=true) noexcept LIFETIMEBOUND -> bitset&
    {
        if(s) mBits |= integer_t{1u} << static_cast<UnderlyingType>(e);
        else mBits &= ~(integer_t{1u} << static_cast<UnderlyingType>(e));
        return *this;
    }

    constexpr
    auto set() noexcept LIFETIMEBOUND -> bitset& { mBits = AllBits; return *this; }

    constexpr
    auto reset(EnumType const e) noexcept LIFETIMEBOUND -> bitset&
    { mBits &= ~(1u << static_cast<UnderlyingType>(e)); return *this; }

    constexpr
    auto reset() noexcept LIFETIMEBOUND -> bitset& { mBits = 0u; return *this; }

    [[nodiscard]] constexpr
    auto test(EnumType const e) const noexcept -> bool
    { return (mBits & (integer_t{1u} << static_cast<UnderlyingType>(e))) != 0u; }

    [[nodiscard]] constexpr
    auto any() const noexcept -> bool { return mBits != 0u; }
    [[nodiscard]] constexpr
    auto all() const noexcept -> bool { return mBits == AllBits; }
    [[nodiscard]] constexpr
    auto none() const noexcept -> bool { return mBits == 0u; }

    [[nodiscard]] constexpr
    auto flip() noexcept LIFETIMEBOUND -> bitset& { mBits ^= AllBits; return *this; }
    [[nodiscard]] constexpr
    auto flip(EnumType const e) noexcept LIFETIMEBOUND -> bitset&
    { mBits ^= integer_t{1u} << static_cast<UnderlyingType>(e); return *this; }

    [[nodiscard]] constexpr
    auto count() const noexcept -> std::size_t
    { return static_cast<std::size_t>(std::popcount(mBits)); }
    [[nodiscard]] static constexpr
    auto size() noexcept -> std::size_t { return num_bits; }

    [[nodiscard]] constexpr
    auto operator~() const noexcept -> bitset { return bitset{~mBits}; }

    constexpr
    auto operator|=(bitset const &rhs) noexcept LIFETIMEBOUND -> bitset&
    { mBits |= rhs.mBits; return *this; }

    constexpr
    auto operator&=(bitset const &rhs) noexcept LIFETIMEBOUND -> bitset&
    { mBits &= rhs.mBits; return *this; }

    constexpr
    auto operator^=(bitset const &rhs) noexcept LIFETIMEBOUND -> bitset&
    { mBits ^= rhs.mBits; return *this; }

    [[nodiscard]] friend constexpr
    auto operator|(bitset const &lhs, bitset const &rhs) noexcept -> bitset
    { return bitset{lhs.mBits | rhs.mBits}; }

    [[nodiscard]] friend constexpr
    auto operator&(bitset const &lhs, bitset const &rhs) noexcept -> bitset
    { return bitset{lhs.mBits & rhs.mBits}; }

    [[nodiscard]] friend constexpr
    auto operator^(bitset const &lhs, bitset const &rhs) noexcept -> bitset
    { return bitset{lhs.mBits ^ rhs.mBits}; }

    [[nodiscard]] friend constexpr
    auto operator==(bitset const &lhs, bitset const &rhs) noexcept -> bool
    { return lhs.mBits == rhs.mBits; }
};

} /* namespace al */

#endif /* COMMON_BITSET_HPP */
