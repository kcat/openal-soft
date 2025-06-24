
#include "config.h"

#include "strutils.hpp"

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>

#include "alstring.h"

/* NOLINTBEGIN(bugprone-suspicious-stringview-data-usage) */
auto wstr_to_utf8(std::wstring_view wstr) -> std::string
{
    auto ret = std::string{};

    const auto len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), al::sizei(wstr), nullptr, 0,
        nullptr, nullptr);
    if(len > 0)
    {
        ret.resize(static_cast<size_t>(len));
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), al::sizei(wstr), ret.data(), len,
            nullptr, nullptr);
    }

    return ret;
}

auto utf8_to_wstr(std::string_view str) -> std::wstring
{
    auto ret = std::wstring{};

    const auto len = MultiByteToWideChar(CP_UTF8, 0, str.data(), al::sizei(str), nullptr, 0);
    if(len > 0)
    {
        ret.resize(static_cast<size_t>(len));
        MultiByteToWideChar(CP_UTF8, 0, str.data(), al::sizei(str), ret.data(), len);
    }

    return ret;
}
/* NOLINTEND(bugprone-suspicious-stringview-data-usage) */

namespace al {

auto getenv(const char *envname) -> std::optional<std::string>
{
    auto *str = _wgetenv(utf8_to_wstr(envname).c_str());
    if(str && *str != L'\0')
        return wstr_to_utf8(str);
    return std::nullopt;
}

auto getenv(const WCHAR *envname) -> std::optional<std::wstring>
{
    auto *str = _wgetenv(envname);
    if(str && *str != L'\0')
        return str;
    return std::nullopt;
}

} /* namespace al */

#else

namespace al {

auto getenv(const char *envname) -> std::optional<std::string>
{
    auto *str = std::getenv(envname);
    if(str && *str != '\0')
        return str;
    return std::nullopt;
}

} /* namespace al */
#endif
