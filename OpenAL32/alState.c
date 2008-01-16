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
 *  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA  02111-1307, USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <stdlib.h>
#include "alMain.h"
#include "AL/alc.h"
#include "alError.h"
#include "alState.h"

static const ALchar alVendor[] = "OpenAL Community";
static const ALchar alVersion[] = "1.1";
static const ALchar alRenderer[] = "OpenAL Soft";

// Error Messages
static const ALchar alNoError[] = "No Error";
static const ALchar alErrInvalidName[] = "Invalid Name";
static const ALchar alErrInvalidEnum[] = "Invalid Enum";
static const ALchar alErrInvalidValue[] = "Invalid Value";
static const ALchar alErrInvalidOp[] = "Invalid Operation";
static const ALchar alErrOutOfMemory[] = "Out of Memory";

ALAPI ALvoid ALAPIENTRY alEnable(ALenum capability)
{
    ALCcontext *Context;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        switch (capability)
        {
            default:
                alSetError(AL_INVALID_ENUM);
                break;
        }

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }
}

ALAPI ALvoid ALAPIENTRY alDisable(ALenum capability)
{
    ALCcontext *Context;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        switch (capability)
        {
            default:
                alSetError(AL_INVALID_ENUM);
                break;
        }

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }
}

ALAPI ALboolean ALAPIENTRY alIsEnabled(ALenum capability)
{
    ALCcontext *Context;
    ALboolean value=AL_FALSE;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        switch (capability)
        {
            default:
                alSetError(AL_INVALID_ENUM);
                break;
        }

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return value;
}

ALAPI ALboolean ALAPIENTRY alGetBoolean(ALenum pname)
{
    ALCcontext *Context;
    ALboolean value=AL_FALSE;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        switch (pname)
        {
            case AL_DOPPLER_FACTOR:
                if (Context->DopplerFactor != 0.0f)
                    value = AL_TRUE;
                break;

            case AL_DOPPLER_VELOCITY:
                if (Context->DopplerVelocity != 0.0f)
                    value = AL_TRUE;
                break;

            case AL_DISTANCE_MODEL:
                if (Context->DistanceModel == AL_INVERSE_DISTANCE_CLAMPED)
                    value = AL_TRUE;
                break;

            case AL_SPEED_OF_SOUND:
                if (Context->flSpeedOfSound != 0.0f)
                    value = AL_TRUE;
                break;

            default:
                alSetError(AL_INVALID_ENUM);
                break;
        }

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return value;
}

ALAPI ALdouble ALAPIENTRY alGetDouble(ALenum pname)
{
    ALCcontext *Context;
    ALdouble value = 0.0;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        switch (pname)
        {
            case AL_DOPPLER_FACTOR:
                value = (double)Context->DopplerFactor;
                break;

            case AL_DOPPLER_VELOCITY:
                value = (double)Context->DopplerVelocity;
                break;

            case AL_DISTANCE_MODEL:
                value = (double)Context->DistanceModel;
                break;

            case AL_SPEED_OF_SOUND:
                value = (double)Context->flSpeedOfSound;
                break;

            default:
                alSetError(AL_INVALID_ENUM);
                break;
        }

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return value;
}

ALAPI ALfloat ALAPIENTRY alGetFloat(ALenum pname)
{
    ALCcontext *Context;
    ALfloat value = 0.0f;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        switch (pname)
        {
            case AL_DOPPLER_FACTOR:
                value = Context->DopplerFactor;
                break;

            case AL_DOPPLER_VELOCITY:
                value = Context->DopplerVelocity;
                break;

            case AL_DISTANCE_MODEL:
                value = (float)Context->DistanceModel;
                break;

            case AL_SPEED_OF_SOUND:
                value = Context->flSpeedOfSound;
                break;

            default:
                alSetError(AL_INVALID_ENUM);
                break;
        }

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return value;
}

ALAPI ALint ALAPIENTRY alGetInteger(ALenum pname)
{
    ALCcontext *Context;
    ALint value = 0;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        switch (pname)
        {
            case AL_DOPPLER_FACTOR:
                value = (ALint)Context->DopplerFactor;
                break;

            case AL_DOPPLER_VELOCITY:
                value = (ALint)Context->DopplerVelocity;
                break;

            case AL_DISTANCE_MODEL:
                value = (ALint)Context->DistanceModel;
                break;

            case AL_SPEED_OF_SOUND:
                value = (ALint)Context->flSpeedOfSound;
                break;

            default:
                alSetError(AL_INVALID_ENUM);
                break;
        }

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return value;
}

ALAPI ALvoid ALAPIENTRY alGetBooleanv(ALenum pname,ALboolean *data)
{
    ALCcontext *Context;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        if (data)
        {
            switch (pname)
            {
                case AL_DOPPLER_FACTOR:
                    *data = (ALboolean)((Context->DopplerFactor != 0.0f) ? AL_TRUE : AL_FALSE);
                    break;

                case AL_DOPPLER_VELOCITY:
                    *data = (ALboolean)((Context->DopplerVelocity != 0.0f) ? AL_TRUE : AL_FALSE);
                    break;

                case AL_DISTANCE_MODEL:
                    *data = (ALboolean)((Context->DistanceModel == AL_INVERSE_DISTANCE_CLAMPED) ? AL_TRUE : AL_FALSE);
                    break;

                case AL_SPEED_OF_SOUND:
                    *data = (ALboolean)((Context->flSpeedOfSound != 0.0f) ? AL_TRUE : AL_FALSE);
                    break;

                default:
                    alSetError(AL_INVALID_ENUM);
                    break;
            }
        }
        else
        {
            // data is a NULL pointer
            alSetError(AL_INVALID_VALUE);
        }

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return;
}

ALAPI ALvoid ALAPIENTRY alGetDoublev(ALenum pname,ALdouble *data)
{
    ALCcontext *Context;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        if (data)
        {
            switch (pname)
            {
                case AL_DOPPLER_FACTOR:
                    *data = (double)Context->DopplerFactor;
                    break;

                case AL_DOPPLER_VELOCITY:
                    *data = (double)Context->DopplerVelocity;
                    break;

                case AL_DISTANCE_MODEL:
                    *data = (double)Context->DistanceModel;
                    break;

                case AL_SPEED_OF_SOUND:
                    *data = (double)Context->flSpeedOfSound;
                    break;

                default:
                    alSetError(AL_INVALID_ENUM);
                    break;
            }
        }
        else
        {
            // data is a NULL pointer
            alSetError(AL_INVALID_VALUE);
        }

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return;
}

ALAPI ALvoid ALAPIENTRY alGetFloatv(ALenum pname,ALfloat *data)
{
    ALCcontext *Context;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        if (data)
        {
            switch (pname)
            {
                case AL_DOPPLER_FACTOR:
                    *data = Context->DopplerFactor;
                    break;

                case AL_DOPPLER_VELOCITY:
                    *data = Context->DopplerVelocity;
                    break;

                case AL_DISTANCE_MODEL:
                    *data = (float)Context->DistanceModel;
                    break;

                case AL_SPEED_OF_SOUND:
                    *data = Context->flSpeedOfSound;
                    break;

                default:
                    alSetError(AL_INVALID_ENUM);
                    break;
            }
        }
        else
        {
            // data is a NULL pointer
            alSetError(AL_INVALID_VALUE);
        }

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return;
}

ALAPI ALvoid ALAPIENTRY alGetIntegerv(ALenum pname,ALint *data)
{
    ALCcontext *Context;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        if (data)
        {
            switch (pname)
            {
                case AL_DOPPLER_FACTOR:
                    *data = (ALint)Context->DopplerFactor;
                    break;

                case AL_DOPPLER_VELOCITY:
                    *data = (ALint)Context->DopplerVelocity;
                    break;

                case AL_DISTANCE_MODEL:
                    *data = (ALint)Context->DistanceModel;
                    break;

                case AL_SPEED_OF_SOUND:
                    *data = (ALint)Context->flSpeedOfSound;
                    break;

                default:
                    alSetError(AL_INVALID_ENUM);
                    break;
            }
        }
        else
        {
            // data is a NULL pointer
            alSetError(AL_INVALID_VALUE);
        }

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return;
}

ALAPI const ALchar* ALAPIENTRY alGetString(ALenum pname)
{
    const ALchar *value;
    ALCcontext *pContext;

    pContext = alcGetCurrentContext();
    if(!pContext)
    {
        alSetError(AL_INVALID_OPERATION);
        return NULL;
    }

    SuspendContext(pContext);

    switch(pname)
    {
        case AL_VENDOR:
            value=alVendor;
            break;

        case AL_VERSION:
            value=alVersion;
            break;

        case AL_RENDERER:
            value=alRenderer;
            break;

        case AL_EXTENSIONS:
            value=pContext->ExtensionList;//alExtensions;
            break;

        case AL_NO_ERROR:
            value=alNoError;
            break;

        case AL_INVALID_NAME:
            value=alErrInvalidName;
            break;

        case AL_INVALID_ENUM:
            value=alErrInvalidEnum;
            break;

        case AL_INVALID_VALUE:
            value=alErrInvalidValue;
            break;

        case AL_INVALID_OPERATION:
            value=alErrInvalidOp;
            break;

        case AL_OUT_OF_MEMORY:
            value=alErrOutOfMemory;
            break;

        default:
            value=NULL;
            alSetError(AL_INVALID_ENUM);
            break;
    }

    ProcessContext(pContext);

    return value;
}

ALAPI ALvoid ALAPIENTRY alDopplerFactor(ALfloat value)
{
    ALCcontext *Context;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        if (value>=0.0f)
            Context->DopplerFactor = value;
        else
            alSetError(AL_INVALID_VALUE);

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return;
}

ALAPI ALvoid ALAPIENTRY alDopplerVelocity(ALfloat value)
{
    ALCcontext *Context;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        if (value>0.0f)
            Context->DopplerVelocity=value;
        else
            alSetError(AL_INVALID_VALUE);

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return;
}

ALAPI ALvoid ALAPIENTRY alSpeedOfSound(ALfloat flSpeedOfSound)
{
    ALCcontext *pContext;

    pContext = alcGetCurrentContext();
    if (pContext)
    {
        SuspendContext(pContext);

        if (flSpeedOfSound > 0.0f)
            pContext->flSpeedOfSound = flSpeedOfSound;
        else
            alSetError(AL_INVALID_VALUE);

        ProcessContext(pContext);
    }
    else
    {
        alSetError(AL_INVALID_OPERATION);
    }

    return;
}

ALAPI ALvoid ALAPIENTRY alDistanceModel(ALenum value)
{
    ALCcontext *Context;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        switch (value)
        {
            case AL_NONE:
            case AL_INVERSE_DISTANCE:
            case AL_INVERSE_DISTANCE_CLAMPED:
            case AL_LINEAR_DISTANCE:
            case AL_LINEAR_DISTANCE_CLAMPED:
            case AL_EXPONENT_DISTANCE:
            case AL_EXPONENT_DISTANCE_CLAMPED:
                Context->DistanceModel = value;
                break;

            default:
                alSetError(AL_INVALID_VALUE);
                break;
        }

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return;
}
