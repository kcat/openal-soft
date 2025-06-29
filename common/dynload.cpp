
#include "config.h"

#include "dynload.h"

#ifdef _WIN32
#include <windows.h>

#include "fmt/core.h"
#include "gsl/gsl"
#include "strutils.hpp"

auto LoadLib(const gsl::czstring name) -> al::expected<void*, std::string>
{
    if(auto res = LoadLibraryW(utf8_to_wstr(name).c_str()))
        return res;
    const auto err = GetLastError();
    auto message = std::wstring{};
    message.resize(1024u);
    const auto res = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), message.data(),
        gsl::narrow_cast<DWORD>(message.size()), nullptr);
    if(res > 0)
    {
        message.resize(res);
        return al::unexpected(wstr_to_utf8(message));
    }
    return al::unexpected(fmt::format("LoadLibraryW error: {}", err));
}

void CloseLib(void *handle)
{ FreeLibrary(static_cast<HMODULE>(handle)); }

auto GetSymbol(void *handle, const gsl::czstring name) -> al::expected<void*, std::string>
{
    if(auto sym = GetProcAddress(static_cast<HMODULE>(handle), name))
    {
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
        return reinterpret_cast<void*>(sym);
    }
    const auto err = GetLastError();
    auto message = std::wstring{};
    message.resize(1024u);
    const auto res = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), message.data(),
        gsl::narrow_cast<DWORD>(message.size()), nullptr);
    if(res > 0)
    {
        message.resize(res);
        return al::unexpected(wstr_to_utf8(message));
    }
    return al::unexpected(fmt::format("GetProcAddress error: {}", err));
}

#elif defined(HAVE_DLFCN_H)

#include <dlfcn.h>

auto LoadLib(const gsl::czstring name) -> al::expected<void*, std::string>
{
    dlerror();
    auto *handle = dlopen(name, RTLD_NOW);
    if(auto *err = dlerror(); err != nullptr)
    {
        handle = nullptr;
        return al::unexpected(err);
    }
    return handle;
}

void CloseLib(void *handle)
{ dlclose(handle); }

auto GetSymbol(void *handle, const gsl::czstring name) -> al::expected<void*, std::string>
{
    dlerror();
    auto *sym = dlsym(handle, name);
    if(auto *err = dlerror(); err != nullptr)
    {
        sym = nullptr;
        return al::unexpected(err);
    }
    return sym;
}
#endif
