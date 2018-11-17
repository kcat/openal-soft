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
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include "threads.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "uintmap.h"


#ifndef UNUSED
#if defined(__cplusplus)
#define UNUSED(x)
#elif defined(__GNUC__)
#define UNUSED(x) UNUSED_##x __attribute__((unused))
#elif defined(__LCLINT__)
#define UNUSED(x) /*@unused@*/ x
#else
#define UNUSED(x) x
#endif
#endif


#define THREAD_STACK_SIZE (2*1024*1024) /* 2MB */

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>


/* An associative map of uint:void* pairs. The key is the unique Thread ID and
 * the value is the thread HANDLE. The thread ID is passed around as the
 * althrd_t since there is only one ID per thread, whereas a thread may be
 * referenced by multiple different HANDLEs. This map allows retrieving the
 * original handle which is needed to join the thread and get its return value.
 */
static ThrSafeMap<DWORD,HANDLE> ThrdIdHandle{};


void althrd_setname(althrd_t thr, const char *name)
{
#if defined(_MSC_VER)
#define MS_VC_EXCEPTION 0x406D1388
#pragma pack(push,8)
    struct {
        DWORD dwType;     // Must be 0x1000.
        LPCSTR szName;    // Pointer to name (in user addr space).
        DWORD dwThreadID; // Thread ID (-1=caller thread).
        DWORD dwFlags;    // Reserved for future use, must be zero.
    } info;
#pragma pack(pop)
    info.dwType = 0x1000;
    info.szName = name;
    info.dwThreadID = thr;
    info.dwFlags = 0;

    __try {
        RaiseException(MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info);
    }
    __except(EXCEPTION_CONTINUE_EXECUTION) {
    }
#undef MS_VC_EXCEPTION
#else
    (void)thr;
    (void)name;
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
    DWORD thrid;
    HANDLE hdl;

    cntr = static_cast<thread_cntr*>(malloc(sizeof(*cntr)));
    if(!cntr) return althrd_nomem;

    cntr->func = func;
    cntr->arg = arg;

    hdl = CreateThread(NULL, THREAD_STACK_SIZE, althrd_starter, cntr, 0, &thrid);
    if(!hdl)
    {
        free(cntr);
        return althrd_error;
    }
    ThrdIdHandle.InsertEntry(thrid, hdl);

    *thr = thrid;
    return althrd_success;
}

int althrd_detach(althrd_t thr)
{
    HANDLE hdl = ThrdIdHandle.RemoveKey(thr);
    if(!hdl) return althrd_error;

    CloseHandle(hdl);
    return althrd_success;
}

int althrd_join(althrd_t thr, int *res)
{
    DWORD code;

    HANDLE hdl = ThrdIdHandle.RemoveKey(thr);
    if(!hdl) return althrd_error;

    WaitForSingleObject(hdl, INFINITE);
    GetExitCodeThread(hdl, &code);
    CloseHandle(hdl);

    if(res != NULL)
        *res = (int)code;
    return althrd_success;
}


int almtx_init(almtx_t *mtx, int type)
{
    if(!mtx) return althrd_error;

    type &= ~almtx_recursive;
    if(type != almtx_plain)
        return althrd_error;

    InitializeCriticalSection(mtx);
    return althrd_success;
}

void almtx_destroy(almtx_t *mtx)
{
    DeleteCriticalSection(mtx);
}


int alsem_init(alsem_t *sem, unsigned int initial)
{
    *sem = CreateSemaphore(NULL, initial, INT_MAX, NULL);
    if(*sem != NULL) return althrd_success;
    return althrd_error;
}

void alsem_destroy(alsem_t *sem)
{
    CloseHandle(*sem);
}

int alsem_post(alsem_t *sem)
{
    DWORD ret = ReleaseSemaphore(*sem, 1, NULL);
    if(ret) return althrd_success;
    return althrd_error;
}

int alsem_wait(alsem_t *sem)
{
    DWORD ret = WaitForSingleObject(*sem, INFINITE);
    if(ret == WAIT_OBJECT_0) return althrd_success;
    return althrd_error;
}

int alsem_trywait(alsem_t *sem)
{
    DWORD ret = WaitForSingleObject(*sem, 0);
    if(ret == WAIT_OBJECT_0) return althrd_success;
    if(ret == WAIT_TIMEOUT) return althrd_busy;
    return althrd_error;
}


void althrd_deinit(void)
{
}

#else

#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif


void althrd_setname(althrd_t thr, const char *name)
{
#if defined(HAVE_PTHREAD_SETNAME_NP)
#if defined(PTHREAD_SETNAME_NP_ONE_PARAM)
    if(althrd_equal(thr, althrd_current()))
        pthread_setname_np(name);
#elif defined(PTHREAD_SETNAME_NP_THREE_PARAMS)
    pthread_setname_np(thr, "%s", (void*)name);
#else
    pthread_setname_np(thr, name);
#endif
#elif defined(HAVE_PTHREAD_SET_NAME_NP)
    pthread_set_name_np(thr, name);
#else
    (void)thr;
    (void)name;
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
    size_t stackmult = 1;
    int err;

    cntr = static_cast<thread_cntr*>(malloc(sizeof(*cntr)));
    if(!cntr) return althrd_nomem;

    if(pthread_attr_init(&attr) != 0)
    {
        free(cntr);
        return althrd_error;
    }
retry_stacksize:
    if(pthread_attr_setstacksize(&attr, THREAD_STACK_SIZE*stackmult) != 0)
    {
        pthread_attr_destroy(&attr);
        free(cntr);
        return althrd_error;
    }

    cntr->func = func;
    cntr->arg = arg;
    if((err=pthread_create(thr, &attr, althrd_starter, cntr)) == 0)
    {
        pthread_attr_destroy(&attr);
        return althrd_success;
    }

    if(err == EINVAL)
    {
        /* If an invalid stack size, try increasing it (limit x4, 8MB). */
        if(stackmult < 4)
        {
            stackmult *= 2;
            goto retry_stacksize;
        }
        /* If still nothing, try defaults and hope they're good enough. */
        if(pthread_create(thr, NULL, althrd_starter, cntr) == 0)
        {
            pthread_attr_destroy(&attr);
            return althrd_success;
        }
    }
    pthread_attr_destroy(&attr);
    free(cntr);
    return althrd_error;
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

    if(pthread_join(thr, &code) != 0)
        return althrd_error;
    if(res != NULL)
        *res = (int)(intptr_t)code;
    return althrd_success;
}


int almtx_init(almtx_t *mtx, int type)
{
    int ret;

    if(!mtx) return althrd_error;
    if((type&~almtx_recursive) != 0)
        return althrd_error;

    if(type == almtx_plain)
        ret = pthread_mutex_init(mtx, NULL);
    else
    {
        pthread_mutexattr_t attr;

        ret = pthread_mutexattr_init(&attr);
        if(ret) return althrd_error;

        if(type == almtx_recursive)
        {
            ret = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
#ifdef HAVE_PTHREAD_MUTEXATTR_SETKIND_NP
            if(ret != 0)
                ret = pthread_mutexattr_setkind_np(&attr, PTHREAD_MUTEX_RECURSIVE);
#endif
        }
        else
            ret = 1;
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


#ifdef __APPLE__

int alsem_init(alsem_t *sem, unsigned int initial)
{
    *sem = dispatch_semaphore_create(initial);
    return *sem ? althrd_success : althrd_error;
}

void alsem_destroy(alsem_t *sem)
{
    dispatch_release(*sem);
}

int alsem_post(alsem_t *sem)
{
    dispatch_semaphore_signal(*sem);
    return althrd_success;
}

int alsem_wait(alsem_t *sem)
{
    dispatch_semaphore_wait(*sem, DISPATCH_TIME_FOREVER);
    return althrd_success;
}

int alsem_trywait(alsem_t *sem)
{
    long value = dispatch_semaphore_wait(*sem, DISPATCH_TIME_NOW);
    return value == 0 ? althrd_success : althrd_busy;
}

#else /* !__APPLE__ */

int alsem_init(alsem_t *sem, unsigned int initial)
{
    if(sem_init(sem, 0, initial) == 0)
        return althrd_success;
    return althrd_error;
}

void alsem_destroy(alsem_t *sem)
{
    sem_destroy(sem);
}

int alsem_post(alsem_t *sem)
{
    if(sem_post(sem) == 0)
        return althrd_success;
    return althrd_error;
}

int alsem_wait(alsem_t *sem)
{
    if(sem_wait(sem) == 0) return althrd_success;
    if(errno == EINTR) return -2;
    return althrd_error;
}

int alsem_trywait(alsem_t *sem)
{
    if(sem_trywait(sem) == 0) return althrd_success;
    if(errno == EWOULDBLOCK) return althrd_busy;
    if(errno == EINTR) return -2;
    return althrd_error;
}

#endif /* __APPLE__ */

void althrd_deinit(void)
{ }

#endif
