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

#include <math.h>
#include <stdlib.h>

#include "alMain.h"
#include "alFilter.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
#include "alu.h"


typedef struct ALdistortionState {
    DERIVE_FROM_TYPE(ALeffectState);

    /* Effect gains for each channel */
    ALfloat Gain[MAX_OUTPUT_CHANNELS];

    /* Effect parameters */
    ALfilterState lowpass;
    ALfilterState bandpass;
    ALfloat attenuation;
    ALfloat edge_coeff;
} ALdistortionState;

static ALvoid ALdistortionState_Destruct(ALdistortionState *UNUSED(state))
{
}

static ALboolean ALdistortionState_deviceUpdate(ALdistortionState *UNUSED(state), ALCdevice *UNUSED(device))
{
    return AL_TRUE;
}

static ALvoid ALdistortionState_update(ALdistortionState *state, ALCdevice *Device, const ALeffectslot *Slot)
{
    ALfloat frequency = (ALfloat)Device->Frequency;
    ALfloat bandwidth;
    ALfloat cutoff;
    ALfloat edge;

    /* Store distorted signal attenuation settings */
    state->attenuation = Slot->EffectProps.Distortion.Gain;

    /* Store waveshaper edge settings */
    edge = sinf(Slot->EffectProps.Distortion.Edge * (F_PI_2));
    edge = minf(edge, 0.99f);
    state->edge_coeff = 2.0f * edge / (1.0f-edge);

    /* Lowpass filter */
    cutoff = Slot->EffectProps.Distortion.LowpassCutoff;
    /* Bandwidth value is constant in octaves */
    bandwidth = (cutoff / 2.0f) / (cutoff * 0.67f);
    ALfilterState_setParams(&state->lowpass, ALfilterType_LowPass, 1.0f,
        cutoff / (frequency*4.0f), calc_rcpQ_from_bandwidth(cutoff / (frequency*4.0f), bandwidth)
    );

    /* Bandpass filter */
    cutoff = Slot->EffectProps.Distortion.EQCenter;
    /* Convert bandwidth in Hz to octaves */
    bandwidth = Slot->EffectProps.Distortion.EQBandwidth / (cutoff * 0.67f);
    ALfilterState_setParams(&state->bandpass, ALfilterType_BandPass, 1.0f,
        cutoff / (frequency*4.0f), calc_rcpQ_from_bandwidth(cutoff / (frequency*4.0f), bandwidth)
    );

    ComputeAmbientGains(Device, Slot->Gain, state->Gain);
}

static ALvoid ALdistortionState_process(ALdistortionState *state, ALuint SamplesToDo, const ALfloat *restrict SamplesIn, ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALuint NumChannels)
{
    const ALfloat fc = state->edge_coeff;
    ALuint base;
    ALuint it;
    ALuint ot;
    ALuint kt;

    for(base = 0;base < SamplesToDo;)
    {
        float oversample_buffer[64][4];
        ALuint td = minu(64, SamplesToDo-base);

        /* Perform 4x oversampling to avoid aliasing.   */
        /* Oversampling greatly improves distortion     */
        /* quality and allows to implement lowpass and  */
        /* bandpass filters using high frequencies, at  */
        /* which classic IIR filters became unstable.   */

        /* Fill oversample buffer using zero stuffing */
        for(it = 0;it < td;it++)
        {
            oversample_buffer[it][0] = SamplesIn[it+base];
            oversample_buffer[it][1] = 0.0f;
            oversample_buffer[it][2] = 0.0f;
            oversample_buffer[it][3] = 0.0f;
        }

        /* First step, do lowpass filtering of original signal,  */
        /* additionally perform buffer interpolation and lowpass */
        /* cutoff for oversampling (which is fortunately first   */
        /* step of distortion). So combine three operations into */
        /* the one.                                              */
        for(it = 0;it < td;it++)
        {
            for(ot = 0;ot < 4;ot++)
            {
                ALfloat smp;
                smp = ALfilterState_processSingle(&state->lowpass, oversample_buffer[it][ot]);

                /* Restore signal power by multiplying sample by amount of oversampling */
                oversample_buffer[it][ot] = smp * 4.0f;
            }
        }

        for(it = 0;it < td;it++)
        {
            /* Second step, do distortion using waveshaper function  */
            /* to emulate signal processing during tube overdriving. */
            /* Three steps of waveshaping are intended to modify     */
            /* waveform without boost/clipping/attenuation process.  */
            for(ot = 0;ot < 4;ot++)
            {
                ALfloat smp = oversample_buffer[it][ot];

                smp = (1.0f + fc) * smp/(1.0f + fc*fabsf(smp));
                smp = (1.0f + fc) * smp/(1.0f + fc*fabsf(smp)) * -1.0f;
                smp = (1.0f + fc) * smp/(1.0f + fc*fabsf(smp));

                /* Third step, do bandpass filtering of distorted signal */
                smp = ALfilterState_processSingle(&state->bandpass, smp);
                oversample_buffer[it][ot] = smp;
            }
        }

        for(kt = 0;kt < NumChannels;kt++)
        {
            /* Fourth step, final, do attenuation and perform decimation,
             * store only one sample out of 4.
             */
            ALfloat gain = state->Gain[kt] * state->attenuation;
            if(!(fabsf(gain) > GAIN_SILENCE_THRESHOLD))
                continue;

            for(it = 0;it < td;it++)
                SamplesOut[kt][base+it] += gain * oversample_buffer[it][0];
        }

        base += td;
    }
}

DECLARE_DEFAULT_ALLOCATORS(ALdistortionState)

DEFINE_ALEFFECTSTATE_VTABLE(ALdistortionState);


typedef struct ALdistortionStateFactory {
    DERIVE_FROM_TYPE(ALeffectStateFactory);
} ALdistortionStateFactory;

static ALeffectState *ALdistortionStateFactory_create(ALdistortionStateFactory *UNUSED(factory))
{
    ALdistortionState *state;

    state = ALdistortionState_New(sizeof(*state));
    if(!state) return NULL;
    SET_VTABLE2(ALdistortionState, ALeffectState, state);

    ALfilterState_clear(&state->lowpass);
    ALfilterState_clear(&state->bandpass);

    return STATIC_CAST(ALeffectState, state);
}

DEFINE_ALEFFECTSTATEFACTORY_VTABLE(ALdistortionStateFactory);


ALeffectStateFactory *ALdistortionStateFactory_getFactory(void)
{
    static ALdistortionStateFactory DistortionFactory = { { GET_VTABLE2(ALdistortionStateFactory, ALeffectStateFactory) } };

    return STATIC_CAST(ALeffectStateFactory, &DistortionFactory);
}


void ALdistortion_setParami(ALeffect *UNUSED(effect), ALCcontext *context, ALenum UNUSED(param), ALint UNUSED(val))
{ SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM); }
void ALdistortion_setParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{
    ALdistortion_setParami(effect, context, param, vals[0]);
}
void ALdistortion_setParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_DISTORTION_EDGE:
            if(!(val >= AL_DISTORTION_MIN_EDGE && val <= AL_DISTORTION_MAX_EDGE))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Distortion.Edge = val;
            break;

        case AL_DISTORTION_GAIN:
            if(!(val >= AL_DISTORTION_MIN_GAIN && val <= AL_DISTORTION_MAX_GAIN))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Distortion.Gain = val;
            break;

        case AL_DISTORTION_LOWPASS_CUTOFF:
            if(!(val >= AL_DISTORTION_MIN_LOWPASS_CUTOFF && val <= AL_DISTORTION_MAX_LOWPASS_CUTOFF))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Distortion.LowpassCutoff = val;
            break;

        case AL_DISTORTION_EQCENTER:
            if(!(val >= AL_DISTORTION_MIN_EQCENTER && val <= AL_DISTORTION_MAX_EQCENTER))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Distortion.EQCenter = val;
            break;

        case AL_DISTORTION_EQBANDWIDTH:
            if(!(val >= AL_DISTORTION_MIN_EQBANDWIDTH && val <= AL_DISTORTION_MAX_EQBANDWIDTH))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Distortion.EQBandwidth = val;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALdistortion_setParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{
    ALdistortion_setParamf(effect, context, param, vals[0]);
}

void ALdistortion_getParami(const ALeffect *UNUSED(effect), ALCcontext *context, ALenum UNUSED(param), ALint *UNUSED(val))
{ SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM); }
void ALdistortion_getParamiv(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{
    ALdistortion_getParami(effect, context, param, vals);
}
void ALdistortion_getParamf(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val)
{
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_DISTORTION_EDGE:
            *val = props->Distortion.Edge;
            break;

        case AL_DISTORTION_GAIN:
            *val = props->Distortion.Gain;
            break;

        case AL_DISTORTION_LOWPASS_CUTOFF:
            *val = props->Distortion.LowpassCutoff;
            break;

        case AL_DISTORTION_EQCENTER:
            *val = props->Distortion.EQCenter;
            break;

        case AL_DISTORTION_EQBANDWIDTH:
            *val = props->Distortion.EQBandwidth;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALdistortion_getParamfv(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{
    ALdistortion_getParamf(effect, context, param, vals);
}

DEFINE_ALEFFECT_VTABLE(ALdistortion);
