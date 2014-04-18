#ifndef AL_THREADS_H
#define AL_THREADS_H

#include <time.h>


enum {
    althrd_success = 0,
    althrd_nomem,
    althrd_timedout,
    althrd_busy,
    althrd_error
};

enum {
    almtx_plain = 0,
    almtx_recursive = 1,
    almtx_timed = 2
};

typedef int (*althrd_start_t)(void*);
typedef void (*altss_dtor_t)(void*);


#define AL_TIME_UTC 1


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>


#ifndef __MINGW32__
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
#endif

typedef DWORD althrd_t;
typedef CRITICAL_SECTION almtx_t;
typedef DWORD altss_t;
typedef LONG alonce_flag;

#define AL_ONCE_INIT 0

int althrd_sleep(const struct timespec *ts, struct timespec *rem);
void alcall_once(alonce_flag *once, void (*callback)(void));


inline althrd_t althrd_current(void)
{
    return GetCurrentThreadId();
}

inline int althrd_equal(althrd_t thr0, althrd_t thr1)
{
    return thr0 == thr1;
}

inline void althrd_exit(int res)
{
    ExitThread(res);
}

inline void althrd_yield(void)
{
    SwitchToThread();
}


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


inline void *altss_get(altss_t tss_id)
{
    return TlsGetValue(tss_id);
}

inline int altss_set(altss_t tss_id, void *val)
{
    TlsSetValue(tss_id, val);
    return althrd_success;
}

#else

#include <stdint.h>
#include <errno.h>
#include <pthread.h>


typedef pthread_t althrd_t;
typedef pthread_mutex_t almtx_t;
typedef pthread_key_t altss_t;
typedef pthread_once_t alonce_flag;

#define AL_ONCE_INIT PTHREAD_ONCE_INIT


inline althrd_t althrd_current(void)
{
    return pthread_self();
}

inline int althrd_equal(althrd_t thr0, althrd_t thr1)
{
    return pthread_equal(thr0, thr1);
}

inline void althrd_exit(int res)
{
    pthread_exit((void*)(intptr_t)res);
}

inline void althrd_yield(void)
{
    sched_yield();
}

inline int althrd_sleep(const struct timespec *ts, struct timespec *rem)
{
    int ret = nanosleep(ts, rem);
    if(ret != 0)
    {
        ret = ((errno==EINTR) ? -1 : -2);
        errno = 0;
    }
    return ret;
}


inline int almtx_lock(almtx_t *mtx)
{
    int ret = EINVAL;
    if(mtx != NULL)
        ret = pthread_mutex_lock(mtx);
    switch(ret)
    {
        case 0: return althrd_success;
        case EAGAIN: return althrd_busy;
    }
    return althrd_error;
}

inline int almtx_unlock(almtx_t *mtx)
{
    if(!mtx) return althrd_error;
    pthread_mutex_unlock(mtx);
    return althrd_success;
}

inline int almtx_trylock(almtx_t *mtx)
{
    int ret = EINVAL;
    if(mtx != NULL)
        ret = pthread_mutex_trylock(mtx);
    switch(ret)
    {
        case 0: return althrd_success;
        case EAGAIN:
        case EBUSY: return althrd_busy;
    }
    return althrd_error;
}


inline void *altss_get(altss_t tss_id)
{
    return pthread_getspecific(tss_id);
}

inline int altss_set(altss_t tss_id, void *val)
{
    if(pthread_setspecific(tss_id, val) != 0)
        return althrd_error;
    return althrd_success;
}


inline void alcall_once(alonce_flag *once, void (*callback)(void))
{
    pthread_once(once, callback);
}

#endif


int althrd_create(althrd_t *thr, althrd_start_t func, void *arg);
int althrd_detach(althrd_t thr);
int althrd_join(althrd_t thr, int *res);
void althrd_setname(althrd_t thr, const char *name);

int almtx_init(almtx_t *mtx, int type);
void almtx_destroy(almtx_t *mtx);
int almtx_timedlock(almtx_t *mtx, const struct timespec *ts);

int altss_create(altss_t *tss_id, altss_dtor_t callback);
void altss_delete(altss_t tss_id);

int altimespec_get(struct timespec *ts, int base);

void al_nssleep(time_t sec, long nsec);

#endif /* AL_THREADS_H */
