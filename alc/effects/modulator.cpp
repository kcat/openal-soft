/**
 * OpenAL cross platform audio library
 * Copyright (C) 2009 by Chris Robinson.
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
#include <cstdint>
#include <cstdlib>
#include <functional>
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
#include "gsl/gsl"
#include "intrusive_ptr.h"
#include "opthelpers.h"

struct BufferStorage;

namespace {

struct SinFunc {
    static auto Get(u32 const index, float const scale) noexcept(noexcept(std::sin(0.0f))) -> float
    { return std::sin(gsl::narrow_cast<float>(index) * scale); }
};

struct SawFunc {
    static constexpr auto Get(u32 const index, const float scale) noexcept -> float
    { return gsl::narrow_cast<float>(index)*scale - 1.0f; }
};

struct SquareFunc {
    static constexpr auto Get(u32 const index, const float scale) noexcept -> float
    { return gsl::narrow_cast<float>(gsl::narrow_cast<float>(index)*scale < 0.5f)*2.0f - 1.0f; }
};

struct OneFunc {
    static constexpr auto Get(u32 const, const float) noexcept -> float
    { return 1.0f; }
};


struct ModulatorState final : public EffectState {
    std::variant<OneFunc,SinFunc,SawFunc,SquareFunc> mSampleGen;

    u32 mIndex{0};
    u32 mRange{1};
    float mIndexScale{0.0f};

    alignas(16) FloatBufferLine mModSamples{};
    alignas(16) FloatBufferLine mBuffer{};

    struct OutParams {
        u32 mTargetChannel{InvalidChannelIndex.c_val};

        BiquadFilter mFilter;

        float mCurrentGain{};
        float mTargetGain{};
    };
    std::array<OutParams,MaxAmbiChannels> mChans;


    void deviceUpdate(const DeviceBase *device, const BufferStorage *buffer) override;
    void update(const ContextBase *context, const EffectSlotBase *slot, const EffectProps *props,
        const EffectTarget target) override;
    void process(const size_t samplesToDo, const std::span<const FloatBufferLine> samplesIn,
        const std::span<FloatBufferLine> samplesOut) override;
};

void ModulatorState::deviceUpdate(const DeviceBase*, const BufferStorage*)
{
    mChans.fill(OutParams{});
}

void ModulatorState::update(const ContextBase *context, const EffectSlotBase *slot,
    const EffectProps *props_, const EffectTarget target)
{
    auto &props = std::get<ModulatorProps>(*props_);
    auto const device = al::get_not_null(context->mDevice);
    auto const samplerate = static_cast<float>(device->mSampleRate);

    /* The effective frequency will be adjusted to have a whole number of
     * samples per cycle (at 48khz, that allows 8000, 6857.14, 6000, 5333.33,
     * 4800, etc). We could do better by using fixed-point stepping over a sin
     * function, with additive synthesis for the square and sawtooth waveforms,
     * but that may need a more efficient sin function since it needs to do
     * many iterations per sample.
     */
    const auto samplesPerCycle = props.Frequency > 0.0f
        ? samplerate/props.Frequency + 0.5f : 1.0f;
    const auto range = static_cast<u32>(std::clamp(samplesPerCycle, 1.0f, samplerate));
    mIndex = static_cast<u32>((u64{mIndex} * u64{range} / u64{mRange}).c_val);
    mRange = range;

    if(mRange == 1)
    {
        mIndexScale = 0.0f;
        mSampleGen.emplace<OneFunc>();
    }
    else if(props.Waveform == ModulatorWaveform::Sinusoid)
    {
        mIndexScale = std::numbers::pi_v<float>*2.0f / static_cast<float>(mRange);
        mSampleGen.emplace<SinFunc>();
    }
    else if(props.Waveform == ModulatorWaveform::Sawtooth)
    {
        mIndexScale = 2.0f / static_cast<float>(mRange-1);
        mSampleGen.emplace<SawFunc>();
    }
    else if(props.Waveform == ModulatorWaveform::Square)
    {
        /* For square wave, the range should be even (there should be an equal
         * number of high and low samples). An odd number of samples per cycle
         * would need a more complex value generator.
         */
        mRange = (mRange+1) & ~1u;
        mIndexScale = 1.0f / static_cast<float>(mRange-1);
        mSampleGen.emplace<SquareFunc>();
    }

    const auto f0norm = std::clamp(props.HighPassCutoff / samplerate, 1.0f/512.0f, 0.49f);
    /* Bandwidth value is constant in octaves. */
    mChans[0].mFilter.setParamsFromBandwidth(BiquadType::HighPass, f0norm, 1.0f, 0.75f);
    std::ranges::for_each(mChans | std::views::take(slot->Wet.Buffer.size()) | std::views::drop(1),
        [&other=mChans[0].mFilter](BiquadFilter &filter)
    { filter.copyParamsFrom(other); }, &OutParams::mFilter);

    mOutTarget = target.Main->Buffer;
    target.Main->setAmbiMixParams(slot->Wet, slot->Gain,
        [this](usize const idx, u8 const outchan, f32 const outgain)
    {
        mChans[idx].mTargetChannel = outchan.c_val;
        mChans[idx].mTargetGain = outgain;
    });
}

void ModulatorState::process(const size_t samplesToDo,
    const std::span<const FloatBufferLine> samplesIn, const std::span<FloatBufferLine> samplesOut)
{
    ASSUME(samplesToDo > 0);

    std::visit([this,samplesToDo]<typename T>(T&& type [[maybe_unused]])
    {
        using Modulator = std::remove_cvref_t<T>;
        const auto range = mRange;
        const auto scale = mIndexScale;
        auto index = mIndex;

        ASSUME(range > 1);

        auto moditer = mModSamples.begin();
        for(auto i = 0_uz;i < samplesToDo;)
        {
            const auto rem = std::min(static_cast<u32>(samplesToDo-i), range-index);
            moditer = std::ranges::transform(std::views::iota(index, index+rem), moditer,
                [scale](u32 const idx) { return Modulator::Get(idx, scale); }).out;

            i += rem;
            index += rem;
            if(index == range)
                index = 0;
        }
        mIndex = index;
    }, mSampleGen);

    auto chandata = mChans.begin();
    std::ranges::for_each(samplesIn, [&,this](const FloatConstBufferSpan input)
    {
        if(const size_t outidx{chandata->mTargetChannel}; outidx != InvalidChannelIndex)
        {
            chandata->mFilter.process(std::span{input}.first(samplesToDo), mBuffer);
            std::ranges::transform(mBuffer | std::views::take(samplesToDo), mModSamples,
                mBuffer.begin(), std::multiplies{});

            MixSamples(std::span{mBuffer}.first(samplesToDo), samplesOut[outidx],
                chandata->mCurrentGain, chandata->mTargetGain, std::min(samplesToDo, 64_uz));
        }
        ++chandata;
    });
}


struct ModulatorStateFactory final : public EffectStateFactory {
    al::intrusive_ptr<EffectState> create() override
    { return al::intrusive_ptr<EffectState>{new ModulatorState{}}; }
};

} // namespace

auto ModulatorStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>
{
    static ModulatorStateFactory ModulatorFactory{};
    return gsl::make_not_null(&ModulatorFactory);
}
