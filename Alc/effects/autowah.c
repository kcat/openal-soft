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


/* You can tweak the octave of this dynamic filter just changing next macro
 * guitar - (default) 2.0f
 * bass   - 4.0f
 */
#define OCTAVE 2.0f


/* We use a lfo with a custom low-pass filter to generate autowah
 * effect and a high-pass filter to avoid distortion and aliasing.
 * By adding the two filters up, we obtain a dynamic bandpass filter.
 */

typedef struct ALautowahState {
    DERIVE_FROM_TYPE(ALeffectState);

    /* Effect gains for each channel */
    ALfloat Gain[MaxChannels];

    /* Effect parameters */
    ALfloat AttackTime;
    ALfloat ReleaseTime;
    ALfloat Resonance;
    ALfloat PeakGain;
    ALuint Frequency;

    /* Samples processing */
    ALuint lfo;
    ALfilterState low_pass;
    ALfilterState high_pass;
} ALautowahState;

static ALvoid ALautowahState_Destruct(ALautowahState *UNUSED(state))
{
}

static ALboolean ALautowahState_deviceUpdate(ALautowahState *UNUSED(state), ALCdevice *UNUSED(device))
{
    return AL_TRUE;
}

static ALvoid ALautowahState_update(ALautowahState *state, ALCdevice *Device, const ALeffectslot *Slot)
{
    const ALfloat cutoff = LOWPASSFREQREF / (Device->Frequency * 4.0f);
    const ALfloat bandwidth = (cutoff / 2.0f) / (cutoff * 0.67f);
    ALfloat gain;

    /* computing high-pass filter coefficients */
    ALfilterState_setParams(&state->high_pass, ALfilterType_HighPass, 1.0f,
                            cutoff, bandwidth);

    state->AttackTime = Slot->EffectProps.Autowah.AttackTime;
    state->ReleaseTime = Slot->EffectProps.Autowah.ReleaseTime;
    state->Frequency = Device->Frequency;
    state->PeakGain = Slot->EffectProps.Autowah.PeakGain;
    state->Resonance = Slot->EffectProps.Autowah.Resonance;

    state->lfo = 0;

    ALfilterState_clear(&state->low_pass);

    gain = sqrtf(1.0f / Device->NumChan) * Slot->Gain;
    SetGains(Device, gain, state->Gain);
}

static ALvoid ALautowahState_process(ALautowahState *state, ALuint SamplesToDo, const ALfloat *SamplesIn, ALfloat (*SamplesOut)[BUFFERSIZE])
{
    ALuint it, kt;
    ALuint base;

    for(base = 0;base < SamplesToDo;)
    {
        ALfloat temps[64];
        ALuint td = minu(SamplesToDo-base, 64);

        for(it = 0;it < td;it++)
        {
            ALfloat smp = SamplesIn[it+base];
            ALfloat frequency, omega, alpha, peak;

            /* lfo for low-pass shaking */
            if((state->lfo++) % 30 == 0)
            {
                /* Using custom low-pass filter coefficients, to handle the resonance and peak-gain properties. */
                frequency = (1.0f + cosf(state->lfo * (1.0f / lerp(1.0f, 4.0f, state->AttackTime * state->ReleaseTime)) * 2.0f * F_PI / state->Frequency)) / OCTAVE;
                frequency = expf((frequency - 1.0f) * 6.0f);

                /* computing cutoff frequency and peak gain */
                omega = F_PI * frequency;
                alpha = sinf(omega) / (16.0f * (state->Resonance / AL_AUTOWAH_MAX_RESONANCE));
                peak = lerp(1.0f, 10.0f, state->PeakGain / AL_AUTOWAH_MAX_PEAK_GAIN);

                /* computing low-pass filter coefficients */
                state->low_pass.b[0] = (1.0f - cosf(omega)) / 2.0f;
                state->low_pass.b[1] =  1.0f - cosf(omega);
                state->low_pass.b[2] = (1.0f - cosf(omega)) / 2.0f;
                state->low_pass.a[0] =  1.0f + alpha / peak;
                state->low_pass.a[1] = -2.0f * cosf(omega);
                state->low_pass.a[2] =  1.0f - alpha / peak;

                state->low_pass.b[2] /= state->low_pass.a[0];
                state->low_pass.b[1] /= state->low_pass.a[0];
                state->low_pass.b[0] /= state->low_pass.a[0];
                state->low_pass.a[2] /= state->low_pass.a[0];
                state->low_pass.a[1] /= state->low_pass.a[0];
                state->low_pass.a[0] /= state->low_pass.a[0];
            }

            /* do high-pass filter */
            smp = ALfilterState_processSingle(&state->high_pass, smp);

            /* do low-pass filter */
            temps[it] = ALfilterState_processSingle(&state->low_pass, smp);
        }

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

static void ALautowahState_Delete(ALautowahState *state)
{
    free(state);
}

DEFINE_ALEFFECTSTATE_VTABLE(ALautowahState);


typedef struct ALautowahStateFactory {
    DERIVE_FROM_TYPE(ALeffectStateFactory);
} ALautowahStateFactory;

static ALeffectState *ALautowahStateFactory_create(ALautowahStateFactory *UNUSED(factory))
{
    ALautowahState *state;

    state = malloc(sizeof(*state));
    if(!state) return NULL;
    SET_VTABLE2(ALautowahState, ALeffectState, state);

    ALfilterState_clear(&state->low_pass);
    ALfilterState_clear(&state->high_pass);

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
