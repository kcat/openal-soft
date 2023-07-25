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

#include "listener.h"

#include <cmath>
#include <mutex>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/efx.h"

#include "alc/context.h"
#include "almalloc.h"
#include "atomic.h"
#include "core/except.h"
#include "direct_defs.h"
#include "opthelpers.h"


namespace {

inline void UpdateProps(ALCcontext *context)
{
    if(!context->mDeferUpdates)
    {
        UpdateContextProps(context);
        return;
    }
    context->mPropsDirty = true;
}

inline void CommitAndUpdateProps(ALCcontext *context)
{
    if(!context->mDeferUpdates)
    {
#ifdef ALSOFT_EAX
        if(context->eaxNeedsCommit())
        {
            context->mPropsDirty = true;
            context->applyAllUpdates();
            return;
        }
#endif
        UpdateContextProps(context);
        return;
    }
    context->mPropsDirty = true;
}

} // namespace

AL_API DECL_FUNC2(void, alListenerf, ALenum, ALfloat)
FORCE_ALIGN void AL_APIENTRY alListenerfDirect(ALCcontext *context, ALenum param, ALfloat value) noexcept
{
    ALlistener &listener = context->mListener;
    std::lock_guard<std::mutex> _{context->mPropLock};
    switch(param)
    {
    case AL_GAIN:
        if(!(value >= 0.0f && std::isfinite(value)))
            return context->setError(AL_INVALID_VALUE, "Listener gain out of range");
        listener.Gain = value;
        UpdateProps(context);
        break;

    case AL_METERS_PER_UNIT:
        if(!(value >= AL_MIN_METERS_PER_UNIT && value <= AL_MAX_METERS_PER_UNIT))
            return context->setError(AL_INVALID_VALUE, "Listener meters per unit out of range");
        listener.mMetersPerUnit = value;
        UpdateProps(context);
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid listener float property");
    }
}

AL_API DECL_FUNC4(void, alListener3f, ALenum, ALfloat, ALfloat, ALfloat)
FORCE_ALIGN void AL_APIENTRY alListener3fDirect(ALCcontext *context, ALenum param, ALfloat value1,
    ALfloat value2, ALfloat value3) noexcept
{
    ALlistener &listener = context->mListener;
    std::lock_guard<std::mutex> _{context->mPropLock};
    switch(param)
    {
    case AL_POSITION:
        if(!(std::isfinite(value1) && std::isfinite(value2) && std::isfinite(value3)))
            return context->setError(AL_INVALID_VALUE, "Listener position out of range");
        listener.Position[0] = value1;
        listener.Position[1] = value2;
        listener.Position[2] = value3;
        CommitAndUpdateProps(context);
        break;

    case AL_VELOCITY:
        if(!(std::isfinite(value1) && std::isfinite(value2) && std::isfinite(value3)))
            return context->setError(AL_INVALID_VALUE, "Listener velocity out of range");
        listener.Velocity[0] = value1;
        listener.Velocity[1] = value2;
        listener.Velocity[2] = value3;
        CommitAndUpdateProps(context);
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid listener 3-float property");
    }
}

AL_API DECL_FUNC2(void, alListenerfv, ALenum, const ALfloat*)
FORCE_ALIGN void AL_APIENTRY alListenerfvDirect(ALCcontext *context, ALenum param,
    const ALfloat *values) noexcept
{
    if(!values) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "NULL pointer");

    switch(param)
    {
    case AL_GAIN:
    case AL_METERS_PER_UNIT:
        alListenerfDirect(context, param, values[0]);
        return;

    case AL_POSITION:
    case AL_VELOCITY:
        alListener3fDirect(context, param, values[0], values[1], values[2]);
        return;
    }

    ALlistener &listener = context->mListener;
    std::lock_guard<std::mutex> _{context->mPropLock};
    switch(param)
    {
    case AL_ORIENTATION:
        if(!(std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]) &&
             std::isfinite(values[3]) && std::isfinite(values[4]) && std::isfinite(values[5])))
            return context->setError(AL_INVALID_VALUE, "Listener orientation out of range");
        /* AT then UP */
        listener.OrientAt[0] = values[0];
        listener.OrientAt[1] = values[1];
        listener.OrientAt[2] = values[2];
        listener.OrientUp[0] = values[3];
        listener.OrientUp[1] = values[4];
        listener.OrientUp[2] = values[5];
        CommitAndUpdateProps(context);
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid listener float-vector property");
    }
}


AL_API DECL_FUNC2(void, alListeneri, ALenum, ALint)
FORCE_ALIGN void AL_APIENTRY alListeneriDirect(ALCcontext *context, ALenum param, ALint /*value*/) noexcept
{
    std::lock_guard<std::mutex> _{context->mPropLock};
    switch(param)
    {
    default:
        context->setError(AL_INVALID_ENUM, "Invalid listener integer property");
    }
}

AL_API DECL_FUNC4(void, alListener3i, ALenum, ALint, ALint, ALint)
FORCE_ALIGN void AL_APIENTRY alListener3iDirect(ALCcontext *context, ALenum param, ALint value1,
    ALint value2, ALint value3) noexcept
{
    switch(param)
    {
    case AL_POSITION:
    case AL_VELOCITY:
        alListener3fDirect(context, param, static_cast<ALfloat>(value1),
            static_cast<ALfloat>(value2), static_cast<ALfloat>(value3));
        return;
    }

    std::lock_guard<std::mutex> _{context->mPropLock};
    switch(param)
    {
    default:
        context->setError(AL_INVALID_ENUM, "Invalid listener 3-integer property");
    }
}

AL_API DECL_FUNC2(void, alListeneriv, ALenum, const ALint*)
FORCE_ALIGN void AL_APIENTRY alListenerivDirect(ALCcontext *context, ALenum param,
    const ALint *values) noexcept
{
    if(!values) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "NULL pointer");

    switch(param)
    {
    case AL_POSITION:
    case AL_VELOCITY:
        alListener3fDirect(context, param, static_cast<ALfloat>(values[0]),
            static_cast<ALfloat>(values[1]), static_cast<ALfloat>(values[2]));
        return;

    case AL_ORIENTATION:
        const ALfloat fvals[6]{
            static_cast<ALfloat>(values[0]),
            static_cast<ALfloat>(values[1]),
            static_cast<ALfloat>(values[2]),
            static_cast<ALfloat>(values[3]),
            static_cast<ALfloat>(values[4]),
            static_cast<ALfloat>(values[5]),
        };
        alListenerfvDirect(context, param, fvals);
        return;
    }

    std::lock_guard<std::mutex> _{context->mPropLock};
    switch(param)
    {
    default:
        context->setError(AL_INVALID_ENUM, "Invalid listener integer-vector property");
    }
}


AL_API DECL_FUNC2(void, alGetListenerf, ALenum, ALfloat*)
FORCE_ALIGN void AL_APIENTRY alGetListenerfDirect(ALCcontext *context, ALenum param,
    ALfloat *value) noexcept
{
    ALlistener &listener = context->mListener;
    std::lock_guard<std::mutex> _{context->mPropLock};
    if(!value)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    case AL_GAIN:
        *value = listener.Gain;
        break;

    case AL_METERS_PER_UNIT:
        *value = listener.mMetersPerUnit;
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid listener float property");
    }
}

AL_API DECL_FUNC4(void, alGetListener3f, ALenum, ALfloat*, ALfloat*, ALfloat*)
FORCE_ALIGN void AL_APIENTRY alGetListener3fDirect(ALCcontext *context, ALenum param,
    ALfloat *value1, ALfloat *value2, ALfloat *value3) noexcept
{
    ALlistener &listener = context->mListener;
    std::lock_guard<std::mutex> _{context->mPropLock};
    if(!value1 || !value2 || !value3)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    case AL_POSITION:
        *value1 = listener.Position[0];
        *value2 = listener.Position[1];
        *value3 = listener.Position[2];
        break;

    case AL_VELOCITY:
        *value1 = listener.Velocity[0];
        *value2 = listener.Velocity[1];
        *value3 = listener.Velocity[2];
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid listener 3-float property");
    }
}

AL_API DECL_FUNC2(void, alGetListenerfv, ALenum, ALfloat*)
FORCE_ALIGN void AL_APIENTRY alGetListenerfvDirect(ALCcontext *context, ALenum param,
    ALfloat *values) noexcept
{
    switch(param)
    {
    case AL_GAIN:
    case AL_METERS_PER_UNIT:
        alGetListenerfDirect(context, param, values);
        return;

    case AL_POSITION:
    case AL_VELOCITY:
        alGetListener3fDirect(context, param, values+0, values+1, values+2);
        return;
    }

    ALlistener &listener = context->mListener;
    std::lock_guard<std::mutex> _{context->mPropLock};
    if(!values)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    case AL_ORIENTATION:
        // AT then UP
        values[0] = listener.OrientAt[0];
        values[1] = listener.OrientAt[1];
        values[2] = listener.OrientAt[2];
        values[3] = listener.OrientUp[0];
        values[4] = listener.OrientUp[1];
        values[5] = listener.OrientUp[2];
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid listener float-vector property");
    }
}


AL_API DECL_FUNC2(void, alGetListeneri, ALenum, ALint*)
FORCE_ALIGN void AL_APIENTRY alGetListeneriDirect(ALCcontext *context, ALenum param, ALint *value) noexcept
{
    std::lock_guard<std::mutex> _{context->mPropLock};
    if(!value)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    default:
        context->setError(AL_INVALID_ENUM, "Invalid listener integer property");
    }
}

AL_API DECL_FUNC4(void, alGetListener3i, ALenum, ALint*, ALint*, ALint*)
FORCE_ALIGN void AL_APIENTRY alGetListener3iDirect(ALCcontext *context, ALenum param,
    ALint *value1, ALint *value2, ALint *value3) noexcept
{
    ALlistener &listener = context->mListener;
    std::lock_guard<std::mutex> _{context->mPropLock};
    if(!value1 || !value2 || !value3)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    case AL_POSITION:
        *value1 = static_cast<ALint>(listener.Position[0]);
        *value2 = static_cast<ALint>(listener.Position[1]);
        *value3 = static_cast<ALint>(listener.Position[2]);
        break;

    case AL_VELOCITY:
        *value1 = static_cast<ALint>(listener.Velocity[0]);
        *value2 = static_cast<ALint>(listener.Velocity[1]);
        *value3 = static_cast<ALint>(listener.Velocity[2]);
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid listener 3-integer property");
    }
}

AL_API DECL_FUNC2(void, alGetListeneriv, ALenum, ALint*)
FORCE_ALIGN void AL_APIENTRY alGetListenerivDirect(ALCcontext *context, ALenum param,
    ALint *values) noexcept
{
    switch(param)
    {
    case AL_POSITION:
    case AL_VELOCITY:
        alGetListener3iDirect(context, param, values+0, values+1, values+2);
        return;
    }

    ALlistener &listener = context->mListener;
    std::lock_guard<std::mutex> _{context->mPropLock};
    if(!values)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    case AL_ORIENTATION:
        // AT then UP
        values[0] = static_cast<ALint>(listener.OrientAt[0]);
        values[1] = static_cast<ALint>(listener.OrientAt[1]);
        values[2] = static_cast<ALint>(listener.OrientAt[2]);
        values[3] = static_cast<ALint>(listener.OrientUp[0]);
        values[4] = static_cast<ALint>(listener.OrientUp[1]);
        values[5] = static_cast<ALint>(listener.OrientUp[2]);
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid listener integer-vector property");
    }
}
