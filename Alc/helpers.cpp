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

#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#ifdef HAVE_PROC_PIDPATH
#include <libproc.h>
#endif

#ifdef __FreeBSD__
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#ifndef AL_NO_UID_DEFS
#if defined(HAVE_GUIDDEF_H) || defined(HAVE_INITGUID_H)
#define INITGUID
#include <windows.h>
#ifdef HAVE_GUIDDEF_H
#include <guiddef.h>
#else
#include <initguid.h>
#endif

DEFINE_GUID(KSDATAFORMAT_SUBTYPE_PCM,        0x00000001, 0x0000, 0x0010, 0x80,0x00, 0x00,0xaa,0x00,0x38,0x9b,0x71);
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 0x00000003, 0x0000, 0x0010, 0x80,0x00, 0x00,0xaa,0x00,0x38,0x9b,0x71);

DEFINE_GUID(IID_IDirectSoundNotify,   0xb0210783, 0x89cd, 0x11d0, 0xaf,0x08, 0x00,0xa0,0xc9,0x25,0xcd,0x16);

DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xbcde0395, 0xe52f, 0x467c, 0x8e,0x3d, 0xc4,0x57,0x92,0x91,0x69,0x2e);
DEFINE_GUID(IID_IMMDeviceEnumerator,  0xa95664d2, 0x9614, 0x4f35, 0xa7,0x46, 0xde,0x8d,0xb6,0x36,0x17,0xe6);
DEFINE_GUID(IID_IAudioClient,         0x1cb9ad4c, 0xdbfa, 0x4c32, 0xb1,0x78, 0xc2,0xf5,0x68,0xa7,0x03,0xb2);
DEFINE_GUID(IID_IAudioRenderClient,   0xf294acfc, 0x3146, 0x4483, 0xa7,0xbf, 0xad,0xdc,0xa7,0xc2,0x60,0xe2);
DEFINE_GUID(IID_IAudioCaptureClient,  0xc8adbd64, 0xe71e, 0x48a0, 0xa4,0xde, 0x18,0x5c,0x39,0x5c,0xd3,0x17);

#ifdef HAVE_WASAPI
#include <wtypes.h>
#include <devpropdef.h>
#include <propkeydef.h>
DEFINE_DEVPROPKEY(DEVPKEY_Device_FriendlyName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80,0x20, 0x67,0xd1,0x46,0xa8,0x50,0xe0, 14);
DEFINE_PROPERTYKEY(PKEY_AudioEndpoint_FormFactor, 0x1da5d803, 0xd492, 0x4edd, 0x8c,0x23, 0xe0,0xc0,0xff,0xee,0x7f,0x0e, 0);
DEFINE_PROPERTYKEY(PKEY_AudioEndpoint_GUID, 0x1da5d803, 0xd492, 0x4edd, 0x8c, 0x23,0xe0, 0xc0,0xff,0xee,0x7f,0x0e, 4 );
#endif
#endif
#endif /* AL_NO_UID_DEFS */

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif
#ifdef HAVE_INTRIN_H
#include <intrin.h>
#endif
#ifdef HAVE_CPUID_H
#include <cpuid.h>
#endif
#ifdef HAVE_SYS_SYSCONF_H
#include <sys/sysconf.h>
#endif
#ifdef HAVE_FLOAT_H
#include <float.h>
#endif
#ifdef HAVE_IEEEFP_H
#include <ieeefp.h>
#endif

#ifndef _WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#elif defined(_WIN32_IE)
#include <shlobj.h>
#endif

#include <vector>
#include <string>
#include <algorithm>

#include "alMain.h"
#include "alu.h"
#include "cpu_caps.h"
#include "fpu_modes.h"
#include "atomic.h"
#include "uintmap.h"
#include "vector.h"
#include "alstring.h"
#include "compat.h"
#include "threads.h"


#if defined(HAVE_GCC_GET_CPUID) && (defined(__i386__) || defined(__x86_64__) || \
                                    defined(_M_IX86) || defined(_M_X64))
typedef unsigned int reg_type;
static inline void get_cpuid(int f, reg_type *regs)
{ __get_cpuid(f, &regs[0], &regs[1], &regs[2], &regs[3]); }
#define CAN_GET_CPUID
#elif defined(HAVE_CPUID_INTRINSIC) && (defined(__i386__) || defined(__x86_64__) || \
                                        defined(_M_IX86) || defined(_M_X64))
typedef int reg_type;
static inline void get_cpuid(int f, reg_type *regs)
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
    } cpuinf[3] = {{ { 0, 0, 0, 0 } }};

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
    FILE *file = fopen("/proc/cpuinfo", "rt");
    if(!file)
        ERR("Failed to open /proc/cpuinfo, cannot check for NEON support\n");
    else
    {
        al_string features = AL_STRING_INIT_STATIC();
        char buf[256];

        while(fgets(buf, sizeof(buf), file) != NULL)
        {
            if(strncmp(buf, "Features\t:", 10) != 0)
                continue;

            alstr_copy_cstr(&features, buf+10);
            while(VECTOR_BACK(features) != '\n')
            {
                if(fgets(buf, sizeof(buf), file) == NULL)
                    break;
                alstr_append_cstr(&features, buf);
            }
            break;
        }
        fclose(file);
        file = NULL;

        if(!alstr_empty(features))
        {
            const char *str = alstr_get_cstr(features);
            while(isspace(str[0])) ++str;

            TRACE("Got features string:%s\n", str);
            while((str=strstr(str, "neon")) != NULL)
            {
                if(isspace(*(str-1)) && (str[4] == 0 || isspace(str[4])))
                {
                    caps |= CPU_CAP_NEON;
                    break;
                }
                ++str;
            }
        }

        alstr_reset(&features);
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


void SetMixerFPUMode(FPUCtl *ctl)
{
#if defined(__GNUC__) && defined(HAVE_SSE)
    if((CPUCapFlags&CPU_CAP_SSE))
    {
        __asm__ __volatile__("stmxcsr %0" : "=m" (*&ctl->sse_state));
        unsigned int sseState = ctl->sse_state;
        sseState |= 0x8000; /* set flush-to-zero */
        if((CPUCapFlags&CPU_CAP_SSE2))
            sseState |= 0x0040; /* set denormals-are-zero */
        __asm__ __volatile__("ldmxcsr %0" : : "m" (*&sseState));
    }

#elif defined(HAVE___CONTROL87_2)

    __control87_2(0, 0, &ctl->state, &ctl->sse_state);
    _control87(_DN_FLUSH, _MCW_DN);

#elif defined(HAVE__CONTROLFP)

    ctl->state = _controlfp(0, 0);
    _controlfp(_DN_FLUSH, _MCW_DN);
#endif
}

void RestoreFPUMode(const FPUCtl *ctl)
{
#if defined(__GNUC__) && defined(HAVE_SSE)
    if((CPUCapFlags&CPU_CAP_SSE))
        __asm__ __volatile__("ldmxcsr %0" : : "m" (*&ctl->sse_state));

#elif defined(HAVE___CONTROL87_2)

    int mode;
    __control87_2(ctl->state, _MCW_DN, &mode, NULL);
    __control87_2(ctl->sse_state, _MCW_DN, NULL, &mode);

#elif defined(HAVE__CONTROLFP)

    _controlfp(ctl->state, _MCW_DN);
#endif
}


static int StringSortCompare(const void *str1, const void *str2)
{
    return alstr_cmp(*(const_al_string*)str1, *(const_al_string*)str2);
}

#ifdef _WIN32

PathNamePair GetProcBinary()
{
    PathNamePair ret;

    std::vector<WCHAR> fullpath(256);
    DWORD len;
    while((len=GetModuleFileNameW(nullptr, fullpath.data(), fullpath.size())) == fullpath.size())
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

    TRACE("Got: %s, %s\n", ret.path.c_str(), ret.fname.c_str());
    return ret;
}


static WCHAR *FromUTF8(const char *str)
{
    WCHAR *out = NULL;
    int len;

    if((len=MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0)) > 0)
    {
        out = static_cast<WCHAR*>(calloc(sizeof(WCHAR), len));
        MultiByteToWideChar(CP_UTF8, 0, str, -1, out, len);
    }
    return out;
}


void *LoadLib(const char *name)
{
    HANDLE hdl = NULL;
    WCHAR *wname;

    wname = FromUTF8(name);
    if(!wname)
        ERR("Failed to convert UTF-8 filename: \"%s\"\n", name);
    else
    {
        hdl = LoadLibraryW(wname);
        free(wname);
    }
    return hdl;
}
void CloseLib(void *handle)
{ FreeLibrary(reinterpret_cast<HMODULE>(handle)); }
void *GetSymbol(void *handle, const char *name)
{
    void *ret;

    ret = reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
    if(!ret) ERR("Failed to load %s\n", name);
    return ret;
}


void al_print(const char *type, const char *func, const char *fmt, ...)
{
    char str[1024];
    WCHAR *wstr;
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(str, sizeof(str), fmt, ap);
    va_end(ap);

    str[sizeof(str)-1] = 0;
    wstr = FromUTF8(str);
    if(!wstr)
        fprintf(LogFile, "AL lib: %s %s: <UTF-8 error> %s", type, func, str);
    else
    {
        fprintf(LogFile, "AL lib: %s %s: %ls", type, func, wstr);
        free(wstr);
        wstr = NULL;
    }
    fflush(LogFile);
}


static inline int is_slash(int c)
{ return (c == '\\' || c == '/'); }

static void DirectorySearch(const char *path, const char *ext, vector_al_string *results)
{
    al_string pathstr = AL_STRING_INIT_STATIC();
    WIN32_FIND_DATAW fdata;
    WCHAR *wpath;
    HANDLE hdl;

    alstr_copy_cstr(&pathstr, path);
    alstr_append_cstr(&pathstr, "\\*");
    alstr_append_cstr(&pathstr, ext);

    TRACE("Searching %s\n", alstr_get_cstr(pathstr));

    wpath = FromUTF8(alstr_get_cstr(pathstr));

    hdl = FindFirstFileW(wpath, &fdata);
    if(hdl != INVALID_HANDLE_VALUE)
    {
        size_t base = VECTOR_SIZE(*results);
        do {
            al_string str = AL_STRING_INIT_STATIC();
            alstr_copy_cstr(&str, path);
            alstr_append_char(&str, '\\');
            alstr_append_wcstr(&str, fdata.cFileName);
            TRACE("Got result %s\n", alstr_get_cstr(str));
            VECTOR_PUSH_BACK(*results, str);
        } while(FindNextFileW(hdl, &fdata));
        FindClose(hdl);

        if(VECTOR_SIZE(*results) > base)
            qsort(VECTOR_BEGIN(*results)+base, VECTOR_SIZE(*results)-base,
                    sizeof(VECTOR_FRONT(*results)), StringSortCompare);
    }

    free(wpath);
    alstr_reset(&pathstr);
}

vector_al_string SearchDataFiles(const char *ext, const char *subdir)
{
    static const int ids[2] = { CSIDL_APPDATA, CSIDL_COMMON_APPDATA };
    static RefCount search_lock;
    vector_al_string results = VECTOR_INIT_STATIC();
    size_t i;

    while(ATOMIC_EXCHANGE_SEQ(&search_lock, 1u) == 1u)
        althrd_yield();

    /* If the path is absolute, use it directly. */
    if(isalpha(subdir[0]) && subdir[1] == ':' && is_slash(subdir[2]))
    {
        al_string path = AL_STRING_INIT_STATIC();
        alstr_copy_cstr(&path, subdir);
#define FIX_SLASH(i) do { if(*(i) == '/') *(i) = '\\'; } while(0)
        VECTOR_FOR_EACH(char, path, FIX_SLASH);
#undef FIX_SLASH

        DirectorySearch(alstr_get_cstr(path), ext, &results);

        alstr_reset(&path);
    }
    else if(subdir[0] == '\\' && subdir[1] == '\\' && subdir[2] == '?' && subdir[3] == '\\')
        DirectorySearch(subdir, ext, &results);
    else
    {
        al_string path = AL_STRING_INIT_STATIC();
        WCHAR *cwdbuf;

        /* Search the app-local directory. */
        if((cwdbuf=_wgetenv(L"ALSOFT_LOCAL_PATH")) && *cwdbuf != '\0')
        {
            alstr_copy_wcstr(&path, cwdbuf);
            if(is_slash(VECTOR_BACK(path)))
            {
                VECTOR_POP_BACK(path);
                *VECTOR_END(path) = 0;
            }
        }
        else if(!(cwdbuf=_wgetcwd(NULL, 0)))
            alstr_copy_cstr(&path, ".");
        else
        {
            alstr_copy_wcstr(&path, cwdbuf);
            if(is_slash(VECTOR_BACK(path)))
            {
                VECTOR_POP_BACK(path);
                *VECTOR_END(path) = 0;
            }
            free(cwdbuf);
        }
#define FIX_SLASH(i) do { if(*(i) == '/') *(i) = '\\'; } while(0)
        VECTOR_FOR_EACH(char, path, FIX_SLASH);
#undef FIX_SLASH
        DirectorySearch(alstr_get_cstr(path), ext, &results);

        /* Search the local and global data dirs. */
        for(i = 0;i < COUNTOF(ids);i++)
        {
            WCHAR buffer[MAX_PATH];
            if(SHGetSpecialFolderPathW(NULL, buffer, ids[i], FALSE) != FALSE)
            {
                alstr_copy_wcstr(&path, buffer);
                if(!is_slash(VECTOR_BACK(path)))
                    alstr_append_char(&path, '\\');
                alstr_append_cstr(&path, subdir);
#define FIX_SLASH(i) do { if(*(i) == '/') *(i) = '\\'; } while(0)
                VECTOR_FOR_EACH(char, path, FIX_SLASH);
#undef FIX_SLASH

                DirectorySearch(alstr_get_cstr(path), ext, &results);
            }
        }

        alstr_reset(&path);
    }

    ATOMIC_STORE_SEQ(&search_lock, 0u);

    return results;
}

#else

PathNamePair GetProcBinary()
{
    PathNamePair ret;
    std::vector<char> pathname;

#ifdef __FreeBSD__
    size_t pathlen;
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
    if(sysctl(mib, 4, NULL, &pathlen, NULL, 0) == -1)
        WARN("Failed to sysctl kern.proc.pathname: %s\n", strerror(errno));
    else
    {
        pathname.resize(pathlen + 1);
        sysctl(mib, 4, pathname.data(), &pathlen, nullptr, 0);
        pathname[pathlen] = 0;
    }
#endif
#ifdef HAVE_PROC_PIDPATH
    if(pathname.empty())
    {
        const pid_t pid = getpid();
        char procpath[PROC_PIDPATHINFO_MAXSIZE];
        int ret;

        ret = proc_pidpath(pid, procpath, sizeof(procpath));
        if(ret < 1)
            WARN("proc_pidpath(%d, ...) failed: %s\n", pid, strerror(errno));
        else
            pathname.append(procpath, procpath+strlen(procpath));
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

        while(len > 0 && (size_t)len == pathname.size())
        {
            pathname.resize(pathname.size() << 1);
            len = readlink(selfname, pathname.data(), pathname.size());
        }
        if(len <= 0)
        {
            WARN("Failed to readlink %s: %s\n", selfname, strerror(errno));
            return ret;
        }

        pathname[len] = 0;
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

    TRACE("Got: %s, %s\n", ret.path.c_str(), ret.fname.c_str());
    return ret;
}


#ifdef HAVE_DLFCN_H

void *LoadLib(const char *name)
{
    const char *err;
    void *handle;

    dlerror();
    handle = dlopen(name, RTLD_NOW);
    if((err=dlerror()) != NULL)
        handle = NULL;
    return handle;
}
void CloseLib(void *handle)
{ dlclose(handle); }
void *GetSymbol(void *handle, const char *name)
{
    const char *err;
    void *sym;

    dlerror();
    sym = dlsym(handle, name);
    if((err=dlerror()) != NULL)
    {
        WARN("Failed to load %s: %s\n", name, err);
        sym = NULL;
    }
    return sym;
}

#endif /* HAVE_DLFCN_H */

void al_print(const char *type, const char *func, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(LogFile, "AL lib: %s %s: ", type, func);
    vfprintf(LogFile, fmt, ap);
    va_end(ap);

    fflush(LogFile);
}


static void DirectorySearch(const char *path, const char *ext, vector_al_string *results)
{
    size_t extlen = strlen(ext);
    DIR *dir;

    TRACE("Searching %s for *%s\n", path, ext);
    dir = opendir(path);
    if(dir != NULL)
    {
        size_t base = VECTOR_SIZE(*results);
        struct dirent *dirent;
        while((dirent=readdir(dir)) != NULL)
        {
            al_string str;
            size_t len;
            if(strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0)
                continue;

            len = strlen(dirent->d_name);
            if(len <= extlen)
                continue;
            if(strcasecmp(dirent->d_name+len-extlen, ext) != 0)
                continue;

            AL_STRING_INIT(str);
            alstr_copy_cstr(&str, path);
            if(VECTOR_BACK(str) != '/')
                alstr_append_char(&str, '/');
            alstr_append_cstr(&str, dirent->d_name);
            TRACE("Got result %s\n", alstr_get_cstr(str));
            VECTOR_PUSH_BACK(*results, str);
        }
        closedir(dir);

        if(VECTOR_SIZE(*results) > base)
            qsort(VECTOR_BEGIN(*results)+base, VECTOR_SIZE(*results)-base,
                  sizeof(VECTOR_FRONT(*results)), StringSortCompare);
    }
}

vector_al_string SearchDataFiles(const char *ext, const char *subdir)
{
    static RefCount search_lock;
    vector_al_string results = VECTOR_INIT_STATIC();

    while(ATOMIC_EXCHANGE_SEQ(&search_lock, 1u) == 1u)
        althrd_yield();

    if(subdir[0] == '/')
        DirectorySearch(subdir, ext, &results);
    else
    {
        al_string path = AL_STRING_INIT_STATIC();
        const char *str, *next;

        /* Search the app-local directory. */
        if((str=getenv("ALSOFT_LOCAL_PATH")) && *str != '\0')
            DirectorySearch(str, ext, &results);
        else
        {
            size_t cwdlen = 256;
            char *cwdbuf = static_cast<char*>(malloc(cwdlen));
            while(!getcwd(cwdbuf, cwdlen))
            {
                free(cwdbuf);
                cwdbuf = NULL;
                if(errno != ERANGE)
                    break;
                cwdlen <<= 1;
                cwdbuf = static_cast<char*>(malloc(cwdlen));
            }
            if(!cwdbuf)
                DirectorySearch(".", ext, &results);
            else
            {
                DirectorySearch(cwdbuf, ext, &results);
                free(cwdbuf);
                cwdbuf = NULL;
            }
        }

        // Search local data dir
        if((str=getenv("XDG_DATA_HOME")) != NULL && str[0] != '\0')
        {
            alstr_copy_cstr(&path, str);
            if(VECTOR_BACK(path) != '/')
                alstr_append_char(&path, '/');
            alstr_append_cstr(&path, subdir);
            DirectorySearch(alstr_get_cstr(path), ext, &results);
        }
        else if((str=getenv("HOME")) != NULL && str[0] != '\0')
        {
            alstr_copy_cstr(&path, str);
            if(VECTOR_BACK(path) == '/')
            {
                VECTOR_POP_BACK(path);
                *VECTOR_END(path) = 0;
            }
            alstr_append_cstr(&path, "/.local/share/");
            alstr_append_cstr(&path, subdir);
            DirectorySearch(alstr_get_cstr(path), ext, &results);
        }

        // Search global data dirs
        if((str=getenv("XDG_DATA_DIRS")) == NULL || str[0] == '\0')
            str = "/usr/local/share/:/usr/share/";

        next = str;
        while((str=next) != NULL && str[0] != '\0')
        {
            next = strchr(str, ':');
            if(!next)
                alstr_copy_cstr(&path, str);
            else
            {
                alstr_copy_range(&path, str, next);
                ++next;
            }
            if(!alstr_empty(path))
            {
                if(VECTOR_BACK(path) != '/')
                    alstr_append_char(&path, '/');
                alstr_append_cstr(&path, subdir);

                DirectorySearch(alstr_get_cstr(path), ext, &results);
            }
        }

        alstr_reset(&path);
    }

    ATOMIC_STORE_SEQ(&search_lock, 0u);

    return results;
}

#endif


void SetRTPriority(void)
{
    ALboolean failed = AL_FALSE;

#ifdef _WIN32
    if(RTPrioLevel > 0)
        failed = !SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#elif defined(HAVE_PTHREAD_SETSCHEDPARAM) && !defined(__OpenBSD__)
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


void alstr_clear(al_string *str)
{
    if(!alstr_empty(*str))
    {
        /* Reserve one more character than the total size of the string. This
         * is to ensure we have space to add a null terminator in the string
         * data so it can be used as a C-style string.
         */
        VECTOR_RESIZE(*str, 0, 1);
        VECTOR_ELEM(*str, 0) = 0;
    }
}

static inline int alstr_compare(const al_string_char_type *str1, size_t str1len,
                                const al_string_char_type *str2, size_t str2len)
{
    size_t complen = (str1len < str2len) ? str1len : str2len;
    int ret = memcmp(str1, str2, complen);
    if(ret == 0)
    {
        if(str1len > str2len) return  1;
        if(str1len < str2len) return -1;
    }
    return ret;
}
int alstr_cmp(const_al_string str1, const_al_string str2)
{
    return alstr_compare(&VECTOR_FRONT(str1), alstr_length(str1),
                         &VECTOR_FRONT(str2), alstr_length(str2));
}
int alstr_cmp_cstr(const_al_string str1, const al_string_char_type *str2)
{
    return alstr_compare(&VECTOR_FRONT(str1), alstr_length(str1),
                         str2, strlen(str2));
}

void alstr_copy(al_string *str, const_al_string from)
{
    size_t len = alstr_length(from);
    size_t i;

    VECTOR_RESIZE(*str, len, len+1);
    for(i = 0;i < len;i++)
        VECTOR_ELEM(*str, i) = VECTOR_ELEM(from, i);
    VECTOR_ELEM(*str, i) = 0;
}

void alstr_copy_cstr(al_string *str, const al_string_char_type *from)
{
    size_t len = strlen(from);
    size_t i;

    VECTOR_RESIZE(*str, len, len+1);
    for(i = 0;i < len;i++)
        VECTOR_ELEM(*str, i) = from[i];
    VECTOR_ELEM(*str, i) = 0;
}

void alstr_copy_range(al_string *str, const al_string_char_type *from, const al_string_char_type *to)
{
    size_t len = to - from;
    size_t i;

    VECTOR_RESIZE(*str, len, len+1);
    for(i = 0;i < len;i++)
        VECTOR_ELEM(*str, i) = from[i];
    VECTOR_ELEM(*str, i) = 0;
}

void alstr_append_char(al_string *str, const al_string_char_type c)
{
    size_t len = alstr_length(*str);
    VECTOR_RESIZE(*str, len+1, len+2);
    VECTOR_BACK(*str) = c;
    VECTOR_ELEM(*str, len+1) = 0;
}

void alstr_append_cstr(al_string *str, const al_string_char_type *from)
{
    size_t len = strlen(from);
    if(len != 0)
    {
        size_t base = alstr_length(*str);
        size_t i;

        VECTOR_RESIZE(*str, base+len, base+len+1);
        for(i = 0;i < len;i++)
            VECTOR_ELEM(*str, base+i) = from[i];
        VECTOR_ELEM(*str, base+i) = 0;
    }
}

void alstr_append_range(al_string *str, const al_string_char_type *from, const al_string_char_type *to)
{
    size_t len = to - from;
    if(len != 0)
    {
        size_t base = alstr_length(*str);
        size_t i;

        VECTOR_RESIZE(*str, base+len, base+len+1);
        for(i = 0;i < len;i++)
            VECTOR_ELEM(*str, base+i) = from[i];
        VECTOR_ELEM(*str, base+i) = 0;
    }
}

#ifdef _WIN32
void alstr_copy_wcstr(al_string *str, const wchar_t *from)
{
    int len;
    if((len=WideCharToMultiByte(CP_UTF8, 0, from, -1, NULL, 0, NULL, NULL)) > 0)
    {
        VECTOR_RESIZE(*str, len-1, len);
        WideCharToMultiByte(CP_UTF8, 0, from, -1, &VECTOR_FRONT(*str), len, NULL, NULL);
        VECTOR_ELEM(*str, len-1) = 0;
    }
}

void alstr_append_wcstr(al_string *str, const wchar_t *from)
{
    int len;
    if((len=WideCharToMultiByte(CP_UTF8, 0, from, -1, NULL, 0, NULL, NULL)) > 0)
    {
        size_t base = alstr_length(*str);
        VECTOR_RESIZE(*str, base+len-1, base+len);
        WideCharToMultiByte(CP_UTF8, 0, from, -1, &VECTOR_ELEM(*str, base), len, NULL, NULL);
        VECTOR_ELEM(*str, base+len-1) = 0;
    }
}

void alstr_copy_wrange(al_string *str, const wchar_t *from, const wchar_t *to)
{
    int len;
    if((len=WideCharToMultiByte(CP_UTF8, 0, from, (int)(to-from), NULL, 0, NULL, NULL)) > 0)
    {
        VECTOR_RESIZE(*str, len, len+1);
        WideCharToMultiByte(CP_UTF8, 0, from, (int)(to-from), &VECTOR_FRONT(*str), len+1, NULL, NULL);
        VECTOR_ELEM(*str, len) = 0;
    }
}

void alstr_append_wrange(al_string *str, const wchar_t *from, const wchar_t *to)
{
    int len;
    if((len=WideCharToMultiByte(CP_UTF8, 0, from, (int)(to-from), NULL, 0, NULL, NULL)) > 0)
    {
        size_t base = alstr_length(*str);
        VECTOR_RESIZE(*str, base+len, base+len+1);
        WideCharToMultiByte(CP_UTF8, 0, from, (int)(to-from), &VECTOR_ELEM(*str, base), len+1, NULL, NULL);
        VECTOR_ELEM(*str, base+len) = 0;
    }
}
#endif
