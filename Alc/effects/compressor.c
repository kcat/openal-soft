/**
 * OpenAL cross platform audio library
 * Copyright (C) 2013 by Anis A. Hireche
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

#include <stdlib.h>

#include "config.h"
#include "alError.h"
#include "alMain.h"
#include "alAuxEffectSlot.h"
#include "alu.h"


#define AMP_ENVELOPE_MIN  0.5f
#define AMP_ENVELOPE_MAX  2.0f

#define ATTACK_TIME  0.1f /* 100ms to rise from min to max */
#define RELEASE_TIME 0.2f /* 200ms to drop from max to min */


typedef struct ALcompressorState {
    DERIVE_FROM_TYPE(ALeffectState);

    /* Effect gains for each channel */
    ALfloat Gain[MAX_EFFECT_CHANNELS][MAX_OUTPUT_CHANNELS];

    /* Effect parameters */
    ALboolean Enabled;
    ALfloat AttackMult;
    ALfloat ReleaseMult;
    ALfloat EnvFollower;
} ALcompressorState;

static ALvoid ALcompressorState_Destruct(ALcompressorState *state);
static ALboolean ALcompressorState_deviceUpdate(ALcompressorState *state, ALCdevice *device);
static ALvoid ALcompressorState_update(ALcompressorState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props);
static ALvoid ALcompressorState_process(ALcompressorState *state, ALsizei SamplesToDo, const ALfloat (*restrict SamplesIn)[BUFFERSIZE], ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALsizei NumChannels);
DECLARE_DEFAULT_ALLOCATORS(ALcompressorState)

DEFINE_ALEFFECTSTATE_VTABLE(ALcompressorState);


static void ALcompressorState_Construct(ALcompressorState *state)
{
    ALeffectState_Construct(STATIC_CAST(ALeffectState, state));
    SET_VTABLE2(ALcompressorState, ALeffectState, state);

    state->Enabled = AL_TRUE;
    state->AttackMult = 1.0f;
    state->ReleaseMult = 1.0f;
    state->EnvFollower = 1.0f;
}

static ALvoid ALcompressorState_Destruct(ALcompressorState *state)
{
    ALeffectState_Destruct(STATIC_CAST(ALeffectState,state));
}

static ALboolean ALcompressorState_deviceUpdate(ALcompressorState *state, ALCdevice *device)
{
    /* Number of samples to do a full attack and release (non-integer sample
     * counts are okay).
     */
    const ALfloat attackCount  = (ALfloat)device->Frequency * ATTACK_TIME;
    const ALfloat releaseCount = (ALfloat)device->Frequency * RELEASE_TIME;

    /* Calculate per-sample multipliers to attack and release at the desired
     * rates.
     */
    state->AttackMult  = powf(AMP_ENVELOPE_MAX/AMP_ENVELOPE_MIN, 1.0f/attackCount);
    state->ReleaseMult = powf(AMP_ENVELOPE_MIN/AMP_ENVELOPE_MAX, 1.0f/releaseCount);

    return AL_TRUE;
}

static ALvoid ALcompressorState_update(ALcompressorState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props)
{
    const ALCdevice *device = context->Device;
    ALuint i;

    state->Enabled = props->Compressor.OnOff;

    STATIC_CAST(ALeffectState,state)->OutBuffer = device->FOAOut.Buffer;
    STATIC_CAST(ALeffectState,state)->OutChannels = device->FOAOut.NumChannels;
    for(i = 0;i < 4;i++)
        ComputePanGains(&device->FOAOut, IdentityMatrixf.m[i], slot->Params.Gain, state->Gain[i]);
}

static ALvoid ALcompressorState_process(ALcompressorState *state, ALsizei SamplesToDo, const ALfloat (*restrict SamplesIn)[BUFFERSIZE], ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALsizei NumChannels)
{
    ALsizei i, j, k;
    ALsizei base;

    for(base = 0;base < SamplesToDo;)
    {
        ALfloat gains[256];
        ALsizei td = mini(256, SamplesToDo-base);
        ALfloat env = state->EnvFollower;

        /* Generate the per-sample gains from the signal envelope. */
        if(state->Enabled)
        {
            for(i = 0;i < td;++i)
            {
                /* Clamp the absolute amplitude to the defined envelope limits,
                 * then attack or release the envelope to reach it.
                 */
                ALfloat amplitude = clampf(fabsf(SamplesIn[0][base+i]),
                                           AMP_ENVELOPE_MIN, AMP_ENVELOPE_MAX);
                if(amplitude > env)
                    env = minf(env*state->AttackMult, amplitude);
                else if(amplitude < env)
                    env = maxf(env*state->ReleaseMult, amplitude);

                /* Apply the reciprocal of the envelope to normalize the volume
                 * (compress the dynamic range).
                 */
                gains[i] = 1.0f / env;
            }
        }
        else
        {
            /* Same as above, except the amplitude is forced to 1. This helps
             * ensure smooth gain changes when the compressor is turned on and
             * off.
             */
            for(i = 0;i < td;++i)
            {
                ALfloat amplitude = 1.0f;
                if(amplitude > env)
                    env = minf(env*state->AttackMult, amplitude);
                else if(amplitude < env)
                    env = maxf(env*state->ReleaseMult, amplitude);

                gains[i] = 1.0f / env;
            }
        }
        state->EnvFollower = env;

        /* Now compress the signal amplitude to output. */
        for(j = 0;j < MAX_EFFECT_CHANNELS;j++)
        {
            for(k = 0;k < NumChannels;k++)
            {
                ALfloat gain = state->Gain[j][k];
                if(!(fabsf(gain) > GAIN_SILENCE_THRESHOLD))
                    continue;

                for(i = 0;i < td;i++)
                    SamplesOut[k][base+i] += SamplesIn[j][base+i] * gains[i] * gain;
            }
        }

        base += td;
    }
}


typedef struct CompressorStateFactory {
    DERIVE_FROM_TYPE(EffectStateFactory);
} CompressorStateFactory;

static ALeffectState *CompressorStateFactory_create(CompressorStateFactory *UNUSED(factory))
{
    ALcompressorState *state;

    NEW_OBJ0(state, ALcompressorState)();
    if(!state) return NULL;

    return STATIC_CAST(ALeffectState, state);
}

DEFINE_EFFECTSTATEFACTORY_VTABLE(CompressorStateFactory);

EffectStateFactory *CompressorStateFactory_getFactory(void)
{
    static CompressorStateFactory CompressorFactory = { { GET_VTABLE2(CompressorStateFactory, EffectStateFactory) } };

    return STATIC_CAST(EffectStateFactory, &CompressorFactory);
}


void ALcompressor_setParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_COMPRESSOR_ONOFF:
            if(!(val >= AL_COMPRESSOR_MIN_ONOFF && val <= AL_COMPRESSOR_MAX_ONOFF))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Compressor state out of range");
            props->Compressor.OnOff = val;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid compressor integer property 0x%04x",
                       param);
    }
}
void ALcompressor_setParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{ ALcompressor_setParami(effect, context, param, vals[0]); }
void ALcompressor_setParamf(ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALfloat UNUSED(val))
{ alSetError(context, AL_INVALID_ENUM, "Invalid compressor float property 0x%04x", param); }
void ALcompressor_setParamfv(ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, const ALfloat *UNUSED(vals))
{ alSetError(context, AL_INVALID_ENUM, "Invalid compressor float-vector property 0x%04x", param); }

void ALcompressor_getParami(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *val)
{ 
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_COMPRESSOR_ONOFF:
            *val = props->Compressor.OnOff;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid compressor integer property 0x%04x",
                       param);
    }
}
void ALcompressor_getParamiv(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{ ALcompressor_getParami(effect, context, param, vals); }
void ALcompressor_getParamf(const ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALfloat *UNUSED(val))
{ alSetError(context, AL_INVALID_ENUM, "Invalid compressor float property 0x%04x", param); }
void ALcompressor_getParamfv(const ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALfloat *UNUSED(vals))
{ alSetError(context, AL_INVALID_ENUM, "Invalid compressor float-vector property 0x%04x", param); }

DEFINE_ALEFFECT_VTABLE(ALcompressor);
