#ifndef COMMON_BITSET_HPP
#define COMMON_BITSET_HPP

#include <bitset>
#include <concepts>
#include <type_traits>

#include "opthelpers.h"


namespace al {

namespace detail_ {
    void test_int_conversion(...);
    void test_int_conversion(int) = delete;
}

template<typename T>
concept scoped_enum = std::is_enum_v<T> and requires { detail_::test_int_conversion(T{}); };


template<scoped_enum auto MaxIndex>
class bitset {
    using EnumType = decltype(MaxIndex);
    using UnderlyingType = std::make_unsigned_t<std::underlying_type_t<EnumType>>;
    static constexpr std::unsigned_integral auto Count = static_cast<UnderlyingType>(MaxIndex)+1u;

    using BitsetType = std::bitset<Count>;
    BitsetType mBits;

    force_inline explicit constexpr
    bitset(BitsetType const &rhs) noexcept : mBits(rhs) { }

public:
    using reference = typename BitsetType::reference;

    constexpr bitset() noexcept = default;
    constexpr bitset(bitset const &rhs) noexcept = default;
    constexpr ~bitset() noexcept = default;

    [[nodiscard]] force_inline constexpr
    auto get_bitset() const noexcept LIFETIMEBOUND -> BitsetType const& { return mBits; }

    [[nodiscard]] force_inline explicit constexpr
    operator bool() const noexcept { return bool{mBits}; }


    force_inline constexpr
    auto set(EnumType const e, bool s=true) noexcept LIFETIMEBOUND -> bitset&
    { mBits.set(static_cast<UnderlyingType>(e), s); return *this; }

    force_inline constexpr
    auto reset(EnumType const e) noexcept LIFETIMEBOUND -> bitset&
    { mBits.reset(static_cast<UnderlyingType>(e)); return *this; }

    force_inline constexpr
    auto reset() noexcept LIFETIMEBOUND -> bitset& { mBits.reset(); return *this; }

    [[nodiscard]] force_inline constexpr
    auto test(EnumType const e) const noexcept -> bool
    { return mBits.test(static_cast<UnderlyingType>(e)); }

    [[nodiscard]] force_inline constexpr auto any() const noexcept -> bool { return mBits.any(); }
    [[nodiscard]] force_inline constexpr auto all() const noexcept -> bool { return mBits.all(); }
    [[nodiscard]] force_inline constexpr
    auto none() const noexcept -> bool { return mBits.none(); }

    [[nodiscard]] force_inline constexpr
    auto flip() const noexcept LIFETIMEBOUND -> bitset& { mBits.flip(); return *this; }
    [[nodiscard]] force_inline constexpr
    auto flip(EnumType const e) const noexcept LIFETIMEBOUND -> bitset&
    { mBits.flip(static_cast<UnderlyingType>(e)); return *this; }

    [[nodiscard]] force_inline constexpr
    auto count() const noexcept -> std::size_t { return mBits.count(); }
    [[nodiscard]] force_inline constexpr
    auto size() const noexcept -> std::size_t { return mBits.size(); }

    [[nodiscard]] force_inline constexpr
    auto operator[](EnumType const e) noexcept -> decltype(auto)
    { return mBits[static_cast<UnderlyingType>(e)]; }

    [[nodiscard]] force_inline constexpr
    auto operator[](EnumType const e) const noexcept -> decltype(auto)
    { return mBits[static_cast<UnderlyingType>(e)]; }

    [[nodiscard]] force_inline constexpr
    auto operator~() const noexcept -> bitset { return bitset{~mBits}; }

    force_inline constexpr
    auto operator|=(bitset const &rhs LIFETIMEBOUND) noexcept -> bitset&
    { mBits |= rhs.mBits; return *this; }

    force_inline constexpr
    auto operator&=(bitset const &rhs LIFETIMEBOUND) noexcept -> bitset&
    { mBits &= rhs.mBits; return *this; }

    force_inline constexpr
    auto operator^=(bitset const &rhs LIFETIMEBOUND) noexcept -> bitset&
    { mBits ^= rhs.mBits; return *this; }

    [[nodiscard]] force_inline friend constexpr
    auto operator|(bitset const &lhs, bitset const &rhs) noexcept -> bitset
    { return bitset{lhs.mBits | rhs.mBits}; }

    [[nodiscard]] force_inline friend constexpr
    auto operator&(bitset const &lhs, bitset const &rhs) noexcept -> bitset
    { return bitset{lhs.mBits & rhs.mBits}; }

    [[nodiscard]] force_inline friend constexpr
    auto operator^(bitset const &lhs, bitset const &rhs) noexcept -> bitset
    { return bitset{lhs.mBits ^ rhs.mBits}; }

    [[nodiscard]] force_inline friend constexpr
    auto operator==(bitset const &lhs, bitset const &rhs) noexcept -> bool
    { return lhs.mBits() == rhs.mBits(); }
};

} /* namespace al */

#endif /* COMMON_BITSET_HPP */
