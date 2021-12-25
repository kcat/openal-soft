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

#include <cctype>
#include <cstdlib>
#include <cstring>

#include "AL/al.h"
#include "AL/alc.h"

#include "alc/context.h"
#include "alstring.h"
#include "core/except.h"
#include "opthelpers.h"

#if ALSOFT_EAX
#include "eax_al_api.h"
#include "eax_globals.h"
#endif // ALSOFT_EAX


AL_API ALboolean AL_APIENTRY alIsExtensionPresent(const ALchar *extName)
START_API_FUNC
{
#if ALSOFT_EAX
    {
#endif // ALSOFT_EAX

    ContextRef context{GetContextRef()};
    if(unlikely(!context)) return AL_FALSE;

    if(!extName)
        SETERR_RETURN(context, AL_INVALID_VALUE, AL_FALSE, "NULL pointer");

    size_t len{strlen(extName)};
    const char *ptr{context->mExtensionList};
    while(ptr && *ptr)
    {
        if(al::strncasecmp(ptr, extName, len) == 0 && (ptr[len] == '\0' || isspace(ptr[len])))
            return AL_TRUE;

        if((ptr=strchr(ptr, ' ')) != nullptr)
        {
            do {
                ++ptr;
            } while(isspace(*ptr));
        }
    }

#if ALSOFT_EAX
    }

    if (!eax::g_is_disable)
    {
        const auto eax_lock = eax::g_al_api.get_lock();
        return eax::g_al_api.on_alIsExtensionPresent(extName);
    }
    else
#endif // ALSOFT_EAX
    return AL_FALSE;
}
END_API_FUNC


AL_API ALvoid* AL_APIENTRY alGetProcAddress(const ALchar *funcName)
START_API_FUNC
{
    if(!funcName) return nullptr;

#if ALSOFT_EAX
    ::ALvoid* alc_address = nullptr;

    {
        alc_address = alcGetProcAddress(nullptr, funcName);
    }

    if (!eax::g_is_disable && !alc_address)
    {
        const auto eax_lock = eax::g_al_api.get_lock();
        alc_address = eax::g_al_api.on_alGetProcAddress(funcName);
    }

    return alc_address;
#else
    return alcGetProcAddress(nullptr, funcName);
#endif // ALSOFT_EAX
}
END_API_FUNC

AL_API ALenum AL_APIENTRY alGetEnumValue(const ALchar *enumName)
START_API_FUNC
{
    if(!enumName) return static_cast<ALenum>(0);
    return alcGetEnumValue(nullptr, enumName);
}
END_API_FUNC
