/**
 * Reverb for the OpenAL cross platform audio library
 * Copyright (C) 2008-2009 by Christopher Fitzgerald.
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
#include "alu.h"

typedef struct DelayLine
{
    // The delay lines use sample lengths that are powers of 2 to allow
    // bitmasking instead of modulus wrapping.
    ALuint   Mask;
    ALfloat *Line;
} DelayLine;

struct ALverbState
{
    // All delay lines are allocated as a single buffer to reduce memory
    // fragmentation and management code.
    ALfloat  *SampleBuffer;
    // Master effect gain.
    ALfloat   Gain;
    // Initial effect delay and decorrelation.
    DelayLine Delay;
    // The tap points for the initial delay.  First tap goes to early
    // reflections, the last four decorrelate to late reverb.
    ALuint    Tap[5];
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
        // Attenuation to compensate for modal density and decay rate.
        ALfloat   DensityGain;
        // The feed-back and feed-forward all-pass coefficient.
        ALfloat   ApFeedCoeff;
        // Mixing matrix coefficient.
        ALfloat   MixCoeff;
        // Late reverb has 4 parallel all-pass filters.
        ALfloat   ApCoeff[4];
        DelayLine ApDelay[4];
        ALuint    ApOffset[4];
        // In addition to 4 cyclical delay lines.
        ALfloat   Coeff[4];
        DelayLine Delay[4];
        ALuint    Offset[4];
        // The cyclical delay lines are low-pass filtered.
        ALfloat   LpCoeff[4][2];
        ALfloat   LpSample[4];
    } Late;
    // The current read offset for all delay lines.
    ALuint Offset;
};

// All delay line lengths are specified in seconds.

// The lengths of the early delay lines.
static const ALfloat EARLY_LINE_LENGTH[4] =
{
    0.0015f, 0.0045f, 0.0135f, 0.0405f
};

// The lengths of the late all-pass delay lines.
static const ALfloat ALLPASS_LINE_LENGTH[4] =
{
    0.0151f, 0.0167f, 0.0183f, 0.0200f,
};

// The lengths of the late cyclical delay lines.
static const ALfloat LATE_LINE_LENGTH[4] =
{
    0.0211f, 0.0311f, 0.0461f, 0.0680f
};

// The late cyclical delay lines have a variable length dependent on the
// effect's density parameter (inverted for some reason) and this multiplier.
static const ALfloat LATE_LINE_MULTIPLIER = 4.0f;

// Input into the late reverb is decorrelated between four channels.  Their
// timings are dependent on a fraction and multiplier.  See VerbUpdate() for
// the calculations involved.
static const ALfloat DECO_FRACTION = 1.0f / 32.0f;
static const ALfloat DECO_MULTIPLIER = 2.0f;

// The maximum length of initial delay for the master delay line (a sum of
// the maximum early reflection and late reverb delays).
static const ALfloat MASTER_LINE_LENGTH = 0.3f + 0.1f;

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

// Given an input sample, this function produces stereo output for early
// reflections.
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
     * be considered a simple feedback delay network (FDN).
     *          N
     *         ---
     *         \
     * v = 2/N /   d_i
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

    // Refeed the delay lines.
    DelayLineIn(&State->Early.Delay[0], State->Offset, f[0]);
    DelayLineIn(&State->Early.Delay[1], State->Offset, f[1]);
    DelayLineIn(&State->Early.Delay[2], State->Offset, f[2]);
    DelayLineIn(&State->Early.Delay[3], State->Offset, f[3]);

    // To decorrelate the output for stereo separation, the two outputs are
    // obtained from the inner delay lines.
    // Output is instant by using the inputs to them instead of taking the
    // result of the two delay lines directly (f[0] and f[3] instead of d[1]
    // and d[2]).
    out[0] = State->Early.Gain * f[0];
    out[1] = State->Early.Gain * f[3];
}

// All-pass input/output routine for late reverb.
static __inline ALfloat LateAllPassInOut(ALverbState *State, ALuint index, ALfloat in)
{
    ALfloat out;

    out = State->Late.ApCoeff[index] *
          DelayLineOut(&State->Late.ApDelay[index],
                       State->Offset - State->Late.ApOffset[index]);
    out -= (State->Late.ApFeedCoeff * in);
    DelayLineIn(&State->Late.ApDelay[index], State->Offset,
                (State->Late.ApFeedCoeff * out) + in);
    return out;
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
    State->Late.LpSample[index] = (State->Late.LpCoeff[index][0] * in) +
        (State->Late.LpCoeff[index][1] * State->Late.LpSample[index]);
    return State->Late.LpSample[index];
}

// Given four decorrelated input samples, this function produces stereo
// output for late reverb.
static __inline ALvoid LateReverb(ALverbState *State, ALfloat *in, ALfloat *out)
{
    ALfloat d[4], f[4];

    // Obtain the decayed results of the cyclical delay lines, and add the
    // corresponding input channels attenuated by density.  Then pass the
    // results through the low-pass filters.
    d[0] = LateLowPassInOut(State, 0, (State->Late.DensityGain * in[0]) +
                                      LateDelayLineOut(State, 0));
    d[1] = LateLowPassInOut(State, 1, (State->Late.DensityGain * in[1]) +
                                      LateDelayLineOut(State, 1));
    d[2] = LateLowPassInOut(State, 2, (State->Late.DensityGain * in[2]) +
                                      LateDelayLineOut(State, 2));
    d[3] = LateLowPassInOut(State, 3, (State->Late.DensityGain * in[3]) +
                                      LateDelayLineOut(State, 3));

    // To help increase diffusion, run each line through an all-pass filter.
    // The order of the all-pass filters is selected so that the shortest
    // all-pass filter will feed the shortest delay line.
    d[0] = LateAllPassInOut(State, 1, d[0]);
    d[1] = LateAllPassInOut(State, 3, d[1]);
    d[2] = LateAllPassInOut(State, 0, d[2]);
    d[3] = LateAllPassInOut(State, 2, d[3]);

    /* Late reverb is done with a modified feedback delay network (FDN)
     * topology.  Four input lines are each fed through their own all-pass
     * filter and then into the mixing matrix.  The four outputs of the
     * mixing matrix are then cycled back to the inputs.  Each output feeds
     * a different input to form a circlular feed cycle.
     *
     * The mixing matrix used is a 4D skew-symmetric rotation matrix derived
     * using a single unitary rotational parameter:
     *
     *  [  d,  a,  b,  c ]          1 = a^2 + b^2 + c^2 + d^2
     *  [ -a,  d,  c, -b ]
     *  [ -b, -c,  d,  a ]
     *  [ -c,  b, -a,  d ]
     *
     * The rotation is constructed from the effect's diffusion parameter,
     * yielding:  1 = x^2 + 3 y^2; where a, b, and c are the coefficient y
     * with differing signs, and d is the coefficient x.  The matrix is thus:
     *
     *  [  x,  y, -y,  y ]          x = 1 - (0.5 diffusion^3)
     *  [ -y,  x,  y,  y ]          y = sqrt((1 - x^2) / 3)
     *  [  y, -y,  x,  y ]
     *  [ -y, -y, -y,  x ]
     *
     * To reduce the number of multiplies, the x coefficient is applied with
     * the cyclical delay line coefficients.  Thus only the y coefficient is
     * applied when mixing, and is modified to be:  y / x.
     */
    f[0] = d[0] + (State->Late.MixCoeff * ( d[1] - d[2] + d[3]));
    f[1] = d[1] + (State->Late.MixCoeff * (-d[0] + d[2] + d[3]));
    f[2] = d[2] + (State->Late.MixCoeff * ( d[0] - d[1] + d[3]));
    f[3] = d[3] + (State->Late.MixCoeff * (-d[0] - d[1] - d[2]));

    // Output is tapped at the input to the shortest two cyclical delay
    // lines, attenuated by the late reverb gain (which is attenuated by the
    // mixing coefficient x).
    out[0] = State->Late.Gain * f[0];
    out[1] = State->Late.Gain * f[1];

    // The delay lines are fed circularly in the order:
    // 0 -> 1 -> 3 -> 2 -> 0 ...
    DelayLineIn(&State->Late.Delay[0], State->Offset, f[2]);
    DelayLineIn(&State->Late.Delay[1], State->Offset, f[0]);
    DelayLineIn(&State->Late.Delay[2], State->Offset, f[3]);
    DelayLineIn(&State->Late.Delay[3], State->Offset, f[1]);
}

// This creates the reverb state.  It should be called only when the reverb
// effect is loaded into a slot that doesn't already have a reverb effect.
ALverbState *VerbCreate(ALCcontext *Context)
{
    ALverbState *State = NULL;
    ALuint samples, length[13], totalLength, index;

    State = malloc(sizeof(ALverbState));
    if(!State)
        return NULL;

    // All line lengths are powers of 2, calculated from their lengths, with
    // an additional sample in case of rounding errors.

    // See VerbUpdate() for an explanation of the additional calculation
    // added to the master line length.
    samples = (ALuint)
              ((MASTER_LINE_LENGTH +
                (LATE_LINE_LENGTH[0] * (1.0f + LATE_LINE_MULTIPLIER) *
                 (DECO_FRACTION * ((DECO_MULTIPLIER * DECO_MULTIPLIER *
                                    DECO_MULTIPLIER) - 1.0f)))) *
               Context->Frequency) + 1;
    length[0] = NextPowerOf2(samples);
    totalLength = length[0];
    for(index = 0;index < 4;index++)
    {
        samples = (ALuint)(EARLY_LINE_LENGTH[index] * Context->Frequency) + 1;
        length[1 + index] = NextPowerOf2(samples);
        totalLength += length[1 + index];
    }
    for(index = 0;index < 4;index++)
    {
        samples = (ALuint)(ALLPASS_LINE_LENGTH[index] * Context->Frequency) + 1;
        length[5 + index] = NextPowerOf2(samples);
        totalLength += length[5 + index];
    }
    for(index = 0;index < 4;index++)
    {
        samples = (ALuint)(LATE_LINE_LENGTH[index] *
                           (1.0f + LATE_LINE_MULTIPLIER) * Context->Frequency) + 1;
        length[9 + index] = NextPowerOf2(samples);
        totalLength += length[9 + index];
    }

    // All lines share a single sample buffer.
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
    State->Tap[2] = 0;
    State->Tap[3] = 0;
    State->Tap[4] = 0;

    State->Early.Gain = 0.0f;
    for(index = 0;index < 4;index++)
    {
        State->Early.Coeff[index] = 0.0f;
        State->Early.Delay[index].Mask = length[1 + index] - 1;
        State->Early.Delay[index].Line = &State->SampleBuffer[totalLength];
        totalLength += length[1 + index];

        // The early delay lines have their read offsets calculated once.
        State->Early.Offset[index] = (ALuint)(EARLY_LINE_LENGTH[index] *
                                              Context->Frequency);
    }

    State->Late.Gain = 0.0f;
    State->Late.DensityGain = 0.0f;
    State->Late.ApFeedCoeff = 0.0f;
    State->Late.MixCoeff = 0.0f;

    for(index = 0;index < 4;index++)
    {
        State->Late.ApCoeff[index] = 0.0f;
        State->Late.ApDelay[index].Mask = length[5 + index] - 1;
        State->Late.ApDelay[index].Line = &State->SampleBuffer[totalLength];
        totalLength += length[5 + index];

        // The late all-pass lines have their read offsets calculated once.
        State->Late.ApOffset[index] = (ALuint)(ALLPASS_LINE_LENGTH[index] *
                                               Context->Frequency);
    }

    for(index = 0;index < 4;index++)
    {
        State->Late.Coeff[index] = 0.0f;
        State->Late.Delay[index].Mask = length[9 + index] - 1;
        State->Late.Delay[index].Line = &State->SampleBuffer[totalLength];
        totalLength += length[9 + index];

        State->Late.Offset[index] = 0;

        State->Late.LpCoeff[index][0] = 0.0f;
        State->Late.LpCoeff[index][1] = 0.0f;
        State->Late.LpSample[index] = 0.0f;
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
    ALuint index;
    ALfloat length, mixCoeff, cw, g, lpCoeff;
    ALfloat hfRatio = Effect->Reverb.DecayHFRatio;

    // Calculate the master gain (from the slot and master effect gain).
    State->Gain = Slot->Gain * Effect->Reverb.Gain;

    // Calculate the initial delay taps.
    length = Effect->Reverb.ReflectionsDelay;
    State->Tap[0] = (ALuint)(length * Context->Frequency);

    length += Effect->Reverb.LateReverbDelay;

    /* The four inputs to the late reverb are decorrelated to smooth the
     * initial reverb and reduce harsh echos.  The timings are calculated as
     * multiples of a fraction of the smallest cyclical delay time. This
     * result is then adjusted so that the first tap occurs immediately (all
     * taps are reduced by the shortest fraction).
     *
     * offset[index] = ((FRACTION MULTIPLIER^index) - 1) delay
     */
    for(index = 0;index < 4;index++)
    {
        length += LATE_LINE_LENGTH[0] *
            (1.0f + (Effect->Reverb.Density * LATE_LINE_MULTIPLIER)) *
            (DECO_FRACTION * (pow(DECO_MULTIPLIER, (ALfloat)index) - 1.0f));
        State->Tap[1 + index] = (ALuint)(length * Context->Frequency);
    }

    // Set the early reflections gain.
    State->Early.Gain = Effect->Reverb.ReflectionsGain;

    // Calculate the gain (coefficient) for each early delay line.
    for(index = 0;index < 4;index++)
        State->Early.Coeff[index] = pow(10.0f, EARLY_LINE_LENGTH[index] /
                                               Effect->Reverb.LateReverbDelay *
                                               -60.0f / 20.0f);

    // Calculate the first mixing matrix coefficient (x).
    mixCoeff = 1.0f - (0.5f * pow(Effect->Reverb.Diffusion, 3.0f));

    // Set the late reverb gain.  Since the output is tapped prior to the
    // application of the delay line coefficients, this gain needs to be
    // attenuated by the mix coefficient from above.
    State->Late.Gain = Effect->Reverb.LateReverbGain * mixCoeff;

    /* To compensate for changes in modal density and decay time of the late
     * reverb signal, the input is attenuated based on the maximal energy of
     * the outgoing signal.  This is calculated as the ratio between a
     * reference value and the current approximation of energy for the output
     * signal.
     *
     * Reverb output matches exponential decay of the form Sum(a^n), where a
     * is the attenuation coefficient, and n is the sample ranging from 0 to
     * infinity.  The signal energy can thus be approximated using the area
     * under this curve, calculated as:  1 / (1 - a).
     *
     * The reference energy is calculated from a signal at the lowest (effect
     * at 1.0) density with a decay time of one second.
     *
     * The coefficient is calculated as the average length of the cyclical
     * delay lines.  This produces a better result than calculating the gain
     * for each line individually (most likely a side effect of diffusion).
     *
     * The final result is the square root of the ratio bound to a maximum
     * value of 1 (no amplification) and attenuated by 1 / sqrt(2) to
     * compensate for the four decorrelated inputs.
     */
    length = (LATE_LINE_LENGTH[0] + LATE_LINE_LENGTH[1] +
              LATE_LINE_LENGTH[2] + LATE_LINE_LENGTH[3]);
    g = length * (1.0f + LATE_LINE_MULTIPLIER) * 0.25f;
    g = pow(10.0f, g * -60.0f / 20.0f);
    g = 1.0f / (1.0f - (g*g));
    length *= 1.0f + (Effect->Reverb.Density * LATE_LINE_MULTIPLIER) * 0.25f;
    length = pow(10.0f, length / Effect->Reverb.DecayTime * -60.0f / 20.0f);
    length = 1.0f / (1.0f - (length*length));
    State->Late.DensityGain = 0.707106f * __min(aluSqrt(g / length), 1.0f);

    // Calculate the all-pass feed-back and feed-forward coefficient.
    State->Late.ApFeedCoeff = 0.6f * pow(Effect->Reverb.Diffusion, 3.0f);

    // Calculate the mixing matrix coefficient (y / x).
    g = aluSqrt((1.0f - (mixCoeff * mixCoeff)) / 3.0f);
    State->Late.MixCoeff = g / mixCoeff;

    for(index = 0;index < 4;index++)
    {
        // Calculate the gain (coefficient) for each all-pass line.
        State->Late.ApCoeff[index] = pow(10.0f, ALLPASS_LINE_LENGTH[index] /
                                                Effect->Reverb.DecayTime *
                                                -60.0f / 20.0f);
    }

    // If the HF limit parameter is flagged, calculate an appropriate limit
    // based on the air absorption parameter.
    if(Effect->Reverb.DecayHFLimit && Effect->Reverb.AirAbsorptionGainHF < 1.0f)
    {
        ALfloat limitRatio;

        // For each of the cyclical delays, find the attenuation due to air
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

    // Calculate the filter frequency for low-pass or high-pass depending on
    // whether the HF ratio is above 1.
    cw = 2.0f * M_PI * LOWPASSFREQCUTOFF / Context->Frequency;
    if(hfRatio > 1.0f)
        cw = M_PI - cw;
    cw = cos(cw);

    for(index = 0;index < 4;index++)
    {
        // Calculate the length (in seconds) of each cyclical delay line.
        length = LATE_LINE_LENGTH[index] * (1.0f + (Effect->Reverb.Density *
                                                    LATE_LINE_MULTIPLIER));
        // Calculate the delay offset for the cyclical delay lines.
        State->Late.Offset[index] = (ALuint)(length * Context->Frequency);

        // Calculate the gain (coefficient) for each cyclical line.
        State->Late.Coeff[index] = pow(10.0f, length / Effect->Reverb.DecayTime *
                                              -60.0f / 20.0f);

        // Calculate the decay equation for each low-pass filter.
        g = pow(10.0f, length / (Effect->Reverb.DecayTime * hfRatio) *
                       -60.0f / 20.0f);
        if (hfRatio > 1.0f)
            g = State->Late.Coeff[index] / g;
        else
            g = g / State->Late.Coeff[index];
        g  = __max(g, 0.1f);
        g *= g;

        // Calculate the gain (coefficient) for each low-pass filter.
        lpCoeff = 0.0f;
        if(g < 0.9999f) // 1-epsilon
            lpCoeff = (1 - g*cw - aluSqrt(2*g*(1-cw) - g*g*(1 - cw*cw))) / (1 - g);

        // Very low decay times will produce minimal output, so apply an
        // upper bound to the coefficient.
        lpCoeff = __min(lpCoeff, 0.98f);

        // Calculate the filter coefficients for high-pass or low-pass
        // dependent on HF ratio being above 1.
        if(hfRatio > 1.0f) {
            State->Late.LpCoeff[index][0] = 1.0f + lpCoeff;
            State->Late.LpCoeff[index][1] = -lpCoeff;
        } else {
            State->Late.LpCoeff[index][0] = 1.0f - lpCoeff;
            State->Late.LpCoeff[index][1] = lpCoeff;
        }

        // Attenuate the cyclical line coefficients by the mixing coefficient
        // (x).
        State->Late.Coeff[index] *= mixCoeff;
    }
}

// This processes the reverb state, given the input samples and an output
// buffer.
ALvoid VerbProcess(ALverbState *State, ALuint SamplesToDo, const ALfloat *SamplesIn, ALfloat (*SamplesOut)[OUTPUTCHANNELS])
{
    ALuint index;
    ALfloat in[4], early[2], late[2], out[2];

    for(index = 0;index < SamplesToDo;index++)
    {
        // Feed the initial delay line.
        DelayLineIn(&State->Delay, State->Offset, SamplesIn[index]);

        // Calculate the early reflection from the first delay tap.
        in[0] = DelayLineOut(&State->Delay, State->Offset - State->Tap[0]);
        EarlyReflection(State, in[0], early);

        // Calculate the late reverb from the last four delay taps.
        in[0] = DelayLineOut(&State->Delay, State->Offset - State->Tap[1]);
        in[1] = DelayLineOut(&State->Delay, State->Offset - State->Tap[2]);
        in[2] = DelayLineOut(&State->Delay, State->Offset - State->Tap[3]);
        in[3] = DelayLineOut(&State->Delay, State->Offset - State->Tap[4]);
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
