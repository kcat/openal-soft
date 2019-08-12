
#include "config.h"

#include "strutils.h"

#include <cstdlib>


namespace al {

al::optional<std::string> getenv(const char *envname)
{
    const char *str{std::getenv(envname)};
    if(str && str[0] != '\0') return str;
    return al::nullopt;
}

#ifdef _WIN32
al::optional<std::wstring> getenv(const WCHAR *envname)
{
    const WCHAR *str{_wgetenv(envname)};
    if(str && str[0] != L'\0') return str;
    return al::nullopt;
}
#endif

} // namespace al
