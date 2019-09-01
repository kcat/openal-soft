/**
 * OpenAL cross platform audio library
 * Copyright (C) 2013 by Mike Gorchak
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

#include "al/auxeffectslot.h"
#include "alcmain.h"
#include "alcontext.h"
#include "alu.h"
#include "filters/biquad.h"


namespace {

struct DistortionState final : public EffectState {
    /* Effect gains for each channel */
    ALfloat mGain[MAX_OUTPUT_CHANNELS]{};

    /* Effect parameters */
    BiquadFilter mLowpass;
    BiquadFilter mBandpass;
    ALfloat mAttenuation{};
    ALfloat mEdgeCoeff{};

    ALfloat mBuffer[2][BUFFERSIZE]{};


    ALboolean deviceUpdate(const ALCdevice *device) override;
    void update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target) override;
    void process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut) override;

    DEF_NEWDEL(DistortionState)
};

ALboolean DistortionState::deviceUpdate(const ALCdevice*)
{
    mLowpass.clear();
    mBandpass.clear();
    return AL_TRUE;
}

void DistortionState::update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target)
{
    const ALCdevice *device{context->mDevice.get()};

    /* Store waveshaper edge settings. */
    const ALfloat edge{
        minf(std::sin(al::MathDefs<float>::Pi()*0.5f * props->Distortion.Edge), 0.99f)};
    mEdgeCoeff = 2.0f * edge / (1.0f-edge);

    ALfloat cutoff{props->Distortion.LowpassCutoff};
    /* Bandwidth value is constant in octaves. */
    ALfloat bandwidth{(cutoff / 2.0f) / (cutoff * 0.67f)};
    /* Multiply sampling frequency by the amount of oversampling done during
     * processing.
     */
    auto frequency = static_cast<ALfloat>(device->Frequency);
    mLowpass.setParams(BiquadType::LowPass, 1.0f, cutoff / (frequency*4.0f),
        mLowpass.rcpQFromBandwidth(cutoff / (frequency*4.0f), bandwidth));

    cutoff = props->Distortion.EQCenter;
    /* Convert bandwidth in Hz to octaves. */
    bandwidth = props->Distortion.EQBandwidth / (cutoff * 0.67f);
    mBandpass.setParams(BiquadType::BandPass, 1.0f, cutoff / (frequency*4.0f),
        mBandpass.rcpQFromBandwidth(cutoff / (frequency*4.0f), bandwidth));

    ALfloat coeffs[MAX_AMBI_CHANNELS];
    CalcDirectionCoeffs({0.0f, 0.0f, -1.0f}, 0.0f, coeffs);

    mOutTarget = target.Main->Buffer;
    ComputePanGains(target.Main, coeffs, slot->Params.Gain*props->Distortion.Gain, mGain);
}

void DistortionState::process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut)
{
    const ALfloat fc{mEdgeCoeff};
    for(size_t base{0u};base < samplesToDo;)
    {
        /* Perform 4x oversampling to avoid aliasing. Oversampling greatly
         * improves distortion quality and allows to implement lowpass and
         * bandpass filters using high frequencies, at which classic IIR
         * filters became unstable.
         */
        size_t todo{minz(BUFFERSIZE, (samplesToDo-base) * 4)};

        /* Fill oversample buffer using zero stuffing. Multiply the sample by
         * the amount of oversampling to maintain the signal's power.
         */
        for(size_t i{0u};i < todo;i++)
            mBuffer[0][i] = !(i&3) ? samplesIn[0][(i>>2)+base] * 4.0f : 0.0f;

        /* First step, do lowpass filtering of original signal. Additionally
         * perform buffer interpolation and lowpass cutoff for oversampling
         * (which is fortunately first step of distortion). So combine three
         * operations into the one.
         */
        mLowpass.process(mBuffer[1], mBuffer[0], todo);

        /* Second step, do distortion using waveshaper function to emulate
         * signal processing during tube overdriving. Three steps of
         * waveshaping are intended to modify waveform without boost/clipping/
         * attenuation process.
         */
        for(size_t i{0u};i < todo;i++)
        {
            ALfloat smp{mBuffer[1][i]};

            smp = (1.0f + fc) * smp/(1.0f + fc*fabsf(smp));
            smp = (1.0f + fc) * smp/(1.0f + fc*fabsf(smp)) * -1.0f;
            smp = (1.0f + fc) * smp/(1.0f + fc*fabsf(smp));

            mBuffer[0][i] = smp;
        }

        /* Third step, do bandpass filtering of distorted signal. */
        mBandpass.process(mBuffer[1], mBuffer[0], todo);

        todo >>= 2;
        const ALfloat *outgains{mGain};
        for(FloatBufferLine &output : samplesOut)
        {
            /* Fourth step, final, do attenuation and perform decimation,
             * storing only one sample out of four.
             */
            const ALfloat gain{*(outgains++)};
            if(!(std::fabs(gain) > GAIN_SILENCE_THRESHOLD))
                continue;

            for(size_t i{0u};i < todo;i++)
                output[base+i] += gain * mBuffer[1][i*4];
        }

        base += todo;
    }
}


void Distortion_setParami(EffectProps*, ALCcontext *context, ALenum param, ALint)
{ context->setError(AL_INVALID_ENUM, "Invalid distortion integer property 0x%04x", param); }
void Distortion_setParamiv(EffectProps*, ALCcontext *context, ALenum param, const ALint*)
{ context->setError(AL_INVALID_ENUM, "Invalid distortion integer-vector property 0x%04x", param); }
void Distortion_setParamf(EffectProps *props, ALCcontext *context, ALenum param, ALfloat val)
{
    switch(param)
    {
        case AL_DISTORTION_EDGE:
            if(!(val >= AL_DISTORTION_MIN_EDGE && val <= AL_DISTORTION_MAX_EDGE))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Distortion edge out of range");
            props->Distortion.Edge = val;
            break;

        case AL_DISTORTION_GAIN:
            if(!(val >= AL_DISTORTION_MIN_GAIN && val <= AL_DISTORTION_MAX_GAIN))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Distortion gain out of range");
            props->Distortion.Gain = val;
            break;

        case AL_DISTORTION_LOWPASS_CUTOFF:
            if(!(val >= AL_DISTORTION_MIN_LOWPASS_CUTOFF && val <= AL_DISTORTION_MAX_LOWPASS_CUTOFF))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Distortion low-pass cutoff out of range");
            props->Distortion.LowpassCutoff = val;
            break;

        case AL_DISTORTION_EQCENTER:
            if(!(val >= AL_DISTORTION_MIN_EQCENTER && val <= AL_DISTORTION_MAX_EQCENTER))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Distortion EQ center out of range");
            props->Distortion.EQCenter = val;
            break;

        case AL_DISTORTION_EQBANDWIDTH:
            if(!(val >= AL_DISTORTION_MIN_EQBANDWIDTH && val <= AL_DISTORTION_MAX_EQBANDWIDTH))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Distortion EQ bandwidth out of range");
            props->Distortion.EQBandwidth = val;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid distortion float property 0x%04x", param);
    }
}
void Distortion_setParamfv(EffectProps *props, ALCcontext *context, ALenum param, const ALfloat *vals)
{ Distortion_setParamf(props, context, param, vals[0]); }

void Distortion_getParami(const EffectProps*, ALCcontext *context, ALenum param, ALint*)
{ context->setError(AL_INVALID_ENUM, "Invalid distortion integer property 0x%04x", param); }
void Distortion_getParamiv(const EffectProps*, ALCcontext *context, ALenum param, ALint*)
{ context->setError(AL_INVALID_ENUM, "Invalid distortion integer-vector property 0x%04x", param); }
void Distortion_getParamf(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *val)
{
    switch(param)
    {
        case AL_DISTORTION_EDGE:
            *val = props->Distortion.Edge;
            break;

        case AL_DISTORTION_GAIN:
            *val = props->Distortion.Gain;
            break;

        case AL_DISTORTION_LOWPASS_CUTOFF:
            *val = props->Distortion.LowpassCutoff;
            break;

        case AL_DISTORTION_EQCENTER:
            *val = props->Distortion.EQCenter;
            break;

        case AL_DISTORTION_EQBANDWIDTH:
            *val = props->Distortion.EQBandwidth;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid distortion float property 0x%04x", param);
    }
}
void Distortion_getParamfv(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *vals)
{ Distortion_getParamf(props, context, param, vals); }

DEFINE_ALEFFECT_VTABLE(Distortion);


struct DistortionStateFactory final : public EffectStateFactory {
    EffectState *create() override { return new DistortionState{}; }
    EffectProps getDefaultProps() const noexcept override;
    const EffectVtable *getEffectVtable() const noexcept override { return &Distortion_vtable; }
};

EffectProps DistortionStateFactory::getDefaultProps() const noexcept
{
    EffectProps props{};
    props.Distortion.Edge = AL_DISTORTION_DEFAULT_EDGE;
    props.Distortion.Gain = AL_DISTORTION_DEFAULT_GAIN;
    props.Distortion.LowpassCutoff = AL_DISTORTION_DEFAULT_LOWPASS_CUTOFF;
    props.Distortion.EQCenter = AL_DISTORTION_DEFAULT_EQCENTER;
    props.Distortion.EQBandwidth = AL_DISTORTION_DEFAULT_EQBANDWIDTH;
    return props;
}

} // namespace

EffectStateFactory *DistortionStateFactory_getFactory()
{
    static DistortionStateFactory DistortionFactory{};
    return &DistortionFactory;
}
