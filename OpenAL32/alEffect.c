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
 *  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA  02111-1307, USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <stdlib.h>
#include <math.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alEffect.h"
#include "alThunk.h"
#include "alError.h"


ALboolean DisabledEffects[MAX_EFFECTS];


static void InitEffectParams(ALeffect *effect, ALenum type);

#define LookupEffect(m, k) ((ALeffect*)LookupUIntMapKey(&(m), (k)))
#define RemoveEffect(m, k) ((ALeffect*)PopUIntMapValue(&(m), (k)))

AL_API ALvoid AL_APIENTRY alGenEffects(ALsizei n, ALuint *effects)
{
    ALCcontext *Context;
    ALsizei i;

    Context = GetContextRef();
    if(!Context) return;

    if(n < 0 || IsBadWritePtr((void*)effects, n * sizeof(ALuint)))
        alSetError(Context, AL_INVALID_VALUE);
    else
    {
        ALCdevice *device = Context->Device;
        ALenum err;

        for(i = 0;i < n;i++)
        {
            ALeffect *effect = calloc(1, sizeof(ALeffect));
            if(!effect)
            {
                alSetError(Context, AL_OUT_OF_MEMORY);
                alDeleteEffects(i, effects);
                break;
            }
            InitEffectParams(effect, AL_EFFECT_NULL);

            err = NewThunkEntry(&effect->effect);
            if(err == AL_NO_ERROR)
                err = InsertUIntMapEntry(&device->EffectMap, effect->effect, effect);
            if(err != AL_NO_ERROR)
            {
                FreeThunkEntry(effect->effect);
                memset(effect, 0, sizeof(ALeffect));
                free(effect);

                alSetError(Context, err);
                alDeleteEffects(i, effects);
                break;
            }

            effects[i] = effect->effect;
        }
    }

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alDeleteEffects(ALsizei n, ALuint *effects)
{
    ALCcontext *Context;
    ALCdevice *device;
    ALeffect *ALEffect;
    ALsizei i;

    Context = GetContextRef();
    if(!Context) return;

    device = Context->Device;
    if(n < 0)
        alSetError(Context, AL_INVALID_VALUE);
    else
    {
        // Check that all effects are valid
        for(i = 0;i < n;i++)
        {
            if(!effects[i])
                continue;

            if(LookupEffect(device->EffectMap, effects[i]) == NULL)
            {
                alSetError(Context, AL_INVALID_NAME);
                n = 0;
                break;
            }
        }

        for(i = 0;i < n;i++)
        {
            // Recheck that the effect is valid, because there could be duplicated names
            if((ALEffect=RemoveEffect(device->EffectMap, effects[i])) == NULL)
                continue;
            FreeThunkEntry(ALEffect->effect);

            memset(ALEffect, 0, sizeof(ALeffect));
            free(ALEffect);
        }
    }

    ALCcontext_DecRef(Context);
}

AL_API ALboolean AL_APIENTRY alIsEffect(ALuint effect)
{
    ALCcontext *Context;
    ALboolean  result;

    Context = GetContextRef();
    if(!Context) return AL_FALSE;

    result = ((!effect || LookupEffect(Context->Device->EffectMap, effect)) ?
              AL_TRUE : AL_FALSE);

    ALCcontext_DecRef(Context);

    return result;
}

AL_API ALvoid AL_APIENTRY alEffecti(ALuint effect, ALenum param, ALint iValue)
{
    ALCcontext *Context;
    ALCdevice  *Device;
    ALeffect   *ALEffect;

    Context = GetLockedContext();
    if(!Context) return;

    Device = Context->Device;
    if((ALEffect=LookupEffect(Device->EffectMap, effect)) != NULL)
    {
        if(param == AL_EFFECT_TYPE)
        {
            ALboolean isOk = (iValue == AL_EFFECT_NULL);
            ALint i;
            for(i = 0;!isOk && EffectList[i].val;i++)
            {
                if(iValue == EffectList[i].val &&
                   !DisabledEffects[EffectList[i].type])
                    isOk = AL_TRUE;
            }

            if(isOk)
                InitEffectParams(ALEffect, iValue);
            else
                alSetError(Context, AL_INVALID_VALUE);
        }
        else if(ALEffect->type == AL_EFFECT_EAXREVERB)
        {
            switch(param)
            {
            case AL_EAXREVERB_DECAY_HFLIMIT:
                if(iValue >= AL_EAXREVERB_MIN_DECAY_HFLIMIT &&
                   iValue <= AL_EAXREVERB_MAX_DECAY_HFLIMIT)
                    ALEffect->Params.Reverb.DecayHFLimit = iValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
            }
        }
        else if(ALEffect->type == AL_EFFECT_REVERB)
        {
            switch(param)
            {
            case AL_REVERB_DECAY_HFLIMIT:
                if(iValue >= AL_REVERB_MIN_DECAY_HFLIMIT &&
                   iValue <= AL_REVERB_MAX_DECAY_HFLIMIT)
                    ALEffect->Params.Reverb.DecayHFLimit = iValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
            }
        }
        else if(ALEffect->type == AL_EFFECT_ECHO)
        {
            switch(param)
            {
            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
            }
        }
        else if(ALEffect->type == AL_EFFECT_RING_MODULATOR)
        {
            switch(param)
            {
            case AL_RING_MODULATOR_FREQUENCY:
                if(iValue >= AL_RING_MODULATOR_MIN_FREQUENCY &&
                   iValue <= AL_RING_MODULATOR_MAX_FREQUENCY)
                    ALEffect->Params.Modulator.Frequency = iValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
                if(iValue >= AL_RING_MODULATOR_MIN_HIGHPASS_CUTOFF &&
                   iValue <= AL_RING_MODULATOR_MAX_HIGHPASS_CUTOFF)
                    ALEffect->Params.Modulator.HighPassCutoff = iValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_RING_MODULATOR_WAVEFORM:
                if(iValue >= AL_RING_MODULATOR_MIN_WAVEFORM &&
                   iValue <= AL_RING_MODULATOR_MAX_WAVEFORM)
                    ALEffect->Params.Modulator.Waveform = iValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
            }
        }
        else
            alSetError(Context, AL_INVALID_ENUM);
    }
    else
        alSetError(Context, AL_INVALID_NAME);

    UnlockContext(Context);
}

AL_API ALvoid AL_APIENTRY alEffectiv(ALuint effect, ALenum param, ALint *piValues)
{
    /* There are no multi-value int effect parameters */
    alEffecti(effect, param, piValues[0]);
}

AL_API ALvoid AL_APIENTRY alEffectf(ALuint effect, ALenum param, ALfloat flValue)
{
    ALCcontext *Context;
    ALCdevice  *Device;
    ALeffect   *ALEffect;

    Context = GetLockedContext();
    if(!Context) return;

    Device = Context->Device;
    if((ALEffect=LookupEffect(Device->EffectMap, effect)) != NULL)
    {
        if(ALEffect->type == AL_EFFECT_EAXREVERB)
        {
            switch(param)
            {
            case AL_EAXREVERB_DENSITY:
                if(flValue >= AL_EAXREVERB_MIN_DENSITY &&
                   flValue <= AL_EAXREVERB_MAX_DENSITY)
                    ALEffect->Params.Reverb.Density = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_EAXREVERB_DIFFUSION:
                if(flValue >= AL_EAXREVERB_MIN_DIFFUSION &&
                   flValue <= AL_EAXREVERB_MAX_DIFFUSION)
                    ALEffect->Params.Reverb.Diffusion = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_EAXREVERB_GAIN:
                if(flValue >= AL_EAXREVERB_MIN_GAIN &&
                   flValue <= AL_EAXREVERB_MAX_GAIN)
                    ALEffect->Params.Reverb.Gain = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_EAXREVERB_GAINHF:
                if(flValue >= AL_EAXREVERB_MIN_GAINHF &&
                   flValue <= AL_EAXREVERB_MAX_GAINHF)
                    ALEffect->Params.Reverb.GainHF = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_EAXREVERB_GAINLF:
                if(flValue >= AL_EAXREVERB_MIN_GAINLF &&
                   flValue <= AL_EAXREVERB_MAX_GAINLF)
                    ALEffect->Params.Reverb.GainLF = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_EAXREVERB_DECAY_TIME:
                if(flValue >= AL_EAXREVERB_MIN_DECAY_TIME &&
                   flValue <= AL_EAXREVERB_MAX_DECAY_TIME)
                    ALEffect->Params.Reverb.DecayTime = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_EAXREVERB_DECAY_HFRATIO:
                if(flValue >= AL_EAXREVERB_MIN_DECAY_HFRATIO &&
                   flValue <= AL_EAXREVERB_MAX_DECAY_HFRATIO)
                    ALEffect->Params.Reverb.DecayHFRatio = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_EAXREVERB_DECAY_LFRATIO:
                if(flValue >= AL_EAXREVERB_MIN_DECAY_LFRATIO &&
                   flValue <= AL_EAXREVERB_MAX_DECAY_LFRATIO)
                    ALEffect->Params.Reverb.DecayLFRatio = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_EAXREVERB_REFLECTIONS_GAIN:
                if(flValue >= AL_EAXREVERB_MIN_REFLECTIONS_GAIN &&
                   flValue <= AL_EAXREVERB_MAX_REFLECTIONS_GAIN)
                    ALEffect->Params.Reverb.ReflectionsGain = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_EAXREVERB_REFLECTIONS_DELAY:
                if(flValue >= AL_EAXREVERB_MIN_REFLECTIONS_DELAY &&
                   flValue <= AL_EAXREVERB_MAX_REFLECTIONS_DELAY)
                    ALEffect->Params.Reverb.ReflectionsDelay = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_EAXREVERB_LATE_REVERB_GAIN:
                if(flValue >= AL_EAXREVERB_MIN_LATE_REVERB_GAIN &&
                   flValue <= AL_EAXREVERB_MAX_LATE_REVERB_GAIN)
                    ALEffect->Params.Reverb.LateReverbGain = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_EAXREVERB_LATE_REVERB_DELAY:
                if(flValue >= AL_EAXREVERB_MIN_LATE_REVERB_DELAY &&
                   flValue <= AL_EAXREVERB_MAX_LATE_REVERB_DELAY)
                    ALEffect->Params.Reverb.LateReverbDelay = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_EAXREVERB_AIR_ABSORPTION_GAINHF:
                if(flValue >= AL_EAXREVERB_MIN_AIR_ABSORPTION_GAINHF &&
                   flValue <= AL_EAXREVERB_MAX_AIR_ABSORPTION_GAINHF)
                    ALEffect->Params.Reverb.AirAbsorptionGainHF = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_EAXREVERB_ECHO_TIME:
                if(flValue >= AL_EAXREVERB_MIN_ECHO_TIME &&
                   flValue <= AL_EAXREVERB_MAX_ECHO_TIME)
                    ALEffect->Params.Reverb.EchoTime = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_EAXREVERB_ECHO_DEPTH:
                if(flValue >= AL_EAXREVERB_MIN_ECHO_DEPTH &&
                   flValue <= AL_EAXREVERB_MAX_ECHO_DEPTH)
                    ALEffect->Params.Reverb.EchoDepth = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_EAXREVERB_MODULATION_TIME:
                if(flValue >= AL_EAXREVERB_MIN_MODULATION_TIME &&
                   flValue <= AL_EAXREVERB_MAX_MODULATION_TIME)
                    ALEffect->Params.Reverb.ModulationTime = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_EAXREVERB_MODULATION_DEPTH:
                if(flValue >= AL_EAXREVERB_MIN_MODULATION_DEPTH &&
                   flValue <= AL_EAXREVERB_MAX_MODULATION_DEPTH)
                    ALEffect->Params.Reverb.ModulationDepth = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_EAXREVERB_HFREFERENCE:
                if(flValue >= AL_EAXREVERB_MIN_HFREFERENCE &&
                   flValue <= AL_EAXREVERB_MAX_HFREFERENCE)
                    ALEffect->Params.Reverb.HFReference = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_EAXREVERB_LFREFERENCE:
                if(flValue >= AL_EAXREVERB_MIN_LFREFERENCE &&
                   flValue <= AL_EAXREVERB_MAX_LFREFERENCE)
                    ALEffect->Params.Reverb.LFReference = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_EAXREVERB_ROOM_ROLLOFF_FACTOR:
                if(flValue >= 0.0f && flValue <= 10.0f)
                    ALEffect->Params.Reverb.RoomRolloffFactor = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
            }
        }
        else if(ALEffect->type == AL_EFFECT_REVERB)
        {
            switch(param)
            {
            case AL_REVERB_DENSITY:
                if(flValue >= AL_REVERB_MIN_DENSITY &&
                   flValue <= AL_REVERB_MAX_DENSITY)
                    ALEffect->Params.Reverb.Density = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_REVERB_DIFFUSION:
                if(flValue >= AL_REVERB_MIN_DIFFUSION &&
                   flValue <= AL_REVERB_MAX_DIFFUSION)
                    ALEffect->Params.Reverb.Diffusion = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_REVERB_GAIN:
                if(flValue >= AL_REVERB_MIN_GAIN &&
                   flValue <= AL_REVERB_MAX_GAIN)
                    ALEffect->Params.Reverb.Gain = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_REVERB_GAINHF:
                if(flValue >= AL_REVERB_MIN_GAINHF &&
                   flValue <= AL_REVERB_MAX_GAINHF)
                    ALEffect->Params.Reverb.GainHF = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_REVERB_DECAY_TIME:
                if(flValue >= AL_REVERB_MIN_DECAY_TIME &&
                   flValue <= AL_REVERB_MAX_DECAY_TIME)
                    ALEffect->Params.Reverb.DecayTime = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_REVERB_DECAY_HFRATIO:
                if(flValue >= AL_REVERB_MIN_DECAY_HFRATIO &&
                   flValue <= AL_REVERB_MAX_DECAY_HFRATIO)
                    ALEffect->Params.Reverb.DecayHFRatio = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_REVERB_REFLECTIONS_GAIN:
                if(flValue >= AL_REVERB_MIN_REFLECTIONS_GAIN &&
                   flValue <= AL_REVERB_MAX_REFLECTIONS_GAIN)
                    ALEffect->Params.Reverb.ReflectionsGain = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_REVERB_REFLECTIONS_DELAY:
                if(flValue >= AL_REVERB_MIN_REFLECTIONS_DELAY &&
                   flValue <= AL_REVERB_MAX_REFLECTIONS_DELAY)
                    ALEffect->Params.Reverb.ReflectionsDelay = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_REVERB_LATE_REVERB_GAIN:
                if(flValue >= AL_REVERB_MIN_LATE_REVERB_GAIN &&
                   flValue <= AL_REVERB_MAX_LATE_REVERB_GAIN)
                    ALEffect->Params.Reverb.LateReverbGain = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_REVERB_LATE_REVERB_DELAY:
                if(flValue >= AL_REVERB_MIN_LATE_REVERB_DELAY &&
                   flValue <= AL_REVERB_MAX_LATE_REVERB_DELAY)
                    ALEffect->Params.Reverb.LateReverbDelay = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_REVERB_AIR_ABSORPTION_GAINHF:
                if(flValue >= AL_REVERB_MIN_AIR_ABSORPTION_GAINHF &&
                   flValue <= AL_REVERB_MAX_AIR_ABSORPTION_GAINHF)
                    ALEffect->Params.Reverb.AirAbsorptionGainHF = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_REVERB_ROOM_ROLLOFF_FACTOR:
                if(flValue >= AL_REVERB_MIN_ROOM_ROLLOFF_FACTOR &&
                   flValue <= AL_REVERB_MAX_ROOM_ROLLOFF_FACTOR)
                    ALEffect->Params.Reverb.RoomRolloffFactor = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
            }
        }
        else if(ALEffect->type == AL_EFFECT_ECHO)
        {
            switch(param)
            {
            case AL_ECHO_DELAY:
                if(flValue >= AL_ECHO_MIN_DELAY && flValue <= AL_ECHO_MAX_DELAY)
                    ALEffect->Params.Echo.Delay = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_ECHO_LRDELAY:
                if(flValue >= AL_ECHO_MIN_LRDELAY && flValue <= AL_ECHO_MAX_LRDELAY)
                    ALEffect->Params.Echo.LRDelay = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_ECHO_DAMPING:
                if(flValue >= AL_ECHO_MIN_DAMPING && flValue <= AL_ECHO_MAX_DAMPING)
                    ALEffect->Params.Echo.Damping = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_ECHO_FEEDBACK:
                if(flValue >= AL_ECHO_MIN_FEEDBACK && flValue <= AL_ECHO_MAX_FEEDBACK)
                    ALEffect->Params.Echo.Feedback = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_ECHO_SPREAD:
                if(flValue >= AL_ECHO_MIN_SPREAD && flValue <= AL_ECHO_MAX_SPREAD)
                    ALEffect->Params.Echo.Spread = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
            }
        }
        else if(ALEffect->type == AL_EFFECT_RING_MODULATOR)
        {
            switch(param)
            {
            case AL_RING_MODULATOR_FREQUENCY:
                if(flValue >= AL_RING_MODULATOR_MIN_FREQUENCY &&
                   flValue <= AL_RING_MODULATOR_MAX_FREQUENCY)
                    ALEffect->Params.Modulator.Frequency = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
                if(flValue >= AL_RING_MODULATOR_MIN_HIGHPASS_CUTOFF &&
                   flValue <= AL_RING_MODULATOR_MAX_HIGHPASS_CUTOFF)
                    ALEffect->Params.Modulator.HighPassCutoff = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
            }
        }
        else if(ALEffect->type == AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT ||
                ALEffect->type == AL_EFFECT_DEDICATED_DIALOGUE)
        {
            switch(param)
            {
            case AL_DEDICATED_GAIN:
                if(flValue >= 0.0f && isfinite(flValue))
                    ALEffect->Params.Dedicated.Gain = flValue;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
            }
        }
        else
            alSetError(Context, AL_INVALID_ENUM);
    }
    else
        alSetError(Context, AL_INVALID_NAME);

    UnlockContext(Context);
}

AL_API ALvoid AL_APIENTRY alEffectfv(ALuint effect, ALenum param, ALfloat *pflValues)
{
    ALCcontext *Context;
    ALCdevice  *Device;
    ALeffect   *ALEffect;

    Context = GetLockedContext();
    if(!Context) return;

    Device = Context->Device;
    if((ALEffect=LookupEffect(Device->EffectMap, effect)) != NULL)
    {
        if(ALEffect->type == AL_EFFECT_EAXREVERB)
        {
            switch(param)
            {
            case AL_EAXREVERB_REFLECTIONS_PAN:
                if(isfinite(pflValues[0]) && isfinite(pflValues[1]) && isfinite(pflValues[2]))
                {
                    ALEffect->Params.Reverb.ReflectionsPan[0] = pflValues[0];
                    ALEffect->Params.Reverb.ReflectionsPan[1] = pflValues[1];
                    ALEffect->Params.Reverb.ReflectionsPan[2] = pflValues[2];
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;
            case AL_EAXREVERB_LATE_REVERB_PAN:
                if(isfinite(pflValues[0]) && isfinite(pflValues[1]) && isfinite(pflValues[2]))
                {
                    ALEffect->Params.Reverb.LateReverbPan[0] = pflValues[0];
                    ALEffect->Params.Reverb.LateReverbPan[1] = pflValues[1];
                    ALEffect->Params.Reverb.LateReverbPan[2] = pflValues[2];
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            default:
                UnlockContext(Context);
                alEffectf(effect, param, pflValues[0]);
                return;
            }
        }
        else
        {
            UnlockContext(Context);
            alEffectf(effect, param, pflValues[0]);
            return;
        }
    }
    else
        alSetError(Context, AL_INVALID_NAME);

    UnlockContext(Context);
}

AL_API ALvoid AL_APIENTRY alGetEffecti(ALuint effect, ALenum param, ALint *piValue)
{
    ALCcontext *Context;
    ALCdevice  *Device;
    ALeffect   *ALEffect;

    Context = GetLockedContext();
    if(!Context) return;

    Device = Context->Device;
    if((ALEffect=LookupEffect(Device->EffectMap, effect)) != NULL)
    {
        if(param == AL_EFFECT_TYPE)
        {
            *piValue = ALEffect->type;
        }
        else if(ALEffect->type == AL_EFFECT_EAXREVERB)
        {
            switch(param)
            {
            case AL_EAXREVERB_DECAY_HFLIMIT:
                *piValue = ALEffect->Params.Reverb.DecayHFLimit;
                break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
            }
        }
        else if(ALEffect->type == AL_EFFECT_REVERB)
        {
            switch(param)
            {
            case AL_REVERB_DECAY_HFLIMIT:
                *piValue = ALEffect->Params.Reverb.DecayHFLimit;
                break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
            }
        }
        else if(ALEffect->type == AL_EFFECT_ECHO)
        {
            switch(param)
            {
            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
            }
        }
        else if(ALEffect->type == AL_EFFECT_RING_MODULATOR)
        {
            switch(param)
            {
            case AL_RING_MODULATOR_FREQUENCY:
                *piValue = (ALint)ALEffect->Params.Modulator.Frequency;
                break;
            case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
                *piValue = (ALint)ALEffect->Params.Modulator.HighPassCutoff;
                break;
            case AL_RING_MODULATOR_WAVEFORM:
                *piValue = ALEffect->Params.Modulator.Waveform;
                break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
            }
        }
        else
            alSetError(Context, AL_INVALID_ENUM);
    }
    else
        alSetError(Context, AL_INVALID_NAME);

    UnlockContext(Context);
}

AL_API ALvoid AL_APIENTRY alGetEffectiv(ALuint effect, ALenum param, ALint *piValues)
{
    /* There are no multi-value int effect parameters */
    alGetEffecti(effect, param, piValues);
}

AL_API ALvoid AL_APIENTRY alGetEffectf(ALuint effect, ALenum param, ALfloat *pflValue)
{
    ALCcontext *Context;
    ALCdevice  *Device;
    ALeffect   *ALEffect;

    Context = GetLockedContext();
    if(!Context) return;

    Device = Context->Device;
    if((ALEffect=LookupEffect(Device->EffectMap, effect)) != NULL)
    {
        if(ALEffect->type == AL_EFFECT_EAXREVERB)
        {
            switch(param)
            {
            case AL_EAXREVERB_DENSITY:
                *pflValue = ALEffect->Params.Reverb.Density;
                break;

            case AL_EAXREVERB_DIFFUSION:
                *pflValue = ALEffect->Params.Reverb.Diffusion;
                break;

            case AL_EAXREVERB_GAIN:
                *pflValue = ALEffect->Params.Reverb.Gain;
                break;

            case AL_EAXREVERB_GAINHF:
                *pflValue = ALEffect->Params.Reverb.GainHF;
                break;

            case AL_EAXREVERB_GAINLF:
                *pflValue = ALEffect->Params.Reverb.GainLF;
                break;

            case AL_EAXREVERB_DECAY_TIME:
                *pflValue = ALEffect->Params.Reverb.DecayTime;
                break;

            case AL_EAXREVERB_DECAY_HFRATIO:
                *pflValue = ALEffect->Params.Reverb.DecayHFRatio;
                break;

            case AL_EAXREVERB_DECAY_LFRATIO:
                *pflValue = ALEffect->Params.Reverb.DecayLFRatio;
                break;

            case AL_EAXREVERB_REFLECTIONS_GAIN:
                *pflValue = ALEffect->Params.Reverb.ReflectionsGain;
                break;

            case AL_EAXREVERB_REFLECTIONS_DELAY:
                *pflValue = ALEffect->Params.Reverb.ReflectionsDelay;
                break;

            case AL_EAXREVERB_LATE_REVERB_GAIN:
                *pflValue = ALEffect->Params.Reverb.LateReverbGain;
                break;

            case AL_EAXREVERB_LATE_REVERB_DELAY:
                *pflValue = ALEffect->Params.Reverb.LateReverbDelay;
                break;

            case AL_EAXREVERB_AIR_ABSORPTION_GAINHF:
                *pflValue = ALEffect->Params.Reverb.AirAbsorptionGainHF;
                break;

            case AL_EAXREVERB_ECHO_TIME:
                *pflValue = ALEffect->Params.Reverb.EchoTime;
                break;

            case AL_EAXREVERB_ECHO_DEPTH:
                *pflValue = ALEffect->Params.Reverb.EchoDepth;
                break;

            case AL_EAXREVERB_MODULATION_TIME:
                *pflValue = ALEffect->Params.Reverb.ModulationTime;
                break;

            case AL_EAXREVERB_MODULATION_DEPTH:
                *pflValue = ALEffect->Params.Reverb.ModulationDepth;
                break;

            case AL_EAXREVERB_HFREFERENCE:
                *pflValue = ALEffect->Params.Reverb.HFReference;
                break;

            case AL_EAXREVERB_LFREFERENCE:
                *pflValue = ALEffect->Params.Reverb.LFReference;
                break;

            case AL_EAXREVERB_ROOM_ROLLOFF_FACTOR:
                *pflValue = ALEffect->Params.Reverb.RoomRolloffFactor;
                break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
            }
        }
        else if(ALEffect->type == AL_EFFECT_REVERB)
        {
            switch(param)
            {
            case AL_REVERB_DENSITY:
                *pflValue = ALEffect->Params.Reverb.Density;
                break;

            case AL_REVERB_DIFFUSION:
                *pflValue = ALEffect->Params.Reverb.Diffusion;
                break;

            case AL_REVERB_GAIN:
                *pflValue = ALEffect->Params.Reverb.Gain;
                break;

            case AL_REVERB_GAINHF:
                *pflValue = ALEffect->Params.Reverb.GainHF;
                break;

            case AL_REVERB_DECAY_TIME:
                *pflValue = ALEffect->Params.Reverb.DecayTime;
                break;

            case AL_REVERB_DECAY_HFRATIO:
                *pflValue = ALEffect->Params.Reverb.DecayHFRatio;
                break;

            case AL_REVERB_REFLECTIONS_GAIN:
                *pflValue = ALEffect->Params.Reverb.ReflectionsGain;
                break;

            case AL_REVERB_REFLECTIONS_DELAY:
                *pflValue = ALEffect->Params.Reverb.ReflectionsDelay;
                break;

            case AL_REVERB_LATE_REVERB_GAIN:
                *pflValue = ALEffect->Params.Reverb.LateReverbGain;
                break;

            case AL_REVERB_LATE_REVERB_DELAY:
                *pflValue = ALEffect->Params.Reverb.LateReverbDelay;
                break;

            case AL_REVERB_AIR_ABSORPTION_GAINHF:
                *pflValue = ALEffect->Params.Reverb.AirAbsorptionGainHF;
                break;

            case AL_REVERB_ROOM_ROLLOFF_FACTOR:
                *pflValue = ALEffect->Params.Reverb.RoomRolloffFactor;
                break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
            }
        }
        else if(ALEffect->type == AL_EFFECT_ECHO)
        {
            switch(param)
            {
            case AL_ECHO_DELAY:
                *pflValue = ALEffect->Params.Echo.Delay;
                break;

            case AL_ECHO_LRDELAY:
                *pflValue = ALEffect->Params.Echo.LRDelay;
                break;

            case AL_ECHO_DAMPING:
                *pflValue = ALEffect->Params.Echo.Damping;
                break;

            case AL_ECHO_FEEDBACK:
                *pflValue = ALEffect->Params.Echo.Feedback;
                break;

            case AL_ECHO_SPREAD:
                *pflValue = ALEffect->Params.Echo.Spread;
                break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
            }
        }
        else if(ALEffect->type == AL_EFFECT_RING_MODULATOR)
        {
            switch(param)
            {
            case AL_RING_MODULATOR_FREQUENCY:
                *pflValue = ALEffect->Params.Modulator.Frequency;
                break;
            case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
                *pflValue = ALEffect->Params.Modulator.HighPassCutoff;
                break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
            }
        }
        else if(ALEffect->type == AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT ||
                ALEffect->type == AL_EFFECT_DEDICATED_DIALOGUE)
        {
            switch(param)
            {
            case AL_DEDICATED_GAIN:
                *pflValue = ALEffect->Params.Dedicated.Gain;
                break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
            }
        }
        else
            alSetError(Context, AL_INVALID_ENUM);
    }
    else
        alSetError(Context, AL_INVALID_NAME);

    UnlockContext(Context);
}

AL_API ALvoid AL_APIENTRY alGetEffectfv(ALuint effect, ALenum param, ALfloat *pflValues)
{
    ALCcontext *Context;
    ALCdevice  *Device;
    ALeffect   *ALEffect;

    Context = GetLockedContext();
    if(!Context) return;

    Device = Context->Device;
    if((ALEffect=LookupEffect(Device->EffectMap, effect)) != NULL)
    {
        if(ALEffect->type == AL_EFFECT_EAXREVERB)
        {
            switch(param)
            {
            case AL_EAXREVERB_REFLECTIONS_PAN:
                pflValues[0] = ALEffect->Params.Reverb.ReflectionsPan[0];
                pflValues[1] = ALEffect->Params.Reverb.ReflectionsPan[1];
                pflValues[2] = ALEffect->Params.Reverb.ReflectionsPan[2];
                break;
            case AL_EAXREVERB_LATE_REVERB_PAN:
                pflValues[0] = ALEffect->Params.Reverb.LateReverbPan[0];
                pflValues[1] = ALEffect->Params.Reverb.LateReverbPan[1];
                pflValues[2] = ALEffect->Params.Reverb.LateReverbPan[2];
                break;

            default:
                UnlockContext(Context);
                alGetEffectf(effect, param, pflValues);
                return;
            }
        }
        else
        {
            UnlockContext(Context);
            alGetEffectf(effect, param, pflValues);
            return;
        }
    }
    else
        alSetError(Context, AL_INVALID_NAME);

    UnlockContext(Context);
}


ALvoid ReleaseALEffects(ALCdevice *device)
{
    ALsizei i;
    for(i = 0;i < device->EffectMap.size;i++)
    {
        ALeffect *temp = device->EffectMap.array[i].value;
        device->EffectMap.array[i].value = NULL;

        // Release effect structure
        FreeThunkEntry(temp->effect);
        memset(temp, 0, sizeof(ALeffect));
        free(temp);
    }
}


static void InitEffectParams(ALeffect *effect, ALenum type)
{
    effect->type = type;
    switch(type)
    {
    /* NOTE: Standard reverb and EAX reverb use the same defaults for the
     *       shared parameters, and EAX's additional parameters default to
     *       values assumed by standard reverb.
     */
    case AL_EFFECT_EAXREVERB:
    case AL_EFFECT_REVERB:
        effect->Params.Reverb.Density = AL_EAXREVERB_DEFAULT_DENSITY;
        effect->Params.Reverb.Diffusion = AL_EAXREVERB_DEFAULT_DIFFUSION;
        effect->Params.Reverb.Gain = AL_EAXREVERB_DEFAULT_GAIN;
        effect->Params.Reverb.GainHF = AL_EAXREVERB_DEFAULT_GAINHF;
        effect->Params.Reverb.GainLF = AL_EAXREVERB_DEFAULT_GAINLF;
        effect->Params.Reverb.DecayTime = AL_EAXREVERB_DEFAULT_DECAY_TIME;
        effect->Params.Reverb.DecayHFRatio = AL_EAXREVERB_DEFAULT_DECAY_HFRATIO;
        effect->Params.Reverb.DecayLFRatio = AL_EAXREVERB_DEFAULT_DECAY_LFRATIO;
        effect->Params.Reverb.ReflectionsGain = AL_EAXREVERB_DEFAULT_REFLECTIONS_GAIN;
        effect->Params.Reverb.ReflectionsDelay = AL_EAXREVERB_DEFAULT_REFLECTIONS_DELAY;
        effect->Params.Reverb.ReflectionsPan[0] = AL_EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ;
        effect->Params.Reverb.ReflectionsPan[1] = AL_EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ;
        effect->Params.Reverb.ReflectionsPan[2] = AL_EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ;
        effect->Params.Reverb.LateReverbGain = AL_EAXREVERB_DEFAULT_LATE_REVERB_GAIN;
        effect->Params.Reverb.LateReverbDelay = AL_EAXREVERB_DEFAULT_LATE_REVERB_DELAY;
        effect->Params.Reverb.LateReverbPan[0] = AL_EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ;
        effect->Params.Reverb.LateReverbPan[1] = AL_EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ;
        effect->Params.Reverb.LateReverbPan[2] = AL_EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ;
        effect->Params.Reverb.EchoTime = AL_EAXREVERB_DEFAULT_ECHO_TIME;
        effect->Params.Reverb.EchoDepth = AL_EAXREVERB_DEFAULT_ECHO_DEPTH;
        effect->Params.Reverb.ModulationTime = AL_EAXREVERB_DEFAULT_MODULATION_TIME;
        effect->Params.Reverb.ModulationDepth = AL_EAXREVERB_DEFAULT_MODULATION_DEPTH;
        effect->Params.Reverb.AirAbsorptionGainHF = AL_EAXREVERB_DEFAULT_AIR_ABSORPTION_GAINHF;
        effect->Params.Reverb.HFReference = AL_EAXREVERB_DEFAULT_HFREFERENCE;
        effect->Params.Reverb.LFReference = AL_EAXREVERB_DEFAULT_LFREFERENCE;
        effect->Params.Reverb.RoomRolloffFactor = AL_EAXREVERB_DEFAULT_ROOM_ROLLOFF_FACTOR;
        effect->Params.Reverb.DecayHFLimit = AL_EAXREVERB_DEFAULT_DECAY_HFLIMIT;
        break;
    case AL_EFFECT_ECHO:
        effect->Params.Echo.Delay = AL_ECHO_DEFAULT_DELAY;
        effect->Params.Echo.LRDelay = AL_ECHO_DEFAULT_LRDELAY;
        effect->Params.Echo.Damping = AL_ECHO_DEFAULT_DAMPING;
        effect->Params.Echo.Feedback = AL_ECHO_DEFAULT_FEEDBACK;
        effect->Params.Echo.Spread = AL_ECHO_DEFAULT_SPREAD;
        break;
    case AL_EFFECT_RING_MODULATOR:
        effect->Params.Modulator.Frequency = AL_RING_MODULATOR_DEFAULT_FREQUENCY;
        effect->Params.Modulator.HighPassCutoff = AL_RING_MODULATOR_DEFAULT_HIGHPASS_CUTOFF;
        effect->Params.Modulator.Waveform = AL_RING_MODULATOR_DEFAULT_WAVEFORM;
        break;
    case AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT:
    case AL_EFFECT_DEDICATED_DIALOGUE:
        effect->Params.Dedicated.Gain = 1.0f;
        break;
    }
}
