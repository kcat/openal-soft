#ifndef AL_STRUTILS_H
#define AL_STRUTILS_H

#include <string>

#include "aloptional.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>


inline std::string wstr_to_utf8(const WCHAR *wstr)
{
    std::string ret;

    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if(len > 0)
    {
        ret.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &ret[0], len, nullptr, nullptr);
        ret.pop_back();
    }

    return ret;
}

inline std::wstring utf8_to_wstr(const char *str)
{
    std::wstring ret;

    int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
    if(len > 0)
    {
        ret.resize(len);
        MultiByteToWideChar(CP_UTF8, 0, str, -1, &ret[0], len);
        ret.pop_back();
    }

    return ret;
}

#endif

namespace al {

al::optional<std::string> getenv(const char *envname);
#ifdef _WIN32
al::optional<std::wstring> getenv(const WCHAR *envname);
#endif

} // namespace al

#endif /* AL_STRUTILS_H */
