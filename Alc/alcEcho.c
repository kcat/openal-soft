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

#include "AL/al.h"
#include "alFilter.h"
#include "alAuxEffectSlot.h"
#include "alEcho.h"

#ifdef HAVE_SQRTF
#define aluSqrt(x) ((ALfloat)sqrtf((float)(x)))
#else
#define aluSqrt(x) ((ALfloat)sqrt((double)(x)))
#endif

struct ALechoState {
    ALfloat *SampleBuffer;
    ALuint BufferLength;

    // The echo is two tap. The third tap is the offset to write the feedback
    // and input sample to
    struct {
        ALuint offset;
    } Tap[3];
    // The LR gains for the first tap. The second tap uses the reverse
    ALfloat GainL;
    ALfloat GainR;

    ALfloat FeedGain;

    FILTER iirFilter;
    ALfloat history[2];
};

// Find the next power of 2.  Actually, this will return the input value if
// it is already a power of 2.
static ALuint NextPowerOf2(ALuint value)
{
    ALuint powerOf2 = 1;

    if(value)
    {
        value--;
        while(value)
        {
            value >>= 1;
            powerOf2 <<= 1;
        }
    }
    return powerOf2;
}

ALechoState *EchoCreate(ALCcontext *Context)
{
    ALechoState *state;
    ALuint i, maxlen;

    state = malloc(sizeof(*state));
    if(!state)
        return NULL;

    maxlen  = (ALuint)(AL_ECHO_MAX_DELAY * Context->Frequency);
    maxlen += (ALuint)(AL_ECHO_MAX_LRDELAY * Context->Frequency);

    // Use the next power of 2 for the buffer length, so the tap offsets can be
    // wrapped using a mask instead of a modulo
    state->BufferLength = NextPowerOf2(maxlen+1);
    state->SampleBuffer = malloc(state->BufferLength * sizeof(ALfloat));
    if(!state->SampleBuffer)
    {
        free(state);
        return NULL;
    }

    for(i = 0;i < state->BufferLength;i++)
        state->SampleBuffer[i] = 0.0f;

    state->Tap[0].offset = 0;
    state->Tap[1].offset = 0;
    state->Tap[2].offset = 0;
    state->GainL = 0.0f;
    state->GainR = 0.0f;

    for(i = 0;i < sizeof(state->history)/sizeof(state->history[0]);i++)
        state->history[i] = 0.0f;
    state->iirFilter.coeff = 0.0f;

    return state;
}

ALvoid EchoDestroy(ALechoState *state)
{
    if(state)
    {
        free(state->SampleBuffer);
        state->SampleBuffer = NULL;
        free(state);
    }
}

ALvoid EchoUpdate(ALCcontext *Context, struct ALeffectslot *Slot, ALeffect *Effect)
{
    ALechoState *state = Slot->EchoState;
    ALuint newdelay1, newdelay2;
    ALfloat lrpan, cw, a, g;

    newdelay1 = (ALuint)(Effect->Echo.Delay * Context->Frequency);
    newdelay2 = (ALuint)(Effect->Echo.LRDelay * Context->Frequency);

    state->Tap[0].offset = (state->BufferLength - newdelay1 - 1 +
                            state->Tap[2].offset)%state->BufferLength;
    state->Tap[1].offset = (state->BufferLength - newdelay1 - newdelay2 - 1 +
                            state->Tap[2].offset)%state->BufferLength;

    lrpan = Effect->Echo.Spread*0.5f + 0.5f;
    state->GainL = aluSqrt(     lrpan);
    state->GainR = aluSqrt(1.0f-lrpan);

    state->FeedGain = Effect->Echo.Feedback;

    cw = cos(2.0*M_PI * LOWPASSFREQCUTOFF / Context->Frequency);
    g = 1.0f - Effect->Echo.Damping;
    a = 0.0f;
    if(g < 0.9999f) // 1-epsilon
        a = (1 - g*cw - aluSqrt(2*g*(1-cw) - g*g*(1 - cw*cw))) / (1 - g);
    state->iirFilter.coeff = a;
}

ALvoid EchoProcess(ALechoState *state, ALuint SamplesToDo, const ALfloat *SamplesIn, ALfloat (*SamplesOut)[OUTPUTCHANNELS])
{
    ALfloat *history = state->iirFilter.history;
    const ALfloat a = state->iirFilter.coeff;
    const ALuint delay = state->BufferLength-1;
    ALuint tap1off = state->Tap[0].offset;
    ALuint tap2off = state->Tap[1].offset;
    ALuint fboff = state->Tap[2].offset;
    ALfloat samp[2];
    ALuint i;

    for(i = 0;i < SamplesToDo;i++)
    {
        // Apply damping
        samp[0] = state->SampleBuffer[tap2off] + SamplesIn[i];

        samp[0] += (history[0]-samp[0]) * a;
        history[0] = samp[0];
        samp[0] += (history[1]-samp[0]) * a;
        history[1] = samp[0];

        // Apply feedback gain and mix in the new sample
        state->SampleBuffer[fboff] = samp[0] * state->FeedGain;

        tap1off = (tap1off+1) & delay;
        tap2off = (tap2off+1) & delay;
        fboff = (fboff+1) & delay;

        // Sample first tap
        samp[0] = state->SampleBuffer[tap1off]*state->GainL;
        samp[1] = state->SampleBuffer[tap1off]*state->GainR;
        // Sample second tap. Reverse LR panning
        samp[0] += state->SampleBuffer[tap2off]*state->GainR;
        samp[1] += state->SampleBuffer[tap2off]*state->GainL;

        SamplesOut[i][FRONT_LEFT]  += samp[0];
        SamplesOut[i][FRONT_RIGHT] += samp[1];
        SamplesOut[i][SIDE_LEFT]   += samp[0];
        SamplesOut[i][SIDE_RIGHT]  += samp[1];
        SamplesOut[i][BACK_LEFT]   += samp[0];
        SamplesOut[i][BACK_RIGHT]  += samp[1];
    }

    state->Tap[0].offset = tap1off;
    state->Tap[1].offset = tap2off;
    state->Tap[2].offset = fboff;
}
