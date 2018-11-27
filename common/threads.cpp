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


void althrd_setname(const char *name)
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
    info.dwThreadID = -1;
    info.dwFlags = 0;

    __try {
        RaiseException(MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info);
    }
    __except(EXCEPTION_CONTINUE_EXECUTION) {
    }
#undef MS_VC_EXCEPTION
#else
    (void)name;
#endif
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

#else

#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif


void althrd_setname(const char *name)
{
#if defined(HAVE_PTHREAD_SETNAME_NP)
#if defined(PTHREAD_SETNAME_NP_ONE_PARAM)
    pthread_setname_np(name);
#elif defined(PTHREAD_SETNAME_NP_THREE_PARAMS)
    pthread_setname_np(pthread_self(), "%s", (void*)name);
#else
    pthread_setname_np(pthread_self(), name);
#endif
#elif defined(HAVE_PTHREAD_SET_NAME_NP)
    pthread_set_name_np(pthread_self(), name);
#else
    (void)name;
#endif
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

#endif
