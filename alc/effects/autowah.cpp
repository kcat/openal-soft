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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <ranges>
#include <span>
#include <variant>

#include "alc/effects/base.h"
#include "alnumeric.h"
#include "core/ambidefs.h"
#include "core/bufferline.h"
#include "core/context.h"
#include "core/device.h"
#include "core/effects/base.h"
#include "core/effectslot.h"
#include "core/mixer.h"
#include "intrusive_ptr.h"

struct BufferStorage;

namespace {

constexpr auto GainScale = 31621.0f;
constexpr auto MinFreq = 20.0f;
constexpr auto MaxFreq = 2500.0f;
constexpr auto QFactor = 5.0f;

struct AutowahState final : public EffectState {
    /* Effect parameters */
    float mAttackRate{};
    float mReleaseRate{};
    float mResonanceGain{};
    float mPeakGain{};
    float mFreqMinNorm{};
    float mBandwidthNorm{};
    float mEnvDelay{};

    /* Filter components derived from the envelope. */
    struct FilterParam {
        float cos_w0{};
        float alpha{};
    };
    std::array<FilterParam,BufferLineSize> mEnv;

    struct ChannelData {
        uint mTargetChannel{InvalidChannelIndex};

        struct FilterHistory {
            float z1{}, z2{};
        };
        FilterHistory mFilter;

        /* Effect gains for each output channel */
        float mCurrentGain{};
        float mTargetGain{};
    };
    std::array<ChannelData,MaxAmbiChannels> mChans;

    /* Effects buffers */
    alignas(16) FloatBufferLine mBufferOut{};


    void deviceUpdate(const DeviceBase *device, const BufferStorage *buffer) override;
    void update(const ContextBase *context, const EffectSlot *slot, const EffectProps *props,
        const EffectTarget target) override;
    void process(const size_t samplesToDo, const std::span<const FloatBufferLine> samplesIn,
        const std::span<FloatBufferLine> samplesOut) override;
};

void AutowahState::deviceUpdate(const DeviceBase*, const BufferStorage*)
{
    /* (Re-)initializing parameters and clear the buffers. */
    mAttackRate    = 1.0f;
    mReleaseRate   = 1.0f;
    mResonanceGain = 10.0f;
    mPeakGain      = 4.5f;
    mFreqMinNorm   = 4.5e-4f;
    mBandwidthNorm = 0.05f;
    mEnvDelay      = 0.0f;

    mEnv.fill(FilterParam{});
    mChans.fill(ChannelData{});
}

void AutowahState::update(const ContextBase *context, const EffectSlot *slot,
    const EffectProps *props_, const EffectTarget target)
{
    auto &props = std::get<AutowahProps>(*props_);
    auto const device = al::get_not_null(context->mDevice);
    auto const frequency = static_cast<float>(device->mSampleRate);

    const auto ReleaseTime = std::clamp(props.ReleaseTime, 0.001f, 1.0f);

    mAttackRate    = std::exp(-1.0f / (props.AttackTime*frequency));
    mReleaseRate   = std::exp(-1.0f / (ReleaseTime*frequency));
    /* 0-20dB Resonance Peak gain */
    mResonanceGain = std::sqrt(std::log10(props.Resonance)*10.0f / 3.0f);
    mPeakGain      = 1.0f - std::log10(props.PeakGain / GainScale);
    mFreqMinNorm   = MinFreq / frequency;
    mBandwidthNorm = (MaxFreq-MinFreq) / frequency;

    mOutTarget = target.Main->Buffer;
    target.Main->setAmbiMixParams(slot->Wet, slot->Gain,
        [this](const size_t idx, const uint outchan, const float outgain)
    {
        mChans[idx].mTargetChannel = outchan;
        mChans[idx].mTargetGain = outgain;
    });
}

void AutowahState::process(const size_t samplesToDo,
    const std::span<const FloatBufferLine> samplesIn, const std::span<FloatBufferLine> samplesOut)
{
    auto env_delay = mEnvDelay;
    std::ranges::transform(samplesIn[0] | std::views::take(samplesToDo), mEnv.begin(),
        [attack_rate=mAttackRate,release_rate=mReleaseRate,peak_gain=mPeakGain,
        freq_min=mFreqMinNorm,bandwidth=mBandwidthNorm,&env_delay](const float x) -> FilterParam
    {
        /* Envelope follower described on the book: Audio Effects, Theory,
         * Implementation and Application.
         */
        const auto sample = peak_gain * std::fabs(x);
        const auto a = (sample > env_delay) ? attack_rate : release_rate;
        env_delay = lerpf(sample, env_delay, a);

        /* Calculate the cos and alpha components for this sample's filter. */
        const auto w0 = std::min(bandwidth*env_delay + freq_min, 0.46f) *
            (std::numbers::pi_v<float>*2.0f);
        return FilterParam{.cos_w0=std::cos(w0), .alpha=std::sin(w0)*(0.5f/QFactor)};
    });
    mEnvDelay = env_delay;

    auto chandata = mChans.begin();
    std::ranges::for_each(samplesIn, [&,this](const FloatConstBufferSpan insamples)
    {
        const auto outidx = chandata->mTargetChannel;
        if(outidx == InvalidChannelIndex)
        {
            ++chandata;
            return;
        }

        /* This effectively inlines BiquadFilter::setParams for a peaking
         * filter and BiquadFilter::process. The alpha and cosine components
         * for the filter coefficients were previously calculated with the
         * envelope. Because the filter changes for each sample, the
         * coefficients are transient and don't need to be held.
         */
        auto z1 = chandata->mFilter.z1;
        auto z2 = chandata->mFilter.z2;
        std::ranges::transform(insamples | std::views::take(samplesToDo), mEnv, mBufferOut.begin(),
            [res_gain=mResonanceGain,&z1,&z2](const float input, const FilterParam env) -> float
        {
            const auto b = std::array{
                1.0f + env.alpha*res_gain,
                -2.0f * env.cos_w0,
                1.0f - env.alpha*res_gain};
            const auto a = std::array{
                1.0f / (1.0f + env.alpha/res_gain),
                -2.0f * env.cos_w0,
                1.0f - env.alpha/res_gain};

            const auto output = input*(b[0]*a[0]) + z1;
            z1 = input*(b[1]*a[0]) - output*(a[1]*a[0]) + z2;
            z2 = input*(b[2]*a[0]) - output*(a[2]*a[0]);
            return output;
        });
        chandata->mFilter.z1 = z1;
        chandata->mFilter.z2 = z2;

        /* Now, mix the processed sound data to the output. */
        MixSamples(std::span{mBufferOut}.first(samplesToDo), samplesOut[outidx],
            chandata->mCurrentGain, chandata->mTargetGain, samplesToDo);
        ++chandata;
    });
}


struct AutowahStateFactory final : public EffectStateFactory {
    al::intrusive_ptr<EffectState> create() override
    { return al::intrusive_ptr<EffectState>{new AutowahState{}}; }
};

} // namespace

auto AutowahStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>
{
    static AutowahStateFactory AutowahFactory{};
    return &AutowahFactory;
}
