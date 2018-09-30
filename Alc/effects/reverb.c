/**
 * Ambisonic reverb engine for the OpenAL cross platform audio library
 * Copyright (C) 2008-2017 by Chris Robinson and Christopher Fitzgerald.
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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "alMain.h"
#include "alu.h"
#include "alAuxEffectSlot.h"
#include "alListener.h"
#include "alError.h"
#include "filters/defs.h"

/* This is a user config option for modifying the overall output of the reverb
 * effect.
 */
ALfloat ReverbBoost = 1.0f;

/* This is the maximum number of samples processed for each inner loop
 * iteration. */
#define MAX_UPDATE_SAMPLES  256

/* The number of samples used for cross-faded delay lines.  This can be used
 * to balance the compensation for abrupt line changes and attenuation due to
 * minimally lengthed recursive lines.  Try to keep this below the device
 * update size.
 */
#define FADE_SAMPLES  128

/* The number of spatialized lines or channels to process. Four channels allows
 * for a 3D A-Format response. NOTE: This can't be changed without taking care
 * of the conversion matrices, and a few places where the length arrays are
 * assumed to have 4 elements.
 */
#define NUM_LINES 4


/* The B-Format to A-Format conversion matrix. The arrangement of rows is
 * deliberately chosen to align the resulting lines to their spatial opposites
 * (0:above front left <-> 3:above back right, 1:below front right <-> 2:below
 * back left). It's not quite opposite, since the A-Format results in a
 * tetrahedron, but it's close enough. Should the model be extended to 8-lines
 * in the future, true opposites can be used.
 */
static const aluMatrixf B2A = {{
    { 0.288675134595f,  0.288675134595f,  0.288675134595f,  0.288675134595f },
    { 0.288675134595f, -0.288675134595f, -0.288675134595f,  0.288675134595f },
    { 0.288675134595f,  0.288675134595f, -0.288675134595f, -0.288675134595f },
    { 0.288675134595f, -0.288675134595f,  0.288675134595f, -0.288675134595f }
}};

/* Converts A-Format to B-Format. */
static const aluMatrixf A2B = {{
    { 0.866025403785f,  0.866025403785f,  0.866025403785f,  0.866025403785f },
    { 0.866025403785f, -0.866025403785f,  0.866025403785f, -0.866025403785f },
    { 0.866025403785f, -0.866025403785f, -0.866025403785f,  0.866025403785f },
    { 0.866025403785f,  0.866025403785f, -0.866025403785f, -0.866025403785f }
}};

static const ALfloat FadeStep = 1.0f / FADE_SAMPLES;

/* The all-pass and delay lines have a variable length dependent on the
 * effect's density parameter, which helps alter the perceived environment
 * size. The size-to-density conversion is a cubed scale:
 *
 * density = min(1.0, pow(size, 3.0) / DENSITY_SCALE);
 *
 * The line lengths scale linearly with room size, so the inverse density
 * conversion is needed, taking the cube root of the re-scaled density to
 * calculate the line length multiplier:
 *
 *     length_mult = max(5.0, cbrtf(density*DENSITY_SCALE));
 *
 * The density scale below will result in a max line multiplier of 50, for an
 * effective size range of 5m to 50m.
 */
static const ALfloat DENSITY_SCALE = 125000.0f;

/* All delay line lengths are specified in seconds.
 *
 * To approximate early reflections, we break them up into primary (those
 * arriving from the same direction as the source) and secondary (those
 * arriving from the opposite direction).
 *
 * The early taps decorrelate the 4-channel signal to approximate an average
 * room response for the primary reflections after the initial early delay.
 *
 * Given an average room dimension (d_a) and the speed of sound (c) we can
 * calculate the average reflection delay (r_a) regardless of listener and
 * source positions as:
 *
 *     r_a = d_a / c
 *     c   = 343.3
 *
 * This can extended to finding the average difference (r_d) between the
 * maximum (r_1) and minimum (r_0) reflection delays:
 *
 *     r_0 = 2 / 3 r_a
 *         = r_a - r_d / 2
 *         = r_d
 *     r_1 = 4 / 3 r_a
 *         = r_a + r_d / 2
 *         = 2 r_d
 *     r_d = 2 / 3 r_a
 *         = r_1 - r_0
 *
 * As can be determined by integrating the 1D model with a source (s) and
 * listener (l) positioned across the dimension of length (d_a):
 *
 *     r_d = int_(l=0)^d_a (int_(s=0)^d_a |2 d_a - 2 (l + s)| ds) dl / c
 *
 * The initial taps (T_(i=0)^N) are then specified by taking a power series
 * that ranges between r_0 and half of r_1 less r_0:
 *
 *     R_i = 2^(i / (2 N - 1)) r_d
 *         = r_0 + (2^(i / (2 N - 1)) - 1) r_d
 *         = r_0 + T_i
 *     T_i = R_i - r_0
 *         = (2^(i / (2 N - 1)) - 1) r_d
 *
 * Assuming an average of 1m, we get the following taps:
 */
static const ALfloat EARLY_TAP_LENGTHS[NUM_LINES] =
{
    0.0000000e+0f, 2.0213520e-4f, 4.2531060e-4f, 6.7171600e-4f
};

/* The early all-pass filter lengths are based on the early tap lengths:
 *
 *     A_i = R_i / a
 *
 * Where a is the approximate maximum all-pass cycle limit (20).
 */
static const ALfloat EARLY_ALLPASS_LENGTHS[NUM_LINES] =
{
    9.7096800e-5f, 1.0720356e-4f, 1.1836234e-4f, 1.3068260e-4f
};

/* The early delay lines are used to transform the primary reflections into
 * the secondary reflections.  The A-format is arranged in such a way that
 * the channels/lines are spatially opposite:
 *
 *     C_i is opposite C_(N-i-1)
 *
 * The delays of the two opposing reflections (R_i and O_i) from a source
 * anywhere along a particular dimension always sum to twice its full delay:
 *
 *     2 r_a = R_i + O_i
 *
 * With that in mind we can determine the delay between the two reflections
 * and thus specify our early line lengths (L_(i=0)^N) using:
 *
 *     O_i = 2 r_a - R_(N-i-1)
 *     L_i = O_i - R_(N-i-1)
 *         = 2 (r_a - R_(N-i-1))
 *         = 2 (r_a - T_(N-i-1) - r_0)
 *         = 2 r_a (1 - (2 / 3) 2^((N - i - 1) / (2 N - 1)))
 *
 * Using an average dimension of 1m, we get:
 */
static const ALfloat EARLY_LINE_LENGTHS[NUM_LINES] =
{
    5.9850400e-4f, 1.0913150e-3f, 1.5376658e-3f, 1.9419362e-3f
};

/* The late all-pass filter lengths are based on the late line lengths:
 *
 *     A_i = (5 / 3) L_i / r_1
 */
static const ALfloat LATE_ALLPASS_LENGTHS[NUM_LINES] =
{
    1.6182800e-4f, 2.0389060e-4f, 2.8159360e-4f, 3.2365600e-4f
};

/* The late lines are used to approximate the decaying cycle of recursive
 * late reflections.
 *
 * Splitting the lines in half, we start with the shortest reflection paths
 * (L_(i=0)^(N/2)):
 *
 *     L_i = 2^(i / (N - 1)) r_d
 *
 * Then for the opposite (longest) reflection paths (L_(i=N/2)^N):
 *
 *     L_i = 2 r_a - L_(i-N/2)
 *         = 2 r_a - 2^((i - N / 2) / (N - 1)) r_d
 *
 * For our 1m average room, we get:
 */
static const ALfloat LATE_LINE_LENGTHS[NUM_LINES] =
{
    1.9419362e-3f, 2.4466860e-3f, 3.3791220e-3f, 3.8838720e-3f
};


typedef struct DelayLineI {
    /* The delay lines use interleaved samples, with the lengths being powers
     * of 2 to allow the use of bit-masking instead of a modulus for wrapping.
     */
    ALsizei  Mask;
    ALfloat (*Line)[NUM_LINES];
} DelayLineI;

typedef struct VecAllpass {
    DelayLineI Delay;
    ALfloat Coeff;
    ALsizei Offset[NUM_LINES][2];
} VecAllpass;

typedef struct T60Filter {
    /* Two filters are used to adjust the signal. One to control the low
     * frequencies, and one to control the high frequencies.
     */
    ALfloat MidGain[2];
    BiquadFilter HFFilter, LFFilter;
} T60Filter;

typedef struct EarlyReflections {
    /* A Gerzon vector all-pass filter is used to simulate initial diffusion.
     * The spread from this filter also helps smooth out the reverb tail.
     */
    VecAllpass VecAp;

    /* An echo line is used to complete the second half of the early
     * reflections.
     */
    DelayLineI Delay;
    ALsizei    Offset[NUM_LINES][2];
    ALfloat    Coeff[NUM_LINES][2];

    /* The gain for each output channel based on 3D panning. */
    ALfloat CurrentGain[NUM_LINES][MAX_OUTPUT_CHANNELS];
    ALfloat PanGain[NUM_LINES][MAX_OUTPUT_CHANNELS];
} EarlyReflections;

typedef struct LateReverb {
    /* A recursive delay line is used fill in the reverb tail. */
    DelayLineI Delay;
    ALsizei    Offset[NUM_LINES][2];

    /* Attenuation to compensate for the modal density and decay rate of the
     * late lines.
     */
    ALfloat DensityGain[2];

    /* T60 decay filters are used to simulate absorption. */
    T60Filter T60[NUM_LINES];

    /* A Gerzon vector all-pass filter is used to simulate diffusion. */
    VecAllpass VecAp;

    /* The gain for each output channel based on 3D panning. */
    ALfloat CurrentGain[NUM_LINES][MAX_OUTPUT_CHANNELS];
    ALfloat PanGain[NUM_LINES][MAX_OUTPUT_CHANNELS];
} LateReverb;

typedef struct ReverbState {
    DERIVE_FROM_TYPE(ALeffectState);

    /* All delay lines are allocated as a single buffer to reduce memory
     * fragmentation and management code.
     */
    ALfloat *SampleBuffer;
    ALuint   TotalSamples;

    struct {
        /* Calculated parameters which indicate if cross-fading is needed after
         * an update.
         */
        ALfloat Density, Diffusion;
        ALfloat DecayTime, HFDecayTime, LFDecayTime;
        ALfloat HFReference, LFReference;
    } Params;

    /* Master effect filters */
    struct {
        BiquadFilter Lp;
        BiquadFilter Hp;
    } Filter[NUM_LINES];

    /* Core delay line (early reflections and late reverb tap from this). */
    DelayLineI Delay;

    /* Tap points for early reflection delay. */
    ALsizei EarlyDelayTap[NUM_LINES][2];
    ALfloat EarlyDelayCoeff[NUM_LINES][2];

    /* Tap points for late reverb feed and delay. */
    ALsizei LateFeedTap;
    ALsizei LateDelayTap[NUM_LINES][2];

    /* Coefficients for the all-pass and line scattering matrices. */
    ALfloat MixX;
    ALfloat MixY;

    EarlyReflections Early;

    LateReverb Late;

    /* Indicates the cross-fade point for delay line reads [0,FADE_SAMPLES]. */
    ALsizei FadeCount;

    /* Maximum number of samples to process at once. */
    ALsizei MaxUpdate[2];

    /* The current write offset for all delay lines. */
    ALsizei Offset;

    /* Temporary storage used when processing. */
    alignas(16) ALfloat TempSamples[NUM_LINES][MAX_UPDATE_SAMPLES];
    alignas(16) ALfloat MixSamples[NUM_LINES][MAX_UPDATE_SAMPLES];
} ReverbState;

static ALvoid ReverbState_Destruct(ReverbState *State);
static ALboolean ReverbState_deviceUpdate(ReverbState *State, ALCdevice *Device);
static ALvoid ReverbState_update(ReverbState *State, const ALCcontext *Context, const ALeffectslot *Slot, const ALeffectProps *props);
static ALvoid ReverbState_process(ReverbState *State, ALsizei SamplesToDo, const ALfloat (*restrict SamplesIn)[BUFFERSIZE], ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALsizei NumChannels);
DECLARE_DEFAULT_ALLOCATORS(ReverbState)

DEFINE_ALEFFECTSTATE_VTABLE(ReverbState);

static void ReverbState_Construct(ReverbState *state)
{
    ALsizei i, j;

    ALeffectState_Construct(STATIC_CAST(ALeffectState, state));
    SET_VTABLE2(ReverbState, ALeffectState, state);

    state->TotalSamples = 0;
    state->SampleBuffer = NULL;

    state->Params.Density = AL_EAXREVERB_DEFAULT_DENSITY;
    state->Params.Diffusion = AL_EAXREVERB_DEFAULT_DIFFUSION;
    state->Params.DecayTime = AL_EAXREVERB_DEFAULT_DECAY_TIME;
    state->Params.HFDecayTime = AL_EAXREVERB_DEFAULT_DECAY_TIME*AL_EAXREVERB_DEFAULT_DECAY_HFRATIO;
    state->Params.LFDecayTime = AL_EAXREVERB_DEFAULT_DECAY_TIME*AL_EAXREVERB_DEFAULT_DECAY_LFRATIO;
    state->Params.HFReference = AL_EAXREVERB_DEFAULT_HFREFERENCE;
    state->Params.LFReference = AL_EAXREVERB_DEFAULT_LFREFERENCE;

    for(i = 0;i < NUM_LINES;i++)
    {
        BiquadFilter_clear(&state->Filter[i].Lp);
        BiquadFilter_clear(&state->Filter[i].Hp);
    }

    state->Delay.Mask = 0;
    state->Delay.Line = NULL;

    for(i = 0;i < NUM_LINES;i++)
    {
        state->EarlyDelayTap[i][0] = 0;
        state->EarlyDelayTap[i][1] = 0;
        state->EarlyDelayCoeff[i][0] = 0.0f;
        state->EarlyDelayCoeff[i][1] = 0.0f;
    }

    state->LateFeedTap = 0;

    for(i = 0;i < NUM_LINES;i++)
    {
        state->LateDelayTap[i][0] = 0;
        state->LateDelayTap[i][1] = 0;
    }

    state->MixX = 0.0f;
    state->MixY = 0.0f;

    state->Early.VecAp.Delay.Mask = 0;
    state->Early.VecAp.Delay.Line = NULL;
    state->Early.VecAp.Coeff = 0.0f;
    state->Early.Delay.Mask = 0;
    state->Early.Delay.Line = NULL;
    for(i = 0;i < NUM_LINES;i++)
    {
        state->Early.VecAp.Offset[i][0] = 0;
        state->Early.VecAp.Offset[i][1] = 0;
        state->Early.Offset[i][0] = 0;
        state->Early.Offset[i][1] = 0;
        state->Early.Coeff[i][0] = 0.0f;
        state->Early.Coeff[i][1] = 0.0f;
    }

    state->Late.DensityGain[0] = 0.0f;
    state->Late.DensityGain[1] = 0.0f;
    state->Late.Delay.Mask = 0;
    state->Late.Delay.Line = NULL;
    state->Late.VecAp.Delay.Mask = 0;
    state->Late.VecAp.Delay.Line = NULL;
    state->Late.VecAp.Coeff = 0.0f;
    for(i = 0;i < NUM_LINES;i++)
    {
        state->Late.Offset[i][0] = 0;
        state->Late.Offset[i][1] = 0;

        state->Late.VecAp.Offset[i][0] = 0;
        state->Late.VecAp.Offset[i][1] = 0;

        state->Late.T60[i].MidGain[0] = 0.0f;
        state->Late.T60[i].MidGain[1] = 0.0f;
        BiquadFilter_clear(&state->Late.T60[i].HFFilter);
        BiquadFilter_clear(&state->Late.T60[i].LFFilter);
    }

    for(i = 0;i < NUM_LINES;i++)
    {
        for(j = 0;j < MAX_OUTPUT_CHANNELS;j++)
        {
            state->Early.CurrentGain[i][j] = 0.0f;
            state->Early.PanGain[i][j] = 0.0f;
            state->Late.CurrentGain[i][j] = 0.0f;
            state->Late.PanGain[i][j] = 0.0f;
        }
    }

    state->FadeCount = 0;
    state->MaxUpdate[0] = MAX_UPDATE_SAMPLES;
    state->MaxUpdate[1] = MAX_UPDATE_SAMPLES;
    state->Offset = 0;
}

static ALvoid ReverbState_Destruct(ReverbState *State)
{
    al_free(State->SampleBuffer);
    State->SampleBuffer = NULL;

    ALeffectState_Destruct(STATIC_CAST(ALeffectState,State));
}

/**************************************
 *  Device Update                     *
 **************************************/

static inline ALfloat CalcDelayLengthMult(ALfloat density)
{
    return maxf(5.0f, cbrtf(density*DENSITY_SCALE));
}

/* Given the allocated sample buffer, this function updates each delay line
 * offset.
 */
static inline ALvoid RealizeLineOffset(ALfloat *sampleBuffer, DelayLineI *Delay)
{
    union {
        ALfloat *f;
        ALfloat (*f4)[NUM_LINES];
    } u;
    u.f = &sampleBuffer[(ptrdiff_t)Delay->Line * NUM_LINES];
    Delay->Line = u.f4;
}

/* Calculate the length of a delay line and store its mask and offset. */
static ALuint CalcLineLength(const ALfloat length, const ptrdiff_t offset, const ALuint frequency,
                             const ALuint extra, DelayLineI *Delay)
{
    ALuint samples;

    /* All line lengths are powers of 2, calculated from their lengths in
     * seconds, rounded up.
     */
    samples = float2int(ceilf(length*frequency));
    samples = NextPowerOf2(samples + extra);

    /* All lines share a single sample buffer. */
    Delay->Mask = samples - 1;
    Delay->Line = (ALfloat(*)[NUM_LINES])offset;

    /* Return the sample count for accumulation. */
    return samples;
}

/* Calculates the delay line metrics and allocates the shared sample buffer
 * for all lines given the sample rate (frequency).  If an allocation failure
 * occurs, it returns AL_FALSE.
 */
static ALboolean AllocLines(const ALuint frequency, ReverbState *State)
{
    ALuint totalSamples, i;
    ALfloat multiplier, length;

    /* All delay line lengths are calculated to accomodate the full range of
     * lengths given their respective paramters.
     */
    totalSamples = 0;

    /* Multiplier for the maximum density value, i.e. density=1, which is
     * actually the least density...
     */
    multiplier = CalcDelayLengthMult(AL_EAXREVERB_MAX_DENSITY);

    /* The main delay length includes the maximum early reflection delay, the
     * largest early tap width, the maximum late reverb delay, and the
     * largest late tap width.  Finally, it must also be extended by the
     * update size (MAX_UPDATE_SAMPLES) for block processing.
     */
    length = AL_EAXREVERB_MAX_REFLECTIONS_DELAY + EARLY_TAP_LENGTHS[NUM_LINES-1]*multiplier +
             AL_EAXREVERB_MAX_LATE_REVERB_DELAY +
             (LATE_LINE_LENGTHS[NUM_LINES-1] - LATE_LINE_LENGTHS[0])*0.25f*multiplier;
    totalSamples += CalcLineLength(length, totalSamples, frequency, MAX_UPDATE_SAMPLES,
                                   &State->Delay);

    /* The early vector all-pass line. */
    length = EARLY_ALLPASS_LENGTHS[NUM_LINES-1] * multiplier;
    totalSamples += CalcLineLength(length, totalSamples, frequency, 0,
                                   &State->Early.VecAp.Delay);

    /* The early reflection line. */
    length = EARLY_LINE_LENGTHS[NUM_LINES-1] * multiplier;
    totalSamples += CalcLineLength(length, totalSamples, frequency, 0,
                                   &State->Early.Delay);

    /* The late vector all-pass line. */
    length = LATE_ALLPASS_LENGTHS[NUM_LINES-1] * multiplier;
    totalSamples += CalcLineLength(length, totalSamples, frequency, 0,
                                   &State->Late.VecAp.Delay);

    /* The late delay lines are calculated from the largest maximum density
     * line length.
     */
    length = LATE_LINE_LENGTHS[NUM_LINES-1] * multiplier;
    totalSamples += CalcLineLength(length, totalSamples, frequency, 0,
                                   &State->Late.Delay);

    if(totalSamples != State->TotalSamples)
    {
        ALfloat *newBuffer;

        TRACE("New reverb buffer length: %ux4 samples\n", totalSamples);
        newBuffer = al_calloc(16, sizeof(ALfloat[NUM_LINES]) * totalSamples);
        if(!newBuffer) return AL_FALSE;

        al_free(State->SampleBuffer);
        State->SampleBuffer = newBuffer;
        State->TotalSamples = totalSamples;
    }

    /* Update all delays to reflect the new sample buffer. */
    RealizeLineOffset(State->SampleBuffer, &State->Delay);
    RealizeLineOffset(State->SampleBuffer, &State->Early.VecAp.Delay);
    RealizeLineOffset(State->SampleBuffer, &State->Early.Delay);
    RealizeLineOffset(State->SampleBuffer, &State->Late.VecAp.Delay);
    RealizeLineOffset(State->SampleBuffer, &State->Late.Delay);

    /* Clear the sample buffer. */
    for(i = 0;i < State->TotalSamples;i++)
        State->SampleBuffer[i] = 0.0f;

    return AL_TRUE;
}

static ALboolean ReverbState_deviceUpdate(ReverbState *State, ALCdevice *Device)
{
    ALuint frequency = Device->Frequency;
    ALfloat multiplier;
    ALsizei i, j;

    /* Allocate the delay lines. */
    if(!AllocLines(frequency, State))
        return AL_FALSE;

    multiplier = CalcDelayLengthMult(AL_EAXREVERB_MAX_DENSITY);

    /* The late feed taps are set a fixed position past the latest delay tap. */
    State->LateFeedTap = float2int((AL_EAXREVERB_MAX_REFLECTIONS_DELAY +
                                    EARLY_TAP_LENGTHS[NUM_LINES-1]*multiplier) *
                                   frequency);

    /* Clear filters and gain coefficients since the delay lines were all just
     * cleared (if not reallocated).
     */
    for(i = 0;i < NUM_LINES;i++)
    {
        BiquadFilter_clear(&State->Filter[i].Lp);
        BiquadFilter_clear(&State->Filter[i].Hp);
    }

    for(i = 0;i < NUM_LINES;i++)
    {
        State->EarlyDelayCoeff[i][0] = 0.0f;
        State->EarlyDelayCoeff[i][1] = 0.0f;
    }

    for(i = 0;i < NUM_LINES;i++)
    {
        State->Early.Coeff[i][0] = 0.0f;
        State->Early.Coeff[i][1] = 0.0f;
    }

    State->Late.DensityGain[0] = 0.0f;
    State->Late.DensityGain[1] = 0.0f;
    for(i = 0;i < NUM_LINES;i++)
    {
        State->Late.T60[i].MidGain[0] = 0.0f;
        State->Late.T60[i].MidGain[1] = 0.0f;
        BiquadFilter_clear(&State->Late.T60[i].HFFilter);
        BiquadFilter_clear(&State->Late.T60[i].LFFilter);
    }

    for(i = 0;i < NUM_LINES;i++)
    {
        for(j = 0;j < MAX_OUTPUT_CHANNELS;j++)
        {
            State->Early.CurrentGain[i][j] = 0.0f;
            State->Early.PanGain[i][j] = 0.0f;
            State->Late.CurrentGain[i][j] = 0.0f;
            State->Late.PanGain[i][j] = 0.0f;
        }
    }

    /* Reset counters and offset base. */
    State->FadeCount = 0;
    State->MaxUpdate[0] = MAX_UPDATE_SAMPLES;
    State->MaxUpdate[1] = MAX_UPDATE_SAMPLES;
    State->Offset = 0;

    return AL_TRUE;
}

/**************************************
 *  Effect Update                     *
 **************************************/

/* Calculate a decay coefficient given the length of each cycle and the time
 * until the decay reaches -60 dB.
 */
static inline ALfloat CalcDecayCoeff(const ALfloat length, const ALfloat decayTime)
{
    return powf(REVERB_DECAY_GAIN, length/decayTime);
}

/* Calculate a decay length from a coefficient and the time until the decay
 * reaches -60 dB.
 */
static inline ALfloat CalcDecayLength(const ALfloat coeff, const ALfloat decayTime)
{
    return log10f(coeff) * decayTime / log10f(REVERB_DECAY_GAIN);
}

/* Calculate an attenuation to be applied to the input of any echo models to
 * compensate for modal density and decay time.
 */
static inline ALfloat CalcDensityGain(const ALfloat a)
{
    /* The energy of a signal can be obtained by finding the area under the
     * squared signal.  This takes the form of Sum(x_n^2), where x is the
     * amplitude for the sample n.
     *
     * Decaying feedback matches exponential decay of the form Sum(a^n),
     * where a is the attenuation coefficient, and n is the sample.  The area
     * under this decay curve can be calculated as:  1 / (1 - a).
     *
     * Modifying the above equation to find the area under the squared curve
     * (for energy) yields:  1 / (1 - a^2).  Input attenuation can then be
     * calculated by inverting the square root of this approximation,
     * yielding:  1 / sqrt(1 / (1 - a^2)), simplified to: sqrt(1 - a^2).
     */
    return sqrtf(1.0f - a*a);
}

/* Calculate the scattering matrix coefficients given a diffusion factor. */
static inline ALvoid CalcMatrixCoeffs(const ALfloat diffusion, ALfloat *x, ALfloat *y)
{
    ALfloat n, t;

    /* The matrix is of order 4, so n is sqrt(4 - 1). */
    n = sqrtf(3.0f);
    t = diffusion * atanf(n);

    /* Calculate the first mixing matrix coefficient. */
    *x = cosf(t);
    /* Calculate the second mixing matrix coefficient. */
    *y = sinf(t) / n;
}

/* Calculate the limited HF ratio for use with the late reverb low-pass
 * filters.
 */
static ALfloat CalcLimitedHfRatio(const ALfloat hfRatio, const ALfloat airAbsorptionGainHF,
                                  const ALfloat decayTime, const ALfloat SpeedOfSound)
{
    ALfloat limitRatio;

    /* Find the attenuation due to air absorption in dB (converting delay
     * time to meters using the speed of sound).  Then reversing the decay
     * equation, solve for HF ratio.  The delay length is cancelled out of
     * the equation, so it can be calculated once for all lines.
     */
    limitRatio = 1.0f / (CalcDecayLength(airAbsorptionGainHF, decayTime) * SpeedOfSound);

    /* Using the limit calculated above, apply the upper bound to the HF ratio.
     */
    return minf(limitRatio, hfRatio);
}


/* Calculates the 3-band T60 damping coefficients for a particular delay line
 * of specified length, using a combination of two shelf filter sections given
 * decay times for each band split at two reference frequencies.
 */
static void CalcT60DampingCoeffs(const ALfloat length, const ALfloat lfDecayTime,
                                 const ALfloat mfDecayTime, const ALfloat hfDecayTime,
                                 const ALfloat lf0norm, const ALfloat hf0norm,
                                 T60Filter *filter)
{
    ALfloat lfGain = CalcDecayCoeff(length, lfDecayTime);
    ALfloat mfGain = CalcDecayCoeff(length, mfDecayTime);
    ALfloat hfGain = CalcDecayCoeff(length, hfDecayTime);

    filter->MidGain[1] = mfGain;
    BiquadFilter_setParams(&filter->LFFilter, BiquadType_LowShelf, lfGain/mfGain, lf0norm,
                           calc_rcpQ_from_slope(lfGain/mfGain, 1.0f));
    BiquadFilter_setParams(&filter->HFFilter, BiquadType_HighShelf, hfGain/mfGain, hf0norm,
                           calc_rcpQ_from_slope(hfGain/mfGain, 1.0f));
}

/* Update the offsets for the main effect delay line. */
static ALvoid UpdateDelayLine(const ALfloat earlyDelay, const ALfloat lateDelay, const ALfloat density, const ALfloat decayTime, const ALuint frequency, ReverbState *State)
{
    ALfloat multiplier, length;
    ALuint i;

    multiplier = CalcDelayLengthMult(density);

    /* Early reflection taps are decorrelated by means of an average room
     * reflection approximation described above the definition of the taps.
     * This approximation is linear and so the above density multiplier can
     * be applied to adjust the width of the taps.  A single-band decay
     * coefficient is applied to simulate initial attenuation and absorption.
     *
     * Late reverb taps are based on the late line lengths to allow a zero-
     * delay path and offsets that would continue the propagation naturally
     * into the late lines.
     */
    for(i = 0;i < NUM_LINES;i++)
    {
        length = earlyDelay + EARLY_TAP_LENGTHS[i]*multiplier;
        State->EarlyDelayTap[i][1] = float2int(length * frequency);

        length = EARLY_TAP_LENGTHS[i]*multiplier;
        State->EarlyDelayCoeff[i][1] = CalcDecayCoeff(length, decayTime);

        length = lateDelay + (LATE_LINE_LENGTHS[i] - LATE_LINE_LENGTHS[0])*0.25f*multiplier;
        State->LateDelayTap[i][1] = State->LateFeedTap + float2int(length * frequency);
    }
}

/* Update the early reflection line lengths and gain coefficients. */
static ALvoid UpdateEarlyLines(const ALfloat density, const ALfloat diffusion, const ALfloat decayTime, const ALuint frequency, EarlyReflections *Early)
{
    ALfloat multiplier, length;
    ALsizei i;

    multiplier = CalcDelayLengthMult(density);

    /* Calculate the all-pass feed-back/forward coefficient. */
    Early->VecAp.Coeff = sqrtf(0.5f) * powf(diffusion, 2.0f);

    for(i = 0;i < NUM_LINES;i++)
    {
        /* Calculate the length (in seconds) of each all-pass line. */
        length = EARLY_ALLPASS_LENGTHS[i] * multiplier;

        /* Calculate the delay offset for each all-pass line. */
        Early->VecAp.Offset[i][1] = float2int(length * frequency);

        /* Calculate the length (in seconds) of each delay line. */
        length = EARLY_LINE_LENGTHS[i] * multiplier;

        /* Calculate the delay offset for each delay line. */
        Early->Offset[i][1] = float2int(length * frequency);

        /* Calculate the gain (coefficient) for each line. */
        Early->Coeff[i][1] = CalcDecayCoeff(length, decayTime);
    }
}

/* Update the late reverb line lengths and T60 coefficients. */
static ALvoid UpdateLateLines(const ALfloat density, const ALfloat diffusion, const ALfloat lfDecayTime, const ALfloat mfDecayTime, const ALfloat hfDecayTime, const ALfloat lf0norm, const ALfloat hf0norm, const ALuint frequency, LateReverb *Late)
{
    /* Scaling factor to convert the normalized reference frequencies from
     * representing 0...freq to 0...max_reference.
     */
    const ALfloat norm_weight_factor = (ALfloat)frequency / AL_EAXREVERB_MAX_HFREFERENCE;
    ALfloat multiplier, length, bandWeights[3];
    ALsizei i;

    /* To compensate for changes in modal density and decay time of the late
     * reverb signal, the input is attenuated based on the maximal energy of
     * the outgoing signal.  This approximation is used to keep the apparent
     * energy of the signal equal for all ranges of density and decay time.
     *
     * The average length of the delay lines is used to calculate the
     * attenuation coefficient.
     */
    multiplier = CalcDelayLengthMult(density);
    length = (LATE_LINE_LENGTHS[0] + LATE_LINE_LENGTHS[1] +
              LATE_LINE_LENGTHS[2] + LATE_LINE_LENGTHS[3]) / 4.0f * multiplier;
    length += (LATE_ALLPASS_LENGTHS[0] + LATE_ALLPASS_LENGTHS[1] +
               LATE_ALLPASS_LENGTHS[2] + LATE_ALLPASS_LENGTHS[3]) / 4.0f * multiplier;
    /* The density gain calculation uses an average decay time weighted by
     * approximate bandwidth. This attempts to compensate for losses of energy
     * that reduce decay time due to scattering into highly attenuated bands.
     */
    bandWeights[0] = lf0norm*norm_weight_factor;
    bandWeights[1] = hf0norm*norm_weight_factor - lf0norm*norm_weight_factor;
    bandWeights[2] = 1.0f - hf0norm*norm_weight_factor;
    Late->DensityGain[1] = CalcDensityGain(
        CalcDecayCoeff(length,
            bandWeights[0]*lfDecayTime + bandWeights[1]*mfDecayTime + bandWeights[2]*hfDecayTime
        )
    );

    /* Calculate the all-pass feed-back/forward coefficient. */
    Late->VecAp.Coeff = sqrtf(0.5f) * powf(diffusion, 2.0f);

    for(i = 0;i < NUM_LINES;i++)
    {
        /* Calculate the length (in seconds) of each all-pass line. */
        length = LATE_ALLPASS_LENGTHS[i] * multiplier;

        /* Calculate the delay offset for each all-pass line. */
        Late->VecAp.Offset[i][1] = float2int(length * frequency);

        /* Calculate the length (in seconds) of each delay line. */
        length = LATE_LINE_LENGTHS[i] * multiplier;

        /* Calculate the delay offset for each delay line. */
        Late->Offset[i][1] = float2int(length*frequency + 0.5f);

        /* Approximate the absorption that the vector all-pass would exhibit
         * given the current diffusion so we don't have to process a full T60
         * filter for each of its four lines.
         */
        length += lerp(LATE_ALLPASS_LENGTHS[i],
                       (LATE_ALLPASS_LENGTHS[0] + LATE_ALLPASS_LENGTHS[1] +
                        LATE_ALLPASS_LENGTHS[2] + LATE_ALLPASS_LENGTHS[3]) / 4.0f,
                       diffusion) * multiplier;

        /* Calculate the T60 damping coefficients for each line. */
        CalcT60DampingCoeffs(length, lfDecayTime, mfDecayTime, hfDecayTime,
                             lf0norm, hf0norm, &Late->T60[i]);
    }
}

/* Creates a transform matrix given a reverb vector. The vector pans the reverb
 * reflections toward the given direction, using its magnitude (up to 1) as a
 * focal strength. This function results in a B-Format transformation matrix
 * that spatially focuses the signal in the desired direction.
 */
static aluMatrixf GetTransformFromVector(const ALfloat *vec)
{
    aluMatrixf focus;
    ALfloat norm[3];
    ALfloat mag;

    /* Normalize the panning vector according to the N3D scale, which has an
     * extra sqrt(3) term on the directional components. Converting from OpenAL
     * to B-Format also requires negating X (ACN 1) and Z (ACN 3). Note however
     * that the reverb panning vectors use left-handed coordinates, unlike the
     * rest of OpenAL which use right-handed. This is fixed by negating Z,
     * which cancels out with the B-Format Z negation.
     */
    mag = sqrtf(vec[0]*vec[0] + vec[1]*vec[1] + vec[2]*vec[2]);
    if(mag > 1.0f)
    {
        norm[0] = vec[0] / mag * -SQRTF_3;
        norm[1] = vec[1] / mag * SQRTF_3;
        norm[2] = vec[2] / mag * SQRTF_3;
        mag = 1.0f;
    }
    else
    {
        /* If the magnitude is less than or equal to 1, just apply the sqrt(3)
         * term. There's no need to renormalize the magnitude since it would
         * just be reapplied in the matrix.
         */
        norm[0] = vec[0] * -SQRTF_3;
        norm[1] = vec[1] * SQRTF_3;
        norm[2] = vec[2] * SQRTF_3;
    }

    aluMatrixfSet(&focus,
        1.0f,   0.0f,    0.0f,   0.0f,
        norm[0], 1.0f-mag, 0.0f, 0.0f,
        norm[1], 0.0f, 1.0f-mag, 0.0f,
        norm[2], 0.0f, 0.0f, 1.0f-mag
    );

    return focus;
}

/* Update the early and late 3D panning gains. */
static ALvoid Update3DPanning(const ALCdevice *Device, const ALfloat *ReflectionsPan, const ALfloat *LateReverbPan, const ALfloat earlyGain, const ALfloat lateGain, ReverbState *State)
{
    aluMatrixf transform, rot;
    ALsizei i;

    STATIC_CAST(ALeffectState,State)->OutBuffer = Device->FOAOut.Buffer;
    STATIC_CAST(ALeffectState,State)->OutChannels = Device->FOAOut.NumChannels;

    /* Note: _res is transposed. */
#define MATRIX_MULT(_res, _m1, _m2) do {                                                   \
    int row, col;                                                                          \
    for(col = 0;col < 4;col++)                                                             \
    {                                                                                      \
        for(row = 0;row < 4;row++)                                                         \
            _res.m[col][row] = _m1.m[row][0]*_m2.m[0][col] + _m1.m[row][1]*_m2.m[1][col] + \
                               _m1.m[row][2]*_m2.m[2][col] + _m1.m[row][3]*_m2.m[3][col];  \
    }                                                                                      \
} while(0)
    /* Create a matrix that first converts A-Format to B-Format, then
     * transforms the B-Format signal according to the panning vector.
     */
    rot = GetTransformFromVector(ReflectionsPan);
    MATRIX_MULT(transform, rot, A2B);
    memset(&State->Early.PanGain, 0, sizeof(State->Early.PanGain));
    for(i = 0;i < MAX_EFFECT_CHANNELS;i++)
        ComputePanGains(&Device->FOAOut, transform.m[i], earlyGain,
                        State->Early.PanGain[i]);

    rot = GetTransformFromVector(LateReverbPan);
    MATRIX_MULT(transform, rot, A2B);
    memset(&State->Late.PanGain, 0, sizeof(State->Late.PanGain));
    for(i = 0;i < MAX_EFFECT_CHANNELS;i++)
        ComputePanGains(&Device->FOAOut, transform.m[i], lateGain,
                        State->Late.PanGain[i]);
#undef MATRIX_MULT
}

static void ReverbState_update(ReverbState *State, const ALCcontext *Context, const ALeffectslot *Slot, const ALeffectProps *props)
{
    const ALCdevice *Device = Context->Device;
    const ALlistener *Listener = Context->Listener;
    ALuint frequency = Device->Frequency;
    ALfloat lf0norm, hf0norm, hfRatio;
    ALfloat lfDecayTime, hfDecayTime;
    ALfloat gain, gainlf, gainhf;
    ALsizei i;

    /* Calculate the master filters */
    hf0norm = minf(props->Reverb.HFReference / frequency, 0.49f);
    /* Restrict the filter gains from going below -60dB to keep the filter from
     * killing most of the signal.
     */
    gainhf = maxf(props->Reverb.GainHF, 0.001f);
    BiquadFilter_setParams(&State->Filter[0].Lp, BiquadType_HighShelf, gainhf, hf0norm,
                           calc_rcpQ_from_slope(gainhf, 1.0f));
    lf0norm = minf(props->Reverb.LFReference / frequency, 0.49f);
    gainlf = maxf(props->Reverb.GainLF, 0.001f);
    BiquadFilter_setParams(&State->Filter[0].Hp, BiquadType_LowShelf, gainlf, lf0norm,
                           calc_rcpQ_from_slope(gainlf, 1.0f));
    for(i = 1;i < NUM_LINES;i++)
    {
        BiquadFilter_copyParams(&State->Filter[i].Lp, &State->Filter[0].Lp);
        BiquadFilter_copyParams(&State->Filter[i].Hp, &State->Filter[0].Hp);
    }

    /* Update the main effect delay and associated taps. */
    UpdateDelayLine(props->Reverb.ReflectionsDelay, props->Reverb.LateReverbDelay,
                    props->Reverb.Density, props->Reverb.DecayTime, frequency,
                    State);

    /* Update the early lines. */
    UpdateEarlyLines(props->Reverb.Density, props->Reverb.Diffusion,
                     props->Reverb.DecayTime, frequency, &State->Early);

    /* Get the mixing matrix coefficients. */
    CalcMatrixCoeffs(props->Reverb.Diffusion, &State->MixX, &State->MixY);

    /* If the HF limit parameter is flagged, calculate an appropriate limit
     * based on the air absorption parameter.
     */
    hfRatio = props->Reverb.DecayHFRatio;
    if(props->Reverb.DecayHFLimit && props->Reverb.AirAbsorptionGainHF < 1.0f)
        hfRatio = CalcLimitedHfRatio(hfRatio, props->Reverb.AirAbsorptionGainHF,
            props->Reverb.DecayTime, Listener->Params.ReverbSpeedOfSound
        );

    /* Calculate the LF/HF decay times. */
    lfDecayTime = clampf(props->Reverb.DecayTime * props->Reverb.DecayLFRatio,
                         AL_EAXREVERB_MIN_DECAY_TIME, AL_EAXREVERB_MAX_DECAY_TIME);
    hfDecayTime = clampf(props->Reverb.DecayTime * hfRatio,
                         AL_EAXREVERB_MIN_DECAY_TIME, AL_EAXREVERB_MAX_DECAY_TIME);

    /* Update the late lines. */
    UpdateLateLines(props->Reverb.Density, props->Reverb.Diffusion,
        lfDecayTime, props->Reverb.DecayTime, hfDecayTime, lf0norm, hf0norm,
        frequency, &State->Late
    );

    /* Update early and late 3D panning. */
    gain = props->Reverb.Gain * Slot->Params.Gain * ReverbBoost;
    Update3DPanning(Device, props->Reverb.ReflectionsPan, props->Reverb.LateReverbPan,
                    props->Reverb.ReflectionsGain*gain, props->Reverb.LateReverbGain*gain,
                    State);

    /* Calculate the max update size from the smallest relevant delay. */
    State->MaxUpdate[1] = mini(MAX_UPDATE_SAMPLES,
        mini(State->Early.Offset[0][1], State->Late.Offset[0][1])
    );

    /* Determine if delay-line cross-fading is required. Density is essentially
     * a master control for the feedback delays, so changes the offsets of many
     * delay lines.
     */
    if(State->Params.Density != props->Reverb.Density ||
        /* Diffusion and decay times influences the decay rate (gain) of the
         * late reverb T60 filter.
         */
       State->Params.Diffusion != props->Reverb.Diffusion ||
       State->Params.DecayTime != props->Reverb.DecayTime ||
       State->Params.HFDecayTime != hfDecayTime ||
       State->Params.LFDecayTime != lfDecayTime ||
       /* HF/LF References control the weighting used to calculate the density
        * gain.
        */
       State->Params.HFReference != props->Reverb.HFReference ||
       State->Params.LFReference != props->Reverb.LFReference)
        State->FadeCount = 0;
    State->Params.Density = props->Reverb.Density;
    State->Params.Diffusion = props->Reverb.Diffusion;
    State->Params.DecayTime = props->Reverb.DecayTime;
    State->Params.HFDecayTime = hfDecayTime;
    State->Params.LFDecayTime = lfDecayTime;
    State->Params.HFReference = props->Reverb.HFReference;
    State->Params.LFReference = props->Reverb.LFReference;
}


/**************************************
 *  Effect Processing                 *
 **************************************/

/* Basic delay line input/output routines. */
static inline ALfloat DelayLineOut(const DelayLineI *Delay, const ALsizei offset, const ALsizei c)
{
    return Delay->Line[offset&Delay->Mask][c];
}

/* Cross-faded delay line output routine.  Instead of interpolating the
 * offsets, this interpolates (cross-fades) the outputs at each offset.
 */
static inline ALfloat FadedDelayLineOut(const DelayLineI *Delay, const ALsizei off0,
                                        const ALsizei off1, const ALsizei c,
                                        const ALfloat sc0, const ALfloat sc1)
{
    return Delay->Line[off0&Delay->Mask][c]*sc0 +
           Delay->Line[off1&Delay->Mask][c]*sc1;
}


static inline void DelayLineIn(const DelayLineI *Delay, ALsizei offset, const ALsizei c,
                               const ALfloat *restrict in, ALsizei count)
{
    ALsizei i;
    for(i = 0;i < count;i++)
        Delay->Line[(offset++)&Delay->Mask][c] = *(in++);
}

/* Applies a scattering matrix to the 4-line (vector) input.  This is used
 * for both the below vector all-pass model and to perform modal feed-back
 * delay network (FDN) mixing.
 *
 * The matrix is derived from a skew-symmetric matrix to form a 4D rotation
 * matrix with a single unitary rotational parameter:
 *
 *     [  d,  a,  b,  c ]          1 = a^2 + b^2 + c^2 + d^2
 *     [ -a,  d,  c, -b ]
 *     [ -b, -c,  d,  a ]
 *     [ -c,  b, -a,  d ]
 *
 * The rotation is constructed from the effect's diffusion parameter,
 * yielding:
 *
 *     1 = x^2 + 3 y^2
 *
 * Where a, b, and c are the coefficient y with differing signs, and d is the
 * coefficient x.  The final matrix is thus:
 *
 *     [  x,  y, -y,  y ]          n = sqrt(matrix_order - 1)
 *     [ -y,  x,  y,  y ]          t = diffusion_parameter * atan(n)
 *     [  y, -y,  x,  y ]          x = cos(t)
 *     [ -y, -y, -y,  x ]          y = sin(t) / n
 *
 * Any square orthogonal matrix with an order that is a power of two will
 * work (where ^T is transpose, ^-1 is inverse):
 *
 *     M^T = M^-1
 *
 * Using that knowledge, finding an appropriate matrix can be accomplished
 * naively by searching all combinations of:
 *
 *     M = D + S - S^T
 *
 * Where D is a diagonal matrix (of x), and S is a triangular matrix (of y)
 * whose combination of signs are being iterated.
 */
static inline void VectorPartialScatter(ALfloat *restrict out, const ALfloat *restrict in,
                                        const ALfloat xCoeff, const ALfloat yCoeff)
{
    out[0] = xCoeff*in[0] + yCoeff*(          in[1] + -in[2] + in[3]);
    out[1] = xCoeff*in[1] + yCoeff*(-in[0]          +  in[2] + in[3]);
    out[2] = xCoeff*in[2] + yCoeff*( in[0] + -in[1]          + in[3]);
    out[3] = xCoeff*in[3] + yCoeff*(-in[0] + -in[1] + -in[2]        );
}
#define VectorScatterDelayIn(delay, o, in, xcoeff, ycoeff) \
    VectorPartialScatter((delay)->Line[(o)&(delay)->Mask], in, xcoeff, ycoeff)

/* Utilizes the above, but reverses the input channels. */
static inline void VectorScatterRevDelayIn(const DelayLineI *Delay, ALint offset,
                                           const ALfloat xCoeff, const ALfloat yCoeff,
                                           const ALfloat (*restrict in)[MAX_UPDATE_SAMPLES],
                                           const ALsizei count)
{
    const DelayLineI delay = *Delay;
    ALsizei i, j;

    for(i = 0;i < count;++i)
    {
        ALfloat f[NUM_LINES];
        for(j = 0;j < NUM_LINES;j++)
            f[NUM_LINES-1-j] = in[j][i];

        VectorScatterDelayIn(&delay, offset++, f, xCoeff, yCoeff);
    }
}

/* This applies a Gerzon multiple-in/multiple-out (MIMO) vector all-pass
 * filter to the 4-line input.
 *
 * It works by vectorizing a regular all-pass filter and replacing the delay
 * element with a scattering matrix (like the one above) and a diagonal
 * matrix of delay elements.
 *
 * Two static specializations are used for transitional (cross-faded) delay
 * line processing and non-transitional processing.
 */
static void VectorAllpass_Unfaded(ALfloat (*restrict samples)[MAX_UPDATE_SAMPLES], ALsizei offset,
                                  const ALfloat xCoeff, const ALfloat yCoeff, ALsizei todo,
                                  VecAllpass *Vap)
{
    const DelayLineI delay = Vap->Delay;
    const ALfloat feedCoeff = Vap->Coeff;
    ALsizei vap_offset[NUM_LINES];
    ALsizei i, j;

    ASSUME(todo > 0);

    for(j = 0;j < NUM_LINES;j++)
        vap_offset[j] = offset-Vap->Offset[j][0];
    for(i = 0;i < todo;i++)
    {
        ALfloat f[NUM_LINES];

        for(j = 0;j < NUM_LINES;j++)
        {
            ALfloat input = samples[j][i];
            ALfloat out = DelayLineOut(&delay, vap_offset[j]++, j) - feedCoeff*input;
            f[j] = input + feedCoeff*out;

            samples[j][i] = out;
        }

        VectorScatterDelayIn(&delay, offset, f, xCoeff, yCoeff);
        ++offset;
    }
}
static void VectorAllpass_Faded(ALfloat (*restrict samples)[MAX_UPDATE_SAMPLES], ALsizei offset,
                                const ALfloat xCoeff, const ALfloat yCoeff, ALfloat fade,
                                ALsizei todo, VecAllpass *Vap)
{
    const DelayLineI delay = Vap->Delay;
    const ALfloat feedCoeff = Vap->Coeff;
    ALsizei vap_offset[NUM_LINES][2];
    ALsizei i, j;

    ASSUME(todo > 0);

    fade *= 1.0f/FADE_SAMPLES;
    for(j = 0;j < NUM_LINES;j++)
    {
        vap_offset[j][0] = offset-Vap->Offset[j][0];
        vap_offset[j][1] = offset-Vap->Offset[j][1];
    }
    for(i = 0;i < todo;i++)
    {
        ALfloat f[NUM_LINES];

        for(j = 0;j < NUM_LINES;j++)
        {
            ALfloat input = samples[j][i];
            ALfloat out =
                FadedDelayLineOut(&delay, vap_offset[j][0]++, vap_offset[j][1]++, j,
                    1.0f-fade, fade
                ) - feedCoeff*input;
            f[j] = input + feedCoeff*out;

            samples[j][i] = out;
        }
        fade += FadeStep;

        VectorScatterDelayIn(&delay, offset, f, xCoeff, yCoeff);
        ++offset;
    }
}

/* This generates early reflections.
 *
 * This is done by obtaining the primary reflections (those arriving from the
 * same direction as the source) from the main delay line.  These are
 * attenuated and all-pass filtered (based on the diffusion parameter).
 *
 * The early lines are then fed in reverse (according to the approximately
 * opposite spatial location of the A-Format lines) to create the secondary
 * reflections (those arriving from the opposite direction as the source).
 *
 * The early response is then completed by combining the primary reflections
 * with the delayed and attenuated output from the early lines.
 *
 * Finally, the early response is reversed, scattered (based on diffusion),
 * and fed into the late reverb section of the main delay line.
 *
 * Two static specializations are used for transitional (cross-faded) delay
 * line processing and non-transitional processing.
 */
static void EarlyReflection_Unfaded(ReverbState *State, ALsizei offset, const ALsizei todo,
                                    ALfloat (*restrict out)[MAX_UPDATE_SAMPLES])
{
    ALfloat (*restrict temps)[MAX_UPDATE_SAMPLES] = State->TempSamples;
    const DelayLineI early_delay = State->Early.Delay;
    const DelayLineI main_delay = State->Delay;
    const ALfloat mixX = State->MixX;
    const ALfloat mixY = State->MixY;
    ALsizei late_feed_tap;
    ALsizei i, j;

    ASSUME(todo > 0);

    /* First, load decorrelated samples from the main delay line as the primary
     * reflections.
     */
    for(j = 0;j < NUM_LINES;j++)
    {
        ALsizei early_delay_tap = offset - State->EarlyDelayTap[j][0];
        ALfloat coeff = State->EarlyDelayCoeff[j][0];
        for(i = 0;i < todo;i++)
            temps[j][i] = DelayLineOut(&main_delay, early_delay_tap++, j) * coeff;
    }

    /* Apply a vector all-pass, to help color the initial reflections based on
     * the diffusion strength.
     */
    VectorAllpass_Unfaded(temps, offset, mixX, mixY, todo, &State->Early.VecAp);

    /* Apply a delay and bounce to generate secondary reflections, combine with
     * the primary reflections and write out the result for mixing.
     */
    for(j = 0;j < NUM_LINES;j++)
    {
        ALint early_feedb_tap = offset - State->Early.Offset[j][0];
        ALfloat early_feedb_coeff = State->Early.Coeff[j][0];

        for(i = 0;i < todo;i++)
            out[j][i] = DelayLineOut(&early_delay, early_feedb_tap++, j)*early_feedb_coeff +
                        temps[j][i];
    }
    for(j = 0;j < NUM_LINES;j++)
        DelayLineIn(&early_delay, offset, NUM_LINES-1-j, temps[j], todo);

    /* Also write the result back to the main delay line for the late reverb
     * stage to pick up at the appropriate time, appplying a scatter and
     * bounce to improve the initial diffusion in the late reverb.
     */
    late_feed_tap = offset - State->LateFeedTap;
    VectorScatterRevDelayIn(&main_delay, late_feed_tap, mixX, mixY, out, todo);
}
static void EarlyReflection_Faded(ReverbState *State, ALsizei offset, const ALsizei todo,
                                  const ALfloat fade, ALfloat (*restrict out)[MAX_UPDATE_SAMPLES])
{
    ALfloat (*restrict temps)[MAX_UPDATE_SAMPLES] = State->TempSamples;
    const DelayLineI early_delay = State->Early.Delay;
    const DelayLineI main_delay = State->Delay;
    const ALfloat mixX = State->MixX;
    const ALfloat mixY = State->MixY;
    ALsizei late_feed_tap;
    ALsizei i, j;

    ASSUME(todo > 0);

    for(j = 0;j < NUM_LINES;j++)
    {
        ALsizei early_delay_tap0 = offset - State->EarlyDelayTap[j][0];
        ALsizei early_delay_tap1 = offset - State->EarlyDelayTap[j][1];
        ALfloat oldCoeff = State->EarlyDelayCoeff[j][0];
        ALfloat oldCoeffStep = -oldCoeff / FADE_SAMPLES;
        ALfloat newCoeffStep = State->EarlyDelayCoeff[j][1] / FADE_SAMPLES;
        ALfloat fadeCount = fade;

        for(i = 0;i < todo;i++)
        {
            const ALfloat fade0 = oldCoeff + oldCoeffStep*fadeCount;
            const ALfloat fade1 = newCoeffStep*fadeCount;
            temps[j][i] = FadedDelayLineOut(&main_delay,
                early_delay_tap0++, early_delay_tap1++, j, fade0, fade1
            );
            fadeCount += 1.0f;
        }
    }

    VectorAllpass_Faded(temps, offset, mixX, mixY, fade, todo, &State->Early.VecAp);

    for(j = 0;j < NUM_LINES;j++)
    {
        ALint feedb_tap0 = offset - State->Early.Offset[j][0];
        ALint feedb_tap1 = offset - State->Early.Offset[j][1];
        ALfloat feedb_oldCoeff = State->Early.Coeff[j][0];
        ALfloat feedb_oldCoeffStep = -feedb_oldCoeff / FADE_SAMPLES;
        ALfloat feedb_newCoeffStep = State->Early.Coeff[j][1] / FADE_SAMPLES;
        ALfloat fadeCount = fade;

        for(i = 0;i < todo;i++)
        {
            const ALfloat fade0 = feedb_oldCoeff + feedb_oldCoeffStep*fadeCount;
            const ALfloat fade1 = feedb_newCoeffStep*fadeCount;
            out[j][i] = FadedDelayLineOut(&early_delay,
                feedb_tap0++, feedb_tap1++, j, fade0, fade1
            ) + temps[j][i];
            fadeCount += 1.0f;
        }
    }
    for(j = 0;j < NUM_LINES;j++)
        DelayLineIn(&early_delay, offset, NUM_LINES-1-j, temps[j], todo);

    late_feed_tap = offset - State->LateFeedTap;
    VectorScatterRevDelayIn(&main_delay, late_feed_tap, mixX, mixY, out, todo);
}

/* Applies the two T60 damping filter sections. */
static inline void LateT60Filter(ALfloat *restrict samples, const ALsizei todo, T60Filter *filter)
{
    ALfloat temp[MAX_UPDATE_SAMPLES];
    BiquadFilter_process(&filter->HFFilter, temp, samples, todo);
    BiquadFilter_process(&filter->LFFilter, samples, temp, todo);
}

/* This generates the reverb tail using a modified feed-back delay network
 * (FDN).
 *
 * Results from the early reflections are mixed with the output from the late
 * delay lines.
 *
 * The late response is then completed by T60 and all-pass filtering the mix.
 *
 * Finally, the lines are reversed (so they feed their opposite directions)
 * and scattered with the FDN matrix before re-feeding the delay lines.
 *
 * Two variations are made, one for for transitional (cross-faded) delay line
 * processing and one for non-transitional processing.
 */
static void LateReverb_Unfaded(ReverbState *State, ALsizei offset, const ALsizei todo,
                               ALfloat (*restrict out)[MAX_UPDATE_SAMPLES])
{
    ALfloat (*restrict temps)[MAX_UPDATE_SAMPLES] = State->TempSamples;
    const DelayLineI late_delay = State->Late.Delay;
    const DelayLineI main_delay = State->Delay;
    const ALfloat mixX = State->MixX;
    const ALfloat mixY = State->MixY;
    ALsizei i, j;

    ASSUME(todo > 0);

    /* First, load decorrelated samples from the main and feedback delay lines.
     * Filter the signal to apply its frequency-dependent decay.
     */
    for(j = 0;j < NUM_LINES;j++)
    {
        ALsizei late_delay_tap = offset - State->LateDelayTap[j][0];
        ALsizei late_feedb_tap = offset - State->Late.Offset[j][0];
        ALfloat midGain = State->Late.T60[j].MidGain[0];
        const ALfloat densityGain = State->Late.DensityGain[0] * midGain;
        for(i = 0;i < todo;i++)
            temps[j][i] = DelayLineOut(&main_delay, late_delay_tap++, j)*densityGain +
                          DelayLineOut(&late_delay, late_feedb_tap++, j)*midGain;
        LateT60Filter(temps[j], todo, &State->Late.T60[j]);
    }

    /* Apply a vector all-pass to improve micro-surface diffusion, and write
     * out the results for mixing.
     */
    VectorAllpass_Unfaded(temps, offset, mixX, mixY, todo, &State->Late.VecAp);

    for(j = 0;j < NUM_LINES;j++)
        memcpy(out[j], temps[j], todo*sizeof(ALfloat));

    /* Finally, scatter and bounce the results to refeed the feedback buffer. */
    VectorScatterRevDelayIn(&late_delay, offset, mixX, mixY, out, todo);
}
static void LateReverb_Faded(ReverbState *State, ALsizei offset, const ALsizei todo,
                             const ALfloat fade, ALfloat (*restrict out)[MAX_UPDATE_SAMPLES])
{
    ALfloat (*restrict temps)[MAX_UPDATE_SAMPLES] = State->TempSamples;
    const DelayLineI late_delay = State->Late.Delay;
    const DelayLineI main_delay = State->Delay;
    const ALfloat mixX = State->MixX;
    const ALfloat mixY = State->MixY;
    ALsizei i, j;

    ASSUME(todo > 0);

    for(j = 0;j < NUM_LINES;j++)
    {
        const ALfloat oldMidGain = State->Late.T60[j].MidGain[0];
        const ALfloat midGain = State->Late.T60[j].MidGain[1];
        const ALfloat oldMidStep = -oldMidGain / FADE_SAMPLES;
        const ALfloat midStep = midGain / FADE_SAMPLES;
        const ALfloat oldDensityGain = State->Late.DensityGain[0] * oldMidGain;
        const ALfloat densityGain = State->Late.DensityGain[1] * midGain;
        const ALfloat oldDensityStep = -oldDensityGain / FADE_SAMPLES;
        const ALfloat densityStep = densityGain / FADE_SAMPLES;
        ALsizei late_delay_tap0 = offset - State->LateDelayTap[j][0];
        ALsizei late_delay_tap1 = offset - State->LateDelayTap[j][1];
        ALsizei late_feedb_tap0 = offset - State->Late.Offset[j][0];
        ALsizei late_feedb_tap1 = offset - State->Late.Offset[j][1];
        ALfloat fadeCount = fade;

        for(i = 0;i < todo;i++)
        {
            const ALfloat fade0 = oldDensityGain + oldDensityStep*fadeCount;
            const ALfloat fade1 = densityStep*fadeCount;
            const ALfloat gfade0 = oldMidGain + oldMidStep*fadeCount;
            const ALfloat gfade1 = midStep*fadeCount;
            temps[j][i] =
                FadedDelayLineOut(&main_delay, late_delay_tap0++, late_delay_tap1++, j,
                    fade0, fade1) +
                FadedDelayLineOut(&late_delay, late_feedb_tap0++, late_feedb_tap1++, j,
                    gfade0, gfade1);
            fadeCount += 1.0f;
        }
        LateT60Filter(temps[j], todo, &State->Late.T60[j]);
    }

    VectorAllpass_Faded(temps, offset, mixX, mixY, fade, todo, &State->Late.VecAp);

    for(j = 0;j < NUM_LINES;j++)
        memcpy(out[j], temps[j], todo*sizeof(ALfloat));

    VectorScatterRevDelayIn(&late_delay, offset, mixX, mixY, temps, todo);
}

static ALvoid ReverbState_process(ReverbState *State, ALsizei SamplesToDo, const ALfloat (*restrict SamplesIn)[BUFFERSIZE], ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALsizei NumChannels)
{
    ALfloat (*restrict afmt)[MAX_UPDATE_SAMPLES] = State->TempSamples;
    ALfloat (*restrict samples)[MAX_UPDATE_SAMPLES] = State->MixSamples;
    ALsizei fadeCount = State->FadeCount;
    ALsizei offset = State->Offset;
    ALsizei base, c;

    /* Process reverb for these samples. */
    for(base = 0;base < SamplesToDo;)
    {
        ALsizei todo = SamplesToDo - base;
        /* If cross-fading, don't do more samples than there are to fade. */
        if(FADE_SAMPLES-fadeCount > 0)
        {
            todo = mini(todo, FADE_SAMPLES-fadeCount);
            todo = mini(todo, State->MaxUpdate[0]);
        }
        todo = mini(todo, State->MaxUpdate[1]);
        /* If this is not the final update, ensure the update size is a
         * multiple of 4 for the SIMD mixers.
         */
        if(todo < SamplesToDo-base)
            todo &= ~3;

        /* Convert B-Format to A-Format for processing. */
        memset(afmt, 0, sizeof(*afmt)*NUM_LINES);
        for(c = 0;c < NUM_LINES;c++)
            MixRowSamples(afmt[c], B2A.m[c],
                SamplesIn, MAX_EFFECT_CHANNELS, base, todo
            );

        /* Process the samples for reverb. */
        for(c = 0;c < NUM_LINES;c++)
        {
            /* Band-pass the incoming samples. */
            BiquadFilter_process(&State->Filter[c].Lp, samples[0], afmt[c], todo);
            BiquadFilter_process(&State->Filter[c].Hp, samples[1], samples[0], todo);

            /* Feed the initial delay line. */
            DelayLineIn(&State->Delay, offset, c, samples[1], todo);
        }

        if(UNLIKELY(fadeCount < FADE_SAMPLES))
        {
            ALfloat fade = (ALfloat)fadeCount;

            /* Generate early reflections. */
            EarlyReflection_Faded(State, offset, todo, fade, samples);
            /* Mix the A-Format results to output, implicitly converting back
             * to B-Format.
             */
            for(c = 0;c < NUM_LINES;c++)
                MixSamples(samples[c], NumChannels, SamplesOut,
                    State->Early.CurrentGain[c], State->Early.PanGain[c],
                    SamplesToDo-base, base, todo
                );

            /* Generate and mix late reverb. */
            LateReverb_Faded(State, offset, todo, fade, samples);
            for(c = 0;c < NUM_LINES;c++)
                MixSamples(samples[c], NumChannels, SamplesOut,
                    State->Late.CurrentGain[c], State->Late.PanGain[c],
                    SamplesToDo-base, base, todo
                );

            /* Step fading forward. */
            fadeCount += todo;
            if(LIKELY(fadeCount >= FADE_SAMPLES))
            {
                /* Update the cross-fading delay line taps. */
                fadeCount = FADE_SAMPLES;
                for(c = 0;c < NUM_LINES;c++)
                {
                    State->EarlyDelayTap[c][0] = State->EarlyDelayTap[c][1];
                    State->EarlyDelayCoeff[c][0] = State->EarlyDelayCoeff[c][1];
                    State->Early.VecAp.Offset[c][0] = State->Early.VecAp.Offset[c][1];
                    State->Early.Offset[c][0] = State->Early.Offset[c][1];
                    State->Early.Coeff[c][0] = State->Early.Coeff[c][1];
                    State->LateDelayTap[c][0] = State->LateDelayTap[c][1];
                    State->Late.VecAp.Offset[c][0] = State->Late.VecAp.Offset[c][1];
                    State->Late.Offset[c][0] = State->Late.Offset[c][1];
                    State->Late.T60[c].MidGain[0] = State->Late.T60[c].MidGain[1];
                }
                State->Late.DensityGain[0] = State->Late.DensityGain[1];
                State->MaxUpdate[0] = State->MaxUpdate[1];
            }
        }
        else
        {
            /* Generate and mix early reflections. */
            EarlyReflection_Unfaded(State, offset, todo, samples);
            for(c = 0;c < NUM_LINES;c++)
                MixSamples(samples[c], NumChannels, SamplesOut,
                    State->Early.CurrentGain[c], State->Early.PanGain[c],
                    SamplesToDo-base, base, todo
                );

            /* Generate and mix late reverb. */
            LateReverb_Unfaded(State, offset, todo, samples);
            for(c = 0;c < NUM_LINES;c++)
                MixSamples(samples[c], NumChannels, SamplesOut,
                    State->Late.CurrentGain[c], State->Late.PanGain[c],
                    SamplesToDo-base, base, todo
                );
        }

        /* Step all delays forward. */
        offset += todo;

        base += todo;
    }
    State->Offset = offset;
    State->FadeCount = fadeCount;
}


typedef struct ReverbStateFactory {
    DERIVE_FROM_TYPE(EffectStateFactory);
} ReverbStateFactory;

static ALeffectState *ReverbStateFactory_create(ReverbStateFactory* UNUSED(factory))
{
    ReverbState *state;

    NEW_OBJ0(state, ReverbState)();
    if(!state) return NULL;

    return STATIC_CAST(ALeffectState, state);
}

DEFINE_EFFECTSTATEFACTORY_VTABLE(ReverbStateFactory);

EffectStateFactory *ReverbStateFactory_getFactory(void)
{
    static ReverbStateFactory ReverbFactory = { { GET_VTABLE2(ReverbStateFactory, EffectStateFactory) } };

    return STATIC_CAST(EffectStateFactory, &ReverbFactory);
}


void ALeaxreverb_setParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_EAXREVERB_DECAY_HFLIMIT:
            if(!(val >= AL_EAXREVERB_MIN_DECAY_HFLIMIT && val <= AL_EAXREVERB_MAX_DECAY_HFLIMIT))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb decay hflimit out of range");
            props->Reverb.DecayHFLimit = val;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid EAX reverb integer property 0x%04x",
                       param);
    }
}
void ALeaxreverb_setParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{ ALeaxreverb_setParami(effect, context, param, vals[0]); }
void ALeaxreverb_setParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_EAXREVERB_DENSITY:
            if(!(val >= AL_EAXREVERB_MIN_DENSITY && val <= AL_EAXREVERB_MAX_DENSITY))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb density out of range");
            props->Reverb.Density = val;
            break;

        case AL_EAXREVERB_DIFFUSION:
            if(!(val >= AL_EAXREVERB_MIN_DIFFUSION && val <= AL_EAXREVERB_MAX_DIFFUSION))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb diffusion out of range");
            props->Reverb.Diffusion = val;
            break;

        case AL_EAXREVERB_GAIN:
            if(!(val >= AL_EAXREVERB_MIN_GAIN && val <= AL_EAXREVERB_MAX_GAIN))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb gain out of range");
            props->Reverb.Gain = val;
            break;

        case AL_EAXREVERB_GAINHF:
            if(!(val >= AL_EAXREVERB_MIN_GAINHF && val <= AL_EAXREVERB_MAX_GAINHF))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb gainhf out of range");
            props->Reverb.GainHF = val;
            break;

        case AL_EAXREVERB_GAINLF:
            if(!(val >= AL_EAXREVERB_MIN_GAINLF && val <= AL_EAXREVERB_MAX_GAINLF))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb gainlf out of range");
            props->Reverb.GainLF = val;
            break;

        case AL_EAXREVERB_DECAY_TIME:
            if(!(val >= AL_EAXREVERB_MIN_DECAY_TIME && val <= AL_EAXREVERB_MAX_DECAY_TIME))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb decay time out of range");
            props->Reverb.DecayTime = val;
            break;

        case AL_EAXREVERB_DECAY_HFRATIO:
            if(!(val >= AL_EAXREVERB_MIN_DECAY_HFRATIO && val <= AL_EAXREVERB_MAX_DECAY_HFRATIO))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb decay hfratio out of range");
            props->Reverb.DecayHFRatio = val;
            break;

        case AL_EAXREVERB_DECAY_LFRATIO:
            if(!(val >= AL_EAXREVERB_MIN_DECAY_LFRATIO && val <= AL_EAXREVERB_MAX_DECAY_LFRATIO))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb decay lfratio out of range");
            props->Reverb.DecayLFRatio = val;
            break;

        case AL_EAXREVERB_REFLECTIONS_GAIN:
            if(!(val >= AL_EAXREVERB_MIN_REFLECTIONS_GAIN && val <= AL_EAXREVERB_MAX_REFLECTIONS_GAIN))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb reflections gain out of range");
            props->Reverb.ReflectionsGain = val;
            break;

        case AL_EAXREVERB_REFLECTIONS_DELAY:
            if(!(val >= AL_EAXREVERB_MIN_REFLECTIONS_DELAY && val <= AL_EAXREVERB_MAX_REFLECTIONS_DELAY))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb reflections delay out of range");
            props->Reverb.ReflectionsDelay = val;
            break;

        case AL_EAXREVERB_LATE_REVERB_GAIN:
            if(!(val >= AL_EAXREVERB_MIN_LATE_REVERB_GAIN && val <= AL_EAXREVERB_MAX_LATE_REVERB_GAIN))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb late reverb gain out of range");
            props->Reverb.LateReverbGain = val;
            break;

        case AL_EAXREVERB_LATE_REVERB_DELAY:
            if(!(val >= AL_EAXREVERB_MIN_LATE_REVERB_DELAY && val <= AL_EAXREVERB_MAX_LATE_REVERB_DELAY))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb late reverb delay out of range");
            props->Reverb.LateReverbDelay = val;
            break;

        case AL_EAXREVERB_AIR_ABSORPTION_GAINHF:
            if(!(val >= AL_EAXREVERB_MIN_AIR_ABSORPTION_GAINHF && val <= AL_EAXREVERB_MAX_AIR_ABSORPTION_GAINHF))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb air absorption gainhf out of range");
            props->Reverb.AirAbsorptionGainHF = val;
            break;

        case AL_EAXREVERB_ECHO_TIME:
            if(!(val >= AL_EAXREVERB_MIN_ECHO_TIME && val <= AL_EAXREVERB_MAX_ECHO_TIME))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb echo time out of range");
            props->Reverb.EchoTime = val;
            break;

        case AL_EAXREVERB_ECHO_DEPTH:
            if(!(val >= AL_EAXREVERB_MIN_ECHO_DEPTH && val <= AL_EAXREVERB_MAX_ECHO_DEPTH))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb echo depth out of range");
            props->Reverb.EchoDepth = val;
            break;

        case AL_EAXREVERB_MODULATION_TIME:
            if(!(val >= AL_EAXREVERB_MIN_MODULATION_TIME && val <= AL_EAXREVERB_MAX_MODULATION_TIME))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb modulation time out of range");
            props->Reverb.ModulationTime = val;
            break;

        case AL_EAXREVERB_MODULATION_DEPTH:
            if(!(val >= AL_EAXREVERB_MIN_MODULATION_DEPTH && val <= AL_EAXREVERB_MAX_MODULATION_DEPTH))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb modulation depth out of range");
            props->Reverb.ModulationDepth = val;
            break;

        case AL_EAXREVERB_HFREFERENCE:
            if(!(val >= AL_EAXREVERB_MIN_HFREFERENCE && val <= AL_EAXREVERB_MAX_HFREFERENCE))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb hfreference out of range");
            props->Reverb.HFReference = val;
            break;

        case AL_EAXREVERB_LFREFERENCE:
            if(!(val >= AL_EAXREVERB_MIN_LFREFERENCE && val <= AL_EAXREVERB_MAX_LFREFERENCE))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb lfreference out of range");
            props->Reverb.LFReference = val;
            break;

        case AL_EAXREVERB_ROOM_ROLLOFF_FACTOR:
            if(!(val >= AL_EAXREVERB_MIN_ROOM_ROLLOFF_FACTOR && val <= AL_EAXREVERB_MAX_ROOM_ROLLOFF_FACTOR))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb room rolloff factor out of range");
            props->Reverb.RoomRolloffFactor = val;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid EAX reverb float property 0x%04x",
                       param);
    }
}
void ALeaxreverb_setParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_EAXREVERB_REFLECTIONS_PAN:
            if(!(isfinite(vals[0]) && isfinite(vals[1]) && isfinite(vals[2])))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb reflections pan out of range");
            props->Reverb.ReflectionsPan[0] = vals[0];
            props->Reverb.ReflectionsPan[1] = vals[1];
            props->Reverb.ReflectionsPan[2] = vals[2];
            break;
        case AL_EAXREVERB_LATE_REVERB_PAN:
            if(!(isfinite(vals[0]) && isfinite(vals[1]) && isfinite(vals[2])))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb late reverb pan out of range");
            props->Reverb.LateReverbPan[0] = vals[0];
            props->Reverb.LateReverbPan[1] = vals[1];
            props->Reverb.LateReverbPan[2] = vals[2];
            break;

        default:
            ALeaxreverb_setParamf(effect, context, param, vals[0]);
            break;
    }
}

void ALeaxreverb_getParami(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *val)
{
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_EAXREVERB_DECAY_HFLIMIT:
            *val = props->Reverb.DecayHFLimit;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid EAX reverb integer property 0x%04x",
                       param);
    }
}
void ALeaxreverb_getParamiv(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{ ALeaxreverb_getParami(effect, context, param, vals); }
void ALeaxreverb_getParamf(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val)
{
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_EAXREVERB_DENSITY:
            *val = props->Reverb.Density;
            break;

        case AL_EAXREVERB_DIFFUSION:
            *val = props->Reverb.Diffusion;
            break;

        case AL_EAXREVERB_GAIN:
            *val = props->Reverb.Gain;
            break;

        case AL_EAXREVERB_GAINHF:
            *val = props->Reverb.GainHF;
            break;

        case AL_EAXREVERB_GAINLF:
            *val = props->Reverb.GainLF;
            break;

        case AL_EAXREVERB_DECAY_TIME:
            *val = props->Reverb.DecayTime;
            break;

        case AL_EAXREVERB_DECAY_HFRATIO:
            *val = props->Reverb.DecayHFRatio;
            break;

        case AL_EAXREVERB_DECAY_LFRATIO:
            *val = props->Reverb.DecayLFRatio;
            break;

        case AL_EAXREVERB_REFLECTIONS_GAIN:
            *val = props->Reverb.ReflectionsGain;
            break;

        case AL_EAXREVERB_REFLECTIONS_DELAY:
            *val = props->Reverb.ReflectionsDelay;
            break;

        case AL_EAXREVERB_LATE_REVERB_GAIN:
            *val = props->Reverb.LateReverbGain;
            break;

        case AL_EAXREVERB_LATE_REVERB_DELAY:
            *val = props->Reverb.LateReverbDelay;
            break;

        case AL_EAXREVERB_AIR_ABSORPTION_GAINHF:
            *val = props->Reverb.AirAbsorptionGainHF;
            break;

        case AL_EAXREVERB_ECHO_TIME:
            *val = props->Reverb.EchoTime;
            break;

        case AL_EAXREVERB_ECHO_DEPTH:
            *val = props->Reverb.EchoDepth;
            break;

        case AL_EAXREVERB_MODULATION_TIME:
            *val = props->Reverb.ModulationTime;
            break;

        case AL_EAXREVERB_MODULATION_DEPTH:
            *val = props->Reverb.ModulationDepth;
            break;

        case AL_EAXREVERB_HFREFERENCE:
            *val = props->Reverb.HFReference;
            break;

        case AL_EAXREVERB_LFREFERENCE:
            *val = props->Reverb.LFReference;
            break;

        case AL_EAXREVERB_ROOM_ROLLOFF_FACTOR:
            *val = props->Reverb.RoomRolloffFactor;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid EAX reverb float property 0x%04x",
                       param);
    }
}
void ALeaxreverb_getParamfv(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_EAXREVERB_REFLECTIONS_PAN:
            vals[0] = props->Reverb.ReflectionsPan[0];
            vals[1] = props->Reverb.ReflectionsPan[1];
            vals[2] = props->Reverb.ReflectionsPan[2];
            break;
        case AL_EAXREVERB_LATE_REVERB_PAN:
            vals[0] = props->Reverb.LateReverbPan[0];
            vals[1] = props->Reverb.LateReverbPan[1];
            vals[2] = props->Reverb.LateReverbPan[2];
            break;

        default:
            ALeaxreverb_getParamf(effect, context, param, vals);
            break;
    }
}

DEFINE_ALEFFECT_VTABLE(ALeaxreverb);

void ALreverb_setParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_REVERB_DECAY_HFLIMIT:
            if(!(val >= AL_REVERB_MIN_DECAY_HFLIMIT && val <= AL_REVERB_MAX_DECAY_HFLIMIT))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Reverb decay hflimit out of range");
            props->Reverb.DecayHFLimit = val;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid reverb integer property 0x%04x", param);
    }
}
void ALreverb_setParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{ ALreverb_setParami(effect, context, param, vals[0]); }
void ALreverb_setParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_REVERB_DENSITY:
            if(!(val >= AL_REVERB_MIN_DENSITY && val <= AL_REVERB_MAX_DENSITY))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Reverb density out of range");
            props->Reverb.Density = val;
            break;

        case AL_REVERB_DIFFUSION:
            if(!(val >= AL_REVERB_MIN_DIFFUSION && val <= AL_REVERB_MAX_DIFFUSION))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Reverb diffusion out of range");
            props->Reverb.Diffusion = val;
            break;

        case AL_REVERB_GAIN:
            if(!(val >= AL_REVERB_MIN_GAIN && val <= AL_REVERB_MAX_GAIN))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Reverb gain out of range");
            props->Reverb.Gain = val;
            break;

        case AL_REVERB_GAINHF:
            if(!(val >= AL_REVERB_MIN_GAINHF && val <= AL_REVERB_MAX_GAINHF))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Reverb gainhf out of range");
            props->Reverb.GainHF = val;
            break;

        case AL_REVERB_DECAY_TIME:
            if(!(val >= AL_REVERB_MIN_DECAY_TIME && val <= AL_REVERB_MAX_DECAY_TIME))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Reverb decay time out of range");
            props->Reverb.DecayTime = val;
            break;

        case AL_REVERB_DECAY_HFRATIO:
            if(!(val >= AL_REVERB_MIN_DECAY_HFRATIO && val <= AL_REVERB_MAX_DECAY_HFRATIO))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Reverb decay hfratio out of range");
            props->Reverb.DecayHFRatio = val;
            break;

        case AL_REVERB_REFLECTIONS_GAIN:
            if(!(val >= AL_REVERB_MIN_REFLECTIONS_GAIN && val <= AL_REVERB_MAX_REFLECTIONS_GAIN))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Reverb reflections gain out of range");
            props->Reverb.ReflectionsGain = val;
            break;

        case AL_REVERB_REFLECTIONS_DELAY:
            if(!(val >= AL_REVERB_MIN_REFLECTIONS_DELAY && val <= AL_REVERB_MAX_REFLECTIONS_DELAY))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Reverb reflections delay out of range");
            props->Reverb.ReflectionsDelay = val;
            break;

        case AL_REVERB_LATE_REVERB_GAIN:
            if(!(val >= AL_REVERB_MIN_LATE_REVERB_GAIN && val <= AL_REVERB_MAX_LATE_REVERB_GAIN))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Reverb late reverb gain out of range");
            props->Reverb.LateReverbGain = val;
            break;

        case AL_REVERB_LATE_REVERB_DELAY:
            if(!(val >= AL_REVERB_MIN_LATE_REVERB_DELAY && val <= AL_REVERB_MAX_LATE_REVERB_DELAY))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Reverb late reverb delay out of range");
            props->Reverb.LateReverbDelay = val;
            break;

        case AL_REVERB_AIR_ABSORPTION_GAINHF:
            if(!(val >= AL_REVERB_MIN_AIR_ABSORPTION_GAINHF && val <= AL_REVERB_MAX_AIR_ABSORPTION_GAINHF))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Reverb air absorption gainhf out of range");
            props->Reverb.AirAbsorptionGainHF = val;
            break;

        case AL_REVERB_ROOM_ROLLOFF_FACTOR:
            if(!(val >= AL_REVERB_MIN_ROOM_ROLLOFF_FACTOR && val <= AL_REVERB_MAX_ROOM_ROLLOFF_FACTOR))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Reverb room rolloff factor out of range");
            props->Reverb.RoomRolloffFactor = val;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid reverb float property 0x%04x", param);
    }
}
void ALreverb_setParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{ ALreverb_setParamf(effect, context, param, vals[0]); }

void ALreverb_getParami(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *val)
{
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_REVERB_DECAY_HFLIMIT:
            *val = props->Reverb.DecayHFLimit;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid reverb integer property 0x%04x", param);
    }
}
void ALreverb_getParamiv(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{ ALreverb_getParami(effect, context, param, vals); }
void ALreverb_getParamf(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val)
{
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_REVERB_DENSITY:
            *val = props->Reverb.Density;
            break;

        case AL_REVERB_DIFFUSION:
            *val = props->Reverb.Diffusion;
            break;

        case AL_REVERB_GAIN:
            *val = props->Reverb.Gain;
            break;

        case AL_REVERB_GAINHF:
            *val = props->Reverb.GainHF;
            break;

        case AL_REVERB_DECAY_TIME:
            *val = props->Reverb.DecayTime;
            break;

        case AL_REVERB_DECAY_HFRATIO:
            *val = props->Reverb.DecayHFRatio;
            break;

        case AL_REVERB_REFLECTIONS_GAIN:
            *val = props->Reverb.ReflectionsGain;
            break;

        case AL_REVERB_REFLECTIONS_DELAY:
            *val = props->Reverb.ReflectionsDelay;
            break;

        case AL_REVERB_LATE_REVERB_GAIN:
            *val = props->Reverb.LateReverbGain;
            break;

        case AL_REVERB_LATE_REVERB_DELAY:
            *val = props->Reverb.LateReverbDelay;
            break;

        case AL_REVERB_AIR_ABSORPTION_GAINHF:
            *val = props->Reverb.AirAbsorptionGainHF;
            break;

        case AL_REVERB_ROOM_ROLLOFF_FACTOR:
            *val = props->Reverb.RoomRolloffFactor;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid reverb float property 0x%04x", param);
    }
}
void ALreverb_getParamfv(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{ ALreverb_getParamf(effect, context, param, vals); }

DEFINE_ALEFFECT_VTABLE(ALreverb);
