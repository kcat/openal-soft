#ifndef AL_STRUTILS_HPP
#define AL_STRUTILS_HPP

#include <optional>
#include <string>

#ifdef _WIN32
#include <cwchar>
#include <string_view>

auto wstr_to_utf8(std::wstring_view wstr) -> std::string;
auto utf8_to_wstr(std::string_view str) -> std::wstring;

namespace al {

auto getenv(const wchar_t *envname) -> std::optional<std::wstring>;

} /* namespace al */
#endif

namespace al {

auto getenv(const char *envname) -> std::optional<std::string>;

} /* namespace al */

#endif /* AL_STRUTILS_HPP */
