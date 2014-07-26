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
#include "AL/al.h"
#include "AL/alext.h"
#include "alError.h"
#include "alSource.h"
#include "alAuxEffectSlot.h"
#include "alMidi.h"

#include "midi/base.h"


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
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    switch(capability)
    {
    case AL_SOURCE_DISTANCE_MODEL:
        context->SourceDistanceModel = AL_TRUE;
        ATOMIC_STORE(&context->UpdateSources, AL_TRUE);
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alDisable(ALenum capability)
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    switch(capability)
    {
    case AL_SOURCE_DISTANCE_MODEL:
        context->SourceDistanceModel = AL_FALSE;
        ATOMIC_STORE(&context->UpdateSources, AL_TRUE);
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALboolean AL_APIENTRY alIsEnabled(ALenum capability)
{
    ALCcontext *context;
    ALboolean value=AL_FALSE;

    context = GetContextRef();
    if(!context) return AL_FALSE;

    switch(capability)
    {
    case AL_SOURCE_DISTANCE_MODEL:
        value = context->SourceDistanceModel;
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);

    return value;
}

AL_API ALboolean AL_APIENTRY alGetBoolean(ALenum pname)
{
    ALCcontext *context;
    ALboolean value=AL_FALSE;

    context = GetContextRef();
    if(!context) return AL_FALSE;

    switch(pname)
    {
    case AL_DOPPLER_FACTOR:
        if(context->DopplerFactor != 0.0f)
            value = AL_TRUE;
        break;

    case AL_DOPPLER_VELOCITY:
        if(context->DopplerVelocity != 0.0f)
            value = AL_TRUE;
        break;

    case AL_DISTANCE_MODEL:
        if(context->DistanceModel == AL_INVERSE_DISTANCE_CLAMPED)
            value = AL_TRUE;
        break;

    case AL_SPEED_OF_SOUND:
        if(context->SpeedOfSound != 0.0f)
            value = AL_TRUE;
        break;

    case AL_DEFERRED_UPDATES_SOFT:
        value = context->DeferUpdates;
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);

    return value;
}

AL_API ALdouble AL_APIENTRY alGetDouble(ALenum pname)
{
    ALCdevice *device;
    ALCcontext *context;
    ALdouble value = 0.0;

    context = GetContextRef();
    if(!context) return 0.0;

    switch(pname)
    {
    case AL_DOPPLER_FACTOR:
        value = (ALdouble)context->DopplerFactor;
        break;

    case AL_DOPPLER_VELOCITY:
        value = (ALdouble)context->DopplerVelocity;
        break;

    case AL_DISTANCE_MODEL:
        value = (ALdouble)context->DistanceModel;
        break;

    case AL_SPEED_OF_SOUND:
        value = (ALdouble)context->SpeedOfSound;
        break;

    case AL_DEFERRED_UPDATES_SOFT:
        value = (ALdouble)context->DeferUpdates;
        break;

    case AL_MIDI_GAIN_SOFT:
        device = context->Device;
        value = (ALdouble)MidiSynth_getGain(device->Synth);
        break;

    case AL_MIDI_STATE_SOFT:
        device = context->Device;
        value = (ALdouble)MidiSynth_getState(device->Synth);
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);

    return value;
}

AL_API ALfloat AL_APIENTRY alGetFloat(ALenum pname)
{
    ALCdevice *device;
    ALCcontext *context;
    ALfloat value = 0.0f;

    context = GetContextRef();
    if(!context) return 0.0f;

    switch(pname)
    {
    case AL_DOPPLER_FACTOR:
        value = context->DopplerFactor;
        break;

    case AL_DOPPLER_VELOCITY:
        value = context->DopplerVelocity;
        break;

    case AL_DISTANCE_MODEL:
        value = (ALfloat)context->DistanceModel;
        break;

    case AL_SPEED_OF_SOUND:
        value = context->SpeedOfSound;
        break;

    case AL_DEFERRED_UPDATES_SOFT:
        value = (ALfloat)context->DeferUpdates;
        break;

    case AL_MIDI_GAIN_SOFT:
        device = context->Device;
        value = MidiSynth_getGain(device->Synth);
        break;

    case AL_MIDI_STATE_SOFT:
        device = context->Device;
        value = (ALfloat)MidiSynth_getState(device->Synth);
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);

    return value;
}

AL_API ALint AL_APIENTRY alGetInteger(ALenum pname)
{
    ALCcontext *context;
    ALCdevice *device;
    MidiSynth *synth;
    ALint value = 0;

    context = GetContextRef();
    if(!context) return 0;

    switch(pname)
    {
    case AL_DOPPLER_FACTOR:
        value = (ALint)context->DopplerFactor;
        break;

    case AL_DOPPLER_VELOCITY:
        value = (ALint)context->DopplerVelocity;
        break;

    case AL_DISTANCE_MODEL:
        value = (ALint)context->DistanceModel;
        break;

    case AL_SPEED_OF_SOUND:
        value = (ALint)context->SpeedOfSound;
        break;

    case AL_DEFERRED_UPDATES_SOFT:
        value = (ALint)context->DeferUpdates;
        break;

    case AL_SOUNDFONTS_SIZE_SOFT:
        device = context->Device;
        synth = device->Synth;
        value = synth->NumSoundfonts;
        break;

    case AL_MIDI_STATE_SOFT:
        device = context->Device;
        value = MidiSynth_getState(device->Synth);
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);

    return value;
}

AL_API ALint64SOFT AL_APIENTRY alGetInteger64SOFT(ALenum pname)
{
    ALCcontext *context;
    ALCdevice *device;
    MidiSynth *synth;
    ALint64SOFT value = 0;

    context = GetContextRef();
    if(!context) return 0;

    switch(pname)
    {
    case AL_DOPPLER_FACTOR:
        value = (ALint64SOFT)context->DopplerFactor;
        break;

    case AL_DOPPLER_VELOCITY:
        value = (ALint64SOFT)context->DopplerVelocity;
        break;

    case AL_DISTANCE_MODEL:
        value = (ALint64SOFT)context->DistanceModel;
        break;

    case AL_SPEED_OF_SOUND:
        value = (ALint64SOFT)context->SpeedOfSound;
        break;

    case AL_DEFERRED_UPDATES_SOFT:
        value = (ALint64SOFT)context->DeferUpdates;
        break;

    case AL_MIDI_CLOCK_SOFT:
        device = context->Device;
        ALCdevice_Lock(device);
        value = MidiSynth_getTime(device->Synth);
        ALCdevice_Unlock(device);
        break;

    case AL_SOUNDFONTS_SIZE_SOFT:
        device = context->Device;
        synth = device->Synth;
        value = (ALint64SOFT)synth->NumSoundfonts;
        break;

    case AL_MIDI_STATE_SOFT:
        device = context->Device;
        value = (ALint64SOFT)MidiSynth_getState(device->Synth);
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);

    return value;
}

AL_API ALvoid AL_APIENTRY alGetBooleanv(ALenum pname, ALboolean *values)
{
    ALCcontext *context;

    if(values)
    {
        switch(pname)
        {
            case AL_DOPPLER_FACTOR:
            case AL_DOPPLER_VELOCITY:
            case AL_DISTANCE_MODEL:
            case AL_SPEED_OF_SOUND:
            case AL_DEFERRED_UPDATES_SOFT:
                values[0] = alGetBoolean(pname);
                return;
        }
    }

    context = GetContextRef();
    if(!context) return;

    if(!(values))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch(pname)
    {
    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alGetDoublev(ALenum pname, ALdouble *values)
{
    ALCcontext *context;

    if(values)
    {
        switch(pname)
        {
            case AL_DOPPLER_FACTOR:
            case AL_DOPPLER_VELOCITY:
            case AL_DISTANCE_MODEL:
            case AL_SPEED_OF_SOUND:
            case AL_DEFERRED_UPDATES_SOFT:
            case AL_MIDI_GAIN_SOFT:
            case AL_MIDI_STATE_SOFT:
                values[0] = alGetDouble(pname);
                return;
        }
    }

    context = GetContextRef();
    if(!context) return;

    if(!(values))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch(pname)
    {
    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alGetFloatv(ALenum pname, ALfloat *values)
{
    ALCcontext *context;

    if(values)
    {
        switch(pname)
        {
            case AL_DOPPLER_FACTOR:
            case AL_DOPPLER_VELOCITY:
            case AL_DISTANCE_MODEL:
            case AL_SPEED_OF_SOUND:
            case AL_DEFERRED_UPDATES_SOFT:
            case AL_MIDI_GAIN_SOFT:
            case AL_MIDI_STATE_SOFT:
                values[0] = alGetFloat(pname);
                return;
        }
    }

    context = GetContextRef();
    if(!context) return;

    if(!(values))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
    switch(pname)
    {
    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alGetIntegerv(ALenum pname, ALint *values)
{
    ALCcontext *context;
    ALCdevice *device;
    MidiSynth *synth;
    ALsizei i;

    if(values)
    {
        switch(pname)
        {
            case AL_DOPPLER_FACTOR:
            case AL_DOPPLER_VELOCITY:
            case AL_DISTANCE_MODEL:
            case AL_SPEED_OF_SOUND:
            case AL_DEFERRED_UPDATES_SOFT:
            case AL_SOUNDFONTS_SIZE_SOFT:
            case AL_MIDI_STATE_SOFT:
                values[0] = alGetInteger(pname);
                return;
        }
    }

    context = GetContextRef();
    if(!context) return;

    switch(pname)
    {
    case AL_SOUNDFONTS_SOFT:
        device = context->Device;
        synth = device->Synth;
        if(synth->NumSoundfonts > 0)
        {
            if(!(values))
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
            for(i = 0;i < synth->NumSoundfonts;i++)
                values[i] = synth->Soundfonts[i]->id;
        }
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alGetInteger64vSOFT(ALenum pname, ALint64SOFT *values)
{
    ALCcontext *context;
    ALCdevice *device;
    MidiSynth *synth;
    ALsizei i;

    if(values)
    {
        switch(pname)
        {
            case AL_DOPPLER_FACTOR:
            case AL_DOPPLER_VELOCITY:
            case AL_DISTANCE_MODEL:
            case AL_SPEED_OF_SOUND:
            case AL_DEFERRED_UPDATES_SOFT:
            case AL_MIDI_CLOCK_SOFT:
            case AL_SOUNDFONTS_SIZE_SOFT:
            case AL_MIDI_STATE_SOFT:
                values[0] = alGetInteger64SOFT(pname);
                return;
        }
    }

    context = GetContextRef();
    if(!context) return;

    switch(pname)
    {
    case AL_SOUNDFONTS_SOFT:
        device = context->Device;
        synth = device->Synth;
        if(synth->NumSoundfonts > 0)
        {
            if(!(values))
                SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);
            for(i = 0;i < synth->NumSoundfonts;i++)
                values[i] = (ALint64SOFT)synth->Soundfonts[i]->id;
        }
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API const ALchar* AL_APIENTRY alGetString(ALenum pname)
{
    const ALchar *value = NULL;
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return NULL;

    switch(pname)
    {
    case AL_VENDOR:
        value = alVendor;
        break;

    case AL_VERSION:
        value = alVersion;
        break;

    case AL_RENDERER:
        value = alRenderer;
        break;

    case AL_EXTENSIONS:
        value = context->ExtensionList;
        break;

    case AL_NO_ERROR:
        value = alNoError;
        break;

    case AL_INVALID_NAME:
        value = alErrInvalidName;
        break;

    case AL_INVALID_ENUM:
        value = alErrInvalidEnum;
        break;

    case AL_INVALID_VALUE:
        value = alErrInvalidValue;
        break;

    case AL_INVALID_OPERATION:
        value = alErrInvalidOp;
        break;

    case AL_OUT_OF_MEMORY:
        value = alErrOutOfMemory;
        break;

    default:
        SET_ERROR_AND_GOTO(context, AL_INVALID_ENUM, done);
    }

done:
    ALCcontext_DecRef(context);

    return value;
}

AL_API ALvoid AL_APIENTRY alDopplerFactor(ALfloat value)
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    if(!(value >= 0.0f && isfinite(value)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    context->DopplerFactor = value;
    ATOMIC_STORE(&context->UpdateSources, AL_TRUE);

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alDopplerVelocity(ALfloat value)
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    if(!(value >= 0.0f && isfinite(value)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    context->DopplerVelocity = value;
    ATOMIC_STORE(&context->UpdateSources, AL_TRUE);

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alSpeedOfSound(ALfloat value)
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    if(!(value > 0.0f && isfinite(value)))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    context->SpeedOfSound = value;
    ATOMIC_STORE(&context->UpdateSources, AL_TRUE);

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alDistanceModel(ALenum value)
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    if(!(value == AL_INVERSE_DISTANCE || value == AL_INVERSE_DISTANCE_CLAMPED ||
         value == AL_LINEAR_DISTANCE || value == AL_LINEAR_DISTANCE_CLAMPED ||
         value == AL_EXPONENT_DISTANCE || value == AL_EXPONENT_DISTANCE_CLAMPED ||
         value == AL_NONE))
        SET_ERROR_AND_GOTO(context, AL_INVALID_VALUE, done);

    context->DistanceModel = value;
    if(!context->SourceDistanceModel)
        ATOMIC_STORE(&context->UpdateSources, AL_TRUE);

done:
    ALCcontext_DecRef(context);
}


AL_API ALvoid AL_APIENTRY alDeferUpdatesSOFT(void)
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    if(!context->DeferUpdates)
    {
        ALboolean UpdateSources;
        ALactivesource **src, **src_end;
        ALeffectslot **slot, **slot_end;
        FPUCtl oldMode;

        SetMixerFPUMode(&oldMode);

        LockContext(context);
        context->DeferUpdates = AL_TRUE;

        /* Make sure all pending updates are performed */
        UpdateSources = ATOMIC_EXCHANGE(ALenum, &context->UpdateSources, AL_FALSE);

        src = context->ActiveSources;
        src_end = src + context->ActiveSourceCount;
        while(src != src_end)
        {
            ALsource *source = (*src)->Source;

            if(source->state != AL_PLAYING && source->state != AL_PAUSED)
            {
                ALactivesource *temp = *(--src_end);
                *src_end = *src;
                *src = temp;
                --(context->ActiveSourceCount);
                continue;
            }

            if(ATOMIC_EXCHANGE(ALenum, &source->NeedsUpdate, AL_FALSE) || UpdateSources)
                (*src)->Update(*src, context);

            src++;
        }

        slot = VECTOR_ITER_BEGIN(context->ActiveAuxSlots);
        slot_end = VECTOR_ITER_END(context->ActiveAuxSlots);
        while(slot != slot_end)
        {
            if(ATOMIC_EXCHANGE(ALenum, &(*slot)->NeedsUpdate, AL_FALSE))
                V((*slot)->EffectState,update)(context->Device, *slot);
            slot++;
        }

        UnlockContext(context);
        RestoreFPUMode(&oldMode);
    }

    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alProcessUpdatesSOFT(void)
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    if(ExchangeInt(&context->DeferUpdates, AL_FALSE))
    {
        ALsizei pos;

        LockContext(context);
        LockUIntMapRead(&context->SourceMap);
        for(pos = 0;pos < context->SourceMap.size;pos++)
        {
            ALsource *Source = context->SourceMap.array[pos].value;
            ALenum new_state;

            if((Source->state == AL_PLAYING || Source->state == AL_PAUSED) &&
               Source->Offset >= 0.0)
            {
                ReadLock(&Source->queue_lock);
                ApplyOffset(Source);
                ReadUnlock(&Source->queue_lock);
            }

            new_state = ExchangeInt(&Source->new_state, AL_NONE);
            if(new_state)
                SetSourceState(Source, context, new_state);
        }
        UnlockUIntMapRead(&context->SourceMap);
        UnlockContext(context);
    }

    ALCcontext_DecRef(context);
}
