/**
 * OpenAL cross platform audio library
 * Copyright (C) 2013 by Anis A. Hireche
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

#include <stdlib.h>

#include "config.h"
#include "alError.h"
#include "alMain.h"
#include "alAuxEffectSlot.h"
#include "alu.h"

typedef struct ALcompressorStateFactory {
    DERIVE_FROM_TYPE(ALeffectStateFactory);
} ALcompressorStateFactory;

static ALcompressorStateFactory CompressorFactory;


typedef struct ALcompressorState {
    DERIVE_FROM_TYPE(ALeffectState);

    /* Effect gains for each channel */
    ALfloat Gain[MaxChannels];

    /* Effect parameters */
    ALboolean Enabled;
    ALfloat Envelope;
    ALfloat EnvGain;
    ALfloat Attack;
    ALfloat Release;
} ALcompressorState;

static ALvoid ALcompressorState_Destruct(ALcompressorState *state)
{
    (void)state;
}

static ALboolean ALcompressorState_deviceUpdate(ALcompressorState *state, ALCdevice *device)
{
    state->Attack = expf(-1.0f / (device->Frequency * 0.0001f));
    state->Release = expf(-1.0f / (device->Frequency * 0.3f));
    state->Envelope = 0.0f;
    state->EnvGain = 1.0f;

    return AL_TRUE;
}

static ALvoid ALcompressorState_update(ALcompressorState *state, ALCdevice *Device, const ALeffectslot *Slot)
{
    ALfloat gain;

    state->Enabled = Slot->EffectProps.Compressor.OnOff;
    /* FIXME: Could maybe use the compressor's attack/release to move the gain to 1? */
    if(!state->Enabled)
        state->EnvGain = 1.0f;

    gain = sqrtf(1.0f / Device->NumChan) * Slot->Gain;
    SetGains(Device, gain, state->Gain);
}

static ALvoid ALcompressorState_process(ALcompressorState *state, ALuint SamplesToDo, const ALfloat *SamplesIn, ALfloat (*SamplesOut)[BUFFERSIZE])
{
    ALfloat env = state->Envelope;
    ALfloat envgain = state->EnvGain;
    ALfloat tattack = state->Attack;
    ALfloat trelease = state->Release;
    ALuint it, kt;
    ALuint base;
    ALfloat theta;
    ALfloat rms;

    for(base = 0;base < SamplesToDo;)
    {
        ALfloat temps[64];
        ALuint td = minu(SamplesToDo-base, 64);
        ALfloat summ = 0.0f;

        for(it = 0;it < td;it++)
        {
            ALfloat smp = SamplesIn[it+base] * 0.5f;
            summ += smp * smp;
            temps[it] = smp;
        }

        if(state->Enabled)
        {
            const ALfloat threshold = 0.5f;
            const ALfloat slope = 0.5f;

            /* computing rms and envelope */
            rms = sqrtf(summ / td);
            theta = ((rms > env) ? tattack : trelease);
            env = (1.0f - theta)*rms + theta*env;

            /* applying a hard-knee rms based compressor */
            if(env > threshold)
                envgain = envgain - (env - threshold) * slope;
        }

        for(kt = 0;kt < MaxChannels;kt++)
        {
            ALfloat gain = state->Gain[kt] * envgain * 2.0f;
            if(!(gain > GAIN_SILENCE_THRESHOLD))
                continue;

            for(it = 0;it < td;it++)
                SamplesOut[kt][base+it] += gain * temps[it];
        }

        base += td;
    }

    state->Envelope = env;
    state->EnvGain = envgain;
}

static void ALcompressorState_Delete(ALcompressorState *state)
{
    free(state);
}

DEFINE_ALEFFECTSTATE_VTABLE(ALcompressorState);


static ALeffectState *ALcompressorStateFactory_create(ALcompressorStateFactory *factory)
{
    ALcompressorState *state;
    (void)factory;

    state = malloc(sizeof(*state));
    if(!state) return NULL;
    SET_VTABLE2(ALcompressorState, ALeffectState, state);

    return STATIC_CAST(ALeffectState, state);
}

DEFINE_ALEFFECTSTATEFACTORY_VTABLE(ALcompressorStateFactory);

ALeffectStateFactory *ALcompressorStateFactory_getFactory(void)
{
    SET_VTABLE2(ALcompressorStateFactory, ALeffectStateFactory, &CompressorFactory);
    return STATIC_CAST(ALeffectStateFactory, &CompressorFactory);
}


void ALcompressor_setParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_COMPRESSOR_ONOFF:
            if(!(val >= AL_COMPRESSOR_MIN_ONOFF && val <= AL_COMPRESSOR_MAX_ONOFF))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Compressor.OnOff = val;
            break;

    default:
        SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}

void ALcompressor_setParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{
    ALcompressor_setParami(effect, context, param, vals[0]);
}

void ALcompressor_setParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM); (void)effect;(void)param;(void)val;}

void ALcompressor_setParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{
    ALcompressor_setParamf(effect, context, param, vals[0]);
}

void ALcompressor_getParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint *val)
{ 
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_COMPRESSOR_ONOFF:
            *val = props->Compressor.OnOff;
            break;
        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALcompressor_getParamiv(ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{
    ALcompressor_getParami(effect, context, param, vals);
}
void ALcompressor_getParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val)
{SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM); (void)effect;(void)param;(void)val;}

void ALcompressor_getParamfv(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{
    ALcompressor_getParamf(effect, context, param, vals);
}

DEFINE_ALEFFECT_VTABLE(ALcompressor);
