
#include "config.h"

#include <cstdlib>
#include <string>

#if HAVE_CXXMODULES
import common.std;

import envutils;
import zstring_view;
#if defined(_WIN32)
import alstring;
import opthelpers;
#endif

#else

#include <optional>

#include "envutils.hpp"
#include "zstring_view.hpp"
#if defined(_WIN32)
#include "alstring.h"
#include "opthelpers.hpp"
#endif
#endif

namespace al {

#if defined(_WIN32)
auto getenv(al::zstring_view const envname) -> std::optional<std::string>
{
    if(auto const*const str = _wgetenv(utf8_to_wstr(envname).c_str()); str && *str != L'\0')
        return al::assign_result{[str]{ return wstr_to_utf8(str); }};
    return std::nullopt;
}

auto getenv(al::wzstring_view const envname) -> std::optional<std::wstring>
{
    if(auto const*const str = _wgetenv(envname.c_str()); str && *str != L'\0')
        return str;
    return std::nullopt;
}

#else

auto getenv(al::zstring_view const envname) -> std::optional<std::string>
{
    if(auto const*const str = std::getenv(envname.c_str()); str && *str != '\0')
        return str;
    return std::nullopt;
}
#endif

} /* namespace al */
