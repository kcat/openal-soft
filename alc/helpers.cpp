/**
 * OpenAL cross platform audio library
 * Copyright (C) 2011 by authors.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#ifdef _WIN32
#ifdef __MINGW32__
#define _WIN32_IE 0x501
#else
#define _WIN32_IE 0x400
#endif
#endif

#include "config.h"

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#ifdef HAVE_INTRIN_H
#include <intrin.h>
#endif
#ifdef HAVE_CPUID_H
#include <cpuid.h>
#endif
#ifdef HAVE_SSE_INTRINSICS
#include <xmmintrin.h>
#endif
#ifdef HAVE_SYS_SYSCONF_H
#include <sys/sysconf.h>
#endif

#ifdef HAVE_PROC_PIDPATH
#include <libproc.h>
#endif

#ifdef __FreeBSD__
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#elif defined(_WIN32_IE)
#include <shlobj.h>
#endif

#include "alcmain.h"
#include "almalloc.h"
#include "alfstream.h"
#include "alspan.h"
#include "alstring.h"
#include "compat.h"
#include "cpu_caps.h"
#include "fpu_modes.h"
#include "logging.h"
#include "strutils.h"
#include "vector.h"


#if defined(HAVE_GCC_GET_CPUID) && (defined(__i386__) || defined(__x86_64__) || \
                                    defined(_M_IX86) || defined(_M_X64))
using reg_type = unsigned int;
static inline void get_cpuid(unsigned int f, reg_type *regs)
{ __get_cpuid(f, &regs[0], &regs[1], &regs[2], &regs[3]); }
#define CAN_GET_CPUID
#elif defined(HAVE_CPUID_INTRINSIC) && (defined(__i386__) || defined(__x86_64__) || \
                                        defined(_M_IX86) || defined(_M_X64))
using reg_type = int;
static inline void get_cpuid(unsigned int f, reg_type *regs)
{ (__cpuid)(regs, f); }
#define CAN_GET_CPUID
#endif

int CPUCapFlags = 0;

void FillCPUCaps(int capfilter)
{
    int caps = 0;

/* FIXME: We really should get this for all available CPUs in case different
 * CPUs have different caps (is that possible on one machine?). */
#ifdef CAN_GET_CPUID
    union {
        reg_type regs[4];
        char str[sizeof(reg_type[4])];
    } cpuinf[3]{};

    get_cpuid(0, cpuinf[0].regs);
    if(cpuinf[0].regs[0] == 0)
        ERR("Failed to get CPUID\n");
    else
    {
        unsigned int maxfunc = cpuinf[0].regs[0];
        unsigned int maxextfunc;

        get_cpuid(0x80000000, cpuinf[0].regs);
        maxextfunc = cpuinf[0].regs[0];

        TRACE("Detected max CPUID function: 0x%x (ext. 0x%x)\n", maxfunc, maxextfunc);

        TRACE("Vendor ID: \"%.4s%.4s%.4s\"\n", cpuinf[0].str+4, cpuinf[0].str+12, cpuinf[0].str+8);
        if(maxextfunc >= 0x80000004)
        {
            get_cpuid(0x80000002, cpuinf[0].regs);
            get_cpuid(0x80000003, cpuinf[1].regs);
            get_cpuid(0x80000004, cpuinf[2].regs);
            TRACE("Name: \"%.16s%.16s%.16s\"\n", cpuinf[0].str, cpuinf[1].str, cpuinf[2].str);
        }

        if(maxfunc >= 1)
        {
            get_cpuid(1, cpuinf[0].regs);
            if((cpuinf[0].regs[3]&(1<<25)))
                caps |= CPU_CAP_SSE;
            if((caps&CPU_CAP_SSE) && (cpuinf[0].regs[3]&(1<<26)))
                caps |= CPU_CAP_SSE2;
            if((caps&CPU_CAP_SSE2) && (cpuinf[0].regs[2]&(1<<0)))
                caps |= CPU_CAP_SSE3;
            if((caps&CPU_CAP_SSE3) && (cpuinf[0].regs[2]&(1<<19)))
                caps |= CPU_CAP_SSE4_1;
        }
    }
#else
    /* Assume support for whatever's supported if we can't check for it */
#if defined(HAVE_SSE4_1)
#warning "Assuming SSE 4.1 run-time support!"
    caps |= CPU_CAP_SSE | CPU_CAP_SSE2 | CPU_CAP_SSE3 | CPU_CAP_SSE4_1;
#elif defined(HAVE_SSE3)
#warning "Assuming SSE 3 run-time support!"
    caps |= CPU_CAP_SSE | CPU_CAP_SSE2 | CPU_CAP_SSE3;
#elif defined(HAVE_SSE2)
#warning "Assuming SSE 2 run-time support!"
    caps |= CPU_CAP_SSE | CPU_CAP_SSE2;
#elif defined(HAVE_SSE)
#warning "Assuming SSE run-time support!"
    caps |= CPU_CAP_SSE;
#endif
#endif
#ifdef HAVE_NEON
    al::ifstream file{"/proc/cpuinfo"};
    if(!file.is_open())
        ERR("Failed to open /proc/cpuinfo, cannot check for NEON support\n");
    else
    {
        std::string features;

        auto getline = [](std::istream &f, std::string &output) -> bool
        {
            while(f.good() && f.peek() == '\n')
                f.ignore();
            return std::getline(f, output) && !output.empty();

        };
        while(getline(file, features))
        {
            if(features.compare(0, 10, "Features\t:", 10) == 0)
                break;
        }
        file.close();

        size_t extpos{9};
        while((extpos=features.find("neon", extpos+1)) != std::string::npos)
        {
            if((extpos == 0 || std::isspace(features[extpos-1])) &&
                (extpos+4 == features.length() || std::isspace(features[extpos+4])))
            {
                caps |= CPU_CAP_NEON;
                break;
            }
        }
    }
#endif

    TRACE("Extensions:%s%s%s%s%s%s\n",
        ((capfilter&CPU_CAP_SSE)    ? ((caps&CPU_CAP_SSE)    ? " +SSE"    : " -SSE")    : ""),
        ((capfilter&CPU_CAP_SSE2)   ? ((caps&CPU_CAP_SSE2)   ? " +SSE2"   : " -SSE2")   : ""),
        ((capfilter&CPU_CAP_SSE3)   ? ((caps&CPU_CAP_SSE3)   ? " +SSE3"   : " -SSE3")   : ""),
        ((capfilter&CPU_CAP_SSE4_1) ? ((caps&CPU_CAP_SSE4_1) ? " +SSE4.1" : " -SSE4.1") : ""),
        ((capfilter&CPU_CAP_NEON)   ? ((caps&CPU_CAP_NEON)   ? " +NEON"   : " -NEON")   : ""),
        ((!capfilter) ? " -none-" : "")
    );
    CPUCapFlags = caps & capfilter;
}


FPUCtl::FPUCtl()
{
#if defined(HAVE_SSE_INTRINSICS)
    this->sse_state = _mm_getcsr();
    unsigned int sseState = this->sse_state;
    sseState |= 0x8000; /* set flush-to-zero */
    sseState |= 0x0040; /* set denormals-are-zero */
    _mm_setcsr(sseState);

#elif defined(__GNUC__) && defined(HAVE_SSE)

    if((CPUCapFlags&CPU_CAP_SSE))
    {
        __asm__ __volatile__("stmxcsr %0" : "=m" (*&this->sse_state));
        unsigned int sseState = this->sse_state;
        sseState |= 0x8000; /* set flush-to-zero */
        if((CPUCapFlags&CPU_CAP_SSE2))
            sseState |= 0x0040; /* set denormals-are-zero */
        __asm__ __volatile__("ldmxcsr %0" : : "m" (*&sseState));
    }
#endif

    this->in_mode = true;
}

void FPUCtl::leave()
{
    if(!this->in_mode) return;

#if defined(HAVE_SSE_INTRINSICS)
    _mm_setcsr(this->sse_state);

#elif defined(__GNUC__) && defined(HAVE_SSE)

    if((CPUCapFlags&CPU_CAP_SSE))
        __asm__ __volatile__("ldmxcsr %0" : : "m" (*&this->sse_state));
#endif
    this->in_mode = false;
}


#ifdef _WIN32

const PathNamePair &GetProcBinary()
{
    static PathNamePair ret;
    if(!ret.fname.empty() || !ret.path.empty())
        return ret;

    al::vector<WCHAR> fullpath(256);
    DWORD len;
    while((len=GetModuleFileNameW(nullptr, fullpath.data(), static_cast<DWORD>(fullpath.size()))) == fullpath.size())
        fullpath.resize(fullpath.size() << 1);
    if(len == 0)
    {
        ERR("Failed to get process name: error %lu\n", GetLastError());
        return ret;
    }

    fullpath.resize(len);
    if(fullpath.back() != 0)
        fullpath.push_back(0);

    auto sep = std::find(fullpath.rbegin()+1, fullpath.rend(), '\\');
    sep = std::find(fullpath.rbegin()+1, sep, '/');
    if(sep != fullpath.rend())
    {
        *sep = 0;
        ret.fname = wstr_to_utf8(&*sep + 1);
        ret.path = wstr_to_utf8(fullpath.data());
    }
    else
        ret.fname = wstr_to_utf8(fullpath.data());

    TRACE("Got binary: %s, %s\n", ret.path.c_str(), ret.fname.c_str());
    return ret;
}


void al_print(FILE *logfile, const char *fmt, ...)
{
    al::vector<char> dynmsg;
    char stcmsg[256];
    char *str{stcmsg};

    va_list args, args2;
    va_start(args, fmt);
    va_copy(args2, args);
    int msglen{std::vsnprintf(str, sizeof(stcmsg), fmt, args)};
    if UNLIKELY(msglen >= 0 && static_cast<size_t>(msglen) >= sizeof(stcmsg))
    {
        dynmsg.resize(static_cast<size_t>(msglen) + 1u);
        str = dynmsg.data();
        msglen = std::vsnprintf(str, dynmsg.size(), fmt, args2);
    }
    va_end(args2);
    va_end(args);

    std::wstring wstr{utf8_to_wstr(str)};
    fputws(wstr.c_str(), logfile);
    fflush(logfile);
}


static inline int is_slash(int c)
{ return (c == '\\' || c == '/'); }

static void DirectorySearch(const char *path, const char *ext, al::vector<std::string> *const results)
{
    std::string pathstr{path};
    pathstr += "\\*";
    pathstr += ext;
    TRACE("Searching %s\n", pathstr.c_str());

    std::wstring wpath{utf8_to_wstr(pathstr.c_str())};
    WIN32_FIND_DATAW fdata;
    HANDLE hdl{FindFirstFileW(wpath.c_str(), &fdata)};
    if(hdl == INVALID_HANDLE_VALUE) return;

    const auto base = results->size();

    do {
        results->emplace_back();
        std::string &str = results->back();
        str = path;
        str += '\\';
        str += wstr_to_utf8(fdata.cFileName);
    } while(FindNextFileW(hdl, &fdata));
    FindClose(hdl);

    const al::span<std::string> newlist{results->data()+base, results->size()-base};
    std::sort(newlist.begin(), newlist.end());
    for(const auto &name : newlist)
        TRACE(" got %s\n", name.c_str());
}

al::vector<std::string> SearchDataFiles(const char *ext, const char *subdir)
{
    static std::mutex search_lock;
    std::lock_guard<std::mutex> _{search_lock};

    /* If the path is absolute, use it directly. */
    al::vector<std::string> results;
    if(isalpha(subdir[0]) && subdir[1] == ':' && is_slash(subdir[2]))
    {
        std::string path{subdir};
        std::replace(path.begin(), path.end(), '/', '\\');
        DirectorySearch(path.c_str(), ext, &results);
        return results;
    }
    if(subdir[0] == '\\' && subdir[1] == '\\' && subdir[2] == '?' && subdir[3] == '\\')
    {
        DirectorySearch(subdir, ext, &results);
        return results;
    }

    std::string path;

    /* Search the app-local directory. */
    if(auto localpath = al::getenv(L"ALSOFT_LOCAL_PATH"))
    {
        path = wstr_to_utf8(localpath->c_str());
        if(is_slash(path.back()))
            path.pop_back();
    }
    else if(WCHAR *cwdbuf{_wgetcwd(nullptr, 0)})
    {
        path = wstr_to_utf8(cwdbuf);
        if(is_slash(path.back()))
            path.pop_back();
        free(cwdbuf);
    }
    else
        path = ".";
    std::replace(path.begin(), path.end(), '/', '\\');
    DirectorySearch(path.c_str(), ext, &results);

    /* Search the local and global data dirs. */
    static const int ids[2]{ CSIDL_APPDATA, CSIDL_COMMON_APPDATA };
    for(int id : ids)
    {
        WCHAR buffer[MAX_PATH];
        if(SHGetSpecialFolderPathW(nullptr, buffer, id, FALSE) == FALSE)
            continue;

        path = wstr_to_utf8(buffer);
        if(!is_slash(path.back()))
            path += '\\';
        path += subdir;
        std::replace(path.begin(), path.end(), '/', '\\');

        DirectorySearch(path.c_str(), ext, &results);
    }

    return results;
}

void SetRTPriority(void)
{
    bool failed = false;
    if(RTPrioLevel > 0)
        failed = !SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    if(failed) ERR("Failed to set priority level for thread\n");
}

#else

#if defined(HAVE_PTHREAD_SETSCHEDPARAM) && !defined(__OpenBSD__)
#include <pthread.h>
#include <sched.h>
#endif

const PathNamePair &GetProcBinary()
{
    static PathNamePair ret;
    if(!ret.fname.empty() || !ret.path.empty())
        return ret;

    al::vector<char> pathname;
#ifdef __FreeBSD__
    size_t pathlen;
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
    if(sysctl(mib, 4, nullptr, &pathlen, nullptr, 0) == -1)
        WARN("Failed to sysctl kern.proc.pathname: %s\n", strerror(errno));
    else
    {
        pathname.resize(pathlen + 1);
        sysctl(mib, 4, pathname.data(), &pathlen, nullptr, 0);
        pathname.resize(pathlen);
    }
#endif
#ifdef HAVE_PROC_PIDPATH
    if(pathname.empty())
    {
        char procpath[PROC_PIDPATHINFO_MAXSIZE]{};
        const pid_t pid{getpid()};
        if(proc_pidpath(pid, procpath, sizeof(procpath)) < 1)
            ERR("proc_pidpath(%d, ...) failed: %s\n", pid, strerror(errno));
        else
            pathname.insert(pathname.end(), procpath, procpath+strlen(procpath));
    }
#endif
    if(pathname.empty())
    {
        pathname.resize(256);

        const char *selfname{"/proc/self/exe"};
        ssize_t len{readlink(selfname, pathname.data(), pathname.size())};
        if(len == -1 && errno == ENOENT)
        {
            selfname = "/proc/self/file";
            len = readlink(selfname, pathname.data(), pathname.size());
        }
        if(len == -1 && errno == ENOENT)
        {
            selfname = "/proc/curproc/exe";
            len = readlink(selfname, pathname.data(), pathname.size());
        }
        if(len == -1 && errno == ENOENT)
        {
            selfname = "/proc/curproc/file";
            len = readlink(selfname, pathname.data(), pathname.size());
        }

        while(len > 0 && static_cast<size_t>(len) == pathname.size())
        {
            pathname.resize(pathname.size() << 1);
            len = readlink(selfname, pathname.data(), pathname.size());
        }
        if(len <= 0)
        {
            WARN("Failed to readlink %s: %s\n", selfname, strerror(errno));
            return ret;
        }

        pathname.resize(static_cast<size_t>(len));
    }
    while(!pathname.empty() && pathname.back() == 0)
        pathname.pop_back();

    auto sep = std::find(pathname.crbegin(), pathname.crend(), '/');
    if(sep != pathname.crend())
    {
        ret.path = std::string(pathname.cbegin(), sep.base()-1);
        ret.fname = std::string(sep.base(), pathname.cend());
    }
    else
        ret.fname = std::string(pathname.cbegin(), pathname.cend());

    TRACE("Got binary: %s, %s\n", ret.path.c_str(), ret.fname.c_str());
    return ret;
}


void al_print(FILE *logfile, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(logfile, fmt, ap);
    va_end(ap);

    fflush(logfile);
}


static void DirectorySearch(const char *path, const char *ext, al::vector<std::string> *const results)
{
    TRACE("Searching %s for *%s\n", path, ext);
    DIR *dir{opendir(path)};
    if(!dir) return;

    const auto base = results->size();
    const size_t extlen{strlen(ext)};

    struct dirent *dirent;
    while((dirent=readdir(dir)) != nullptr)
    {
        if(strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0)
            continue;

        const size_t len{strlen(dirent->d_name)};
        if(len <= extlen) continue;
        if(al::strcasecmp(dirent->d_name+len-extlen, ext) != 0)
            continue;

        results->emplace_back();
        std::string &str = results->back();
        str = path;
        if(str.back() != '/')
            str.push_back('/');
        str += dirent->d_name;
    }
    closedir(dir);

    const al::span<std::string> newlist{results->data()+base, results->size()-base};
    std::sort(newlist.begin(), newlist.end());
    for(const auto &name : newlist)
        TRACE(" got %s\n", name.c_str());
}

al::vector<std::string> SearchDataFiles(const char *ext, const char *subdir)
{
    static std::mutex search_lock;
    std::lock_guard<std::mutex> _{search_lock};

    al::vector<std::string> results;
    if(subdir[0] == '/')
    {
        DirectorySearch(subdir, ext, &results);
        return results;
    }

    /* Search the app-local directory. */
    if(auto localpath = al::getenv("ALSOFT_LOCAL_PATH"))
        DirectorySearch(localpath->c_str(), ext, &results);
    else
    {
        al::vector<char> cwdbuf(256);
        while(!getcwd(cwdbuf.data(), cwdbuf.size()))
        {
            if(errno != ERANGE)
            {
                cwdbuf.clear();
                break;
            }
            cwdbuf.resize(cwdbuf.size() << 1);
        }
        if(cwdbuf.empty())
            DirectorySearch(".", ext, &results);
        else
        {
            DirectorySearch(cwdbuf.data(), ext, &results);
            cwdbuf.clear();
        }
    }

    // Search local data dir
    if(auto datapath = al::getenv("XDG_DATA_HOME"))
    {
        std::string &path = *datapath;
        if(path.back() != '/')
            path += '/';
        path += subdir;
        DirectorySearch(path.c_str(), ext, &results);
    }
    else if(auto homepath = al::getenv("HOME"))
    {
        std::string &path = *homepath;
        if(path.back() == '/')
            path.pop_back();
        path += "/.local/share/";
        path += subdir;
        DirectorySearch(path.c_str(), ext, &results);
    }

    // Search global data dirs
    std::string datadirs{al::getenv("XDG_DATA_DIRS").value_or("/usr/local/share/:/usr/share/")};

    size_t curpos{0u};
    while(curpos < datadirs.size())
    {
        size_t nextpos{datadirs.find(':', curpos)};

        std::string path{(nextpos != std::string::npos) ?
            datadirs.substr(curpos, nextpos++ - curpos) : datadirs.substr(curpos)};
        curpos = nextpos;

        if(path.empty()) continue;
        if(path.back() != '/')
            path += '/';
        path += subdir;

        DirectorySearch(path.c_str(), ext, &results);
    }

    return results;
}

void SetRTPriority()
{
    bool failed = false;
#if defined(HAVE_PTHREAD_SETSCHEDPARAM) && !defined(__OpenBSD__)
    if(RTPrioLevel > 0)
    {
        struct sched_param param;
        /* Use the minimum real-time priority possible for now (on Linux this
         * should be 1 for SCHED_RR) */
        param.sched_priority = sched_get_priority_min(SCHED_RR);
        failed = !!pthread_setschedparam(pthread_self(), SCHED_RR, &param);
    }
#else
    /* Real-time priority not available */
    failed = (RTPrioLevel>0);
#endif
    if(failed)
        ERR("Failed to set priority level for thread\n");
}

#endif
