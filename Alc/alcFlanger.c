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


typedef struct ALflangerState {
    // Must be first in all effects!
    ALeffectState state;

    ALfloat *SampleBufferLeft;
    ALfloat *SampleBufferRight;
    ALuint BufferLength;
    ALint offset;
    ALfloat lfo_coeff;
    ALint lfo_disp;

    /* Gains for left and right sides */
    ALfloat Gain[2][MaxChannels];

    /* effect parameters */
    ALint waveform;
    ALint phase;
    ALfloat rate;
    ALfloat depth;
    ALfloat feedback;
    ALfloat delay;
    ALfloat frequency;
} ALflangerState;

static ALvoid FlangerDestroy(ALeffectState *effect)
{
    ALflangerState *state = (ALflangerState*)effect;

    if(state)
    {
        free(state->SampleBufferLeft);
        state->SampleBufferLeft = NULL;

        free(state->SampleBufferRight);
        state->SampleBufferRight = NULL;

        free(state);
    }
}

static ALboolean FlangerDeviceUpdate(ALeffectState *effect, ALCdevice *Device)
{
    ALflangerState *state = (ALflangerState*)effect;
    ALuint maxlen;
    ALuint it;

    maxlen = fastf2u(AL_FLANGER_MAX_DELAY * 3.0f * Device->Frequency) + 1;
    maxlen = NextPowerOf2(maxlen);

    if (maxlen != state->BufferLength)
    {
        void *temp;

        temp = realloc(state->SampleBufferLeft, maxlen * sizeof(ALfloat));
        if (!temp)
        {
            return AL_FALSE;
        }
        state->SampleBufferLeft = temp;

        temp = realloc(state->SampleBufferRight, maxlen * sizeof(ALfloat));
        if (!temp)
        {
            return AL_FALSE;
        }
        state->SampleBufferRight = temp;

        state->BufferLength = maxlen;
    }

    for (it = 0; it < state->BufferLength; it++)
    {
        state->SampleBufferLeft[it] = 0.0f;
        state->SampleBufferRight[it] = 0.0f;
    }

    state->frequency=(ALfloat)Device->Frequency;

    return AL_TRUE;
}

static ALvoid FlangerUpdate(ALeffectState *effect, ALCdevice *Device, const ALeffectslot *Slot)
{
    ALflangerState *state = (ALflangerState*)effect;
    ALuint it;

    for (it = 0; it < MaxChannels; it++)
    {
        state->Gain[0][it] = 0.0f;
        state->Gain[1][it] = 0.0f;
    }

    state->waveform = Slot->effect.Flanger.Waveform;
    state->phase = Slot->effect.Flanger.Phase;
    state->rate = Slot->effect.Flanger.Rate;
    state->depth = Slot->effect.Flanger.Depth;
    state->feedback = Slot->effect.Flanger.Feedback;
    state->delay = Slot->effect.Flanger.Delay;
    state->frequency=(ALfloat)Device->Frequency;

    /* Gains for left and right sides */
    ComputeAngleGains(Device, atan2f(-1.0f, 0.0f), 0.0f, Slot->Gain, state->Gain[0]);
    ComputeAngleGains(Device, atan2f(+1.0f, 0.0f), 0.0f, Slot->Gain, state->Gain[1]);

    /* Calculate LFO coefficient */
    switch (state->waveform)
    {
        case AL_FLANGER_WAVEFORM_TRIANGLE:
             if (state->rate == 0.0f)
             {
                 state->lfo_coeff = 0.0f;
             }
             else
             {
                 state->lfo_coeff = 1.0f / ((ALfloat)Device->Frequency / state->rate);
             }
             break;
        case AL_FLANGER_WAVEFORM_SINUSOID:
             if (state->rate == 0.0f)
             {
                 state->lfo_coeff = 0.0f;
             }
             else
             {
                 state->lfo_coeff = F_PI * 2.0f / ((ALfloat)Device->Frequency / state->rate);
             }
             break;
    }

    /* Calculate lfo phase displacement */
    if ((state->phase == 0) || (state->rate == 0.0f))
    {
        state->lfo_disp = 0;
    }
    else
    {
        state->lfo_disp = (ALint) ((ALfloat)Device->Frequency /
                          state->rate / (360.0f / (ALfloat)state->phase));
    }
}

static ALvoid FlangerProcess(ALeffectState *effect, ALuint SamplesToDo, const ALfloat *RESTRICT SamplesIn, ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE])
{
    ALflangerState *state = (ALflangerState*)effect;
    const ALuint mask = state->BufferLength-1;
    ALuint it;
    ALuint kt;
    ALint offset;
    ALfloat lfo_value_left = 0.0f;
    ALfloat lfo_value_right = 0.0f;
    ALint delay_left = 0;
    ALint delay_right = 0;
    ALfloat smp;

    offset=state->offset;

    switch (state->waveform)
    {
        case AL_FLANGER_WAVEFORM_TRIANGLE:
             for (it = 0; it < SamplesToDo; it++, offset++)
             {
                 lfo_value_left = 2.0f - fabsf(2.0f - fmodf(state->lfo_coeff *
                                  offset * 4.0f, 4.0f));
                 lfo_value_left *= state->depth * state->delay;
                 lfo_value_left += state->delay;
                 delay_left = (ALint)(lfo_value_left * state->frequency);
                 lfo_value_right = 2.0f - fabsf(2.0f - fmodf(state->lfo_coeff *
                                   (offset + state->lfo_disp) * 4.0f, 4.0f));
                 lfo_value_right *= state->depth * state->delay;
                 lfo_value_right += state->delay;
                 delay_right = (ALint)(lfo_value_right * state->frequency);

                 smp = state->SampleBufferLeft[(offset-delay_left) & mask];
                 for (kt = 0; kt < MaxChannels; kt++)
                 {
                     SamplesOut[kt][it] += smp * state->Gain[0][kt];
                 }
                 state->SampleBufferLeft[offset & mask] = (smp + SamplesIn[it]) * state->feedback;
                 smp = state->SampleBufferRight[(offset-delay_right) & mask];
                 for (kt = 0; kt < MaxChannels; kt++)
                 {
                     SamplesOut[kt][it] += smp * state->Gain[1][kt];
                 }
                 state->SampleBufferRight[offset & mask] = (smp + SamplesIn[it]) * state->feedback;
             }
             break;
        case AL_FLANGER_WAVEFORM_SINUSOID:
             for (it = 0; it < SamplesToDo; it++, offset++)
             {
                 lfo_value_left = 1.0f + sinf(fmodf(state->lfo_coeff *
                                  offset, 2 * F_PI));
                 lfo_value_left *= state->depth * state->delay;
                 lfo_value_left += state->delay;
                 delay_left = (ALint)(lfo_value_left * state->frequency);
                 lfo_value_right = 1.0f + sinf(fmodf(state->lfo_coeff *
                                   (offset + state->lfo_disp), 2 * F_PI));
                 lfo_value_right *= state->depth * state->delay;
                 lfo_value_right += state->delay;
                 delay_right = (ALint)(lfo_value_right * state->frequency);

                 smp = state->SampleBufferLeft[(offset-delay_left) & mask];
                 for (kt = 0; kt < MaxChannels; kt++)
                 {
                     SamplesOut[kt][it] += smp * state->Gain[0][kt];
                 }
                 state->SampleBufferLeft[offset & mask] = (smp + SamplesIn[it]) * state->feedback;
                 smp = state->SampleBufferRight[(offset-delay_right) & mask];
                 for (kt = 0; kt < MaxChannels; kt++)
                 {
                     SamplesOut[kt][it] += smp * state->Gain[1][kt];
                 }
                 state->SampleBufferRight[offset & mask] = (smp + SamplesIn[it]) * state->feedback;
             }
             break;
    }

    state->offset=offset;
}

ALeffectState *FlangerCreate(void)
{
    ALflangerState *state;

    state = malloc(sizeof(*state));
    if(!state)
        return NULL;

    state->state.Destroy = FlangerDestroy;
    state->state.DeviceUpdate = FlangerDeviceUpdate;
    state->state.Update = FlangerUpdate;
    state->state.Process = FlangerProcess;

    state->BufferLength = 0;
    state->SampleBufferLeft = NULL;
    state->SampleBufferRight = NULL;
    state->offset = 0;

    return &state->state;
}

void flanger_SetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val)
{
    switch(param)
    {
        case AL_FLANGER_WAVEFORM:
            if(val >= AL_FLANGER_MIN_WAVEFORM && val <= AL_FLANGER_MAX_WAVEFORM)
                effect->Flanger.Waveform = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_FLANGER_PHASE:
            if(val >= AL_FLANGER_MIN_PHASE && val <= AL_FLANGER_MAX_PHASE)
                effect->Flanger.Phase = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        default:
            alSetError(context, AL_INVALID_ENUM);
            break;
    }
}
void flanger_SetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{
    flanger_SetParami(effect, context, param, vals[0]);
}
void flanger_SetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    switch(param)
    {
        case AL_FLANGER_RATE:
            if(val >= AL_FLANGER_MIN_RATE && val <= AL_FLANGER_MAX_RATE)
                effect->Flanger.Rate = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_FLANGER_DEPTH:
            if(val >= AL_FLANGER_MIN_DEPTH && val <= AL_FLANGER_MAX_DEPTH)
                effect->Flanger.Depth = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_FLANGER_FEEDBACK:
            if(val >= AL_FLANGER_MIN_FEEDBACK && val <= AL_FLANGER_MAX_FEEDBACK)
                effect->Flanger.Feedback = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_FLANGER_DELAY:
            if(val >= AL_FLANGER_MIN_DELAY && val <= AL_FLANGER_MAX_DELAY)
                effect->Flanger.Delay = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        default:
            alSetError(context, AL_INVALID_ENUM);
            break;
    }
}
void flanger_SetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{
    flanger_SetParamf(effect, context, param, vals[0]);
}

void flanger_GetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint *val)
{
    switch(param)
    {
        case AL_FLANGER_WAVEFORM:
            *val = effect->Flanger.Waveform;
            break;

        case AL_FLANGER_PHASE:
            *val = effect->Flanger.Phase;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM);
            break;
    }
}
void flanger_GetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{
    flanger_GetParami(effect, context, param, vals);
}
void flanger_GetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val)
{
    switch(param)
    {
        case AL_FLANGER_RATE:
            *val = effect->Flanger.Rate;
            break;

        case AL_FLANGER_DEPTH:
            *val = effect->Flanger.Depth;
            break;

        case AL_FLANGER_FEEDBACK:
            *val = effect->Flanger.Feedback;
            break;

        case AL_FLANGER_DELAY:
            *val = effect->Flanger.Delay;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM);
            break;
    }
}
void flanger_GetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{
    flanger_GetParamf(effect, context, param, vals);
}
