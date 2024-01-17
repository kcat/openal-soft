#ifndef AL_STRING_H
#define AL_STRING_H

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string_view>


namespace al {

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
