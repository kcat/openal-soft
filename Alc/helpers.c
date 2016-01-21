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
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#ifdef HAVE_DIRENT_H
#include <dirent.h>
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

#ifdef HAVE_MMDEVAPI
#include <devpropdef.h>
#include <propkeydef.h>
DEFINE_DEVPROPKEY(DEVPKEY_Device_FriendlyName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80,0x20, 0x67,0xd1,0x46,0xa8,0x50,0xe0, 14);
DEFINE_PROPERTYKEY(PKEY_AudioEndpoint_FormFactor, 0x1da5d803, 0xd492, 0x4edd, 0x8c,0x23, 0xe0,0xc0,0xff,0xee,0x7f,0x0e, 0);
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
#include <unistd.h>
#elif defined(_WIN32_IE)
#include <shlobj.h>
#endif

#include "alMain.h"
#include "alu.h"
#include "atomic.h"
#include "uintmap.h"
#include "vector.h"
#include "alstring.h"
#include "compat.h"
#include "threads.h"


extern inline ALuint NextPowerOf2(ALuint value);
extern inline ALint fastf2i(ALfloat f);
extern inline ALuint fastf2u(ALfloat f);


ALuint CPUCapFlags = 0;


void FillCPUCaps(ALuint capfilter)
{
    ALuint caps = 0;

/* FIXME: We really should get this for all available CPUs in case different
 * CPUs have different caps (is that possible on one machine?). */
#if defined(HAVE_GCC_GET_CPUID) && (defined(__i386__) || defined(__x86_64__) || \
                                    defined(_M_IX86) || defined(_M_X64))
    union {
        unsigned int regs[4];
        char str[sizeof(unsigned int[4])];
    } cpuinf[3];

    if(!__get_cpuid(0, &cpuinf[0].regs[0], &cpuinf[0].regs[1], &cpuinf[0].regs[2], &cpuinf[0].regs[3]))
        ERR("Failed to get CPUID\n");
    else
    {
        unsigned int maxfunc = cpuinf[0].regs[0];
        unsigned int maxextfunc = 0;

        if(__get_cpuid(0x80000000, &cpuinf[0].regs[0], &cpuinf[0].regs[1], &cpuinf[0].regs[2], &cpuinf[0].regs[3]))
            maxextfunc = cpuinf[0].regs[0];
        TRACE("Detected max CPUID function: 0x%x (ext. 0x%x)\n", maxfunc, maxextfunc);

        TRACE("Vendor ID: \"%.4s%.4s%.4s\"\n", cpuinf[0].str+4, cpuinf[0].str+12, cpuinf[0].str+8);
        if(maxextfunc >= 0x80000004 &&
           __get_cpuid(0x80000002, &cpuinf[0].regs[0], &cpuinf[0].regs[1], &cpuinf[0].regs[2], &cpuinf[0].regs[3]) &&
           __get_cpuid(0x80000003, &cpuinf[1].regs[0], &cpuinf[1].regs[1], &cpuinf[1].regs[2], &cpuinf[1].regs[3]) &&
           __get_cpuid(0x80000004, &cpuinf[2].regs[0], &cpuinf[2].regs[1], &cpuinf[2].regs[2], &cpuinf[2].regs[3]))
            TRACE("Name: \"%.16s%.16s%.16s\"\n", cpuinf[0].str, cpuinf[1].str, cpuinf[2].str);

        if(maxfunc >= 1 &&
           __get_cpuid(1, &cpuinf[0].regs[0], &cpuinf[0].regs[1], &cpuinf[0].regs[2], &cpuinf[0].regs[3]))
        {
            if((cpuinf[0].regs[3]&(1<<25)))
            {
                caps |= CPU_CAP_SSE;
                if((cpuinf[0].regs[3]&(1<<26)))
                {
                    caps |= CPU_CAP_SSE2;
                    if((cpuinf[0].regs[2]&(1<<0)))
                    {
                        caps |= CPU_CAP_SSE3;
                        if((cpuinf[0].regs[2]&(1<<19)))
                            caps |= CPU_CAP_SSE4_1;
                    }
                }
            }
        }
    }
#elif defined(HAVE_CPUID_INTRINSIC) && (defined(__i386__) || defined(__x86_64__) || \
                                        defined(_M_IX86) || defined(_M_X64))
    union {
        int regs[4];
        char str[sizeof(int[4])];
    } cpuinf[3];

    (__cpuid)(cpuinf[0].regs, 0);
    if(cpuinf[0].regs[0] == 0)
        ERR("Failed to get CPUID\n");
    else
    {
        unsigned int maxfunc = cpuinf[0].regs[0];
        unsigned int maxextfunc;

        (__cpuid)(cpuinf[0].regs, 0x80000000);
        maxextfunc = cpuinf[0].regs[0];

        TRACE("Detected max CPUID function: 0x%x (ext. 0x%x)\n", maxfunc, maxextfunc);

        TRACE("Vendor ID: \"%.4s%.4s%.4s\"\n", cpuinf[0].str+4, cpuinf[0].str+12, cpuinf[0].str+8);
        if(maxextfunc >= 0x80000004)
        {
            (__cpuid)(cpuinf[0].regs, 0x80000002);
            (__cpuid)(cpuinf[1].regs, 0x80000003);
            (__cpuid)(cpuinf[2].regs, 0x80000004);
            TRACE("Name: \"%.16s%.16s%.16s\"\n", cpuinf[0].str, cpuinf[1].str, cpuinf[2].str);
        }

        if(maxfunc >= 1)
        {
            (__cpuid)(cpuinf[0].regs, 1);
            if((cpuinf[0].regs[3]&(1<<25)))
            {
                caps |= CPU_CAP_SSE;
                if((cpuinf[0].regs[3]&(1<<26)))
                {
                    caps |= CPU_CAP_SSE2;
                    if((cpuinf[0].regs[2]&(1<<0)))
                    {
                        caps |= CPU_CAP_SSE3;
                        if((cpuinf[0].regs[2]&(1<<19)))
                            caps |= CPU_CAP_SSE4_1;
                    }
                }
            }
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
    /* Assume Neon support if compiled with it */
    caps |= CPU_CAP_NEON;
#endif

    TRACE("Extensions:%s%s%s%s%s%s\n",
        ((capfilter&CPU_CAP_SSE)    ? ((caps&CPU_CAP_SSE)    ? " +SSE"    : " -SSE")    : ""),
        ((capfilter&CPU_CAP_SSE2)   ? ((caps&CPU_CAP_SSE2)   ? " +SSE2"   : " -SSE2")   : ""),
        ((capfilter&CPU_CAP_SSE3)   ? ((caps&CPU_CAP_SSE3)   ? " +SSE3"   : " -SSE3")   : ""),
        ((capfilter&CPU_CAP_SSE4_1) ? ((caps&CPU_CAP_SSE4_1) ? " +SSE4.1" : " -SSE4.1") : ""),
        ((capfilter&CPU_CAP_NEON)   ? ((caps&CPU_CAP_NEON)   ? " +Neon"   : " -Neon")   : ""),
        ((!capfilter) ? " -none-" : "")
    );
    CPUCapFlags = caps & capfilter;
}


void *al_malloc(size_t alignment, size_t size)
{
#if defined(HAVE_ALIGNED_ALLOC)
    size = (size+(alignment-1))&~(alignment-1);
    return aligned_alloc(alignment, size);
#elif defined(HAVE_POSIX_MEMALIGN)
    void *ret;
    if(posix_memalign(&ret, alignment, size) == 0)
        return ret;
    return NULL;
#elif defined(HAVE__ALIGNED_MALLOC)
    return _aligned_malloc(size, alignment);
#else
    char *ret = malloc(size+alignment);
    if(ret != NULL)
    {
        *(ret++) = 0x00;
        while(((ptrdiff_t)ret&(alignment-1)) != 0)
            *(ret++) = 0x55;
    }
    return ret;
#endif
}

void *al_calloc(size_t alignment, size_t size)
{
    void *ret = al_malloc(alignment, size);
    if(ret) memset(ret, 0, size);
    return ret;
}

void al_free(void *ptr)
{
#if defined(HAVE_ALIGNED_ALLOC) || defined(HAVE_POSIX_MEMALIGN)
    free(ptr);
#elif defined(HAVE__ALIGNED_MALLOC)
    _aligned_free(ptr);
#else
    if(ptr != NULL)
    {
        char *finder = ptr;
        do {
            --finder;
        } while(*finder == 0x55);
        free(finder);
    }
#endif
}


void SetMixerFPUMode(FPUCtl *ctl)
{
#ifdef HAVE_FENV_H
    fegetenv(STATIC_CAST(fenv_t, ctl));
#if defined(__GNUC__) && defined(HAVE_SSE)
    /* FIXME: Some fegetenv implementations can get the SSE environment too?
     * How to tell when it does? */
    if((CPUCapFlags&CPU_CAP_SSE))
        __asm__ __volatile__("stmxcsr %0" : "=m" (*&ctl->sse_state));
#endif

#ifdef FE_TOWARDZERO
    fesetround(FE_TOWARDZERO);
#endif
#if defined(__GNUC__) && defined(HAVE_SSE)
    if((CPUCapFlags&CPU_CAP_SSE))
    {
        int sseState = ctl->sse_state;
        sseState |= 0x6000; /* set round-to-zero */
        sseState |= 0x8000; /* set flush-to-zero */
        if((CPUCapFlags&CPU_CAP_SSE2))
            sseState |= 0x0040; /* set denormals-are-zero */
        __asm__ __volatile__("ldmxcsr %0" : : "m" (*&sseState));
    }
#endif

#elif defined(HAVE___CONTROL87_2)

    int mode;
    __control87_2(0, 0, &ctl->state, NULL);
    __control87_2(_RC_CHOP, _MCW_RC, &mode, NULL);
#ifdef HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
    {
        __control87_2(0, 0, NULL, &ctl->sse_state);
        __control87_2(_RC_CHOP|_DN_FLUSH, _MCW_RC|_MCW_DN, NULL, &mode);
    }
#endif

#elif defined(HAVE__CONTROLFP)

    ctl->state = _controlfp(0, 0);
    (void)_controlfp(_RC_CHOP, _MCW_RC);
#endif
}

void RestoreFPUMode(const FPUCtl *ctl)
{
#ifdef HAVE_FENV_H
    fesetenv(STATIC_CAST(fenv_t, ctl));
#if defined(__GNUC__) && defined(HAVE_SSE)
    if((CPUCapFlags&CPU_CAP_SSE))
        __asm__ __volatile__("ldmxcsr %0" : : "m" (*&ctl->sse_state));
#endif

#elif defined(HAVE___CONTROL87_2)

    int mode;
    __control87_2(ctl->state, _MCW_RC, &mode, NULL);
#ifdef HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        __control87_2(ctl->sse_state, _MCW_RC|_MCW_DN, NULL, &mode);
#endif

#elif defined(HAVE__CONTROLFP)

    _controlfp(ctl->state, _MCW_RC);
#endif
}


static int StringSortCompare(const void *str1, const void *str2)
{
    return al_string_cmp(*(const_al_string*)str1, *(const_al_string*)str2);
}

#ifdef _WIN32

static WCHAR *FromUTF8(const char *str)
{
    WCHAR *out = NULL;
    int len;

    if((len=MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0)) > 0)
    {
        out = calloc(sizeof(WCHAR), len);
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
{ FreeLibrary((HANDLE)handle); }
void *GetSymbol(void *handle, const char *name)
{
    void *ret;

    ret = (void*)GetProcAddress((HANDLE)handle, name);
    if(ret == NULL)
        ERR("Failed to load %s\n", name);
    return ret;
}

WCHAR *strdupW(const WCHAR *str)
{
    const WCHAR *n;
    WCHAR *ret;
    size_t len;

    n = str;
    while(*n) n++;
    len = n - str;

    ret = calloc(sizeof(WCHAR), len+1);
    if(ret != NULL)
        memcpy(ret, str, sizeof(WCHAR)*len);
    return ret;
}

FILE *al_fopen(const char *fname, const char *mode)
{
    WCHAR *wname=NULL, *wmode=NULL;
    FILE *file = NULL;

    wname = FromUTF8(fname);
    wmode = FromUTF8(mode);
    if(!wname)
        ERR("Failed to convert UTF-8 filename: \"%s\"\n", fname);
    else if(!wmode)
        ERR("Failed to convert UTF-8 mode: \"%s\"\n", mode);
    else
        file = _wfopen(wname, wmode);

    free(wname);
    free(wmode);

    return file;
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

FILE *OpenDataFile(const char *fname, const char *subdir)
{
    static const int ids[2] = { CSIDL_APPDATA, CSIDL_COMMON_APPDATA };
    WCHAR *wname=NULL, *wsubdir=NULL;
    FILE *f;
    size_t i;

    wname = FromUTF8(fname);
    if(!wname)
    {
        ERR("Failed to convert UTF-8 filename: \"%s\"\n", fname);
        return NULL;
    }

    /* If the path is absolute, open it directly. */
    if(wname[0] != '\0' && wname[1] == ':' && is_slash(wname[2]))
    {
        f = _wfopen(wname, L"rb");
        if(f) TRACE("Opened %s\n", fname);
        else WARN("Could not open %s\n", fname);
        free(wname);
        return f;
    }

    /* Try the current directory first before the data directories. */
    if((f=_wfopen(wname, L"rb")) != NULL)
    {
        TRACE("Opened %s\n", fname);
        free(wname);
        return f;
    }

    wsubdir = FromUTF8(subdir);
    if(!wsubdir)
    {
        ERR("Failed to convert UTF-8 subdir: \"%s\"\n", subdir);
        free(wname);
        return NULL;
    }

    for(i = 0;i < COUNTOF(ids);i++)
    {
        WCHAR buffer[PATH_MAX];
        size_t len;

        if(SHGetSpecialFolderPathW(NULL, buffer, ids[i], FALSE) == FALSE)
            continue;

        len = lstrlenW(buffer);
        if(len > 0 && is_slash(buffer[len-1]))
            buffer[--len] = '\0';
        _snwprintf(buffer+len, PATH_MAX-len, L"/%ls/%ls", wsubdir, wname);
        len = lstrlenW(buffer);
        while(len > 0)
        {
            --len;
            if(buffer[len] == '/')
                buffer[len] = '\\';
        }

        if((f=_wfopen(buffer, L"rb")) != NULL)
        {
            al_string filepath = AL_STRING_INIT_STATIC();
            al_string_copy_wcstr(&filepath, buffer);
            TRACE("Opened %s\n", al_string_get_cstr(filepath));
            al_string_deinit(&filepath);
            break;
        }
    }
    free(wname);
    free(wsubdir);

    if(f == NULL)
        WARN("Could not open %s\\%s\n", subdir, fname);
    return f;
}


static size_t strlenW(const WCHAR *str)
{
    const WCHAR *end = str;
    while(*end) ++end;
    return end-str;
}

static const WCHAR *strchrW(const WCHAR *str, WCHAR ch)
{
    for(;*str != 0;++str)
    {
        if(*str == ch)
            return str;
    }
    return NULL;
}

static const WCHAR *strrchrW(const WCHAR *str, WCHAR ch)
{
    const WCHAR *ret = NULL;
    for(;*str != 0;++str)
    {
        if(*str == ch)
            ret = str;
    }
    return ret;
}

static const WCHAR *strstrW(const WCHAR *haystack, const WCHAR *needle)
{
    size_t len = strlenW(needle);
    while(*haystack != 0)
    {
        if(CompareStringW(GetThreadLocale(), NORM_IGNORECASE,
                          haystack, len, needle, len) == CSTR_EQUAL)
            return haystack;

        do {
            ++haystack;
        } while(((*haystack)&0xC000) == 0x8000);
    }
    return NULL;
}


/* Compares the filename in the find data with the match string. The match
 * string may contain the "%r" marker to signifiy a sample rate (really any
 * positive integer), "%%" to signify a single '%', or "%s" for a (non-greedy)
 * string.
 */
static int MatchFilter(const WCHAR *match, const WIN32_FIND_DATAW *fdata)
{
    const WCHAR *name = fdata->cFileName;
    int ret = 1;

    do {
        const WCHAR *p = strchrW(match, '%');
        if(!p)
            ret = CompareStringW(GetThreadLocale(), NORM_IGNORECASE,
                                 match, -1, name, -1) == CSTR_EQUAL;
        else
        {
            int len = p-match;
            ret = lstrlenW(name) >= len;
            if(ret)
                ret = CompareStringW(GetThreadLocale(), NORM_IGNORECASE,
                                     match, len, name, len) == CSTR_EQUAL;
            if(ret)
            {
                match += len;
                name += len;

                ++p;
                if(*p == 'r')
                {
                    unsigned long l = 0;
                    while(*name >= '0' && *name <= '9')
                    {
                        l = l*10 + (*name-'0');
                        ++name;
                    }
                    ret = l > 0;
                    ++p;
                }
                else if(*p == 's')
                {
                    const WCHAR *next = p+1;
                    if(*next != '\0' && *next != '%')
                    {
                        const WCHAR *next_p = strchrW(next, '%');
                        const WCHAR *m;

                        if(!next_p)
                            m = strstrW(name, next);
                        else
                        {
                            WCHAR *tmp = malloc((next_p - next + 1) * 2);
                            memcpy(tmp, next, (next_p - next) * 2);
                            tmp[next_p - next] = 0;

                            m = strstrW(name, tmp);

                            free(tmp);
                        }

                        ret = !!m;
                        if(ret)
                        {
                            size_t l;
                            if(next_p) l = next_p - next;
                            else l = strlenW(next);

                            name = m + l;
                            next += l;
                        }
                    }
                    p = next;
                }
            }
        }

        match = p;
    } while(ret && match && *match);

    return ret;
}

static void RecurseDirectorySearch(const char *path, const WCHAR *match, vector_al_string *results)
{
    WIN32_FIND_DATAW fdata;
    const WCHAR *sep, *p;
    HANDLE hdl;

    if(!match[0])
        return;

    /* Find the last directory separator and the next '%' marker in the match
     * string. */
    sep = strrchrW(match, '\\');
    p = strchrW(match, '%');

    /* If there's no separator, test the files in the specified path against
     * the match string, and add the results. */
    if(!sep)
    {
        al_string pathstr = AL_STRING_INIT_STATIC();
        WCHAR *wpath;

        TRACE("Searching %s for %ls\n", path, match);

        al_string_append_cstr(&pathstr, path);
        al_string_append_cstr(&pathstr, "\\*.*");
        wpath = FromUTF8(al_string_get_cstr(pathstr));

        hdl = FindFirstFileW(wpath, &fdata);
        if(hdl != INVALID_HANDLE_VALUE)
        {
            size_t base = VECTOR_SIZE(*results);
            do {
                if(MatchFilter(match, &fdata))
                {
                    al_string str = AL_STRING_INIT_STATIC();
                    al_string_copy_cstr(&str, path);
                    al_string_append_char(&str, '\\');
                    al_string_append_wcstr(&str, fdata.cFileName);
                    TRACE("Got result %s\n", al_string_get_cstr(str));
                    VECTOR_PUSH_BACK(*results, str);
                }
            } while(FindNextFileW(hdl, &fdata));
            FindClose(hdl);

            if(VECTOR_SIZE(*results) > base)
                qsort(VECTOR_ITER_BEGIN(*results)+base, VECTOR_SIZE(*results)-base,
                      sizeof(VECTOR_FRONT(*results)), StringSortCompare);
        }

        free(wpath);
        al_string_deinit(&pathstr);

        return;
    }

    /* If there's no '%' marker, or it's after the final separator, append the
     * remaining directories to the path and recurse into it with the remaining
     * filename portion. */
    if(!p || p-sep >= 0)
    {
        al_string npath = AL_STRING_INIT_STATIC();
        al_string_append_cstr(&npath, path);
        al_string_append_char(&npath, '\\');
        al_string_append_wrange(&npath, match, sep);

        TRACE("Recursing into %s with %ls\n", al_string_get_cstr(npath), sep+1);
        RecurseDirectorySearch(al_string_get_cstr(npath), sep+1, results);

        al_string_deinit(&npath);
        return;
    }

    /* Look for the last separator before the '%' marker, and the first
     * separator after it. */
    sep = strchrW(match, '\\');
    if(sep-p >= 0) sep = NULL;
    for(;;)
    {
        const WCHAR *next = strchrW(sep?sep+1:match, '\\');
        if(next-p < 0)
        {
            al_string npath = AL_STRING_INIT_STATIC();
            WCHAR *nwpath, *nwmatch;

            /* Append up to the last directory before the one with a '%'. */
            al_string_copy_cstr(&npath, path);
            if(sep)
            {
                al_string_append_char(&npath, '\\');
                al_string_append_wrange(&npath, match, sep);
            }
            al_string_append_cstr(&npath, "\\*.*");
            nwpath = FromUTF8(al_string_get_cstr(npath));

            /* Take the directory name containing a '%' as a new string to
             * match against. */
            if(!sep)
            {
                nwmatch = calloc(2, next-match+1);
                memcpy(nwmatch, match, (next-match)*2);
            }
            else
            {
                nwmatch = calloc(2, next-(sep+1)+1);
                memcpy(nwmatch, sep+1, (next-(sep+1))*2);
            }

            /* For each matching directory name, recurse into it with the
             * remaining string. */
            TRACE("Searching %s for %ls\n", al_string_get_cstr(npath), nwmatch);
            hdl = FindFirstFileW(nwpath, &fdata);
            if(hdl != INVALID_HANDLE_VALUE)
            {
                do {
                    if(MatchFilter(nwmatch, &fdata))
                    {
                        al_string ndir = AL_STRING_INIT_STATIC();
                        al_string_copy(&ndir, npath);
                        al_string_append_char(&ndir, '\\');
                        al_string_append_wcstr(&ndir, fdata.cFileName);
                        TRACE("Recursing %s with %ls\n", al_string_get_cstr(ndir), next+1);
                        RecurseDirectorySearch(al_string_get_cstr(ndir), next+1, results);
                        al_string_deinit(&ndir);
                    }
                } while(FindNextFileW(hdl, &fdata));
                FindClose(hdl);
            }

            free(nwmatch);
            free(nwpath);
            al_string_deinit(&npath);
            break;
        }
        sep = next;
    }
}

vector_al_string SearchDataFiles(const char *match, const char *subdir)
{
    static const int ids[2] = { CSIDL_APPDATA, CSIDL_COMMON_APPDATA };
    static RefCount search_lock;
    vector_al_string results = VECTOR_INIT_STATIC();
    WCHAR *wmatch;
    size_t i;

    while(ATOMIC_EXCHANGE(uint, &search_lock, 1) == 1)
        althrd_yield();

    wmatch = FromUTF8(match);
    if(!wmatch)
    {
        ERR("Failed to convert UTF-8 filename: \"%s\"\n", match);
        return results;
    }
    for(i = 0;wmatch[i];++i)
    {
        if(wmatch[i] == '/')
            wmatch[i] = '\\';
    }

    /* If the path is absolute, use it directly. */
    if(isalpha(wmatch[0]) && wmatch[1] == ':' && is_slash(wmatch[2]))
    {
        char drv[3] = { (char)wmatch[0], ':', 0 };
        RecurseDirectorySearch(drv, wmatch+3, &results);
    }
    else if(wmatch[0] == '\\' && wmatch[1] == '\\' && wmatch[2] == '?' && wmatch[3] == '\\')
        RecurseDirectorySearch("\\\\?", wmatch+4, &results);
    else
    {
        al_string path = AL_STRING_INIT_STATIC();
        WCHAR *cwdbuf;

        /* Search the app-local directory. */
        if((cwdbuf=_wgetenv(L"ALSOFT_LOCAL_PATH")) && *cwdbuf != '\0')
        {
            al_string_copy_wcstr(&path, cwdbuf);
            if(is_slash(VECTOR_BACK(path)))
            {
                VECTOR_POP_BACK(path);
                *VECTOR_ITER_END(path) = 0;
            }
        }
        else if(!(cwdbuf=_wgetcwd(NULL, 0)))
            al_string_copy_cstr(&path, ".");
        else
        {
            al_string_copy_wcstr(&path, cwdbuf);
            if(is_slash(VECTOR_BACK(path)))
            {
                VECTOR_POP_BACK(path);
                *VECTOR_ITER_END(path) = 0;
            }
            free(cwdbuf);
        }
#define FIX_SLASH(i) do { if(*(i) == '/') *(i) = '\\'; } while(0)
        VECTOR_FOR_EACH(char, path, FIX_SLASH);
#undef FIX_SLASH
        RecurseDirectorySearch(al_string_get_cstr(path), wmatch, &results);

        /* Search the local and global data dirs. */
        for(i = 0;i < COUNTOF(ids);i++)
        {
            WCHAR buffer[PATH_MAX];
            if(SHGetSpecialFolderPathW(NULL, buffer, ids[i], FALSE) != FALSE)
            {
                al_string_copy_wcstr(&path, buffer);
                if(!is_slash(VECTOR_BACK(path)))
                    al_string_append_char(&path, '\\');
                al_string_append_cstr(&path, subdir);
#define FIX_SLASH(i) do { if(*(i) == '/') *(i) = '\\'; } while(0)
                VECTOR_FOR_EACH(char, path, FIX_SLASH);
#undef FIX_SLASH

                RecurseDirectorySearch(al_string_get_cstr(path), wmatch, &results);
            }
        }

        al_string_deinit(&path);
    }

    free(wmatch);
    ATOMIC_STORE(&search_lock, 0);

    return results;
}

#else

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


FILE *OpenDataFile(const char *fname, const char *subdir)
{
    char buffer[PATH_MAX] = "";
    const char *str, *next;
    FILE *f;

    if(fname[0] == '/')
    {
        if((f=al_fopen(fname, "rb")) != NULL)
        {
            TRACE("Opened %s\n", fname);
            return f;
        }
        WARN("Could not open %s\n", fname);
        return NULL;
    }

    if((f=al_fopen(fname, "rb")) != NULL)
    {
        TRACE("Opened %s\n", fname);
        return f;
    }

    if((str=getenv("XDG_DATA_HOME")) != NULL && str[0] != '\0')
        snprintf(buffer, sizeof(buffer), "%s/%s/%s", str, subdir, fname);
    else if((str=getenv("HOME")) != NULL && str[0] != '\0')
        snprintf(buffer, sizeof(buffer), "%s/.local/share/%s/%s", str, subdir, fname);
    if(buffer[0])
    {
        if((f=al_fopen(buffer, "rb")) != NULL)
        {
            TRACE("Opened %s\n", buffer);
            return f;
        }
    }

    if((str=getenv("XDG_DATA_DIRS")) == NULL || str[0] == '\0')
        str = "/usr/local/share/:/usr/share/";

    next = str;
    while((str=next) != NULL && str[0] != '\0')
    {
        size_t len;
        next = strchr(str, ':');

        if(!next)
            len = strlen(str);
        else
        {
            len = next - str;
            next++;
        }

        if(len > sizeof(buffer)-1)
            len = sizeof(buffer)-1;
        strncpy(buffer, str, len);
        buffer[len] = '\0';
        snprintf(buffer+len, sizeof(buffer)-len, "/%s/%s", subdir, fname);

        if((f=al_fopen(buffer, "rb")) != NULL)
        {
            TRACE("Opened %s\n", buffer);
            return f;
        }
    }
    WARN("Could not open %s/%s\n", subdir, fname);

    return NULL;
}


static int MatchFilter(const char *name, const char *match)
{
    int ret = 1;

    do {
        const char *p = strchr(match, '%');
        if(!p)
            ret = strcmp(match, name) == 0;
        else
        {
            size_t len = p-match;
            ret = strncmp(match, name, len) == 0;
            if(ret)
            {
                match += len;
                name += len;

                ++p;
                if(*p == 'r')
                {
                    char *end;
                    ret = strtoul(name, &end, 10) > 0;
                    if(ret) name = end;
                    ++p;
                }
                else if(*p == 's')
                {
                    const char *next = p+1;
                    if(*next != '\0' && *next != '%')
                    {
                        const char *next_p = strchr(next, '%');
                        const char *m;

                        if(!next_p)
                            m = strstr(name, next);
                        else
                        {
                            char *tmp = malloc(next_p - next + 1);
                            memcpy(tmp, next, next_p - next);
                            tmp[next_p - next] = 0;

                            m = strstr(name, tmp);

                            free(tmp);
                        }

                        ret = !!m;
                        if(ret)
                        {
                            size_t l;
                            if(next_p) l = next_p - next;
                            else l = strlen(next);

                            name = m + l;
                            next += l;
                        }
                    }
                    p = next;
                }
            }
        }

        match = p;
    } while(ret && match && *match);

    return ret;
}

static void RecurseDirectorySearch(const char *path, const char *match, vector_al_string *results)
{
    char *sep, *p;

    if(!match[0])
        return;

    sep = strrchr(match, '/');
    p = strchr(match, '%');

    if(!sep)
    {
        DIR *dir;

        TRACE("Searching %s for %s\n", path?path:"/", match);
        dir = opendir(path?path:"/");
        if(dir != NULL)
        {
            size_t base = VECTOR_SIZE(*results);
            struct dirent *dirent;
            while((dirent=readdir(dir)) != NULL)
            {
                al_string str;
                if(strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0 ||
                   !MatchFilter(dirent->d_name, match))
                    continue;

                AL_STRING_INIT(str);
                if(path) al_string_copy_cstr(&str, path);
                al_string_append_char(&str, '/');
                al_string_append_cstr(&str, dirent->d_name);
                TRACE("Got result %s\n", al_string_get_cstr(str));
                VECTOR_PUSH_BACK(*results, str);
            }
            closedir(dir);

            if(VECTOR_SIZE(*results) > base)
                qsort(VECTOR_ITER_BEGIN(*results)+base, VECTOR_SIZE(*results)-base,
                      sizeof(VECTOR_FRONT(*results)), StringSortCompare);
        }

        return;
    }

    if(!p || p-sep >= 0)
    {
        al_string npath = AL_STRING_INIT_STATIC();
        if(path) al_string_append_cstr(&npath, path);
        al_string_append_char(&npath, '/');
        al_string_append_range(&npath, match, sep);

        TRACE("Recursing into %s with %s\n", al_string_get_cstr(npath), sep+1);
        RecurseDirectorySearch(al_string_get_cstr(npath), sep+1, results);

        al_string_deinit(&npath);
        return;
    }

    sep = strchr(match, '/');
    if(sep-p >= 0) sep = NULL;
    for(;;)
    {
        char *next = strchr(sep?sep+1:match, '/');
        if(next-p < 0)
        {
            al_string npath = AL_STRING_INIT_STATIC();
            al_string nmatch = AL_STRING_INIT_STATIC();
            const char *tomatch;
            DIR *dir;

            if(!sep)
            {
                al_string_append_cstr(&npath, path?path:"/.");
                tomatch = match;
            }
            else
            {
                if(path) al_string_append_cstr(&npath, path);
                al_string_append_char(&npath, '/');
                al_string_append_range(&npath, match, sep);

                al_string_append_range(&nmatch, sep+1, next);
                tomatch = al_string_get_cstr(nmatch);
            }

            TRACE("Searching %s for %s\n", al_string_get_cstr(npath), tomatch);
            dir = opendir(path?path:"/");
            if(dir != NULL)
            {
                al_string ndir = AL_STRING_INIT_STATIC();
                struct dirent *dirent;

                while((dirent=readdir(dir)) != NULL)
                {
                    if(strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0 ||
                       !MatchFilter(dirent->d_name, tomatch))
                        continue;
                    al_string_copy(&ndir, npath);
                    al_string_append_char(&ndir, '/');
                    al_string_append_cstr(&ndir, dirent->d_name);
                    TRACE("Recursing %s with %s\n", al_string_get_cstr(ndir), next+1);
                    RecurseDirectorySearch(al_string_get_cstr(ndir), next+1, results);
                }
                closedir(dir);

                al_string_deinit(&ndir);
            }

            al_string_deinit(&nmatch);
            al_string_deinit(&npath);
            break;
        }

        sep = next;
    }
}

vector_al_string SearchDataFiles(const char *match, const char *subdir)
{
    static RefCount search_lock;
    vector_al_string results = VECTOR_INIT_STATIC();

    while(ATOMIC_EXCHANGE(uint, &search_lock, 1) == 1)
        althrd_yield();

    if(match[0] == '/')
        RecurseDirectorySearch(NULL, match+1, &results);
    else
    {
        al_string path = AL_STRING_INIT_STATIC();
        const char *str, *next;
        char cwdbuf[PATH_MAX];

        /* Search the app-local directory. */
        if((str=getenv("ALSOFT_LOCAL_PATH")) && *str != '\0')
        {
            strncpy(cwdbuf, str, sizeof(cwdbuf)-1);
            cwdbuf[sizeof(cwdbuf)-1] = '\0';
        }
        else if(!getcwd(cwdbuf, sizeof(cwdbuf)))
            strcpy(cwdbuf, ".");
        RecurseDirectorySearch(cwdbuf, match, &results);

        // Search local data dir
        if((str=getenv("XDG_DATA_HOME")) != NULL && str[0] != '\0')
        {
            al_string_append_cstr(&path, str);
            al_string_append_char(&path, '/');
            al_string_append_cstr(&path, subdir);
        }
        else if((str=getenv("HOME")) != NULL && str[0] != '\0')
        {
            al_string_append_cstr(&path, str);
            al_string_append_cstr(&path, "/.local/share/");
            al_string_append_cstr(&path, subdir);
        }
        if(!al_string_empty(path))
            RecurseDirectorySearch(al_string_get_cstr(path), match, &results);

        // Search global data dirs
        if((str=getenv("XDG_DATA_DIRS")) == NULL || str[0] == '\0')
            str = "/usr/local/share/:/usr/share/";

        next = str;
        while((str=next) != NULL && str[0] != '\0')
        {
            next = strchr(str, ':');
            if(!next)
                al_string_copy_cstr(&path, str);
            else
            {
                al_string_clear(&path);
                al_string_append_range(&path, str, next);
                ++next;
            }
            if(!al_string_empty(path))
            {
                al_string_append_char(&path, '/');
                al_string_append_cstr(&path, subdir);

                RecurseDirectorySearch(al_string_get_cstr(path), match, &results);
            }
        }

        al_string_deinit(&path);
    }

    ATOMIC_STORE(&search_lock, 0);

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


ALboolean vector_reserve(char *ptr, size_t base_size, size_t obj_size, size_t obj_count, ALboolean exact)
{
    vector_ *vecptr = (vector_*)ptr;
    if((*vecptr ? (*vecptr)->Capacity : 0) < obj_count)
    {
        size_t old_size = (*vecptr ? (*vecptr)->Size : 0);
        void *temp;

        /* Use the next power-of-2 size if we don't need to allocate the exact
         * amount. This is preferred when regularly increasing the vector since
         * it means fewer reallocations. Though it means it also wastes some
         * memory. */
        if(exact == AL_FALSE && obj_count < INT_MAX)
            obj_count = NextPowerOf2((ALuint)obj_count);

        /* Need to be explicit with the caller type's base size, because it
         * could have extra padding before the start of the array (that is,
         * sizeof(*vector_) may not equal base_size). */
        temp = realloc(*vecptr, base_size + obj_size*obj_count);
        if(temp == NULL) return AL_FALSE;

        *vecptr = temp;
        (*vecptr)->Capacity = obj_count;
        (*vecptr)->Size = old_size;
    }
    return AL_TRUE;
}

ALboolean vector_resize(char *ptr, size_t base_size, size_t obj_size, size_t obj_count)
{
    vector_ *vecptr = (vector_*)ptr;
    if(*vecptr || obj_count > 0)
    {
        if(!vector_reserve((char*)vecptr, base_size, obj_size, obj_count, AL_TRUE))
            return AL_FALSE;
        (*vecptr)->Size = obj_count;
    }
    return AL_TRUE;
}

ALboolean vector_insert(char *ptr, size_t base_size, size_t obj_size, void *ins_pos, const void *datstart, const void *datend)
{
    vector_ *vecptr = (vector_*)ptr;
    if(datstart != datend)
    {
        ptrdiff_t ins_elem = (*vecptr ? ((char*)ins_pos - ((char*)(*vecptr) + base_size)) :
                                        ((char*)ins_pos - (char*)NULL)) /
                             obj_size;
        ptrdiff_t numins = ((const char*)datend - (const char*)datstart) / obj_size;

        assert(numins > 0);
        if((size_t)numins + VECTOR_SIZE(*vecptr) < (size_t)numins ||
           !vector_reserve((char*)vecptr, base_size, obj_size, VECTOR_SIZE(*vecptr)+numins, AL_TRUE))
            return AL_FALSE;

        /* NOTE: ins_pos may have been invalidated if *vecptr moved. Use ins_elem instead. */
        if((size_t)ins_elem < (*vecptr)->Size)
        {
            memmove((char*)(*vecptr) + base_size + ((ins_elem+numins)*obj_size),
                    (char*)(*vecptr) + base_size + ((ins_elem       )*obj_size),
                    ((*vecptr)->Size-ins_elem)*obj_size);
        }
        memcpy((char*)(*vecptr) + base_size + (ins_elem*obj_size),
               datstart, numins*obj_size);
        (*vecptr)->Size += numins;
    }
    return AL_TRUE;
}


extern inline void al_string_deinit(al_string *str);
extern inline size_t al_string_length(const_al_string str);
extern inline ALboolean al_string_empty(const_al_string str);
extern inline const al_string_char_type *al_string_get_cstr(const_al_string str);

void al_string_clear(al_string *str)
{
    /* Reserve one more character than the total size of the string. This is to
     * ensure we have space to add a null terminator in the string data so it
     * can be used as a C-style string. */
    VECTOR_RESERVE(*str, 1);
    VECTOR_RESIZE(*str, 0);
    *VECTOR_ITER_END(*str) = 0;
}

static inline int al_string_compare(const al_string_char_type *str1, size_t str1len,
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
int al_string_cmp(const_al_string str1, const_al_string str2)
{
    return al_string_compare(&VECTOR_FRONT(str1), al_string_length(str1),
                             &VECTOR_FRONT(str2), al_string_length(str2));
}
int al_string_cmp_cstr(const_al_string str1, const al_string_char_type *str2)
{
    return al_string_compare(&VECTOR_FRONT(str1), al_string_length(str1),
                             str2, strlen(str2));
}

void al_string_copy(al_string *str, const_al_string from)
{
    size_t len = al_string_length(from);
    VECTOR_RESERVE(*str, len+1);
    VECTOR_RESIZE(*str, 0);
    VECTOR_INSERT(*str, VECTOR_ITER_END(*str), VECTOR_ITER_BEGIN(from), VECTOR_ITER_BEGIN(from)+len);
    *VECTOR_ITER_END(*str) = 0;
}

void al_string_copy_cstr(al_string *str, const al_string_char_type *from)
{
    size_t len = strlen(from);
    VECTOR_RESERVE(*str, len+1);
    VECTOR_RESIZE(*str, 0);
    VECTOR_INSERT(*str, VECTOR_ITER_END(*str), from, from+len);
    *VECTOR_ITER_END(*str) = 0;
}

void al_string_append_char(al_string *str, const al_string_char_type c)
{
    VECTOR_RESERVE(*str, al_string_length(*str)+2);
    VECTOR_PUSH_BACK(*str, c);
    *VECTOR_ITER_END(*str) = 0;
}

void al_string_append_cstr(al_string *str, const al_string_char_type *from)
{
    size_t len = strlen(from);
    if(len != 0)
    {
        VECTOR_RESERVE(*str, al_string_length(*str)+len+1);
        VECTOR_INSERT(*str, VECTOR_ITER_END(*str), from, from+len);
        *VECTOR_ITER_END(*str) = 0;
    }
}

void al_string_append_range(al_string *str, const al_string_char_type *from, const al_string_char_type *to)
{
    if(to != from)
    {
        VECTOR_RESERVE(*str, al_string_length(*str)+(to-from)+1);
        VECTOR_INSERT(*str, VECTOR_ITER_END(*str), from, to);
        *VECTOR_ITER_END(*str) = 0;
    }
}

#ifdef _WIN32
void al_string_copy_wcstr(al_string *str, const wchar_t *from)
{
    int len;
    if((len=WideCharToMultiByte(CP_UTF8, 0, from, -1, NULL, 0, NULL, NULL)) > 0)
    {
        VECTOR_RESERVE(*str, len);
        VECTOR_RESIZE(*str, len-1);
        WideCharToMultiByte(CP_UTF8, 0, from, -1, &VECTOR_FRONT(*str), len, NULL, NULL);
        *VECTOR_ITER_END(*str) = 0;
    }
}

void al_string_append_wcstr(al_string *str, const wchar_t *from)
{
    int len;
    if((len=WideCharToMultiByte(CP_UTF8, 0, from, -1, NULL, 0, NULL, NULL)) > 0)
    {
        size_t strlen = al_string_length(*str);
        VECTOR_RESERVE(*str, strlen+len);
        VECTOR_RESIZE(*str, strlen+len-1);
        WideCharToMultiByte(CP_UTF8, 0, from, -1, &VECTOR_FRONT(*str) + strlen, len, NULL, NULL);
        *VECTOR_ITER_END(*str) = 0;
    }
}

void al_string_append_wrange(al_string *str, const wchar_t *from, const wchar_t *to)
{
    int len;
    if((len=WideCharToMultiByte(CP_UTF8, 0, from, (int)(to-from), NULL, 0, NULL, NULL)) > 0)
    {
        size_t strlen = al_string_length(*str);
        VECTOR_RESERVE(*str, strlen+len+1);
        VECTOR_RESIZE(*str, strlen+len);
        WideCharToMultiByte(CP_UTF8, 0, from, (int)(to-from), &VECTOR_FRONT(*str) + strlen, len+1, NULL, NULL);
        *VECTOR_ITER_END(*str) = 0;
    }
}
#endif
