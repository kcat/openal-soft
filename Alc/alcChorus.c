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


typedef struct ALchorusState {
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
} ALchorusState;

static ALvoid ChorusDestroy(ALeffectState *effect)
{
    ALchorusState *state = (ALchorusState*)effect;

    if(state)
    {
        free(state->SampleBufferLeft);
        state->SampleBufferLeft = NULL;

        free(state->SampleBufferRight);
        state->SampleBufferRight = NULL;

        free(state);
    }
}

static ALboolean ChorusDeviceUpdate(ALeffectState *effect, ALCdevice *Device)
{
    ALchorusState *state = (ALchorusState*)effect;
    ALuint maxlen;
    ALuint it;

    maxlen = fastf2u(AL_CHORUS_MAX_DELAY * 3.0f * Device->Frequency) + 1;
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

static ALvoid ChorusUpdate(ALeffectState *effect, ALCdevice *Device, const ALeffectslot *Slot)
{
    ALchorusState *state = (ALchorusState*)effect;
    ALuint it;

    for (it = 0; it < MaxChannels; it++)
    {
        state->Gain[0][it] = 0.0f;
        state->Gain[1][it] = 0.0f;
    }

    state->waveform = Slot->effect.Chorus.Waveform;
    state->phase = Slot->effect.Chorus.Phase;
    state->rate = Slot->effect.Chorus.Rate;
    state->depth = Slot->effect.Chorus.Depth;
    state->feedback = Slot->effect.Chorus.Feedback;
    state->delay = Slot->effect.Chorus.Delay;
    state->frequency=(ALfloat)Device->Frequency;

    /* Gains for left and right sides */
    ComputeAngleGains(Device, atan2f(-1.0f, 0.0f), 0.0f, Slot->Gain, state->Gain[0]);
    ComputeAngleGains(Device, atan2f(+1.0f, 0.0f), 0.0f, Slot->Gain, state->Gain[1]);

    /* Calculate LFO coefficient */
    switch (state->waveform)
    {
        case AL_CHORUS_WAVEFORM_TRIANGLE:
             if (state->rate == 0.0f)
             {
                 state->lfo_coeff = 0.0f;
             }
             else
             {
                 state->lfo_coeff = 1.0f / ((ALfloat)Device->Frequency / state->rate);
             }
             break;
        case AL_CHORUS_WAVEFORM_SINUSOID:
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

static ALvoid ChorusProcess(ALeffectState *effect, ALuint SamplesToDo, const ALfloat *RESTRICT SamplesIn, ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE])
{
    ALchorusState *state = (ALchorusState*)effect;
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
        case AL_CHORUS_WAVEFORM_TRIANGLE:
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
        case AL_CHORUS_WAVEFORM_SINUSOID:
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

ALeffectState *ChorusCreate(void)
{
    ALchorusState *state;

    state = malloc(sizeof(*state));
    if(!state)
        return NULL;

    state->state.Destroy = ChorusDestroy;
    state->state.DeviceUpdate = ChorusDeviceUpdate;
    state->state.Update = ChorusUpdate;
    state->state.Process = ChorusProcess;

    state->BufferLength = 0;
    state->SampleBufferLeft = NULL;
    state->SampleBufferRight = NULL;
    state->offset = 0;

    return &state->state;
}

void chorus_SetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val)
{
    switch(param)
    {
        case AL_CHORUS_WAVEFORM:
            if(val >= AL_CHORUS_MIN_WAVEFORM && val <= AL_CHORUS_MAX_WAVEFORM)
                effect->Chorus.Waveform = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_CHORUS_PHASE:
            if(val >= AL_CHORUS_MIN_PHASE && val <= AL_CHORUS_MAX_PHASE)
                effect->Chorus.Phase = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        default:
            alSetError(context, AL_INVALID_ENUM);
            break;
    }
}
void chorus_SetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{
    chorus_SetParami(effect, context, param, vals[0]);
}
void chorus_SetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    switch(param)
    {
        case AL_CHORUS_RATE:
            if(val >= AL_CHORUS_MIN_RATE && val <= AL_CHORUS_MAX_RATE)
                effect->Chorus.Rate = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_CHORUS_DEPTH:
            if(val >= AL_CHORUS_MIN_DEPTH && val <= AL_CHORUS_MAX_DEPTH)
                effect->Chorus.Depth = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_CHORUS_FEEDBACK:
            if(val >= AL_CHORUS_MIN_FEEDBACK && val <= AL_CHORUS_MAX_FEEDBACK)
                effect->Chorus.Feedback = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_CHORUS_DELAY:
            if(val >= AL_CHORUS_MIN_DELAY && val <= AL_CHORUS_MAX_DELAY)
                effect->Chorus.Delay = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        default:
            alSetError(context, AL_INVALID_ENUM);
            break;
    }
}
void chorus_SetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{
    chorus_SetParamf(effect, context, param, vals[0]);
}

void chorus_GetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint *val)
{
    switch(param)
    {
        case AL_CHORUS_WAVEFORM:
            *val = effect->Chorus.Waveform;
            break;

        case AL_CHORUS_PHASE:
            *val = effect->Chorus.Phase;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM);
            break;
    }
}
void chorus_GetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{
    chorus_GetParami(effect, context, param, vals);
}
void chorus_GetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val)
{
    switch(param)
    {
        case AL_CHORUS_RATE:
            *val = effect->Chorus.Rate;
            break;

        case AL_CHORUS_DEPTH:
            *val = effect->Chorus.Depth;
            break;

        case AL_CHORUS_FEEDBACK:
            *val = effect->Chorus.Feedback;
            break;

        case AL_CHORUS_DELAY:
            *val = effect->Chorus.Delay;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM);
            break;
    }
}
void chorus_GetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{
    chorus_GetParamf(effect, context, param, vals);
}
