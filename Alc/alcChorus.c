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
    DERIVE_FROM_TYPE(ALeffectState);

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
    ALint delay;
    ALfloat depth;
    ALfloat feedback;
} ALchorusState;

static ALvoid ChorusDestroy(ALeffectState *effect)
{
    ALchorusState *state = GET_PARENT_TYPE(ALchorusState, ALeffectState, effect);
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
    ALchorusState *state = GET_PARENT_TYPE(ALchorusState, ALeffectState, effect);
    ALuint maxlen;
    ALuint it;

    maxlen = fastf2u(AL_CHORUS_MAX_DELAY * 3.0f * Device->Frequency) + 1;
    maxlen = NextPowerOf2(maxlen);

    if(maxlen != state->BufferLength)
    {
        void *temp;

        temp = realloc(state->SampleBufferLeft, maxlen * sizeof(ALfloat));
        if(!temp) return AL_FALSE;
        state->SampleBufferLeft = temp;

        temp = realloc(state->SampleBufferRight, maxlen * sizeof(ALfloat));
        if(!temp) return AL_FALSE;
        state->SampleBufferRight = temp;

        state->BufferLength = maxlen;
    }

    for(it = 0;it < state->BufferLength;it++)
    {
        state->SampleBufferLeft[it] = 0.0f;
        state->SampleBufferRight[it] = 0.0f;
    }

    return AL_TRUE;
}

static ALvoid ChorusUpdate(ALeffectState *effect, ALCdevice *Device, const ALeffectslot *Slot)
{
    ALchorusState *state = GET_PARENT_TYPE(ALchorusState, ALeffectState, effect);
    ALfloat frequency = Device->Frequency;
    ALfloat rate;
    ALint phase;
    ALuint it;

    for (it = 0; it < MaxChannels; it++)
    {
        state->Gain[0][it] = 0.0f;
        state->Gain[1][it] = 0.0f;
    }

    state->waveform = Slot->effect.Chorus.Waveform;
    state->depth = Slot->effect.Chorus.Depth;
    state->feedback = Slot->effect.Chorus.Feedback;
    state->delay = fastf2i(Slot->effect.Chorus.Delay * frequency);

    /* Gains for left and right sides */
    ComputeAngleGains(Device, atan2f(-1.0f, 0.0f), 0.0f, Slot->Gain, state->Gain[0]);
    ComputeAngleGains(Device, atan2f(+1.0f, 0.0f), 0.0f, Slot->Gain, state->Gain[1]);

    phase = Slot->effect.Chorus.Phase;
    rate = Slot->effect.Chorus.Rate;

    /* Calculate LFO coefficient */
    switch (state->waveform)
    {
        case AL_CHORUS_WAVEFORM_TRIANGLE:
             if(rate == 0.0f)
                 state->lfo_coeff = 0.0f;
             else
                 state->lfo_coeff = 1.0f / (frequency / rate);
             break;
        case AL_CHORUS_WAVEFORM_SINUSOID:
             if(rate == 0.0f)
                 state->lfo_coeff = 0.0f;
             else
                 state->lfo_coeff = F_PI*2.0f / (frequency / rate);
             break;
    }

    /* Calculate lfo phase displacement */
    if(phase == 0 || rate == 0.0f)
        state->lfo_disp = 0;
    else
        state->lfo_disp = fastf2i(frequency / rate / (360.0f/phase));
}

static __inline void Triangle(ALint *delay_left, ALint *delay_right, ALint offset, const ALchorusState *state)
{
    ALfloat lfo_value;

    lfo_value = 2.0f - fabsf(2.0f - fmodf(state->lfo_coeff*offset*4.0f, 4.0f));
    lfo_value *= state->depth * state->delay;
    *delay_left = fastf2i(lfo_value) + state->delay;

    lfo_value = 2.0f - fabsf(2.0f - fmodf(state->lfo_coeff *
                                          (offset+state->lfo_disp)*4.0f,
                                          4.0f));
    lfo_value *= state->depth * state->delay;
    *delay_right = fastf2i(lfo_value) + state->delay;
}

static __inline void Sinusoid(ALint *delay_left, ALint *delay_right, ALint offset, const ALchorusState *state)
{
    ALfloat lfo_value;

    lfo_value = 1.0f + sinf(fmodf(state->lfo_coeff*offset, 2.0f*F_PI));
    lfo_value *= state->depth * state->delay;
    *delay_left = fastf2i(lfo_value) + state->delay;

    lfo_value = 1.0f + sinf(fmodf(state->lfo_coeff*(offset+state->lfo_disp),
                                  2.0f*F_PI));
    lfo_value *= state->depth * state->delay;
    *delay_right = fastf2i(lfo_value) + state->delay;
}

#define DECL_TEMPLATE(func)                                                    \
static void Process##func(ALchorusState *state, ALuint SamplesToDo,            \
                          const ALfloat *RESTRICT SamplesIn,                   \
                          ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE])          \
{                                                                              \
    const ALint mask = state->BufferLength-1;                                  \
    ALint offset = state->offset;                                              \
    ALuint it, kt;                                                             \
    ALuint base;                                                               \
                                                                               \
    for(base = 0;base < SamplesToDo;)                                          \
    {                                                                          \
        ALfloat temps[64][2];                                                  \
        ALuint td = minu(SamplesToDo-base, 64);                                \
                                                                               \
        for(it = 0;it < td;it++,offset++)                                      \
        {                                                                      \
            ALint delay_left, delay_right;                                     \
            (func)(&delay_left, &delay_right, offset, state);                  \
                                                                               \
            temps[it][0] = state->SampleBufferLeft[(offset-delay_left)&mask];  \
            state->SampleBufferLeft[offset&mask] = (temps[it][0] +             \
                                                    SamplesIn[it+base]) *      \
                                                   state->feedback;            \
                                                                               \
            temps[it][1] = state->SampleBufferRight[(offset-delay_right)&mask];\
            state->SampleBufferRight[offset&mask] = (temps[it][1] +            \
                                                     SamplesIn[it+base]) *     \
                                                    state->feedback;           \
        }                                                                      \
                                                                               \
        for(kt = 0;kt < MaxChannels;kt++)                                      \
        {                                                                      \
            ALfloat gain = state->Gain[0][kt];                                 \
            if(gain > 0.00001f)                                                \
            {                                                                  \
                for(it = 0;it < td;it++)                                       \
                    SamplesOut[kt][it+base] += temps[it][0] * gain;            \
            }                                                                  \
                                                                               \
            gain = state->Gain[1][kt];                                         \
            if(gain > 0.00001f)                                                \
            {                                                                  \
                for(it = 0;it < td;it++)                                       \
                    SamplesOut[kt][it+base] += temps[it][1] * gain;            \
            }                                                                  \
        }                                                                      \
                                                                               \
        base += td;                                                            \
    }                                                                          \
                                                                               \
    state->offset = offset;                                                    \
}

DECL_TEMPLATE(Triangle)
DECL_TEMPLATE(Sinusoid)

#undef DECL_TEMPLATE

static ALvoid ChorusProcess(ALeffectState *effect, ALuint SamplesToDo, const ALfloat *RESTRICT SamplesIn, ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE])
{
    ALchorusState *state = GET_PARENT_TYPE(ALchorusState, ALeffectState, effect);

    if(state->waveform == AL_CHORUS_WAVEFORM_TRIANGLE)
        ProcessTriangle(state, SamplesToDo, SamplesIn, SamplesOut);
    else if(state->waveform == AL_CHORUS_WAVEFORM_SINUSOID)
        ProcessSinusoid(state, SamplesToDo, SamplesIn, SamplesOut);
}

ALeffectState *ChorusCreate(void)
{
    ALchorusState *state;

    state = malloc(sizeof(*state));
    if(!state)
        return NULL;

    GET_DERIVED_TYPE(ALeffectState, state)->Destroy = ChorusDestroy;
    GET_DERIVED_TYPE(ALeffectState, state)->DeviceUpdate = ChorusDeviceUpdate;
    GET_DERIVED_TYPE(ALeffectState, state)->Update = ChorusUpdate;
    GET_DERIVED_TYPE(ALeffectState, state)->Process = ChorusProcess;

    state->BufferLength = 0;
    state->SampleBufferLeft = NULL;
    state->SampleBufferRight = NULL;
    state->offset = 0;

    return GET_DERIVED_TYPE(ALeffectState, state);
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
