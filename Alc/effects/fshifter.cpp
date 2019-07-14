/**
 * OpenAL cross platform audio library
 * Copyright (C) 2018 by Raul Herraiz.
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
#include <array>
#include <complex>
#include <algorithm>

#include "alMain.h"
#include "alcontext.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
#include "alu.h"

#include "alcomplex.h"

namespace {

using complex_d = std::complex<double>;

#define HIL_SIZE 1024
#define OVERSAMP (1<<2)

#define HIL_STEP     (HIL_SIZE / OVERSAMP)
#define FIFO_LATENCY (HIL_STEP * (OVERSAMP-1))

/* Define a Hann window, used to filter the HIL input and output. */
/* Making this constexpr seems to require C++14. */
std::array<ALdouble,HIL_SIZE> InitHannWindow()
{
    std::array<ALdouble,HIL_SIZE> ret;
    /* Create lookup table of the Hann window for the desired size, i.e. HIL_SIZE */
    for(ALsizei i{0};i < HIL_SIZE>>1;i++)
    {
        ALdouble val = std::sin(al::MathDefs<double>::Pi() * i / ALdouble{HIL_SIZE-1});
        ret[i] = ret[HIL_SIZE-1-i] = val * val;
    }
    return ret;
}
alignas(16) const std::array<ALdouble,HIL_SIZE> HannWindow = InitHannWindow();


struct FshifterState final : public EffectState {
    /* Effect parameters */
    ALsizei  mCount{};
    ALsizei  mPhaseStep{};
    ALsizei  mPhase{};
    ALdouble mLdSign{};

    /*Effects buffers*/ 
    ALfloat   mInFIFO[HIL_SIZE]{};
    complex_d mOutFIFO[HIL_SIZE]{};
    complex_d mOutputAccum[HIL_SIZE]{};
    complex_d mAnalytic[HIL_SIZE]{};
    complex_d mOutdata[BUFFERSIZE]{};

    alignas(16) ALfloat mBufferOut[BUFFERSIZE]{};

    /* Effect gains for each output channel */
    ALfloat mCurrentGains[MAX_OUTPUT_CHANNELS]{};
    ALfloat mTargetGains[MAX_OUTPUT_CHANNELS]{};


    ALboolean deviceUpdate(const ALCdevice *device) override;
    void update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target) override;
    void process(const ALsizei samplesToDo, const FloatBufferLine *RESTRICT samplesIn, const ALsizei numInput, const al::span<FloatBufferLine> samplesOut) override;

    DEF_NEWDEL(FshifterState)
};

ALboolean FshifterState::deviceUpdate(const ALCdevice *UNUSED(device))
{
    /* (Re-)initializing parameters and clear the buffers. */
    mCount     = FIFO_LATENCY;
    mPhaseStep = 0;
    mPhase     = 0;
    mLdSign    = 1.0;

    std::fill(std::begin(mInFIFO),      std::end(mInFIFO),      0.0f);
    std::fill(std::begin(mOutFIFO),     std::end(mOutFIFO),     complex_d{});
    std::fill(std::begin(mOutputAccum), std::end(mOutputAccum), complex_d{});
    std::fill(std::begin(mAnalytic),    std::end(mAnalytic),    complex_d{});

    std::fill(std::begin(mCurrentGains), std::end(mCurrentGains), 0.0f);
    std::fill(std::begin(mTargetGains),  std::end(mTargetGains),  0.0f);

    return AL_TRUE;
}

void FshifterState::update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target)
{
    const ALCdevice *device{context->Device};

    ALfloat step{props->Fshifter.Frequency / static_cast<ALfloat>(device->Frequency)};
    mPhaseStep = fastf2i(minf(step, 0.5f) * FRACTIONONE);

    switch(props->Fshifter.LeftDirection)
    {
        case AL_FREQUENCY_SHIFTER_DIRECTION_DOWN:
            mLdSign = -1.0;
            break;

        case AL_FREQUENCY_SHIFTER_DIRECTION_UP:
            mLdSign = 1.0;
            break;

        case AL_FREQUENCY_SHIFTER_DIRECTION_OFF:
            mPhase = 0;
            mPhaseStep = 0;
            break;
    }

    ALfloat coeffs[MAX_AMBI_CHANNELS];
    CalcDirectionCoeffs({0.0f, 0.0f, -1.0f}, 0.0f, coeffs);

    mOutTarget = target.Main->Buffer;
    ComputePanGains(target.Main, coeffs, slot->Params.Gain, mTargetGains);
}

void FshifterState::process(const ALsizei samplesToDo, const FloatBufferLine *RESTRICT samplesIn, const ALsizei /*numInput*/, const al::span<FloatBufferLine> samplesOut)
{
    static constexpr complex_d complex_zero{0.0, 0.0};
    ALfloat *RESTRICT BufferOut = mBufferOut;
    ALsizei j, k, base;

    for(base = 0;base < samplesToDo;)
    {
        const ALsizei todo{mini(HIL_SIZE-mCount, samplesToDo-base)};

        ASSUME(todo > 0);

        /* Fill FIFO buffer with samples data */
        k = mCount;
        for(j = 0;j < todo;j++,k++)
        {
            mInFIFO[k] = samplesIn[0][base+j];
            mOutdata[base+j]  = mOutFIFO[k-FIFO_LATENCY];
        }
        mCount += todo;
        base += todo;

        /* Check whether FIFO buffer is filled */
        if(mCount < HIL_SIZE) continue;
        mCount = FIFO_LATENCY;

        /* Real signal windowing and store in Analytic buffer */
        for(k = 0;k < HIL_SIZE;k++)
        {
            mAnalytic[k].real(mInFIFO[k] * HannWindow[k]);
            mAnalytic[k].imag(0.0);
        }

        /* Processing signal by Discrete Hilbert Transform (analytical signal). */
        complex_hilbert(mAnalytic);

        /* Windowing and add to output accumulator */
        for(k = 0;k < HIL_SIZE;k++)
            mOutputAccum[k] += 2.0/OVERSAMP*HannWindow[k]*mAnalytic[k];

        /* Shift accumulator, input & output FIFO */
        for(k = 0;k < HIL_STEP;k++) mOutFIFO[k] = mOutputAccum[k];
        for(j = 0;k < HIL_SIZE;k++,j++) mOutputAccum[j] = mOutputAccum[k];
        for(;j < HIL_SIZE;j++) mOutputAccum[j] = complex_zero;
        for(k = 0;k < FIFO_LATENCY;k++)
            mInFIFO[k] = mInFIFO[k+HIL_STEP];
    }

    /* Process frequency shifter using the analytic signal obtained. */
    for(k = 0;k < samplesToDo;k++)
    {
        double phase = mPhase * ((1.0/FRACTIONONE) * al::MathDefs<double>::Tau());
        BufferOut[k] = static_cast<float>(mOutdata[k].real()*std::cos(phase) +
            mOutdata[k].imag()*std::sin(phase)*mLdSign);

        mPhase += mPhaseStep;
        mPhase &= FRACTIONMASK;
    }

    /* Now, mix the processed sound data to the output. */
    MixSamples(BufferOut, samplesOut, mCurrentGains, mTargetGains, maxi(samplesToDo, 512), 0,
        samplesToDo);
}


void Fshifter_setParamf(EffectProps *props, ALCcontext *context, ALenum param, ALfloat val)
{
    switch(param)
    {
        case AL_FREQUENCY_SHIFTER_FREQUENCY:
            if(!(val >= AL_FREQUENCY_SHIFTER_MIN_FREQUENCY && val <= AL_FREQUENCY_SHIFTER_MAX_FREQUENCY))
                SETERR_RETURN(context, AL_INVALID_VALUE,,"Frequency shifter frequency out of range");
            props->Fshifter.Frequency = val;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid frequency shifter float property 0x%04x", param);
    }
}
void Fshifter_setParamfv(EffectProps *props, ALCcontext *context, ALenum param, const ALfloat *vals)
{ Fshifter_setParamf(props, context, param, vals[0]); }

void Fshifter_setParami(EffectProps *props, ALCcontext *context, ALenum param, ALint val)
{
    switch(param)
    {
        case AL_FREQUENCY_SHIFTER_LEFT_DIRECTION:
            if(!(val >= AL_FREQUENCY_SHIFTER_MIN_LEFT_DIRECTION && val <= AL_FREQUENCY_SHIFTER_MAX_LEFT_DIRECTION))
                SETERR_RETURN(context, AL_INVALID_VALUE,,"Frequency shifter left direction out of range");
            props->Fshifter.LeftDirection = val;
            break;

        case AL_FREQUENCY_SHIFTER_RIGHT_DIRECTION:
            if(!(val >= AL_FREQUENCY_SHIFTER_MIN_RIGHT_DIRECTION && val <= AL_FREQUENCY_SHIFTER_MAX_RIGHT_DIRECTION))
                SETERR_RETURN(context, AL_INVALID_VALUE,,"Frequency shifter right direction out of range");
            props->Fshifter.RightDirection = val;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid frequency shifter integer property 0x%04x", param);
    }
}
void Fshifter_setParamiv(EffectProps *props, ALCcontext *context, ALenum param, const ALint *vals)
{ Fshifter_setParami(props, context, param, vals[0]); }

void Fshifter_getParami(const EffectProps *props, ALCcontext *context, ALenum param, ALint *val)
{
    switch(param)
    {
        case AL_FREQUENCY_SHIFTER_LEFT_DIRECTION:
            *val = props->Fshifter.LeftDirection;
            break;
        case AL_FREQUENCY_SHIFTER_RIGHT_DIRECTION:
            *val = props->Fshifter.RightDirection;
            break;
        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid frequency shifter integer property 0x%04x", param);
    }
}
void Fshifter_getParamiv(const EffectProps *props, ALCcontext *context, ALenum param, ALint *vals)
{ Fshifter_getParami(props, context, param, vals); }

void Fshifter_getParamf(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *val)
{
    switch(param)
    {
        case AL_FREQUENCY_SHIFTER_FREQUENCY:
            *val = props->Fshifter.Frequency;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid frequency shifter float property 0x%04x", param);
    }
}
void Fshifter_getParamfv(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *vals)
{ Fshifter_getParamf(props, context, param, vals); }

DEFINE_ALEFFECT_VTABLE(Fshifter);


struct FshifterStateFactory final : public EffectStateFactory {
    EffectState *create() override { return new FshifterState{}; }
    EffectProps getDefaultProps() const noexcept override;
    const EffectVtable *getEffectVtable() const noexcept override { return &Fshifter_vtable; }
};

EffectProps FshifterStateFactory::getDefaultProps() const noexcept
{
    EffectProps props{};
    props.Fshifter.Frequency      = AL_FREQUENCY_SHIFTER_DEFAULT_FREQUENCY;
    props.Fshifter.LeftDirection  = AL_FREQUENCY_SHIFTER_DEFAULT_LEFT_DIRECTION;
    props.Fshifter.RightDirection = AL_FREQUENCY_SHIFTER_DEFAULT_RIGHT_DIRECTION;
    return props;
}

} // namespace

EffectStateFactory *FshifterStateFactory_getFactory()
{
    static FshifterStateFactory FshifterFactory{};
    return &FshifterFactory;
}
