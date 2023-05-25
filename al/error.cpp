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

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <atomic>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <vector>

#include "AL/al.h"
#include "AL/alc.h"

#include "al/debug.h"
#include "alc/alconfig.h"
#include "alc/context.h"
#include "almalloc.h"
#include "alstring.h"
#include "core/except.h"
#include "core/logging.h"
#include "direct_defs.h"
#include "opthelpers.h"
#include "strutils.h"


bool TrapALError{false};

void ALCcontext::setError(ALenum errorCode, const char *msg, ...)
{
    auto message = std::vector<char>(256);

    va_list args, args2;
    va_start(args, msg);
    va_copy(args2, args);
    int msglen{std::vsnprintf(message.data(), message.size(), msg, args)};
    if(msglen >= 0 && static_cast<size_t>(msglen) >= message.size())
    {
        message.resize(static_cast<size_t>(msglen) + 1u);
        msglen = std::vsnprintf(message.data(), message.size(), msg, args2);
    }
    va_end(args2);
    va_end(args);

    if(msglen >= 0)
        msg = message.data();
    else
    {
        msg = "<internal error constructing message>";
        msglen = static_cast<int>(strlen(msg));
    }

    WARN("Error generated on context %p, code 0x%04x, \"%s\"\n",
        decltype(std::declval<void*>()){this}, errorCode, msg);
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
    mLastError.compare_exchange_strong(curerr, errorCode);

    debugMessage(DebugSource::API, DebugType::Error, 0, DebugSeverity::High,
        {msg, static_cast<uint>(msglen)});
}

/* Special-case alGetError since it (potentially) raises a debug signal and
 * returns a non-default value for a null context.
 */
AL_API ALenum AL_APIENTRY alGetError(void) noexcept
{
    auto context = GetContextRef();
    if(!context) UNLIKELY
    {
        static const ALenum deferror{[](const char *envname, const char *optname) -> ALenum
        {
            auto optstr = al::getenv(envname);
            if(!optstr)
                optstr = ConfigValueStr(nullptr, "game_compat", optname);

            if(optstr)
            {
                char *end{};
                auto value = std::strtoul(optstr->c_str(), &end, 0);
                if(end && *end == '\0' && value <= std::numeric_limits<ALenum>::max())
                    return static_cast<ALenum>(value);
                ERR("Invalid default error value: \"%s\"", optstr->c_str());
            }
            return AL_INVALID_OPERATION;
        }("__ALSOFT_DEFAULT_ERROR", "default-error")};

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
    return alGetErrorDirect(context.get());
}

FORCE_ALIGN ALenum AL_APIENTRY alGetErrorDirect(ALCcontext *context) noexcept
{
    return context->mLastError.exchange(AL_NO_ERROR);
}
