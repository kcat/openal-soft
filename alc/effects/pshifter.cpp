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
#include "alnumeric.h"
#include "core/ambidefs.h"
#include "core/bufferline.h"
#include "core/device.h"
#include "core/effects/base.h"
#include "core/effectslot.h"
#include "core/mixer.h"
#include "core/mixer/defs.h"
#include "intrusive_ptr.h"
#include "pffft.h"

struct BufferStorage;
struct ContextBase;


namespace {

constexpr auto NumLines = 4_uz;

/* The B-Format to A-Format conversion matrix. This produces a tetrahedral
 * array of discrete signals, weighted to avoid inverse ghosting since relative
 * phase is lost for the out-of-phase response to sum correctly.
 */
constexpr auto DecodeCoeff = static_cast<float>(0.25 / 3.0);
alignas(16) constexpr std::array<std::array<float, NumLines>, NumLines> B2A{{
    /*   W          Y             Z             X      */
    {{ 0.25f,  DecodeCoeff,  DecodeCoeff,  DecodeCoeff }}, /* A0 */
    {{ 0.25f, -DecodeCoeff, -DecodeCoeff,  DecodeCoeff }}, /* A1 */
    {{ 0.25f,  DecodeCoeff, -DecodeCoeff, -DecodeCoeff }}, /* A2 */
    {{ 0.25f, -DecodeCoeff,  DecodeCoeff, -DecodeCoeff }}  /* A3 */
}};

/* Converts A-Format to B-Format for output. */
constexpr auto EncodeCoeff = static_cast<float>(0.5 * 3.0);
alignas(16) constexpr std::array<std::array<float, NumLines>, NumLines> A2B{{
    /*     A0            A1            A2            A3      */
    {{        1.0f,         1.0f,         1.0f,         1.0f }}, /* W */
    {{ EncodeCoeff, -EncodeCoeff,  EncodeCoeff, -EncodeCoeff }}, /* Y */
    {{ EncodeCoeff, -EncodeCoeff, -EncodeCoeff,  EncodeCoeff }}, /* Z */
    {{ EncodeCoeff,  EncodeCoeff, -EncodeCoeff, -EncodeCoeff }}  /* X */
}};


using complex_f = std::complex<float>;

constexpr auto StftSize = 1024_uz;
constexpr auto StftHalfSize = StftSize >> 1;
constexpr auto OversampleFactor = 8_uz;

static_assert(StftSize%OversampleFactor == 0, "Factor must be a clean divisor of the size");
constexpr auto StftStep = StftSize / OversampleFactor;

/* Define a Hann window, used to filter the STFT input and output. */
struct Windower {
    alignas(16) std::array<float,StftSize> mData{};

    Windower() noexcept
    {
        static constexpr auto scale = std::numbers::pi / double{StftSize};
        /* Create lookup table of the Hann window for the desired size. */
        std::ranges::transform(std::views::iota(0u, unsigned{StftHalfSize}), mData.begin(),
            [](unsigned const i) -> float
        {
            const auto val = std::sin((i+0.5) * scale);
            return static_cast<float>(val * val);
        });
        std::ranges::copy(mData | std::views::take(StftHalfSize), mData.rbegin());
    }
};
const Windower gWindow{};


struct FrequencyBin {
    float Magnitude;
    float FreqBin;
};


struct PshifterState final : public EffectState {
    /* Effect parameters */
    usize mCount{};
    usize mPos{};
    unsigned mPitchShiftI{};
    float mPitchShift{};

    PFFFTSetup mFft;
    alignas(16) std::array<float,StftSize> mFftBuffer{};
    alignas(16) std::array<float,StftSize> mFftWorkBuffer{};

    std::array<FrequencyBin,StftHalfSize+1> mAnalysisBuffer{};
    std::array<FrequencyBin,StftHalfSize+1> mSynthesisBuffer{};

    struct ProcessParams {
        /* Effects buffers */
        std::array<float, StftSize> mFIFO{};
        std::array<float, StftHalfSize+1> mLastPhase{};
        std::array<float, StftHalfSize+1> mSumPhase{};
        std::array<float, StftSize> mOutputAccum{};

        unsigned mTargetChannel{InvalidChannelIndex.c_val};

        /* Current and target gain for this channel. */
        float mCurrentGain{};
        float mTargetGain{};
    };
    std::array<ProcessParams, NumLines> mChans;

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
        EffectTarget target) override;
    void process(size_t samplesToDo, std::span<const FloatBufferLine> samplesIn,
        std::span<FloatBufferLine> samplesOut) override;
};

void PshifterState::deviceUpdate(DeviceBase const *device, BufferStorage const*)
{
    if(!mFft)
        mFft = PFFFTSetup{StftSize, PFFFT_REAL};

    /* (Re-)initializing parameters and clear the buffers. */
    mCount       = 0;
    mPos         = StftSize - StftStep;
    mPitchShiftI = MixerFracOne;
    mPitchShift  = 1.0f;

    mChans.fill(ProcessParams{});
    mFftBuffer.fill(0.0f);
    mAnalysisBuffer.fill(FrequencyBin{});
    mSynthesisBuffer.fill(FrequencyBin{});

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

void PshifterState::update(const ContextBase*, const EffectSlotBase *slot,
    const EffectProps *props_, const EffectTarget target)
{
    auto &props = std::get<PshifterProps>(*props_);
    const auto tune = props.CoarseTune*100 + props.FineTune;
    const auto pitch = std::pow(2.0f, static_cast<float>(tune) / 1200.0f);
    mPitchShiftI = fastf2u(std::clamp(pitch, 0.5f, 2.0f)*MixerFracOne);
    mPitchShift = static_cast<float>(mPitchShiftI) * float{1.0f/MixerFracOne};

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

void PshifterState::process(const size_t samplesToDo,
    const std::span<const FloatBufferLine> samplesIn, const std::span<FloatBufferLine> samplesOut)
{
    /* Pitch shifter engine based on the work of Stephan Bernsee.
     * http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/
     */

    /* Cycle offset per update expected of each frequency bin (bin 0 is none,
     * bin 1 is x1, bin 2 is x2, etc).
     */
    static constexpr auto expected_cycles = std::numbers::pi_v<float>*2.0f / OversampleFactor;

    /* Clear the B-Format buffer that accumulates the result. */
    for(auto &outbuf : mBBuffer)
        std::ranges::fill(outbuf | std::views::take(samplesToDo), 0.0f);

    auto const numInput = std::min(samplesIn.size(), NumLines);
    for(auto base = 0_uz;base < samplesToDo;)
    {
        const auto todo = std::min(StftStep-mCount, samplesToDo-base);

        /* Retrieve the output samples from the FIFO and fill in the new input
         * samples.
         */
        for(const auto c : std::views::iota(0_uz, NumLines))
        {
            auto fiforange = std::span{mChans[c].mFIFO}.subspan(mPos+mCount, todo);
            /* Convert FIFO A-Format to B-Format output. */
            for(const auto i : std::views::iota(0_uz, NumLines))
            {
                const auto tmpspan = std::span{mBBuffer[i]}.subspan(base, todo);
                std::ranges::transform(tmpspan, fiforange, tmpspan.begin(),
                    [gain=A2B[i][c]](float const sample, float const in) noexcept -> float
                { return sample + in*gain; });
            }

            /* Convert B-Format input to FIFO A-Format. */
            std::ranges::fill(fiforange, 0.0f);
            for(const auto i : std::views::iota(0_uz, numInput))
            {
                auto const input = std::span{samplesIn[i]}.subspan(base, todo);
                std::ranges::transform(fiforange, input, fiforange.begin(),
                    [gain=B2A[c][i]](float const sample, float const in) noexcept -> float
                { return sample + in*gain; });
            }
        }
        mCount += todo;
        base += todo;

        /* Check whether FIFO buffer is filled with new samples. */
        if(mCount < StftStep) break;
        mCount = 0;
        mPos = (mPos+StftStep) & (StftSize-1);

        for(auto &chandata : mChans)
        {
            auto fifo = std::span{chandata.mFIFO};

            /* Time-domain signal windowing, store in FftBuffer, and apply a
             * forward FFT to get the frequency-domain signal.
             */
            const auto [_, windowiter, fftbufiter] = std::ranges::transform(
                fifo | std::views::drop(mPos), gWindow.mData, mFftBuffer.begin(),
                std::multiplies{});
            std::ranges::transform(fifo.begin(), fifo.end(), windowiter, gWindow.mData.end(),
                fftbufiter, std::multiplies{});

            mFft.transform_ordered(mFftBuffer.begin(), mFftBuffer.begin(), mFftWorkBuffer.begin(),
                PFFFT_FORWARD);

            /* Analyze the obtained data. Since the real FFT is symmetric, only
             * StftHalfSize+1 samples are needed.
             */
            auto lastPhase = std::span{chandata.mLastPhase};
            for(auto k = 0_uz;k < StftHalfSize+1;++k)
            {
                const auto cplx = (k == 0) ? complex_f{mFftBuffer[0]} :
                    (k == StftHalfSize) ? complex_f{mFftBuffer[1]} :
                    complex_f{mFftBuffer[k*2], mFftBuffer[k*2 + 1]};
                const auto magnitude = std::abs(cplx);
                const auto phase = std::arg(cplx);

                /* Compute the phase difference from the last update and subtract
                 * the expected phase difference for this bin.
                 *
                 * When oversampling, the expected per-update offset increments by
                 * 1/OversampleFactor for every frequency bin. So, the offset wraps
                 * every 'OversampleFactor' bin.
                 */
                const auto bin_offset = static_cast<float>(k % OversampleFactor);
                auto tmp = (phase - lastPhase[k]) - bin_offset*expected_cycles;
                /* Store the actual phase for the next update. */
                lastPhase[k] = phase;

                /* Normalize from pi, and wrap the delta between -1 and +1. */
                tmp *= std::numbers::inv_pi_v<float>;
                auto qpd = float2int(tmp);
                tmp -= static_cast<float>(qpd + (qpd%2));

                /* Get deviation from bin frequency (-0.5 to +0.5), and account for
                 * oversampling.
                 */
                tmp *= 0.5f * OversampleFactor;

                /* Compute the k-th partials' frequency bin target and store the
                 * magnitude and frequency bin in the analysis buffer. We don't
                 * need the "true frequency" since it's a linear relationship with
                 * the bin.
                 */
                mAnalysisBuffer[k].Magnitude = magnitude;
                mAnalysisBuffer[k].FreqBin = static_cast<float>(k) + tmp;
            }

            /* Shift the frequency bins according to the pitch adjustment,
             * accumulating the magnitudes of overlapping frequency bins.
             */
            mSynthesisBuffer.fill(FrequencyBin{});

            constexpr auto bin_limit = size_t{((StftHalfSize+1)<<MixerFracBits) - MixerFracHalf - 1};
            const auto bin_count = size_t{std::min(StftHalfSize+1, bin_limit/mPitchShiftI + 1)};
            for(auto k = 0_uz;k < bin_count;++k)
            {
                const auto j = (k*mPitchShiftI + MixerFracHalf) >> MixerFracBits;

                /* If more than two bins end up together, use the target frequency
                 * bin for the one with the dominant magnitude. There might be a
                 * better way to handle this, but it's better than last-index-wins.
                 */
                if(mAnalysisBuffer[k].Magnitude > mSynthesisBuffer[j].Magnitude)
                    mSynthesisBuffer[j].FreqBin = mAnalysisBuffer[k].FreqBin * mPitchShift;
                mSynthesisBuffer[j].Magnitude += mAnalysisBuffer[k].Magnitude;
            }

            /* Reconstruct the frequency-domain signal from the adjusted frequency
             * bins.
             */
            auto sumPhase = std::span{chandata.mSumPhase};
            for(auto k = 0_uz;k < StftHalfSize+1;++k)
            {
                /* Calculate the actual delta phase for this bin's target frequency
                 * bin, and accumulate it to get the actual bin phase.
                 */
                auto tmp = sumPhase[k] + mSynthesisBuffer[k].FreqBin*expected_cycles;

                /* Wrap between -pi and +pi for the sum. If mSumPhase is left to
                 * grow indefinitely, it will lose precision and produce less exact
                 * phase over time.
                 */
                tmp *= std::numbers::inv_pi_v<float>;
                auto qpd = float2int(tmp);
                tmp -= static_cast<float>(qpd + (qpd%2));
                sumPhase[k] = tmp * std::numbers::pi_v<float>;

                const auto cplx = std::polar(mSynthesisBuffer[k].Magnitude, sumPhase[k]);
                if(k == 0)
                    mFftBuffer[0] = cplx.real();
                else if(k == StftHalfSize)
                    mFftBuffer[1] = cplx.real();
                else
                {
                    mFftBuffer[k*2 + 0] = cplx.real();
                    mFftBuffer[k*2 + 1] = cplx.imag();
                }
            }

            /* Apply an inverse FFT to get the time-domain signal, and accumulate
             * for the output with windowing.
             */
            mFft.transform_ordered(mFftBuffer.begin(), mFftBuffer.begin(), mFftWorkBuffer.begin(),
                PFFFT_BACKWARD);

            static constexpr auto scale = float{3.0f / OversampleFactor / StftSize};
            std::ranges::transform(mFftBuffer, gWindow.mData, mFftBuffer.begin(),
                [](const float a, const float w) noexcept { return w*a*scale; });

            auto outputAccum = std::span{chandata.mOutputAccum};
            const auto accumrange = outputAccum | std::views::drop(mPos);
            std::ranges::transform(accumrange, mFftBuffer, accumrange.begin(), std::plus{});
            std::ranges::transform(outputAccum, mFftBuffer | std::views::drop(StftSize-mPos),
                outputAccum.begin(), std::plus{});

            /* Copy out the accumulated result, then clear for the next iteration. */
            const auto outrange = accumrange | std::views::take(StftStep);
            std::ranges::copy(outrange, (fifo | std::views::drop(mPos)).begin());
            std::ranges::fill(outrange, 0.0f);
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


struct PshifterStateFactory final : public EffectStateFactory {
    al::intrusive_ptr<EffectState> create() override
    { return al::intrusive_ptr<EffectState>{new PshifterState{}}; }
};

} // namespace

auto PshifterStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>
{
    static PshifterStateFactory PshifterFactory{};
    return gsl::make_not_null(&PshifterFactory);
}
