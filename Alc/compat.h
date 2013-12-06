#ifndef AL_COMPAT_H
#define AL_COMPAT_H

#include "AL/al.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef DWORD althread_key_t;
int althread_key_create(althread_key_t *key, void (*callback)(void*));
int althread_key_delete(althread_key_t key);
void *althread_getspecific(althread_key_t key);
int althread_setspecific(althread_key_t key, void *val);

typedef LONG althread_once_t;
#define ALTHREAD_ONCE_INIT 0
void althread_once(althread_once_t *once, void (*callback)(void));

inline int alsched_yield(void)
{ SwitchToThread(); return 0; }

WCHAR *strdupW(const WCHAR *str);

#define HAVE_DYNLOAD 1

#else

#include <pthread.h>

typedef pthread_mutex_t CRITICAL_SECTION;
void InitializeCriticalSection(CRITICAL_SECTION *cs);
void DeleteCriticalSection(CRITICAL_SECTION *cs);
void EnterCriticalSection(CRITICAL_SECTION *cs);
void LeaveCriticalSection(CRITICAL_SECTION *cs);

ALuint timeGetTime(void);
void Sleep(ALuint t);

#define althread_key_t pthread_key_t
#define althread_key_create pthread_key_create
#define althread_key_delete pthread_key_delete
#define althread_getspecific pthread_getspecific
#define althread_setspecific pthread_setspecific

#define althread_once_t pthread_once_t
#define ALTHREAD_ONCE_INIT PTHREAD_ONCE_INIT
#define althread_once pthread_once

#define alsched_yield sched_yield

#if defined(HAVE_DLFCN_H)
#define HAVE_DYNLOAD 1
#endif

#endif

#ifdef HAVE_DYNLOAD
void *LoadLib(const char *name);
void CloseLib(void *handle);
void *GetSymbol(void *handle, const char *name);
#endif

#endif /* AL_COMPAT_H */
