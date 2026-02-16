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
#include <ranges>
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

constexpr auto NumLines = 4_uz;

/* The B-Format to A-Format conversion matrix. This produces a tetrahedral
 * array of discrete signals. Note, A0 and A1 are left-side responses while A2
 * and A3 are right-side responses, which is important to distinguish for the
 * Phase property affecting the right output separately from the left output.
 */
alignas(16) constexpr std::array<std::array<float, NumLines>, NumLines> B2A{{
    /*   W       Y       Z       X   */
    {{ 0.25f,  0.25f,  0.25f,  0.25f }}, /* A0 */
    {{ 0.25f,  0.25f, -0.25f, -0.25f }}, /* A1 */
    {{ 0.25f, -0.25f, -0.25f,  0.25f }}, /* A2 */
    {{ 0.25f, -0.25f,  0.25f, -0.25f }}  /* A3 */
}};

/* Converts A-Format to B-Format for output. */
alignas(16) constexpr std::array<std::array<float, NumLines>, NumLines> A2B{{
    /*  A0     A1     A2     A3  */
    {{ 1.0f,  1.0f,  1.0f,  1.0f }}, /* W */
    {{ 1.0f,  1.0f, -1.0f, -1.0f }}, /* Y */
    {{ 1.0f, -1.0f, -1.0f,  1.0f }}, /* Z */
    {{ 1.0f, -1.0f,  1.0f, -1.0f }}  /* X */
}};


struct ChorusState final : public EffectState {
    std::vector<float> mDelayBuffers;
    unsigned mOffset{0};

    unsigned mLfoOffset{0};
    unsigned mLfoRange{1};
    float mLfoScale{0.0f};
    unsigned mLfoDisp{0};

    /* Calculated delays to apply to the left and right outputs. */
    std::array<std::array<unsigned, BufferLineSize>, 2> mModDelays{};

    /* Temp storage for the A-Format-converted input and the B-Format output. */
    alignas(16) std::array<FloatBufferLine, NumLines> mABuffer{};
    alignas(16) FloatBufferLine mTempLine{};
    alignas(16) std::array<FloatBufferLine, NumLines> mBBuffer{};

    /* effect parameters */
    ChorusWaveform mWaveform{};
    int mDelay{0};
    float mDepth{0.0f};
    float mFeedback{0.0f};


    struct OutParams {
        unsigned mTargetChannel{InvalidChannelIndex.c_val};

        /* Current and target gain for this channel. */
        float mCurrentGain{};
        float mTargetGain{};
    };
    std::array<OutParams, NumLines> mChans;

    /* When the device is mixing to higher-order B-Format, the output needs
     * high-frequency adjustment and (potentially) mixing into higher order
     * channels to compensate.
     */
    struct UpsampleParams {
        float mHfScale{1.0f};
        BandSplitter mSplitter;
        std::array<float, MaxAmbiChannels> mCurrentGains{};
        std::array<float, MaxAmbiChannels> mTargetGains{};
    };
    std::optional<std::array<UpsampleParams, NumLines>> mUpsampler;


    void calcTriangleDelays(const size_t todo);
    void calcSinusoidDelays(const size_t todo);

    void deviceUpdate(const DeviceBase *device, const BufferStorage*) final;
    void update(const ContextBase *context, const EffectSlotBase *slot, const EffectProps *props_,
        const EffectTarget target) final;
    void process(const size_t samplesToDo, const std::span<const FloatBufferLine> samplesIn,
        const std::span<FloatBufferLine> samplesOut) final;
};


void ChorusState::deviceUpdate(const DeviceBase *device, const BufferStorage*)
{
    static constexpr auto MaxDelay = std::max(ChorusMaxDelay, FlangerMaxDelay);
    const auto frequency = static_cast<float>(device->mSampleRate);

    const auto maxlen = usize{NextPowerOf2(float2uint(MaxDelay*2.0f*frequency) + 1u)} * NumLines;
    if(maxlen != mDelayBuffers.size())
        decltype(mDelayBuffers)(maxlen).swap(mDelayBuffers);
    std::ranges::fill(mDelayBuffers, 0.0f);

    mChans.fill(OutParams{});
    mUpsampler.reset();

    if(device->mAmbiOrder > 1)
    {
        auto const hfscales = AmbiScale::GetHFOrderScales(1, device->mAmbiOrder,
            device->m2DMixing);
        auto idx = 0_uz;

        auto const splitter = BandSplitter{device->mXOverFreq
            / static_cast<float>(device->mSampleRate)};

        auto &upsampler = mUpsampler.emplace();
        for(auto &chandata : upsampler)
        {
            chandata.mHfScale = hfscales[idx];
            idx = 1;

            chandata.mSplitter = splitter;
        }
    }
}

void ChorusState::update(const ContextBase *context, const EffectSlotBase *slot,
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
        mLfoDisp = (mLfoRange*static_cast<unsigned>(phase) + 180) / 360;
    }

    mOutTarget = target.Main->Buffer;
    target.Main->setAmbiMixParams(slot->Wet, slot->Gain,
        [this](usize const idx, u8 const outchan, float const outgain)
    {
        if(idx < mChans.size())
        {
            mChans[idx].mTargetChannel = outchan.c_val;
            mChans[idx].mTargetGain = outgain;
        }
    });

    if(mUpsampler.has_value())
    {
        auto &upsampler = mUpsampler.value();
        const auto upmatrix = std::span{AmbiScale::FirstOrderUp};

        auto const outgain = slot->Gain;
        for(auto const idx : std::views::iota(0_uz, mChans.size()))
        {
            if(mChans[idx].mTargetChannel != InvalidChannelIndex)
                ComputePanGains(target.Main, upmatrix[idx], outgain, upsampler[idx].mTargetGains);
        }
    }
}


void ChorusState::calcTriangleDelays(const size_t todo)
{
    const auto lfo_range = mLfoRange;
    const auto lfo_scale = mLfoScale;
    const auto depth = mDepth;
    const auto delay = mDelay;

    auto gen_lfo = [lfo_scale,depth,delay](unsigned const offset) -> unsigned
    {
        const float offset_norm{static_cast<float>(offset) * lfo_scale};
        return static_cast<unsigned>(fastf2i((1.0f-std::abs(2.0f-offset_norm)) * depth) + delay);
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

    mLfoOffset = static_cast<unsigned>(mLfoOffset+todo) % lfo_range;
}

void ChorusState::calcSinusoidDelays(const size_t todo)
{
    const auto lfo_range = mLfoRange;
    const auto lfo_scale = mLfoScale;
    const auto depth = mDepth;
    const auto delay = mDelay;

    auto gen_lfo = [lfo_scale,depth,delay](unsigned const offset) -> unsigned
    {
        auto const offset_norm = float{static_cast<float>(offset) * lfo_scale};
        return static_cast<unsigned>(fastf2i(std::sin(offset_norm)*depth) + delay);
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

    mLfoOffset = static_cast<unsigned>(mLfoOffset+todo) % lfo_range;
}

void ChorusState::process(const size_t samplesToDo,
    const std::span<const FloatBufferLine> samplesIn, const std::span<FloatBufferLine> samplesOut)
{
    /* Convert B-Format to A-Format for processing. */
    const auto numInput = std::min(samplesIn.size(), NumLines);
    for(const auto c : std::views::iota(0_uz, NumLines))
    {
        const auto tmpspan = std::span{mABuffer[c]}.first(samplesToDo);
        std::ranges::fill(tmpspan, 0.0f);
        for(const auto i : std::views::iota(0_uz, numInput))
        {
            std::ranges::transform(tmpspan, samplesIn[i], tmpspan.begin(),
                [gain=B2A[c][i]](float const sample, float const in) noexcept -> float
            { return sample + in*gain; });
        }
    }

    /* Clear the B-Format buffer that accumulates the result. */
    for(auto &outbuf : mBBuffer)
        std::ranges::fill(outbuf | std::views::take(samplesToDo), 0.0f);

    if(mWaveform == ChorusWaveform::Sinusoid)
        calcSinusoidDelays(samplesToDo);
    else /*if(mWaveform == ChorusWaveform::Triangle)*/
        calcTriangleDelays(samplesToDo);

    const auto bufmask = mDelayBuffers.size()/NumLines - 1;
    const auto feedback = mFeedback;
    const auto avgdelay = (static_cast<unsigned>(mDelay) + MixerFracHalf) >> MixerFracBits;

    for(const auto c : std::views::iota(0_uz, NumLines))
    {
        auto const moddelays = (c < NumLines/2) ? std::span{mModDelays[0]}
            : std::span{mModDelays[1]};

        auto const delaybuf = std::span{mDelayBuffers}.subspan((bufmask+1)*c, bufmask+1);
        auto offset = mOffset;
        std::ranges::transform(mABuffer[c] | std::views::take(samplesToDo), moddelays,
            mTempLine.begin(),
            [bufmask, feedback, avgdelay, delaybuf, &offset](float const input,
                unsigned const moddelay)
        {
            /* Feed the buffer's input first (necessary for delays < 1). */
            delaybuf[offset&bufmask] = input;

            /* Tap for this output. */
            auto const delay = offset - (moddelay >> gCubicTable.sTableBits);
            auto const phase = moddelay & gCubicTable.sTableMask;
            auto const sample = delaybuf[(delay+1) & bufmask]*gCubicTable.getCoeff0(phase) +
                delaybuf[(delay  ) & bufmask]*gCubicTable.getCoeff1(phase) +
                delaybuf[(delay-1) & bufmask]*gCubicTable.getCoeff2(phase) +
                delaybuf[(delay-2) & bufmask]*gCubicTable.getCoeff3(phase);

            /* Accumulate feedback from the average delay of the taps. */
            delaybuf[offset&bufmask] += delaybuf[(offset-avgdelay) & bufmask] * feedback;
            ++offset;

            return sample;
        });

        for(const auto i : std::views::iota(0_uz, NumLines))
        {
            const auto tmpspan = std::span{mBBuffer[i]}.first(samplesToDo);
            std::ranges::transform(tmpspan, mTempLine, tmpspan.begin(),
                [gain=A2B[i][c]](float const sample, float const in) noexcept -> float
            { return sample + in*gain; });
        }
    }
    mOffset += samplesToDo;

    if(mUpsampler.has_value())
    {
        auto &upsampler = mUpsampler.value();
        auto chandata = mChans.begin();
        for(const auto c : std::views::iota(0_uz, NumLines))
        {
            auto &upchan = upsampler[c];
            if(chandata->mTargetChannel != InvalidChannelIndex)
            {
                auto src = std::span{mBBuffer[c]}.first(samplesToDo);
                upchan.mSplitter.processHfScale(src, src, upchan.mHfScale);
                MixSamples(src, samplesOut, upchan.mCurrentGains, upchan.mTargetGains, samplesToDo,
                    0);
            }
            ++chandata;
        }
    }
    else
    {
        auto chandata = mChans.begin();
        for(const auto c : std::views::iota(0_uz, NumLines))
        {
            if(auto const outidx = chandata->mTargetChannel; outidx != InvalidChannelIndex)
                MixSamples(std::span{mBBuffer[c]}.first(samplesToDo), samplesOut[outidx],
                    chandata->mCurrentGain, chandata->mTargetGain, samplesToDo);
            ++chandata;
        }
    }
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
