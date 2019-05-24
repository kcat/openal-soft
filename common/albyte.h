#ifndef AL_BYTE_H
#define AL_BYTE_H

#include <type_traits>

namespace al {

/* The "canonical" way to store raw byte data. Like C++17's std::byte, it's not
 * treated as a character type and does not work with arithmatic ops. Only
 * bitwise ops are allowed.
 */
enum class byte : unsigned char { };

template<typename T, typename std::enable_if<std::is_integral<T>::value,int>::type = 0>
inline constexpr T to_integer(al::byte b) noexcept { return T(b); }


template<typename T, typename std::enable_if<std::is_integral<T>::value,int>::type = 0>
inline constexpr al::byte operator<<(al::byte lhs, T rhs) noexcept
{ return al::byte(to_integer<unsigned int>(lhs) << rhs); }

template<typename T, typename std::enable_if<std::is_integral<T>::value,int>::type = 0>
inline constexpr al::byte operator>>(al::byte lhs, T rhs) noexcept
{ return al::byte(to_integer<unsigned int>(lhs) >> rhs); }

#define AL_DECL_OP(op)                                                        \
template<typename T, typename std::enable_if<std::is_integral<T>::value,int>::type = 0> \
inline constexpr al::byte operator op (al::byte lhs, T rhs) noexcept          \
{ return al::byte(to_integer<unsigned int>(lhs) op rhs); }                    \
inline constexpr al::byte operator op (al::byte lhs, al::byte rhs) noexcept   \
{ return al::byte(lhs op to_integer<unsigned int>(rhs)); }

AL_DECL_OP(|)
AL_DECL_OP(&)
AL_DECL_OP(^)

#undef AL_DECL_OP

inline constexpr al::byte operator~(al::byte b) noexcept
{ return al::byte(~to_integer<unsigned int>(b)); }


template<typename T, typename std::enable_if<std::is_integral<T>::value,int>::type = 0>
inline al::byte& operator<<=(al::byte &lhs, T rhs) noexcept
{ lhs = lhs << rhs; return lhs; }

template<typename T, typename std::enable_if<std::is_integral<T>::value,int>::type = 0>
inline al::byte& operator>>=(al::byte &lhs, T rhs) noexcept
{ lhs = lhs >> rhs; return lhs; }

#define AL_DECL_OP(op)                                                        \
template<typename T, typename std::enable_if<std::is_integral<T>::value,int>::type = 0> \
inline al::byte& operator op##= (al::byte &lhs, T rhs) noexcept               \
{ lhs = lhs op rhs; return lhs; }                                             \
inline al::byte& operator op##= (al::byte &lhs, al::byte rhs) noexcept        \
{ lhs = lhs op rhs; return lhs; }

AL_DECL_OP(|)
AL_DECL_OP(&)
AL_DECL_OP(^)

#undef AL_DECL_OP

} // namespace al

#endif /* AL_BYTE_H */
