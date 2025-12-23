#ifndef AL_TYPES_HPP
#define AL_TYPES_HPP

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "gsl/gsl"


using i8 = std::int8_t;
using u8 = std::uint8_t;
using i16 = std::int16_t;
using u16 = std::uint16_t;
using i32 = std::int32_t;
using u32 = std::uint32_t;
using i64 = std::int64_t;
using u64 = std::uint64_t;
using isize = std::make_signed_t<std::size_t>;
using usize = std::size_t;
using f32 = float;
using f64 = double;


[[nodiscard]] consteval
auto operator ""_i8(unsigned long long const n) noexcept { return gsl::narrow<i8>(n); }
[[nodiscard]] consteval
auto operator ""_u8(unsigned long long const n) noexcept { return gsl::narrow<u8>(n); }

[[nodiscard]] consteval
auto operator ""_i16(unsigned long long const n) noexcept { return gsl::narrow<i16>(n); }
[[nodiscard]] consteval
auto operator ""_u16(unsigned long long const n) noexcept { return gsl::narrow<u16>(n); }

[[nodiscard]] consteval
auto operator ""_i32(unsigned long long const n) noexcept { return gsl::narrow<i32>(n); }
[[nodiscard]] consteval
auto operator ""_u32(unsigned long long const n) noexcept { return gsl::narrow<u32>(n); }

[[nodiscard]] consteval
auto operator ""_i64(unsigned long long const n) noexcept { return gsl::narrow<i64>(n); }
[[nodiscard]] consteval
auto operator ""_u64(unsigned long long const n) noexcept { return gsl::narrow<u64>(n); }

[[nodiscard]] consteval
auto operator ""_z(unsigned long long const n) noexcept { return gsl::narrow<isize>(n); }
[[nodiscard]] consteval
auto operator ""_uz(unsigned long long const n) noexcept { return gsl::narrow<usize>(n); }
[[nodiscard]] consteval
auto operator ""_zu(unsigned long long const n) noexcept { return gsl::narrow<usize>(n); }

[[nodiscard]] consteval
auto operator ""_f32(long double const n) noexcept { return static_cast<f32>(n); }
[[nodiscard]] consteval
auto operator ""_f64(long double const n) noexcept { return static_cast<f64>(n); }

#endif /* AL_TYPES_HPP */
