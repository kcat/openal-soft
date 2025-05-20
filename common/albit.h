#ifndef AL_BIT_H
#define AL_BIT_H

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace al {

template<std::integral T>
constexpr auto byteswap(T value) noexcept -> T
{
    static_assert(std::has_unique_object_representations_v<T>);
    auto bytes = std::bit_cast<std::array<std::byte,sizeof(T)>>(value);
    std::ranges::reverse(bytes);
    return std::bit_cast<T>(bytes);
}

} // namespace al

#endif /* AL_BIT_H */
