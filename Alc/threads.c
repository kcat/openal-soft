/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
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
 *  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA  02111-1307, USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include "threads.h"

#include <stdlib.h>
#include <errno.h>

#include "alMain.h"
#include "alThunk.h"


extern inline int althrd_equal(althrd_t thr0, althrd_t thr1);
extern inline void althrd_exit(int res);
extern inline void althrd_yield(void);

extern inline int almtx_lock(almtx_t *mtx);
extern inline int almtx_unlock(almtx_t *mtx);
extern inline int almtx_trylock(almtx_t *mtx);

extern inline void al_nssleep(time_t sec, long nsec);


#define THREAD_STACK_SIZE (1*1024*1024) /* 1MB */

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>


void SetThreadName(const char *name)
{
#if defined(_MSC_VER)
#define MS_VC_EXCEPTION 0x406D1388
    struct {
        DWORD dwType;     // Must be 0x1000.
        LPCSTR szName;    // Pointer to name (in user addr space).
        DWORD dwThreadID; // Thread ID (-1=caller thread).
        DWORD dwFlags;    // Reserved for future use, must be zero.
    } info;
    info.dwType = 0x1000;
    info.szName = name;
    info.dwThreadID = -1;
    info.dwFlags = 0;

    __try {
        RaiseException(MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(DWORD), (ULONG_PTR*)&info);
    }
    __except(EXCEPTION_CONTINUE_EXECUTION) {
    }
#undef MS_VC_EXCEPTION
#else
    TRACE("Can't set thread %04lx name to \"%s\"\n", GetCurrentThreadId(), name);
#endif
}


typedef struct thread_cntr {
    althrd_start_t func;
    void *arg;
} thread_cntr;

static DWORD WINAPI althrd_starter(void *arg)
{
    thread_cntr cntr;
    memcpy(&cntr, arg, sizeof(cntr));
    free(arg);

    return (DWORD)((*cntr.func)(cntr.arg));
}


int althrd_create(althrd_t *thr, althrd_start_t func, void *arg)
{
    thread_cntr *cntr;
    DWORD dummy;
    HANDLE hdl;

    cntr = malloc(sizeof(*cntr));
    if(!cntr) return althrd_nomem;

    cntr->func = func;
    cntr->arg = arg;

    hdl = CreateThread(NULL, THREAD_STACK_SIZE, althrd_starter, cntr, 0, &dummy);
    if(!hdl)
    {
        free(cntr);
        return althrd_error;
    }

    *thr = hdl;
    return althrd_success;
}

int althrd_detach(althrd_t thr)
{
    if(!thr) return althrd_error;
    CloseHandle(thr);

    return althrd_success;
}

int althrd_join(althrd_t thr, int *res)
{
    DWORD code;

    if(!thr) return althrd_error;

    WaitForSingleObject(thr, INFINITE);
    GetExitCodeThread(thr, &code);
    CloseHandle(thr);

    *res = (int)code;
    return althrd_success;
}

int althrd_sleep(const struct timespec *ts, struct timespec* UNUSED(rem))
{
    DWORD msec;
    msec  = ts->tv_sec * 1000;
    msec += (ts->tv_nsec+999999) / 1000000;
    Sleep(msec);
    return 0;
}


int almtx_init(almtx_t *mtx, int type)
{
    if(!mtx) return althrd_error;
    type &= ~(almtx_recursive|almtx_timed|almtx_normal|almtx_errorcheck);
    if(type != 0) return althrd_error;

    InitializeCriticalSection(mtx);
    return althrd_success;
}

void almtx_destroy(almtx_t *mtx)
{
    DeleteCriticalSection(mtx);
}

int almtx_timedlock(almtx_t *mtx, const struct timespec *ts)
{
    DWORD start, timelen;
    int ret;

    if(!mtx || !ts)
        return althrd_error;

    timelen  = ts->tv_sec * 1000;
    timelen += (ts->tv_nsec+999999) / 1000000;

    start = timeGetTime();
    while((ret=almtx_trylock(mtx)) == althrd_busy)
    {
        DWORD now = timeGetTime();
        if(now-start >= timelen)
            break;
        SwitchToThread();
    }

    return ret;
}


#else

#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif


extern inline althrd_t althrd_current(void);
extern inline int althrd_sleep(const struct timespec *ts, struct timespec *rem);


void SetThreadName(const char *name)
{
#if defined(HAVE_PTHREAD_SETNAME_NP)
#if defined(__GNUC__)
    if(pthread_setname_np(pthread_self(), name) != 0)
#elif defined(__APPLE__)
    if(pthread_setname_np(name) != 0)
#endif
        WARN("Failed to set thread name to \"%s\": %s\n", name, strerror(errno));
#elif defined(HAVE_PTHREAD_SET_NAME_NP)
    pthread_set_name_np(pthread_self(), name);
#else
    TRACE("Can't set thread name to \"%s\"\n", name);
#endif
}


typedef struct thread_cntr {
    althrd_start_t func;
    void *arg;
} thread_cntr;

static void *althrd_starter(void *arg)
{
    thread_cntr cntr;
    memcpy(&cntr, arg, sizeof(cntr));
    free(arg);

    return (void*)(intptr_t)((*cntr.func)(cntr.arg));
}


int althrd_create(althrd_t *thr, althrd_start_t func, void *arg)
{
    thread_cntr *cntr;
    pthread_attr_t attr;

    cntr = malloc(sizeof(*cntr));
    if(!cntr) return althrd_nomem;

    if(pthread_attr_init(&attr) != 0)
    {
        free(cntr);
        return althrd_error;
    }
    if(pthread_attr_setstacksize(&attr, THREAD_STACK_SIZE) != 0)
    {
        pthread_attr_destroy(&attr);
        free(cntr);
        return althrd_error;
    }

    cntr->func = func;
    cntr->arg = arg;
    if(pthread_create(thr, &attr, althrd_starter, cntr) != 0)
    {
        pthread_attr_destroy(&attr);
        free(cntr);
        return althrd_error;
    }
    pthread_attr_destroy(&attr);

    return althrd_success;
}

int althrd_detach(althrd_t thr)
{
    if(pthread_detach(thr) != 0)
        return althrd_error;
    return althrd_success;
}

int althrd_join(althrd_t thr, int *res)
{
    void *code;

    if(!res) return althrd_error;

    if(pthread_join(thr, &code) != 0)
        return althrd_error;
    *res = (int)(intptr_t)code;
    return althrd_success;
}


int almtx_init(almtx_t *mtx, int type)
{
    int ret;

    if(!mtx) return althrd_error;
    if((type&~(almtx_normal|almtx_recursive|almtx_timed|almtx_errorcheck)) != 0)
        return althrd_error;

    type &= ~almtx_timed;
    if(type == almtx_plain)
        ret = pthread_mutex_init(mtx, NULL);
    else
    {
        pthread_mutexattr_t attr;

        ret = pthread_mutexattr_init(&attr);
        if(ret) return althrd_error;

        switch(type)
        {
        case almtx_normal:
            ret = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
            break;
        case almtx_recursive:
            ret = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
#ifdef HAVE_PTHREAD_NP_H
            if(ret != 0)
                ret = pthread_mutexattr_setkind_np(&attr, PTHREAD_MUTEX_RECURSIVE);
#endif
            break;
        case almtx_errorcheck:
            ret = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
            break;
        default:
            ret = 1;
        }
        if(ret == 0)
            ret = pthread_mutex_init(mtx, &attr);
        pthread_mutexattr_destroy(&attr);
    }
    return ret ? althrd_error : althrd_success;
}

void almtx_destroy(almtx_t *mtx)
{
    pthread_mutex_destroy(mtx);
}

int almtx_timedlock(almtx_t *mtx, const struct timespec *ts)
{
    if(!mtx || !ts)
        return althrd_error;

    if(pthread_mutex_timedlock(mtx, ts) != 0)
        return althrd_busy;
    return althrd_success;
}

#endif
