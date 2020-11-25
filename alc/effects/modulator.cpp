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

#include <cmath>
#include <cstdlib>

#include <cmath>
#include <algorithm>

#include "al/auxeffectslot.h"
#include "alcmain.h"
#include "alcontext.h"
#include "alu.h"
#include "filters/biquad.h"
#include "vecmat.h"


namespace {

#define MAX_UPDATE_SAMPLES 128

#define WAVEFORM_FRACBITS  24
#define WAVEFORM_FRACONE   (1<<WAVEFORM_FRACBITS)
#define WAVEFORM_FRACMASK  (WAVEFORM_FRACONE-1)

inline float Sin(ALuint index)
{
    constexpr float scale{al::MathDefs<float>::Tau() / WAVEFORM_FRACONE};
    return std::sin(static_cast<float>(index) * scale);
}

inline float Saw(ALuint index)
{ return static_cast<float>(index)*(2.0f/WAVEFORM_FRACONE) - 1.0f; }

inline float Square(ALuint index)
{ return static_cast<float>(static_cast<int>((index>>(WAVEFORM_FRACBITS-2))&2) - 1); }

inline float One(ALuint) { return 1.0f; }

template<float (&func)(ALuint)>
void Modulate(float *RESTRICT dst, ALuint index, const ALuint step, size_t todo)
{
    for(size_t i{0u};i < todo;i++)
    {
        index += step;
        index &= WAVEFORM_FRACMASK;
        dst[i] = func(index);
    }
}


struct ModulatorState final : public EffectState {
    void (*mGetSamples)(float*RESTRICT, ALuint, const ALuint, size_t){};

    ALuint mIndex{0};
    ALuint mStep{1};

    struct {
        BiquadFilter Filter;

        float CurrentGains[MAX_OUTPUT_CHANNELS]{};
        float TargetGains[MAX_OUTPUT_CHANNELS]{};
    } mChans[MAX_AMBI_CHANNELS];


    void deviceUpdate(const ALCdevice *device) override;
    void update(const ALCcontext *context, const EffectSlot *slot, const EffectProps *props,
        const EffectTarget target) override;
    void process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn,
        const al::span<FloatBufferLine> samplesOut) override;

    DEF_NEWDEL(ModulatorState)
};

void ModulatorState::deviceUpdate(const ALCdevice*)
{
    for(auto &e : mChans)
    {
        e.Filter.clear();
        std::fill(std::begin(e.CurrentGains), std::end(e.CurrentGains), 0.0f);
    }
}

void ModulatorState::update(const ALCcontext *context, const EffectSlot *slot,
    const EffectProps *props, const EffectTarget target)
{
    const ALCdevice *device{context->mDevice.get()};

    const float step{props->Modulator.Frequency / static_cast<float>(device->Frequency)};
    mStep = fastf2u(clampf(step*WAVEFORM_FRACONE, 0.0f, float{WAVEFORM_FRACONE-1}));

    if(mStep == 0)
        mGetSamples = Modulate<One>;
    else if(props->Modulator.Waveform == AL_RING_MODULATOR_SINUSOID)
        mGetSamples = Modulate<Sin>;
    else if(props->Modulator.Waveform == AL_RING_MODULATOR_SAWTOOTH)
        mGetSamples = Modulate<Saw>;
    else /*if(props->Modulator.Waveform == AL_RING_MODULATOR_SQUARE)*/
        mGetSamples = Modulate<Square>;

    float f0norm{props->Modulator.HighPassCutoff / static_cast<float>(device->Frequency)};
    f0norm = clampf(f0norm, 1.0f/512.0f, 0.49f);
    /* Bandwidth value is constant in octaves. */
    mChans[0].Filter.setParamsFromBandwidth(BiquadType::HighPass, f0norm, 1.0f, 0.75f);
    for(size_t i{1u};i < slot->Wet.Buffer.size();++i)
        mChans[i].Filter.copyParamsFrom(mChans[0].Filter);

    mOutTarget = target.Main->Buffer;
    auto set_gains = [slot,target](auto &chan, al::span<const float,MAX_AMBI_CHANNELS> coeffs)
    { ComputePanGains(target.Main, coeffs.data(), slot->Gain, chan.TargetGains); };
    SetAmbiPanIdentity(std::begin(mChans), slot->Wet.Buffer.size(), set_gains);
}

void ModulatorState::process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut)
{
    for(size_t base{0u};base < samplesToDo;)
    {
        alignas(16) float modsamples[MAX_UPDATE_SAMPLES];
        size_t td{minz(MAX_UPDATE_SAMPLES, samplesToDo-base)};

        mGetSamples(modsamples, mIndex, mStep, td);
        mIndex += static_cast<ALuint>(mStep * td);
        mIndex &= WAVEFORM_FRACMASK;

        auto chandata = std::addressof(mChans[0]);
        for(const auto &input : samplesIn)
        {
            alignas(16) float temps[MAX_UPDATE_SAMPLES];

            chandata->Filter.process({&input[base], td}, temps);
            for(size_t i{0u};i < td;i++)
                temps[i] *= modsamples[i];

            MixSamples({temps, td}, samplesOut, chandata->CurrentGains, chandata->TargetGains,
                samplesToDo-base, base);
            ++chandata;
        }

        base += td;
    }
}


struct ModulatorStateFactory final : public EffectStateFactory {
    EffectState *create() override { return new ModulatorState{}; }
};

} // namespace

EffectStateFactory *ModulatorStateFactory_getFactory()
{
    static ModulatorStateFactory ModulatorFactory{};
    return &ModulatorFactory;
}
