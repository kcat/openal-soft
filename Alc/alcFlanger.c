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


typedef struct ALflangerStateFactory {
    DERIVE_FROM_TYPE(ALeffectStateFactory);
} ALflangerStateFactory;

static ALflangerStateFactory FlangerFactory;


typedef struct ALflangerState {
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
} ALflangerState;

static ALvoid ALflangerState_Destruct(ALflangerState *state)
{
    free(state->SampleBufferLeft);
    state->SampleBufferLeft = NULL;

    free(state->SampleBufferRight);
    state->SampleBufferRight = NULL;
}

static ALboolean ALflangerState_DeviceUpdate(ALflangerState *state, ALCdevice *Device)
{
    ALuint maxlen;
    ALuint it;

    maxlen = fastf2u(AL_FLANGER_MAX_DELAY * 3.0f * Device->Frequency) + 1;
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

static ALvoid ALflangerState_Update(ALflangerState *state, ALCdevice *Device, const ALeffectslot *Slot)
{
    ALfloat frequency = (ALfloat)Device->Frequency;
    ALfloat rate;
    ALint phase;
    ALuint it;

    for(it = 0;it < MaxChannels;it++)
    {
        state->Gain[0][it] = 0.0f;
        state->Gain[1][it] = 0.0f;
    }

    state->waveform = Slot->effect.Flanger.Waveform;
    state->depth = Slot->effect.Flanger.Depth;
    state->feedback = Slot->effect.Flanger.Feedback;
    state->delay = fastf2i(Slot->effect.Flanger.Delay * frequency);

    /* Gains for left and right sides */
    ComputeAngleGains(Device, atan2f(-1.0f, 0.0f), 0.0f, Slot->Gain, state->Gain[0]);
    ComputeAngleGains(Device, atan2f(+1.0f, 0.0f), 0.0f, Slot->Gain, state->Gain[1]);

    phase = Slot->effect.Flanger.Phase;
    rate = Slot->effect.Flanger.Rate;

    /* Calculate LFO coefficient */
    switch(state->waveform)
    {
        case AL_FLANGER_WAVEFORM_TRIANGLE:
             if(rate == 0.0f)
                 state->lfo_coeff = 0.0f;
             else
                 state->lfo_coeff = 1.0f / (frequency / rate);
             break;
        case AL_FLANGER_WAVEFORM_SINUSOID:
             if(rate == 0.0f)
                 state->lfo_coeff = 0.0f;
             else
                 state->lfo_coeff = F_PI * 2.0f / (frequency / rate);
             break;
    }

    /* Calculate lfo phase displacement */
    if(phase == 0 || rate == 0.0f)
        state->lfo_disp = 0;
    else
        state->lfo_disp = fastf2i(frequency / rate / (360.0f/phase));
}

static __inline void Triangle(ALint *delay_left, ALint *delay_right, ALint offset, const ALflangerState *state)
{
    ALfloat lfo_value;

    lfo_value = 2.0f - fabsf(2.0f - fmodf(state->lfo_coeff * offset * 4.0f, 4.0f));
    lfo_value *= state->depth * state->delay;
    *delay_left = fastf2i(lfo_value) + state->delay;

    lfo_value = 2.0f - fabsf(2.0f - fmodf(state->lfo_coeff *
                                          (offset+state->lfo_disp) * 4.0f,
                                          4.0f));
    lfo_value *= state->depth * state->delay;
    *delay_right = fastf2i(lfo_value) + state->delay;
}

static __inline void Sinusoid(ALint *delay_left, ALint *delay_right, ALint offset, const ALflangerState *state)
{
    ALfloat lfo_value;

    lfo_value = 1.0f + sinf(fmodf(state->lfo_coeff * offset, 2.0f*F_PI));
    lfo_value *= state->depth * state->delay;
    *delay_left = fastf2i(lfo_value) + state->delay;

    lfo_value = 1.0f + sinf(fmodf(state->lfo_coeff * (offset+state->lfo_disp),
                                  2.0f*F_PI));
    lfo_value *= state->depth * state->delay;
    *delay_right = fastf2i(lfo_value) + state->delay;
}

#define DECL_TEMPLATE(func)                                                    \
static void Process##func(ALflangerState *state, ALuint SamplesToDo,           \
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

static ALvoid ALflangerState_Process(ALflangerState *state, ALuint SamplesToDo, const ALfloat *RESTRICT SamplesIn, ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE])
{
    if(state->waveform == AL_FLANGER_WAVEFORM_TRIANGLE)
        ProcessTriangle(state, SamplesToDo, SamplesIn, SamplesOut);
    else if(state->waveform == AL_FLANGER_WAVEFORM_SINUSOID)
        ProcessSinusoid(state, SamplesToDo, SamplesIn, SamplesOut);
}

static ALeffectStateFactory *ALflangerState_getCreator(void)
{
    return STATIC_CAST(ALeffectStateFactory, &FlangerFactory);
}

DEFINE_ALEFFECTSTATE_VTABLE(ALflangerState);


ALeffectState *ALflangerStateFactory_create(void)
{
    ALflangerState *state;

    state = malloc(sizeof(*state));
    if(!state) return NULL;
    SET_VTABLE2(ALflangerState, ALeffectState, state);

    state->BufferLength = 0;
    state->SampleBufferLeft = NULL;
    state->SampleBufferRight = NULL;
    state->offset = 0;

    return STATIC_CAST(ALeffectState, state);
}

static ALvoid ALflangerStateFactory_destroy(ALeffectState *effect)
{
    ALflangerState *state = STATIC_UPCAST(ALflangerState, ALeffectState, effect);
    ALflangerState_Destruct(state);
    free(state);
}

DEFINE_ALEFFECTSTATEFACTORY_VTABLE(ALflangerStateFactory);


static void init_flanger_factory(void)
{
    SET_VTABLE2(ALflangerStateFactory, ALeffectStateFactory, &FlangerFactory);
}

ALeffectStateFactory *ALflangerStateFactory_getFactory(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, init_flanger_factory);
    return STATIC_CAST(ALeffectStateFactory, &FlangerFactory);
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
