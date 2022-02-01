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

#ifdef ALSOFT_EAX
#include "eax_globals.h"
#include "eax_x_ram.h"
#endif // ALSOFT_EAX

AL_API ALboolean AL_APIENTRY alIsExtensionPresent(const ALchar *extName)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(unlikely(!context)) return AL_FALSE;

    if(!extName)
        SETERR_RETURN(context, AL_INVALID_VALUE, AL_FALSE, "NULL pointer");

    size_t len{strlen(extName)};
#ifdef ALSOFT_EAX
    if (al::strcasecmp(eax_v2_0_ext_name_1, extName) == 0 ||
        al::strcasecmp(eax_v2_0_ext_name_2, extName) == 0 ||
        al::strcasecmp(eax_v3_0_ext_name, extName) == 0 ||
        al::strcasecmp(eax_v4_0_ext_name, extName) == 0 ||
        al::strcasecmp(eax_v5_0_ext_name, extName) == 0)
    {
        const auto is_present = eax_g_is_enabled && context->eax_is_capable();
        return is_present ? AL_TRUE : AL_FALSE;
    }

    if (al::strcasecmp(eax_x_ram_ext_name, extName) == 0)
    {
        const auto is_present = eax_g_is_enabled;
        return is_present ? AL_TRUE : AL_FALSE;
    }
#endif // ALSOFT_EAX
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

    return AL_FALSE;
}
END_API_FUNC


AL_API ALvoid* AL_APIENTRY alGetProcAddress(const ALchar *funcName)
START_API_FUNC
{
    if(!funcName) return nullptr;
#ifdef ALSOFT_EAX
    if (al::strcasecmp(funcName, eax_eax_set_func_name) == 0)
    {
        if (!eax_g_is_enabled)
        {
            return nullptr;
        }

        ContextRef context{GetContextRef()};

        if (!context || !context->eax_is_capable())
        {
            return nullptr;
        }

        return reinterpret_cast<ALvoid*>(EAXSet);
    }

    if (al::strcasecmp(funcName, eax_eax_get_func_name) == 0)
    {
        if (!eax_g_is_enabled)
        {
            return nullptr;
        }

        ContextRef context{GetContextRef()};

        if (!context || !context->eax_is_capable())
        {
            return nullptr;
        }

        return reinterpret_cast<ALvoid*>(EAXGet);
    }

    if (al::strcasecmp(funcName, eax_eax_set_buffer_mode_func_name) == 0)
    {
        if (!eax_g_is_enabled)
        {
            return nullptr;
        }

        ContextRef context{GetContextRef()};

        if (!context)
        {
            return nullptr;
        }

        return reinterpret_cast<ALvoid*>(EAXSetBufferMode);
    }

    if (al::strcasecmp(funcName, eax_eax_get_buffer_mode_func_name) == 0)
    {
        if (!eax_g_is_enabled)
        {
            return nullptr;
        }

        ContextRef context{GetContextRef()};

        if (!context)
        {
            return nullptr;
        }

        return reinterpret_cast<ALvoid*>(EAXGetBufferMode);
    }
#endif // ALSOFT_EAX
    return alcGetProcAddress(nullptr, funcName);
}
END_API_FUNC

AL_API ALenum AL_APIENTRY alGetEnumValue(const ALchar *enumName)
START_API_FUNC
{
    if(!enumName) return static_cast<ALenum>(0);
#ifdef ALSOFT_EAX
    if (eax_g_is_enabled)
    {
        struct Descriptor
        {
            const char* name;
            ALenum value;
        }; // Descriptor

        constexpr Descriptor descriptors[] =
        {
            Descriptor{AL_EAX_RAM_SIZE_NAME, AL_EAX_RAM_SIZE},
            Descriptor{AL_EAX_RAM_FREE_NAME, AL_EAX_RAM_FREE},

            Descriptor{AL_STORAGE_AUTOMATIC_NAME, AL_STORAGE_AUTOMATIC},
            Descriptor{AL_STORAGE_HARDWARE_NAME, AL_STORAGE_HARDWARE},
            Descriptor{AL_STORAGE_ACCESSIBLE_NAME, AL_STORAGE_ACCESSIBLE},
        }; // descriptors

        for (const auto& descriptor : descriptors)
        {
            if (strcmp(descriptor.name, enumName) == 0)
            {
                return descriptor.value;
            }
        }
    }
#endif // ALSOFT_EAX
    return alcGetEnumValue(nullptr, enumName);
}
END_API_FUNC
