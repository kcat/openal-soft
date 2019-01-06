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

#include <algorithm>

#include "alMain.h"
#include "alcontext.h"
#include "alFilter.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
#include "alu.h"
#include "filters/biquad.h"
#include "vector.h"


struct ALechoState final : public EffectState {
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

    ALfloat mFeedGain{0.0f};

    BiquadFilter mFilter;


    ALboolean deviceUpdate(const ALCdevice *device) override;
    void update(const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props, const EffectTarget target) override;
    void process(ALsizei samplesToDo, const ALfloat (*RESTRICT samplesIn)[BUFFERSIZE], ALfloat (*RESTRICT samplesOut)[BUFFERSIZE], ALsizei numChannels) override;

    DEF_NEWDEL(ALechoState)
};

ALboolean ALechoState::deviceUpdate(const ALCdevice *Device)
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

void ALechoState::update(const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props, const EffectTarget target)
{
    const ALCdevice *device = context->Device;
    ALuint frequency = device->Frequency;
    ALfloat gainhf, lrpan, spread;

    mTap[0].delay = maxi(float2int(props->Echo.Delay*frequency + 0.5f), 1);
    mTap[1].delay = float2int(props->Echo.LRDelay*frequency + 0.5f);
    mTap[1].delay += mTap[0].delay;

    spread = props->Echo.Spread;
    if(spread < 0.0f) lrpan = -1.0f;
    else lrpan = 1.0f;
    /* Convert echo spread (where 0 = omni, +/-1 = directional) to coverage
     * spread (where 0 = point, tau = omni).
     */
    spread = asinf(1.0f - fabsf(spread))*4.0f;

    mFeedGain = props->Echo.Feedback;

    gainhf = maxf(1.0f - props->Echo.Damping, 0.0625f); /* Limit -24dB */
    mFilter.setParams(BiquadType::HighShelf, gainhf, LOWPASSFREQREF/frequency,
        calc_rcpQ_from_slope(gainhf, 1.0f)
    );

    ALfloat coeffs[2][MAX_AMBI_COEFFS];
    CalcAngleCoeffs(al::MathDefs<float>::Pi()*-0.5f*lrpan, 0.0f, spread, coeffs[0]);
    CalcAngleCoeffs(al::MathDefs<float>::Pi()* 0.5f*lrpan, 0.0f, spread, coeffs[1]);

    mOutBuffer = target.Main->Buffer;
    mOutChannels = target.Main->NumChannels;
    ComputePanGains(target.Main, coeffs[0], slot->Params.Gain, mGains[0].Target);
    ComputePanGains(target.Main, coeffs[1], slot->Params.Gain, mGains[1].Target);
}

void ALechoState::process(ALsizei SamplesToDo, const ALfloat (*RESTRICT SamplesIn)[BUFFERSIZE], ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE], ALsizei NumChannels)
{
    const auto mask = static_cast<ALsizei>(mSampleBuffer.size()-1);
    const ALsizei tap1{mTap[0].delay};
    const ALsizei tap2{mTap[1].delay};
    ALfloat *RESTRICT delaybuf{mSampleBuffer.data()};
    ALsizei offset{mOffset};
    ALfloat z1, z2;
    ALsizei base;
    ALsizei c, i;

    std::tie(z1, z2) = mFilter.getComponents();
    for(base = 0;base < SamplesToDo;)
    {
        alignas(16) ALfloat temps[2][128];
        ALsizei td = mini(128, SamplesToDo-base);

        for(i = 0;i < td;i++)
        {
            /* Feed the delay buffer's input first. */
            delaybuf[offset&mask] = SamplesIn[0][i+base];

            /* First tap */
            temps[0][i] = delaybuf[(offset-tap1) & mask];
            /* Second tap */
            temps[1][i] = delaybuf[(offset-tap2) & mask];

            /* Apply damping to the second tap, then add it to the buffer with
             * feedback attenuation.
             */
            float out{mFilter.processOne(temps[1][i], z1, z2)};

            delaybuf[offset&mask] += out * mFeedGain;
            offset++;
        }

        for(c = 0;c < 2;c++)
            MixSamples(temps[c], NumChannels, SamplesOut, mGains[c].Current,
                       mGains[c].Target, SamplesToDo-base, base, td);

        base += td;
    }
    mFilter.setComponents(z1, z2);

    mOffset = offset;
}


struct EchoStateFactory final : public EffectStateFactory {
    EffectState *create() override;
};

EffectState *EchoStateFactory::create()
{ return new ALechoState{}; }

EffectStateFactory *EchoStateFactory_getFactory(void)
{
    static EchoStateFactory EchoFactory{};
    return &EchoFactory;
}


void ALecho_setParami(ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALint UNUSED(val))
{ alSetError(context, AL_INVALID_ENUM, "Invalid echo integer property 0x%04x", param); }
void ALecho_setParamiv(ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, const ALint *UNUSED(vals))
{ alSetError(context, AL_INVALID_ENUM, "Invalid echo integer-vector property 0x%04x", param); }
void ALecho_setParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    ALeffectProps *props = &effect->Props;
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
void ALecho_setParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{ ALecho_setParamf(effect, context, param, vals[0]); }

void ALecho_getParami(const ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALint *UNUSED(val))
{ alSetError(context, AL_INVALID_ENUM, "Invalid echo integer property 0x%04x", param); }
void ALecho_getParamiv(const ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALint *UNUSED(vals))
{ alSetError(context, AL_INVALID_ENUM, "Invalid echo integer-vector property 0x%04x", param); }
void ALecho_getParamf(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val)
{
    const ALeffectProps *props = &effect->Props;
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
void ALecho_getParamfv(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{ ALecho_getParamf(effect, context, param, vals); }

DEFINE_ALEFFECT_VTABLE(ALecho);
