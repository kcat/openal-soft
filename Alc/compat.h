#ifndef AL_COMPAT_H
#define AL_COMPAT_H

#include "AL/al.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef DWORD pthread_key_t;
int pthread_key_create(pthread_key_t *key, void (*callback)(void*));
int pthread_key_delete(pthread_key_t key);
void *pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, void *val);

#define HAVE_DYNLOAD 1
void *LoadLib(const char *name);
void CloseLib(void *handle);
void *GetSymbol(void *handle, const char *name);

WCHAR *strdupW(const WCHAR *str);

typedef LONG pthread_once_t;
#define PTHREAD_ONCE_INIT 0
void pthread_once(pthread_once_t *once, void (*callback)(void));

static inline int sched_yield(void)
{ SwitchToThread(); return 0; }

#else

#include <pthread.h>

typedef pthread_mutex_t CRITICAL_SECTION;
void InitializeCriticalSection(CRITICAL_SECTION *cs);
void DeleteCriticalSection(CRITICAL_SECTION *cs);
void EnterCriticalSection(CRITICAL_SECTION *cs);
void LeaveCriticalSection(CRITICAL_SECTION *cs);

ALuint timeGetTime(void);
void Sleep(ALuint t);

#if defined(HAVE_DLFCN_H)
#define HAVE_DYNLOAD 1
void *LoadLib(const char *name);
void CloseLib(void *handle);
void *GetSymbol(void *handle, const char *name);
#endif

#endif

#endif /* AL_COMPAT_H */
