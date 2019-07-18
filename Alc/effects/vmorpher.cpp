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

#include "alMain.h"
#include "alcontext.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
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

inline ALfloat Sin(ALsizei index)
{
    return (std::sin(static_cast<ALfloat>(index) *
        (al::MathDefs<float>::Tau() / ALfloat{WAVEFORM_FRACONE})))*0.5f+0.5f;
}

inline ALfloat Saw(ALsizei index)
{
    return (static_cast<ALfloat>(index)*(2.0f/WAVEFORM_FRACONE) - 1.0f)*0.5f+0.5f;
}

inline ALfloat Triangle(ALsizei index)
{
    return (std::fabs(static_cast<ALfloat>(index) * (al::MathDefs<float>::Tau() / WAVEFORM_FRACONE) -
        al::MathDefs<float>::Pi()) / al::MathDefs<float>::Pi())*0.5f+0.5f;
}

inline ALfloat Half(ALsizei UNUSED(index))
{
    return 0.5f;
}

template<ALfloat func(ALsizei)>
void Oscillate(ALfloat *RESTRICT dst, ALsizei index, const ALsizei step, ALsizei todo)
{
    for(ALsizei i{0};i < todo;i++)
    {
        index += step;
        index &= WAVEFORM_FRACMASK;
        dst[i] = func(index);
    }
}

struct FormantFilter
{
    inline void process(const ALfloat* samplesIn, ALfloat* samplesOut, const ALsizei numInput)
    {
        /* A state variable filter from a topology-preserving transform.
         * Based on a talk given by Ivan Cohen: https://www.youtube.com/watch?v=esjHXGPyrhg
         */
        const ALfloat g = std::tan(al::MathDefs<float>::Pi() * f0norm);
        const ALfloat h = 1.0f / (1 + (g / Q_FACTOR) + (g * g));

        for (ALsizei i{0};i < numInput;i++)
        {
            const ALfloat H = h * (samplesIn[i] - (1.0f / Q_FACTOR + g) * s1 - s2);
            const ALfloat B = g * H + s1;
            const ALfloat L = g * B + s2;

            s1 = g * H + B;
            s2 = g * B + L;

            // Apply peak and accumulate samples.
            samplesOut[i] += B * fGain;
        }
    }

    inline void clear()
    {
        s1 = 0.0f;
        s2 = 0.0f;
    }

    ALfloat f0norm;
    ALfloat fGain;
    ALfloat s1;
    ALfloat s2;
};


struct VmorpherState final : public EffectState {
    struct {
        /* Effect parameters */
        FormantFilter Formants[NUM_FILTERS][NUM_FORMANTS];

        /* Effect gains for each channel */
        ALfloat CurrentGains[MAX_OUTPUT_CHANNELS]{};
        ALfloat TargetGains[MAX_OUTPUT_CHANNELS]{};
    } mChans[MAX_AMBI_CHANNELS];

    void (*mGetSamples)(ALfloat* RESTRICT, ALsizei, const ALsizei, ALsizei) {};

    ALsizei mIndex{0};
    ALsizei mStep{1};

    /* Effects buffers */
    ALfloat mSampleBufferA[MAX_UPDATE_SAMPLES]{};
    ALfloat mSampleBufferB[MAX_UPDATE_SAMPLES]{};

    ALboolean deviceUpdate(const ALCdevice *device) override;
    void update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target) override;
    void process(const ALsizei samplesToDo, const FloatBufferLine *RESTRICT samplesIn, const ALsizei numInput, const al::span<FloatBufferLine> samplesOut) override;

    DEF_NEWDEL(VmorpherState)
};

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
    const ALCdevice *device{context->Device};
    const ALfloat frequency{static_cast<ALfloat>(device->Frequency)};
    const ALfloat step{props->Vmorpher.Rate / static_cast<ALfloat>(device->Frequency)};
    mStep = fastf2i(clampf(step*WAVEFORM_FRACONE, 0.0f, ALfloat{WAVEFORM_FRACONE-1}));

    if(mStep == 0)
        mGetSamples = Oscillate<Half>;
    else if(props->Vmorpher.Waveform == AL_VOCAL_MORPHER_WAVEFORM_SINUSOID)
        mGetSamples = Oscillate<Sin>;
    else if(props->Vmorpher.Waveform == AL_VOCAL_MORPHER_WAVEFORM_SAWTOOTH)
        mGetSamples = Oscillate<Saw>;
    else /*if(props->Vmorpher.Waveform == AL_VOCAL_MORPHER_WAVEFORM_TRIANGLE)*/
        mGetSamples = Oscillate<Triangle>;

    auto& vowelA = mChans[0].Formants[VOWEL_A_INDEX];
    auto& vowelB = mChans[0].Formants[VOWEL_B_INDEX];

    const ALfloat pitchA{fastf2i(std::pow(2.0f, props->Vmorpher.PhonemeACoarseTuning*100.0f / 2400.0f)*FRACTIONONE) * (1.0f/FRACTIONONE)};
    const ALfloat pitchB{fastf2i(std::pow(2.0f, props->Vmorpher.PhonemeBCoarseTuning*100.0f / 2400.0f)*FRACTIONONE) * (1.0f/FRACTIONONE)};

    /* Using soprano formant set of values to
     * better match mid-range frequency space.
     *
     * See: https://www.classes.cs.uchicago.edu/archive/1999/spring/CS295/Computing_Resources/Csound/CsManual3.48b1.HTML/Appendices/table3.html
     */
    switch(props->Vmorpher.PhonemeA)
    {
        case AL_VOCAL_MORPHER_PHONEME_A:
            vowelA[0].f0norm =  (800 * pitchA) / frequency;
            vowelA[1].f0norm = (1150 * pitchA) / frequency;
            vowelA[2].f0norm = (2900 * pitchA) / frequency;
            vowelA[3].f0norm = (3900 * pitchA) / frequency;

            vowelA[0].fGain = 1.000000f; /* std::pow(10.0f,   0 / 20.0f); */
            vowelA[1].fGain = 0.501187f; /* std::pow(10.0f,  -6 / 20.0f); */
            vowelA[2].fGain = 0.025118f; /* std::pow(10.0f, -32 / 20.0f); */
            vowelA[3].fGain = 0.100000f; /* std::pow(10.0f, -20 / 20.0f); */
            break;
        case AL_VOCAL_MORPHER_PHONEME_E:
            vowelA[0].f0norm =  (350 * pitchA) / frequency;
            vowelA[1].f0norm = (2000 * pitchA) / frequency;
            vowelA[2].f0norm = (2800 * pitchA) / frequency;
            vowelA[3].f0norm = (3600 * pitchA) / frequency;

            vowelA[0].fGain = 1.000000f; /* std::pow(10.0f,   0 / 20.0f); */
            vowelA[1].fGain = 0.100000f; /* std::pow(10.0f, -20 / 20.0f); */
            vowelA[2].fGain = 0.177827f; /* std::pow(10.0f, -15 / 20.0f); */
            vowelA[3].fGain = 0.009999f; /* std::pow(10.0f, -40 / 20.0f); */
            break;
        case AL_VOCAL_MORPHER_PHONEME_I:
            vowelA[0].f0norm =  (270 * pitchA) / frequency;
            vowelA[1].f0norm = (2140 * pitchA) / frequency;
            vowelA[2].f0norm = (2950 * pitchA) / frequency;
            vowelA[3].f0norm = (3900 * pitchA) / frequency;

            vowelA[0].fGain = 1.000000f; /* std::pow(10.0f,   0 / 20.0f); */
            vowelA[1].fGain = 0.251188f; /* std::pow(10.0f, -12 / 20.0f); */
            vowelA[2].fGain = 0.050118f; /* std::pow(10.0f, -26 / 20.0f); */
            vowelA[3].fGain = 0.050118f; /* std::pow(10.0f, -26 / 20.0f); */
            break;
        case AL_VOCAL_MORPHER_PHONEME_O:
            vowelA[0].f0norm =  (450 * pitchA) / frequency;
            vowelA[1].f0norm =  (800 * pitchA) / frequency;
            vowelA[2].f0norm = (2830 * pitchA) / frequency;
            vowelA[3].f0norm = (3800 * pitchA) / frequency;

            vowelA[0].fGain = 1.000000f; /* std::pow(10.0f,   0 / 20.0f); */
            vowelA[1].fGain = 0.281838f; /* std::pow(10.0f, -11 / 20.0f); */
            vowelA[2].fGain = 0.079432f; /* std::pow(10.0f, -22 / 20.0f); */
            vowelA[3].fGain = 0.079432f; /* std::pow(10.0f, -22 / 20.0f); */
            break;
        case AL_VOCAL_MORPHER_PHONEME_U:
            vowelA[0].f0norm =  (325 * pitchA) / frequency;
            vowelA[1].f0norm =  (700 * pitchA) / frequency;
            vowelA[2].f0norm = (2700 * pitchA) / frequency;
            vowelA[3].f0norm = (3800 * pitchA) / frequency;

            vowelA[0].fGain = 1.000000f; /* std::pow(10.0f,   0 / 20.0f); */
            vowelA[1].fGain = 0.158489f; /* std::pow(10.0f, -16 / 20.0f); */
            vowelA[2].fGain = 0.017782f; /* std::pow(10.0f, -35 / 20.0f); */
            vowelA[3].fGain = 0.009999f; /* std::pow(10.0f, -40 / 20.0f); */
            break;
    }

    switch(props->Vmorpher.PhonemeB)
    {
        case AL_VOCAL_MORPHER_PHONEME_A:
            vowelB[0].f0norm =  (800 * pitchB) / frequency;
            vowelB[1].f0norm = (1150 * pitchB) / frequency;
            vowelB[2].f0norm = (2900 * pitchB) / frequency;
            vowelB[3].f0norm = (3900 * pitchB) / frequency;

            vowelB[0].fGain = 1.000000f; /* std::pow(10.0f,   0 / 20.0f); */
            vowelB[1].fGain = 0.501187f; /* std::pow(10.0f,  -6 / 20.0f); */
            vowelB[2].fGain = 0.025118f; /* std::pow(10.0f, -32 / 20.0f); */
            vowelB[3].fGain = 0.100000f; /* std::pow(10.0f, -20 / 20.0f); */
            break;
        case AL_VOCAL_MORPHER_PHONEME_E:
            vowelB[0].f0norm =  (350 * pitchB) / frequency;
            vowelB[1].f0norm = (2000 * pitchB) / frequency;
            vowelB[2].f0norm = (2800 * pitchB) / frequency;
            vowelB[3].f0norm = (3600 * pitchB) / frequency;

            vowelB[0].fGain = 1.000000f; /* std::pow(10.0f,   0 / 20.0f); */
            vowelB[1].fGain = 0.100000f; /* std::pow(10.0f, -20 / 20.0f); */
            vowelB[2].fGain = 0.177827f; /* std::pow(10.0f, -15 / 20.0f); */
            vowelB[3].fGain = 0.009999f; /* std::pow(10.0f, -40 / 20.0f); */
            break;
        case AL_VOCAL_MORPHER_PHONEME_I:
            vowelB[0].f0norm =  (270 * pitchB) / frequency;
            vowelB[1].f0norm = (2140 * pitchB) / frequency;
            vowelB[2].f0norm = (2950 * pitchB) / frequency;
            vowelB[3].f0norm = (3900 * pitchB) / frequency;

            vowelB[0].fGain = 1.000000f; /* std::pow(10.0f,   0 / 20.0f); */
            vowelB[1].fGain = 0.251188f; /* std::pow(10.0f, -12 / 20.0f); */
            vowelB[2].fGain = 0.050118f; /* std::pow(10.0f, -26 / 20.0f); */
            vowelB[3].fGain = 0.050118f; /* std::pow(10.0f, -26 / 20.0f); */
            break;
        case AL_VOCAL_MORPHER_PHONEME_O:
            vowelB[0].f0norm =  (450 * pitchB) / frequency;
            vowelB[1].f0norm =  (800 * pitchB) / frequency;
            vowelB[2].f0norm = (2830 * pitchB) / frequency;
            vowelB[3].f0norm = (3800 * pitchB) / frequency;

            vowelB[0].fGain = 1.000000f; /* std::pow(10.0f,   0 / 20.0f); */
            vowelB[1].fGain = 0.281838f; /* std::pow(10.0f, -11 / 20.0f); */
            vowelB[2].fGain = 0.079432f; /* std::pow(10.0f, -22 / 20.0f); */
            vowelB[3].fGain = 0.079432f; /* std::pow(10.0f, -22 / 20.0f); */
            break;
        case AL_VOCAL_MORPHER_PHONEME_U:
            vowelB[0].f0norm =  (325 * pitchB) / frequency;
            vowelB[1].f0norm =  (700 * pitchB) / frequency;
            vowelB[2].f0norm = (2700 * pitchB) / frequency;
            vowelB[3].f0norm = (3800 * pitchB) / frequency;

            vowelB[0].fGain = 1.000000f; /* std::pow(10.0f,   0 / 20.0f); */
            vowelB[1].fGain = 0.158489f; /* std::pow(10.0f, -16 / 20.0f); */
            vowelB[2].fGain = 0.017782f; /* std::pow(10.0f, -35 / 20.0f); */
            vowelB[3].fGain = 0.009999f; /* std::pow(10.0f, -40 / 20.0f); */
            break;
    }

    /* Copy the filter coefficients for the other input channels. */
    for(ALuint i{1u};i < slot->Wet.Buffer.size();++i)
    {
        mChans[i].Formants[VOWEL_A_INDEX][0] = vowelA[0];
        mChans[i].Formants[VOWEL_A_INDEX][1] = vowelA[1];
        mChans[i].Formants[VOWEL_A_INDEX][2] = vowelA[2];
        mChans[i].Formants[VOWEL_A_INDEX][3] = vowelA[3];

        mChans[i].Formants[VOWEL_B_INDEX][0] = vowelB[0];
        mChans[i].Formants[VOWEL_B_INDEX][1] = vowelB[1];
        mChans[i].Formants[VOWEL_B_INDEX][2] = vowelB[2];
        mChans[i].Formants[VOWEL_B_INDEX][3] = vowelB[3];
    }

    mOutTarget = target.Main->Buffer;
    for(ALuint i{0u};i < slot->Wet.Buffer.size();++i)
    {
        auto coeffs = GetAmbiIdentityRow(i);
        ComputePanGains(target.Main, coeffs.data(), slot->Params.Gain, mChans[i].TargetGains);
    }
}

void VmorpherState::process(const ALsizei samplesToDo, const FloatBufferLine *RESTRICT samplesIn, const ALsizei numInput, const al::span<FloatBufferLine> samplesOut)
{
    /* Following the EFX specification for a conformant implementation which describes
     * the effect as a pair of 4-band formant filters blended together using an LFO.
     */
    for(ALsizei base{0};base < samplesToDo;)
    {
        alignas(16) ALfloat lfo[MAX_UPDATE_SAMPLES];
        const ALsizei td = mini(MAX_UPDATE_SAMPLES, samplesToDo-base);

        mGetSamples(lfo, mIndex, mStep, td);
        mIndex += (mStep * td) & WAVEFORM_FRACMASK;
        mIndex &= WAVEFORM_FRACMASK;

        ASSUME(numInput > 0);
        for(ALsizei c{0};c < numInput;c++)
        {
            for (ALsizei i{0};i < td;i++)
            {
                mSampleBufferA[i] = 0.0f;
                mSampleBufferB[i] = 0.0f;
            }

            auto& vowelA = mChans[c].Formants[VOWEL_A_INDEX];
            auto& vowelB = mChans[c].Formants[VOWEL_B_INDEX];

            /* Process first vowel. */
            vowelA[0].process(&samplesIn[c][base], mSampleBufferA, td);
            vowelA[1].process(&samplesIn[c][base], mSampleBufferA, td);
            vowelA[2].process(&samplesIn[c][base], mSampleBufferA, td);
            vowelA[3].process(&samplesIn[c][base], mSampleBufferA, td);

            /* Process second vowel. */
            vowelB[0].process(&samplesIn[c][base], mSampleBufferB, td);
            vowelB[1].process(&samplesIn[c][base], mSampleBufferB, td);
            vowelB[2].process(&samplesIn[c][base], mSampleBufferB, td);
            vowelB[3].process(&samplesIn[c][base], mSampleBufferB, td);

            alignas(16) ALfloat samplesBlended[MAX_UPDATE_SAMPLES];

            for (ALsizei i{0};i < td;i++)
                samplesBlended[i] = lerp(mSampleBufferA[i], mSampleBufferB[i], lfo[i]);

            /* Now, mix the processed sound data to the output. */
            MixSamples(samplesBlended, samplesOut, mChans[c].CurrentGains, mChans[c].TargetGains,
                samplesToDo-base, base, td);
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
            alSetError(context, AL_INVALID_ENUM, "Invalid vocal morpher integer property 0x%04x", param);
    }
}
void Vmorpher_setParamiv(EffectProps*, ALCcontext *context, ALenum param, const ALint*)
{ alSetError(context, AL_INVALID_ENUM, "Invalid vocal morpher integer-vector property 0x%04x", param); }
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
            alSetError(context, AL_INVALID_ENUM, "Invalid vocal morpher float property 0x%04x", param);
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
            alSetError(context, AL_INVALID_ENUM, "Invalid vocal morpher integer property 0x%04x", param);
    }
}
void Vmorpher_getParamiv(const EffectProps*, ALCcontext *context, ALenum param, ALint*)
{ alSetError(context, AL_INVALID_ENUM, "Invalid vocal morpher integer-vector property 0x%04x", param); }
void Vmorpher_getParamf(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *val)
{
    switch(param)
    {
        case AL_VOCAL_MORPHER_RATE:
            *val = props->Vmorpher.Rate;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid vocal morpher float property 0x%04x", param);
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
