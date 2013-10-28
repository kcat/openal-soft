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

#define THREAD_STACK_SIZE (1*1024*1024) /* 1MB */

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct althread_info {
    ALuint (*func)(ALvoid*);
    ALvoid *ptr;
    HANDLE hdl;
} althread_info;

static DWORD CALLBACK StarterFunc(void *ptr)
{
    althread_info *inf = (althread_info*)ptr;
    ALuint ret;

    ret = inf->func(inf->ptr);
    ExitThread((DWORD)ret);

    return (DWORD)ret;
}


ALboolean StartThread(althread_t *thread, ALuint (*func)(ALvoid*), ALvoid *ptr)
{
    althread_info *info;
    DWORD dummy;

    info = malloc(sizeof(*info));
    if(!info) return AL_FALSE;

    info->func = func;
    info->ptr = ptr;

    info->hdl = CreateThread(NULL, THREAD_STACK_SIZE, StarterFunc, info, 0, &dummy);
    if(!info->hdl)
    {
        free(info);
        return AL_FALSE;
    }

    *thread = info;
    return AL_TRUE;
}

ALuint StopThread(althread_t thread)
{
    DWORD ret = 0;

    WaitForSingleObject(thread->hdl, INFINITE);
    GetExitCodeThread(thread->hdl, &ret);
    CloseHandle(thread->hdl);

    free(thread);

    return (ALuint)ret;
}


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

#else

#include <pthread.h>

typedef struct althread_info {
    ALuint (*func)(ALvoid*);
    ALvoid *ptr;
    ALuint ret;
    pthread_t hdl;
} althread_info;

static void *StarterFunc(void *ptr)
{
    althread_info *inf = (althread_info*)ptr;
    inf->ret = inf->func(inf->ptr);
    return NULL;
}


ALboolean StartThread(althread_t *thread, ALuint (*func)(ALvoid*), ALvoid *ptr)
{
    pthread_attr_t attr;
    althread_info *info;

    info = malloc(sizeof(*info));
    if(!info) return AL_FALSE;

    if(pthread_attr_init(&attr) != 0)
    {
        free(info);
        return AL_FALSE;
    }
    if(pthread_attr_setstacksize(&attr, THREAD_STACK_SIZE) != 0)
    {
        pthread_attr_destroy(&attr);
        free(info);
        return AL_FALSE;
    }

    info->func = func;
    info->ptr = ptr;
    if(pthread_create(&info->hdl, &attr, StarterFunc, info) != 0)
    {
        pthread_attr_destroy(&attr);
        free(info);
        return AL_FALSE;
    }
    pthread_attr_destroy(&attr);

    *thread = info;
    return AL_TRUE;
}

ALuint StopThread(althread_t thread)
{
    ALuint ret;

    pthread_join(thread->hdl, NULL);
    ret = thread->ret;

    free(thread);

    return ret;
}


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

#endif
