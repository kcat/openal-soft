#ifndef AL_STRING_H
#define AL_STRING_H

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string_view>


namespace al {

[[nodiscard]] constexpr
auto sizei(const std::string_view str) noexcept -> int
{ return static_cast<int>(std::min<std::size_t>(str.size(), std::numeric_limits<int>::max())); }

[[nodiscard]] constexpr
auto sizei(const std::wstring_view str) noexcept -> int
{ return static_cast<int>(std::min<std::size_t>(str.size(), std::numeric_limits<int>::max())); }


[[nodiscard]]
constexpr bool contains(const std::string_view str0, const std::string_view str1) noexcept
{ return str0.find(str1) != std::string_view::npos; }

[[nodiscard]]
auto case_compare(const std::string_view str0, const std::string_view str1) noexcept -> std::weak_ordering;

[[nodiscard]]
auto case_compare(const std::wstring_view str0, const std::wstring_view str1) noexcept -> std::weak_ordering;

/* C++20 changes path::u8string() to return a string using a new/distinct
 * char8_t type for UTF-8 strings, and deprecates u8path in favor of using
 * fs::path(char8_t*). However, support for this type with standard string
 * functions is totally inadequate, and we already hold UTF-8 with plain char*
 * strings. So these functions are used to reinterpret between char and char8_t
 * string views.
 */
inline auto char_as_u8(const std::string_view str) -> std::u8string_view
{ return std::u8string_view{reinterpret_cast<const char8_t*>(str.data()), str.size()}; }

inline auto u8_as_char(const std::u8string_view str) -> std::string_view
{ return std::string_view{reinterpret_cast<const char*>(str.data()), str.size()}; }

} // namespace al

#endif /* AL_STRING_H */
