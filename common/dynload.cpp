
#include "config.h"

#include "dynload.h"

#if HAVE_DYNLOAD

#ifdef _WIN32
#include <windows.h>

#include "alformat.hpp"
#include "gsl/gsl"
#include "strutils.hpp"

auto LoadLib(al::zstring_view const name) -> al::expected<LibHandle, std::string>
{
    if(auto const res = LoadLibraryW(utf8_to_wstr(name).c_str())) [[likely]]
        return reinterpret_cast<LibHandle>(res); /* NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) */
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
    return al::unexpected(al::format("LoadLibraryW error: {}", err));
}

void CloseLib(LibHandle const handle)
{
    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
}

auto GetSymbol_(LibHandle const handle, al::zstring_view const name)
    -> al::expected<void*, std::string>
{
    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
    if(auto const sym = GetProcAddress(reinterpret_cast<HMODULE>(handle), name.c_str())) [[likely]]
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
    return al::unexpected(al::format("GetProcAddress error: {}", err));
}

#elif defined(HAVE_DLFCN_H)

#include <dlfcn.h>

auto LoadLib(al::zstring_view const name) -> al::expected<LibHandle, std::string>
{
    if(auto *const handle = dlopen(name.c_str(), RTLD_NOW))
        return static_cast<LibHandle>(handle);

    if(auto *const err = dlerror())
        return al::unexpected(err);
    return al::unexpected("dlerror() == NULL");
}

void CloseLib(LibHandle const handle)
{ dlclose(handle); }

auto GetSymbol_(LibHandle const handle, al::zstring_view const name)
    -> al::expected<void*, std::string>
{
    if(auto *const sym = dlsym(handle, name.c_str()))
        return sym;

    if(auto *const err = dlerror())
        return al::unexpected(err);
    return al::unexpected("dlerror() == NULL");
}
#endif

#endif
