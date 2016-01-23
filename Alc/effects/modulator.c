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
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

    ALfloat Gain[MAX_OUTPUT_CHANNELS];

    ALfilterState Filter;
} ALmodulatorState;

#define WAVEFORM_FRACBITS  24
#define WAVEFORM_FRACONE   (1<<WAVEFORM_FRACBITS)
#define WAVEFORM_FRACMASK  (WAVEFORM_FRACONE-1)

static inline ALfloat Sin(ALuint index)
{
    return sinf(index*(F_TAU/WAVEFORM_FRACONE) - F_PI)*0.5f + 0.5f;
}

static inline ALfloat Saw(ALuint index)
{
    return (ALfloat)index / WAVEFORM_FRACONE;
}

static inline ALfloat Square(ALuint index)
{
    return (ALfloat)((index >> (WAVEFORM_FRACBITS - 1)) & 1);
}

#define DECL_TEMPLATE(func)                                                   \
static void Process##func(ALmodulatorState *state, ALuint SamplesToDo,        \
  const ALfloat *restrict SamplesIn,                                          \
  ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALuint NumChannels)             \
{                                                                             \
    const ALuint step = state->step;                                          \
    ALuint index = state->index;                                              \
    ALuint base;                                                              \
                                                                              \
    for(base = 0;base < SamplesToDo;)                                         \
    {                                                                         \
        ALfloat temps[256];                                                   \
        ALuint td = minu(256, SamplesToDo-base);                              \
        ALuint i, k;                                                          \
                                                                              \
        for(i = 0;i < td;i++)                                                 \
        {                                                                     \
            ALfloat samp;                                                     \
            samp = SamplesIn[base+i];                                         \
            samp = ALfilterState_processSingle(&state->Filter, samp);         \
                                                                              \
            index += step;                                                    \
            index &= WAVEFORM_FRACMASK;                                       \
            temps[i] = samp * func(index);                                    \
        }                                                                     \
                                                                              \
        for(k = 0;k < NumChannels;k++)                                        \
        {                                                                     \
            ALfloat gain = state->Gain[k];                                    \
            if(!(fabsf(gain) > GAIN_SILENCE_THRESHOLD))                       \
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


static ALvoid ALmodulatorState_Destruct(ALmodulatorState *UNUSED(state))
{
}

static ALboolean ALmodulatorState_deviceUpdate(ALmodulatorState *UNUSED(state), ALCdevice *UNUSED(device))
{
    return AL_TRUE;
}

static ALvoid ALmodulatorState_update(ALmodulatorState *state, ALCdevice *Device, const ALeffectslot *Slot)
{
    ALfloat cw, a;

    if(Slot->EffectProps.Modulator.Waveform == AL_RING_MODULATOR_SINUSOID)
        state->Waveform = SINUSOID;
    else if(Slot->EffectProps.Modulator.Waveform == AL_RING_MODULATOR_SAWTOOTH)
        state->Waveform = SAWTOOTH;
    else if(Slot->EffectProps.Modulator.Waveform == AL_RING_MODULATOR_SQUARE)
        state->Waveform = SQUARE;

    state->step = fastf2u(Slot->EffectProps.Modulator.Frequency*WAVEFORM_FRACONE /
                          Device->Frequency);
    if(state->step == 0) state->step = 1;

    /* Custom filter coeffs, which match the old version instead of a low-shelf. */
    cw = cosf(F_TAU * Slot->EffectProps.Modulator.HighPassCutoff / Device->Frequency);
    a = (2.0f-cw) - sqrtf(powf(2.0f-cw, 2.0f) - 1.0f);

    state->Filter.a1 = -a;
    state->Filter.a2 = 0.0f;
    state->Filter.b1 = -a;
    state->Filter.b2 = 0.0f;
    state->Filter.input_gain = a;

    ComputeAmbientGains(Device, Slot->Gain, state->Gain);
}

static ALvoid ALmodulatorState_process(ALmodulatorState *state, ALuint SamplesToDo, const ALfloat *restrict SamplesIn, ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALuint NumChannels)
{
    switch(state->Waveform)
    {
        case SINUSOID:
            ProcessSin(state, SamplesToDo, SamplesIn, SamplesOut, NumChannels);
            break;

        case SAWTOOTH:
            ProcessSaw(state, SamplesToDo, SamplesIn, SamplesOut, NumChannels);
            break;

        case SQUARE:
            ProcessSquare(state, SamplesToDo, SamplesIn, SamplesOut, NumChannels);
            break;
    }
}

DECLARE_DEFAULT_ALLOCATORS(ALmodulatorState)

DEFINE_ALEFFECTSTATE_VTABLE(ALmodulatorState);


typedef struct ALmodulatorStateFactory {
    DERIVE_FROM_TYPE(ALeffectStateFactory);
} ALmodulatorStateFactory;

static ALeffectState *ALmodulatorStateFactory_create(ALmodulatorStateFactory *UNUSED(factory))
{
    ALmodulatorState *state;

    state = ALmodulatorState_New(sizeof(*state));
    if(!state) return NULL;
    SET_VTABLE2(ALmodulatorState, ALeffectState, state);

    state->index = 0;
    state->step = 1;

    ALfilterState_clear(&state->Filter);

    return STATIC_CAST(ALeffectState, state);
}

DEFINE_ALEFFECTSTATEFACTORY_VTABLE(ALmodulatorStateFactory);

ALeffectStateFactory *ALmodulatorStateFactory_getFactory(void)
{
    static ALmodulatorStateFactory ModulatorFactory = { { GET_VTABLE2(ALmodulatorStateFactory, ALeffectStateFactory) } };

    return STATIC_CAST(ALeffectStateFactory, &ModulatorFactory);
}


void ALmodulator_setParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_RING_MODULATOR_FREQUENCY:
            if(!(val >= AL_RING_MODULATOR_MIN_FREQUENCY && val <= AL_RING_MODULATOR_MAX_FREQUENCY))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Modulator.Frequency = val;
            break;

        case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
            if(!(val >= AL_RING_MODULATOR_MIN_HIGHPASS_CUTOFF && val <= AL_RING_MODULATOR_MAX_HIGHPASS_CUTOFF))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Modulator.HighPassCutoff = val;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALmodulator_setParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{
    ALmodulator_setParamf(effect, context, param, vals[0]);
}
void ALmodulator_setParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_RING_MODULATOR_FREQUENCY:
        case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
            ALmodulator_setParamf(effect, context, param, (ALfloat)val);
            break;

        case AL_RING_MODULATOR_WAVEFORM:
            if(!(val >= AL_RING_MODULATOR_MIN_WAVEFORM && val <= AL_RING_MODULATOR_MAX_WAVEFORM))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Modulator.Waveform = val;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALmodulator_setParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{
    ALmodulator_setParami(effect, context, param, vals[0]);
}

void ALmodulator_getParami(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *val)
{
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_RING_MODULATOR_FREQUENCY:
            *val = (ALint)props->Modulator.Frequency;
            break;
        case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
            *val = (ALint)props->Modulator.HighPassCutoff;
            break;
        case AL_RING_MODULATOR_WAVEFORM:
            *val = props->Modulator.Waveform;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALmodulator_getParamiv(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{
    ALmodulator_getParami(effect, context, param, vals);
}
void ALmodulator_getParamf(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val)
{
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_RING_MODULATOR_FREQUENCY:
            *val = props->Modulator.Frequency;
            break;
        case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
            *val = props->Modulator.HighPassCutoff;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALmodulator_getParamfv(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{
    ALmodulator_getParamf(effect, context, param, vals);
}

DEFINE_ALEFFECT_VTABLE(ALmodulator);
