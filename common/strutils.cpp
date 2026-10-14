
#include "config.h"

#include "strutils.hpp"

#include <cstdlib>

#if HAVE_CXXMODULES
#if defined(_WIN32)
import alstring;
#endif
import gsl;

#else

#if defined(_WIN32)
#include "alstring.h"
#endif
#include "gsl/zstring"
#endif

namespace al {

#if defined(_WIN32)
auto getenv(gsl::czstring const envname) -> std::optional<std::string>
{
    if(auto const*const str = _wgetenv(utf8_to_wstr(envname).c_str()); str && *str != L'\0')
        return wstr_to_utf8(str);
    return std::nullopt;
}

auto getenv(gsl::cwzstring const envname) -> std::optional<std::wstring>
{
    if(auto const*const str = _wgetenv(envname); str && *str != L'\0')
        return str;
    return std::nullopt;
}

#else

auto getenv(gsl::czstring const envname) -> std::optional<std::string>
{
    if(auto const*const str = std::getenv(envname); str && *str != '\0')
        return str;
    return std::nullopt;
}
#endif

} /* namespace al */
