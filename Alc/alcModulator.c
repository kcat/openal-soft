/**
 * OpenAL cross platform audio library
 * Copyright (C) 2009 by Chris Robinson.
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

#include <math.h>
#include <stdlib.h>

#include "alMain.h"
#include "alFilter.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
#include "alu.h"


typedef struct ALmodulatorState {
    DERIVE_FROM_TYPE(ALeffectState);

    enum {
        SINUSOID,
        SAWTOOTH,
        SQUARE
    } Waveform;

    ALuint index;
    ALuint step;

    ALfloat Gain[MaxChannels];

    FILTER iirFilter;
    ALfloat history[1];
} ALmodulatorState;

#define WAVEFORM_FRACBITS  24
#define WAVEFORM_FRACONE   (1<<WAVEFORM_FRACBITS)
#define WAVEFORM_FRACMASK  (WAVEFORM_FRACONE-1)

static __inline ALfloat Sin(ALuint index)
{
    return sinf(index * (F_PI*2.0f / WAVEFORM_FRACONE) - F_PI)*0.5f + 0.5f;
}

static __inline ALfloat Saw(ALuint index)
{
    return (ALfloat)index / WAVEFORM_FRACONE;
}

static __inline ALfloat Square(ALuint index)
{
    return (ALfloat)((index >> (WAVEFORM_FRACBITS - 1)) & 1);
}


static __inline ALfloat hpFilter1P(FILTER *iir, ALuint offset, ALfloat input)
{
    ALfloat *history = &iir->history[offset];
    ALfloat a = iir->coeff;
    ALfloat output = input;

    output = output + (history[0]-output)*a;
    history[0] = output;

    return input - output;
}


#define DECL_TEMPLATE(func)                                                   \
static void Process##func(ALmodulatorState *state, ALuint SamplesToDo,        \
  const ALfloat *RESTRICT SamplesIn,                                          \
  ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE])                                 \
{                                                                             \
    const ALuint step = state->step;                                          \
    ALuint index = state->index;                                              \
    ALuint base;                                                              \
                                                                              \
    for(base = 0;base < SamplesToDo;)                                         \
    {                                                                         \
        ALfloat temps[64];                                                    \
        ALuint td = minu(SamplesToDo-base, 64);                               \
        ALuint i, k;                                                          \
                                                                              \
        for(i = 0;i < td;i++)                                                 \
        {                                                                     \
            ALfloat samp;                                                     \
            samp = SamplesIn[base+i];                                         \
            samp = hpFilter1P(&state->iirFilter, 0, samp);                    \
                                                                              \
            index += step;                                                    \
            index &= WAVEFORM_FRACMASK;                                       \
            temps[i] = samp * func(index);                                    \
        }                                                                     \
                                                                              \
        for(k = 0;k < MaxChannels;k++)                                        \
        {                                                                     \
            ALfloat gain = state->Gain[k];                                    \
            if(!(gain > 0.00001f))                                            \
                continue;                                                     \
                                                                              \
            for(i = 0;i < td;i++)                                             \
                SamplesOut[k][base+i] += gain * temps[i];                     \
        }                                                                     \
                                                                              \
        base += td;                                                           \
    }                                                                         \
    state->index = index;                                                     \
}

DECL_TEMPLATE(Sin)
DECL_TEMPLATE(Saw)
DECL_TEMPLATE(Square)

#undef DECL_TEMPLATE


static ALvoid ModulatorDestroy(ALeffectState *effect)
{
    ALmodulatorState *state = GET_PARENT_TYPE(ALmodulatorState, ALeffectState, effect);
    free(state);
}

static ALboolean ModulatorDeviceUpdate(ALeffectState *effect, ALCdevice *Device)
{
    return AL_TRUE;
    (void)effect;
    (void)Device;
}

static ALvoid ModulatorUpdate(ALeffectState *effect, ALCdevice *Device, const ALeffectslot *Slot)
{
    ALmodulatorState *state = GET_PARENT_TYPE(ALmodulatorState, ALeffectState, effect);
    ALfloat gain, cw, a = 0.0f;
    ALuint index;

    if(Slot->effect.Modulator.Waveform == AL_RING_MODULATOR_SINUSOID)
        state->Waveform = SINUSOID;
    else if(Slot->effect.Modulator.Waveform == AL_RING_MODULATOR_SAWTOOTH)
        state->Waveform = SAWTOOTH;
    else if(Slot->effect.Modulator.Waveform == AL_RING_MODULATOR_SQUARE)
        state->Waveform = SQUARE;

    state->step = fastf2u(Slot->effect.Modulator.Frequency*WAVEFORM_FRACONE /
                          Device->Frequency);
    if(state->step == 0) state->step = 1;

    cw = cosf(F_PI*2.0f * Slot->effect.Modulator.HighPassCutoff /
                          Device->Frequency);
    a = (2.0f-cw) - sqrtf(powf(2.0f-cw, 2.0f) - 1.0f);
    state->iirFilter.coeff = a;

    gain = sqrtf(1.0f/Device->NumChan);
    gain *= Slot->Gain;
    for(index = 0;index < MaxChannels;index++)
        state->Gain[index] = 0.0f;
    for(index = 0;index < Device->NumChan;index++)
    {
        enum Channel chan = Device->Speaker2Chan[index];
        state->Gain[chan] = gain;
    }
}

static ALvoid ModulatorProcess(ALeffectState *effect, ALuint SamplesToDo, const ALfloat *RESTRICT SamplesIn, ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE])
{
    ALmodulatorState *state = GET_PARENT_TYPE(ALmodulatorState, ALeffectState, effect);

    switch(state->Waveform)
    {
        case SINUSOID:
            ProcessSin(state, SamplesToDo, SamplesIn, SamplesOut);
            break;

        case SAWTOOTH:
            ProcessSaw(state, SamplesToDo, SamplesIn, SamplesOut);
            break;

        case SQUARE:
            ProcessSquare(state, SamplesToDo, SamplesIn, SamplesOut);
            break;
    }
}

ALeffectState *ModulatorCreate(void)
{
    ALmodulatorState *state;

    state = malloc(sizeof(*state));
    if(!state)
        return NULL;

    GET_DERIVED_TYPE(ALeffectState, state)->Destroy = ModulatorDestroy;
    GET_DERIVED_TYPE(ALeffectState, state)->DeviceUpdate = ModulatorDeviceUpdate;
    GET_DERIVED_TYPE(ALeffectState, state)->Update = ModulatorUpdate;
    GET_DERIVED_TYPE(ALeffectState, state)->Process = ModulatorProcess;

    state->index = 0;
    state->step = 1;

    state->iirFilter.coeff = 0.0f;
    state->iirFilter.history[0] = 0.0f;

    return GET_DERIVED_TYPE(ALeffectState, state);
}

void mod_SetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    switch(param)
    {
        case AL_RING_MODULATOR_FREQUENCY:
            if(val >= AL_RING_MODULATOR_MIN_FREQUENCY && val <= AL_RING_MODULATOR_MAX_FREQUENCY)
                effect->Modulator.Frequency = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
            if(val >= AL_RING_MODULATOR_MIN_HIGHPASS_CUTOFF && val <= AL_RING_MODULATOR_MAX_HIGHPASS_CUTOFF)
                effect->Modulator.HighPassCutoff = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        default:
            alSetError(context, AL_INVALID_ENUM);
            break;
    }
}
void mod_SetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{
    mod_SetParamf(effect, context, param, vals[0]);
}
void mod_SetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val)
{
    switch(param)
    {
        case AL_RING_MODULATOR_FREQUENCY:
        case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
            mod_SetParamf(effect, context, param, (ALfloat)val);
            break;

        case AL_RING_MODULATOR_WAVEFORM:
            if(val >= AL_RING_MODULATOR_MIN_WAVEFORM && val <= AL_RING_MODULATOR_MAX_WAVEFORM)
                effect->Modulator.Waveform = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        default:
            alSetError(context, AL_INVALID_ENUM);
            break;
    }
}
void mod_SetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{
    mod_SetParami(effect, context, param, vals[0]);
}

void mod_GetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint *val)
{
    switch(param)
    {
        case AL_RING_MODULATOR_FREQUENCY:
            *val = (ALint)effect->Modulator.Frequency;
            break;
        case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
            *val = (ALint)effect->Modulator.HighPassCutoff;
            break;
        case AL_RING_MODULATOR_WAVEFORM:
            *val = effect->Modulator.Waveform;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM);
            break;
    }
}
void mod_GetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{
    mod_GetParami(effect, context, param, vals);
}
void mod_GetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val)
{
    switch(param)
    {
        case AL_RING_MODULATOR_FREQUENCY:
            *val = effect->Modulator.Frequency;
            break;
        case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
            *val = effect->Modulator.HighPassCutoff;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM);
            break;
    }
}
void mod_GetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{
    mod_GetParamf(effect, context, param, vals);
}
