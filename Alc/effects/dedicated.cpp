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

#include <stdlib.h>
#include <cmath>
#include <algorithm>

#include "alMain.h"
#include "alcontext.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
#include "alu.h"


struct ALdedicatedState final : public ALeffectState {
    ALfloat mCurrentGains[MAX_OUTPUT_CHANNELS];
    ALfloat mTargetGains[MAX_OUTPUT_CHANNELS];
};

static ALvoid ALdedicatedState_Destruct(ALdedicatedState *state);
static ALboolean ALdedicatedState_deviceUpdate(ALdedicatedState *state, ALCdevice *device);
static ALvoid ALdedicatedState_update(ALdedicatedState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props);
static ALvoid ALdedicatedState_process(ALdedicatedState *state, ALsizei SamplesToDo, const ALfloat (*RESTRICT SamplesIn)[BUFFERSIZE], ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE], ALsizei NumChannels);
DECLARE_DEFAULT_ALLOCATORS(ALdedicatedState)

DEFINE_ALEFFECTSTATE_VTABLE(ALdedicatedState);


static void ALdedicatedState_Construct(ALdedicatedState *state)
{
    new (state) ALdedicatedState{};
    ALeffectState_Construct(STATIC_CAST(ALeffectState, state));
    SET_VTABLE2(ALdedicatedState, ALeffectState, state);
}

static ALvoid ALdedicatedState_Destruct(ALdedicatedState *state)
{
    ALeffectState_Destruct(STATIC_CAST(ALeffectState,state));
    state->~ALdedicatedState();
}

static ALboolean ALdedicatedState_deviceUpdate(ALdedicatedState *state, ALCdevice *UNUSED(device))
{
    std::fill(std::begin(state->mCurrentGains), std::end(state->mCurrentGains), 0.0f);
    return AL_TRUE;
}

static ALvoid ALdedicatedState_update(ALdedicatedState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props)
{
    const ALCdevice *device = context->Device;
    ALfloat Gain;

    std::fill(std::begin(state->mTargetGains), std::end(state->mTargetGains), 0.0f);

    Gain = slot->Params.Gain * props->Dedicated.Gain;
    if(slot->Params.EffectType == AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT)
    {
        int idx;
        if((idx=GetChannelIdxByName(&device->RealOut, LFE)) != -1)
        {
            state->OutBuffer = device->RealOut.Buffer;
            state->OutChannels = device->RealOut.NumChannels;
            state->mTargetGains[idx] = Gain;
        }
    }
    else if(slot->Params.EffectType == AL_EFFECT_DEDICATED_DIALOGUE)
    {
        /* Dialog goes to the front-center speaker if it exists, otherwise it
         * plays from the front-center location. */
        int idx{GetChannelIdxByName(&device->RealOut, FrontCenter)};
        if(idx != -1)
        {
            state->OutBuffer = device->RealOut.Buffer;
            state->OutChannels = device->RealOut.NumChannels;
            state->mTargetGains[idx] = Gain;
        }
        else
        {
            ALfloat coeffs[MAX_AMBI_COEFFS];
            CalcAngleCoeffs(0.0f, 0.0f, 0.0f, coeffs);

            state->OutBuffer = device->Dry.Buffer;
            state->OutChannels = device->Dry.NumChannels;
            ComputePanGains(&device->Dry, coeffs, Gain, state->mTargetGains);
        }
    }
}

static ALvoid ALdedicatedState_process(ALdedicatedState *state, ALsizei SamplesToDo, const ALfloat (*RESTRICT SamplesIn)[BUFFERSIZE], ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE], ALsizei NumChannels)
{
    MixSamples(SamplesIn[0], NumChannels, SamplesOut, state->mCurrentGains,
               state->mTargetGains, SamplesToDo, 0, SamplesToDo);
}


struct DedicatedStateFactory final : public EffectStateFactory {
    ALeffectState *create() override;
};

ALeffectState *DedicatedStateFactory::create()
{
    ALdedicatedState *state;
    NEW_OBJ0(state, ALdedicatedState)();
    return state;
}

EffectStateFactory *DedicatedStateFactory_getFactory(void)
{
    static DedicatedStateFactory DedicatedFactory{};
    return &DedicatedFactory;
}


void ALdedicated_setParami(ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALint UNUSED(val))
{ alSetError(context, AL_INVALID_ENUM, "Invalid dedicated integer property 0x%04x", param); }
void ALdedicated_setParamiv(ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, const ALint *UNUSED(vals))
{ alSetError(context, AL_INVALID_ENUM, "Invalid dedicated integer-vector property 0x%04x", param); }
void ALdedicated_setParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_DEDICATED_GAIN:
            if(!(val >= 0.0f && std::isfinite(val)))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Dedicated gain out of range");
            props->Dedicated.Gain = val;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid dedicated float property 0x%04x", param);
    }
}
void ALdedicated_setParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{ ALdedicated_setParamf(effect, context, param, vals[0]); }

void ALdedicated_getParami(const ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALint *UNUSED(val))
{ alSetError(context, AL_INVALID_ENUM, "Invalid dedicated integer property 0x%04x", param); }
void ALdedicated_getParamiv(const ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALint *UNUSED(vals))
{ alSetError(context, AL_INVALID_ENUM, "Invalid dedicated integer-vector property 0x%04x", param); }
void ALdedicated_getParamf(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val)
{
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_DEDICATED_GAIN:
            *val = props->Dedicated.Gain;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid dedicated float property 0x%04x", param);
    }
}
void ALdedicated_getParamfv(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{ ALdedicated_getParamf(effect, context, param, vals); }

DEFINE_ALEFFECT_VTABLE(ALdedicated);
