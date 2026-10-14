
#include "config.h"

#if defined(_WIN32)
#include <string>
#include <string_view>
#include <windows.h>

#if HAVE_CXXMODULES
import alstring;
import gsl;
#else
#include "alstring.h"
#include "gsl/gsl"
#endif


/* NOLINTBEGIN(bugprone-suspicious-stringview-data-usage) */
auto wstr_to_utf8(std::wstring_view const wstr) -> std::string
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

    ret.resize(gsl::narrow<std::size_t>(len));
    len = WideCharToMultiByte(CP_UTF8, flags, wstr.data(), u16len, ret.data(), len, nullptr,
        nullptr);
    if(len < 1) [[unlikely]]
    {
        ret.clear();
        return ret;
    }

    return ret;
}

auto utf8_to_wstr(std::string_view const str) -> std::wstring
{
    static constexpr auto flags = DWORD{MB_ERR_INVALID_CHARS};
    auto ret = std::wstring{};
    if(str.empty()) [[unlikely]]
        return ret;

    const auto u8len = gsl::narrow<int>(str.size());
    auto len = MultiByteToWideChar(CP_UTF8, flags, str.data(), u8len, nullptr, 0);
    if(len < 1) [[unlikely]]
        return ret;

    ret.resize(gsl::narrow<std::size_t>(len));
    len = MultiByteToWideChar(CP_UTF8, flags, str.data(), u8len, ret.data(), len);
    if(len < 1) [[unlikely]]
    {
        ret.clear();
        return ret;
    }

    return ret;
}
/* NOLINTEND(bugprone-suspicious-stringview-data-usage) */

#endif
