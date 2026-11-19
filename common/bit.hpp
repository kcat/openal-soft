#ifndef COMMON_BIT_HPP
#define COMMON_BIT_HPP

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "opthelper_macros.hpp"

namespace al {

template<std::unsigned_integral T> constexpr
auto bit_reverse(T const& value) noexcept -> T
{
    if constexpr(sizeof(T) == 0)
        return value;
#if HAS_BUILTIN(__builtin_bitreverse8)
    else if constexpr(sizeof(T) == 1)
        return __builtin_bitreverse8(static_cast<std::uint8_t>(value));
#endif
#if HAS_BUILTIN(__builtin_bitreverse16)
    else if constexpr(sizeof(T) == 2)
        return __builtin_bitreverse16(static_cast<std::uint16_t>(value));
#endif
#if HAS_BUILTIN(__builtin_bitreverse32)
    else if constexpr(sizeof(T) == 4)
        return __builtin_bitreverse32(static_cast<std::uint32_t>(value));
#endif
#if HAS_BUILTIN(__builtin_bitreverse64)
    else if constexpr(sizeof(T) == 8)
        return __builtin_bitreverse64(static_cast<std::uint64_t>(value));
#endif
    else if constexpr(sizeof(T) <= 4)
    {
        auto rev = std::uint32_t{value};
        rev = ((rev&std::uint32_t{0xaaaa'aaaa}) >> 1) | ((rev&std::uint32_t{0x5555'5555}) << 1);
        rev = ((rev&std::uint32_t{0xcccc'cccc}) >> 2) | ((rev&std::uint32_t{0x3333'3333}) << 2);
        rev = ((rev&std::uint32_t{0xf0f0'f0f0}) >> 4) | ((rev&std::uint32_t{0x0f0f'0f0f}) << 4);
        rev = ((rev&std::uint32_t{0xff00'ff00}) >> 8) | ((rev&std::uint32_t{0x00ff'00ff}) << 8);
        rev = ((rev >> 16) | (rev << 16)) >> (32 - sizeof(T)*8);
        return static_cast<T>(rev);
    }
    else
    {
        auto rev = std::uint64_t{value};
        rev = ((rev&std::uint64_t{0xaaaa'aaaa'aaaa'aaaa}) >> 1)
            | ((rev&std::uint64_t{0x5555'5555'5555'5555}) << 1);
        rev = ((rev&std::uint64_t{0xcccc'cccc'cccc'cccc}) >> 2)
            | ((rev&std::uint64_t{0x3333'3333'3333'3333}) << 2);
        rev = ((rev&std::uint64_t{0xf0f0'f0f0'f0f0'f0f0}) >> 4)
            | ((rev&std::uint64_t{0x0f0f'0f0f'0f0f'0f0f}) << 4);
        rev = ((rev&std::uint64_t{0xff00'ff00'ff00'ff00}) >> 8)
            | ((rev&std::uint64_t{0x00ff'00ff'00ff'00ff}) << 8);
        rev = ((rev&std::uint64_t{0xffff'0000'ffff'0000}) >> 16)
            | ((rev&std::uint64_t{0x0000'ffff'0000'ffff}) << 16);
        rev = ((rev >> 32) | (rev << 32)) >> (64 - sizeof(T)*8);
        return static_cast<T>(rev);
    }
}

template<std::integral T> constexpr
auto byteswap(T const& value) noexcept -> T
{
    static_assert(std::has_unique_object_representations_v<T>);
    auto bytes = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
    std::ranges::reverse(bytes);
    return std::bit_cast<T>(bytes);
}

} /* namespace al */

#endif /* COMMON_BIT_HPP */
