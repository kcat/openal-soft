#ifndef AL_STRING_H
#define AL_STRING_H

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>


namespace al {

template<typename ...Ts>
[[nodiscard]] constexpr
auto sizei(const std::basic_string_view<Ts...> str) noexcept -> int
{ return static_cast<int>(std::min<std::size_t>(str.size(), std::numeric_limits<int>::max())); }

template<typename ...Ts>
[[nodiscard]] constexpr
auto sizei(const std::basic_string<Ts...> &str) noexcept -> int
{ return static_cast<int>(std::min<std::size_t>(str.size(), std::numeric_limits<int>::max())); }


[[nodiscard]]
constexpr bool contains(const std::string_view str0, const std::string_view str1) noexcept
{ return str0.find(str1) != std::string_view::npos; }

[[nodiscard]]
constexpr bool starts_with(const std::string_view str0, const std::string_view str1) noexcept
{ return str0.substr(0, std::min(str0.size(), str1.size())) == str1; }

[[nodiscard]]
constexpr bool ends_with(const std::string_view str0, const std::string_view str1) noexcept
{ return str0.substr(str0.size() - std::min(str0.size(), str1.size())) == str1; }

[[nodiscard]]
int case_compare(const std::string_view str0, const std::string_view str1) noexcept;

[[nodiscard]]
int case_compare(const std::wstring_view str0, const std::wstring_view str1) noexcept;

/* C++20 changes path::u8string() to return a string using a new/distinct
 * char8_t type for UTF-8 strings. However, support for this with standard
 * string functions is totally inadequate, and we already hold UTF-8 with plain
 * char strings. So this function is used to reinterpret a char8_t string as a
 * char string_view.
 */
#if defined(__cpp_lib_char8_t) && __cpp_lib_char8_t >= 201907L
inline auto u8_as_char(const std::u8string_view str) -> std::string_view
#else
inline auto u8_as_char(const std::string_view str) -> std::string_view
#endif
{
    return std::string_view{reinterpret_cast<const char*>(str.data()), str.size()};
}

} // namespace al

#endif /* AL_STRING_H */
