#ifndef AL_BIT_H
#define AL_BIT_H

#include <algorithm>
#include <array>
#ifndef __GNUC__
#include <cstdint>
#endif
#include <cstring>
#include <new>
#include <type_traits>
#if !defined(__GNUC__) && (defined(_WIN32) || defined(_WIN64))
#include <intrin.h>
#endif

namespace al {

template<typename To, typename From>
std::enable_if_t<sizeof(To) == sizeof(From) && std::is_trivially_copyable_v<From>
    && std::is_trivially_copyable_v<To>,
To> bit_cast(const From &src) noexcept
{
    alignas(To) std::array<char,sizeof(To)> dst;
    std::memcpy(dst.data(), &src, sizeof(To));
    return *std::launder(reinterpret_cast<To*>(dst.data()));
}

template<typename T>
std::enable_if_t<std::is_integral_v<T>,
T> byteswap(T value) noexcept
{
    static_assert(std::has_unique_object_representations_v<T>);
    auto bytes = al::bit_cast<std::array<std::byte,sizeof(T)>>(value);
    std::reverse(bytes.begin(), bytes.end());
    return al::bit_cast<T>(bytes);
}

#ifdef __BYTE_ORDER__
enum class endian {
    little = __ORDER_LITTLE_ENDIAN__,
    big = __ORDER_BIG_ENDIAN__,
    native = __BYTE_ORDER__
};

#else

/* This doesn't support mixed-endian. */
namespace detail_ {
constexpr bool IsLittleEndian() noexcept
{
    static_assert(sizeof(char) < sizeof(int), "char is too big");

    constexpr int test_val{1};
    return static_cast<const char&>(test_val) ? true : false;
}
} // namespace detail_

enum class endian {
    big = 0,
    little = 1,
    native = detail_::IsLittleEndian() ? little : big
};
#endif

} // namespace al

#endif /* AL_BIT_H */
