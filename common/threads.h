#ifndef AL_THREADS_H
#define AL_THREADS_H

#include <time.h>

#if defined(__GNUC__) && defined(__i386__)
/* force_align_arg_pointer is required for proper function arguments aligning
 * when SSE code is used. Some systems (Windows, QNX) do not guarantee our
 * thread functions will be properly aligned on the stack, even though GCC may
 * generate code with the assumption that it is. */
#define FORCE_ALIGN __attribute__((force_align_arg_pointer))
#else
#define FORCE_ALIGN
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    althrd_success = 0,
    althrd_error,
    althrd_nomem,
    althrd_timedout,
    althrd_busy
};

enum {
    almtx_plain = 0,
    almtx_recursive = 1,
};

typedef int (*althrd_start_t)(void*);


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>


typedef DWORD althrd_t;
typedef CRITICAL_SECTION almtx_t;
typedef HANDLE alsem_t;
typedef LONG alonce_flag;

#define AL_ONCE_FLAG_INIT 0

void alcall_once(alonce_flag *once, void (*callback)(void));

void althrd_deinit(void);

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

#else

#include <stdint.h>
#include <errno.h>
#include <pthread.h>
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#else /* !__APPLE__ */
#include <semaphore.h>
#endif /* __APPLE__ */


typedef pthread_t althrd_t;
typedef pthread_mutex_t almtx_t;
#ifdef __APPLE__
typedef dispatch_semaphore_t alsem_t;
#else /* !__APPLE__ */
typedef sem_t alsem_t;
#endif /* __APPLE__ */
typedef pthread_once_t alonce_flag;

#define AL_ONCE_FLAG_INIT PTHREAD_ONCE_INIT


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


inline int almtx_lock(almtx_t *mtx)
{
    if(pthread_mutex_lock(mtx) != 0)
        return althrd_error;
    return althrd_success;
}

inline int almtx_unlock(almtx_t *mtx)
{
    if(pthread_mutex_unlock(mtx) != 0)
        return althrd_error;
    return althrd_success;
}

inline int almtx_trylock(almtx_t *mtx)
{
    int ret = pthread_mutex_trylock(mtx);
    switch(ret)
    {
        case 0: return althrd_success;
        case EBUSY: return althrd_busy;
    }
    return althrd_error;
}


inline void alcall_once(alonce_flag *once, void (*callback)(void))
{
    pthread_once(once, callback);
}


inline void althrd_deinit(void) { }

#endif


int althrd_create(althrd_t *thr, althrd_start_t func, void *arg);
int althrd_detach(althrd_t thr);
int althrd_join(althrd_t thr, int *res);
void althrd_setname(althrd_t thr, const char *name);

int almtx_init(almtx_t *mtx, int type);
void almtx_destroy(almtx_t *mtx);

int alsem_init(alsem_t *sem, unsigned int initial);
void alsem_destroy(alsem_t *sem);
int alsem_post(alsem_t *sem);
int alsem_wait(alsem_t *sem);
int alsem_trywait(alsem_t *sem);

#ifdef __cplusplus
} // extern "C"

#include <mutex>

/* Add specializations for std::lock_guard and std::unique_lock which take an
 * almtx_t and call the appropriate almtx_* functions.
 */
namespace std {

template<>
class lock_guard<almtx_t> {
    almtx_t &mMtx;

public:
    using mutex_type = almtx_t;

    explicit lock_guard(almtx_t &mtx) : mMtx(mtx) { almtx_lock(&mMtx); }
    lock_guard(almtx_t &mtx, std::adopt_lock_t) : mMtx(mtx) { }
    ~lock_guard() { almtx_unlock(&mMtx); }

    lock_guard(const lock_guard&) = delete;
    lock_guard& operator=(const lock_guard&) = delete;
};

template<>
class unique_lock<almtx_t> {
    almtx_t *mMtx{nullptr};
    bool mLocked{false};

public:
    using mutex_type = almtx_t;

    explicit unique_lock(almtx_t &mtx) : mMtx(&mtx) { almtx_lock(mMtx); mLocked = true; }
    unique_lock(unique_lock&& rhs) : mMtx(rhs.mMtx), mLocked(rhs.mLocked)
    { rhs.mMtx = nullptr; rhs.mLocked = false; }
    ~unique_lock() { if(mLocked) almtx_unlock(mMtx); }

    unique_lock& operator=(const unique_lock&) = delete;
    unique_lock& operator=(unique_lock&& rhs)
    {
        if(mLocked)
            almtx_unlock(mMtx);
        mMtx = rhs.mMtx; rhs.mMtx = nullptr;
        mLocked = rhs.mLocked; rhs.mLocked = false;
        return *this;
    }

    void lock()
    {
        almtx_lock(mMtx);
        mLocked = true;
    }
    void unlock()
    {
        mLocked = false;
        almtx_unlock(mMtx);
    }
};

} // namespace std
#endif

#endif /* AL_THREADS_H */
