/**
 * OpenAL cross platform audio library
 * Copyright (C) 2009 by Chris Robinson.
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

#include "config.h"

#include <math.h>
#include <stdlib.h>

#include "alMain.h"
#include "alFilter.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
#include "alu.h"


typedef struct ALechoState {
    // Must be first in all effects!
    ALeffectState state;

    ALfloat *SampleBuffer;
    ALuint BufferLength;

    // The echo is two tap. The delay is the number of samples from before the
    // current offset
    struct {
        ALuint delay;
    } Tap[2];
    ALuint Offset;
    /* The panning gains for the two taps */
    ALfloat Gain[2][MaxChannels];

    ALfloat FeedGain;

    FILTER iirFilter;
    ALfloat history[2];
} ALechoState;

static ALvoid EchoDestroy(ALeffectState *effect)
{
    ALechoState *state = (ALechoState*)effect;
    if(state)
    {
        free(state->SampleBuffer);
        state->SampleBuffer = NULL;
        free(state);
    }
}

static ALboolean EchoDeviceUpdate(ALeffectState *effect, ALCdevice *Device)
{
    ALechoState *state = (ALechoState*)effect;
    ALuint maxlen, i;

    // Use the next power of 2 for the buffer length, so the tap offsets can be
    // wrapped using a mask instead of a modulo
    maxlen  = fastf2u(AL_ECHO_MAX_DELAY * Device->Frequency) + 1;
    maxlen += fastf2u(AL_ECHO_MAX_LRDELAY * Device->Frequency) + 1;
    maxlen  = NextPowerOf2(maxlen);

    if(maxlen != state->BufferLength)
    {
        void *temp;

        temp = realloc(state->SampleBuffer, maxlen * sizeof(ALfloat));
        if(!temp)
            return AL_FALSE;
        state->SampleBuffer = temp;
        state->BufferLength = maxlen;
    }
    for(i = 0;i < state->BufferLength;i++)
        state->SampleBuffer[i] = 0.0f;

    return AL_TRUE;
}

static ALvoid EchoUpdate(ALeffectState *effect, ALCdevice *Device, const ALeffectslot *Slot)
{
    ALechoState *state = (ALechoState*)effect;
    ALuint frequency = Device->Frequency;
    ALfloat lrpan, cw, g, gain;
    ALfloat dirGain;
    ALuint i;

    state->Tap[0].delay = fastf2u(Slot->effect.Echo.Delay * frequency) + 1;
    state->Tap[1].delay = fastf2u(Slot->effect.Echo.LRDelay * frequency);
    state->Tap[1].delay += state->Tap[0].delay;

    lrpan = Slot->effect.Echo.Spread;

    state->FeedGain = Slot->effect.Echo.Feedback;

    cw = cosf(F_PI*2.0f * LOWPASSFREQREF / frequency);
    g = 1.0f - Slot->effect.Echo.Damping;
    state->iirFilter.coeff = lpCoeffCalc(g, cw);

    gain = Slot->Gain;
    for(i = 0;i < MaxChannels;i++)
    {
        state->Gain[0][i] = 0.0f;
        state->Gain[1][i] = 0.0f;
    }

    dirGain = fabsf(lrpan);

    /* First tap panning */
    ComputeAngleGains(Device, atan2f(-lrpan, 0.0f), (1.0f-dirGain)*F_PI, gain, state->Gain[0]);

    /* Second tap panning */
    ComputeAngleGains(Device, atan2f(+lrpan, 0.0f), (1.0f-dirGain)*F_PI, gain, state->Gain[1]);
}

static ALvoid EchoProcess(ALeffectState *effect, ALuint SamplesToDo, const ALfloat *RESTRICT SamplesIn, ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE])
{
    ALechoState *state = (ALechoState*)effect;
    const ALuint mask = state->BufferLength-1;
    const ALuint tap1 = state->Tap[0].delay;
    const ALuint tap2 = state->Tap[1].delay;
    ALuint offset = state->Offset;
    ALfloat smp;
    ALuint i, k;

    for(i = 0;i < SamplesToDo;i++,offset++)
    {
        /* First tap */
        smp = state->SampleBuffer[(offset-tap1) & mask];
        for(k = 0;k < MaxChannels;k++)
            SamplesOut[k][i] += smp * state->Gain[0][k];

        /* Second tap */
        smp = state->SampleBuffer[(offset-tap2) & mask];
        for(k = 0;k < MaxChannels;k++)
            SamplesOut[k][i] += smp * state->Gain[1][k];

        // Apply damping and feedback gain to the second tap, and mix in the
        // new sample
        smp = lpFilter2P(&state->iirFilter, 0, smp+SamplesIn[i]);
        state->SampleBuffer[offset&mask] = smp * state->FeedGain;
    }
    state->Offset = offset;
}

ALeffectState *EchoCreate(void)
{
    ALechoState *state;

    state = malloc(sizeof(*state));
    if(!state)
        return NULL;

    state->state.Destroy = EchoDestroy;
    state->state.DeviceUpdate = EchoDeviceUpdate;
    state->state.Update = EchoUpdate;
    state->state.Process = EchoProcess;

    state->BufferLength = 0;
    state->SampleBuffer = NULL;

    state->Tap[0].delay = 0;
    state->Tap[1].delay = 0;
    state->Offset = 0;

    state->iirFilter.coeff = 0.0f;
    state->iirFilter.history[0] = 0.0f;
    state->iirFilter.history[1] = 0.0f;

    return &state->state;
}

void echo_SetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val)
{ (void)effect;(void)param;(void)val; alSetError(context, AL_INVALID_ENUM); }
void echo_SetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{
    echo_SetParami(effect, context, param, vals[0]);
}
void echo_SetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    switch(param)
    {
        case AL_ECHO_DELAY:
            if(val >= AL_ECHO_MIN_DELAY && val <= AL_ECHO_MAX_DELAY)
                effect->Echo.Delay = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_ECHO_LRDELAY:
            if(val >= AL_ECHO_MIN_LRDELAY && val <= AL_ECHO_MAX_LRDELAY)
                effect->Echo.LRDelay = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_ECHO_DAMPING:
            if(val >= AL_ECHO_MIN_DAMPING && val <= AL_ECHO_MAX_DAMPING)
                effect->Echo.Damping = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_ECHO_FEEDBACK:
            if(val >= AL_ECHO_MIN_FEEDBACK && val <= AL_ECHO_MAX_FEEDBACK)
                effect->Echo.Feedback = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_ECHO_SPREAD:
            if(val >= AL_ECHO_MIN_SPREAD && val <= AL_ECHO_MAX_SPREAD)
                effect->Echo.Spread = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        default:
            alSetError(context, AL_INVALID_ENUM);
            break;
    }
}
void echo_SetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{
    echo_SetParamf(effect, context, param, vals[0]);
}

void echo_GetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint *val)
{ (void)effect;(void)param;(void)val; alSetError(context, AL_INVALID_ENUM); }
void echo_GetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{
    echo_GetParami(effect, context, param, vals);
}
void echo_GetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val)
{
    switch(param)
    {
        case AL_ECHO_DELAY:
            *val = effect->Echo.Delay;
            break;

        case AL_ECHO_LRDELAY:
            *val = effect->Echo.LRDelay;
            break;

        case AL_ECHO_DAMPING:
            *val = effect->Echo.Damping;
            break;

        case AL_ECHO_FEEDBACK:
            *val = effect->Echo.Feedback;
            break;

        case AL_ECHO_SPREAD:
            *val = effect->Echo.Spread;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM);
            break;
    }
}
void echo_GetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{
    echo_GetParamf(effect, context, param, vals);
}
