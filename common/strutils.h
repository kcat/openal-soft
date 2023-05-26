#ifndef AL_STRUTILS_H
#define AL_STRUTILS_H

#include <optional>
#include <string>

#ifdef _WIN32
#include <wchar.h>

std::string wstr_to_utf8(const wchar_t *wstr);
std::wstring utf8_to_wstr(const char *str);
#endif

namespace al {

std::optional<std::string> getenv(const char *envname);
#ifdef _WIN32
std::optional<std::wstring> getenv(const wchar_t *envname);
#endif

} // namespace al

#endif /* AL_STRUTILS_H */
