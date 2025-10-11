
#include "config.h"

#include "dynload.h"

#ifdef _WIN32
#include <windows.h>

#include <format>

#include "gsl/gsl"
#include "strutils.hpp"

auto LoadLib(gsl::czstring const name) -> al::expected<void*, std::string>
{
    if(auto const res = LoadLibraryW(utf8_to_wstr(name).c_str())) [[likely]]
        return res;
    auto const err = GetLastError();
    auto message = std::wstring{};
    message.resize(1024u);
    auto const res = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), message.data(),
        gsl::narrow_cast<DWORD>(message.size()), nullptr);
    if(res > 0)
    {
        message.resize(res);
        return al::unexpected(wstr_to_utf8(message));
    }
    return al::unexpected(std::format("LoadLibraryW error: {}", err));
}

void CloseLib(void *const handle)
{ FreeLibrary(static_cast<HMODULE>(handle)); }

auto GetSymbol(void *const handle, gsl::czstring const name) -> al::expected<void*, std::string>
{
    if(auto const sym = GetProcAddress(static_cast<HMODULE>(handle), name)) [[likely]]
    {
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
        return reinterpret_cast<void*>(sym);
    }
    auto const err = GetLastError();
    auto message = std::wstring{};
    message.resize(1024u);
    auto const res = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), message.data(),
        gsl::narrow_cast<DWORD>(message.size()), nullptr);
    if(res > 0)
    {
        message.resize(res);
        return al::unexpected(wstr_to_utf8(message));
    }
    return al::unexpected(std::format("GetProcAddress error: {}", err));
}

#elif defined(HAVE_DLFCN_H)

#include <dlfcn.h>

auto LoadLib(gsl::czstring const name) -> al::expected<void*, std::string>
{
    dlerror();
    auto *const handle = dlopen(name, RTLD_NOW);
    if(auto *const err = dlerror())
        return al::unexpected(err);
    return handle;
}

void CloseLib(void *const handle)
{ dlclose(handle); }

auto GetSymbol(void *const handle, gsl::czstring const name) -> al::expected<void*, std::string>
{
    dlerror();
    auto *const sym = dlsym(handle, name);
    if(auto *const err = dlerror())
        return al::unexpected(err);
    return sym;
}
#endif
