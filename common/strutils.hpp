#ifndef AL_STRUTILS_HPP
#define AL_STRUTILS_HPP

#include <optional>
#include <string>

#include "gsl/zstring"

namespace al {

auto getenv(gsl::czstring envname) -> std::optional<std::string>;
#ifdef _WIN32
auto getenv(gsl::cwzstring envname) -> std::optional<std::wstring>;
#endif

} /* namespace al */

#endif /* AL_STRUTILS_HPP */
