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

#include <cmath>

#include "alMain.h"
#include "alcontext.h"
#include "alu.h"
#include "alError.h"
#include "alListener.h"
#include "alSource.h"

#define DO_UPDATEPROPS() do {                                                 \
    if(!context->DeferUpdates.load(std::memory_order_acquire))                \
        UpdateListenerProps(context.get());                                   \
    else                                                                      \
        listener.PropsClean.clear(std::memory_order_release);                 \
} while(0)


AL_API ALvoid AL_APIENTRY alListenerf(ALenum param, ALfloat value)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    ALlistener &listener = context->Listener;
    std::lock_guard<almtx_t> _{context->PropLock};
    switch(param)
    {
    case AL_GAIN:
        if(!(value >= 0.0f && std::isfinite(value)))
            SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Listener gain out of range");
        listener.Gain = value;
        DO_UPDATEPROPS();
        break;

    case AL_METERS_PER_UNIT:
        if(!(value >= AL_MIN_METERS_PER_UNIT && value <= AL_MAX_METERS_PER_UNIT))
            SETERR_RETURN(context.get(), AL_INVALID_VALUE,,
                          "Listener meters per unit out of range");
        context->MetersPerUnit = value;
        if(!context->DeferUpdates.load(std::memory_order_acquire))
            UpdateContextProps(context.get());
        else
            context->PropsClean.clear(std::memory_order_release);
        break;

    default:
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid listener float property");
    }
}


AL_API ALvoid AL_APIENTRY alListener3f(ALenum param, ALfloat value1, ALfloat value2, ALfloat value3)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    ALlistener &listener = context->Listener;
    std::lock_guard<almtx_t> _{context->PropLock};
    switch(param)
    {
    case AL_POSITION:
        if(!(std::isfinite(value1) && std::isfinite(value2) && std::isfinite(value3)))
            SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Listener position out of range");
        listener.Position[0] = value1;
        listener.Position[1] = value2;
        listener.Position[2] = value3;
        DO_UPDATEPROPS();
        break;

    case AL_VELOCITY:
        if(!(std::isfinite(value1) && std::isfinite(value2) && std::isfinite(value3)))
            SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Listener velocity out of range");
        listener.Velocity[0] = value1;
        listener.Velocity[1] = value2;
        listener.Velocity[2] = value3;
        DO_UPDATEPROPS();
        break;

    default:
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid listener 3-float property");
    }
}


AL_API ALvoid AL_APIENTRY alListenerfv(ALenum param, const ALfloat *values)
{
    if(values)
    {
        switch(param)
        {
        case AL_GAIN:
        case AL_METERS_PER_UNIT:
            alListenerf(param, values[0]);
            return;

        case AL_POSITION:
        case AL_VELOCITY:
            alListener3f(param, values[0], values[1], values[2]);
            return;
        }
    }

    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    ALlistener &listener = context->Listener;
    std::lock_guard<almtx_t> _{context->PropLock};
    if(!values) SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "NULL pointer");
    switch(param)
    {
    case AL_ORIENTATION:
        if(!(std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]) &&
             std::isfinite(values[3]) && std::isfinite(values[4]) && std::isfinite(values[5])))
            SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Listener orientation out of range");
        /* AT then UP */
        listener.Forward[0] = values[0];
        listener.Forward[1] = values[1];
        listener.Forward[2] = values[2];
        listener.Up[0] = values[3];
        listener.Up[1] = values[4];
        listener.Up[2] = values[5];
        DO_UPDATEPROPS();
        break;

    default:
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid listener float-vector property");
    }
}


AL_API ALvoid AL_APIENTRY alListeneri(ALenum param, ALint UNUSED(value))
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<almtx_t> _{context->PropLock};
    switch(param)
    {
    default:
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid listener integer property");
    }
}


AL_API void AL_APIENTRY alListener3i(ALenum param, ALint value1, ALint value2, ALint value3)
{
    switch(param)
    {
    case AL_POSITION:
    case AL_VELOCITY:
        alListener3f(param, (ALfloat)value1, (ALfloat)value2, (ALfloat)value3);
        return;
    }

    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<almtx_t> _{context->PropLock};
    switch(param)
    {
    default:
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid listener 3-integer property");
    }
}


AL_API void AL_APIENTRY alListeneriv(ALenum param, const ALint *values)
{
    if(values)
    {
        ALfloat fvals[6];
        switch(param)
        {
        case AL_POSITION:
        case AL_VELOCITY:
            alListener3f(param, (ALfloat)values[0], (ALfloat)values[1], (ALfloat)values[2]);
            return;

        case AL_ORIENTATION:
            fvals[0] = (ALfloat)values[0];
            fvals[1] = (ALfloat)values[1];
            fvals[2] = (ALfloat)values[2];
            fvals[3] = (ALfloat)values[3];
            fvals[4] = (ALfloat)values[4];
            fvals[5] = (ALfloat)values[5];
            alListenerfv(param, fvals);
            return;
        }
    }

    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<almtx_t> _{context->PropLock};
    if(!values)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    default:
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid listener integer-vector property");
    }
}


AL_API ALvoid AL_APIENTRY alGetListenerf(ALenum param, ALfloat *value)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    ALlistener &listener = context->Listener;
    std::lock_guard<almtx_t> _{context->PropLock};
    if(!value)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    case AL_GAIN:
        *value = listener.Gain;
        break;

    case AL_METERS_PER_UNIT:
        *value = context->MetersPerUnit;
        break;

    default:
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid listener float property");
    }
}


AL_API ALvoid AL_APIENTRY alGetListener3f(ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    ALlistener &listener = context->Listener;
    std::lock_guard<almtx_t> _{context->PropLock};
    if(!value1 || !value2 || !value3)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
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
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid listener 3-float property");
    }
}


AL_API ALvoid AL_APIENTRY alGetListenerfv(ALenum param, ALfloat *values)
{
    switch(param)
    {
    case AL_GAIN:
    case AL_METERS_PER_UNIT:
        alGetListenerf(param, values);
        return;

    case AL_POSITION:
    case AL_VELOCITY:
        alGetListener3f(param, values+0, values+1, values+2);
        return;
    }

    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    ALlistener &listener = context->Listener;
    std::lock_guard<almtx_t> _{context->PropLock};
    if(!values)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    case AL_ORIENTATION:
        // AT then UP
        values[0] = listener.Forward[0];
        values[1] = listener.Forward[1];
        values[2] = listener.Forward[2];
        values[3] = listener.Up[0];
        values[4] = listener.Up[1];
        values[5] = listener.Up[2];
        break;

    default:
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid listener float-vector property");
    }
}


AL_API ALvoid AL_APIENTRY alGetListeneri(ALenum param, ALint *value)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<almtx_t> _{context->PropLock};
    if(!value)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    default:
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid listener integer property");
    }
}


AL_API void AL_APIENTRY alGetListener3i(ALenum param, ALint *value1, ALint *value2, ALint *value3)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    ALlistener &listener = context->Listener;
    std::lock_guard<almtx_t> _{context->PropLock};
    if(!value1 || !value2 || !value3)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    case AL_POSITION:
        *value1 = (ALint)listener.Position[0];
        *value2 = (ALint)listener.Position[1];
        *value3 = (ALint)listener.Position[2];
        break;

    case AL_VELOCITY:
        *value1 = (ALint)listener.Velocity[0];
        *value2 = (ALint)listener.Velocity[1];
        *value3 = (ALint)listener.Velocity[2];
        break;

    default:
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid listener 3-integer property");
    }
}


AL_API void AL_APIENTRY alGetListeneriv(ALenum param, ALint* values)
{
    switch(param)
    {
    case AL_POSITION:
    case AL_VELOCITY:
        alGetListener3i(param, values+0, values+1, values+2);
        return;
    }

    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    ALlistener &listener = context->Listener;
    std::lock_guard<almtx_t> _{context->PropLock};
    if(!values)
        alSetError(context.get(), AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    case AL_ORIENTATION:
        // AT then UP
        values[0] = (ALint)listener.Forward[0];
        values[1] = (ALint)listener.Forward[1];
        values[2] = (ALint)listener.Forward[2];
        values[3] = (ALint)listener.Up[0];
        values[4] = (ALint)listener.Up[1];
        values[5] = (ALint)listener.Up[2];
        break;

    default:
        alSetError(context.get(), AL_INVALID_ENUM, "Invalid listener integer-vector property");
    }
}


void UpdateListenerProps(ALCcontext *context)
{
    /* Get an unused proprty container, or allocate a new one as needed. */
    ALlistenerProps *props{context->FreeListenerProps.load(std::memory_order_acquire)};
    if(!props)
        props = static_cast<ALlistenerProps*>(al_calloc(16, sizeof(*props)));
    else
    {
        struct ALlistenerProps *next;
        do {
            next = props->next.load(std::memory_order_relaxed);
        } while(context->FreeListenerProps.compare_exchange_weak(props, next,
                std::memory_order_seq_cst, std::memory_order_acquire) == 0);
    }

    /* Copy in current property values. */
    ALlistener &listener = context->Listener;
    props->Position[0] = listener.Position[0];
    props->Position[1] = listener.Position[1];
    props->Position[2] = listener.Position[2];

    props->Velocity[0] = listener.Velocity[0];
    props->Velocity[1] = listener.Velocity[1];
    props->Velocity[2] = listener.Velocity[2];

    props->Forward[0] = listener.Forward[0];
    props->Forward[1] = listener.Forward[1];
    props->Forward[2] = listener.Forward[2];
    props->Up[0] = listener.Up[0];
    props->Up[1] = listener.Up[1];
    props->Up[2] = listener.Up[2];

    props->Gain = listener.Gain;

    /* Set the new container for updating internal parameters. */
    props = listener.Update.exchange(props, std::memory_order_acq_rel);
    if(props)
    {
        /* If there was an unused update container, put it back in the
         * freelist.
         */
        AtomicReplaceHead(context->FreeListenerProps, props);
    }
}
