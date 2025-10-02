
#include "config.h"

#include "dbus_wrap.h"

#if HAVE_DYNLOAD

#include <type_traits>

#include "gsl/gsl"
#include "logging.h"


void PrepareDBus()
{
    auto *libname = "libdbus-1.so.3";

    if(auto libresult = LoadLib(libname))
        dbus_handle = libresult.value();
    else
    {
        WARN("Failed to load {}: {}", libname, libresult.error());
        return;
    }

    static constexpr auto load_func = []<typename T>(T &func, const gsl::czstring name) -> bool
    {
        auto funcresult = GetSymbol(dbus_handle, name);
        if(!funcresult)
        {
            WARN("Failed to load function {}: {}", name, funcresult.error());
            return false;
        }
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
        func = reinterpret_cast<T>(funcresult.value());
        return true;
    };
    auto ok = true;
#define LOAD_FUNC(f) ok &= load_func(p##f, #f);
    DBUS_FUNCTIONS(LOAD_FUNC)
#undef LOAD_FUNC
    if(!ok)
    {
        CloseLib(dbus_handle);
        dbus_handle = nullptr;
    }
}
#endif
