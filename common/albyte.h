#ifndef AL_BYTE_H
#define AL_BYTE_H

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

using uint = unsigned int;

namespace al {

/* The "canonical" way to store raw byte data. Like C++17's std::byte, it's not
 * treated as a character type and does not work with arithmatic ops. Only
 * bitwise ops are allowed.
 */
enum class byte : unsigned char { };

template<typename T>
constexpr std::enable_if_t<std::is_integral<T>::value,T>
to_integer(al::byte b) noexcept { return T(b); }


template<typename T>
constexpr std::enable_if_t<std::is_integral<T>::value,al::byte>
operator<<(al::byte lhs, T rhs) noexcept { return al::byte(to_integer<uint>(lhs) << rhs); }

template<typename T>
constexpr std::enable_if_t<std::is_integral<T>::value,al::byte>
operator>>(al::byte lhs, T rhs) noexcept { return al::byte(to_integer<uint>(lhs) >> rhs); }

template<typename T>
constexpr std::enable_if_t<std::is_integral<T>::value,al::byte&>
operator<<=(al::byte &lhs, T rhs) noexcept { lhs = lhs << rhs; return lhs; }

template<typename T>
constexpr std::enable_if_t<std::is_integral<T>::value,al::byte&>
operator>>=(al::byte &lhs, T rhs) noexcept { lhs = lhs >> rhs; return lhs; }

#define AL_DECL_OP(op, opeq)                                                  \
template<typename T>                                                          \
constexpr std::enable_if_t<std::is_integral<T>::value,al::byte>               \
operator op (al::byte lhs, T rhs) noexcept                                    \
{ return al::byte(to_integer<uint>(lhs) op static_cast<uint>(rhs)); }         \
                                                                              \
template<typename T>                                                          \
constexpr std::enable_if_t<std::is_integral<T>::value,al::byte&>              \
operator opeq (al::byte &lhs, T rhs) noexcept { lhs = lhs op rhs; return lhs; } \
                                                                              \
constexpr al::byte operator op (al::byte lhs, al::byte rhs) noexcept          \
{ return al::byte(lhs op to_integer<uint>(rhs)); }                            \
                                                                              \
constexpr al::byte& operator opeq (al::byte &lhs, al::byte rhs) noexcept      \
{ lhs = lhs op rhs; return lhs; }

AL_DECL_OP(|, |=)
AL_DECL_OP(&, &=)
AL_DECL_OP(^, ^=)

#undef AL_DECL_OP

constexpr al::byte operator~(al::byte b) noexcept
{ return al::byte(~to_integer<uint>(b)); }

} // namespace al

#endif /* AL_BYTE_H */
