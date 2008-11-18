/**
 * OpenAL cross platform audio library
 * Copyright (C) 2008 by Christopher Fitzgerald.
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
#include "AL/alc.h"
#include "alMain.h"
#include "alAuxEffectSlot.h"
#include "alEffect.h"
#include "alReverb.h"

#ifdef HAVE_SQRTF
#define aluSqrt(x) ((ALfloat)sqrtf((float)(x)))
#else
#define aluSqrt(x) ((ALfloat)sqrt((double)(x)))
#endif

// fixes for mingw32.
#if defined(max) && !defined(__max)
#define __max max
#endif
#if defined(min) && !defined(__min)
#define __min min
#endif

typedef struct DelayLine
{
    // The delay lines use lengths that are powers of 2 to allow bitmasking
    // instead of modulus wrapping.
    ALuint   Mask;
    ALfloat *Line;
} DelayLine;

struct ALverbState
{
    // All delay lines are allocated as a single buffer to reduce memory
    // fragmentation and teardown code.
    ALfloat  *SampleBuffer;
    // Master reverb gain.
    ALfloat   Gain;
    // Initial reverb delay.
    DelayLine Delay;
    // The tap points for the initial delay.  First tap goes to early
    // reflections, the second to late reverb.
    ALuint    Tap[2];
    struct {
        // Gain for early reflections.
        ALfloat   Gain;
        // Early reflections are done with 4 delay lines.
        ALfloat   Coeff[4];
        DelayLine Delay[4];
        ALuint    Offset[4];
    } Early;
    struct {
        // Gain for late reverb.
        ALfloat   Gain;
        // Diffusion of late reverb.
        ALfloat   Diffusion;
        // Late reverb is done with 8 delay lines.
        ALfloat   Coeff[8];
        DelayLine Delay[8];
        ALuint    Offset[8];
        // The input and last 4 delay lines are low-pass filtered.
        ALfloat   LpCoeff[5];
        ALfloat   LpSample[5];
    } Late;
    ALuint Offset;
};

// All delay line lengths are specified in seconds.

// The length of the initial delay line (a sum of the maximum delay before
// early reflections and late reverb; 0.3 + 0.1).
static const ALfloat MASTER_LINE_LENGTH = 0.4000f;

// The lengths of the early delay lines.
static const ALfloat EARLY_LINE_LENGTH[4] =
{
    0.0015f, 0.0045f, 0.0135f, 0.0405f
};

// The lengths of the late delay lines.
static const ALfloat LATE_LINE_LENGTH[8] =
{
    0.0015f, 0.0037f, 0.0093f, 0.0234f,
    0.0100f, 0.0150f, 0.0225f, 0.0337f
};

// The last 4 late delay lines have a variable length dependent on the effect
// density parameter and this multiplier.
static const ALfloat LATE_LINE_MULTIPLIER = 9.0f;

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

// Basic delay line input/output routines.
static __inline ALfloat DelayLineOut(DelayLine *Delay, ALuint offset)
{
    return Delay->Line[offset&Delay->Mask];
}

static __inline ALvoid DelayLineIn(DelayLine *Delay, ALuint offset, ALfloat in)
{
    Delay->Line[offset&Delay->Mask] = in;
}

// Delay line output routine for early reflections.
static __inline ALfloat EarlyDelayLineOut(ALverbState *State, ALuint index)
{
    return State->Early.Coeff[index] *
           DelayLineOut(&State->Early.Delay[index],
                        State->Offset - State->Early.Offset[index]);
}

// Given an input sample, this function produces a decorrelated stereo output
// for early reflections.
static __inline ALvoid EarlyReflection(ALverbState *State, ALfloat in, ALfloat *out)
{
    ALfloat d[4], v, f[4];

    // Obtain the decayed results of each early delay line.
    d[0] = EarlyDelayLineOut(State, 0);
    d[1] = EarlyDelayLineOut(State, 1);
    d[2] = EarlyDelayLineOut(State, 2);
    d[3] = EarlyDelayLineOut(State, 3);

    /* The following uses a lossless scattering junction from waveguide
     * theory.  It actually amounts to a householder mixing matrix, which
     * will produce a maximally diffuse response, and means this can probably
     * be considered a simple FDN.
     *          N
     *         ---
     *         \
     * v = 2/N /   di
     *         ---
     *         i=1
     */
    v = (d[0] + d[1] + d[2] + d[3]) * 0.5f;
    // The junction is loaded with the input here.
    v += in;

    // Calculate the feed values for the delay lines.
    f[0] = v - d[0];
    f[1] = v - d[1];
    f[2] = v - d[2];
    f[3] = v - d[3];

    // To increase reflection complexity (and help reduce coloration) the
    // delay lines cyclicly refeed themselves (0 -> 1 -> 3 -> 2 -> 0...).
    DelayLineIn(&State->Early.Delay[0], State->Offset, f[2]);
    DelayLineIn(&State->Early.Delay[1], State->Offset, f[0]);
    DelayLineIn(&State->Early.Delay[2], State->Offset, f[3]);
    DelayLineIn(&State->Early.Delay[3], State->Offset, f[1]);

    // To decorrelate the output for stereo separation, the cyclical nature
    // of the feed path is exploited.  The two outputs are obtained from the
    // inner delay lines.
    // Output is instant by using the inputs to them instead of taking the
    // result of the two delay lines directly (f[0] and f[3] instead of d[1]
    // and d[2]).
    out[0] = State->Early.Gain * f[0];
    out[1] = State->Early.Gain * f[3];
}

// Delay line output routine for late reverb.
static __inline ALfloat LateDelayLineOut(ALverbState *State, ALuint index)
{
    return State->Late.Coeff[index] *
           DelayLineOut(&State->Late.Delay[index],
                        State->Offset - State->Late.Offset[index]);
}

// Low-pass filter input/output routine for late reverb.
static __inline ALfloat LateLowPassInOut(ALverbState *State, ALuint index, ALfloat in)
{
    State->Late.LpSample[index] = in + ((State->Late.LpSample[index] - in) *
                                        State->Late.LpCoeff[index]);
    return State->Late.LpSample[index];
}

// Given an input sample, this function produces a decorrelated stereo output
// for late reverb.
static __inline ALvoid LateReverb(ALverbState *State, ALfloat in, ALfloat *out)
{
    ALfloat din, d[8], v, dv, f[8];

    // Since the input will be sent directly to the output as in the early
    // reflections function, it needs to take into account some immediate
    // absorption.
    in = LateLowPassInOut(State, 0, in);

    // When diffusion is full, no input is directly passed to the variable-
    // length delay lines (the last 4).
    din = (1.0f - State->Late.Diffusion) * in;

    // Obtain the decayed results of the fixed-length delay lines.
    d[0] = LateDelayLineOut(State, 0);
    d[1] = LateDelayLineOut(State, 1);
    d[2] = LateDelayLineOut(State, 2);
    d[3] = LateDelayLineOut(State, 3);
    // Obtain the decayed and low-pass filtered results of the variable-
    // length delay lines.
    d[4] = LateLowPassInOut(State, 1, LateDelayLineOut(State, 4));
    d[5] = LateLowPassInOut(State, 2, LateDelayLineOut(State, 5));
    d[6] = LateLowPassInOut(State, 3, LateDelayLineOut(State, 6));
    d[7] = LateLowPassInOut(State, 4, LateDelayLineOut(State, 7));

    // The waveguide formula used in the early reflections function works
    // great for high diffusion, but it is not obviously paramerized to allow
    // a variable diffusion.  With only limited time and resources, what
    // follows is the best variation of that formula I could come up with.
    // First, there are 8 delay lines used.  The first 4 are fixed-length and
    // generate the highest density of the diffuse response.  The last 4 are
    // variable-length, and are used to smooth out the diffuse response.  The
    // density effect parameter alters their length.  The inner two delay
    // lines of each group have their signs reversed (more about this later).
    v = (d[0] - d[1] - d[2] + d[3] +
         d[4] - d[5] - d[6] + d[7]) * 0.25f;
    // Diffusion is applied as a reduction of the junction pressure for all
    // branches.  This presents two problems.  When the diffusion factor (0
    // to 1) reaches 0.5, the average feed value is reduced (the junction
    // becomes lossy).  Thus, at 0.5 the signal decays almost twice as fast
    // as it should.  The second problem is the introduction of some
    // resonant frequencies (coloration).  The reversed signs above are used
    // to help combat some of the coloration by adding variations along the
    // feed cycle.
    v *= State->Late.Diffusion;
    // Load the junction with the input.  To reduce the noticeable echo of
    // the longer delay lines (the variable-length ones) the input is loaded
    // with the inverse of the effect diffusion.  So at full diffusion, the
    // input is not applied to the last 4 delay lines.  Input signs reversed
    // to balance the equation.
    dv = v + din;
    v += in;

    // As with the reversed signs above, to balance the equation the signs
    // need to be reversed here, too.
    f[0] = d[0] - v;
    f[1] = d[1] + v;
    f[2] = d[2] + v;
    f[3] = d[3] - v;
    f[4] = d[4] - dv;
    f[5] = d[5] + dv;
    f[6] = d[6] + dv;
    f[7] = d[7] - dv;

    // Feed the fixed-length delay lines with their own cycle (0 -> 1 -> 3 ->
    // 2 -> 0...).
    DelayLineIn(&State->Late.Delay[0], State->Offset, f[2]);
    DelayLineIn(&State->Late.Delay[1], State->Offset, f[0]);
    DelayLineIn(&State->Late.Delay[2], State->Offset, f[3]);
    DelayLineIn(&State->Late.Delay[3], State->Offset, f[1]);
    // Feed the variable-length delay lines with their cycle (4 -> 6 -> 7 ->
    // 5 -> 4...).
    DelayLineIn(&State->Late.Delay[4], State->Offset, f[5]);
    DelayLineIn(&State->Late.Delay[5], State->Offset, f[7]);
    DelayLineIn(&State->Late.Delay[6], State->Offset, f[4]);
    DelayLineIn(&State->Late.Delay[7], State->Offset, f[6]);

    // Output is derived from the values fed to the inner two variable-length
    // delay lines (5 and 6).
    out[0] = State->Late.Gain * f[7];
    out[1] = State->Late.Gain * f[4];
}

// This creates the reverb state.  It should be called only when the reverb
// effect is loaded into a slot that doesn't already have a reverb effect.
ALverbState *VerbCreate(ALCcontext *Context)
{
    ALverbState *State = NULL;
    ALuint length[13], totalLength, index;

    State = malloc(sizeof(ALverbState));
    if(!State)
        return NULL;

    // All line lengths are powers of 2, calculated from the line timings and
    // the addition of an extra sample (for safety).
    length[0] = NextPowerOf2((ALuint)(MASTER_LINE_LENGTH*Context->Frequency) + 1);
    totalLength = length[0];
    for(index = 0;index < 4;index++)
    {
        length[1+index] = NextPowerOf2((ALuint)(EARLY_LINE_LENGTH[index]*Context->Frequency) + 1);
        totalLength += length[1+index];
    }
    for(index = 0;index < 4;index++)
    {
        length[5+index] = NextPowerOf2((ALuint)(LATE_LINE_LENGTH[index]*Context->Frequency) + 1);
        totalLength += length[5+index];
    }
    for(index = 4;index < 8;index++)
    {
        length[5+index] = NextPowerOf2((ALuint)(LATE_LINE_LENGTH[index]*(1.0f + LATE_LINE_MULTIPLIER)*Context->Frequency) + 1);
        totalLength += length[5+index];
    }

    // They all share a single sample buffer.
    State->SampleBuffer = malloc(totalLength * sizeof(ALfloat));
    if(!State->SampleBuffer)
    {
        free(State);
        return NULL;
    }
    for(index = 0; index < totalLength;index++)
        State->SampleBuffer[index] = 0.0f;

    // Each one has its mask and start address calculated one time.
    State->Gain = 0.0f;
    State->Delay.Mask = length[0] - 1;
    State->Delay.Line = &State->SampleBuffer[0];
    totalLength = length[0];

    State->Tap[0] = 0;
    State->Tap[1] = 0;

    State->Early.Gain = 0.0f;
    // All fixed-length delay lines have their read-write offsets calculated
    // one time.
    for(index = 0;index < 4;index++)
    {
        State->Early.Coeff[index] = 0.0f;
        State->Early.Delay[index].Mask = length[1 + index] - 1;
        State->Early.Delay[index].Line = &State->SampleBuffer[totalLength];
        totalLength += length[1 + index];

        State->Early.Offset[index] = (ALuint)(EARLY_LINE_LENGTH[index] * Context->Frequency);
    }

    State->Late.Gain = 0.0f;
    State->Late.Diffusion = 0.0f;
    for(index = 0;index < 8;index++)
    {
        State->Late.Coeff[index] = 0.0f;
        State->Late.Delay[index].Mask = length[5 + index] - 1;
        State->Late.Delay[index].Line = &State->SampleBuffer[totalLength];
        totalLength += length[5 + index];

        State->Late.Offset[index] = 0;
        if(index < 4)
        {
            State->Late.Offset[index] = (ALuint)(LATE_LINE_LENGTH[index] * Context->Frequency);
            State->Late.LpCoeff[index] = 0.0f;
            State->Late.LpSample[index] = 0.0f;
        }
        else if(index == 4)
        {
            State->Late.LpCoeff[index] = 0.0f;
            State->Late.LpSample[index] = 0.0f;
        }
    }

    State->Offset = 0;
    return State;
}

// This destroys the reverb state.  It should be called only when the effect
// slot has a different (or no) effect loaded over the reverb effect.
ALvoid VerbDestroy(ALverbState *State)
{
    if(State)
    {
        free(State->SampleBuffer);
        State->SampleBuffer = NULL;
        free(State);
    }
}

// This updates the reverb state.  This is called any time the reverb effect
// is loaded into a slot.
ALvoid VerbUpdate(ALCcontext *Context, ALeffectslot *Slot, ALeffect *Effect)
{
    ALverbState *State = Slot->ReverbState;
    ALuint index, index2;
    ALfloat length, lpcoeff, cw, g;
    ALfloat hfRatio = Effect->Reverb.DecayHFRatio;

    // Calculate the master gain (from the slot and master reverb gain).
    State->Gain = Slot->Gain * Effect->Reverb.Gain;

    // Calculate the initial delay taps.
    length = Effect->Reverb.ReflectionsDelay;
    State->Tap[0] = (ALuint)(length * Context->Frequency);
    length += Effect->Reverb.LateReverbDelay;
    State->Tap[1] = (ALuint)(length * Context->Frequency);

    // Calculate the early reflections gain.  Right now this uses a gain of
    // 0.75 to compensate for the increase in density.  It should probably
    // use a power (RMS) based measurement from the resulting distribution of
    // early delay lines.
    State->Early.Gain = Effect->Reverb.ReflectionsGain * 0.75f;

    // Calculate the gain (coefficient) for each early delay line.
    for(index = 0;index < 4;index++)
        State->Early.Coeff[index] = pow(10.0f, EARLY_LINE_LENGTH[index] /
                                               Effect->Reverb.LateReverbDelay *
                                               -60.0f / 20.0f);

    // Calculate the late reverb gain, adjusted by density, diffusion, and
    // decay time.  To be accurate, the adjustments should probably use power
    // measurements for each contribution, but they are not too bad as they
    // are.
    State->Late.Gain = Effect->Reverb.LateReverbGain *
                       (0.45f + (0.55f * Effect->Reverb.Density)) *
                       (1.0f - (0.25f * Effect->Reverb.Diffusion)) *
                       (1.0f - (0.025f * Effect->Reverb.DecayTime));
    State->Late.Diffusion = Effect->Reverb.Diffusion;

    // The EFX specification does not make it clear whether the air
    // absorption parameter should always take effect.  Both Generic Software
    // and Generic Hardware only apply it when HF limit is flagged, so that's
    // what is done here.
    // If the HF limit parameter is flagged, calculate an appropriate limit
    // based on the air absorption parameter.
    if(Effect->Reverb.DecayHFLimit && Effect->Reverb.AirAbsorptionGainHF < 1.0f)
    {
        ALfloat limitRatio;

        // The following is my best guess at how to limit the HF ratio by the
        // air absorption parameter.
        // For each of the last 4 delays, find the attenuation due to air
        // absorption in dB (converting delay time to meters using the speed
        // of sound).  Then reversing the decay equation, solve for HF ratio.
        // The delay length is cancelled out of the equation, so it can be
        // calculated once for all lines.
        limitRatio = 1.0f / (log10(Effect->Reverb.AirAbsorptionGainHF) *
                             SPEEDOFSOUNDMETRESPERSEC *
                             Effect->Reverb.DecayTime / -60.0f * 20.0f);
        // Need to limit the result to a minimum of 0.1, just like the HF
        // ratio parameter.
        limitRatio = __max(limitRatio, 0.1f);

        // Using the limit calculated above, apply the upper bound to the
        // HF ratio.
        hfRatio = __min(hfRatio, limitRatio);
    }

    cw = cos(2.0f*3.141592654f * LOWPASSFREQCUTOFF / Context->Frequency);

    for(index = 0;index < 8;index++)
    {
        // Calculate the length (in seconds) of each delay line.
        length = LATE_LINE_LENGTH[index];
        if(index >= 4)
        {
            index2 = index - 3;

            length *= 1.0f + (Effect->Reverb.Density * LATE_LINE_MULTIPLIER);

            // Calculate the delay offset for the variable-length delay
            // lines.
            State->Late.Offset[index] = (ALuint)(length * Context->Frequency);
        }
        // Calculate the gain (coefficient) for each line.
        State->Late.Coeff[index] = pow(10.0f, length / Effect->Reverb.DecayTime *
                                              -60.0f / 20.0f);
        if(index >= 4)
        {
            // Calculate the decay equation for each low-pass filter.
            g = pow(10.0f, length / (Effect->Reverb.DecayTime * hfRatio) *
                           -60.0f / 20.0f) /
                State->Late.Coeff[index];
            g  = __max(g, 0.1f);
            g *= g;
            // Calculate the gain (coefficient) for each low-pass filter.
            lpcoeff = 0.0f;
            if(g < 0.9999f) // 1-epsilon
                lpcoeff = (1 - g*cw - aluSqrt(2*g*(1-cw) - g*g*(1 - cw*cw))) / (1 - g);

            // Very low decay times will produce minimal output, so apply an
            // upper bound to the coefficient.
            State->Late.LpCoeff[index2] = __min(lpcoeff, 0.98f);
        }
    }

    // This just calculates the coefficient for the late reverb input low-
    // pass filter.  It is calculated based the average (hence -30 instead
    // of -60) length of the inner two variable-length delay lines.
    length = LATE_LINE_LENGTH[5] * (1.0f + Effect->Reverb.Density * LATE_LINE_MULTIPLIER) +
             LATE_LINE_LENGTH[6] * (1.0f + Effect->Reverb.Density * LATE_LINE_MULTIPLIER);

    g = pow(10.0f, length / (Effect->Reverb.DecayTime * hfRatio) * -30.0f / 20.0f) /
        pow(10.0f, length / Effect->Reverb.DecayTime * -30.0f / 20.0f);
    g  = __max(g, 0.1f);
    g *= g;

    lpcoeff = 0.0f;
    if(g < 0.9999f) // 1-epsilon
        lpcoeff = (1 - g*cw - aluSqrt(2*g*(1-cw) - g*g*(1 - cw*cw))) / (1 - g);

    State->Late.LpCoeff[0] = __min(lpcoeff, 0.98f);
}

// This processes the reverb state, given the input samples and an output
// buffer.
ALvoid VerbProcess(ALverbState *State, ALuint SamplesToDo, const ALfloat *SamplesIn, ALfloat (*SamplesOut)[OUTPUTCHANNELS])
{
    ALuint index;
    ALfloat in, early[2], late[2], out[2];

    for(index = 0;index < SamplesToDo;index++)
    {
        // Feed the initial delay line.
        DelayLineIn(&State->Delay, State->Offset, SamplesIn[index]);

        // Calculate the early reflection from the first delay tap.
        in = DelayLineOut(&State->Delay, State->Offset - State->Tap[0]);
        EarlyReflection(State, in, early);

        // Calculate the late reverb from the second delay tap.
        in = DelayLineOut(&State->Delay, State->Offset - State->Tap[1]);
        LateReverb(State, in, late);

        // Mix early reflections and late reverb.
        out[0] = State->Gain * (early[0] + late[0]);
        out[1] = State->Gain * (early[1] + late[1]);

        // Step all delays forward one sample.
        State->Offset++;

        // Output the results.
        SamplesOut[index][FRONT_LEFT]  += out[0];
        SamplesOut[index][FRONT_RIGHT] += out[1];
        SamplesOut[index][SIDE_LEFT]   += out[0];
        SamplesOut[index][SIDE_RIGHT]  += out[1];
        SamplesOut[index][BACK_LEFT]   += out[0];
        SamplesOut[index][BACK_RIGHT]  += out[1];
    }
}
