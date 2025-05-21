
#include "config.h"

#include "dynload.h"

#ifdef _WIN32
#include <windows.h>

#include "strutils.h"

void *LoadLib(const char *name)
{
    auto wname = utf8_to_wstr(name);
    return LoadLibraryW(wname.c_str());
}
void CloseLib(void *handle)
{ FreeLibrary(static_cast<HMODULE>(handle)); }
void *GetSymbol(void *handle, const char *name)
{
    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
}

#elif defined(HAVE_DLFCN_H)

#include <dlfcn.h>

void *LoadLib(const char *name)
{
    dlerror();
    auto *handle = dlopen(name, RTLD_NOW);
    if(auto *err = dlerror(); err != nullptr)
        handle = nullptr;
    return handle;
}
void CloseLib(void *handle)
{ dlclose(handle); }
void *GetSymbol(void *handle, const char *name)
{
    dlerror();
    auto *sym = dlsym(handle, name);
    if(auto *err = dlerror(); err != nullptr)
        sym = nullptr;
    return sym;
}
#endif
