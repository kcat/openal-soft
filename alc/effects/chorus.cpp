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
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <span>
#include <variant>
#include <vector>

#include "alc/effects/base.h"
#include "alnumeric.h"
#include "core/ambidefs.h"
#include "core/bufferline.h"
#include "core/context.h"
#include "core/cubic_tables.h"
#include "core/device.h"
#include "core/effects/base.h"
#include "core/effectslot.h"
#include "core/mixer.h"
#include "core/mixer/defs.h"
#include "core/resampler_limits.h"
#include "intrusive_ptr.h"
#include "opthelpers.h"

struct BufferStorage;

namespace {

using uint = unsigned int;

constexpr auto inv_sqrt2 = static_cast<float>(1.0 / std::numbers::sqrt2);
constexpr auto lcoeffs_pw = CalcDirectionCoeffs(std::array{-1.0f, 0.0f, 0.0f});
constexpr auto rcoeffs_pw = CalcDirectionCoeffs(std::array{ 1.0f, 0.0f, 0.0f});
constexpr auto lcoeffs_nrml = CalcDirectionCoeffs(std::array{-inv_sqrt2, 0.0f, inv_sqrt2});
constexpr auto rcoeffs_nrml = CalcDirectionCoeffs(std::array{ inv_sqrt2, 0.0f, inv_sqrt2});


struct ChorusState final : public EffectState {
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

    void deviceUpdate(const DeviceBase *device, const BufferStorage*) final;
    void update(const ContextBase *context, const EffectSlot *slot, const EffectProps *props_,
        const EffectTarget target) final;
    void process(const size_t samplesToDo, const std::span<const FloatBufferLine> samplesIn,
        const std::span<FloatBufferLine> samplesOut) final;
};


void ChorusState::deviceUpdate(const DeviceBase *Device, const BufferStorage*)
{
    static constexpr auto MaxDelay = std::max(ChorusMaxDelay, FlangerMaxDelay);
    const auto frequency = static_cast<float>(Device->mSampleRate);
    const auto maxlen = size_t{NextPowerOf2(float2uint(MaxDelay*2.0f*frequency) + 1u)};
    if(maxlen != mDelayBuffer.size())
        decltype(mDelayBuffer)(maxlen).swap(mDelayBuffer);

    std::ranges::fill(mDelayBuffer, 0.0f);
    mGains.fill(OutGains{});
}

void ChorusState::update(const ContextBase *context, const EffectSlot *slot,
    const EffectProps *props_, const EffectTarget target)
{
    static constexpr auto mindelay = int{MaxResamplerEdge << gCubicTable.sTableBits};
    auto &props = std::get<ChorusProps>(*props_);

    /* The LFO depth is scaled to be relative to the sample delay. Clamp the
     * delay and depth to allow enough padding for resampling.
     */
    auto const device = al::get_not_null(context->mDevice);
    auto const frequency = static_cast<float>(device->mSampleRate);

    mWaveform = props.Waveform;

    const auto stepscale = float{frequency * gCubicTable.sTableSteps};
    mDelay = std::max(float2int(std::round(props.Delay * stepscale)), mindelay);
    mDepth = std::min(static_cast<float>(mDelay) * props.Depth,
        static_cast<float>(mDelay - mindelay));

    mFeedback = props.Feedback;

    /* Gains for left and right sides */
    const auto ispairwise = device->mRenderMode == RenderMode::Pairwise;
    const auto lcoeffs = (!ispairwise) ? std::span{lcoeffs_nrml} : std::span{lcoeffs_pw};
    const auto rcoeffs = (!ispairwise) ? std::span{rcoeffs_nrml} : std::span{rcoeffs_pw};

    /* Attenuate the outputs by -3dB, since we duplicate a single mono input to
     * separate left/right outputs.
     */
    const auto gain = slot->Gain * (1.0f/std::numbers::sqrt2_v<float>);
    mOutTarget = target.Main->Buffer;
    ComputePanGains(target.Main, lcoeffs, gain, mGains[0].Target);
    ComputePanGains(target.Main, rcoeffs, gain, mGains[1].Target);

    if(!(props.Rate > 0.0f))
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
        static constexpr auto range_limit = std::numeric_limits<int>::max()/360 - 180;
        const auto range = std::round(frequency / props.Rate);
        const auto lfo_range = float2uint(std::min(range, float{range_limit}));

        mLfoOffset = mLfoOffset * lfo_range / mLfoRange;
        mLfoRange = lfo_range;
        switch(mWaveform)
        {
        case ChorusWaveform::Triangle:
            mLfoScale = 4.0f / static_cast<float>(mLfoRange);
            break;
        case ChorusWaveform::Sinusoid:
            mLfoScale = std::numbers::pi_v<float>*2.0f / static_cast<float>(mLfoRange);
            break;
        }

        /* Calculate lfo phase displacement */
        auto phase = props.Phase;
        if(phase < 0) phase += 360;
        mLfoDisp = (mLfoRange*static_cast<uint>(phase) + 180) / 360;
    }
}


void ChorusState::calcTriangleDelays(const size_t todo)
{
    const auto lfo_range = mLfoRange;
    const auto lfo_scale = mLfoScale;
    const auto depth = mDepth;
    const auto delay = mDelay;

    auto gen_lfo = [lfo_scale,depth,delay](const uint offset) -> uint
    {
        const float offset_norm{static_cast<float>(offset) * lfo_scale};
        return static_cast<uint>(fastf2i((1.0f-std::abs(2.0f-offset_norm)) * depth) + delay);
    };

    auto offset = mLfoOffset;
    ASSUME(lfo_range > offset);
    auto ldelays = mModDelays[0].begin();
    for(size_t i{0};i < todo;)
    {
        const auto rem = std::min(todo-i, size_t{lfo_range-offset});
        ldelays = std::generate_n(ldelays, rem, [&offset,gen_lfo] { return gen_lfo(offset++); });
        if(offset == lfo_range) offset = 0;
        i += rem;
    }

    offset = (mLfoOffset+mLfoDisp) % lfo_range;
    auto rdelays = mModDelays[1].begin();
    for(size_t i{0};i < todo;)
    {
        const auto rem = std::min(todo-i, size_t{lfo_range-offset});
        rdelays = std::generate_n(rdelays, rem, [&offset,gen_lfo] { return gen_lfo(offset++); });
        if(offset == lfo_range) offset = 0;
        i += rem;
    }

    mLfoOffset = static_cast<uint>(mLfoOffset+todo) % lfo_range;
}

void ChorusState::calcSinusoidDelays(const size_t todo)
{
    const auto lfo_range = mLfoRange;
    const auto lfo_scale = mLfoScale;
    const auto depth = mDepth;
    const auto delay = mDelay;

    auto gen_lfo = [lfo_scale,depth,delay](const uint offset) -> uint
    {
        const float offset_norm{static_cast<float>(offset) * lfo_scale};
        return static_cast<uint>(fastf2i(std::sin(offset_norm)*depth) + delay);
    };

    auto offset = mLfoOffset;
    ASSUME(lfo_range > offset);
    auto ldelays = mModDelays[0].begin();
    for(size_t i{0};i < todo;)
    {
        const auto rem = std::min(todo-i, size_t{lfo_range-offset});
        ldelays = std::generate_n(ldelays, rem, [&offset,gen_lfo] { return gen_lfo(offset++); });
        if(offset == lfo_range) offset = 0;
        i += rem;
    }

    offset = (mLfoOffset+mLfoDisp) % lfo_range;
    auto rdelays = mModDelays[1].begin();
    for(size_t i{0};i < todo;)
    {
        const auto rem = std::min(todo-i, size_t{lfo_range-offset});
        rdelays = std::generate_n(rdelays, rem, [&offset,gen_lfo] { return gen_lfo(offset++); });
        if(offset == lfo_range) offset = 0;
        i += rem;
    }

    mLfoOffset = static_cast<uint>(mLfoOffset+todo) % lfo_range;
}

void ChorusState::process(const size_t samplesToDo,
    const std::span<const FloatBufferLine> samplesIn, const std::span<FloatBufferLine> samplesOut)
{
    const auto delaybuf = std::span{mDelayBuffer};
    const auto bufmask = delaybuf.size()-1;
    const auto feedback = mFeedback;
    const auto avgdelay = (static_cast<uint>(mDelay) + MixerFracHalf) >> MixerFracBits;
    auto offset = mOffset;

    if(mWaveform == ChorusWaveform::Sinusoid)
        calcSinusoidDelays(samplesToDo);
    else /*if(mWaveform == ChorusWaveform::Triangle)*/
        calcTriangleDelays(samplesToDo);

    const auto ldelays = std::span{mModDelays[0]};
    const auto rdelays = std::span{mModDelays[1]};
    const auto lbuffer = std::span{mBuffer[0]};
    const auto rbuffer = std::span{mBuffer[1]};
    for(size_t i{0u};i < samplesToDo;++i)
    {
        /* Feed the buffer's input first (necessary for delays < 1). */
        delaybuf[offset&bufmask] = samplesIn[0][i];

        /* Tap for the left output. */
        auto delay = offset - (ldelays[i] >> gCubicTable.sTableBits);
        auto phase = ldelays[i] & gCubicTable.sTableMask;
        lbuffer[i] = delaybuf[(delay+1) & bufmask]*gCubicTable.getCoeff0(phase) +
            delaybuf[(delay  ) & bufmask]*gCubicTable.getCoeff1(phase) +
            delaybuf[(delay-1) & bufmask]*gCubicTable.getCoeff2(phase) +
            delaybuf[(delay-2) & bufmask]*gCubicTable.getCoeff3(phase);

        /* Tap for the right output. */
        delay = offset - (rdelays[i] >> gCubicTable.sTableBits);
        phase = rdelays[i] & gCubicTable.sTableMask;
        rbuffer[i] = delaybuf[(delay+1) & bufmask]*gCubicTable.getCoeff0(phase) +
            delaybuf[(delay  ) & bufmask]*gCubicTable.getCoeff1(phase) +
            delaybuf[(delay-1) & bufmask]*gCubicTable.getCoeff2(phase) +
            delaybuf[(delay-2) & bufmask]*gCubicTable.getCoeff3(phase);

        /* Accumulate feedback from the average delay of the taps. */
        delaybuf[offset&bufmask] += delaybuf[(offset-avgdelay) & bufmask] * feedback;
        ++offset;
    }

    MixSamples(lbuffer.first(samplesToDo), samplesOut, mGains[0].Current, mGains[0].Target,
        samplesToDo, 0);
    MixSamples(rbuffer.first(samplesToDo), samplesOut, mGains[1].Current, mGains[1].Target,
        samplesToDo, 0);

    mOffset = offset;
}


struct ChorusStateFactory final : public EffectStateFactory {
    al::intrusive_ptr<EffectState> create() override
    { return al::intrusive_ptr<EffectState>{new ChorusState{}}; }
};

} // namespace

auto ChorusStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>
{
    static ChorusStateFactory ChorusFactory{};
    return gsl::make_not_null(&ChorusFactory);
}
