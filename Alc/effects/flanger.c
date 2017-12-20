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


enum FlangerWaveForm {
    FWF_Triangle = AL_FLANGER_WAVEFORM_TRIANGLE,
    FWF_Sinusoid = AL_FLANGER_WAVEFORM_SINUSOID
};

typedef struct ALflangerState {
    DERIVE_FROM_TYPE(ALeffectState);

    ALfloat *SampleBuffer;
    ALsizei BufferLength;
    ALsizei offset;

    ALsizei lfo_offset;
    ALsizei lfo_range;
    ALfloat lfo_scale;
    ALint lfo_disp;

    /* Gains for left and right sides */
    struct {
        ALfloat Current[MAX_OUTPUT_CHANNELS];
        ALfloat Target[MAX_OUTPUT_CHANNELS];
    } Gains[2];

    /* effect parameters */
    enum FlangerWaveForm waveform;
    ALint delay;
    ALfloat depth;
    ALfloat feedback;
} ALflangerState;

static ALvoid ALflangerState_Destruct(ALflangerState *state);
static ALboolean ALflangerState_deviceUpdate(ALflangerState *state, ALCdevice *Device);
static ALvoid ALflangerState_update(ALflangerState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props);
static ALvoid ALflangerState_process(ALflangerState *state, ALsizei SamplesToDo, const ALfloat (*restrict SamplesIn)[BUFFERSIZE], ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALsizei NumChannels);
DECLARE_DEFAULT_ALLOCATORS(ALflangerState)

DEFINE_ALEFFECTSTATE_VTABLE(ALflangerState);


static void ALflangerState_Construct(ALflangerState *state)
{
    ALeffectState_Construct(STATIC_CAST(ALeffectState, state));
    SET_VTABLE2(ALflangerState, ALeffectState, state);

    state->BufferLength = 0;
    state->SampleBuffer = NULL;
    state->offset = 0;
    state->lfo_offset = 0;
    state->lfo_range = 1;
    state->waveform = FWF_Triangle;
}

static ALvoid ALflangerState_Destruct(ALflangerState *state)
{
    al_free(state->SampleBuffer);
    state->SampleBuffer = NULL;

    ALeffectState_Destruct(STATIC_CAST(ALeffectState,state));
}

static ALboolean ALflangerState_deviceUpdate(ALflangerState *state, ALCdevice *Device)
{
    ALsizei maxlen;

    maxlen = fastf2i(AL_FLANGER_MAX_DELAY * 2.0f * Device->Frequency) + 1;
    maxlen = NextPowerOf2(maxlen);

    if(maxlen != state->BufferLength)
    {
        void *temp = al_calloc(16, maxlen * sizeof(ALfloat));
        if(!temp) return AL_FALSE;

        al_free(state->SampleBuffer);
        state->SampleBuffer = temp;

        state->BufferLength = maxlen;
    }

    memset(state->SampleBuffer, 0, state->BufferLength*sizeof(ALfloat));
    memset(state->Gains, 0, sizeof(state->Gains));

    return AL_TRUE;
}

static ALvoid ALflangerState_update(ALflangerState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props)
{
    const ALCdevice *device = context->Device;
    ALfloat frequency = (ALfloat)device->Frequency;
    ALfloat coeffs[MAX_AMBI_COEFFS];
    ALfloat delay;
    ALfloat rate;
    ALint phase;

    switch(props->Flanger.Waveform)
    {
        case AL_FLANGER_WAVEFORM_TRIANGLE:
            state->waveform = FWF_Triangle;
            break;
        case AL_FLANGER_WAVEFORM_SINUSOID:
            state->waveform = FWF_Sinusoid;
            break;
    }

    /* The LFO depth is scaled to be relative to the sample delay. */
    delay = props->Flanger.Delay*frequency * FRACTIONONE;
    state->depth = props->Flanger.Depth * delay;

    /* Offset the delay so that the center point remains the same with the LFO
     * ranging from 0...2 instead of -1...+1.
     */
    state->delay = fastf2i(delay-state->depth + 0.5f);

    state->feedback = props->Flanger.Feedback;

    /* Gains for left and right sides */
    CalcAngleCoeffs(-F_PI_2, 0.0f, 0.0f, coeffs);
    ComputePanningGains(device->Dry, coeffs, slot->Params.Gain, state->Gains[0].Target);
    CalcAngleCoeffs( F_PI_2, 0.0f, 0.0f, coeffs);
    ComputePanningGains(device->Dry, coeffs, slot->Params.Gain, state->Gains[1].Target);

    phase = props->Flanger.Phase;
    rate = props->Flanger.Rate;
    if(!(rate > 0.0f))
    {
        state->lfo_offset = 0;
        state->lfo_range = 1;
        state->lfo_scale = 0.0f;
        state->lfo_disp = 0;
    }
    else
    {
        /* Calculate LFO coefficient (number of samples per cycle). Limit the
         * max range to avoid overflow when calculating the displacement.
         */
        ALsizei lfo_range = mini(fastf2i(frequency/rate + 0.5f), INT_MAX/360 - 180);

        state->lfo_offset = fastf2i((ALfloat)state->lfo_offset/state->lfo_range*
                                    lfo_range + 0.5f) % lfo_range;
        state->lfo_range = lfo_range;
        switch(state->waveform)
        {
            case FWF_Triangle:
                state->lfo_scale = 4.0f / state->lfo_range;
                break;
            case FWF_Sinusoid:
                state->lfo_scale = F_TAU / state->lfo_range;
                break;
        }

        /* Calculate lfo phase displacement */
        if(phase < 0) phase = 360 + phase;
        state->lfo_disp = (state->lfo_range*phase + 180) / 360;
    }
}

static void GetTriangleDelays(ALint *restrict delays, ALsizei offset, const ALsizei lfo_range,
                              const ALfloat lfo_scale, const ALfloat depth, const ALsizei delay,
                              const ALsizei todo)
{
    ALsizei i;
    for(i = 0;i < todo;i++)
    {
        delays[i] = fastf2i((2.0f - fabsf(2.0f - lfo_scale*offset)) * depth) + delay;
        offset = (offset+1)%lfo_range;
    }
}

static void GetSinusoidDelays(ALint *restrict delays, ALsizei offset, const ALsizei lfo_range,
                              const ALfloat lfo_scale, const ALfloat depth, const ALsizei delay,
                              const ALsizei todo)
{
    ALsizei i;
    for(i = 0;i < todo;i++)
    {
        delays[i] = fastf2i((sinf(lfo_scale*offset)+1.0f) * depth) + delay;
        offset = (offset+1)%lfo_range;
    }
}

static ALvoid ALflangerState_process(ALflangerState *state, ALsizei SamplesToDo, const ALfloat (*restrict SamplesIn)[BUFFERSIZE], ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALsizei NumChannels)
{
    const ALsizei bufmask = state->BufferLength-1;
    const ALfloat feedback = state->feedback;
    const ALsizei avgdelay = (state->delay+fastf2i(state->depth) + (FRACTIONONE>>1)) >>
                             FRACTIONBITS;
    ALfloat *restrict delaybuf = state->SampleBuffer;
    ALsizei offset = state->offset;
    ALsizei i, c;
    ALsizei base;

    for(base = 0;base < SamplesToDo;)
    {
        const ALsizei todo = mini(256, SamplesToDo-base);
        ALint moddelays[2][256];
        ALfloat temps[2][256];

        if(state->waveform == FWF_Triangle)
        {
            GetTriangleDelays(moddelays[0], state->lfo_offset, state->lfo_range, state->lfo_scale,
                              state->depth, state->delay, todo);
            GetTriangleDelays(moddelays[1], (state->lfo_offset+state->lfo_disp)%state->lfo_range,
                              state->lfo_range, state->lfo_scale, state->depth, state->delay,
                              todo);
        }
        else /*if(state->waveform == FWF_Sinusoid)*/
        {
            GetSinusoidDelays(moddelays[0], state->lfo_offset, state->lfo_range, state->lfo_scale,
                              state->depth, state->delay, todo);
            GetSinusoidDelays(moddelays[1], (state->lfo_offset+state->lfo_disp)%state->lfo_range,
                              state->lfo_range, state->lfo_scale, state->depth, state->delay,
                              todo);
        }
        state->lfo_offset = (state->lfo_offset+todo) % state->lfo_range;

        for(i = 0;i < todo;i++)
        {
            ALint delay;
            ALfloat mu;

            // Feed the buffer's input first (necessary for delays < 1).
            delaybuf[offset&bufmask] = SamplesIn[0][base+i];

            // Tap for the left output.
            delay = moddelays[0][i] >> FRACTIONBITS;
            mu = (moddelays[0][i]&FRACTIONMASK) * (1.0f/FRACTIONONE);
            temps[0][i] = delaybuf[(offset-delay) & bufmask]*(1.0f-mu) +
                          delaybuf[(offset-(delay+1)) & bufmask]*mu;

            // Tap for the right output.
            delay = moddelays[1][i] >> FRACTIONBITS;
            mu = (moddelays[1][i]&FRACTIONMASK) * (1.0f/FRACTIONONE);
            temps[1][i] = delaybuf[(offset-delay) & bufmask]*(1.0f-mu) +
                          delaybuf[(offset-(delay+1)) & bufmask]*mu;

            // Accumulate feedback from the average delay.
            delaybuf[offset&bufmask] += delaybuf[(offset-avgdelay) & bufmask] * feedback;
            offset++;
        }

        for(c = 0;c < 2;c++)
            MixSamples(temps[c], NumChannels, SamplesOut, state->Gains[c].Current,
                       state->Gains[c].Target, 0, base, todo);

        base += todo;
    }

    state->offset = offset;
}


typedef struct ALflangerStateFactory {
    DERIVE_FROM_TYPE(ALeffectStateFactory);
} ALflangerStateFactory;

ALeffectState *ALflangerStateFactory_create(ALflangerStateFactory *UNUSED(factory))
{
    ALflangerState *state;

    NEW_OBJ0(state, ALflangerState)();
    if(!state) return NULL;

    return STATIC_CAST(ALeffectState, state);
}

DEFINE_ALEFFECTSTATEFACTORY_VTABLE(ALflangerStateFactory);

ALeffectStateFactory *ALflangerStateFactory_getFactory(void)
{
    static ALflangerStateFactory FlangerFactory = { { GET_VTABLE2(ALflangerStateFactory, ALeffectStateFactory) } };

    return STATIC_CAST(ALeffectStateFactory, &FlangerFactory);
}


void ALflanger_setParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_FLANGER_WAVEFORM:
            if(!(val >= AL_FLANGER_MIN_WAVEFORM && val <= AL_FLANGER_MAX_WAVEFORM))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Flanger.Waveform = val;
            break;

        case AL_FLANGER_PHASE:
            if(!(val >= AL_FLANGER_MIN_PHASE && val <= AL_FLANGER_MAX_PHASE))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Flanger.Phase = val;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALflanger_setParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{
    ALflanger_setParami(effect, context, param, vals[0]);
}
void ALflanger_setParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_FLANGER_RATE:
            if(!(val >= AL_FLANGER_MIN_RATE && val <= AL_FLANGER_MAX_RATE))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Flanger.Rate = val;
            break;

        case AL_FLANGER_DEPTH:
            if(!(val >= AL_FLANGER_MIN_DEPTH && val <= AL_FLANGER_MAX_DEPTH))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Flanger.Depth = val;
            break;

        case AL_FLANGER_FEEDBACK:
            if(!(val >= AL_FLANGER_MIN_FEEDBACK && val <= AL_FLANGER_MAX_FEEDBACK))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Flanger.Feedback = val;
            break;

        case AL_FLANGER_DELAY:
            if(!(val >= AL_FLANGER_MIN_DELAY && val <= AL_FLANGER_MAX_DELAY))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Flanger.Delay = val;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALflanger_setParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{
    ALflanger_setParamf(effect, context, param, vals[0]);
}

void ALflanger_getParami(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *val)
{
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_FLANGER_WAVEFORM:
            *val = props->Flanger.Waveform;
            break;

        case AL_FLANGER_PHASE:
            *val = props->Flanger.Phase;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALflanger_getParamiv(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{
    ALflanger_getParami(effect, context, param, vals);
}
void ALflanger_getParamf(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val)
{
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_FLANGER_RATE:
            *val = props->Flanger.Rate;
            break;

        case AL_FLANGER_DEPTH:
            *val = props->Flanger.Depth;
            break;

        case AL_FLANGER_FEEDBACK:
            *val = props->Flanger.Feedback;
            break;

        case AL_FLANGER_DELAY:
            *val = props->Flanger.Delay;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALflanger_getParamfv(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{
    ALflanger_getParamf(effect, context, param, vals);
}

DEFINE_ALEFFECT_VTABLE(ALflanger);
