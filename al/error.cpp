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
#include <windows.h>
#endif

#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

#include "AL/al.h"
#include "AL/alc.h"

#include "al/debug.h"
#include "alc/alconfig.h"
#include "alc/context.h"
#include "alformat.hpp"
#include "alnumeric.h"
#include "core/except.h"
#include "core/logging.h"
#include "direct_defs.h"
#include "gsl/gsl"
#include "strutils.hpp"


namespace {

auto alGetError(gsl::not_null<al::Context*> context) noexcept -> ALenum
{
    auto ret = context->mLastThreadError.get();
    if(ret != AL_NO_ERROR) [[unlikely]]
        context->mLastThreadError.set(AL_NO_ERROR);
    return ret;
}

} // namespace


void al::Context::setErrorImpl(ALenum const errorCode, al::string_view const fmt, al::format_args args)
{
    const auto message = al::vformat(fmt, std::move(args));

    WARN("Error generated on context {}, code {:#04x}, \"{}\"",
        decltype(std::declval<void*>()){this}, as_unsigned(errorCode), message);
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

    if(mLastThreadError.get() == AL_NO_ERROR)
        mLastThreadError.set(errorCode);

    debugMessage(DebugSource::API, DebugType::Error, as_unsigned(errorCode), DebugSeverity::High,
        message);
}

void al::Context::throw_error_impl(ALenum const errorCode, al::string_view const fmt,
    al::format_args args)
{
    setErrorImpl(errorCode, fmt, std::move(args));
    throw al::base_exception{};
}


/* Special-case alGetError since it (potentially) raises a debug signal and
 * returns a non-default value for a null context.
 */
AL_API auto AL_APIENTRY alGetError() noexcept -> ALenum
{
    if(auto context = GetContextRef()) [[likely]]
        return alGetError(gsl::make_not_null(context.get()));

    static constexpr auto get_value = [](gsl::czstring envname, std::string_view optname) -> ALenum
    {
        auto optstr = al::getenv(envname);
        if(!optstr)
            optstr = ConfigValueStr({}, "game_compat", optname);
        if(optstr)
        {
            try {
                auto idx = 0_uz;
                auto value = std::stoi(*optstr, &idx, 0);
                if(idx >= optstr->size() || std::isspace(optstr->at(idx)))
                    return value;
            } catch(...) {
            }
            ERR("Invalid default error value: \"{}\"", *optstr);
        }
        return AL_INVALID_OPERATION;
    };
    static const auto deferror = get_value("__ALSOFT_DEFAULT_ERROR", "default-error");

    WARN("Querying error state on null context (implicitly {:#04x})", as_unsigned(deferror));
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

FORCE_ALIGN auto AL_APIENTRY alGetErrorDirect(ALCcontext *context) noexcept -> ALenum
{
    return alGetError(al::verify_context(context));
}
