#ifndef AL_STRING_H
#define AL_STRING_H

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>


namespace al {

template<typename T, typename Traits>
[[nodiscard]] constexpr
auto sizei(const std::basic_string_view<T,Traits> str) noexcept -> int
{ return static_cast<int>(std::min<std::size_t>(str.size(), std::numeric_limits<int>::max())); }

[[nodiscard]]
constexpr bool contains(const std::string_view str0, const std::string_view str1) noexcept
{ return str0.find(str1) != std::string_view::npos; }

[[nodiscard]]
constexpr bool starts_with(const std::string_view str0, const std::string_view str1) noexcept
{ return str0.substr(0, std::min(str0.size(), str1.size())) == str1; }

[[nodiscard]]
int case_compare(const std::string_view str0, const std::string_view str1) noexcept;

[[nodiscard]]
int strcasecmp(const char *str0, const char *str1) noexcept;
[[nodiscard]]
int strncasecmp(const char *str0, const char *str1, std::size_t len) noexcept;

} // namespace al

#endif /* AL_STRING_H */
