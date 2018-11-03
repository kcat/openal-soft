#ifndef AL_COMPAT_H
#define AL_COMPAT_H

#include "alstring.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

WCHAR *strdupW(const WCHAR *str);

/* Opens a file with standard I/O. The filename is expected to be UTF-8. */
FILE *al_fopen(const char *fname, const char *mode);

#define HAVE_DYNLOAD 1

#ifdef __cplusplus
} // extern "C"

#include <string>

inline std::string wstr_to_utf8(const WCHAR *wstr)
{
    std::string ret;

    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if(len > 0)
    {
        ret.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &ret[0], len, nullptr, nullptr);
        ret.pop_back();
    }

    return ret;
}

extern "C" {
#endif /* __cplusplus */

#else

#define al_fopen fopen

#if defined(HAVE_DLFCN_H)
#define HAVE_DYNLOAD 1
#endif

#endif

struct FileMapping {
#ifdef _WIN32
    HANDLE file;
    HANDLE fmap;
#else
    int fd;
#endif
    void *ptr;
    size_t len;
};
struct FileMapping MapFileToMem(const char *fname);
void UnmapFileMem(const struct FileMapping *mapping);

void GetProcBinary(al_string *path, al_string *fname);

#ifdef HAVE_DYNLOAD
void *LoadLib(const char *name);
void CloseLib(void *handle);
void *GetSymbol(void *handle, const char *name);
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AL_COMPAT_H */
