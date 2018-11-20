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

#include <cmath>
#include <algorithm>

#include "alMain.h"
#include "alcontext.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
#include "alu.h"
#include "filters/defs.h"
#include "vecmat.h"


#define MAX_UPDATE_SAMPLES 128

struct ALmodulatorState final : public ALeffectState {
    void (*mGetSamples)(ALfloat*RESTRICT, ALsizei, const ALsizei, ALsizei){};

    ALsizei mIndex{0};
    ALsizei mStep{1};

    struct {
        BiquadFilter Filter;

        ALfloat CurrentGains[MAX_OUTPUT_CHANNELS]{};
        ALfloat TargetGains[MAX_OUTPUT_CHANNELS]{};
    } mChans[MAX_EFFECT_CHANNELS];
};

static ALvoid ALmodulatorState_Destruct(ALmodulatorState *state);
static ALboolean ALmodulatorState_deviceUpdate(ALmodulatorState *state, ALCdevice *device);
static ALvoid ALmodulatorState_update(ALmodulatorState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props);
static ALvoid ALmodulatorState_process(ALmodulatorState *state, ALsizei SamplesToDo, const ALfloat (*RESTRICT SamplesIn)[BUFFERSIZE], ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE], ALsizei NumChannels);
DECLARE_DEFAULT_ALLOCATORS(ALmodulatorState)

DEFINE_ALEFFECTSTATE_VTABLE(ALmodulatorState);


#define WAVEFORM_FRACBITS  24
#define WAVEFORM_FRACONE   (1<<WAVEFORM_FRACBITS)
#define WAVEFORM_FRACMASK  (WAVEFORM_FRACONE-1)

static inline ALfloat Sin(ALsizei index)
{
    return std::sin((ALfloat)index * (F_TAU / (ALfloat)WAVEFORM_FRACONE));
}

static inline ALfloat Saw(ALsizei index)
{
    return (ALfloat)index*(2.0f/WAVEFORM_FRACONE) - 1.0f;
}

static inline ALfloat Square(ALsizei index)
{
    return (ALfloat)(((index>>(WAVEFORM_FRACBITS-2))&2) - 1);
}

static inline ALfloat One(ALsizei UNUSED(index))
{
    return 1.0f;
}

#define DECL_TEMPLATE(func)                                                   \
static void Modulate##func(ALfloat *RESTRICT dst, ALsizei index,              \
                           const ALsizei step, ALsizei todo)                  \
{                                                                             \
    ALsizei i;                                                                \
    for(i = 0;i < todo;i++)                                                   \
    {                                                                         \
        index += step;                                                        \
        index &= WAVEFORM_FRACMASK;                                           \
        dst[i] = func(index);                                                 \
    }                                                                         \
}

DECL_TEMPLATE(Sin)
DECL_TEMPLATE(Saw)
DECL_TEMPLATE(Square)
DECL_TEMPLATE(One)

#undef DECL_TEMPLATE


static void ALmodulatorState_Construct(ALmodulatorState *state)
{
    new (state) ALmodulatorState{};
    ALeffectState_Construct(STATIC_CAST(ALeffectState, state));
    SET_VTABLE2(ALmodulatorState, ALeffectState, state);
}

static ALvoid ALmodulatorState_Destruct(ALmodulatorState *state)
{
    ALeffectState_Destruct(STATIC_CAST(ALeffectState,state));
    state->~ALmodulatorState();
}

static ALboolean ALmodulatorState_deviceUpdate(ALmodulatorState *state, ALCdevice *UNUSED(device))
{
    for(auto &e : state->mChans)
    {
        BiquadFilter_clear(&e.Filter);
        std::fill(std::begin(e.CurrentGains), std::end(e.CurrentGains), 0.0f);
    }
    return AL_TRUE;
}

static ALvoid ALmodulatorState_update(ALmodulatorState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props)
{
    const ALCdevice *device = context->Device;
    ALfloat f0norm;
    ALsizei i;

    state->mStep = fastf2i(props->Modulator.Frequency / (ALfloat)device->Frequency *
                           WAVEFORM_FRACONE);
    state->mStep = clampi(state->mStep, 0, WAVEFORM_FRACONE-1);

    if(state->mStep == 0)
        state->mGetSamples = ModulateOne;
    else if(props->Modulator.Waveform == AL_RING_MODULATOR_SINUSOID)
        state->mGetSamples = ModulateSin;
    else if(props->Modulator.Waveform == AL_RING_MODULATOR_SAWTOOTH)
        state->mGetSamples = ModulateSaw;
    else /*if(Slot->Params.EffectProps.Modulator.Waveform == AL_RING_MODULATOR_SQUARE)*/
        state->mGetSamples = ModulateSquare;

    f0norm = props->Modulator.HighPassCutoff / (ALfloat)device->Frequency;
    f0norm = clampf(f0norm, 1.0f/512.0f, 0.49f);
    /* Bandwidth value is constant in octaves. */
    BiquadFilter_setParams(&state->mChans[0].Filter, BiquadType::HighPass, 1.0f,
                           f0norm, calc_rcpQ_from_bandwidth(f0norm, 0.75f));
    for(i = 1;i < MAX_EFFECT_CHANNELS;i++)
        BiquadFilter_copyParams(&state->mChans[i].Filter, &state->mChans[0].Filter);

    state->OutBuffer = device->FOAOut.Buffer;
    state->OutChannels = device->FOAOut.NumChannels;
    for(i = 0;i < MAX_EFFECT_CHANNELS;i++)
        ComputePanGains(&device->FOAOut, aluMatrixf::Identity.m[i], slot->Params.Gain,
                        state->mChans[i].TargetGains);
}

static ALvoid ALmodulatorState_process(ALmodulatorState *state, ALsizei SamplesToDo, const ALfloat (*RESTRICT SamplesIn)[BUFFERSIZE], ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE], ALsizei NumChannels)
{
    const ALsizei step = state->mStep;
    ALsizei base;

    for(base = 0;base < SamplesToDo;)
    {
        alignas(16) ALfloat modsamples[MAX_UPDATE_SAMPLES];
        ALsizei td = mini(MAX_UPDATE_SAMPLES, SamplesToDo-base);
        ALsizei c, i;

        state->mGetSamples(modsamples, state->mIndex, step, td);
        state->mIndex += (step*td) & WAVEFORM_FRACMASK;
        state->mIndex &= WAVEFORM_FRACMASK;

        for(c = 0;c < MAX_EFFECT_CHANNELS;c++)
        {
            alignas(16) ALfloat temps[MAX_UPDATE_SAMPLES];

            BiquadFilter_process(&state->mChans[c].Filter, temps, &SamplesIn[c][base], td);
            for(i = 0;i < td;i++)
                temps[i] *= modsamples[i];

            MixSamples(temps, NumChannels, SamplesOut, state->mChans[c].CurrentGains,
                       state->mChans[c].TargetGains, SamplesToDo-base, base, td);
        }

        base += td;
    }
}


struct ModulatorStateFactory final : public EffectStateFactory {
    ALeffectState *create() override;
};

ALeffectState *ModulatorStateFactory::create()
{
    ALmodulatorState *state;
    NEW_OBJ0(state, ALmodulatorState)();
    return state;
}

EffectStateFactory *ModulatorStateFactory_getFactory(void)
{
    static ModulatorStateFactory ModulatorFactory{};
    return &ModulatorFactory;
}


void ALmodulator_setParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_RING_MODULATOR_FREQUENCY:
            if(!(val >= AL_RING_MODULATOR_MIN_FREQUENCY && val <= AL_RING_MODULATOR_MAX_FREQUENCY))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Modulator frequency out of range");
            props->Modulator.Frequency = val;
            break;

        case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
            if(!(val >= AL_RING_MODULATOR_MIN_HIGHPASS_CUTOFF && val <= AL_RING_MODULATOR_MAX_HIGHPASS_CUTOFF))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Modulator high-pass cutoff out of range");
            props->Modulator.HighPassCutoff = val;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid modulator float property 0x%04x", param);
    }
}
void ALmodulator_setParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{ ALmodulator_setParamf(effect, context, param, vals[0]); }
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
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Invalid modulator waveform");
            props->Modulator.Waveform = val;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid modulator integer property 0x%04x", param);
    }
}
void ALmodulator_setParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{ ALmodulator_setParami(effect, context, param, vals[0]); }

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
            alSetError(context, AL_INVALID_ENUM, "Invalid modulator integer property 0x%04x", param);
    }
}
void ALmodulator_getParamiv(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{ ALmodulator_getParami(effect, context, param, vals); }
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
            alSetError(context, AL_INVALID_ENUM, "Invalid modulator float property 0x%04x", param);
    }
}
void ALmodulator_getParamfv(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{ ALmodulator_getParamf(effect, context, param, vals); }

DEFINE_ALEFFECT_VTABLE(ALmodulator);
