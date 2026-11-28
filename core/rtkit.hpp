#ifndef foortkithfoo
#define foortkithfoo

/***
        Copyright 2009 Lennart Poettering
        Copyright 2010 David Henningsson <diwic@ubuntu.com>

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

#include <memory>
#include <system_error>
#include <sys/types.h>

#include "expected.hpp"

struct DBusConnection;

/* Return type used by the RTKit methods. It's an expected<> holding the given
 * type on success, or a std::errc code on failure.
 */
template<typename T>
using rtkitret_t = al::expected<T, std::errc>;

class RTKit {
    struct dbusConnectionDeleter {
        void operator()(DBusConnection *conn) const;
    };
    using dbusConnectionPtr = std::unique_ptr<DBusConnection, dbusConnectionDeleter>;

    dbusConnectionPtr mBus;

public:
    [[nodiscard]]
    static auto Create() -> RTKit;

    [[nodiscard]] explicit
    operator bool() const noexcept { return mBus != nullptr; }

    /* This is mostly equivalent to sched_setparam(thread, SCHED_RR, {
     * .sched_priority = priority }). 'thread' needs to be a kernel thread
     * id as returned by gettid(), not a pthread_t! If 'thread' is 0 the
     * current thread is used.
     */
    [[nodiscard]]
    auto make_realtime(pid_t thread, int priority) const -> rtkitret_t<void>;

    /* This is mostly equivalent to setpriority(PRIO_PROCESS, thread,
     * nice_level). 'thread' needs to be a kernel thread id as returned by
     * gettid(), not a pthread_t! If 'thread' is 0 the current thread is
     * used.
     */
    [[nodiscard]]
    auto make_high_priority(pid_t thread, int nice_level) const -> rtkitret_t<void>;

    /* Return the maximum value of realtime priority available. Realtime requests
     * above this value will fail.
     */
    [[nodiscard]]
    auto get_max_realtime_priority() const -> rtkitret_t<int>;

    /* Retreive the minimum value of nice level available. High prio requests
     * below this value will fail.
     */
    [[nodiscard]]
    auto get_min_nice_level() const -> rtkitret_t<int>;

    /* Return the maximum value of RLIMIT_RTTIME to set before attempting a
     * realtime request.
     */
    [[nodiscard]]
    auto get_rttime_usec_max() const -> rtkitret_t<long long>;
};

#endif
