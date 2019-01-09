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

#include <cstdlib>

#include <cmath>
#include <algorithm>

#include "alMain.h"
#include "alcontext.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
#include "alu.h"
#include "filters/biquad.h"
#include "vector.h"


namespace {

static_assert(AL_CHORUS_WAVEFORM_SINUSOID == AL_FLANGER_WAVEFORM_SINUSOID, "Chorus/Flanger waveform value mismatch");
static_assert(AL_CHORUS_WAVEFORM_TRIANGLE == AL_FLANGER_WAVEFORM_TRIANGLE, "Chorus/Flanger waveform value mismatch");

enum class WaveForm {
    Sinusoid,
    Triangle
};

void GetTriangleDelays(ALint *delays, ALsizei offset, ALsizei lfo_range, ALfloat lfo_scale,
                       ALfloat depth, ALsizei delay, ALsizei todo)
{
    std::generate_n<ALint*RESTRICT>(delays, todo,
        [&offset,lfo_range,lfo_scale,depth,delay]() -> ALint
        {
            offset = (offset+1)%lfo_range;
            return fastf2i((1.0f - std::abs(2.0f - lfo_scale*offset)) * depth) + delay;
        }
    );
}

void GetSinusoidDelays(ALint *delays, ALsizei offset, ALsizei lfo_range, ALfloat lfo_scale,
                       ALfloat depth, ALsizei delay, ALsizei todo)
{
    std::generate_n<ALint*RESTRICT>(delays, todo,
        [&offset,lfo_range,lfo_scale,depth,delay]() -> ALint
        {
            offset = (offset+1)%lfo_range;
            return fastf2i(std::sin(lfo_scale*offset) * depth) + delay;
        }
    );
}

struct ChorusState final : public EffectState {
    al::vector<ALfloat,16> mSampleBuffer;
    ALsizei mOffset{0};

    ALsizei mLfoOffset{0};
    ALsizei mLfoRange{1};
    ALfloat mLfoScale{0.0f};
    ALint mLfoDisp{0};

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
    void update(const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props, const EffectTarget target) override;
    void process(ALsizei samplesToDo, const ALfloat (*RESTRICT samplesIn)[BUFFERSIZE], ALfloat (*RESTRICT samplesOut)[BUFFERSIZE], ALsizei numChannels) override;

    DEF_NEWDEL(ChorusState)
};

ALboolean ChorusState::deviceUpdate(const ALCdevice *Device)
{
    const ALfloat max_delay = maxf(AL_CHORUS_MAX_DELAY, AL_FLANGER_MAX_DELAY);
    size_t maxlen;

    maxlen = NextPowerOf2(float2int(max_delay*2.0f*Device->Frequency) + 1u);
    if(maxlen <= 0) return AL_FALSE;

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

void ChorusState::update(const ALCcontext *Context, const ALeffectslot *Slot, const ALeffectProps *props, const EffectTarget target)
{
    static constexpr ALsizei mindelay = MAX_RESAMPLE_PADDING << FRACTIONBITS;

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
    const ALCdevice *device{Context->Device};
    auto frequency = static_cast<ALfloat>(device->Frequency);
    mDelay = maxi(float2int(props->Chorus.Delay*frequency*FRACTIONONE + 0.5f), mindelay);
    mDepth = minf(props->Chorus.Depth * mDelay, static_cast<ALfloat>(mDelay - mindelay));

    mFeedback = props->Chorus.Feedback;

    /* Gains for left and right sides */
    ALfloat coeffs[2][MAX_AMBI_COEFFS];
    CalcAngleCoeffs(al::MathDefs<float>::Pi()*-0.5f, 0.0f, 0.0f, coeffs[0]);
    CalcAngleCoeffs(al::MathDefs<float>::Pi()* 0.5f, 0.0f, 0.0f, coeffs[1]);

    mOutBuffer = target.Main->Buffer;
    mOutChannels = target.Main->NumChannels;
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
        ALsizei lfo_range = float2int(minf(frequency/rate + 0.5f, static_cast<ALfloat>(INT_MAX/360 - 180)));

        mLfoOffset = float2int(static_cast<ALfloat>(mLfoOffset)/mLfoRange*lfo_range + 0.5f) % lfo_range;
        mLfoRange = lfo_range;
        switch(mWaveform)
        {
            case WaveForm::Triangle:
                mLfoScale = 4.0f / mLfoRange;
                break;
            case WaveForm::Sinusoid:
                mLfoScale = al::MathDefs<float>::Tau() / mLfoRange;
                break;
        }

        /* Calculate lfo phase displacement */
        ALint phase{props->Chorus.Phase};
        if(phase < 0) phase = 360 + phase;
        mLfoDisp = (mLfoRange*phase + 180) / 360;
    }
}

void ChorusState::process(ALsizei SamplesToDo, const ALfloat (*RESTRICT SamplesIn)[BUFFERSIZE], ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE], ALsizei NumChannels)
{
    const auto bufmask = static_cast<ALsizei>(mSampleBuffer.size()-1);
    const ALfloat feedback{mFeedback};
    const ALsizei avgdelay{(mDelay + (FRACTIONONE>>1)) >> FRACTIONBITS};
    ALfloat *RESTRICT delaybuf{mSampleBuffer.data()};
    ALsizei offset{mOffset};
    ALsizei i, c;
    ALsizei base;

    for(base = 0;base < SamplesToDo;)
    {
        const ALsizei todo = mini(256, SamplesToDo-base);
        ALint moddelays[2][256];
        alignas(16) ALfloat temps[2][256];

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
        mLfoOffset = (mLfoOffset+todo) % mLfoRange;

        for(i = 0;i < todo;i++)
        {
            // Feed the buffer's input first (necessary for delays < 1).
            delaybuf[offset&bufmask] = SamplesIn[0][base+i];

            // Tap for the left output.
            ALint delay{offset - (moddelays[0][i]>>FRACTIONBITS)};
            ALfloat mu{(moddelays[0][i]&FRACTIONMASK) * (1.0f/FRACTIONONE)};
            temps[0][i] = cubic(delaybuf[(delay+1) & bufmask], delaybuf[(delay  ) & bufmask],
                                delaybuf[(delay-1) & bufmask], delaybuf[(delay-2) & bufmask],
                                mu);

            // Tap for the right output.
            delay = offset - (moddelays[1][i]>>FRACTIONBITS);
            mu = (moddelays[1][i]&FRACTIONMASK) * (1.0f/FRACTIONONE);
            temps[1][i] = cubic(delaybuf[(delay+1) & bufmask], delaybuf[(delay  ) & bufmask],
                                delaybuf[(delay-1) & bufmask], delaybuf[(delay-2) & bufmask],
                                mu);

            // Accumulate feedback from the average delay of the taps.
            delaybuf[offset&bufmask] += delaybuf[(offset-avgdelay) & bufmask] * feedback;
            offset++;
        }

        for(c = 0;c < 2;c++)
            MixSamples(temps[c], NumChannels, SamplesOut, mGains[c].Current,
                       mGains[c].Target, SamplesToDo-base, base, todo);

        base += todo;
    }

    mOffset = offset;
}


struct ChorusStateFactory final : public EffectStateFactory {
    EffectState *create() override;
};

EffectState *ChorusStateFactory::create()
{ return new ChorusState{}; }

} // namespace

EffectStateFactory *ChorusStateFactory_getFactory()
{
    static ChorusStateFactory ChorusFactory{};
    return &ChorusFactory;
}


void ALchorus_setParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val)
{
    ALeffectProps *props = &effect->Props;
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
            alSetError(context, AL_INVALID_ENUM, "Invalid chorus integer property 0x%04x", param);
    }
}
void ALchorus_setParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{ ALchorus_setParami(effect, context, param, vals[0]); }
void ALchorus_setParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    ALeffectProps *props = &effect->Props;
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
            alSetError(context, AL_INVALID_ENUM, "Invalid chorus float property 0x%04x", param);
    }
}
void ALchorus_setParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{ ALchorus_setParamf(effect, context, param, vals[0]); }

void ALchorus_getParami(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *val)
{
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_CHORUS_WAVEFORM:
            *val = props->Chorus.Waveform;
            break;

        case AL_CHORUS_PHASE:
            *val = props->Chorus.Phase;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid chorus integer property 0x%04x", param);
    }
}
void ALchorus_getParamiv(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{ ALchorus_getParami(effect, context, param, vals); }
void ALchorus_getParamf(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val)
{
    const ALeffectProps *props = &effect->Props;
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
            alSetError(context, AL_INVALID_ENUM, "Invalid chorus float property 0x%04x", param);
    }
}
void ALchorus_getParamfv(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{ ALchorus_getParamf(effect, context, param, vals); }

DEFINE_ALEFFECT_VTABLE(ALchorus);


/* Flanger is basically a chorus with a really short delay. They can both use
 * the same processing functions, so piggyback flanger on the chorus functions.
 */
EffectStateFactory *FlangerStateFactory_getFactory()
{ return ChorusStateFactory_getFactory(); }


void ALflanger_setParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val)
{
    ALeffectProps *props = &effect->Props;
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
            alSetError(context, AL_INVALID_ENUM, "Invalid flanger integer property 0x%04x", param);
    }
}
void ALflanger_setParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{ ALflanger_setParami(effect, context, param, vals[0]); }
void ALflanger_setParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    ALeffectProps *props = &effect->Props;
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
            alSetError(context, AL_INVALID_ENUM, "Invalid flanger float property 0x%04x", param);
    }
}
void ALflanger_setParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{ ALflanger_setParamf(effect, context, param, vals[0]); }

void ALflanger_getParami(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *val)
{
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_FLANGER_WAVEFORM:
            *val = props->Chorus.Waveform;
            break;

        case AL_FLANGER_PHASE:
            *val = props->Chorus.Phase;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid flanger integer property 0x%04x", param);
    }
}
void ALflanger_getParamiv(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{ ALflanger_getParami(effect, context, param, vals); }
void ALflanger_getParamf(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val)
{
    const ALeffectProps *props = &effect->Props;
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
            alSetError(context, AL_INVALID_ENUM, "Invalid flanger float property 0x%04x", param);
    }
}
void ALflanger_getParamfv(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{ ALflanger_getParamf(effect, context, param, vals); }

DEFINE_ALEFFECT_VTABLE(ALflanger);
