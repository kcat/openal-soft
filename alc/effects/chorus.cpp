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

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <iterator>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/efx.h"

#include "al/auxeffectslot.h"
#include "alcmain.h"
#include "alcontext.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "alspan.h"
#include "alu.h"
#include "ambidefs.h"
#include "effects/base.h"
#include "math_defs.h"
#include "opthelpers.h"
#include "vector.h"


namespace {

static_assert(AL_CHORUS_WAVEFORM_SINUSOID == AL_FLANGER_WAVEFORM_SINUSOID, "Chorus/Flanger waveform value mismatch");
static_assert(AL_CHORUS_WAVEFORM_TRIANGLE == AL_FLANGER_WAVEFORM_TRIANGLE, "Chorus/Flanger waveform value mismatch");

enum class WaveForm {
    Sinusoid,
    Triangle
};

void GetTriangleDelays(ALuint *delays, const ALuint start_offset, const ALuint lfo_range,
    const ALfloat lfo_scale, const ALfloat depth, const ALsizei delay, const size_t todo)
{
    ASSUME(lfo_range > 0);
    ASSUME(todo > 0);

    ALuint offset{start_offset};
    auto gen_lfo = [&offset,lfo_range,lfo_scale,depth,delay]() -> ALuint
    {
        offset = (offset+1)%lfo_range;
        const float offset_norm{static_cast<float>(offset) * lfo_scale};
        return static_cast<ALuint>(fastf2i((1.0f-std::abs(2.0f-offset_norm)) * depth) + delay);
    };
    std::generate_n(delays, todo, gen_lfo);
}

void GetSinusoidDelays(ALuint *delays, const ALuint start_offset, const ALuint lfo_range,
    const ALfloat lfo_scale, const ALfloat depth, const ALsizei delay, const size_t todo)
{
    ASSUME(lfo_range > 0);
    ASSUME(todo > 0);

    ALuint offset{start_offset};
    auto gen_lfo = [&offset,lfo_range,lfo_scale,depth,delay]() -> ALuint
    {
        offset = (offset+1)%lfo_range;
        const float offset_norm{static_cast<float>(offset) * lfo_scale};
        return static_cast<ALuint>(fastf2i(std::sin(offset_norm)*depth) + delay);
    };
    std::generate_n(delays, todo, gen_lfo);
}

struct ChorusState final : public EffectState {
    al::vector<ALfloat,16> mSampleBuffer;
    ALuint mOffset{0};

    ALuint mLfoOffset{0};
    ALuint mLfoRange{1};
    ALfloat mLfoScale{0.0f};
    ALuint mLfoDisp{0};

    /* Gains for left and right sides */
    struct {
        ALfloat Current[MAX_OUTPUT_CHANNELS]{};
        ALfloat Target[MAX_OUTPUT_CHANNELS]{};
    } mGains[2];

    /* effect parameters */
    WaveForm mWaveform{};
    ALint mDelay{0};
    ALfloat mDepth{0.0f};
    ALfloat mFeedback{0.0f};


    ALboolean deviceUpdate(const ALCdevice *device) override;
    void update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target) override;
    void process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut) override;

    DEF_NEWDEL(ChorusState)
};

ALboolean ChorusState::deviceUpdate(const ALCdevice *Device)
{
    constexpr ALfloat max_delay{maxf(AL_CHORUS_MAX_DELAY, AL_FLANGER_MAX_DELAY)};

    const auto frequency = static_cast<float>(Device->Frequency);
    const size_t maxlen{NextPowerOf2(float2uint(max_delay*2.0f*frequency) + 1u)};
    if(maxlen != mSampleBuffer.size())
    {
        mSampleBuffer.resize(maxlen);
        mSampleBuffer.shrink_to_fit();
    }

    std::fill(mSampleBuffer.begin(), mSampleBuffer.end(), 0.0f);
    for(auto &e : mGains)
    {
        std::fill(std::begin(e.Current), std::end(e.Current), 0.0f);
        std::fill(std::begin(e.Target), std::end(e.Target), 0.0f);
    }

    return AL_TRUE;
}

void ChorusState::update(const ALCcontext *Context, const ALeffectslot *Slot, const EffectProps *props, const EffectTarget target)
{
    constexpr ALsizei mindelay{(MAX_RESAMPLER_PADDING>>1) << FRACTIONBITS};

    switch(props->Chorus.Waveform)
    {
        case AL_CHORUS_WAVEFORM_TRIANGLE:
            mWaveform = WaveForm::Triangle;
            break;
        case AL_CHORUS_WAVEFORM_SINUSOID:
            mWaveform = WaveForm::Sinusoid;
            break;
    }

    /* The LFO depth is scaled to be relative to the sample delay. Clamp the
     * delay and depth to allow enough padding for resampling.
     */
    const ALCdevice *device{Context->mDevice.get()};
    const auto frequency = static_cast<float>(device->Frequency);

    mDelay = maxi(float2int(props->Chorus.Delay*frequency*FRACTIONONE + 0.5f), mindelay);
    mDepth = minf(props->Chorus.Depth * static_cast<float>(mDelay),
        static_cast<float>(mDelay - mindelay));

    mFeedback = props->Chorus.Feedback;

    /* Gains for left and right sides */
    ALfloat coeffs[2][MAX_AMBI_CHANNELS];
    CalcDirectionCoeffs({-1.0f, 0.0f, 0.0f}, 0.0f, coeffs[0]);
    CalcDirectionCoeffs({ 1.0f, 0.0f, 0.0f}, 0.0f, coeffs[1]);

    mOutTarget = target.Main->Buffer;
    ComputePanGains(target.Main, coeffs[0], Slot->Params.Gain, mGains[0].Target);
    ComputePanGains(target.Main, coeffs[1], Slot->Params.Gain, mGains[1].Target);

    ALfloat rate{props->Chorus.Rate};
    if(!(rate > 0.0f))
    {
        mLfoOffset = 0;
        mLfoRange = 1;
        mLfoScale = 0.0f;
        mLfoDisp = 0;
    }
    else
    {
        /* Calculate LFO coefficient (number of samples per cycle). Limit the
         * max range to avoid overflow when calculating the displacement.
         */
        ALuint lfo_range{float2uint(minf(frequency/rate + 0.5f, ALfloat{INT_MAX/360 - 180}))};

        mLfoOffset = mLfoOffset * lfo_range / mLfoRange;
        mLfoRange = lfo_range;
        switch(mWaveform)
        {
            case WaveForm::Triangle:
                mLfoScale = 4.0f / static_cast<float>(mLfoRange);
                break;
            case WaveForm::Sinusoid:
                mLfoScale = al::MathDefs<float>::Tau() / static_cast<float>(mLfoRange);
                break;
        }

        /* Calculate lfo phase displacement */
        ALint phase{props->Chorus.Phase};
        if(phase < 0) phase = 360 + phase;
        mLfoDisp = (mLfoRange*static_cast<ALuint>(phase) + 180) / 360;
    }
}

void ChorusState::process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut)
{
    const size_t bufmask{mSampleBuffer.size()-1};
    const ALfloat feedback{mFeedback};
    const ALuint avgdelay{(static_cast<ALuint>(mDelay) + (FRACTIONONE>>1)) >> FRACTIONBITS};
    ALfloat *RESTRICT delaybuf{mSampleBuffer.data()};
    ALuint offset{mOffset};

    for(size_t base{0u};base < samplesToDo;)
    {
        const size_t todo{minz(256, samplesToDo-base)};

        ALuint moddelays[2][256];
        if(mWaveform == WaveForm::Sinusoid)
        {
            GetSinusoidDelays(moddelays[0], mLfoOffset, mLfoRange, mLfoScale, mDepth, mDelay,
                              todo);
            GetSinusoidDelays(moddelays[1], (mLfoOffset+mLfoDisp)%mLfoRange, mLfoRange, mLfoScale,
                              mDepth, mDelay, todo);
        }
        else /*if(mWaveform == WaveForm::Triangle)*/
        {
            GetTriangleDelays(moddelays[0], mLfoOffset, mLfoRange, mLfoScale, mDepth, mDelay,
                              todo);
            GetTriangleDelays(moddelays[1], (mLfoOffset+mLfoDisp)%mLfoRange, mLfoRange, mLfoScale,
                              mDepth, mDelay, todo);
        }
        mLfoOffset = (mLfoOffset+static_cast<ALuint>(todo)) % mLfoRange;

        alignas(16) ALfloat temps[2][256];
        for(size_t i{0u};i < todo;i++)
        {
            // Feed the buffer's input first (necessary for delays < 1).
            delaybuf[offset&bufmask] = samplesIn[0][base+i];

            // Tap for the left output.
            ALuint delay{offset - (moddelays[0][i]>>FRACTIONBITS)};
            ALfloat mu{static_cast<float>(moddelays[0][i]&FRACTIONMASK) * (1.0f/FRACTIONONE)};
            temps[0][i] = cubic(delaybuf[(delay+1) & bufmask], delaybuf[(delay  ) & bufmask],
                delaybuf[(delay-1) & bufmask], delaybuf[(delay-2) & bufmask], mu);

            // Tap for the right output.
            delay = offset - (moddelays[1][i]>>FRACTIONBITS);
            mu = static_cast<float>(moddelays[1][i]&FRACTIONMASK) * (1.0f/FRACTIONONE);
            temps[1][i] = cubic(delaybuf[(delay+1) & bufmask], delaybuf[(delay  ) & bufmask],
                delaybuf[(delay-1) & bufmask], delaybuf[(delay-2) & bufmask], mu);

            // Accumulate feedback from the average delay of the taps.
            delaybuf[offset&bufmask] += delaybuf[(offset-avgdelay) & bufmask] * feedback;
            ++offset;
        }

        for(ALsizei c{0};c < 2;c++)
            MixSamples({temps[c], todo}, samplesOut, mGains[c].Current, mGains[c].Target,
                samplesToDo-base, base);

        base += todo;
    }

    mOffset = offset;
}


void Chorus_setParami(EffectProps *props, ALCcontext *context, ALenum param, ALint val)
{
    switch(param)
    {
        case AL_CHORUS_WAVEFORM:
            if(!(val >= AL_CHORUS_MIN_WAVEFORM && val <= AL_CHORUS_MAX_WAVEFORM))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Invalid chorus waveform");
            props->Chorus.Waveform = val;
            break;

        case AL_CHORUS_PHASE:
            if(!(val >= AL_CHORUS_MIN_PHASE && val <= AL_CHORUS_MAX_PHASE))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Chorus phase out of range");
            props->Chorus.Phase = val;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid chorus integer property 0x%04x", param);
    }
}
void Chorus_setParamiv(EffectProps *props, ALCcontext *context, ALenum param, const ALint *vals)
{ Chorus_setParami(props, context, param, vals[0]); }
void Chorus_setParamf(EffectProps *props, ALCcontext *context, ALenum param, ALfloat val)
{
    switch(param)
    {
        case AL_CHORUS_RATE:
            if(!(val >= AL_CHORUS_MIN_RATE && val <= AL_CHORUS_MAX_RATE))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Chorus rate out of range");
            props->Chorus.Rate = val;
            break;

        case AL_CHORUS_DEPTH:
            if(!(val >= AL_CHORUS_MIN_DEPTH && val <= AL_CHORUS_MAX_DEPTH))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Chorus depth out of range");
            props->Chorus.Depth = val;
            break;

        case AL_CHORUS_FEEDBACK:
            if(!(val >= AL_CHORUS_MIN_FEEDBACK && val <= AL_CHORUS_MAX_FEEDBACK))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Chorus feedback out of range");
            props->Chorus.Feedback = val;
            break;

        case AL_CHORUS_DELAY:
            if(!(val >= AL_CHORUS_MIN_DELAY && val <= AL_CHORUS_MAX_DELAY))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Chorus delay out of range");
            props->Chorus.Delay = val;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid chorus float property 0x%04x", param);
    }
}
void Chorus_setParamfv(EffectProps *props, ALCcontext *context, ALenum param, const ALfloat *vals)
{ Chorus_setParamf(props, context, param, vals[0]); }

void Chorus_getParami(const EffectProps *props, ALCcontext *context, ALenum param, ALint *val)
{
    switch(param)
    {
        case AL_CHORUS_WAVEFORM:
            *val = props->Chorus.Waveform;
            break;

        case AL_CHORUS_PHASE:
            *val = props->Chorus.Phase;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid chorus integer property 0x%04x", param);
    }
}
void Chorus_getParamiv(const EffectProps *props, ALCcontext *context, ALenum param, ALint *vals)
{ Chorus_getParami(props, context, param, vals); }
void Chorus_getParamf(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *val)
{
    switch(param)
    {
        case AL_CHORUS_RATE:
            *val = props->Chorus.Rate;
            break;

        case AL_CHORUS_DEPTH:
            *val = props->Chorus.Depth;
            break;

        case AL_CHORUS_FEEDBACK:
            *val = props->Chorus.Feedback;
            break;

        case AL_CHORUS_DELAY:
            *val = props->Chorus.Delay;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid chorus float property 0x%04x", param);
    }
}
void Chorus_getParamfv(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *vals)
{ Chorus_getParamf(props, context, param, vals); }

DEFINE_ALEFFECT_VTABLE(Chorus);


struct ChorusStateFactory final : public EffectStateFactory {
    EffectState *create() override { return new ChorusState{}; }
    EffectProps getDefaultProps() const noexcept override;
    const EffectVtable *getEffectVtable() const noexcept override { return &Chorus_vtable; }
};

EffectProps ChorusStateFactory::getDefaultProps() const noexcept
{
    EffectProps props{};
    props.Chorus.Waveform = AL_CHORUS_DEFAULT_WAVEFORM;
    props.Chorus.Phase = AL_CHORUS_DEFAULT_PHASE;
    props.Chorus.Rate = AL_CHORUS_DEFAULT_RATE;
    props.Chorus.Depth = AL_CHORUS_DEFAULT_DEPTH;
    props.Chorus.Feedback = AL_CHORUS_DEFAULT_FEEDBACK;
    props.Chorus.Delay = AL_CHORUS_DEFAULT_DELAY;
    return props;
}


void Flanger_setParami(EffectProps *props, ALCcontext *context, ALenum param, ALint val)
{
    switch(param)
    {
        case AL_FLANGER_WAVEFORM:
            if(!(val >= AL_FLANGER_MIN_WAVEFORM && val <= AL_FLANGER_MAX_WAVEFORM))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Invalid flanger waveform");
            props->Chorus.Waveform = val;
            break;

        case AL_FLANGER_PHASE:
            if(!(val >= AL_FLANGER_MIN_PHASE && val <= AL_FLANGER_MAX_PHASE))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Flanger phase out of range");
            props->Chorus.Phase = val;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid flanger integer property 0x%04x", param);
    }
}
void Flanger_setParamiv(EffectProps *props, ALCcontext *context, ALenum param, const ALint *vals)
{ Flanger_setParami(props, context, param, vals[0]); }
void Flanger_setParamf(EffectProps *props, ALCcontext *context, ALenum param, ALfloat val)
{
    switch(param)
    {
        case AL_FLANGER_RATE:
            if(!(val >= AL_FLANGER_MIN_RATE && val <= AL_FLANGER_MAX_RATE))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Flanger rate out of range");
            props->Chorus.Rate = val;
            break;

        case AL_FLANGER_DEPTH:
            if(!(val >= AL_FLANGER_MIN_DEPTH && val <= AL_FLANGER_MAX_DEPTH))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Flanger depth out of range");
            props->Chorus.Depth = val;
            break;

        case AL_FLANGER_FEEDBACK:
            if(!(val >= AL_FLANGER_MIN_FEEDBACK && val <= AL_FLANGER_MAX_FEEDBACK))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Flanger feedback out of range");
            props->Chorus.Feedback = val;
            break;

        case AL_FLANGER_DELAY:
            if(!(val >= AL_FLANGER_MIN_DELAY && val <= AL_FLANGER_MAX_DELAY))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Flanger delay out of range");
            props->Chorus.Delay = val;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid flanger float property 0x%04x", param);
    }
}
void Flanger_setParamfv(EffectProps *props, ALCcontext *context, ALenum param, const ALfloat *vals)
{ Flanger_setParamf(props, context, param, vals[0]); }

void Flanger_getParami(const EffectProps *props, ALCcontext *context, ALenum param, ALint *val)
{
    switch(param)
    {
        case AL_FLANGER_WAVEFORM:
            *val = props->Chorus.Waveform;
            break;

        case AL_FLANGER_PHASE:
            *val = props->Chorus.Phase;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid flanger integer property 0x%04x", param);
    }
}
void Flanger_getParamiv(const EffectProps *props, ALCcontext *context, ALenum param, ALint *vals)
{ Flanger_getParami(props, context, param, vals); }
void Flanger_getParamf(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *val)
{
    switch(param)
    {
        case AL_FLANGER_RATE:
            *val = props->Chorus.Rate;
            break;

        case AL_FLANGER_DEPTH:
            *val = props->Chorus.Depth;
            break;

        case AL_FLANGER_FEEDBACK:
            *val = props->Chorus.Feedback;
            break;

        case AL_FLANGER_DELAY:
            *val = props->Chorus.Delay;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid flanger float property 0x%04x", param);
    }
}
void Flanger_getParamfv(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *vals)
{ Flanger_getParamf(props, context, param, vals); }

DEFINE_ALEFFECT_VTABLE(Flanger);


/* Flanger is basically a chorus with a really short delay. They can both use
 * the same processing functions, so piggyback flanger on the chorus functions.
 */
struct FlangerStateFactory final : public EffectStateFactory {
    EffectState *create() override { return new ChorusState{}; }
    EffectProps getDefaultProps() const noexcept override;
    const EffectVtable *getEffectVtable() const noexcept override { return &Flanger_vtable; }
};

EffectProps FlangerStateFactory::getDefaultProps() const noexcept
{
    EffectProps props{};
    props.Chorus.Waveform = AL_FLANGER_DEFAULT_WAVEFORM;
    props.Chorus.Phase = AL_FLANGER_DEFAULT_PHASE;
    props.Chorus.Rate = AL_FLANGER_DEFAULT_RATE;
    props.Chorus.Depth = AL_FLANGER_DEFAULT_DEPTH;
    props.Chorus.Feedback = AL_FLANGER_DEFAULT_FEEDBACK;
    props.Chorus.Delay = AL_FLANGER_DEFAULT_DELAY;
    return props;
}

} // namespace

EffectStateFactory *ChorusStateFactory_getFactory()
{
    static ChorusStateFactory ChorusFactory{};
    return &ChorusFactory;
}

EffectStateFactory *FlangerStateFactory_getFactory()
{
    static FlangerStateFactory FlangerFactory{};
    return &FlangerFactory;
}
