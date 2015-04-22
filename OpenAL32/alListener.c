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

#include "alMain.h"
#include "AL/alc.h"
#include "alError.h"
#include "alListener.h"
#include "alSource.h"

AL_API ALvoid AL_APIENTRY alListenerf(ALenum param, ALfloat value)
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    switch(param)
    {
    case AL_GAIN:
        if(!(value >= 0.0f && isfinite(value)))
            SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

        context->Listener->Gain = value;
        ATOMIC_STORE(&context->UpdateSources, AL_TRUE);
        break;

    case AL_METERS_PER_UNIT:
        if(!(value >= 0.0f && isfinite(value)))
            SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

        context->Listener->MetersPerUnit = value;
        ATOMIC_STORE(&context->UpdateSources, AL_TRUE);
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API ALvoid AL_APIENTRY alListener3f(ALenum param, ALfloat value1, ALfloat value2, ALfloat value3)
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    switch(param)
    {
    case AL_POSITION:
        if(!(isfinite(value1) && isfinite(value2) && isfinite(value3)))
            SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

        LockContext(context);
        aluVectorSet(&context->Listener->Position, value1, value2, value3, 1.0f);
        ATOMIC_STORE(&context->UpdateSources, AL_TRUE);
        UnlockContext(context);
        break;

    case AL_VELOCITY:
        if(!(isfinite(value1) && isfinite(value2) && isfinite(value3)))
            SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

        LockContext(context);
        aluVectorSet(&context->Listener->Velocity, value1, value2, value3, 0.0f);
        ATOMIC_STORE(&context->UpdateSources, AL_TRUE);
        UnlockContext(context);
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API ALvoid AL_APIENTRY alListenerfv(ALenum param, const ALfloat *values)
{
    ALCcontext *context;

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

    context = GetContextRef();
    if(!context) return;

    if(!(values))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch(param)
    {
    case AL_ORIENTATION:
        if(!(isfinite(values[0]) && isfinite(values[1]) && isfinite(values[2]) &&
             isfinite(values[3]) && isfinite(values[4]) && isfinite(values[5])))
            SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

        LockContext(context);
        /* AT then UP */
        context->Listener->Forward[0] = values[0];
        context->Listener->Forward[1] = values[1];
        context->Listener->Forward[2] = values[2];
        context->Listener->Up[0] = values[3];
        context->Listener->Up[1] = values[4];
        context->Listener->Up[2] = values[5];
        ATOMIC_STORE(&context->UpdateSources, AL_TRUE);
        UnlockContext(context);
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API ALvoid AL_APIENTRY alListeneri(ALenum param, ALint UNUSED(value))
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    switch(param)
    {
    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alListener3i(ALenum param, ALint value1, ALint value2, ALint value3)
{
    ALCcontext *context;

    switch(param)
    {
    case AL_POSITION:
    case AL_VELOCITY:
        alListener3f(param, (ALfloat)value1, (ALfloat)value2, (ALfloat)value3);
        return;
    }

    context = GetContextRef();
    if(!context) return;

    switch(param)
    {
    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alListeneriv(ALenum param, const ALint *values)
{
    ALCcontext *context;

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

    context = GetContextRef();
    if(!context) return;

    if(!(values))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch(param)
    {
    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API ALvoid AL_APIENTRY alGetListenerf(ALenum param, ALfloat *value)
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    if(!(value))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch(param)
    {
    case AL_GAIN:
        *value = context->Listener->Gain;
        break;

    case AL_METERS_PER_UNIT:
        *value = context->Listener->MetersPerUnit;
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API ALvoid AL_APIENTRY alGetListener3f(ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3)
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    if(!(value1 && value2 && value3))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch(param)
    {
    case AL_POSITION:
        LockContext(context);
        *value1 = context->Listener->Position.v[0];
        *value2 = context->Listener->Position.v[1];
        *value3 = context->Listener->Position.v[2];
        UnlockContext(context);
        break;

    case AL_VELOCITY:
        LockContext(context);
        *value1 = context->Listener->Velocity.v[0];
        *value2 = context->Listener->Velocity.v[1];
        *value3 = context->Listener->Velocity.v[2];
        UnlockContext(context);
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API ALvoid AL_APIENTRY alGetListenerfv(ALenum param, ALfloat *values)
{
    ALCcontext *context;

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

    context = GetContextRef();
    if(!context) return;

    if(!(values))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch(param)
    {
    case AL_ORIENTATION:
        LockContext(context);
        // AT then UP
        values[0] = context->Listener->Forward[0];
        values[1] = context->Listener->Forward[1];
        values[2] = context->Listener->Forward[2];
        values[3] = context->Listener->Up[0];
        values[4] = context->Listener->Up[1];
        values[5] = context->Listener->Up[2];
        UnlockContext(context);
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API ALvoid AL_APIENTRY alGetListeneri(ALenum param, ALint *value)
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    if(!(value))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch(param)
    {
    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alGetListener3i(ALenum param, ALint *value1, ALint *value2, ALint *value3)
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    if(!(value1 && value2 && value3))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch (param)
    {
    case AL_POSITION:
        LockContext(context);
        *value1 = (ALint)context->Listener->Position.v[0];
        *value2 = (ALint)context->Listener->Position.v[1];
        *value3 = (ALint)context->Listener->Position.v[2];
        UnlockContext(context);
        break;

    case AL_VELOCITY:
        LockContext(context);
        *value1 = (ALint)context->Listener->Velocity.v[0];
        *value2 = (ALint)context->Listener->Velocity.v[1];
        *value3 = (ALint)context->Listener->Velocity.v[2];
        UnlockContext(context);
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alGetListeneriv(ALenum param, ALint* values)
{
    ALCcontext *context;

    switch(param)
    {
    case AL_POSITION:
    case AL_VELOCITY:
        alGetListener3i(param, values+0, values+1, values+2);
        return;
    }

    context = GetContextRef();
    if(!context) return;

    if(!(values))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch(param)
    {
    case AL_ORIENTATION:
        LockContext(context);
        // AT then UP
        values[0] = (ALint)context->Listener->Forward[0];
        values[1] = (ALint)context->Listener->Forward[1];
        values[2] = (ALint)context->Listener->Forward[2];
        values[3] = (ALint)context->Listener->Up[0];
        values[4] = (ALint)context->Listener->Up[1];
        values[5] = (ALint)context->Listener->Up[2];
        UnlockContext(context);
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}
