#ifndef CORE_DBUS_WRAP_H
#define CORE_DBUS_WRAP_H

#include <memory>

#include <dbus/dbus.h>

#include "dynload.h"


#define DBUS_FUNCTIONS(MAGIC) \
MAGIC(dbus_error_init) \
MAGIC(dbus_error_free) \
MAGIC(dbus_bus_get) \
MAGIC(dbus_connection_set_exit_on_disconnect) \
MAGIC(dbus_connection_unref) \
MAGIC(dbus_connection_send_with_reply_and_block) \
MAGIC(dbus_message_unref) \
MAGIC(dbus_message_new_method_call) \
MAGIC(dbus_message_append_args) \
MAGIC(dbus_message_iter_init) \
MAGIC(dbus_message_iter_next) \
MAGIC(dbus_message_iter_recurse) \
MAGIC(dbus_message_iter_get_arg_type) \
MAGIC(dbus_message_iter_get_basic) \
MAGIC(dbus_set_error_from_message)

#ifdef HAVE_DYNLOAD

#include <mutex>

extern void *dbus_handle;
#define DECL_FUNC(x) extern decltype(x) *p##x;
DBUS_FUNCTIONS(DECL_FUNC)
#undef DECL_FUNC

void PrepareDBus();

inline auto HasDBus()
{
    static std::once_flag init_dbus{};
    std::call_once(init_dbus, PrepareDBus);
    return dbus_handle;
}

#else

#define DECL_FUNC(x) constexpr auto p##x = &x;
DBUS_FUNCTIONS(DECL_FUNC)
#undef DECL_FUNC

constexpr bool HasDBus() noexcept { return true; }
#endif /* HAVE_DYNLOAD */


namespace dbus {

struct Error {
    Error() { (*pdbus_error_init)(&mError); }
    ~Error() { (*pdbus_error_free)(&mError); }
    DBusError* operator->() { return &mError; }
    DBusError &get() { return mError; }
private:
    DBusError mError{};
};

struct ConnectionDeleter {
    void operator()(DBusConnection *c) { (*pdbus_connection_unref)(c); }
};
using ConnectionPtr = std::unique_ptr<DBusConnection,ConnectionDeleter>;

} // namespace dbus


#endif /* CORE_DBUS_WRAP_H */
