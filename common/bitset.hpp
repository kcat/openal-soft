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

    template<typename T> requires(std::is_enum_v<T> and requires { test_int_conversion(T{}); })
    [[nodiscard]] consteval auto get_count() noexcept
    {
        using UnderlyingType = std::make_unsigned_t<std::underlying_type_t<T>>;
        /* The given enum type's "MaxValue" enumeration specifies the largest
         * index that will be used for the bitset. Or the "Count" enumeration
         * specifies the number if indices (it's undefined to access the
         * "Count" index itself).
         */
        if constexpr(requires { T::MaxValue; })
        {
            static_assert(not requires { T::Count; }); /* Avoid ambiguity. */
            return static_cast<UnderlyingType>(static_cast<UnderlyingType>(T::MaxValue) + 1);
        }
        else if constexpr(requires { T::Count; })
            return static_cast<UnderlyingType>(T::Count);
    }
}

template<typename T>
concept scoped_enum = std::is_enum_v<T> and requires { detail_::test_int_conversion(T{}); };

template<scoped_enum EnumType>
class bitset {
    using UnderlyingType = std::make_unsigned_t<std::underlying_type_t<EnumType>>;

    using BitsetType = std::bitset<detail_::get_count<EnumType>()>;
    BitsetType mBits;

    force_inline explicit constexpr
    bitset(BitsetType const &rhs) noexcept : mBits(rhs) { }

public:
    using reference = typename BitsetType::reference;

    constexpr bitset() noexcept = default;
    constexpr bitset(bitset const &rhs) noexcept = default;
    constexpr ~bitset() noexcept = default;

    force_inline explicit constexpr bitset(unsigned long long const arg) noexcept : mBits{arg} { }

    template<typename CharT, typename Traits, typename Alloc> force_inline explicit constexpr
    bitset(std::basic_string<CharT,Traits,Alloc> const& str,
        typename std::basic_string<CharT,Traits,Alloc>::size_type const pos=0,
        typename std::basic_string<CharT,Traits,Alloc>::size_type const
            n=std::basic_string<CharT,Traits,Alloc>::npos,
        CharT const zero=CharT{'0'}, CharT const one=CharT{'1'}) : mBits{str, pos, n, zero, one}
    { }

    template<typename CharT, typename Traits> force_inline explicit constexpr
    bitset(std::basic_string_view<CharT,Traits> const str, std::size_t const pos=0,
        std::size_t const n=static_cast<std::size_t>(-1), CharT const zero=CharT{'0'},
        CharT const one=CharT{'1'}) : mBits{str, pos, n, zero, one}
    { }

    template<typename CharT> force_inline explicit constexpr
    bitset(CharT const *const str, std::size_t const n=static_cast<std::size_t>(-1),
        CharT const zero=CharT{'0'}, CharT const one=CharT{'1'}) : mBits{str, n, zero, one}
    { }


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
    auto operator|=(bitset const &rhs) noexcept LIFETIMEBOUND -> bitset&
    { mBits |= rhs.mBits; return *this; }

    force_inline constexpr
    auto operator&=(bitset const &rhs) noexcept LIFETIMEBOUND -> bitset&
    { mBits &= rhs.mBits; return *this; }

    force_inline constexpr
    auto operator^=(bitset const &rhs) noexcept LIFETIMEBOUND -> bitset&
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
