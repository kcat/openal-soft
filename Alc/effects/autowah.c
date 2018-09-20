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

    /* Filter components derived from the envelope. */
    struct {
        ALfloat cos_w0;
        ALfloat alpha;
    } Env[BUFFERSIZE];

    struct {
        /* Effect filters' history. */
        struct {
            ALfloat z1, z2;
        } Filter;

        /* Effect gains for each output channel */
        ALfloat CurrentGains[MAX_OUTPUT_CHANNELS];
        ALfloat TargetGains[MAX_OUTPUT_CHANNELS];
    } Chans[MAX_EFFECT_CHANNELS];

    /* Effects buffers */
    alignas(16) ALfloat BufferOut[BUFFERSIZE];
} ALautowahState;

static ALvoid ALautowahState_Destruct(ALautowahState *state);
static ALboolean ALautowahState_deviceUpdate(ALautowahState *state, ALCdevice *device);
static ALvoid ALautowahState_update(ALautowahState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props);
static ALvoid ALautowahState_process(ALautowahState *state, ALsizei SamplesToDo, const ALfloat (*restrict SamplesIn)[BUFFERSIZE], ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALsizei NumChannels);
DECLARE_DEFAULT_ALLOCATORS(ALautowahState)

DEFINE_ALEFFECTSTATE_VTABLE(ALautowahState);

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

    memset(state->Env, 0, sizeof(state->Env));

    for(i = 0;i < MAX_EFFECT_CHANNELS;i++)
    {
        for(j = 0;j < MAX_OUTPUT_CHANNELS;j++)
            state->Chans[i].CurrentGains[j] = 0.0f;
        state->Chans[i].Filter.z1 = 0.0f;
        state->Chans[i].Filter.z2 = 0.0f;
    }

    return AL_TRUE;
}

static ALvoid ALautowahState_update(ALautowahState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props)
{
    const ALCdevice *device = context->Device;
    ALfloat ReleaseTime;
    ALsizei i;

    ReleaseTime = clampf(props->Autowah.ReleaseTime, 0.001f, 1.0f);

    state->AttackRate    = expf(-1.0f / (props->Autowah.AttackTime*device->Frequency));
    state->ReleaseRate   = expf(-1.0f / (ReleaseTime*device->Frequency));
    /* 0-20dB Resonance Peak gain */
    state->ResonanceGain = sqrtf(log10f(props->Autowah.Resonance)*10.0f / 3.0f);
    state->PeakGain      = 1.0f - log10f(props->Autowah.PeakGain/AL_AUTOWAH_MAX_PEAK_GAIN);
    state->FreqMinNorm   = MIN_FREQ / device->Frequency;
    state->BandwidthNorm = (MAX_FREQ-MIN_FREQ) / device->Frequency;

    STATIC_CAST(ALeffectState,state)->OutBuffer = device->FOAOut.Buffer;
    STATIC_CAST(ALeffectState,state)->OutChannels = device->FOAOut.NumChannels;
    for(i = 0;i < MAX_EFFECT_CHANNELS;i++)
        ComputePanGains(&device->FOAOut, IdentityMatrixf.m[i], slot->Params.Gain,
                        state->Chans[i].TargetGains);
}

static ALvoid ALautowahState_process(ALautowahState *state, ALsizei SamplesToDo, const ALfloat (*restrict SamplesIn)[BUFFERSIZE], ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALsizei NumChannels)
{
    const ALfloat attack_rate = state->AttackRate;
    const ALfloat release_rate = state->ReleaseRate;
    const ALfloat res_gain = state->ResonanceGain;
    const ALfloat peak_gain = state->PeakGain;
    const ALfloat freq_min = state->FreqMinNorm;
    const ALfloat bandwidth = state->BandwidthNorm;
    ALfloat env_delay;
    ALsizei c, i;

    env_delay = state->env_delay;
    for(i = 0;i < SamplesToDo;i++)
    {
        ALfloat w0, sample, a;

        /* Envelope follower described on the book: Audio Effects, Theory,
         * Implementation and Application.
         */
        sample = peak_gain * fabsf(SamplesIn[0][i]);
        a = (sample > env_delay) ? attack_rate : release_rate;
        env_delay = lerp(sample, env_delay, a);

        /* Calculate the cos and alpha components for this sample's filter. */
        w0 = minf((bandwidth*env_delay + freq_min), 0.46f) * F_TAU;
        state->Env[i].cos_w0 = cosf(w0);
        state->Env[i].alpha = sinf(w0)/(2.0f * Q_FACTOR);
    }
    state->env_delay = env_delay;

    for(c = 0;c < MAX_EFFECT_CHANNELS; c++)
    {
        /* This effectively inlines BiquadFilter_setParams for a peaking
         * filter and BiquadFilter_processC. The alpha and cosine components
         * for the filter coefficients were previously calculated with the
         * envelope. Because the filter changes for each sample, the
         * coefficients are transient and don't need to be held.
         */
        ALfloat z1 = state->Chans[c].Filter.z1;
        ALfloat z2 = state->Chans[c].Filter.z2;

        for(i = 0;i < SamplesToDo;i++)
        {
            const ALfloat alpha = state->Env[i].alpha;
            const ALfloat cos_w0 = state->Env[i].cos_w0;
            ALfloat input, output;
            ALfloat a[3], b[3];

            b[0] =  1.0f + alpha*res_gain;
            b[1] = -2.0f * cos_w0;
            b[2] =  1.0f - alpha*res_gain;
            a[0] =  1.0f + alpha/res_gain;
            a[1] = -2.0f * cos_w0;
            a[2] =  1.0f - alpha/res_gain;

            input = SamplesIn[c][i];
            output = input*(b[0]/a[0]) + z1;
            z1 = input*(b[1]/a[0]) - output*(a[1]/a[0]) + z2;
            z2 = input*(b[2]/a[0]) - output*(a[2]/a[0]);
            state->BufferOut[i] = output;
        }
        state->Chans[c].Filter.z1 = z1;
        state->Chans[c].Filter.z2 = z2;

        /* Now, mix the processed sound data to the output. */
        MixSamples(state->BufferOut, NumChannels, SamplesOut, state->Chans[c].CurrentGains,
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
