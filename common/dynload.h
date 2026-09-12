#ifndef AL_DYNLOAD_H
#define AL_DYNLOAD_H

#include "config.h"

#if HAVE_DYNLOAD && (defined(_WIN32) || defined(HAVE_DLFCN_H))

#include <string>

#include "expected.hpp"
#include "zstring_view.hpp"

extern "C" struct LibHandleStruct;
using LibHandle = LibHandleStruct*;

[[nodiscard]]
auto LoadLib(al::zstring_view name) -> al::expected<LibHandle, std::string>;
void CloseLib(LibHandle handle);
[[nodiscard]]
auto GetSymbol_(LibHandle handle, al::zstring_view name) -> al::expected<void*, std::string>;

template<typename T> [[nodiscard]]
auto GetSymbolAddress(LibHandle const handle, al::zstring_view const name)
    -> al::expected<T*, std::string>
{
    return GetSymbol_(handle, name)
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
        .transform([](void *fn) { return reinterpret_cast<T*>(fn); });
}

#endif

#endif /* AL_DYNLOAD_H */
