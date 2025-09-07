
#include "config.h"

#include "strutils.hpp"

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>

#include "gsl/gsl"

/* NOLINTBEGIN(bugprone-suspicious-stringview-data-usage) */
auto wstr_to_utf8(std::wstring_view wstr) -> std::string
{
    static constexpr auto flags = DWORD{WC_ERR_INVALID_CHARS};
    auto ret = std::string{};
    if(wstr.empty()) [[unlikely]]
        return ret;

    const auto u16len = gsl::narrow<int>(wstr.size());
    auto len = WideCharToMultiByte(CP_UTF8, flags, wstr.data(), u16len, nullptr, 0, nullptr,
        nullptr);
    if(len < 1) [[unlikely]]
        return ret;

    ret.resize(gsl::narrow<size_t>(len));
    len = WideCharToMultiByte(CP_UTF8, flags, wstr.data(), u16len, ret.data(), len, nullptr,
        nullptr);
    if(len < 1) [[unlikely]]
    {
        ret.clear();
        return ret;
    }

    return ret;
}

auto utf8_to_wstr(std::string_view str) -> std::wstring
{
    static constexpr auto flags = DWORD{MB_ERR_INVALID_CHARS};
    auto ret = std::wstring{};
    if(str.empty()) [[unlikely]]
        return ret;

    const auto u8len = gsl::narrow<int>(str.size());
    auto len = MultiByteToWideChar(CP_UTF8, flags, str.data(), u8len, nullptr, 0);
    if(len < 1) [[unlikely]]
        return ret;

    ret.resize(gsl::narrow<size_t>(len));
    len = MultiByteToWideChar(CP_UTF8, flags, str.data(), u8len, ret.data(), len);
    if(len < 1) [[unlikely]]
    {
        ret.clear();
        return ret;
    }

    return ret;
}
/* NOLINTEND(bugprone-suspicious-stringview-data-usage) */

namespace al {

auto getenv(const gsl::czstring envname) -> std::optional<std::string>
{
    auto *str = _wgetenv(utf8_to_wstr(envname).c_str());
    if(str && *str != L'\0')
        return wstr_to_utf8(str);
    return std::nullopt;
}

auto getenv(const gsl::cwzstring envname) -> std::optional<std::wstring>
{
    auto *str = _wgetenv(envname);
    if(str && *str != L'\0')
        return str;
    return std::nullopt;
}

} /* namespace al */

#else

namespace al {

auto getenv(const gsl::czstring envname) -> std::optional<std::string>
{
    auto *str = std::getenv(envname);
    if(str && *str != '\0')
        return str;
    return std::nullopt;
}

} /* namespace al */
#endif
