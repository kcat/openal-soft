/**
 * OpenAL cross platform audio library
 * Copyright (C) 2011 by Chris Robinson.
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
#include <cstdlib>
#include <variant>

#include "alc/effects/base.h"
#include "alspan.h"
#include "core/bufferline.h"
#include "core/devformat.h"
#include "core/device.h"
#include "core/effects/base.h"
#include "core/effectslot.h"
#include "core/mixer.h"
#include "intrusive_ptr.h"

struct BufferStorage;
struct ContextBase;


namespace {

using uint = unsigned int;

struct DedicatedState : public EffectState {
    /* The "dedicated" effect can output to the real output, so should have
     * gains for all possible output channels and not just the main ambisonic
     * buffer.
     */
    std::array<float,MaxOutputChannels> mCurrentGains{};
    std::array<float,MaxOutputChannels> mTargetGains{};


    void deviceUpdate(const DeviceBase *device, const BufferStorage *buffer) final;
    void update(const ContextBase *context, const EffectSlot *slot, const EffectProps *props,
        const EffectTarget target) override;
    void process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn,
        const al::span<FloatBufferLine> samplesOut) final;
};

struct DedicatedLfeState final : public DedicatedState {
    void update(const ContextBase *context, const EffectSlot *slot, const EffectProps *props,
        const EffectTarget target) final;
};

void DedicatedState::deviceUpdate(const DeviceBase*, const BufferStorage*)
{
    std::fill(mCurrentGains.begin(), mCurrentGains.end(), 0.0f);
}

void DedicatedState::update(const ContextBase*, const EffectSlot *slot,
    const EffectProps *props, const EffectTarget target)
{
    std::fill(mTargetGains.begin(), mTargetGains.end(), 0.0f);

    const float Gain{slot->Gain * std::get<DedicatedDialogProps>(*props).Gain};

    /* Dialog goes to the front-center speaker if it exists, otherwise it plays
     * from the front-center location.
     */
    const size_t idx{target.RealOut ? target.RealOut->ChannelIndex[FrontCenter]
        : InvalidChannelIndex};
    if(idx != InvalidChannelIndex)
    {
        mOutTarget = target.RealOut->Buffer;
        mTargetGains[idx] = Gain;
    }
    else
    {
        static constexpr auto coeffs = CalcDirectionCoeffs(std::array{0.0f, 0.0f, -1.0f});

        mOutTarget = target.Main->Buffer;
        ComputePanGains(target.Main, coeffs, Gain, mTargetGains);
    }
}

void DedicatedLfeState::update(const ContextBase*, const EffectSlot *slot,
    const EffectProps *props, const EffectTarget target)
{
    std::fill(mTargetGains.begin(), mTargetGains.end(), 0.0f);

    const float Gain{slot->Gain * std::get<DedicatedLfeProps>(*props).Gain};

    const size_t idx{target.RealOut ? target.RealOut->ChannelIndex[LFE] : InvalidChannelIndex};
    if(idx != InvalidChannelIndex)
    {
        mOutTarget = target.RealOut->Buffer;
        mTargetGains[idx] = Gain;
    }
}

void DedicatedState::process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut)
{
    MixSamples({samplesIn[0].data(), samplesToDo}, samplesOut, mCurrentGains.data(),
        mTargetGains.data(), samplesToDo, 0);
}


struct DedicatedDialogStateFactory final : public EffectStateFactory {
    al::intrusive_ptr<EffectState> create() override
    { return al::intrusive_ptr<EffectState>{new DedicatedState{}}; }
};

struct DedicatedLfeStateFactory final : public EffectStateFactory {
    al::intrusive_ptr<EffectState> create() override
    { return al::intrusive_ptr<EffectState>{new DedicatedLfeState{}}; }
};

} // namespace

EffectStateFactory *DedicatedDialogStateFactory_getFactory()
{
    static DedicatedDialogStateFactory DedicatedFactory{};
    return &DedicatedFactory;
}

EffectStateFactory *DedicatedLfeStateFactory_getFactory()
{
    static DedicatedLfeStateFactory DedicatedFactory{};
    return &DedicatedFactory;
}
