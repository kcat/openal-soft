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

using uint = unsigned int;
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
        std::ranges::transform(std::views::iota(0u, uint{HilHalfSize}), mData.begin(),
            [](const uint i) -> float
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

    /* Effects buffers */
    std::array<double,HilSize> mInFIFO{};
    std::array<complex_d,HilStep> mOutFIFO{};
    std::array<complex_d,HilSize> mOutputAccum{};
    std::array<complex_d,HilSize> mAnalytic{};
    std::array<complex_d,BufferLineSize> mOutdata{};

    alignas(16) FloatBufferLine mBufferOut{};

    /* Effect gains for each output channel */
    struct OutParams {
        uint mPhaseStep{};
        uint mPhase{};
        double mSign{};
        std::array<float,MaxAmbiChannels> Current{};
        std::array<float,MaxAmbiChannels> Target{};
    };
    std::array<OutParams,2> mChans;


    void deviceUpdate(const DeviceBase *device, const BufferStorage *buffer) override;
    void update(const ContextBase *context, const EffectSlotBase *slot, const EffectProps *props,
        const EffectTarget target) override;
    void process(const size_t samplesToDo, const std::span<const FloatBufferLine> samplesIn,
        const std::span<FloatBufferLine> samplesOut) override;
};

void FshifterState::deviceUpdate(const DeviceBase*, const BufferStorage*)
{
    /* (Re-)initializing parameters and clear the buffers. */
    mCount = 0;
    mPos = HilSize - HilStep;

    mInFIFO.fill(0.0);
    mOutFIFO.fill(complex_d{});
    mOutputAccum.fill(complex_d{});
    mAnalytic.fill(complex_d{});

    mBufferOut.fill(0.0f);
    mChans.fill(OutParams{});
}

void FshifterState::update(const ContextBase *context, const EffectSlotBase *slot,
    const EffectProps *props_, const EffectTarget target)
{
    auto &props = std::get<FshifterProps>(*props_);
    auto const device = al::get_not_null(context->mDevice);

    const auto step = props.Frequency / static_cast<float>(device->mSampleRate);
    std::ranges::fill(mChans | std::views::transform(&OutParams::mPhaseStep),
        fastf2u(std::min(step, 1.0f) * MixerFracOne));

    switch(props.LeftDirection)
    {
    case FShifterDirection::Down: mChans[0].mSign = -1.0; break;
    case FShifterDirection::Up: mChans[0].mSign = 1.0; break;
    case FShifterDirection::Off:
        mChans[0].mPhase     = 0;
        mChans[0].mPhaseStep = 0;
        break;
    }

    switch(props.RightDirection)
    {
    case FShifterDirection::Down: mChans[1].mSign = -1.0; break;
    case FShifterDirection::Up: mChans[1].mSign = 1.0; break;
    case FShifterDirection::Off:
        mChans[1].mPhase     = 0;
        mChans[1].mPhaseStep = 0;
        break;
    }

    static constexpr auto inv_sqrt2 = static_cast<float>(1.0 / std::numbers::sqrt2);
    static constexpr auto lcoeffs_pw = CalcDirectionCoeffs(std::array{-1.0f, 0.0f, 0.0f});
    static constexpr auto rcoeffs_pw = CalcDirectionCoeffs(std::array{ 1.0f, 0.0f, 0.0f});
    static constexpr auto lcoeffs_nrml = CalcDirectionCoeffs(std::array{-inv_sqrt2, 0.0f, inv_sqrt2});
    static constexpr auto rcoeffs_nrml = CalcDirectionCoeffs(std::array{ inv_sqrt2, 0.0f, inv_sqrt2});
    auto &lcoeffs = (device->mRenderMode != RenderMode::Pairwise) ? lcoeffs_nrml : lcoeffs_pw;
    auto &rcoeffs = (device->mRenderMode != RenderMode::Pairwise) ? rcoeffs_nrml : rcoeffs_pw;

    mOutTarget = target.Main->Buffer;
    ComputePanGains(target.Main, lcoeffs, slot->Gain, mChans[0].Target);
    ComputePanGains(target.Main, rcoeffs, slot->Gain, mChans[1].Target);
}

void FshifterState::process(const size_t samplesToDo,
    const std::span<const FloatBufferLine> samplesIn, const std::span<FloatBufferLine> samplesOut)
{
    for(auto base=0_uz;base < samplesToDo;)
    {
        const auto todo = std::min(HilStep-mCount, samplesToDo-base);

        /* Fill FIFO buffer with samples data */
        std::ranges::copy(samplesIn[0] | std::views::drop(base) | std::views::take(todo),
            (mInFIFO | std::views::drop(mPos+mCount)).begin());
        std::ranges::copy(mOutFIFO | std::views::drop(mCount) | std::views::take(todo),
            (mOutdata | std::views::drop(base)).begin());
        mCount += todo;
        base += todo;

        /* Check whether FIFO buffer is filled */
        if(mCount < HilStep) break;
        mCount = 0;
        mPos = (mPos+HilStep) & (HilSize-1);

        /* Real signal windowing and store in Analytic buffer */
        const auto [_, windowiter, analyticiter] = std::ranges::transform(
            mInFIFO | std::views::drop(mPos), gWindow.mData, mAnalytic.begin(), std::multiplies{});
        std::ranges::transform(mInFIFO.begin(), mInFIFO.end(), windowiter, gWindow.mData.end(),
            analyticiter, std::multiplies{});

        /* Processing signal by Discrete Hilbert Transform (analytical signal). */
        complex_hilbert(mAnalytic);

        /* Windowing and add to output accumulator */
        std::ranges::transform(mAnalytic, gWindow.mData, mAnalytic.begin(),
            [](const complex_d &a, const double w) noexcept { return 2.0/OversampleFactor*w*a; });

        const auto accumrange = mOutputAccum | std::views::drop(mPos);
        std::ranges::transform(accumrange, mAnalytic, accumrange.begin(), std::plus{});
        std::ranges::transform(mOutputAccum, mAnalytic | std::views::drop(HilSize-mPos),
            mOutputAccum.begin(), std::plus{});

        /* Copy out the accumulated result, then clear for the next iteration. */
        const auto outrange = accumrange | std::views::take(HilStep);
        std::ranges::copy(outrange, mOutFIFO.begin());
        std::ranges::fill(outrange, 0.0f);
    }

    /* Process frequency shifter using the analytic signal obtained. */
    std::ranges::for_each(mChans, [&,this](OutParams &params)
    {
        const auto sign = params.mSign;
        const auto phase_step = params.mPhaseStep;
        auto phase_idx = params.mPhase;
        std::ranges::transform(mOutdata | std::views::take(samplesToDo), mBufferOut.begin(),
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

        /* Now, mix the processed sound data to the output. */
        MixSamples(std::span{mBufferOut}.first(samplesToDo), samplesOut, params.Current,
            params.Target, std::max(samplesToDo, 512_uz), 0);
    });
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
