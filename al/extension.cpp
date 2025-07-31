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

#include <string_view>

#include "AL/al.h"
#include "AL/alc.h"

#include "alc/context.h"
#include "alstring.h"
#include "direct_defs.h"


namespace {

auto alIsExtensionPresent(gsl::strict_not_null<ALCcontext*> context, const ALchar *extName)
    noexcept -> ALboolean
{
    if(!extName) [[unlikely]]
    {
        context->setError(AL_INVALID_VALUE, "NULL pointer");
        return AL_FALSE;
    }

    const auto tofind = std::string_view{extName};
    const auto found = std::ranges::any_of(context->mExtensions, [tofind](std::string_view ext)
    { return tofind.size() == ext.size() && al::case_compare(ext, tofind) == 0; });
    return found ? AL_TRUE : AL_FALSE;
}

} // namespace

AL_API DECL_FUNC1(ALboolean, alIsExtensionPresent, const ALchar*,extName)

AL_API auto AL_APIENTRY alGetProcAddress(const ALchar *funcName) noexcept -> ALvoid*
{
    if(!funcName) return nullptr;
    return alcGetProcAddress(nullptr, funcName);
}

FORCE_ALIGN auto AL_APIENTRY alGetProcAddressDirect(ALCcontext*, const ALchar *funcName) noexcept
    -> ALvoid*
{
    if(!funcName) return nullptr;
    return alcGetProcAddress(nullptr, funcName);
}

AL_API auto AL_APIENTRY alGetEnumValue(const ALchar *enumName) noexcept -> ALenum
{
    if(!enumName) return ALenum{0};
    return alcGetEnumValue(nullptr, enumName);
}

FORCE_ALIGN auto AL_APIENTRY alGetEnumValueDirect(ALCcontext*, const ALchar *enumName) noexcept
    -> ALenum
{
    if(!enumName) return ALenum{0};
    return alcGetEnumValue(nullptr, enumName);
}
