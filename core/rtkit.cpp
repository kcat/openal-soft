/*-*- Mode: C; c-basic-offset: 8 -*-*/

/***
        Copyright 2009 Lennart Poettering
        Copyright 2010 David Henningsson <diwic@ubuntu.com>
        Copyright 2021 Chris Robinson

        Permission is hereby granted, free of charge, to any person
        obtaining a copy of this software and associated documentation files
        (the "Software"), to deal in the Software without restriction,
        including without limitation the rights to use, copy, modify, merge,
        publish, distribute, sublicense, and/or sell copies of the Software,
        and to permit persons to whom the Software is furnished to do so,
        subject to the following conditions:

        The above copyright notice and this permission notice shall be
        included in all copies or substantial portions of the Software.

        THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
        EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
        MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
        NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
        BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
        ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
        CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
        SOFTWARE.
***/

#include "config.h"

#include "rtkit.h"

#include <cerrno>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <memory>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#ifdef __linux__
#include <sys/syscall.h>
#elif defined(__FreeBSD__)
#include <sys/thr.h>
#endif


namespace dbus {

constexpr auto TypeString = int{'s'};
constexpr auto TypeVariant = int{'v'};
constexpr auto TypeInt32 = int{'i'};
constexpr auto TypeUInt32 = int{'u'};
constexpr auto TypeInt64 = int{'x'};
constexpr auto TypeUInt64 = int{'t'};
constexpr auto TypeInvalid = int{'\0'};

using MessagePtr = std::unique_ptr<DBusMessage, decltype([](DBusMessage *m)
    { dbus_message_unref(m); })>;

} // namespace dbus

namespace {

inline auto _gettid() -> pid_t
{
#ifdef __linux__
    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) */
    return static_cast<pid_t>(syscall(SYS_gettid));
#elif defined(__FreeBSD__)
    auto pid = long{};
    thr_self(&pid);
    return static_cast<pid_t>(pid);
#else
#warning gettid not available
    return 0;
#endif
}

int translate_error(const char *name)
{
    if(strcmp(name, DBUS_ERROR_NO_MEMORY) == 0)
        return -ENOMEM;
    if(strcmp(name, DBUS_ERROR_SERVICE_UNKNOWN) == 0
        || strcmp(name, DBUS_ERROR_NAME_HAS_NO_OWNER) == 0)
        return -ENOENT;
    if(strcmp(name, DBUS_ERROR_ACCESS_DENIED) == 0
        || strcmp(name, DBUS_ERROR_AUTH_FAILED) == 0)
        return -EACCES;
    return -EIO;
}

int rtkit_get_int_property(DBusConnection *connection, const char *propname, long long *propval)
{
    const auto m = dbus::MessagePtr{dbus_message_new_method_call(RTKIT_SERVICE_NAME,
        RTKIT_OBJECT_PATH, "org.freedesktop.DBus.Properties", "Get")};
    if(!m) return -ENOMEM;

    auto *interfacestr = RTKIT_SERVICE_NAME;
    const auto ready = dbus_message_append_args(m.get(),
        dbus::TypeString, &interfacestr,
        dbus::TypeString, &propname,
        dbus::TypeInvalid);
    if(!ready) return -ENOMEM;

    auto error = dbus::Error{};
    const auto r = dbus::MessagePtr{dbus_connection_send_with_reply_and_block(connection, m.get(),
        -1, &error.get())};
    if(!r) return translate_error(error->name);

    if(dbus_set_error_from_message(&error.get(), r.get()))
        return translate_error(error->name);

    auto ret = -EBADMSG;
    auto iter = DBusMessageIter{};
    dbus_message_iter_init(r.get(), &iter);
    while(auto curtype = dbus_message_iter_get_arg_type(&iter))
    {
        if(curtype == dbus::TypeVariant)
        {
            auto subiter = DBusMessageIter{};
            dbus_message_iter_recurse(&iter, &subiter);

            curtype = dbus_message_iter_get_arg_type(&subiter);
            while(curtype != dbus::TypeInvalid)
            {
                if(curtype == dbus::TypeInt32)
                {
                    auto i32 = dbus_int32_t{};
                    dbus_message_iter_get_basic(&subiter, &i32);
                    *propval = i32;
                    ret = 0;
                }

                if(curtype == dbus::TypeInt64)
                {
                    auto i64 = dbus_int64_t{};
                    dbus_message_iter_get_basic(&subiter, &i64);
                    *propval = i64;
                    ret = 0;
                }

                dbus_message_iter_next(&subiter);
                curtype = dbus_message_iter_get_arg_type(&subiter);
            }
        }
        dbus_message_iter_next(&iter);
    }

    return ret;
}

} // namespace

int rtkit_get_max_realtime_priority(DBusConnection *system_bus)
{
    long long retval{};
    const auto err = rtkit_get_int_property(system_bus, "MaxRealtimePriority", &retval);
    return err < 0 ? err : static_cast<int>(retval);
}

int rtkit_get_min_nice_level(DBusConnection *system_bus, int *min_nice_level)
{
    long long retval{};
    const auto err = rtkit_get_int_property(system_bus, "MinNiceLevel", &retval);
    if(err >= 0) *min_nice_level = static_cast<int>(retval);
    return err;
}

long long rtkit_get_rttime_usec_max(DBusConnection *system_bus)
{
    long long retval{};
    const auto err = rtkit_get_int_property(system_bus, "RTTimeUSecMax", &retval);
    return err < 0 ? err : retval;
}

int rtkit_make_realtime(DBusConnection *system_bus, pid_t thread, int priority)
{
    if(thread == 0)
        thread = _gettid();
    if(thread == 0)
        return -ENOTSUP;

    const auto m = dbus::MessagePtr{dbus_message_new_method_call(RTKIT_SERVICE_NAME,
        RTKIT_OBJECT_PATH, "org.freedesktop.RealtimeKit1", "MakeThreadRealtime")};
    if(!m) return -ENOMEM;

    auto u64 = static_cast<dbus_uint64_t>(thread);
    auto u32 = static_cast<dbus_uint32_t>(priority);
    const auto ready = dbus_message_append_args(m.get(),
        dbus::TypeUInt64, &u64,
        dbus::TypeUInt32, &u32,
        dbus::TypeInvalid);
    if(!ready) return -ENOMEM;

    auto error = dbus::Error{};
    const auto r = dbus::MessagePtr{dbus_connection_send_with_reply_and_block(system_bus, m.get(),
        -1, &error.get())};
    if(!r) return translate_error(error->name);

    if(dbus_set_error_from_message(&error.get(), r.get()))
        return translate_error(error->name);

    return 0;
}

int rtkit_make_high_priority(DBusConnection *system_bus, pid_t thread, int nice_level)
{
    if(thread == 0)
        thread = _gettid();
    if(thread == 0)
        return -ENOTSUP;

    const auto m = dbus::MessagePtr{dbus_message_new_method_call(RTKIT_SERVICE_NAME,
        RTKIT_OBJECT_PATH, "org.freedesktop.RealtimeKit1", "MakeThreadHighPriority")};
    if(!m) return -ENOMEM;

    auto u64 = static_cast<dbus_uint64_t>(thread);
    auto s32 = static_cast<dbus_int32_t>(nice_level);
    const auto ready = dbus_message_append_args(m.get(),
        dbus::TypeUInt64, &u64,
        dbus::TypeInt32, &s32,
        dbus::TypeInvalid);
    if(!ready) return -ENOMEM;

    auto error = dbus::Error{};
    const auto r = dbus::MessagePtr{dbus_connection_send_with_reply_and_block(system_bus, m.get(),
        -1, &error.get())};
    if(!r) return translate_error(error->name);

    if(dbus_set_error_from_message(&error.get(), r.get()))
        return translate_error(error->name);

    return 0;
}
