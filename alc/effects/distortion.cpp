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
#include "core/filters/biquad.h"
#include "core/mixer.h"
#include "intrusive_ptr.h"

struct BufferStorage;

namespace {

constexpr auto NumLines = 4_uz;

/* The B-Format to A-Format conversion matrix. This produces a tetrahedral
 * array of discrete signals.
 */
constexpr auto DecodeCoeff = static_cast<float>(0.25 / std::numbers::sqrt3);
alignas(16) constexpr std::array<std::array<float, NumLines>, NumLines> B2A{{
    /*   W          Y             Z             X      */
    {{ 0.25f,  DecodeCoeff,  DecodeCoeff,  DecodeCoeff }}, /* A0 */
    {{ 0.25f, -DecodeCoeff, -DecodeCoeff,  DecodeCoeff }}, /* A1 */
    {{ 0.25f,  DecodeCoeff, -DecodeCoeff, -DecodeCoeff }}, /* A2 */
    {{ 0.25f, -DecodeCoeff,  DecodeCoeff, -DecodeCoeff }}  /* A3 */
}};

/* Converts A-Format to B-Format for output. */
constexpr auto EncodeCoeff = static_cast<float>(0.5 * std::numbers::sqrt3);
alignas(16) constexpr std::array<std::array<float, NumLines>, NumLines> A2B{{
    /*     A0            A1            A2            A3      */
    {{        1.0f,         1.0f,         1.0f,         1.0f }}, /* W */
    {{ EncodeCoeff, -EncodeCoeff,  EncodeCoeff, -EncodeCoeff }}, /* Y */
    {{ EncodeCoeff, -EncodeCoeff, -EncodeCoeff,  EncodeCoeff }}, /* Z */
    {{ EncodeCoeff,  EncodeCoeff, -EncodeCoeff, -EncodeCoeff }}  /* X */
}};


struct DistortionState final : public EffectState {
    struct OutParams {
        unsigned mTargetChannel{InvalidChannelIndex.c_val};

        /* Effect parameters */
        BiquadFilter mLowpass;
        BiquadFilter mBandpass;

        /* Effect gains for each channel */
        float mCurrentGain{};
        float mTargetGain{};
    };
    std::array<OutParams, NumLines> mChans;

    float mEdgeCoeff{};

    alignas(16) std::array<FloatBufferLine, NumLines> mABuffer{};
    alignas(16) std::array<FloatBufferLine, NumLines> mBBuffer{};

    alignas(16) std::array<FloatBufferLine, 2> mTempBuffer{};

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

void DistortionState::deviceUpdate(DeviceBase const *const device, const BufferStorage*)
{
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

void DistortionState::update(const ContextBase *context, const EffectSlotBase *slot,
    const EffectProps *props_, const EffectTarget target)
{
    auto &props = std::get<DistortionProps>(*props_);
    auto const device = al::get_not_null(context->mDevice);

    /* Store waveshaper edge settings. */
    const auto edge = std::min(std::sin(std::numbers::pi_v<float>*0.5f * props.Edge), 0.99f);
    mEdgeCoeff = 2.0f * edge / (1.0f-edge);

    auto cutoff = props.LowpassCutoff;
    /* Bandwidth value is constant in octaves. */
    auto bandwidth = 0.746268656716f; /* (cutoff * 0.5f) / (cutoff * 0.67f) */
    /* Divide normalized frequency by the amount of oversampling done during
     * processing.
     */
    auto frequency = static_cast<float>(device->mSampleRate);
    mChans[0].mLowpass.setParamsFromBandwidth(BiquadType::LowPass, cutoff/frequency*0.25f, 1.0f,
        bandwidth);

    cutoff = props.EQCenter;
    /* Convert bandwidth in Hz to octaves. */
    bandwidth = props.EQBandwidth / (cutoff * 0.67f);
    mChans[0].mBandpass.setParamsFromBandwidth(BiquadType::BandPass, cutoff/frequency*0.25f, 1.0f,
        bandwidth);

    for(auto &chandata : mChans | std::views::drop(1))
    {
        chandata.mLowpass.copyParamsFrom(mChans[0].mLowpass);
        chandata.mBandpass.copyParamsFrom(mChans[0].mBandpass);
    }

    mOutTarget = target.Main->Buffer;
    target.Main->setAmbiMixParams(slot->Wet, slot->Gain*props.Gain,
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

        auto const outgain = slot->Gain * props.Gain;
        for(auto const idx : std::views::iota(0_uz, mChans.size()))
        {
            if(mChans[idx].mTargetChannel != InvalidChannelIndex)
                ComputePanGains(target.Main, upmatrix[idx], outgain, upsampler[idx].mTargetGains);
        }
    }
}

void DistortionState::process(const size_t samplesToDo,
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

    for(auto base=0_uz;base < samplesToDo;)
    {
        /* Perform 4x oversampling to avoid aliasing. Oversampling greatly
         * improves distortion quality and allows to implement lowpass and
         * bandpass filters using high frequencies, at which classic IIR
         * filters became unstable.
         */
        auto const todo = std::min(BufferLineSize, (samplesToDo-base) * 4_uz);

        for(const auto c : std::views::iota(0_uz, NumLines))
        {
            /* Fill oversample buffer using zero stuffing. Multiply the sample
             * by the amount of oversampling to maintain the signal's power.
             */
            for(size_t i{0u};i < todo;i++)
                mTempBuffer[0][i] = !(i&3) ? mABuffer[c][(i>>2)+base] * 4.0f : 0.0f;

            /* First step, do lowpass filtering of original signal. Also
             * perform buffer interpolation and lowpass cutoff for oversampling
             * (which is fortunately first step of distortion). So combine
             * three operations into the one.
             */
            mChans[c].mLowpass.process(std::span{mTempBuffer[0]}.first(todo), mTempBuffer[1]);

            /* Second step, do distortion using waveshaper function to emulate
             * signal processing during tube overdriving. Three steps of
             * waveshaping are intended to modify waveform without boost/
             * clipping/attenuation process.
             */
            std::ranges::transform(mTempBuffer[1] | std::views::take(todo), mTempBuffer[0].begin(),
                [fc=mEdgeCoeff](float smp) -> float
            {
                smp = ( 1.0f + fc) * smp/(1.0f + fc*std::fabs(smp));
                smp = (-1.0f - fc) * smp/(1.0f + fc*std::fabs(smp));
                smp = ( 1.0f + fc) * smp/(1.0f + fc*std::fabs(smp));
                return smp;
            });

            /* Third step, do bandpass filtering of distorted signal. */
            mChans[c].mBandpass.process(std::span{mTempBuffer[0]}.first(todo), mTempBuffer[1]);

            /* Fourth step, convert A-Format to B-Format and decimate, storing
             * only one sample out of four.
             */
            for(const auto i : std::views::iota(0_uz, NumLines))
            {
                auto src = mTempBuffer[1].cbegin();
                const auto dst = std::span{mBBuffer[i]}.subspan(base, todo >> 2);
                std::ranges::transform(dst, dst.begin(),
                    [gain=A2B[i][c], &src](float sample) noexcept
                {
                    sample += *src * gain;
                    std::advance(src, 4);
                    return sample;
                });
            }
        }

        base += todo >> 2;
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


struct DistortionStateFactory final : public EffectStateFactory {
    al::intrusive_ptr<EffectState> create() override
    { return al::intrusive_ptr<EffectState>{new DistortionState{}}; }
};

} // namespace

auto DistortionStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>
{
    static DistortionStateFactory DistortionFactory{};
    return gsl::make_not_null(&DistortionFactory);
}
