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
#include "AL/alext.h"
#include "alError.h"
#include "alSource.h"
#include "alAuxEffectSlot.h"
#include "alState.h"

static const ALchar alVendor[] = "OpenAL Community";
static const ALchar alVersion[] = "1.1 ALSOFT "ALSOFT_VERSION;
static const ALchar alRenderer[] = "OpenAL Soft";

// Error Messages
static const ALchar alNoError[] = "No Error";
static const ALchar alErrInvalidName[] = "Invalid Name";
static const ALchar alErrInvalidEnum[] = "Invalid Enum";
static const ALchar alErrInvalidValue[] = "Invalid Value";
static const ALchar alErrInvalidOp[] = "Invalid Operation";
static const ALchar alErrOutOfMemory[] = "Out of Memory";

AL_API ALvoid AL_APIENTRY alEnable(ALenum capability)
{
    ALCcontext *Context;

    Context = GetLockedContext();
    if(!Context) return;

    switch(capability)
    {
        case AL_SOURCE_DISTANCE_MODEL:
            Context->SourceDistanceModel = AL_TRUE;
            Context->UpdateSources = AL_TRUE;
            break;

        default:
            alSetError(Context, AL_INVALID_ENUM);
            break;
    }

    UnlockContext(Context);
}

AL_API ALvoid AL_APIENTRY alDisable(ALenum capability)
{
    ALCcontext *Context;

    Context = GetLockedContext();
    if(!Context) return;

    switch(capability)
    {
        case AL_SOURCE_DISTANCE_MODEL:
            Context->SourceDistanceModel = AL_FALSE;
            Context->UpdateSources = AL_TRUE;
            break;

        default:
            alSetError(Context, AL_INVALID_ENUM);
            break;
    }

    UnlockContext(Context);
}

AL_API ALboolean AL_APIENTRY alIsEnabled(ALenum capability)
{
    ALCcontext *Context;
    ALboolean value=AL_FALSE;

    Context = GetLockedContext();
    if(!Context) return AL_FALSE;

    switch(capability)
    {
        case AL_SOURCE_DISTANCE_MODEL:
            value = Context->SourceDistanceModel;
            break;

        default:
            alSetError(Context, AL_INVALID_ENUM);
            break;
    }

    UnlockContext(Context);

    return value;
}

AL_API ALboolean AL_APIENTRY alGetBoolean(ALenum pname)
{
    ALCcontext *Context;
    ALboolean value=AL_FALSE;

    Context = GetLockedContext();
    if(!Context) return AL_FALSE;

    switch(pname)
    {
        case AL_DOPPLER_FACTOR:
            if(Context->DopplerFactor != 0.0f)
                value = AL_TRUE;
            break;

        case AL_DOPPLER_VELOCITY:
            if(Context->DopplerVelocity != 0.0f)
                value = AL_TRUE;
            break;

        case AL_DISTANCE_MODEL:
            if(Context->DistanceModel == AL_INVERSE_DISTANCE_CLAMPED)
                value = AL_TRUE;
            break;

        case AL_SPEED_OF_SOUND:
            if(Context->flSpeedOfSound != 0.0f)
                value = AL_TRUE;
            break;

        case AL_DEFERRED_UPDATES_SOFT:
            value = Context->DeferUpdates;
            break;

        default:
            alSetError(Context, AL_INVALID_ENUM);
            break;
    }

    UnlockContext(Context);

    return value;
}

AL_API ALdouble AL_APIENTRY alGetDouble(ALenum pname)
{
    ALCcontext *Context;
    ALdouble value = 0.0;

    Context = GetLockedContext();
    if(!Context) return 0.0;

    switch(pname)
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

        case AL_DEFERRED_UPDATES_SOFT:
            value = (ALdouble)Context->DeferUpdates;
            break;

        default:
            alSetError(Context, AL_INVALID_ENUM);
            break;
    }

    UnlockContext(Context);

    return value;
}

AL_API ALfloat AL_APIENTRY alGetFloat(ALenum pname)
{
    ALCcontext *Context;
    ALfloat value = 0.0f;

    Context = GetLockedContext();
    if(!Context) return 0.0f;

    switch(pname)
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

        case AL_DEFERRED_UPDATES_SOFT:
            value = (ALfloat)Context->DeferUpdates;
            break;

        default:
            alSetError(Context, AL_INVALID_ENUM);
            break;
    }

    UnlockContext(Context);

    return value;
}

AL_API ALint AL_APIENTRY alGetInteger(ALenum pname)
{
    ALCcontext *Context;
    ALint value = 0;

    Context = GetLockedContext();
    if(!Context) return 0;

    switch(pname)
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

        case AL_DEFERRED_UPDATES_SOFT:
            value = (ALint)Context->DeferUpdates;
            break;

        default:
            alSetError(Context, AL_INVALID_ENUM);
            break;
    }

    UnlockContext(Context);

    return value;
}

AL_API ALvoid AL_APIENTRY alGetBooleanv(ALenum pname,ALboolean *data)
{
    ALCcontext *Context;

    if(data)
    {
        switch(pname)
        {
            case AL_DOPPLER_FACTOR:
            case AL_DOPPLER_VELOCITY:
            case AL_DISTANCE_MODEL:
            case AL_SPEED_OF_SOUND:
            case AL_DEFERRED_UPDATES_SOFT:
                *data = alGetBoolean(pname);
                return;
        }
    }

    Context = GetLockedContext();
    if(!Context) return;

    if(data)
    {
        switch(pname)
        {
            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
        }
    }
    else
    {
        // data is a NULL pointer
        alSetError(Context, AL_INVALID_VALUE);
    }

    UnlockContext(Context);
}

AL_API ALvoid AL_APIENTRY alGetDoublev(ALenum pname,ALdouble *data)
{
    ALCcontext *Context;

    if(data)
    {
        switch(pname)
        {
            case AL_DOPPLER_FACTOR:
            case AL_DOPPLER_VELOCITY:
            case AL_DISTANCE_MODEL:
            case AL_SPEED_OF_SOUND:
            case AL_DEFERRED_UPDATES_SOFT:
                *data = alGetDouble(pname);
                return;
        }
    }

    Context = GetLockedContext();
    if(!Context) return;

    if(data)
    {
        switch(pname)
        {
            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
        }
    }
    else
    {
        // data is a NULL pointer
        alSetError(Context, AL_INVALID_VALUE);
    }

    UnlockContext(Context);
}

AL_API ALvoid AL_APIENTRY alGetFloatv(ALenum pname,ALfloat *data)
{
    ALCcontext *Context;

    if(data)
    {
        switch(pname)
        {
            case AL_DOPPLER_FACTOR:
            case AL_DOPPLER_VELOCITY:
            case AL_DISTANCE_MODEL:
            case AL_SPEED_OF_SOUND:
            case AL_DEFERRED_UPDATES_SOFT:
                *data = alGetFloat(pname);
                return;
        }
    }

    Context = GetLockedContext();
    if(!Context) return;

    if(data)
    {
        switch(pname)
        {
            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
        }
    }
    else
    {
        // data is a NULL pointer
        alSetError(Context, AL_INVALID_VALUE);
    }

    UnlockContext(Context);
}

AL_API ALvoid AL_APIENTRY alGetIntegerv(ALenum pname,ALint *data)
{
    ALCcontext *Context;

    if(data)
    {
        switch(pname)
        {
            case AL_DOPPLER_FACTOR:
            case AL_DOPPLER_VELOCITY:
            case AL_DISTANCE_MODEL:
            case AL_SPEED_OF_SOUND:
            case AL_DEFERRED_UPDATES_SOFT:
                *data = alGetInteger(pname);
                return;
        }
    }

    Context = GetLockedContext();
    if(!Context) return;

    if(data)
    {
        switch(pname)
        {
            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
        }
    }
    else
    {
        // data is a NULL pointer
        alSetError(Context, AL_INVALID_VALUE);
    }

    UnlockContext(Context);
}

AL_API const ALchar* AL_APIENTRY alGetString(ALenum pname)
{
    const ALchar *value;
    ALCcontext *pContext;

    pContext = GetLockedContext();
    if(!pContext) return NULL;

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
            alSetError(pContext, AL_INVALID_ENUM);
            break;
    }

    UnlockContext(pContext);

    return value;
}

AL_API ALvoid AL_APIENTRY alDopplerFactor(ALfloat value)
{
    ALCcontext *Context;

    Context = GetLockedContext();
    if(!Context) return;

    if(value >= 0.0f && isfinite(value))
    {
        Context->DopplerFactor = value;
        Context->UpdateSources = AL_TRUE;
    }
    else
        alSetError(Context, AL_INVALID_VALUE);

    UnlockContext(Context);
}

AL_API ALvoid AL_APIENTRY alDopplerVelocity(ALfloat value)
{
    ALCcontext *Context;

    Context = GetLockedContext();
    if(!Context) return;

    if(value > 0.0f && isfinite(value))
    {
        Context->DopplerVelocity=value;
        Context->UpdateSources = AL_TRUE;
    }
    else
        alSetError(Context, AL_INVALID_VALUE);

    UnlockContext(Context);
}

AL_API ALvoid AL_APIENTRY alSpeedOfSound(ALfloat flSpeedOfSound)
{
    ALCcontext *pContext;

    pContext = GetLockedContext();
    if(!pContext) return;

    if(flSpeedOfSound > 0.0f && isfinite(flSpeedOfSound))
    {
        pContext->flSpeedOfSound = flSpeedOfSound;
        pContext->UpdateSources = AL_TRUE;
    }
    else
        alSetError(pContext, AL_INVALID_VALUE);

    UnlockContext(pContext);
}

AL_API ALvoid AL_APIENTRY alDistanceModel(ALenum value)
{
    ALCcontext *Context;

    Context = GetLockedContext();
    if(!Context) return;

    switch(value)
    {
        case AL_NONE:
        case AL_INVERSE_DISTANCE:
        case AL_INVERSE_DISTANCE_CLAMPED:
        case AL_LINEAR_DISTANCE:
        case AL_LINEAR_DISTANCE_CLAMPED:
        case AL_EXPONENT_DISTANCE:
        case AL_EXPONENT_DISTANCE_CLAMPED:
            Context->DistanceModel = value;
            Context->UpdateSources = AL_TRUE;
            break;

        default:
            alSetError(Context, AL_INVALID_VALUE);
            break;
    }

    UnlockContext(Context);
}


AL_API ALvoid AL_APIENTRY alDeferUpdatesSOFT(void)
{
    ALCcontext *Context;

    Context = GetReffedContext();
    if(!Context) return;

    if(!Context->DeferUpdates)
    {
        ALboolean UpdateSources;
        ALsource **src, **src_end;
        ALeffectslot *ALEffectSlot;
        ALsizei e;

        LockContext(Context);
        Context->DeferUpdates = AL_TRUE;

        /* Make sure all pending updates are performed */
        UpdateSources = Exchange_ALenum(&Context->UpdateSources, AL_FALSE);

        src = Context->ActiveSources;
        src_end = src + Context->ActiveSourceCount;
        while(src != src_end)
        {
            if((*src)->state != AL_PLAYING)
            {
                Context->ActiveSourceCount--;
                *src = *(--src_end);
                continue;
            }

            if(Exchange_ALenum(&(*src)->NeedsUpdate, AL_FALSE) || UpdateSources)
                ALsource_Update(*src, Context);

            src++;
        }

        for(e = 0;e < Context->EffectSlotMap.size;e++)
        {
            ALEffectSlot = Context->EffectSlotMap.array[e].value;
            if(Exchange_ALenum(&ALEffectSlot->NeedsUpdate, AL_FALSE))
                ALEffect_Update(ALEffectSlot->EffectState, Context, ALEffectSlot);
        }
        UnlockContext(Context);
    }

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alProcessUpdatesSOFT(void)
{
    ALCcontext *Context;

    Context = GetReffedContext();
    if(!Context) return;

    if(Exchange_ALenum(&Context->DeferUpdates, AL_FALSE))
    {
        ALsizei pos;

        LockContext(Context);
        for(pos = 0;pos < Context->SourceMap.size;pos++)
        {
            ALsource *Source = Context->SourceMap.array[pos].value;
            ALenum new_state;

            if(Source->lOffset != -1)
                ApplyOffset(Source);

            new_state = Exchange_ALenum(&Source->new_state, AL_NONE);
            if(new_state)
                SetSourceState(Source, Context, new_state);
        }
        UnlockContext(Context);
    }

    ALCcontext_DecRef(Context);
}
