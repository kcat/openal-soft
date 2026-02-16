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
#include <complex>
#include <cstdlib>
#include <numbers>
#include <ranges>
#include <span>
#include <variant>

#include "alc/effects/base.h"
#include "alcomplex.h"
#include "alnumeric.h"
#include "core/ambidefs.h"
#include "core/bufferline.h"
#include "core/context.h"
#include "core/device.h"
#include "core/effects/base.h"
#include "core/effectslot.h"
#include "core/mixer.h"
#include "core/mixer/defs.h"
#include "intrusive_ptr.h"


struct BufferStorage;

namespace {

constexpr auto NumLines = 4_uz;

/* The B-Format to A-Format conversion matrix. This produces a tetrahedral
 * array of discrete signals. Note, A0 and A1 are left-side responses while A2
 * and A3 are right-side responses, which is important to distinguish for the
 * Direction properties affecting the left output separately from the right
 * output.
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


using complex_d = std::complex<double>;

constexpr auto HilSize = 1024_uz;
constexpr auto HilHalfSize = HilSize >> 1;
constexpr auto OversampleFactor = 4_uz;

static_assert(HilSize%OversampleFactor == 0, "Factor must be a clean divisor of the size");
constexpr auto HilStep{HilSize / OversampleFactor};

/* Define a Hann window, used to filter the HIL input and output. */
struct Windower {
    alignas(16) std::array<double,HilSize> mData{};

    Windower() noexcept
    {
        static constexpr auto scale = std::numbers::pi / double{HilSize};
        /* Create lookup table of the Hann window for the desired size. */
        std::ranges::transform(std::views::iota(0u, unsigned{HilHalfSize}), mData.begin(),
            [](unsigned const i) -> float
        {
            const auto val = std::sin((i+0.5) * scale);
            return static_cast<float>(val * val);
        });
        std::ranges::copy(mData | std::views::take(HilHalfSize), mData.rbegin());
    }
};
const Windower gWindow{};


struct FshifterState final : public EffectState {
    /* Effect parameters */
    size_t mCount{};
    size_t mPos{};

    struct ProcessParams {
        /* Effects buffers */
        std::array<double, HilSize> mInFIFO{};
        std::array<complex_d, HilStep> mOutFIFO{};
        std::array<complex_d, HilSize> mOutputAccum{};
        std::array<complex_d, BufferLineSize> mOutdata{};

        unsigned mPhaseStep{};
        unsigned mPhase{};
        double mSign{1.0};

        unsigned mTargetChannel{InvalidChannelIndex.c_val};

        /* Current and target gain for this channel. */
        float mCurrentGain{};
        float mTargetGain{};
    };
    std::array<ProcessParams, NumLines> mChans;

    std::array<complex_d, HilSize> mAnalytic{};

    alignas(16) FloatBufferLine mTempLine{};
    alignas(16) std::array<FloatBufferLine, NumLines> mBBuffer{};

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

    void deviceUpdate(const DeviceBase *device, const BufferStorage *buffer) override;
    void update(const ContextBase *context, const EffectSlotBase *slot, const EffectProps *props,
        const EffectTarget target) override;
    void process(const size_t samplesToDo, const std::span<const FloatBufferLine> samplesIn,
        const std::span<FloatBufferLine> samplesOut) override;
};

void FshifterState::deviceUpdate(DeviceBase const *device, BufferStorage const*)
{
    /* (Re-)initializing parameters and clear the buffers. */
    mCount = 0;
    mPos = HilSize - HilStep;

    mChans.fill(ProcessParams{});
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

void FshifterState::update(const ContextBase *context, const EffectSlotBase *slot,
    const EffectProps *props_, const EffectTarget target)
{
    auto &props = std::get<FshifterProps>(*props_);
    auto const device = al::get_not_null(context->mDevice);

    const auto step = props.Frequency / static_cast<float>(device->mSampleRate);
    std::ranges::fill(mChans | std::views::transform(&ProcessParams::mPhaseStep),
        fastf2u(std::min(step, 1.0f) * MixerFracOne));

    auto const lchans = std::span{mChans}.first(2);
    switch(props.LeftDirection)
    {
    case FShifterDirection::Down:
        std::ranges::fill(lchans | std::views::transform(&ProcessParams::mSign), -1.0);
        break;
    case FShifterDirection::Up:
        std::ranges::fill(lchans | std::views::transform(&ProcessParams::mSign), 1.0);
        break;
    case FShifterDirection::Off:
        std::ranges::fill(lchans | std::views::transform(&ProcessParams::mPhase), 0);
        std::ranges::fill(lchans | std::views::transform(&ProcessParams::mPhaseStep), 0);
        break;
    }

    auto const rchans = std::span{mChans}.last(2);
    switch(props.RightDirection)
    {
    case FShifterDirection::Down:
        std::ranges::fill(rchans | std::views::transform(&ProcessParams::mSign), -1.0);
        break;
    case FShifterDirection::Up:
        std::ranges::fill(rchans | std::views::transform(&ProcessParams::mSign), 1.0);
        break;
    case FShifterDirection::Off:
        std::ranges::fill(rchans | std::views::transform(&ProcessParams::mPhase), 0);
        std::ranges::fill(rchans | std::views::transform(&ProcessParams::mPhaseStep), 0);
        break;
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

void FshifterState::process(const size_t samplesToDo,
    const std::span<const FloatBufferLine> samplesIn, const std::span<FloatBufferLine> samplesOut)
{
    /* Clear the B-Format buffer that accumulates the result. */
    for(auto &outbuf : mBBuffer)
        std::ranges::fill(outbuf | std::views::take(samplesToDo), 0.0f);

    auto const numInput = std::min(samplesIn.size(), NumLines);
    for(auto base=0_uz;base < samplesToDo;)
    {
        const auto todo = std::min(HilStep-mCount, samplesToDo-base);

        /* Fill FIFO buffer with samples data */
        for(const auto c : std::views::iota(0_uz, NumLines))
        {
            auto infifo = std::span{mChans[c].mInFIFO}.subspan(mPos+mCount, todo);
            /* Convert B-Format input to FIFO A-Format. */
            std::ranges::fill(infifo, 0.0);
            for(const auto i : std::views::iota(0_uz, numInput))
            {
                auto const input = std::span{samplesIn[i]}.subspan(base, todo);
                std::ranges::transform(infifo, input, infifo.begin(),
                    [gain=B2A[c][i]](double const sample, double const in) noexcept -> double
                { return sample + in*gain; });
            }

            /* Copy FIFO A-Format to output. */
            std::ranges::copy(std::span{mChans[c].mOutFIFO}.subspan(mCount, todo),
                std::span{mChans[c].mOutdata}.subspan(base).begin());
        }
        mCount += todo;
        base += todo;

        /* Check whether FIFO buffer is filled */
        if(mCount < HilStep) break;
        mCount = 0;
        mPos = (mPos+HilStep) & (HilSize-1);

        for(auto &chandata : mChans)
        {
            /* Real signal windowing and store in Analytic buffer */
            const auto [_, windowiter, analyticiter] = std::ranges::transform(
                chandata.mInFIFO | std::views::drop(mPos), gWindow.mData, mAnalytic.begin(),
                std::multiplies{});
            std::ranges::transform(chandata.mInFIFO.begin(), chandata.mInFIFO.end(), windowiter,
                gWindow.mData.end(), analyticiter, std::multiplies{});

            /* Processing signal by Discrete Hilbert Transform (analytical signal). */
            complex_hilbert(mAnalytic);

            /* Windowing and add to output accumulator */
            std::ranges::transform(mAnalytic, gWindow.mData, mAnalytic.begin(),
                [](const complex_d &a, const double w)  { return 2.0/OversampleFactor*w*a; });

            const auto accumrange = chandata.mOutputAccum | std::views::drop(mPos);
            std::ranges::transform(accumrange, mAnalytic, accumrange.begin(), std::plus{});
            std::ranges::transform(chandata.mOutputAccum, std::span{mAnalytic}.last(mPos),
                chandata.mOutputAccum.begin(), std::plus{});

            /* Copy out the accumulated result, then clear for the next iteration. */
            const auto outrange = accumrange | std::views::take(HilStep);
            std::ranges::copy(outrange, chandata.mOutFIFO.begin());
            std::ranges::fill(outrange, 0.0f);
        }
    }

    /* Process frequency shifter using the analytic signal obtained and store
     * in the B-Format buffer.
     */
    for(const auto c : std::views::iota(0_uz, NumLines))
    {
        auto &params = mChans[c];
        const auto sign = params.mSign;
        const auto phase_step = params.mPhaseStep;
        auto phase_idx = params.mPhase;
        std::ranges::transform(params.mOutdata | std::views::take(samplesToDo), mTempLine.begin(),
            [&phase_idx,phase_step,sign](const complex_d &in) -> float
        {
            const auto phase = phase_idx * (std::numbers::pi*2.0 / MixerFracOne);
            const auto out = static_cast<float>(in.real()*std::cos(phase) +
                in.imag()*std::sin(phase)*sign);

            phase_idx += phase_step;
            phase_idx &= MixerFracMask;
            return out;
        });
        params.mPhase = phase_idx;

        for(const auto i : std::views::iota(0_uz, NumLines))
        {
            const auto tmpspan = std::span{mBBuffer[i]}.first(samplesToDo);
            std::ranges::transform(tmpspan, mTempLine, tmpspan.begin(),
                [gain=A2B[i][c]](float const sample, float const in) noexcept -> float
            { return sample + in*gain; });
        }
    }

    /* Now, mix the processed sound data to the output. */
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


struct FshifterStateFactory final : public EffectStateFactory {
    al::intrusive_ptr<EffectState> create() override
    { return al::intrusive_ptr<EffectState>{new FshifterState{}}; }
};

} // namespace

auto FshifterStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>
{
    static FshifterStateFactory FshifterFactory{};
    return gsl::make_not_null(&FshifterFactory);
}
