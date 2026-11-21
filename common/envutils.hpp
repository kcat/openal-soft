#ifndef COMMON_ENVUTILS_HPP
#define COMMON_ENVUTILS_HPP

#include <optional>
#include <string>

#include "zstring_view.hpp"

namespace al {

auto getenv(al::zstring_view envname) -> std::optional<std::string>;
#ifdef _WIN32
auto getenv(al::wzstring_view envname) -> std::optional<std::wstring>;
#endif

} /* namespace al */

#endif /* COMMON_ENVUTILS_HPP */
