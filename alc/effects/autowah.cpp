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

#include <algorithm>

#include "al/auxeffectslot.h"
#include "alcmain.h"
#include "alcontext.h"
#include "alu.h"
#include "filters/biquad.h"
#include "vecmat.h"

namespace {

#define MIN_FREQ 20.0f
#define MAX_FREQ 2500.0f
#define Q_FACTOR 5.0f

struct AutowahState final : public EffectState {
    /* Effect parameters */
    ALfloat mAttackRate;
    ALfloat mReleaseRate;
    ALfloat mResonanceGain;
    ALfloat mPeakGain;
    ALfloat mFreqMinNorm;
    ALfloat mBandwidthNorm;
    ALfloat mEnvDelay;

    /* Filter components derived from the envelope. */
    struct {
        ALfloat cos_w0;
        ALfloat alpha;
    } mEnv[BUFFERSIZE];

    struct {
        /* Effect filters' history. */
        struct {
            ALfloat z1, z2;
        } Filter;

        /* Effect gains for each output channel */
        ALfloat CurrentGains[MAX_OUTPUT_CHANNELS];
        ALfloat TargetGains[MAX_OUTPUT_CHANNELS];
    } mChans[MAX_AMBI_CHANNELS];

    /* Effects buffers */
    alignas(16) ALfloat mBufferOut[BUFFERSIZE];


    ALboolean deviceUpdate(const ALCdevice *device) override;
    void update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target) override;
    void process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut) override;

    DEF_NEWDEL(AutowahState)
};

ALboolean AutowahState::deviceUpdate(const ALCdevice*)
{
    /* (Re-)initializing parameters and clear the buffers. */

    mAttackRate    = 1.0f;
    mReleaseRate   = 1.0f;
    mResonanceGain = 10.0f;
    mPeakGain      = 4.5f;
    mFreqMinNorm   = 4.5e-4f;
    mBandwidthNorm = 0.05f;
    mEnvDelay      = 0.0f;

    for(auto &e : mEnv)
    {
        e.cos_w0 = 0.0f;
        e.alpha = 0.0f;
    }

    for(auto &chan : mChans)
    {
        std::fill(std::begin(chan.CurrentGains), std::end(chan.CurrentGains), 0.0f);
        chan.Filter.z1 = 0.0f;
        chan.Filter.z2 = 0.0f;
    }

    return AL_TRUE;
}

void AutowahState::update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target)
{
    const ALCdevice *device{context->mDevice.get()};
    const auto frequency = static_cast<float>(device->Frequency);

    const ALfloat ReleaseTime{clampf(props->Autowah.ReleaseTime, 0.001f, 1.0f)};

    mAttackRate    = std::exp(-1.0f / (props->Autowah.AttackTime*frequency));
    mReleaseRate   = std::exp(-1.0f / (ReleaseTime*frequency));
    /* 0-20dB Resonance Peak gain */
    mResonanceGain = std::sqrt(std::log10(props->Autowah.Resonance)*10.0f / 3.0f);
    mPeakGain      = 1.0f - std::log10(props->Autowah.PeakGain/AL_AUTOWAH_MAX_PEAK_GAIN);
    mFreqMinNorm   = MIN_FREQ / frequency;
    mBandwidthNorm = (MAX_FREQ-MIN_FREQ) / frequency;

    mOutTarget = target.Main->Buffer;
    for(size_t i{0u};i < slot->Wet.Buffer.size();++i)
    {
        auto coeffs = GetAmbiIdentityRow(i);
        ComputePanGains(target.Main, coeffs.data(), slot->Params.Gain, mChans[i].TargetGains);
    }
}

void AutowahState::process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut)
{
    const ALfloat attack_rate = mAttackRate;
    const ALfloat release_rate = mReleaseRate;
    const ALfloat res_gain = mResonanceGain;
    const ALfloat peak_gain = mPeakGain;
    const ALfloat freq_min = mFreqMinNorm;
    const ALfloat bandwidth = mBandwidthNorm;

    ALfloat env_delay{mEnvDelay};
    for(size_t i{0u};i < samplesToDo;i++)
    {
        ALfloat w0, sample, a;

        /* Envelope follower described on the book: Audio Effects, Theory,
         * Implementation and Application.
         */
        sample = peak_gain * std::fabs(samplesIn[0][i]);
        a = (sample > env_delay) ? attack_rate : release_rate;
        env_delay = lerp(sample, env_delay, a);

        /* Calculate the cos and alpha components for this sample's filter. */
        w0 = minf((bandwidth*env_delay + freq_min), 0.46f) * al::MathDefs<float>::Tau();
        mEnv[i].cos_w0 = cosf(w0);
        mEnv[i].alpha = sinf(w0)/(2.0f * Q_FACTOR);
    }
    mEnvDelay = env_delay;

    auto chandata = std::addressof(mChans[0]);
    for(const auto &insamples : samplesIn)
    {
        /* This effectively inlines BiquadFilter_setParams for a peaking
         * filter and BiquadFilter_processC. The alpha and cosine components
         * for the filter coefficients were previously calculated with the
         * envelope. Because the filter changes for each sample, the
         * coefficients are transient and don't need to be held.
         */
        ALfloat z1{chandata->Filter.z1};
        ALfloat z2{chandata->Filter.z2};

        for(size_t i{0u};i < samplesToDo;i++)
        {
            const ALfloat alpha = mEnv[i].alpha;
            const ALfloat cos_w0 = mEnv[i].cos_w0;
            ALfloat input, output;
            ALfloat a[3], b[3];

            b[0] =  1.0f + alpha*res_gain;
            b[1] = -2.0f * cos_w0;
            b[2] =  1.0f - alpha*res_gain;
            a[0] =  1.0f + alpha/res_gain;
            a[1] = -2.0f * cos_w0;
            a[2] =  1.0f - alpha/res_gain;

            input = insamples[i];
            output = input*(b[0]/a[0]) + z1;
            z1 = input*(b[1]/a[0]) - output*(a[1]/a[0]) + z2;
            z2 = input*(b[2]/a[0]) - output*(a[2]/a[0]);
            mBufferOut[i] = output;
        }
        chandata->Filter.z1 = z1;
        chandata->Filter.z2 = z2;

        /* Now, mix the processed sound data to the output. */
        MixSamples({mBufferOut, samplesToDo}, samplesOut, chandata->CurrentGains,
            chandata->TargetGains, samplesToDo, 0);
        ++chandata;
    }
}


void Autowah_setParamf(EffectProps *props, ALCcontext *context, ALenum param, ALfloat val)
{
    switch(param)
    {
        case AL_AUTOWAH_ATTACK_TIME:
            if(!(val >= AL_AUTOWAH_MIN_ATTACK_TIME && val <= AL_AUTOWAH_MAX_ATTACK_TIME))
                SETERR_RETURN(context, AL_INVALID_VALUE,,"Autowah attack time out of range");
            props->Autowah.AttackTime = val;
            break;

        case AL_AUTOWAH_RELEASE_TIME:
            if(!(val >= AL_AUTOWAH_MIN_RELEASE_TIME && val <= AL_AUTOWAH_MAX_RELEASE_TIME))
                SETERR_RETURN(context, AL_INVALID_VALUE,,"Autowah release time out of range");
            props->Autowah.ReleaseTime = val;
            break;

        case AL_AUTOWAH_RESONANCE:
            if(!(val >= AL_AUTOWAH_MIN_RESONANCE && val <= AL_AUTOWAH_MAX_RESONANCE))
                SETERR_RETURN(context, AL_INVALID_VALUE,,"Autowah resonance out of range");
            props->Autowah.Resonance = val;
            break;

        case AL_AUTOWAH_PEAK_GAIN:
            if(!(val >= AL_AUTOWAH_MIN_PEAK_GAIN && val <= AL_AUTOWAH_MAX_PEAK_GAIN))
                SETERR_RETURN(context, AL_INVALID_VALUE,,"Autowah peak gain out of range");
            props->Autowah.PeakGain = val;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid autowah float property 0x%04x", param);
    }
}
void Autowah_setParamfv(EffectProps *props, ALCcontext *context, ALenum param, const ALfloat *vals)
{ Autowah_setParamf(props, context, param, vals[0]); }

void Autowah_setParami(EffectProps*, ALCcontext *context, ALenum param, ALint)
{ context->setError(AL_INVALID_ENUM, "Invalid autowah integer property 0x%04x", param); }
void Autowah_setParamiv(EffectProps*, ALCcontext *context, ALenum param, const ALint*)
{ context->setError(AL_INVALID_ENUM, "Invalid autowah integer vector property 0x%04x", param); }

void Autowah_getParamf(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *val)
{
    switch(param)
    {
        case AL_AUTOWAH_ATTACK_TIME:
            *val = props->Autowah.AttackTime;
            break;

        case AL_AUTOWAH_RELEASE_TIME:
            *val = props->Autowah.ReleaseTime;
            break;

        case AL_AUTOWAH_RESONANCE:
            *val = props->Autowah.Resonance;
            break;

        case AL_AUTOWAH_PEAK_GAIN:
            *val = props->Autowah.PeakGain;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid autowah float property 0x%04x", param);
    }

}
void Autowah_getParamfv(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *vals)
{ Autowah_getParamf(props, context, param, vals); }

void Autowah_getParami(const EffectProps*, ALCcontext *context, ALenum param, ALint*)
{ context->setError(AL_INVALID_ENUM, "Invalid autowah integer property 0x%04x", param); }
void Autowah_getParamiv(const EffectProps*, ALCcontext *context, ALenum param, ALint*)
{ context->setError(AL_INVALID_ENUM, "Invalid autowah integer vector property 0x%04x", param); }

DEFINE_ALEFFECT_VTABLE(Autowah);


struct AutowahStateFactory final : public EffectStateFactory {
    EffectState *create() override { return new AutowahState{}; }
    EffectProps getDefaultProps() const noexcept override;
    const EffectVtable *getEffectVtable() const noexcept override { return &Autowah_vtable; }
};

EffectProps AutowahStateFactory::getDefaultProps() const noexcept
{
    EffectProps props{};
    props.Autowah.AttackTime = AL_AUTOWAH_DEFAULT_ATTACK_TIME;
    props.Autowah.ReleaseTime = AL_AUTOWAH_DEFAULT_RELEASE_TIME;
    props.Autowah.Resonance = AL_AUTOWAH_DEFAULT_RESONANCE;
    props.Autowah.PeakGain = AL_AUTOWAH_DEFAULT_PEAK_GAIN;
    return props;
}

} // namespace

EffectStateFactory *AutowahStateFactory_getFactory()
{
    static AutowahStateFactory AutowahFactory{};
    return &AutowahFactory;
}
