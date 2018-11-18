/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2000 by authors.
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

#include <signal.h>
#include <stdarg.h>

#ifdef HAVE_WINDOWS_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "alMain.h"
#include "alcontext.h"
#include "alError.h"

ALboolean TrapALError = AL_FALSE;

void alSetError(ALCcontext *context, ALenum errorCode, const char *msg, ...)
{
    char message[1024]{};

    va_list args;
    va_start(args, msg);
    int msglen{snprintf(message, sizeof(message), msg, args)};
    va_end(args);

    if(msglen < 0 || (size_t)msglen >= sizeof(message))
    {
        message[sizeof(message)-1] = 0;
        msglen = (int)strlen(message);
    }
    if(msglen > 0)
        msg = message;
    else
    {
        msg = "<internal error constructing message>";
        msglen = (int)strlen(msg);
    }

    WARN("Error generated on context %p, code 0x%04x, \"%s\"\n",
         context, errorCode, message);
    if(TrapALError)
    {
#ifdef _WIN32
        /* DebugBreak will cause an exception if there is no debugger */
        if(IsDebuggerPresent())
            DebugBreak();
#elif defined(SIGTRAP)
        raise(SIGTRAP);
#endif
    }

    ALenum curerr{AL_NO_ERROR};
    ATOMIC_COMPARE_EXCHANGE_STRONG_SEQ(&context->LastError, &curerr, errorCode);
    if((ATOMIC_LOAD(&context->EnabledEvts, almemory_order_relaxed)&EventType_Error))
    {
        std::lock_guard<almtx_t> _{context->EventCbLock};
        ALbitfieldSOFT enabledevts{ATOMIC_LOAD(&context->EnabledEvts, almemory_order_relaxed)};
        if((enabledevts&EventType_Error) && context->EventCb)
            (*context->EventCb)(AL_EVENT_TYPE_ERROR_SOFT, 0, errorCode, msglen, msg,
                                context->EventParam);
    }
}

AL_API ALenum AL_APIENTRY alGetError(void)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context))
    {
        constexpr ALenum deferror{AL_INVALID_OPERATION};
        WARN("Querying error state on null context (implicitly 0x%04x)\n", deferror);
        if(TrapALError)
        {
#ifdef _WIN32
            if(IsDebuggerPresent())
                DebugBreak();
#elif defined(SIGTRAP)
            raise(SIGTRAP);
#endif
        }
        return deferror;
    }

    return ATOMIC_EXCHANGE_SEQ(&context->LastError, AL_NO_ERROR);
}
