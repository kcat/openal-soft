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

#include <cstdlib>
#include <cmath>
#include <algorithm>

#include "al/auxeffectslot.h"
#include "alcmain.h"
#include "alcontext.h"
#include "alu.h"


namespace {

struct DedicatedState final : public EffectState {
    float mCurrentGains[MAX_OUTPUT_CHANNELS];
    float mTargetGains[MAX_OUTPUT_CHANNELS];


    void deviceUpdate(const ALCdevice *device) override;
    void update(const ALCcontext *context, const EffectSlot *slot, const EffectProps *props,
        const EffectTarget target) override;
    void process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn,
        const al::span<FloatBufferLine> samplesOut) override;

    DEF_NEWDEL(DedicatedState)
};

void DedicatedState::deviceUpdate(const ALCdevice*)
{
    std::fill(std::begin(mCurrentGains), std::end(mCurrentGains), 0.0f);
}

void DedicatedState::update(const ALCcontext*, const EffectSlot *slot,
    const EffectProps *props, const EffectTarget target)
{
    std::fill(std::begin(mTargetGains), std::end(mTargetGains), 0.0f);

    const float Gain{slot->Gain * props->Dedicated.Gain};

    if(slot->EffectType == AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT)
    {
        const ALuint idx{!target.RealOut ? INVALID_CHANNEL_INDEX :
            GetChannelIdxByName(*target.RealOut, LFE)};
        if(idx != INVALID_CHANNEL_INDEX)
        {
            mOutTarget = target.RealOut->Buffer;
            mTargetGains[idx] = Gain;
        }
    }
    else if(slot->EffectType == AL_EFFECT_DEDICATED_DIALOGUE)
    {
        /* Dialog goes to the front-center speaker if it exists, otherwise it
         * plays from the front-center location. */
        const ALuint idx{!target.RealOut ? INVALID_CHANNEL_INDEX :
            GetChannelIdxByName(*target.RealOut, FrontCenter)};
        if(idx != INVALID_CHANNEL_INDEX)
        {
            mOutTarget = target.RealOut->Buffer;
            mTargetGains[idx] = Gain;
        }
        else
        {
            const auto coeffs = CalcDirectionCoeffs({0.0f, 0.0f, -1.0f}, 0.0f);

            mOutTarget = target.Main->Buffer;
            ComputePanGains(target.Main, coeffs.data(), Gain, mTargetGains);
        }
    }
}

void DedicatedState::process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut)
{
    MixSamples({samplesIn[0].data(), samplesToDo}, samplesOut, mCurrentGains, mTargetGains,
        samplesToDo, 0);
}


struct DedicatedStateFactory final : public EffectStateFactory {
    EffectState *create() override { return new DedicatedState{}; }
};

} // namespace

EffectStateFactory *DedicatedStateFactory_getFactory()
{
    static DedicatedStateFactory DedicatedFactory{};
    return &DedicatedFactory;
}
