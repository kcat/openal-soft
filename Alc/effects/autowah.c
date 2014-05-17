/**
 * OpenAL cross platform audio library
 * Copyright (C) 2013 by Anis A. Hireche, Nasca Octavian Paul
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
 *  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA  02111-1307, USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include <stdlib.h>

#include "config.h"
#include "alu.h"
#include "alFilter.h"
#include "alError.h"
#include "alMain.h"
#include "alAuxEffectSlot.h"


/* Auto-wah is simply a low-pass filter with a cutoff frequency that shifts up
 * or down depending on the input signal, and a resonant peak at the cutoff.
 *
 * Currently, we assume a cutoff frequency range of 500hz (no amplitude) to
 * 3khz (peak gain). Peak gain is assumed to be in normalized scale.
 */

typedef struct ALautowahState {
    DERIVE_FROM_TYPE(ALeffectState);

    /* Effect gains for each channel */
    ALfloat Gain[MaxChannels];

    /* Effect parameters */
    ALfloat AttackRate;
    ALfloat ReleaseRate;
    ALfloat Resonance;
    ALfloat PeakGain;
    ALfloat GainCtrl;
    ALfloat Frequency;

    /* Samples processing */
    ALfilterState LowPass;
} ALautowahState;

static ALvoid ALautowahState_Destruct(ALautowahState *UNUSED(state))
{
}

static ALboolean ALautowahState_deviceUpdate(ALautowahState *state, ALCdevice *device)
{
    state->Frequency = (ALfloat)device->Frequency;
    return AL_TRUE;
}

static ALvoid ALautowahState_update(ALautowahState *state, ALCdevice *device, const ALeffectslot *slot)
{
    ALfloat attackTime, releaseTime;
    ALfloat gain;

    attackTime = slot->EffectProps.Autowah.AttackTime * state->Frequency;
    releaseTime = slot->EffectProps.Autowah.ReleaseTime * state->Frequency;

    state->AttackRate = powf(1.0f/GAIN_SILENCE_THRESHOLD, 1.0f/attackTime);
    state->ReleaseRate = powf(GAIN_SILENCE_THRESHOLD/1.0f, 1.0f/releaseTime);
    state->PeakGain = slot->EffectProps.Autowah.PeakGain;
    state->Resonance = slot->EffectProps.Autowah.Resonance;

    gain = sqrtf(1.0f / device->NumChan) * slot->Gain;
    SetGains(device, gain, state->Gain);
}

static ALvoid ALautowahState_process(ALautowahState *state, ALuint SamplesToDo, const ALfloat *SamplesIn, ALfloat (*SamplesOut)[BUFFERSIZE])
{
    ALuint it, kt;
    ALuint base;

    for(base = 0;base < SamplesToDo;)
    {
        ALfloat temps[64];
        ALuint td = minu(SamplesToDo-base, 64);
        ALfloat gain = state->GainCtrl;

        for(it = 0;it < td;it++)
        {
            ALfloat smp = SamplesIn[it+base];
            ALfloat alpha, w0;
            ALfloat amplitude;
            ALfloat cutoff;

            /* Similar to compressor, we get the current amplitude of the
             * incoming signal, and attack or release to reach it. */
            amplitude = fabsf(smp);
            if(amplitude > gain)
                gain = minf(gain*state->AttackRate, amplitude);
            else if(amplitude < gain)
                gain = maxf(gain*state->ReleaseRate, amplitude);
            gain = maxf(gain, GAIN_SILENCE_THRESHOLD);

            /* FIXME: What range does the filter cover? */
            cutoff = lerp(20.0f, 20000.0f, minf(gain/state->PeakGain, 1.0f));

            /* The code below is like calling ALfilterState_setParams with
             * ALfilterType_LowPass. However, instead of passing a bandwidth,
             * we use the resonance property for Q. This also inlines the call.
             */
            w0 = F_2PI * cutoff / state->Frequency;

            /* FIXME: Resonance controls the resonant peak, or Q. How? Not sure
             * that Q = resonance*0.1. */
            alpha = sinf(w0) / (2.0f * state->Resonance*0.1f);
            state->LowPass.b[0] = (1.0f - cosf(w0)) / 2.0f;
            state->LowPass.b[1] =  1.0f - cosf(w0);
            state->LowPass.b[2] = (1.0f - cosf(w0)) / 2.0f;
            state->LowPass.a[0] =  1.0f + alpha;
            state->LowPass.a[1] = -2.0f * cosf(w0);
            state->LowPass.a[2] =  1.0f - alpha;

            state->LowPass.b[2] /= state->LowPass.a[0];
            state->LowPass.b[1] /= state->LowPass.a[0];
            state->LowPass.b[0] /= state->LowPass.a[0];
            state->LowPass.a[2] /= state->LowPass.a[0];
            state->LowPass.a[1] /= state->LowPass.a[0];
            state->LowPass.a[0] /= state->LowPass.a[0];

            temps[it] = ALfilterState_processSingle(&state->LowPass, smp);
        }
        state->GainCtrl = gain;

        for(kt = 0;kt < MaxChannels;kt++)
        {
            ALfloat gain = state->Gain[kt];
            if(!(gain > GAIN_SILENCE_THRESHOLD))
                continue;

            for(it = 0;it < td;it++)
                SamplesOut[kt][base+it] += gain * temps[it];
        }

        base += td;
    }
}

DECLARE_DEFAULT_ALLOCATORS(ALautowahState)

DEFINE_ALEFFECTSTATE_VTABLE(ALautowahState);


typedef struct ALautowahStateFactory {
    DERIVE_FROM_TYPE(ALeffectStateFactory);
} ALautowahStateFactory;

static ALeffectState *ALautowahStateFactory_create(ALautowahStateFactory *UNUSED(factory))
{
    ALautowahState *state;

    state = ALautowahState_New(sizeof(*state));
    if(!state) return NULL;
    SET_VTABLE2(ALautowahState, ALeffectState, state);

    state->AttackRate = 1.0f;
    state->ReleaseRate = 1.0f;
    state->Resonance = 2.0f;
    state->PeakGain = 1.0f;
    state->GainCtrl = 1.0f;

    ALfilterState_clear(&state->LowPass);

    return STATIC_CAST(ALeffectState, state);
}

DEFINE_ALEFFECTSTATEFACTORY_VTABLE(ALautowahStateFactory);

ALeffectStateFactory *ALautowahStateFactory_getFactory(void)
{
    static ALautowahStateFactory AutowahFactory = { { GET_VTABLE2(ALautowahStateFactory, ALeffectStateFactory) } };

    return STATIC_CAST(ALeffectStateFactory, &AutowahFactory);
}


void ALautowah_setParami(ALeffect *UNUSED(effect), ALCcontext *context, ALenum UNUSED(param), ALint UNUSED(val))
{ SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM); }
void ALautowah_setParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{
    ALautowah_setParami(effect, context, param, vals[0]);
}
void ALautowah_setParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_AUTOWAH_ATTACK_TIME:
            if(!(val >= AL_AUTOWAH_MIN_ATTACK_TIME && val <= AL_AUTOWAH_MAX_ATTACK_TIME))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Autowah.AttackTime = val;
            break;

        case AL_AUTOWAH_RELEASE_TIME:
            if(!(val >= AL_AUTOWAH_MIN_RELEASE_TIME && val <= AL_AUTOWAH_MAX_RELEASE_TIME))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Autowah.ReleaseTime = val;
            break;

        case AL_AUTOWAH_RESONANCE:
            if(!(val >= AL_AUTOWAH_MIN_RESONANCE && val <= AL_AUTOWAH_MAX_RESONANCE))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Autowah.Resonance = val;
            break;

        case AL_AUTOWAH_PEAK_GAIN:
            if(!(val >= AL_AUTOWAH_MIN_PEAK_GAIN && val <= AL_AUTOWAH_MAX_PEAK_GAIN))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Autowah.PeakGain = val;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALautowah_setParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{
    ALautowah_setParamf(effect, context, param, vals[0]);
}

void ALautowah_getParami(const ALeffect *UNUSED(effect), ALCcontext *context, ALenum UNUSED(param), ALint *UNUSED(val))
{ SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM); }
void ALautowah_getParamiv(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{
    ALautowah_getParami(effect, context, param, vals);
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
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALautowah_getParamfv(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{
    ALautowah_getParamf(effect, context, param, vals);
}

DEFINE_ALEFFECT_VTABLE(ALautowah);
