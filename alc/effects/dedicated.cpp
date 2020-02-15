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
    ALfloat mCurrentGains[MAX_OUTPUT_CHANNELS];
    ALfloat mTargetGains[MAX_OUTPUT_CHANNELS];


    ALboolean deviceUpdate(const ALCdevice *device) override;
    void update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target) override;
    void process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut) override;

    DEF_NEWDEL(DedicatedState)
};

ALboolean DedicatedState::deviceUpdate(const ALCdevice*)
{
    std::fill(std::begin(mCurrentGains), std::end(mCurrentGains), 0.0f);
    return AL_TRUE;
}

void DedicatedState::update(const ALCcontext*, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target)
{
    std::fill(std::begin(mTargetGains), std::end(mTargetGains), 0.0f);

    const ALfloat Gain{slot->Params.Gain * props->Dedicated.Gain};

    if(slot->Params.EffectType == AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT)
    {
        const ALuint idx{!target.RealOut ? INVALID_CHANNEL_INDEX :
            GetChannelIdxByName(*target.RealOut, LFE)};
        if(idx != INVALID_CHANNEL_INDEX)
        {
            mOutTarget = target.RealOut->Buffer;
            mTargetGains[idx] = Gain;
        }
    }
    else if(slot->Params.EffectType == AL_EFFECT_DEDICATED_DIALOGUE)
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
            ALfloat coeffs[MAX_AMBI_CHANNELS];
            CalcDirectionCoeffs({0.0f, 0.0f, -1.0f}, 0.0f, coeffs);

            mOutTarget = target.Main->Buffer;
            ComputePanGains(target.Main, coeffs, Gain, mTargetGains);
        }
    }
}

void DedicatedState::process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut)
{
    MixSamples({samplesIn[0].data(), samplesToDo}, samplesOut, mCurrentGains, mTargetGains,
        samplesToDo, 0);
}


void Dedicated_setParami(EffectProps*, ALCcontext *context, ALenum param, ALint)
{ context->setError(AL_INVALID_ENUM, "Invalid dedicated integer property 0x%04x", param); }
void Dedicated_setParamiv(EffectProps*, ALCcontext *context, ALenum param, const ALint*)
{ context->setError(AL_INVALID_ENUM, "Invalid dedicated integer-vector property 0x%04x", param); }
void Dedicated_setParamf(EffectProps *props, ALCcontext *context, ALenum param, ALfloat val)
{
    switch(param)
    {
        case AL_DEDICATED_GAIN:
            if(!(val >= 0.0f && std::isfinite(val)))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Dedicated gain out of range");
            props->Dedicated.Gain = val;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid dedicated float property 0x%04x", param);
    }
}
void Dedicated_setParamfv(EffectProps *props, ALCcontext *context, ALenum param, const ALfloat *vals)
{ Dedicated_setParamf(props, context, param, vals[0]); }

void Dedicated_getParami(const EffectProps*, ALCcontext *context, ALenum param, ALint*)
{ context->setError(AL_INVALID_ENUM, "Invalid dedicated integer property 0x%04x", param); }
void Dedicated_getParamiv(const EffectProps*, ALCcontext *context, ALenum param, ALint*)
{ context->setError(AL_INVALID_ENUM, "Invalid dedicated integer-vector property 0x%04x", param); }
void Dedicated_getParamf(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *val)
{
    switch(param)
    {
        case AL_DEDICATED_GAIN:
            *val = props->Dedicated.Gain;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid dedicated float property 0x%04x", param);
    }
}
void Dedicated_getParamfv(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *vals)
{ Dedicated_getParamf(props, context, param, vals); }

DEFINE_ALEFFECT_VTABLE(Dedicated);


struct DedicatedStateFactory final : public EffectStateFactory {
    EffectState *create() override { return new DedicatedState{}; }
    EffectProps getDefaultProps() const noexcept override;
    const EffectVtable *getEffectVtable() const noexcept override { return &Dedicated_vtable; }
};

EffectProps DedicatedStateFactory::getDefaultProps() const noexcept
{
    EffectProps props{};
    props.Dedicated.Gain = 1.0f;
    return props;
}

} // namespace

EffectStateFactory *DedicatedStateFactory_getFactory()
{
    static DedicatedStateFactory DedicatedFactory{};
    return &DedicatedFactory;
}
