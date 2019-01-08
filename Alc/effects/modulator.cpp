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
#include "filters/biquad.h"
#include "vecmat.h"


#define MAX_UPDATE_SAMPLES 128

#define WAVEFORM_FRACBITS  24
#define WAVEFORM_FRACONE   (1<<WAVEFORM_FRACBITS)
#define WAVEFORM_FRACMASK  (WAVEFORM_FRACONE-1)

static inline ALfloat Sin(ALsizei index)
{
    return std::sin(static_cast<ALfloat>(index) * (al::MathDefs<float>::Tau() / static_cast<ALfloat>WAVEFORM_FRACONE));
}

static inline ALfloat Saw(ALsizei index)
{
    return static_cast<ALfloat>(index)*(2.0f/WAVEFORM_FRACONE) - 1.0f;
}

static inline ALfloat Square(ALsizei index)
{
    return static_cast<ALfloat>(((index>>(WAVEFORM_FRACBITS-2))&2) - 1);
}

static inline ALfloat One(ALsizei UNUSED(index))
{
    return 1.0f;
}

template<ALfloat func(ALsizei)>
static void Modulate(ALfloat *RESTRICT dst, ALsizei index, const ALsizei step, ALsizei todo)
{
    ALsizei i;
    for(i = 0;i < todo;i++)
    {
        index += step;
        index &= WAVEFORM_FRACMASK;
        dst[i] = func(index);
    }
}


struct ALmodulatorState final : public EffectState {
    void (*mGetSamples)(ALfloat*RESTRICT, ALsizei, const ALsizei, ALsizei){};

    ALsizei mIndex{0};
    ALsizei mStep{1};

    struct {
        BiquadFilter Filter;

        ALfloat CurrentGains[MAX_OUTPUT_CHANNELS]{};
        ALfloat TargetGains[MAX_OUTPUT_CHANNELS]{};
    } mChans[MAX_EFFECT_CHANNELS];


    ALboolean deviceUpdate(const ALCdevice *device) override;
    void update(const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props, const EffectTarget target) override;
    void process(ALsizei samplesToDo, const ALfloat (*RESTRICT samplesIn)[BUFFERSIZE], ALfloat (*RESTRICT samplesOut)[BUFFERSIZE], ALsizei numChannels) override;

    DEF_NEWDEL(ALmodulatorState)
};

ALboolean ALmodulatorState::deviceUpdate(const ALCdevice *UNUSED(device))
{
    for(auto &e : mChans)
    {
        e.Filter.clear();
        std::fill(std::begin(e.CurrentGains), std::end(e.CurrentGains), 0.0f);
    }
    return AL_TRUE;
}

void ALmodulatorState::update(const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props, const EffectTarget target)
{
    const ALCdevice *device = context->Device;
    ALfloat f0norm;
    ALsizei i;

    mStep = fastf2i(props->Modulator.Frequency / static_cast<ALfloat>(device->Frequency) * WAVEFORM_FRACONE);
    mStep = clampi(mStep, 0, WAVEFORM_FRACONE-1);

    if(mStep == 0)
        mGetSamples = Modulate<One>;
    else if(props->Modulator.Waveform == AL_RING_MODULATOR_SINUSOID)
        mGetSamples = Modulate<Sin>;
    else if(props->Modulator.Waveform == AL_RING_MODULATOR_SAWTOOTH)
        mGetSamples = Modulate<Saw>;
    else /*if(Slot->Params.EffectProps.Modulator.Waveform == AL_RING_MODULATOR_SQUARE)*/
        mGetSamples = Modulate<Square>;

    f0norm = props->Modulator.HighPassCutoff / static_cast<ALfloat>(device->Frequency);
    f0norm = clampf(f0norm, 1.0f/512.0f, 0.49f);
    /* Bandwidth value is constant in octaves. */
    mChans[0].Filter.setParams(BiquadType::HighPass, 1.0f, f0norm,
        calc_rcpQ_from_bandwidth(f0norm, 0.75f));
    for(i = 1;i < MAX_EFFECT_CHANNELS;i++)
        mChans[i].Filter.copyParamsFrom(mChans[0].Filter);

    mOutBuffer = target.FOAOut->Buffer;
    mOutChannels = target.FOAOut->NumChannels;
    for(i = 0;i < MAX_EFFECT_CHANNELS;i++)
        ComputePanGains(target.FOAOut, alu::Matrix::Identity()[i].data(), slot->Params.Gain,
            mChans[i].TargetGains);
}

void ALmodulatorState::process(ALsizei SamplesToDo, const ALfloat (*RESTRICT SamplesIn)[BUFFERSIZE], ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE], ALsizei NumChannels)
{
    const ALsizei step = mStep;
    ALsizei base;

    for(base = 0;base < SamplesToDo;)
    {
        alignas(16) ALfloat modsamples[MAX_UPDATE_SAMPLES];
        ALsizei td = mini(MAX_UPDATE_SAMPLES, SamplesToDo-base);
        ALsizei c, i;

        mGetSamples(modsamples, mIndex, step, td);
        mIndex += (step*td) & WAVEFORM_FRACMASK;
        mIndex &= WAVEFORM_FRACMASK;

        for(c = 0;c < MAX_EFFECT_CHANNELS;c++)
        {
            alignas(16) ALfloat temps[MAX_UPDATE_SAMPLES];

            mChans[c].Filter.process(temps, &SamplesIn[c][base], td);
            for(i = 0;i < td;i++)
                temps[i] *= modsamples[i];

            MixSamples(temps, NumChannels, SamplesOut, mChans[c].CurrentGains,
                       mChans[c].TargetGains, SamplesToDo-base, base, td);
        }

        base += td;
    }
}


struct ModulatorStateFactory final : public EffectStateFactory {
    EffectState *create() override;
};

EffectState *ModulatorStateFactory::create()
{ return new ALmodulatorState{}; }

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
            ALmodulator_setParamf(effect, context, param, static_cast<ALfloat>(val));
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
            *val = static_cast<ALint>(props->Modulator.Frequency);
            break;
        case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
            *val = static_cast<ALint>(props->Modulator.HighPassCutoff);
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
