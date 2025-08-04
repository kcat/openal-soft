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
#include "core/mixer/defs.h"
#include "intrusive_ptr.h"

struct BufferStorage;

namespace {

struct DistortionState final : public EffectState {
    /* Effect gains for each channel */
    std::array<float,MaxAmbiChannels> mGain{};

    /* Effect parameters */
    BiquadFilter mLowpass;
    BiquadFilter mBandpass;
    float mEdgeCoeff{};

    alignas(16) std::array<FloatBufferLine,2> mBuffer{};


    void deviceUpdate(const DeviceBase *device, const BufferStorage *buffer) override;
    void update(const ContextBase *context, const EffectSlot *slot, const EffectProps *props,
        const EffectTarget target) override;
    void process(const size_t samplesToDo, const std::span<const FloatBufferLine> samplesIn,
        const std::span<FloatBufferLine> samplesOut) override;
};

void DistortionState::deviceUpdate(const DeviceBase*, const BufferStorage*)
{
    mLowpass.clear();
    mBandpass.clear();
}

void DistortionState::update(const ContextBase *context, const EffectSlot *slot,
    const EffectProps *props_, const EffectTarget target)
{
    auto &props = std::get<DistortionProps>(*props_);
    auto const device = al::get_not_null(context->mDevice);

    /* Store waveshaper edge settings. */
    const auto edge = std::min(std::sin(std::numbers::pi_v<float>*0.5f * props.Edge), 0.99f);
    mEdgeCoeff = 2.0f * edge / (1.0f-edge);

    auto cutoff = props.LowpassCutoff;
    /* Bandwidth value is constant in octaves. */
    auto bandwidth = (cutoff * 0.5f) / (cutoff * 0.67f);
    /* Divide normalized frequency by the amount of oversampling done during
     * processing.
     */
    auto frequency = static_cast<float>(device->mSampleRate);
    mLowpass.setParamsFromBandwidth(BiquadType::LowPass, cutoff/frequency*0.25f, 1.0f, bandwidth);

    cutoff = props.EQCenter;
    /* Convert bandwidth in Hz to octaves. */
    bandwidth = props.EQBandwidth / (cutoff * 0.67f);
    mBandpass.setParamsFromBandwidth(BiquadType::BandPass, cutoff/frequency*0.25f, 1.0f,bandwidth);

    static constexpr auto coeffs = CalcDirectionCoeffs(std::array{0.0f, 0.0f, -1.0f});

    mOutTarget = target.Main->Buffer;
    ComputePanGains(target.Main, coeffs, slot->Gain*props.Gain, mGain);
}

void DistortionState::process(const size_t samplesToDo,
    const std::span<const FloatBufferLine> samplesIn, const std::span<FloatBufferLine> samplesOut)
{
    for(auto base=0_uz;base < samplesToDo;)
    {
        /* Perform 4x oversampling to avoid aliasing. Oversampling greatly
         * improves distortion quality and allows to implement lowpass and
         * bandpass filters using high frequencies, at which classic IIR
         * filters became unstable.
         */
        auto todo = std::min(BufferLineSize, (samplesToDo-base) * 4_uz);

        /* Fill oversample buffer using zero stuffing. Multiply the sample by
         * the amount of oversampling to maintain the signal's power.
         */
        for(size_t i{0u};i < todo;i++)
            mBuffer[0][i] = !(i&3) ? samplesIn[0][(i>>2)+base] * 4.0f : 0.0f;

        /* First step, do lowpass filtering of original signal. Additionally
         * perform buffer interpolation and lowpass cutoff for oversampling
         * (which is fortunately first step of distortion). So combine three
         * operations into the one.
         */
        mLowpass.process(std::span{mBuffer[0]}.first(todo), mBuffer[1]);

        /* Second step, do distortion using waveshaper function to emulate
         * signal processing during tube overdriving. Three steps of
         * waveshaping are intended to modify waveform without boost/clipping/
         * attenuation process.
         */
        std::ranges::transform(mBuffer[1] | std::views::take(todo), mBuffer[0].begin(),
            [fc=mEdgeCoeff](float smp) -> float
        {
            smp = (1.0f + fc) * smp/(1.0f + fc*std::fabs(smp));
            smp = (1.0f + fc) * smp/(1.0f + fc*std::fabs(smp)) * -1.0f;
            smp = (1.0f + fc) * smp/(1.0f + fc*std::fabs(smp));
            return smp;
        });

        /* Third step, do bandpass filtering of distorted signal. */
        mBandpass.process(std::span{mBuffer[0]}.first(todo), mBuffer[1]);

        todo >>= 2;
        auto outgains = mGain.cbegin();
        std::ranges::for_each(samplesOut, [this,base,todo,&outgains](const FloatBufferSpan output)
        {
            /* Fourth step, final, do attenuation and perform decimation,
             * storing only one sample out of four.
             */
            const auto gain = *(outgains++);
            if(!(std::fabs(gain) > GainSilenceThreshold))
                return;

            auto src = mBuffer[1].cbegin();
            const auto dst = std::span{output}.subspan(base, todo);
            std::ranges::transform(dst, dst.begin(), [gain,&src](float sample) noexcept -> float
            {
                sample += *src * gain;
                std::advance(src, 4);
                return sample;
            });
        });

        base += todo;
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
