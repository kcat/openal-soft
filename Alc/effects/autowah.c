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

#include <math.h>
#include <stdlib.h>

#include "alMain.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
#include "alu.h"
#include "filters/defs.h"

#define MIN_FREQ 20.0f
#define MAX_FREQ 2500.0f
#define Q_FACTOR 5.0f

typedef struct ALautowahState {
    DERIVE_FROM_TYPE(ALeffectState);

    /* Effect parameters */
    ALfloat AttackRate;
    ALfloat ReleaseRate;
    ALfloat ResonanceGain;
    ALfloat PeakGain;
    ALfloat FreqMinNorm;
    ALfloat BandwidthNorm;
    ALfloat env_delay;

    struct {
        /* Effect gains for each output channel */
        ALfloat CurrentGains[MAX_OUTPUT_CHANNELS];
        ALfloat TargetGains[MAX_OUTPUT_CHANNELS];

        /* Effect filters */
        BiquadFilter Filter;
    } Chans[MAX_EFFECT_CHANNELS];

    /*Effects buffers*/ 
    alignas(16) ALfloat BufferOut[MAX_EFFECT_CHANNELS][BUFFERSIZE];
} ALautowahState;

static ALvoid ALautowahState_Destruct(ALautowahState *state);
static ALboolean ALautowahState_deviceUpdate(ALautowahState *state, ALCdevice *device);
static ALvoid ALautowahState_update(ALautowahState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props);
static ALvoid ALautowahState_process(ALautowahState *state, ALsizei SamplesToDo, const ALfloat (*restrict SamplesIn)[BUFFERSIZE], ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALsizei NumChannels);
DECLARE_DEFAULT_ALLOCATORS(ALautowahState)

DEFINE_ALEFFECTSTATE_VTABLE(ALautowahState);

/*Envelope follewer described on the book: Audio Effects, Theory, Implementation and Application*/
static inline ALfloat envelope_follower(ALautowahState *state, ALfloat SampleIn)
{
    ALfloat alpha, Sample;

    Sample =  state->PeakGain*fabsf(SampleIn);
    alpha  = (Sample > state->env_delay) ? state->AttackRate : state->ReleaseRate;
    state->env_delay = alpha*state->env_delay + (1.0f-alpha)*Sample;

    return state->env_delay;
}

static void ALautowahState_Construct(ALautowahState *state)
{
    ALeffectState_Construct(STATIC_CAST(ALeffectState, state));
    SET_VTABLE2(ALautowahState, ALeffectState, state);
}

static ALvoid ALautowahState_Destruct(ALautowahState *state)
{
    ALeffectState_Destruct(STATIC_CAST(ALeffectState,state));
}

static ALboolean ALautowahState_deviceUpdate(ALautowahState *state, ALCdevice *UNUSED(device))
{
    /* (Re-)initializing parameters and clear the buffers. */
    ALsizei i, j;

    state->AttackRate    = 1.0f;
    state->ReleaseRate   = 1.0f;
    state->ResonanceGain = 10.0f;
    state->PeakGain      = 4.5f;
    state->FreqMinNorm   = 4.5e-4f;
    state->BandwidthNorm = 0.05f;
    state->env_delay     = 0.0f;

    for(i = 0;i < MAX_EFFECT_CHANNELS;i++)
    {
        BiquadFilter_clear(&state->Chans[i].Filter);
        for(j = 0;j < MAX_OUTPUT_CHANNELS;j++)
            state->Chans[i].CurrentGains[j] = 0.0f;
    }

    return AL_TRUE;
}

static ALvoid ALautowahState_update(ALautowahState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props)
{
    const ALCdevice *device = context->Device;
    ALfloat ReleaseTime;
    ALuint i;

    ReleaseTime = clampf(props->Autowah.ReleaseTime,0.001f,1.0f);

    state->AttackRate    = expf(-1.0f/(props->Autowah.AttackTime*device->Frequency));
    state->ReleaseRate   = expf(-1.0f/(ReleaseTime*device->Frequency));
    state->ResonanceGain = 10.0f/3.0f*log10f(props->Autowah.Resonance);/*0-20dB Resonance Peak gain*/
    state->PeakGain      = 1.0f -log10f(props->Autowah.PeakGain/AL_AUTOWAH_MAX_PEAK_GAIN);
    state->FreqMinNorm   = MIN_FREQ/device->Frequency;
    state->BandwidthNorm = (MAX_FREQ - MIN_FREQ)/device->Frequency;

    STATIC_CAST(ALeffectState,state)->OutBuffer = device->FOAOut.Buffer;
    STATIC_CAST(ALeffectState,state)->OutChannels = device->FOAOut.NumChannels;
    for(i = 0;i < MAX_EFFECT_CHANNELS;i++)
        ComputeFirstOrderGains(&device->FOAOut, IdentityMatrixf.m[i],
                               slot->Params.Gain, state->Chans[i].TargetGains);
}

static ALvoid ALautowahState_process(ALautowahState *state, ALsizei SamplesToDo, const ALfloat (*restrict SamplesIn)[BUFFERSIZE], ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALsizei NumChannels)
{
    ALfloat (*restrict BufferOut)[BUFFERSIZE] = state->BufferOut;
    ALfloat f0norm[BUFFERSIZE];
    ALsizei c, i;

    for(i = 0;i < SamplesToDo;i++)
    {
            ALfloat env_out;
            env_out   = envelope_follower(state, SamplesIn[0][i]);
            f0norm[i] = state->BandwidthNorm*env_out + state->FreqMinNorm;
    }

    for(c = 0;c < MAX_EFFECT_CHANNELS; c++)
    {
        for(i = 0;i < SamplesToDo;i++)
        {
            ALfloat temp;

            BiquadFilter_setParams(&state->Chans[c].Filter, BiquadType_Peaking,
                                   state->ResonanceGain, f0norm[i], 1.0f/Q_FACTOR);
            BiquadFilter_process(&state->Chans[c].Filter, &temp, &SamplesIn[c][i], 1);

            BufferOut[c][i] = temp;
        }
        /* Now, mix the processed sound data to the output. */
        MixSamples(BufferOut[c], NumChannels, SamplesOut, state->Chans[c].CurrentGains,
                   state->Chans[c].TargetGains, SamplesToDo, 0, SamplesToDo);
    }
}

typedef struct AutowahStateFactory {
    DERIVE_FROM_TYPE(EffectStateFactory);
} AutowahStateFactory;

static ALeffectState *AutowahStateFactory_create(AutowahStateFactory *UNUSED(factory))
{
    ALautowahState *state;

    NEW_OBJ0(state, ALautowahState)();
    if(!state) return NULL;

    return STATIC_CAST(ALeffectState, state);
}

DEFINE_EFFECTSTATEFACTORY_VTABLE(AutowahStateFactory);

EffectStateFactory *AutowahStateFactory_getFactory(void)
{
    static AutowahStateFactory AutowahFactory = { { GET_VTABLE2(AutowahStateFactory, EffectStateFactory) } };

    return STATIC_CAST(EffectStateFactory, &AutowahFactory);
}

void ALautowah_setParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_AUTOWAH_ATTACK_TIME:
            if(!(val >= AL_AUTOWAH_MIN_ATTACK_TIME && val <= AL_AUTOWAH_MAX_ATTACK_TIME))
                SETERR_RETURN(context, AL_INVALID_VALUE,,"Autowah attack time out of range");
            props->Autowah.AttackTime = val;
            break;

        case AL_AUTOWAH_RELEASE_TIME:
            if(!(val >= AL_AUTOWAH_MIN_RELEASE_TIME && val <= AL_AUTOWAH_MAX_RELEASE_TIME))
                SETERR_RETURN(context, AL_INVALID_VALUE,,"Autowah release time out of range");
            props->Autowah.ReleaseTime = val;
            break;

        case AL_AUTOWAH_RESONANCE:
            if(!(val >= AL_AUTOWAH_MIN_RESONANCE && val <= AL_AUTOWAH_MAX_RESONANCE))
                SETERR_RETURN(context, AL_INVALID_VALUE,,"Autowah resonance out of range");
            props->Autowah.Resonance = val;
            break;

        case AL_AUTOWAH_PEAK_GAIN:
            if(!(val >= AL_AUTOWAH_MIN_PEAK_GAIN && val <= AL_AUTOWAH_MAX_PEAK_GAIN))
                SETERR_RETURN(context, AL_INVALID_VALUE,,"Autowah peak gain out of range");
            props->Autowah.PeakGain = val;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid autowah float property 0x%04x", param);
    }
}

void ALautowah_setParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{
    ALautowah_setParamf(effect, context, param, vals[0]);
}

void ALautowah_setParami(ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALint UNUSED(val))
{
    alSetError(context, AL_INVALID_ENUM, "Invalid autowah integer property 0x%04x", param);
}

void ALautowah_setParamiv(ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, const ALint *UNUSED(vals))
{
    alSetError(context, AL_INVALID_ENUM, "Invalid autowah integer vector property 0x%04x", param);
}

void ALautowah_getParami(const ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALint *UNUSED(val))
{
    alSetError(context, AL_INVALID_ENUM, "Invalid autowah integer property 0x%04x", param);
}
void ALautowah_getParamiv(const ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALint *UNUSED(vals))
{
    alSetError(context, AL_INVALID_ENUM, "Invalid autowah integer vector property 0x%04x", param);
}

void ALautowah_getParamf(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val)
{

    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_AUTOWAH_ATTACK_TIME:
            *val = props->Autowah.AttackTime;
            break;

        case AL_AUTOWAH_RELEASE_TIME:
            *val = props->Autowah.ReleaseTime;
            break;

        case AL_AUTOWAH_RESONANCE:
            *val = props->Autowah.Resonance;
            break;

        case AL_AUTOWAH_PEAK_GAIN:
            *val = props->Autowah.PeakGain;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid autowah float property 0x%04x", param);
    }

}

void ALautowah_getParamfv(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{
    ALautowah_getParamf(effect, context, param, vals);
}

DEFINE_ALEFFECT_VTABLE(ALautowah);
