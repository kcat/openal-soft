
#include "config.h"

#include "helpers.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <cctype>
#ifdef _MSVC_STL_UPDATE && __has_include(<xfilesystem_abi.h>)
#include <xfilesystem_abi.h> // HACK: For MSVC?
#endif
#include <shlobj.h>

#include "almalloc.h"

#else

#include <cerrno>
#include <dirent.h>
#include <unistd.h>
#ifdef __FreeBSD__
#include <sys/sysctl.h>
#endif
#ifdef __HAIKU__
#include <FindDirectory.h>
#endif
#ifdef HAVE_PROC_PIDPATH
#include <libproc.h>
#endif
#if defined(HAVE_PTHREAD_SETSCHEDPARAM) && !defined(__OpenBSD__)
#include <pthread.h>
#include <sched.h>
#endif
#if HAVE_RTKIT
#include <sys/resource.h>

#include "rtkit.h"
#ifndef RLIMIT_RTTIME
#define RLIMIT_RTTIME 15
#endif
#endif
#endif

#include "alnumeric.h"
#include "alstring.h"
#include "strutils.hpp"

#if HAVE_CXXMODULES
import filesystem;
import logging;
#else
#include "filesystem.h"
#include "logging.h"
#endif


namespace {

using namespace std::string_view_literals;

auto gSearchLock = std::mutex{};

void DirectorySearch(const fs::path &path, const std::string_view ext,
    std::vector<std::string> *const results)
{
    const auto base = results->size();

    try {
        auto const fpath = path.lexically_normal();
        if(!fs::exists(fpath))
            return;

        TRACE("Searching {} for *{}", al::u8_as_char(fpath.u8string()), ext);
        for(auto&& dirent : fs::directory_iterator{fpath})
        {
            auto&& entrypath = dirent.path();
            if(!entrypath.has_extension())
                continue;

            if(fs::status(entrypath).type() != fs::file_type::regular)
                continue;
            const auto u8ext = entrypath.extension().u8string();
            if(is_eq(al::case_compare(al::u8_as_char(u8ext), ext)))
                results->emplace_back(al::u8_as_char(entrypath.u8string()));
        }
    }
    catch(std::exception& e) {
        ERR("Exception enumerating files: {}", e.what());
    }

    const auto newlist = std::span{*results}.subspan(base);
    std::ranges::sort(newlist);
    for(const auto &name : newlist)
        TRACE(" got {}", name);
}

} // namespace

#ifdef _WIN32
auto GetProcBinary() -> const PathNamePair&
{
    static const auto procbin = std::invoke([]() -> PathNamePair
    {
        auto res = PathNamePair{};

#if !ALSOFT_UWP
        auto pathlen = DWORD{256};
        auto fullpath = std::wstring(pathlen, L'\0');
        auto len = GetModuleFileNameW(nullptr, fullpath.data(), pathlen);
        while(len == fullpath.size())
        {
            pathlen <<= 1;
            if(pathlen == 0)
            {
                /* pathlen overflow (more than 4 billion characters??) */
                len = 0;
                break;
            }
            fullpath.resize(pathlen);
            len = GetModuleFileNameW(nullptr, fullpath.data(), pathlen);
        }
        if(len == 0)
        {
            ERR("Failed to get process name: error {}", GetLastError());
            return res;
        }

        fullpath.resize(len);
#else
        if(__argc < 1 || !__wargv)
        {
            ERR("Failed to get process name: __argc = {}, __wargv = {}", __argc,
                static_cast<void*>(__wargv));
            return res;
        }
        const auto *exePath = __wargv[0];
        if(!exePath)
        {
            ERR("Failed to get process name: __wargv[0] == nullptr");
            return res;
        }
        auto fullpath = std::wstring{exePath};
#endif
        std::ranges::replace(fullpath, L'/', L'\\');

        if(auto seppos = fullpath.rfind(L'\\'); seppos < fullpath.size())
        {
            res.path = wstr_to_utf8(std::wstring_view{fullpath}.substr(0, seppos));
            res.fname = wstr_to_utf8(std::wstring_view{fullpath}.substr(seppos+1));
        }
        else
            res.fname = wstr_to_utf8(fullpath);

        TRACE("Got binary: {}, {}", res.path, res.fname);
        return res;
    });
    return procbin;
}

namespace {

#if !ALSOFT_UWP && !defined(_GAMING_XBOX)
struct CoTaskMemDeleter {
    void operator()(void *mem) const { CoTaskMemFree(mem); }
};
#endif

} // namespace

auto SearchDataFiles(const std::string_view ext) -> std::vector<std::string>
{
    const auto srchlock = std::lock_guard{gSearchLock};

    /* Search the app-local directory. */
    auto results = std::vector<std::string>{};
    if(auto localpath = al::getenv(L"ALSOFT_LOCAL_PATH"))
        DirectorySearch(*localpath, ext, &results);
    else if(auto curpath = fs::current_path(); !curpath.empty())
        DirectorySearch(curpath, ext, &results);

    return results;
}

auto SearchDataFiles(const std::string_view ext, const std::string_view subdir)
    -> std::vector<std::string>
{
    const auto srchlock = std::lock_guard{gSearchLock};

    /* If the path is absolute, use it directly. */
    auto results = std::vector<std::string>{};
    const auto path = fs::path(al::char_as_u8(subdir));
    if(path.is_absolute())
    {
        DirectorySearch(path, ext, &results);
        return results;
    }

#if !ALSOFT_UWP && !defined(_GAMING_XBOX)
    /* Search the local and global data dirs. */
    for(const auto &folderid : std::array{FOLDERID_RoamingAppData, FOLDERID_ProgramData})
    {
        auto buffer = std::unique_ptr<WCHAR,CoTaskMemDeleter>{};
        const HRESULT hr{SHGetKnownFolderPath(folderid, KF_FLAG_DONT_UNEXPAND, nullptr,
            al::out_ptr(buffer))};
        if(FAILED(hr) || !buffer || !*buffer)
            continue;

        DirectorySearch(fs::path{buffer.get()}/path, ext, &results);
    }
#endif

    return results;
}

void SetRTPriority()
{
#if !ALSOFT_UWP
    if(RTPrioLevel > 0)
    {
        if(!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
            ERR("Failed to set priority level for thread");
    }
#endif
}

#else

auto GetProcBinary() -> const PathNamePair&
{
    static const auto procbin = std::invoke([]() -> PathNamePair
    {
        auto pathname = std::string{};
#ifdef __FreeBSD__
        auto pathlen = size_t{};
        auto mib = std::array<int,4>{{CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1}};
        if(sysctl(mib.data(), mib.size(), nullptr, &pathlen, nullptr, 0) == -1)
            WARN("Failed to sysctl kern.proc.pathname: {}",
                std::generic_category().message(errno));
        else
        {
            auto procpath = std::vector<char>(pathlen+1, '\0');
            sysctl(mib.data(), mib.size(), procpath.data(), &pathlen, nullptr, 0);
            pathname = procpath.data();
        }
#endif
#ifdef HAVE_PROC_PIDPATH
        if(pathname.empty())
        {
            auto procpath = std::array<char,PROC_PIDPATHINFO_MAXSIZE>{};
            const auto pid = getpid();
            if(proc_pidpath(pid, procpath.data(), procpath.size()) < 1)
                ERR("proc_pidpath({}, ...) failed: {}", pid,
                    std::generic_category().message(errno));
            else
                pathname = procpath.data();
        }
#endif
#ifdef __HAIKU__
        if(pathname.empty())
        {
            auto procpath = std::array<char,PATH_MAX>{};
            if(find_path(B_APP_IMAGE_SYMBOL, B_FIND_PATH_IMAGE_PATH, NULL, procpath.data(), procpath.size()) == B_OK)
                pathname = procpath.data();
        }
#endif
#ifndef __SWITCH__
        if(pathname.empty())
        {
            constexpr auto SelfLinkNames = std::array{
                "/proc/self/exe"sv,
                "/proc/self/file"sv,
                "/proc/curproc/exe"sv,
                "/proc/curproc/file"sv,
            };

            for(const std::string_view name : SelfLinkNames)
            {
                try {
                    if(!fs::exists(name))
                        continue;
                    if(auto path = fs::read_symlink(name); !path.empty())
                    {
                        pathname = al::u8_as_char(path.u8string());
                        break;
                    }
                }
                catch(std::exception& e) {
                    WARN("Exception getting symlink {}: {}", name, e.what());
                }
            }
        }
#endif

        auto res = PathNamePair{};
        if(const auto seppos = pathname.rfind('/'); seppos < pathname.size())
        {
            res.path = std::string_view{pathname}.substr(0, seppos);
            res.fname = std::string_view{pathname}.substr(seppos+1);
        }
        else
            res.fname = pathname;

        TRACE(R"(Got binary: "{}", "{}")", res.path, res.fname);
        return res;
    });
    return procbin;
}

auto SearchDataFiles(const std::string_view ext) -> std::vector<std::string>
{
    const auto srchlock = std::lock_guard{gSearchLock};

    /* Search the app-local directory. */
    auto results = std::vector<std::string>{};
    if(auto const localpath = al::getenv("ALSOFT_LOCAL_PATH"))
        DirectorySearch(*localpath, ext, &results);
    else if(auto const curpath = fs::current_path(); !curpath.empty())
        DirectorySearch(curpath, ext, &results);

    return results;
}

auto SearchDataFiles(const std::string_view ext, const std::string_view subdir)
    -> std::vector<std::string>
{
    const auto srchlock = std::lock_guard{gSearchLock};

    auto results = std::vector<std::string>{};
    auto path = fs::path(al::char_as_u8(subdir));
    if(path.is_absolute())
    {
        DirectorySearch(path, ext, &results);
        return results;
    }

    /* Search local data dir */
    if(auto const datapath = al::getenv("XDG_DATA_HOME"))
        DirectorySearch(fs::path{*datapath}/path, ext, &results);
    else if(auto const homepath = al::getenv("HOME"))
        DirectorySearch(fs::path{*homepath}/".local/share"/path, ext, &results);

    /* Search global data dirs */
    const auto datadirs = std::string{al::getenv("XDG_DATA_DIRS")
        .value_or("/usr/local/share/:/usr/share/")};

    auto curpos = std::size_t{0};
    while(curpos < datadirs.size())
    {
        auto nextpos = datadirs.find(':', curpos);

        const auto pathname{(nextpos != std::string::npos)
            ? std::string_view{datadirs}.substr(curpos, nextpos++ - curpos)
            : std::string_view{datadirs}.substr(curpos)};
        curpos = nextpos;

        if(!pathname.empty())
            DirectorySearch(fs::path{pathname}/path, ext, &results);
    }

#ifdef ALSOFT_INSTALL_DATADIR
    /* Search the installation data directory */
    if(auto instpath = fs::path{ALSOFT_INSTALL_DATADIR}; !instpath.empty())
        DirectorySearch(instpath/path, ext, &results);
#endif

    return results;
}

namespace {

bool SetRTPriorityPthread(int const prio [[maybe_unused]])
{
    auto err = ENOTSUP;
#if defined(HAVE_PTHREAD_SETSCHEDPARAM) && !defined(__OpenBSD__)
    /* Get the min and max priority for SCHED_RR. Limit the max priority to
     * half, for now, to ensure the thread can't take the highest priority and
     * go rogue.
     */
    const auto rtmin = sched_get_priority_min(SCHED_RR);
    auto rtmax = sched_get_priority_max(SCHED_RR);
    rtmax = (rtmax-rtmin)/2 + rtmin;

    auto param = sched_param{};
    param.sched_priority = std::clamp(prio, rtmin, rtmax);
#ifdef SCHED_RESET_ON_FORK
    err = pthread_setschedparam(pthread_self(), SCHED_RR|SCHED_RESET_ON_FORK, &param);
    if(err == EINVAL)
#endif
        err = pthread_setschedparam(pthread_self(), SCHED_RR, &param);
    if(err == 0) return true;
#endif
    WARN("pthread_setschedparam failed: {} ({})", std::generic_category().message(err), err);
    return false;
}

bool SetRTPriorityRTKit(int prio [[maybe_unused]])
{
#if HAVE_RTKIT
    auto const rtkit = RTKit::Create();
    if(not rtkit) return false;

    auto const nicemin = rtkit.get_min_nice_level();
    if(not nicemin.has_value())
    {
        auto const err = std::make_error_code(nicemin.error());
        ERR("Could not query RTKit: {} ({})", err.message(), err.value());
        return false;
    }
    auto rtmax = rtkit.get_max_realtime_priority();
    if(not rtmax.has_value())
    {
        auto const err = std::make_error_code(rtmax.error());
        ERR("Could not get max realtime priority: {} ({})", err.message(), err.value());
        return false;
    }
    TRACE("Maximum real-time priority: {}, minimum niceness: {}", *rtmax, *nicemin);

    if(*rtmax > 0)
    {
        if(AllowRTTimeLimit)
        {
            auto const res = rtkit.get_rttime_usec_max()
                .and_then([](long long const maxtime) -> rtkitret_t<void>
                {
                    auto rlim = rlimit{};
                    if(getrlimit(RLIMIT_RTTIME, &rlim) != 0)
                        return al::unexpected(std::errc{errno});

                    TRACE("RTTime max: {} (hard: {}, soft: {})", maxtime, rlim.rlim_max,
                        rlim.rlim_cur);
                    if(maxtime > 0 and std::cmp_greater(rlim.rlim_max, maxtime))
                    {
                        rlim.rlim_max = al::saturate_cast<rlim_t>(maxtime);
                        rlim.rlim_cur = std::min(rlim.rlim_cur, rlim.rlim_max);
                        if(setrlimit(RLIMIT_RTTIME, &rlim) != 0)
                            return al::unexpected(std::errc{errno});
                    }
                    return {};
                });
            if(not res.has_value())
            {
                auto const err = std::make_error_code(res.error());
                WARN("Failed to set RLIMIT_RTTIME for RTKit: {} ({})", err.message(), err.value());
            }
        }

        /* Limit the maximum real-time priority to half. */
        *rtmax = (*rtmax+1)/2;
        prio = std::clamp(prio, 1, *rtmax);

        TRACE("Making real-time with priority {} (max: {})", prio, *rtmax);
        auto const res = rtkit.make_realtime(0, prio);
        if(res.has_value()) return true;

        auto const err = std::make_error_code(res.error());
        WARN("Failed to set real-time priority: {} ({})", err.message(), err.value());
    }
    /* Don't try to set the niceness for non-Linux systems. Standard POSIX has
     * niceness as a per-process attribute, while the intent here is for the
     * audio processing thread only to get a priority boost. Currently only
     * Linux is known to have per-thread niceness.
     */
#ifdef __linux__
    if(*nicemin < 0)
    {
        TRACE("Making high priority with niceness {}", *nicemin);
        auto const res = rtkit.make_high_priority(0, *nicemin);
        if(res.has_value()) return true;

        auto const err = std::make_error_code(res.error());
        WARN("Failed to set high priority: {} ({})", err.message(), err.value());
    }
#endif /* __linux__ */

#else

    WARN("D-Bus not supported");
#endif
    return false;
}

} // namespace

void SetRTPriority()
{
    if(RTPrioLevel <= 0)
        return;

    if(!SetRTPriorityPthread(RTPrioLevel))
        SetRTPriorityRTKit(RTPrioLevel);
}

#endif
