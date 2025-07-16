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
#include <functional>
#include <ranges>
#include <span>
#include <variant>

#include "alc/effects/base.h"
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

/*  The document  "Effects Extension Guide.pdf"  says that low and high  *
 *  frequencies are cutoff frequencies. This is not fully correct, they  *
 *  are corner frequencies for low and high shelf filters. If they were  *
 *  just cutoff frequencies, there would be no need in cutoff frequency  *
 *  gains, which are present.  Documentation for  "Creative Proteus X2"  *
 *  software describes  4-band equalizer functionality in a much better  *
 *  way.  This equalizer seems  to be a predecessor  of  OpenAL  4-band  *
 *  equalizer.  With low and high  shelf filters  we are able to cutoff  *
 *  frequencies below and/or above corner frequencies using attenuation  *
 *  gains (below 1.0) and amplify all low and/or high frequencies using  *
 *  gains above 1.0.                                                     *
 *                                                                       *
 *     Low-shelf       Low Mid Band      High Mid Band     High-shelf    *
 *      corner            center             center          corner      *
 *     frequency        frequency          frequency       frequency     *
 *    50Hz..800Hz     200Hz..3000Hz      1000Hz..8000Hz  4000Hz..16000Hz *
 *                                                                       *
 *          |               |                  |               |         *
 *          |               |                  |               |         *
 *   B -----+            /--+--\            /--+--\            +-----    *
 *   O      |\          |   |   |          |   |   |          /|         *
 *   O      | \        -    |    -        -    |    -        / |         *
 *   S +    |  \      |     |     |      |     |     |      /  |         *
 *   T      |   |    |      |      |    |      |      |    |   |         *
 * ---------+---------------+------------------+---------------+-------- *
 *   C      |   |    |      |      |    |      |      |    |   |         *
 *   U -    |  /      |     |     |      |     |     |      \  |         *
 *   T      | /        -    |    -        -    |    -        \ |         *
 *   O      |/          |   |   |          |   |   |          \|         *
 *   F -----+            \--+--/            \--+--/            +-----    *
 *   F      |               |                  |               |         *
 *          |               |                  |               |         *
 *                                                                       *
 * Gains vary from 0.126 up to 7.943, which means from -18dB attenuation *
 * up to +18dB amplification. Band width varies from 0.01 up to 1.0 in   *
 * octaves for two mid bands.                                            *
 *                                                                       *
 * Implementation is based on the "Cookbook formulae for audio EQ biquad *
 * filter coefficients" by Robert Bristow-Johnson                        *
 * http://www.musicdsp.org/files/Audio-EQ-Cookbook.txt                   */


struct EqualizerState final : public EffectState {
    struct OutParams {
        uint mTargetChannel{InvalidChannelIndex};

        /* Effect parameters */
        std::array<BiquadFilter,4> mFilter;

        /* Effect gains for each channel */
        float mCurrentGain{};
        float mTargetGain{};
    };
    std::array<OutParams,MaxAmbiChannels> mChans;

    alignas(16) FloatBufferLine mSampleBuffer{};


    void deviceUpdate(const DeviceBase *device, const BufferStorage *buffer) override;
    void update(const ContextBase *context, const EffectSlot *slot, const EffectProps *props,
        const EffectTarget target) override;
    void process(const size_t samplesToDo, const std::span<const FloatBufferLine> samplesIn,
        const std::span<FloatBufferLine> samplesOut) override;
};

void EqualizerState::deviceUpdate(const DeviceBase*, const BufferStorage*)
{
    mChans.fill(OutParams{});
}

void EqualizerState::update(const ContextBase *context, const EffectSlot *slot,
    const EffectProps *props_, const EffectTarget target)
{
    auto &props = std::get<EqualizerProps>(*props_);
    auto const device = al::get_not_null(context->mDevice);
    auto const frequency = static_cast<float>(device->mSampleRate);

    /* Calculate coefficients for the each type of filter. Note that the shelf
     * and peaking filters' gain is for the centerpoint of the transition band,
     * while the effect property gains are for the shelf/peak itself. So the
     * property gains need their dB halved (sqrt of linear gain) for the
     * shelf/peak to reach the provided gain.
     */
    auto gain = std::sqrt(props.LowGain);
    auto f0norm = props.LowCutoff / frequency;
    mChans[0].mFilter[0].setParamsFromSlope(BiquadType::LowShelf, f0norm, gain, 0.75f);

    gain = std::sqrt(props.Mid1Gain);
    f0norm = props.Mid1Center / frequency;
    mChans[0].mFilter[1].setParamsFromBandwidth(BiquadType::Peaking, f0norm, gain,
        props.Mid1Width);

    gain = std::sqrt(props.Mid2Gain);
    f0norm = props.Mid2Center / frequency;
    mChans[0].mFilter[2].setParamsFromBandwidth(BiquadType::Peaking, f0norm, gain,
        props.Mid2Width);

    gain = std::sqrt(props.HighGain);
    f0norm = props.HighCutoff / frequency;
    mChans[0].mFilter[3].setParamsFromSlope(BiquadType::HighShelf, f0norm, gain, 0.75f);

    /* Copy the filter coefficients for the other input channels. */
    std::ranges::for_each(mChans | std::views::take(slot->Wet.Buffer.size()) | std::views::drop(1),
        [filters=std::span{mChans[0].mFilter}](const std::span<BiquadFilter,4> targets)
    {
        targets[0].copyParamsFrom(filters[0]);
        targets[1].copyParamsFrom(filters[1]);
        targets[2].copyParamsFrom(filters[2]);
        targets[3].copyParamsFrom(filters[3]);
    }, &OutParams::mFilter);

    mOutTarget = target.Main->Buffer;
    target.Main->setAmbiMixParams(slot->Wet, slot->Gain,
        [this](const size_t idx, const uint outchan, const float outgain)
    {
        mChans[idx].mTargetChannel = outchan;
        mChans[idx].mTargetGain = outgain;
    });
}

void EqualizerState::process(const size_t samplesToDo,
    const std::span<const FloatBufferLine> samplesIn, const std::span<FloatBufferLine> samplesOut)
{
    const auto buffer = std::span{mSampleBuffer}.first(samplesToDo);
    auto chan = mChans.begin();
    std::ranges::for_each(samplesIn, [=,&chan](const FloatConstBufferSpan input)
    {
        if(const auto outidx = size_t{chan->mTargetChannel}; outidx != InvalidChannelIndex)
        {
            const auto inbuf = std::span{input}.first(samplesToDo);
            DualBiquad{chan->mFilter[0], chan->mFilter[1]}.process(inbuf, buffer);
            DualBiquad{chan->mFilter[2], chan->mFilter[3]}.process(buffer, buffer);

            MixSamples(buffer, samplesOut[outidx], chan->mCurrentGain, chan->mTargetGain,
                samplesToDo);
        }
        ++chan;
    });
}


struct EqualizerStateFactory final : public EffectStateFactory {
    al::intrusive_ptr<EffectState> create() override
    { return al::intrusive_ptr<EffectState>{new EqualizerState{}}; }
};

} // namespace

auto EqualizerStateFactory_getFactory() -> gsl::strict_not_null<EffectStateFactory*>
{
    static EqualizerStateFactory EqualizerFactory{};
    return gsl::make_not_null(&EqualizerFactory);
}
