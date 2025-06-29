#ifndef AL_STRUTILS_HPP
#define AL_STRUTILS_HPP

#include <optional>
#include <string>

#include "gsl/gsl"

#ifdef _WIN32
#include <string_view>

auto wstr_to_utf8(std::wstring_view wstr) -> std::string;
auto utf8_to_wstr(std::string_view str) -> std::wstring;

namespace al {

auto getenv(const gsl::cwzstring envname) -> std::optional<std::wstring>;

} /* namespace al */
#endif

namespace al {

auto getenv(const gsl::czstring envname) -> std::optional<std::string>;

} /* namespace al */

#endif /* AL_STRUTILS_HPP */
