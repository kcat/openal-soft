
#include "config.h"

#include "strutils.h"

#include <cstdlib>


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

std::string wstr_to_utf8(std::wstring_view wstr)
{
    std::string ret;

    int len{WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.length()), nullptr,
        0, nullptr, nullptr)};
    if(len > 0)
    {
        ret.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.length()), &ret[0], len,
            nullptr, nullptr);
    }

    return ret;
}

std::wstring utf8_to_wstr(std::string_view str)
{
    std::wstring ret;

    int len{MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.length()), nullptr,
        0)};
    if(len > 0)
    {
        ret.resize(len);
        MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.length()), &ret[0], len);
    }

    return ret;
}
#endif

namespace al {

std::optional<std::string> getenv(const char *envname)
{
#ifdef _GAMING_XBOX
    const char *str{::getenv(envname)};
#else
    const char *str{std::getenv(envname)};
#endif
    if(str && str[0] != '\0')
        return str;
    return std::nullopt;
}

#ifdef _WIN32
std::optional<std::wstring> getenv(const WCHAR *envname)
{
    const WCHAR *str{_wgetenv(envname)};
    if(str && str[0] != L'\0')
        return str;
    return std::nullopt;
}
#endif

} // namespace al
