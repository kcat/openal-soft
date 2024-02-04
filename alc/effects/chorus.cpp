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
#include <array>
#include <climits>
#include <cstdlib>
#include <iterator>
#include <vector>

#include "alc/effects/base.h"
#include "almalloc.h"
#include "alnumbers.h"
#include "alnumeric.h"
#include "alspan.h"
#include "core/bufferline.h"
#include "core/context.h"
#include "core/devformat.h"
#include "core/device.h"
#include "core/effectslot.h"
#include "core/mixer.h"
#include "core/mixer/defs.h"
#include "core/resampler_limits.h"
#include "intrusive_ptr.h"
#include "opthelpers.h"


namespace {

using uint = unsigned int;

constexpr auto inv_sqrt2 = static_cast<float>(1.0 / al::numbers::sqrt2);
constexpr auto lcoeffs_pw = CalcDirectionCoeffs(std::array{-1.0f, 0.0f, 0.0f});
constexpr auto rcoeffs_pw = CalcDirectionCoeffs(std::array{ 1.0f, 0.0f, 0.0f});
constexpr auto lcoeffs_nrml = CalcDirectionCoeffs(std::array{-inv_sqrt2, 0.0f, inv_sqrt2});
constexpr auto rcoeffs_nrml = CalcDirectionCoeffs(std::array{ inv_sqrt2, 0.0f, inv_sqrt2});


struct ChorusState : public EffectState {
    std::vector<float> mDelayBuffer;
    uint mOffset{0};

    uint mLfoOffset{0};
    uint mLfoRange{1};
    float mLfoScale{0.0f};
    uint mLfoDisp{0};

    /* Calculated delays to apply to the left and right outputs. */
    std::array<std::array<uint,BufferLineSize>,2> mModDelays{};

    /* Temp storage for the modulated left and right outputs. */
    alignas(16) std::array<FloatBufferLine,2> mBuffer{};

    /* Gains for left and right outputs. */
    struct OutGains {
        std::array<float,MaxAmbiChannels> Current{};
        std::array<float,MaxAmbiChannels> Target{};
    };
    std::array<OutGains,2> mGains;

    /* effect parameters */
    ChorusWaveform mWaveform{};
    int mDelay{0};
    float mDepth{0.0f};
    float mFeedback{0.0f};

    void calcTriangleDelays(const size_t todo);
    void calcSinusoidDelays(const size_t todo);

    void deviceUpdate(const DeviceBase *device, const float MaxDelay);
    void update(const ContextBase *context, const EffectSlot *slot, const ChorusWaveform waveform,
            const float delay, const float depth, const float feedback, const float rate,
            int phase, const EffectTarget target);

    void deviceUpdate(const DeviceBase *device, const BufferStorage*) override
    { deviceUpdate(device, ChorusMaxDelay); }
    void update(const ContextBase *context, const EffectSlot *slot, const EffectProps *props_,
        const EffectTarget target) override
    {
        auto &props = std::get<ChorusProps>(*props_);
        update(context, slot, props.Waveform, props.Delay, props.Depth, props.Feedback, props.Rate,
            props.Phase, target);
    }
    void process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn,
        const al::span<FloatBufferLine> samplesOut) final;
};

struct FlangerState final : public ChorusState {
    void deviceUpdate(const DeviceBase *device, const BufferStorage*) final
    { ChorusState::deviceUpdate(device, FlangerMaxDelay); }
    void update(const ContextBase *context, const EffectSlot *slot, const EffectProps *props_,
        const EffectTarget target) final
    {
        auto &props = std::get<FlangerProps>(*props_);
        ChorusState::update(context, slot, props.Waveform, props.Delay, props.Depth,
            props.Feedback, props.Rate, props.Phase, target);
    }
};


void ChorusState::deviceUpdate(const DeviceBase *Device, const float MaxDelay)
{
    const auto frequency = static_cast<float>(Device->Frequency);
    const size_t maxlen{NextPowerOf2(float2uint(MaxDelay*2.0f*frequency) + 1u)};
    if(maxlen != mDelayBuffer.size())
        decltype(mDelayBuffer)(maxlen).swap(mDelayBuffer);

    std::fill(mDelayBuffer.begin(), mDelayBuffer.end(), 0.0f);
    for(auto &e : mGains)
    {
        e.Current.fill(0.0f);
        e.Target.fill(0.0f);
    }
}

void ChorusState::update(const ContextBase *context, const EffectSlot *slot,
    const ChorusWaveform waveform, const float delay, const float depth, const float feedback,
    const float rate, int phase, const EffectTarget target)
{
    static constexpr int mindelay{(MaxResamplerPadding>>1) << MixerFracBits};

    /* The LFO depth is scaled to be relative to the sample delay. Clamp the
     * delay and depth to allow enough padding for resampling.
     */
    const DeviceBase *device{context->mDevice};
    const auto frequency = static_cast<float>(device->Frequency);

    mWaveform = waveform;

    mDelay = std::max(float2int(std::round(delay*frequency*MixerFracOne)), mindelay);
    mDepth = std::min(depth * static_cast<float>(mDelay),
        static_cast<float>(mDelay - mindelay));

    mFeedback = feedback;

    /* Gains for left and right sides */
    const bool ispairwise{device->mRenderMode == RenderMode::Pairwise};
    const auto lcoeffs = (!ispairwise) ? al::span{lcoeffs_nrml} : al::span{lcoeffs_pw};
    const auto rcoeffs = (!ispairwise) ? al::span{rcoeffs_nrml} : al::span{rcoeffs_pw};

    mOutTarget = target.Main->Buffer;
    ComputePanGains(target.Main, lcoeffs, slot->Gain, mGains[0].Target);
    ComputePanGains(target.Main, rcoeffs, slot->Gain, mGains[1].Target);

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
        static constexpr int range_limit{std::numeric_limits<int>::max()/360 - 180};
        const uint lfo_range{float2uint(std::min(std::round(frequency/rate), float{range_limit}))};

        mLfoOffset = mLfoOffset * lfo_range / mLfoRange;
        mLfoRange = lfo_range;
        switch(mWaveform)
        {
        case ChorusWaveform::Triangle:
            mLfoScale = 4.0f / static_cast<float>(mLfoRange);
            break;
        case ChorusWaveform::Sinusoid:
            mLfoScale = al::numbers::pi_v<float>*2.0f / static_cast<float>(mLfoRange);
            break;
        }

        /* Calculate lfo phase displacement */
        if(phase < 0) phase = 360 + phase;
        mLfoDisp = (mLfoRange*static_cast<uint>(phase) + 180) / 360;
    }
}


void ChorusState::calcTriangleDelays(const size_t todo)
{
    const uint lfo_range{mLfoRange};
    const float lfo_scale{mLfoScale};
    const float depth{mDepth};
    const int delay{mDelay};

    ASSUME(lfo_range > 0);
    ASSUME(todo > 0);

    auto gen_lfo = [lfo_scale,depth,delay](const uint offset) -> uint
    {
        const float offset_norm{static_cast<float>(offset) * lfo_scale};
        return static_cast<uint>(fastf2i((1.0f-std::abs(2.0f-offset_norm)) * depth) + delay);
    };

    uint offset{mLfoOffset};
    for(size_t i{0};i < todo;)
    {
        size_t rem{std::min(todo-i, size_t{lfo_range-offset})};
        do {
            mModDelays[0][i++] = gen_lfo(offset++);
        } while(--rem);
        if(offset == lfo_range)
            offset = 0;
    }

    offset = (mLfoOffset+mLfoDisp) % lfo_range;
    for(size_t i{0};i < todo;)
    {
        size_t rem{std::min(todo-i, size_t{lfo_range-offset})};
        do {
            mModDelays[1][i++] = gen_lfo(offset++);
        } while(--rem);
        if(offset == lfo_range)
            offset = 0;
    }

    mLfoOffset = static_cast<uint>(mLfoOffset+todo) % lfo_range;
}

void ChorusState::calcSinusoidDelays(const size_t todo)
{
    const uint lfo_range{mLfoRange};
    const float lfo_scale{mLfoScale};
    const float depth{mDepth};
    const int delay{mDelay};

    ASSUME(lfo_range > 0);
    ASSUME(todo > 0);

    auto gen_lfo = [lfo_scale,depth,delay](const uint offset) -> uint
    {
        const float offset_norm{static_cast<float>(offset) * lfo_scale};
        return static_cast<uint>(fastf2i(std::sin(offset_norm)*depth) + delay);
    };

    uint offset{mLfoOffset};
    for(size_t i{0};i < todo;)
    {
        size_t rem{std::min(todo-i, size_t{lfo_range-offset})};
        do {
            mModDelays[0][i++] = gen_lfo(offset++);
        } while(--rem);
        if(offset == lfo_range)
            offset = 0;
    }

    offset = (mLfoOffset+mLfoDisp) % lfo_range;
    for(size_t i{0};i < todo;)
    {
        size_t rem{std::min(todo-i, size_t{lfo_range-offset})};
        do {
            mModDelays[1][i++] = gen_lfo(offset++);
        } while(--rem);
        if(offset == lfo_range)
            offset = 0;
    }

    mLfoOffset = static_cast<uint>(mLfoOffset+todo) % lfo_range;
}

void ChorusState::process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut)
{
    const size_t bufmask{mDelayBuffer.size()-1};
    const float feedback{mFeedback};
    const uint avgdelay{(static_cast<uint>(mDelay) + MixerFracHalf) >> MixerFracBits};
    float *RESTRICT delaybuf{mDelayBuffer.data()};
    uint offset{mOffset};

    if(mWaveform == ChorusWaveform::Sinusoid)
        calcSinusoidDelays(samplesToDo);
    else /*if(mWaveform == ChorusWaveform::Triangle)*/
        calcTriangleDelays(samplesToDo);

    const uint *RESTRICT ldelays{mModDelays[0].data()};
    const uint *RESTRICT rdelays{mModDelays[1].data()};
    float *RESTRICT lbuffer{al::assume_aligned<16>(mBuffer[0].data())};
    float *RESTRICT rbuffer{al::assume_aligned<16>(mBuffer[1].data())};
    for(size_t i{0u};i < samplesToDo;++i)
    {
        // Feed the buffer's input first (necessary for delays < 1).
        delaybuf[offset&bufmask] = samplesIn[0][i];

        // Tap for the left output.
        uint delay{offset - (ldelays[i]>>MixerFracBits)};
        float mu{static_cast<float>(ldelays[i]&MixerFracMask) * (1.0f/MixerFracOne)};
        lbuffer[i] = cubic(delaybuf[(delay+1) & bufmask], delaybuf[(delay  ) & bufmask],
            delaybuf[(delay-1) & bufmask], delaybuf[(delay-2) & bufmask], mu);

        // Tap for the right output.
        delay = offset - (rdelays[i]>>MixerFracBits);
        mu = static_cast<float>(rdelays[i]&MixerFracMask) * (1.0f/MixerFracOne);
        rbuffer[i] = cubic(delaybuf[(delay+1) & bufmask], delaybuf[(delay  ) & bufmask],
            delaybuf[(delay-1) & bufmask], delaybuf[(delay-2) & bufmask], mu);

        // Accumulate feedback from the average delay of the taps.
        delaybuf[offset&bufmask] += delaybuf[(offset-avgdelay) & bufmask] * feedback;
        ++offset;
    }

    MixSamples({lbuffer, samplesToDo}, samplesOut, mGains[0].Current.data(),
        mGains[0].Target.data(), samplesToDo, 0);
    MixSamples({rbuffer, samplesToDo}, samplesOut, mGains[1].Current.data(),
        mGains[1].Target.data(), samplesToDo, 0);

    mOffset = offset;
}


struct ChorusStateFactory final : public EffectStateFactory {
    al::intrusive_ptr<EffectState> create() override
    { return al::intrusive_ptr<EffectState>{new ChorusState{}}; }
};


/* Flanger is basically a chorus with a really short delay. They can both use
 * the same processing functions, so piggyback flanger on the chorus functions.
 */
struct FlangerStateFactory final : public EffectStateFactory {
    al::intrusive_ptr<EffectState> create() override
    { return al::intrusive_ptr<EffectState>{new FlangerState{}}; }
};

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
