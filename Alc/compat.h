#ifndef AL_COMPAT_H
#define AL_COMPAT_H

#include "AL/al.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

WCHAR *strdupW(const WCHAR *str);

/* Opens a file with standard I/O. The filename is expected to be UTF-8. */
FILE *al_fopen(const char *fname, const char *mode);

#define HAVE_DYNLOAD 1

#else

#include <pthread.h>

#define al_fopen(_n, _m) fopen((_n), (_m))

#if defined(HAVE_DLFCN_H) && !defined(IN_IDE_PARSER)
#define HAVE_DYNLOAD 1
#endif

#endif

#ifdef HAVE_DYNLOAD
void *LoadLib(const char *name);
void CloseLib(void *handle);
void *GetSymbol(void *handle, const char *name);
#endif

#endif /* AL_COMPAT_H */
