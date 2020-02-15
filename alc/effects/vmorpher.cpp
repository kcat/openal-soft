/**
 * OpenAL cross platform audio library
 * Copyright (C) 2019 by Anis A. Hireche
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
#include <algorithm>
#include <functional>

#include "al/auxeffectslot.h"
#include "alcmain.h"
#include "alcontext.h"
#include "alu.h"

namespace {

#define MAX_UPDATE_SAMPLES 128
#define NUM_FORMANTS       4
#define NUM_FILTERS        2
#define Q_FACTOR           5.0f

#define VOWEL_A_INDEX      0
#define VOWEL_B_INDEX      1

#define WAVEFORM_FRACBITS  24
#define WAVEFORM_FRACONE   (1<<WAVEFORM_FRACBITS)
#define WAVEFORM_FRACMASK  (WAVEFORM_FRACONE-1)

inline float Sin(ALuint index)
{
    constexpr float scale{al::MathDefs<float>::Tau() / WAVEFORM_FRACONE};
    return std::sin(static_cast<float>(index) * scale)*0.5f + 0.5f;
}

inline float Saw(ALuint index)
{ return static_cast<float>(index) / float{WAVEFORM_FRACONE}; }

inline float Triangle(ALuint index)
{ return std::fabs(static_cast<float>(index)*(2.0f/WAVEFORM_FRACONE) - 1.0f); }

inline float Half(ALuint) { return 0.5f; }

template<float (&func)(ALuint)>
void Oscillate(float *RESTRICT dst, ALuint index, const ALuint step, size_t todo)
{
    for(size_t i{0u};i < todo;i++)
    {
        index += step;
        index &= WAVEFORM_FRACMASK;
        dst[i] = func(index);
    }
}

struct FormantFilter
{
    ALfloat mCoeff{0.0f};
    ALfloat mGain{1.0f};
    ALfloat mS1{0.0f};
    ALfloat mS2{0.0f};

    FormantFilter() = default;
    FormantFilter(ALfloat f0norm, ALfloat gain)
      : mCoeff{std::tan(al::MathDefs<float>::Pi() * f0norm)}, mGain{gain}
    { }

    inline void process(const ALfloat *samplesIn, ALfloat *samplesOut, const size_t numInput)
    {
        /* A state variable filter from a topology-preserving transform.
         * Based on a talk given by Ivan Cohen: https://www.youtube.com/watch?v=esjHXGPyrhg
         */
        const ALfloat g{mCoeff};
        const ALfloat gain{mGain};
        const ALfloat h{1.0f / (1.0f + (g/Q_FACTOR) + (g*g))};
        ALfloat s1{mS1};
        ALfloat s2{mS2};

        for(size_t i{0u};i < numInput;i++)
        {
            const ALfloat H{(samplesIn[i] - (1.0f/Q_FACTOR + g)*s1 - s2)*h};
            const ALfloat B{g*H + s1};
            const ALfloat L{g*B + s2};

            s1 = g*H + B;
            s2 = g*B + L;

            // Apply peak and accumulate samples.
            samplesOut[i] += B * gain;
        }
        mS1 = s1;
        mS2 = s2;
    }

    inline void clear()
    {
        mS1 = 0.0f;
        mS2 = 0.0f;
    }
};


struct VmorpherState final : public EffectState {
    struct {
        /* Effect parameters */
        FormantFilter Formants[NUM_FILTERS][NUM_FORMANTS];

        /* Effect gains for each channel */
        ALfloat CurrentGains[MAX_OUTPUT_CHANNELS]{};
        ALfloat TargetGains[MAX_OUTPUT_CHANNELS]{};
    } mChans[MAX_AMBI_CHANNELS];

    void (*mGetSamples)(float*RESTRICT, ALuint, const ALuint, size_t){};

    ALuint mIndex{0};
    ALuint mStep{1};

    /* Effects buffers */
    ALfloat mSampleBufferA[MAX_UPDATE_SAMPLES]{};
    ALfloat mSampleBufferB[MAX_UPDATE_SAMPLES]{};

    ALboolean deviceUpdate(const ALCdevice *device) override;
    void update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target) override;
    void process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut) override;

    static std::array<FormantFilter,4> getFiltersByPhoneme(ALenum phoneme, ALfloat frequency, ALfloat pitch);

    DEF_NEWDEL(VmorpherState)
};

std::array<FormantFilter,4> VmorpherState::getFiltersByPhoneme(ALenum phoneme, ALfloat frequency, ALfloat pitch)
{
    /* Using soprano formant set of values to
     * better match mid-range frequency space.
     *
     * See: https://www.classes.cs.uchicago.edu/archive/1999/spring/CS295/Computing_Resources/Csound/CsManual3.48b1.HTML/Appendices/table3.html
     */
    switch(phoneme)
    {
    case AL_VOCAL_MORPHER_PHONEME_A:
        return {{
            {( 800 * pitch) / frequency, 1.000000f}, /* std::pow(10.0f,   0 / 20.0f); */
            {(1150 * pitch) / frequency, 0.501187f}, /* std::pow(10.0f,  -6 / 20.0f); */
            {(2900 * pitch) / frequency, 0.025118f}, /* std::pow(10.0f, -32 / 20.0f); */
            {(3900 * pitch) / frequency, 0.100000f}  /* std::pow(10.0f, -20 / 20.0f); */
        }};
    case AL_VOCAL_MORPHER_PHONEME_E:
        return {{
            {( 350 * pitch) / frequency, 1.000000f}, /* std::pow(10.0f,   0 / 20.0f); */
            {(2000 * pitch) / frequency, 0.100000f}, /* std::pow(10.0f, -20 / 20.0f); */
            {(2800 * pitch) / frequency, 0.177827f}, /* std::pow(10.0f, -15 / 20.0f); */
            {(3600 * pitch) / frequency, 0.009999f}  /* std::pow(10.0f, -40 / 20.0f); */
        }};
    case AL_VOCAL_MORPHER_PHONEME_I:
        return {{
            {( 270 * pitch) / frequency, 1.000000f}, /* std::pow(10.0f,   0 / 20.0f); */
            {(2140 * pitch) / frequency, 0.251188f}, /* std::pow(10.0f, -12 / 20.0f); */
            {(2950 * pitch) / frequency, 0.050118f}, /* std::pow(10.0f, -26 / 20.0f); */
            {(3900 * pitch) / frequency, 0.050118f}  /* std::pow(10.0f, -26 / 20.0f); */
        }};
    case AL_VOCAL_MORPHER_PHONEME_O:
        return {{
            {( 450 * pitch) / frequency, 1.000000f}, /* std::pow(10.0f,   0 / 20.0f); */
            {( 800 * pitch) / frequency, 0.281838f}, /* std::pow(10.0f, -11 / 20.0f); */
            {(2830 * pitch) / frequency, 0.079432f}, /* std::pow(10.0f, -22 / 20.0f); */
            {(3800 * pitch) / frequency, 0.079432f}  /* std::pow(10.0f, -22 / 20.0f); */
        }};
    case AL_VOCAL_MORPHER_PHONEME_U:
        return {{
            {( 325 * pitch) / frequency, 1.000000f}, /* std::pow(10.0f,   0 / 20.0f); */
            {( 700 * pitch) / frequency, 0.158489f}, /* std::pow(10.0f, -16 / 20.0f); */
            {(2700 * pitch) / frequency, 0.017782f}, /* std::pow(10.0f, -35 / 20.0f); */
            {(3800 * pitch) / frequency, 0.009999f}  /* std::pow(10.0f, -40 / 20.0f); */
        }};
    }
    return {};
}


ALboolean VmorpherState::deviceUpdate(const ALCdevice* /*device*/)
{
    for(auto &e : mChans)
    {
        std::for_each(std::begin(e.Formants[VOWEL_A_INDEX]), std::end(e.Formants[VOWEL_A_INDEX]),
            std::mem_fn(&FormantFilter::clear));
        std::for_each(std::begin(e.Formants[VOWEL_B_INDEX]), std::end(e.Formants[VOWEL_B_INDEX]),
            std::mem_fn(&FormantFilter::clear));
        std::fill(std::begin(e.CurrentGains), std::end(e.CurrentGains), 0.0f);
    }

    return AL_TRUE;
}

void VmorpherState::update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target)
{
    const ALCdevice *device{context->mDevice.get()};
    const ALfloat frequency{static_cast<ALfloat>(device->Frequency)};
    const ALfloat step{props->Vmorpher.Rate / frequency};
    mStep = fastf2u(clampf(step*WAVEFORM_FRACONE, 0.0f, ALfloat{WAVEFORM_FRACONE-1}));

    if(mStep == 0)
        mGetSamples = Oscillate<Half>;
    else if(props->Vmorpher.Waveform == AL_VOCAL_MORPHER_WAVEFORM_SINUSOID)
        mGetSamples = Oscillate<Sin>;
    else if(props->Vmorpher.Waveform == AL_VOCAL_MORPHER_WAVEFORM_SAWTOOTH)
        mGetSamples = Oscillate<Saw>;
    else /*if(props->Vmorpher.Waveform == AL_VOCAL_MORPHER_WAVEFORM_TRIANGLE)*/
        mGetSamples = Oscillate<Triangle>;

    const ALfloat pitchA{std::pow(2.0f,
        static_cast<float>(props->Vmorpher.PhonemeACoarseTuning) / 12.0f)};
    const ALfloat pitchB{std::pow(2.0f,
        static_cast<float>(props->Vmorpher.PhonemeBCoarseTuning) / 12.0f)};

    auto vowelA = getFiltersByPhoneme(props->Vmorpher.PhonemeA, frequency, pitchA);
    auto vowelB = getFiltersByPhoneme(props->Vmorpher.PhonemeB, frequency, pitchB);

    /* Copy the filter coefficients to the input channels. */
    for(size_t i{0u};i < slot->Wet.Buffer.size();++i)
    {
        std::copy(vowelA.begin(), vowelA.end(), std::begin(mChans[i].Formants[VOWEL_A_INDEX]));
        std::copy(vowelB.begin(), vowelB.end(), std::begin(mChans[i].Formants[VOWEL_B_INDEX]));
    }

    mOutTarget = target.Main->Buffer;
    for(size_t i{0u};i < slot->Wet.Buffer.size();++i)
    {
        auto coeffs = GetAmbiIdentityRow(i);
        ComputePanGains(target.Main, coeffs.data(), slot->Params.Gain, mChans[i].TargetGains);
    }
}

void VmorpherState::process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut)
{
    /* Following the EFX specification for a conformant implementation which describes
     * the effect as a pair of 4-band formant filters blended together using an LFO.
     */
    for(size_t base{0u};base < samplesToDo;)
    {
        alignas(16) ALfloat lfo[MAX_UPDATE_SAMPLES];
        const size_t td{minz(MAX_UPDATE_SAMPLES, samplesToDo-base)};

        mGetSamples(lfo, mIndex, mStep, td);
        mIndex += static_cast<ALuint>(mStep * td);
        mIndex &= WAVEFORM_FRACMASK;

        auto chandata = std::addressof(mChans[0]);
        for(const auto &input : samplesIn)
        {
            std::fill_n(std::begin(mSampleBufferA), td, 0.0f);
            std::fill_n(std::begin(mSampleBufferB), td, 0.0f);

            auto& vowelA = chandata->Formants[VOWEL_A_INDEX];
            auto& vowelB = chandata->Formants[VOWEL_B_INDEX];

            /* Process first vowel. */
            vowelA[0].process(&input[base], mSampleBufferA, td);
            vowelA[1].process(&input[base], mSampleBufferA, td);
            vowelA[2].process(&input[base], mSampleBufferA, td);
            vowelA[3].process(&input[base], mSampleBufferA, td);

            /* Process second vowel. */
            vowelB[0].process(&input[base], mSampleBufferB, td);
            vowelB[1].process(&input[base], mSampleBufferB, td);
            vowelB[2].process(&input[base], mSampleBufferB, td);
            vowelB[3].process(&input[base], mSampleBufferB, td);

            alignas(16) ALfloat blended[MAX_UPDATE_SAMPLES];
            for(size_t i{0u};i < td;i++)
                blended[i] = lerp(mSampleBufferA[i], mSampleBufferB[i], lfo[i]);

            /* Now, mix the processed sound data to the output. */
            MixSamples({blended, td}, samplesOut, chandata->CurrentGains, chandata->TargetGains,
                samplesToDo-base, base);
            ++chandata;
        }

        base += td;
    }
}


void Vmorpher_setParami(EffectProps* props, ALCcontext *context, ALenum param, ALint val)
{
    switch(param)
    {
        case AL_VOCAL_MORPHER_WAVEFORM:
            if(!(val >= AL_VOCAL_MORPHER_MIN_WAVEFORM && val <= AL_VOCAL_MORPHER_MAX_WAVEFORM))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Vocal morpher waveform out of range");
            props->Vmorpher.Waveform = val;
            break;

        case AL_VOCAL_MORPHER_PHONEMEA:
            if(!(val >= AL_VOCAL_MORPHER_MIN_PHONEMEA && val <= AL_VOCAL_MORPHER_MAX_PHONEMEA))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Vocal morpher phoneme-a out of range");
            props->Vmorpher.PhonemeA = val;
            break;

        case AL_VOCAL_MORPHER_PHONEMEB:
            if(!(val >= AL_VOCAL_MORPHER_MIN_PHONEMEB && val <= AL_VOCAL_MORPHER_MAX_PHONEMEB))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Vocal morpher phoneme-b out of range");
            props->Vmorpher.PhonemeB = val;
            break;

        case AL_VOCAL_MORPHER_PHONEMEA_COARSE_TUNING:
            if(!(val >= AL_VOCAL_MORPHER_MIN_PHONEMEA_COARSE_TUNING && val <= AL_VOCAL_MORPHER_MAX_PHONEMEA_COARSE_TUNING))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Vocal morpher phoneme-a coarse tuning out of range");
            props->Vmorpher.PhonemeACoarseTuning = val;
            break;

        case AL_VOCAL_MORPHER_PHONEMEB_COARSE_TUNING:
            if(!(val >= AL_VOCAL_MORPHER_MIN_PHONEMEB_COARSE_TUNING && val <= AL_VOCAL_MORPHER_MAX_PHONEMEB_COARSE_TUNING))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Vocal morpher phoneme-b coarse tuning out of range");
            props->Vmorpher.PhonemeBCoarseTuning = val;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid vocal morpher integer property 0x%04x",
                param);
    }
}
void Vmorpher_setParamiv(EffectProps*, ALCcontext *context, ALenum param, const ALint*)
{ context->setError(AL_INVALID_ENUM, "Invalid vocal morpher integer-vector property 0x%04x", param); }
void Vmorpher_setParamf(EffectProps *props, ALCcontext *context, ALenum param, ALfloat val)
{
    switch(param)
    {
        case AL_VOCAL_MORPHER_RATE:
            if(!(val >= AL_VOCAL_MORPHER_MIN_RATE && val <= AL_VOCAL_MORPHER_MAX_RATE))
              SETERR_RETURN(context, AL_INVALID_VALUE,, "Vocal morpher rate out of range");
            props->Vmorpher.Rate = val;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid vocal morpher float property 0x%04x",
                param);
    }
}
void Vmorpher_setParamfv(EffectProps *props, ALCcontext *context, ALenum param, const ALfloat *vals)
{ Vmorpher_setParamf(props, context, param, vals[0]); }

void Vmorpher_getParami(const EffectProps* props, ALCcontext *context, ALenum param, ALint* val)
{
    switch(param)
    {
        case AL_VOCAL_MORPHER_PHONEMEA:
            *val = props->Vmorpher.PhonemeA;
            break;

        case AL_VOCAL_MORPHER_PHONEMEB:
            *val = props->Vmorpher.PhonemeB;
            break;

        case AL_VOCAL_MORPHER_PHONEMEA_COARSE_TUNING:
            *val = props->Vmorpher.PhonemeACoarseTuning;
            break;

        case AL_VOCAL_MORPHER_PHONEMEB_COARSE_TUNING:
            *val = props->Vmorpher.PhonemeBCoarseTuning;
            break;

        case AL_VOCAL_MORPHER_WAVEFORM:
            *val = props->Vmorpher.Waveform;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid vocal morpher integer property 0x%04x",
                param);
    }
}
void Vmorpher_getParamiv(const EffectProps*, ALCcontext *context, ALenum param, ALint*)
{ context->setError(AL_INVALID_ENUM, "Invalid vocal morpher integer-vector property 0x%04x", param); }
void Vmorpher_getParamf(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *val)
{
    switch(param)
    {
        case AL_VOCAL_MORPHER_RATE:
            *val = props->Vmorpher.Rate;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid vocal morpher float property 0x%04x",
                param);
    }
}
void Vmorpher_getParamfv(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *vals)
{ Vmorpher_getParamf(props, context, param, vals); }

DEFINE_ALEFFECT_VTABLE(Vmorpher);


struct VmorpherStateFactory final : public EffectStateFactory {
    EffectState *create() override { return new VmorpherState{}; }
    EffectProps getDefaultProps() const noexcept override;
    const EffectVtable *getEffectVtable() const noexcept override { return &Vmorpher_vtable; }
};

EffectProps VmorpherStateFactory::getDefaultProps() const noexcept
{
    EffectProps props{};
    props.Vmorpher.Rate                 = AL_VOCAL_MORPHER_DEFAULT_RATE;
    props.Vmorpher.PhonemeA             = AL_VOCAL_MORPHER_DEFAULT_PHONEMEA;
    props.Vmorpher.PhonemeB             = AL_VOCAL_MORPHER_DEFAULT_PHONEMEB;
    props.Vmorpher.PhonemeACoarseTuning = AL_VOCAL_MORPHER_DEFAULT_PHONEMEA_COARSE_TUNING;
    props.Vmorpher.PhonemeBCoarseTuning = AL_VOCAL_MORPHER_DEFAULT_PHONEMEB_COARSE_TUNING;
    props.Vmorpher.Waveform             = AL_VOCAL_MORPHER_DEFAULT_WAVEFORM;
    return props;
}

} // namespace

EffectStateFactory *VmorpherStateFactory_getFactory()
{
    static VmorpherStateFactory VmorpherFactory{};
    return &VmorpherFactory;
}
