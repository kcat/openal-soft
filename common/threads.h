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
#include <mutex>

extern "C" {
#endif

enum {
    althrd_success = 0,
    althrd_error,
    althrd_nomem,
    althrd_timedout,
    althrd_busy
};


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef HANDLE alsem_t;


inline void althrd_yield(void)
{
    SwitchToThread();
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

#ifdef __APPLE__
typedef dispatch_semaphore_t alsem_t;
#else /* !__APPLE__ */
typedef sem_t alsem_t;
#endif /* __APPLE__ */


inline void althrd_yield(void)
{
    sched_yield();
}


#endif

void althrd_setname(const char *name);

int alsem_init(alsem_t *sem, unsigned int initial);
void alsem_destroy(alsem_t *sem);
int alsem_post(alsem_t *sem);
int alsem_wait(alsem_t *sem);
int alsem_trywait(alsem_t *sem);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* AL_THREADS_H */
