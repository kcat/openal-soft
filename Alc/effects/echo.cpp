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

#include <algorithm>

#include "alMain.h"
#include "alcontext.h"
#include "alFilter.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
#include "alu.h"
#include "filters/biquad.h"
#include "vector.h"


namespace {

struct EchoState final : public EffectState {
    al::vector<ALfloat,16> mSampleBuffer;

    // The echo is two tap. The delay is the number of samples from before the
    // current offset
    struct {
        ALsizei delay{0};
    } mTap[2];
    ALsizei mOffset{0};

    /* The panning gains for the two taps */
    struct {
        ALfloat Current[MAX_OUTPUT_CHANNELS]{};
        ALfloat Target[MAX_OUTPUT_CHANNELS]{};
    } mGains[2];

    BiquadFilter mFilter;
    ALfloat mFeedGain{0.0f};

    alignas(16) ALfloat mTempBuffer[2][BUFFERSIZE];

    ALboolean deviceUpdate(const ALCdevice *device) override;
    void update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target) override;
    void process(const ALsizei samplesToDo, const FloatBufferLine *RESTRICT samplesIn, const ALsizei numInput, const al::span<FloatBufferLine> samplesOut) override;

    DEF_NEWDEL(EchoState)
};

ALboolean EchoState::deviceUpdate(const ALCdevice *Device)
{
    ALuint maxlen;

    // Use the next power of 2 for the buffer length, so the tap offsets can be
    // wrapped using a mask instead of a modulo
    maxlen = float2int(AL_ECHO_MAX_DELAY*Device->Frequency + 0.5f) +
             float2int(AL_ECHO_MAX_LRDELAY*Device->Frequency + 0.5f);
    maxlen = NextPowerOf2(maxlen);
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

void EchoState::update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target)
{
    const ALCdevice *device = context->Device;
    const auto frequency = static_cast<ALfloat>(device->Frequency);

    mTap[0].delay = maxi(float2int(props->Echo.Delay*frequency + 0.5f), 1);
    mTap[1].delay = float2int(props->Echo.LRDelay*frequency + 0.5f) + mTap[0].delay;

    const ALfloat gainhf{maxf(1.0f - props->Echo.Damping, 0.0625f)}; /* Limit -24dB */
    mFilter.setParams(BiquadType::HighShelf, gainhf, LOWPASSFREQREF/frequency,
        mFilter.rcpQFromSlope(gainhf, 1.0f));

    mFeedGain = props->Echo.Feedback;

    /* Convert echo spread (where 0 = center, +/-1 = sides) to angle. */
    const ALfloat angle{std::asin(props->Echo.Spread)};

    ALfloat coeffs[2][MAX_AMBI_CHANNELS];
    CalcAngleCoeffs(-angle, 0.0f, 0.0f, coeffs[0]);
    CalcAngleCoeffs( angle, 0.0f, 0.0f, coeffs[1]);

    mOutTarget = target.Main->Buffer;
    ComputePanGains(target.Main, coeffs[0], slot->Params.Gain, mGains[0].Target);
    ComputePanGains(target.Main, coeffs[1], slot->Params.Gain, mGains[1].Target);
}

void EchoState::process(const ALsizei samplesToDo, const FloatBufferLine *RESTRICT samplesIn, const ALsizei /*numInput*/, const al::span<FloatBufferLine> samplesOut)
{
    const auto mask = static_cast<ALsizei>(mSampleBuffer.size()-1);
    ALfloat *RESTRICT delaybuf{mSampleBuffer.data()};
    ALsizei offset{mOffset};
    ALsizei tap1{offset - mTap[0].delay};
    ALsizei tap2{offset - mTap[1].delay};
    ALfloat z1, z2;

    ASSUME(samplesToDo > 0);
    ASSUME(mask > 0);

    std::tie(z1, z2) = mFilter.getComponents();
    for(ALsizei i{0};i < samplesToDo;)
    {
        offset &= mask;
        tap1 &= mask;
        tap2 &= mask;

        ALsizei td{mini(mask+1 - maxi(offset, maxi(tap1, tap2)), samplesToDo-i)};
        do {
            /* Feed the delay buffer's input first. */
            delaybuf[offset] = samplesIn[0][i];

            /* Get delayed output from the first and second taps. Use the
             * second tap for feedback.
             */
            mTempBuffer[0][i] = delaybuf[tap1++];
            mTempBuffer[1][i] = delaybuf[tap2++];
            const float feedb{mTempBuffer[1][i++]};

            /* Add feedback to the delay buffer with damping and attenuation. */
            delaybuf[offset++] += mFilter.processOne(feedb, z1, z2) * mFeedGain;
        } while(--td);
    }
    mFilter.setComponents(z1, z2);
    mOffset = offset;

    for(ALsizei c{0};c < 2;c++)
        MixSamples(mTempBuffer[c], samplesOut, mGains[c].Current, mGains[c].Target, samplesToDo, 0,
            samplesToDo);
}


void Echo_setParami(EffectProps*, ALCcontext *context, ALenum param, ALint)
{ alSetError(context, AL_INVALID_ENUM, "Invalid echo integer property 0x%04x", param); }
void Echo_setParamiv(EffectProps*, ALCcontext *context, ALenum param, const ALint*)
{ alSetError(context, AL_INVALID_ENUM, "Invalid echo integer-vector property 0x%04x", param); }
void Echo_setParamf(EffectProps *props, ALCcontext *context, ALenum param, ALfloat val)
{
    switch(param)
    {
        case AL_ECHO_DELAY:
            if(!(val >= AL_ECHO_MIN_DELAY && val <= AL_ECHO_MAX_DELAY))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Echo delay out of range");
            props->Echo.Delay = val;
            break;

        case AL_ECHO_LRDELAY:
            if(!(val >= AL_ECHO_MIN_LRDELAY && val <= AL_ECHO_MAX_LRDELAY))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Echo LR delay out of range");
            props->Echo.LRDelay = val;
            break;

        case AL_ECHO_DAMPING:
            if(!(val >= AL_ECHO_MIN_DAMPING && val <= AL_ECHO_MAX_DAMPING))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Echo damping out of range");
            props->Echo.Damping = val;
            break;

        case AL_ECHO_FEEDBACK:
            if(!(val >= AL_ECHO_MIN_FEEDBACK && val <= AL_ECHO_MAX_FEEDBACK))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Echo feedback out of range");
            props->Echo.Feedback = val;
            break;

        case AL_ECHO_SPREAD:
            if(!(val >= AL_ECHO_MIN_SPREAD && val <= AL_ECHO_MAX_SPREAD))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Echo spread out of range");
            props->Echo.Spread = val;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid echo float property 0x%04x", param);
    }
}
void Echo_setParamfv(EffectProps *props, ALCcontext *context, ALenum param, const ALfloat *vals)
{ Echo_setParamf(props, context, param, vals[0]); }

void Echo_getParami(const EffectProps*, ALCcontext *context, ALenum param, ALint*)
{ alSetError(context, AL_INVALID_ENUM, "Invalid echo integer property 0x%04x", param); }
void Echo_getParamiv(const EffectProps*, ALCcontext *context, ALenum param, ALint*)
{ alSetError(context, AL_INVALID_ENUM, "Invalid echo integer-vector property 0x%04x", param); }
void Echo_getParamf(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *val)
{
    switch(param)
    {
        case AL_ECHO_DELAY:
            *val = props->Echo.Delay;
            break;

        case AL_ECHO_LRDELAY:
            *val = props->Echo.LRDelay;
            break;

        case AL_ECHO_DAMPING:
            *val = props->Echo.Damping;
            break;

        case AL_ECHO_FEEDBACK:
            *val = props->Echo.Feedback;
            break;

        case AL_ECHO_SPREAD:
            *val = props->Echo.Spread;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid echo float property 0x%04x", param);
    }
}
void Echo_getParamfv(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *vals)
{ Echo_getParamf(props, context, param, vals); }

DEFINE_ALEFFECT_VTABLE(Echo);


struct EchoStateFactory final : public EffectStateFactory {
    EffectState *create() override { return new EchoState{}; }
    EffectProps getDefaultProps() const noexcept override;
    const EffectVtable *getEffectVtable() const noexcept override { return &Echo_vtable; }
};

EffectProps EchoStateFactory::getDefaultProps() const noexcept
{
    EffectProps props{};
    props.Echo.Delay    = AL_ECHO_DEFAULT_DELAY;
    props.Echo.LRDelay  = AL_ECHO_DEFAULT_LRDELAY;
    props.Echo.Damping  = AL_ECHO_DEFAULT_DAMPING;
    props.Echo.Feedback = AL_ECHO_DEFAULT_FEEDBACK;
    props.Echo.Spread   = AL_ECHO_DEFAULT_SPREAD;
    return props;
}

} // namespace

EffectStateFactory *EchoStateFactory_getFactory()
{
    static EchoStateFactory EchoFactory{};
    return &EchoFactory;
}
