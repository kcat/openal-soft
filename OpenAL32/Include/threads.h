#ifndef AL_THREADS_H
#define AL_THREADS_H

#include "alMain.h"

struct althread_info;
typedef struct althread_info* althread_t;

ALboolean StartThread(althread_t *out, ALuint (*func)(ALvoid*), ALvoid *ptr);
ALuint StopThread(althread_t thread);

void SetThreadName(const char *name);


enum {
    althrd_success = 0,
    althrd_timeout,
    althrd_error,
    althrd_busy,
    althrd_nomem
};

enum {
    almtx_plain = 0,
    almtx_recursive = 1,
    almtx_timed = 2,
    almtx_normal = 4,
    almtx_errorcheck = 8
};

typedef struct alxtime {
    time_t sec;
    long nsec;
} alxtime;


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef CRITICAL_SECTION almtx_t;

inline int almtx_lock(almtx_t *mtx)
{
    if(!mtx) return althrd_error;
    EnterCriticalSection(mtx);
    return althrd_success;
}

inline int almtx_unlock(almtx_t *mtx)
{
    if(!mtx) return althrd_error;
    LeaveCriticalSection(mtx);
    return althrd_success;
}

inline int almtx_trylock(almtx_t *mtx)
{
    if(!mtx) return althrd_error;
    if(!TryEnterCriticalSection(mtx))
        return althrd_busy;
    return althrd_success;
}

#else

#include <pthread.h>


typedef pthread_mutex_t almtx_t;

inline int almtx_lock(almtx_t *mtx)
{
    if(!mtx) return althrd_error;
    pthread_mutex_lock(mtx);
    return althrd_success;
}

inline int almtx_unlock(almtx_t *mtx)
{
    if(!mtx) return althrd_error;
    pthread_mutex_unlock(mtx);
    return althrd_success;
}

inline int almtx_trylock(almtx_t *mtx)
{
    if(!mtx) return althrd_error;
    if(pthread_mutex_trylock(mtx) != 0)
        return althrd_busy;
    return althrd_success;
}

#endif

int almtx_init(almtx_t *mtx, int type);
void almtx_destroy(almtx_t *mtx);
int almtx_timedlock(almtx_t *mtx, const alxtime *xt);

#endif /* AL_THREADS_H */
