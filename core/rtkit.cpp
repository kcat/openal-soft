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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "config.h"

#include "rtkit.h"

#include <cerrno>
#include <cstring>
#include <dbus/dbus.h>
#include <memory>
#include <mutex>
#include <string_view>
#include <unistd.h>
#include <sys/types.h>
#ifdef __linux__
#include <sys/syscall.h>
#elif defined(__FreeBSD__)
#include <sys/thr.h>
#endif

#include "dlopennote.h"
#include "dynload.h"


#if HAVE_CXXMODULES
import gsl;
import logging;
import zstring_view;
#else
#include "alformatzsv.hpp"
#include "gsl/gsl"
#include "logging.h"
#endif

namespace {

#if HAVE_DYNLOAD

#define DBUS_FUNCTIONS(MAGIC) \
MAGIC(dbus_error_init); \
MAGIC(dbus_error_free); \
MAGIC(dbus_bus_get); \
MAGIC(dbus_connection_set_exit_on_disconnect); \
MAGIC(dbus_connection_unref); \
MAGIC(dbus_connection_send_with_reply_and_block); \
MAGIC(dbus_message_unref); \
MAGIC(dbus_message_new_method_call); \
MAGIC(dbus_message_append_args); \
MAGIC(dbus_message_iter_init); \
MAGIC(dbus_message_iter_next); \
MAGIC(dbus_message_iter_recurse); \
MAGIC(dbus_message_iter_get_arg_type); \
MAGIC(dbus_message_iter_get_basic); \
MAGIC(dbus_set_error_from_message);

auto dbus_handle = LibHandle{};
#define DECL_FUNC(f) decltype(f)* p##f{}
DBUS_FUNCTIONS(DECL_FUNC)
#undef DECL_FUNC

#ifndef IN_IDE_PARSER
#define dbus_error_init (*pdbus_error_init)
#define dbus_error_free (*pdbus_error_free)
#define dbus_bus_get (*pdbus_bus_get)
#define dbus_connection_set_exit_on_disconnect (*pdbus_connection_set_exit_on_disconnect)
#define dbus_connection_unref (*pdbus_connection_unref)
#define dbus_connection_send_with_reply_and_block (*pdbus_connection_send_with_reply_and_block)
#define dbus_message_unref (*pdbus_message_unref)
#define dbus_message_new_method_call (*pdbus_message_new_method_call)
#define dbus_message_append_args (*pdbus_message_append_args)
#define dbus_message_iter_init (*pdbus_message_iter_init)
#define dbus_message_iter_next (*pdbus_message_iter_next)
#define dbus_message_iter_recurse (*pdbus_message_iter_recurse)
#define dbus_message_iter_get_arg_type (*pdbus_message_iter_get_arg_type)
#define dbus_message_iter_get_basic (*pdbus_message_iter_get_basic)
#define dbus_set_error_from_message (*pdbus_set_error_from_message)
#endif

#define DBUS_LIB "libdbus-1.so.3"

OAL_ELF_NOTE_DLOPEN(
    "core-dbus",
    "RTKit/D-Bus support",
    OAL_ELF_NOTE_DLOPEN_PRIORITY_SUGGESTED,
    DBUS_LIB
);

auto HasDBus() -> bool
{
    static constinit auto init_dbus = std::once_flag{};
    std::call_once(init_dbus, []
    {
        auto constexpr dbus_lib = al::zstring_view{DBUS_LIB};
        if(auto const libresult = LoadLib(dbus_lib))
            dbus_handle = libresult.value();
        else
        {
            WARN("Failed to load {}: {}", dbus_lib, libresult.error());
            return;
        }

        static constexpr auto load_sym = []<typename T>(T *&func, al::zstring_view const name)
            -> bool
        {
            return GetSymbolAddress<T>(dbus_handle, name)
                .transform_error([name](std::string_view const err) {
                    WARN("Failed to load symbol {}: {}", name, err);
                    return false;
                })
                .transform([&func](T *addr) { func = addr; })
                .has_value();
        };
        auto ok = true;
#define LOAD_FUNC(f) ok &= load_sym(p##f, #f)
        DBUS_FUNCTIONS(LOAD_FUNC)
#undef LOAD_FUNC
        if(!ok)
        {
            CloseLib(dbus_handle);
            dbus_handle = nullptr;
        }
    });
    return dbus_handle != nullptr;
}

#else

constexpr auto HasDBus() noexcept -> bool { return true; }
#endif


class dbusError : public DBusError {
public:
    dbusError() : DBusError{} { dbus_error_init(this); }
    dbusError(dbusError const&) = delete;
    dbusError(dbusError&&) = delete;
    ~dbusError() { dbus_error_free(this); }

    void operator=(dbusError const&) = delete;
    void operator=(dbusError&&) = delete;
};

constexpr auto dbusTypeString = int{'s'};
constexpr auto dbusTypeVariant = int{'v'};
constexpr auto dbusTypeInt32 = int{'i'};
constexpr auto dbusTypeUInt32 = int{'u'};
constexpr auto dbusTypeInt64 = int{'x'};
constexpr auto dbusTypeUInt64 = int{'t'};
constexpr auto dbusTypeInvalid = int{'\0'};

using dbusMessagePtr = std::unique_ptr<DBusMessage, decltype([](DBusMessage *const m)
    { dbus_message_unref(m); })>;


#define RTKIT_SERVICE_NAME "org.freedesktop.RealtimeKit1"
#define RTKIT_OBJECT_PATH "/org/freedesktop/RealtimeKit1"

[[nodiscard]]
auto _gettid() -> pid_t
{
#ifdef __linux__
    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) */
    return gsl::narrow_cast<pid_t>(syscall(SYS_gettid));
#elif defined(__FreeBSD__)
    auto pid = long{};
    thr_self(&pid);
    return gsl::narrow_cast<pid_t>(pid);
#else
#warning gettid not available
    return 0;
#endif
}

[[nodiscard]]
auto translate_error(std::string_view const name) -> std::errc
{
    if(name == DBUS_ERROR_NO_MEMORY)
        return std::errc::not_enough_memory;
    if(name == DBUS_ERROR_SERVICE_UNKNOWN || name == DBUS_ERROR_NAME_HAS_NO_OWNER)
        return std::errc::no_such_file_or_directory;
    if(name == DBUS_ERROR_ACCESS_DENIED || name == DBUS_ERROR_AUTH_FAILED)
        return std::errc::permission_denied;
    return std::errc::io_error;
}

[[nodiscard]]
auto rtkit_get_int_property(DBusConnection *const connection, gsl::czstring const propname)
    -> rtkitret_t<long long>
{
    auto const m = dbusMessagePtr{dbus_message_new_method_call(RTKIT_SERVICE_NAME,
        RTKIT_OBJECT_PATH, "org.freedesktop.DBus.Properties", "Get")};
    if(!m) return al::unexpected(std::errc::not_enough_memory);

    auto *const interfacestr = gsl::czstring{RTKIT_SERVICE_NAME};
    auto const ready = dbus_message_append_args(std::to_address(m),
        dbusTypeString, &interfacestr,
        dbusTypeString, &propname,
        dbusTypeInvalid);
    if(!ready) return al::unexpected(std::errc::not_enough_memory);

    auto error = dbusError{};
    auto const r = dbusMessagePtr{dbus_connection_send_with_reply_and_block(connection,
        std::to_address(m), -1, &error)};
    if(!r) return al::unexpected(translate_error(error.name));

    if(dbus_set_error_from_message(&error, std::to_address(r)))
        return al::unexpected(translate_error(error.name));

    auto ret = rtkitret_t<long long>{al::unexpected(std::errc::bad_message)};
    auto iter = DBusMessageIter{};
    dbus_message_iter_init(std::to_address(r), &iter);
    while(auto curtype = dbus_message_iter_get_arg_type(&iter))
    {
        if(curtype == dbusTypeVariant)
        {
            auto subiter = DBusMessageIter{};
            dbus_message_iter_recurse(&iter, &subiter);

            curtype = dbus_message_iter_get_arg_type(&subiter);
            while(curtype != dbusTypeInvalid)
            {
                if(curtype == dbusTypeInt32)
                {
                    auto val32 = dbus_int32_t{};
                    dbus_message_iter_get_basic(&subiter, &val32);
                    ret.emplace(val32);
                }

                if(curtype == dbusTypeInt64)
                {
                    auto val64 = dbus_int64_t{};
                    dbus_message_iter_get_basic(&subiter, &val64);
                    ret.emplace(val64);
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

void RTKit::dbusConnectionDeleter::operator()(DBusConnection *const conn) const
{ dbus_connection_unref(conn); }

auto RTKit::Create() -> RTKit
{
    auto ret = RTKit{};

    if(!HasDBus())
    {
        WARN("D-Bus not available");
        return ret;
    }
    auto error = dbusError{};
    auto conn = dbusConnectionPtr{dbus_bus_get(DBUS_BUS_SYSTEM, &error)};
    if(!conn)
    {
        WARN("D-Bus connection failed with {}: {}", error.name, error.message);
        return ret;
    }

    /* Don't stupidly exit if the connection dies while doing this. */
    dbus_connection_set_exit_on_disconnect(std::to_address(conn), false);
    ret.mBus = std::move(conn);
    return ret;
}

auto RTKit::get_max_realtime_priority() const -> rtkitret_t<int>
{
    return rtkit_get_int_property(mBus.get(), "MaxRealtimePriority")
        .transform([](long long const val) -> int
            { return gsl::narrow_cast<int>(val); });
}

auto RTKit::get_min_nice_level() const -> rtkitret_t<int>
{
    return rtkit_get_int_property(mBus.get(), "MinNiceLevel")
        .transform([](long long const val) -> int
            { return gsl::narrow_cast<int>(val); });;
}

auto RTKit::get_rttime_usec_max() const -> rtkitret_t<long long>
{
    return rtkit_get_int_property(mBus.get(), "RTTimeUSecMax");
}

auto RTKit::make_realtime(pid_t thread, int const priority) const -> rtkitret_t<void>
{
    if(thread == 0)
        thread = _gettid();
    if(thread == 0)
        return al::unexpected(std::errc::not_supported);

    auto const m = dbusMessagePtr{dbus_message_new_method_call(RTKIT_SERVICE_NAME,
        RTKIT_OBJECT_PATH, "org.freedesktop.RealtimeKit1", "MakeThreadRealtime")};
    if(!m) return al::unexpected(std::errc::not_enough_memory);

    auto tid64 = gsl::narrow_cast<dbus_uint64_t>(thread);
    auto prio32 = gsl::narrow_cast<dbus_uint32_t>(priority);
    auto const ready = dbus_message_append_args(std::to_address(m),
        dbusTypeUInt64, &tid64,
        dbusTypeUInt32, &prio32,
        dbusTypeInvalid);
    if(!ready) return al::unexpected(std::errc::not_enough_memory);

    auto error = dbusError{};
    auto const r = dbusMessagePtr{dbus_connection_send_with_reply_and_block(mBus.get(),
        std::to_address(m), -1, &error)};
    if(!r) return al::unexpected(translate_error(error.name));

    if(dbus_set_error_from_message(&error, std::to_address(r)))
        return al::unexpected(translate_error(error.name));

    return {};
}

auto RTKit::make_high_priority(pid_t thread, int const nice_level) const -> rtkitret_t<void>
{
    if(thread == 0)
        thread = _gettid();
    if(thread == 0)
        return al::unexpected(std::errc::not_supported);

    auto const m = dbusMessagePtr{dbus_message_new_method_call(RTKIT_SERVICE_NAME,
        RTKIT_OBJECT_PATH, "org.freedesktop.RealtimeKit1", "MakeThreadHighPriority")};
    if(!m) return al::unexpected(std::errc::not_enough_memory);

    auto tid64 = gsl::narrow_cast<dbus_uint64_t>(thread);
    auto level32 = gsl::narrow_cast<dbus_int32_t>(nice_level);
    auto const ready = dbus_message_append_args(std::to_address(m),
        dbusTypeUInt64, &tid64,
        dbusTypeInt32, &level32,
        dbusTypeInvalid);
    if(!ready) return al::unexpected(std::errc::not_enough_memory);

    auto error = dbusError{};
    auto const r = dbusMessagePtr{dbus_connection_send_with_reply_and_block(mBus.get(),
        std::to_address(m), -1, &error)};
    if(!r) return al::unexpected(translate_error(error.name));

    if(dbus_set_error_from_message(&error, std::to_address(r)))
        return al::unexpected(translate_error(error.name));

    return {};
}
