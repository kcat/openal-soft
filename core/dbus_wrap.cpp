
#include "config.h"

#include "dbus_wrap.h"

#if HAVE_DYNLOAD

#include "gsl/gsl"
#include "logging.h"

namespace {

#define DBUS_LIB "libdbus-1.so.3"

OAL_ELF_NOTE_DLOPEN(
    "core-dbus",
    "RTKit/D-Bus support",
    OAL_ELF_NOTE_DLOPEN_PRIORITY_SUGGESTED,
    DBUS_LIB
);

} /* namespace */

void PrepareDBus()
{
    auto *const dbus_lib = gsl::czstring{DBUS_LIB};
    if(auto const libresult = LoadLib(dbus_lib))
        dbus_handle = libresult.value();
    else
    {
        WARN("Failed to load {}: {}", dbus_lib, libresult.error());
        return;
    }

    static constexpr auto load_func = []<typename T>(T &func, gsl::czstring const name) -> bool
    {
        auto const funcresult = GetSymbol(dbus_handle, name);
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
