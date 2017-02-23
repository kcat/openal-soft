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
#include "alEffect.h"
#include "alFilter.h"
#include "alError.h"
#include "mixer_defs.h"


static const int PrimeTable[1024] = {
       2,    3,    5,    7,   11,  13,    17,   19,   23,   29,   31,   37,
      41,   43,   47,  53,    59,   61,   67,   71,   73,   79,   83,   89,
      97,  101,  103,  107,  109,  113,  127,  131,  137,  139,  149,  151,
     157,  163,  167,  173,  179,  181,  191,  193,  197,  199,  211,  223,
     227,  229,  233,  239,  241,  251,  257,  263,  269,  271,  277,  281,
     283,  293,  307,  311,  313,  317,  331,  337,  347,  349,  353,  359,
     367,  373,  379,  383,  389,  397,  401,  409,  419,  421,  431,  433,
     439,  443,  449,  457,  461,  463,  467,  479,  487,  491,  499,  503,
     509,  521,  523,  541,  547,  557,  563,  569,  571,  577,  587,  593,
     599,  601,  607,  613,  617,  619,  631,  641,  643,  647,  653,  659,
     661,  673,  677,  683,  691,  701,  709,  719,  727,  733,  739,  743,
     751,  757,  761,  769,  773,  787,  797,  809,  811,  821,  823,  827,
     829,  839,  853,  857,  859,  863,  877,  881,  883,  887,  907,  911,
     919,  929,  937,  941,  947,  953,  967,  971,  977,  983,  991,  997,
    1009, 1013, 1019, 1021, 1031, 1033, 1039, 1049, 1051, 1061, 1063, 1069,
    1087, 1091, 1093, 1097, 1103, 1109, 1117, 1123, 1129, 1151, 1153, 1163,
    1171, 1181, 1187, 1193, 1201, 1213, 1217, 1223, 1229, 1231, 1237, 1249,
    1259, 1277, 1279, 1283, 1289, 1291, 1297, 1301, 1303, 1307, 1319, 1321,
    1327, 1361, 1367, 1373, 1381, 1399, 1409, 1423, 1427, 1429, 1433, 1439,
    1447, 1451, 1453, 1459, 1471, 1481, 1483, 1487, 1489, 1493, 1499, 1511,
    1523, 1531, 1543, 1549, 1553, 1559, 1567, 1571, 1579, 1583, 1597, 1601,
    1607, 1609, 1613, 1619, 1621, 1627, 1637, 1657, 1663, 1667, 1669, 1693,
    1697, 1699, 1709, 1721, 1723, 1733, 1741, 1747, 1753, 1759, 1777, 1783,
    1787, 1789, 1801, 1811, 1823, 1831, 1847, 1861, 1867, 1871, 1873, 1877,
    1879, 1889, 1901, 1907, 1913, 1931, 1933, 1949, 1951, 1973, 1979, 1987,
    1993, 1997, 1999, 2003, 2011, 2017, 2027, 2029, 2039, 2053, 2063, 2069,
    2081, 2083, 2087, 2089, 2099, 2111, 2113, 2129, 2131, 2137, 2141, 2143,
    2153, 2161, 2179, 2203, 2207, 2213, 2221, 2237, 2239, 2243, 2251, 2267,
    2269, 2273, 2281, 2287, 2293, 2297, 2309, 2311, 2333, 2339, 2341, 2347,
    2351, 2357, 2371, 2377, 2381, 2383, 2389, 2393, 2399, 2411, 2417, 2423,
    2437, 2441, 2447, 2459, 2467, 2473, 2477, 2503, 2521, 2531, 2539, 2543,
    2549, 2551, 2557, 2579, 2591, 2593, 2609, 2617, 2621, 2633, 2647, 2657,
    2659, 2663, 2671, 2677, 2683, 2687, 2689, 2693, 2699, 2707, 2711, 2713,
    2719, 2729, 2731, 2741, 2749, 2753, 2767, 2777, 2789, 2791, 2797, 2801,
    2803, 2819, 2833, 2837, 2843, 2851, 2857, 2861, 2879, 2887, 2897, 2903,
    2909, 2917, 2927, 2939, 2953, 2957, 2963, 2969, 2971, 2999, 3001, 3011,
    3019, 3023, 3037, 3041, 3049, 3061, 3067, 3079, 3083, 3089, 3109, 3119,
    3121, 3137, 3163, 3167, 3169, 3181, 3187, 3191, 3203, 3209, 3217, 3221,
    3229, 3251, 3253, 3257, 3259, 3271, 3299, 3301, 3307, 3313, 3319, 3323,
    3329, 3331, 3343, 3347, 3359, 3361, 3371, 3373, 3389, 3391, 3407, 3413,
    3433, 3449, 3457, 3461, 3463, 3467, 3469, 3491, 3499, 3511, 3517, 3527,
    3529, 3533, 3539, 3541, 3547, 3557, 3559, 3571, 3581, 3583, 3593, 3607,
    3613, 3617, 3623, 3631, 3637, 3643, 3659, 3671, 3673, 3677, 3691, 3697,
    3701, 3709, 3719, 3727, 3733, 3739, 3761, 3767, 3769, 3779, 3793, 3797,
    3803, 3821, 3823, 3833, 3847, 3851, 3853, 3863, 3877, 3881, 3889, 3907,
    3911, 3917, 3919, 3923, 3929, 3931, 3943, 3947, 3967, 3989, 4001, 4003,
    4007, 4013, 4019, 4021, 4027, 4049, 4051, 4057, 4073, 4079, 4091, 4093,
    4099, 4111, 4127, 4129, 4133, 4139, 4153, 4157, 4159, 4177, 4201, 4211,
    4217, 4219, 4229, 4231, 4241, 4243, 4253, 4259, 4261, 4271, 4273, 4283,
    4289, 4297, 4327, 4337, 4339, 4349, 4357, 4363, 4373, 4391, 4397, 4409,
    4421, 4423, 4441, 4447, 4451, 4457, 4463, 4481, 4483, 4493, 4507, 4513,
    4517, 4519, 4523, 4547, 4549, 4561, 4567, 4583, 4591, 4597, 4603, 4621,
    4637, 4639, 4643, 4649, 4651, 4657, 4663, 4673, 4679, 4691, 4703, 4721,
    4723, 4729, 4733, 4751, 4759, 4783, 4787, 4789, 4793, 4799, 4801, 4813,
    4817, 4831, 4861, 4871, 4877, 4889, 4903, 4909, 4919, 4931, 4933, 4937,
    4943, 4951, 4957, 4967, 4969, 4973, 4987, 4993, 4999, 5003, 5009, 5011,
    5021, 5023, 5039, 5051, 5059, 5077, 5081, 5087, 5099, 5101, 5107, 5113,
    5119, 5147, 5153, 5167, 5171, 5179, 5189, 5197, 5209, 5227, 5231, 5233,
    5237, 5261, 5273, 5279, 5281, 5297, 5303, 5309, 5323, 5333, 5347, 5351,
    5381, 5387, 5393, 5399, 5407, 5413, 5417, 5419, 5431, 5437, 5441, 5443,
    5449, 5471, 5477, 5479, 5483, 5501, 5503, 5507, 5519, 5521, 5527, 5531,
    5557, 5563, 5569, 5573, 5581, 5591, 5623, 5639, 5641, 5647, 5651, 5653,
    5657, 5659, 5669, 5683, 5689, 5693, 5701, 5711, 5717, 5737, 5741, 5743,
    5749, 5779, 5783, 5791, 5801, 5807, 5813, 5821, 5827, 5839, 5843, 5849,
    5851, 5857, 5861, 5867, 5869, 5879, 5881, 5897, 5903, 5923, 5927, 5939,
    5953, 5981, 5987, 6007, 6011, 6029, 6037, 6043, 6047, 6053, 6067, 6073,
    6079, 6089, 6091, 6101, 6113, 6121, 6131, 6133, 6143, 6151, 6163, 6173,
    6197, 6199, 6203, 6211, 6217, 6221, 6229, 6247, 6257, 6263, 6269, 6271,
    6277, 6287, 6299, 6301, 6311, 6317, 6323, 6329, 6337, 6343, 6353, 6359,
    6361, 6367, 6373, 6379, 6389, 6397, 6421, 6427, 6449, 6451, 6469, 6473,
    6481, 6491, 6521, 6529, 6547, 6551, 6553, 6563, 6569, 6571, 6577, 6581,
    6599, 6607, 6619, 6637, 6653, 6659, 6661, 6673, 6679, 6689, 6691, 6701,
    6703, 6709, 6719, 6733, 6737, 6761, 6763, 6779, 6781, 6791, 6793, 6803,
    6823, 6827, 6829, 6833, 6841, 6857, 6863, 6869, 6871, 6883, 6899, 6907,
    6911, 6917, 6947, 6949, 6959, 6961, 6967, 6971, 6977, 6983, 6991, 6997,
    7001, 7013, 7019, 7027, 7039, 7043, 7057, 7069, 7079, 7103, 7109, 7121,
    7127, 7129, 7151, 7159, 7177, 7187, 7193, 7207, 7211, 7213, 7219, 7229,
    7237, 7243, 7247, 7253, 7283, 7297, 7307, 7309, 7321, 7331, 7333, 7349,
    7351, 7369, 7393, 7411, 7417, 7433, 7451, 7457, 7459, 7477, 7481, 7487,
    7489, 7499, 7507, 7517, 7523, 7529, 7537, 7541, 7547, 7549, 7559, 7561,
    7573, 7577, 7583, 7589, 7591, 7603, 7607, 7621, 7639, 7643, 7649, 7669,
    7673, 7681, 7687, 7691, 7699, 7703, 7717, 7723, 7727, 7741, 7753, 7757,
    7759, 7789, 7793, 7817, 7823, 7829, 7841, 7853, 7867, 7873, 7877, 7879,
    7883, 7901, 7907, 7919, 7927, 7933, 7937, 7949, 7951, 7963, 7993, 8009,
    8011, 8017, 8039, 8053, 8059, 8069, 8081, 8087, 8089, 8093, 8101, 8111,
    8117, 8123, 8147, 8161
};

/* This is the maximum number of samples processed for each inner loop
 * iteration. */
#define MAX_UPDATE_SAMPLES  256


static MixerFunc MixSamples = Mix_C;
static RowMixerFunc MixRowSamples = MixRow_C;

static alonce_flag mixfunc_inited = AL_ONCE_FLAG_INIT;
static void init_mixfunc(void)
{
    MixSamples = SelectMixer();
    MixRowSamples = SelectRowMixer();
}


typedef struct DelayLine {
    // The delay lines use sample lengths that are powers of 2 to allow the
    // use of bit-masking instead of a modulus for wrapping.
    ALsizei  Mask;
    ALfloat *Line;
} DelayLine;

typedef struct ALreverbState {
    DERIVE_FROM_TYPE(ALeffectState);

    ALboolean IsEax;

    // All delay lines are allocated as a single buffer to reduce memory
    // fragmentation and management code.
    ALfloat  *SampleBuffer;
    ALuint    TotalSamples;

    // Master effect filters
    struct {
        ALfilterState Lp;
        ALfilterState Hp; // EAX only
    } Filter[4];

    struct {
        // Modulator delay line.
        DelayLine Delay[4];

        // The vibrato time is tracked with an index over a modulus-wrapped
        // range (in samples).
        ALuint    Index;
        ALuint    Range;

        // The depth of frequency change (also in samples) and its filter.
        ALfloat   Depth;
        ALfloat   Coeff;
        ALfloat   Filter;
    } Mod; // EAX only

    /* Core delay line (early reflections and late reverb tap from this). */
    DelayLine Delay;
    /* The tap points for the initial delay. First set go to early
     * reflections, second to late reverb.
     */
    ALsizei EarlyDelayTap[4];
    ALsizei LateDelayTap[4];

    struct {
        // Early reflections are done with 4 delay lines.
        DelayLine Delay[4];
        ALsizei   Offset[4];

        // The gain for each output channel based on 3D panning.
        ALfloat CurrentGain[4][MAX_OUTPUT_CHANNELS];
        ALfloat PanGain[4][MAX_OUTPUT_CHANNELS];
    } Early;

    struct {
        // Attenuation to compensate for the modal density and decay rate of
        // the late lines.
        ALfloat DensityGain;

        // In addition to 4 delay lines.
        ALfloat   Coeff[4];
        DelayLine Delay[4];
        ALsizei   Offset[4];

        // The delay lines are 1-pole low-pass filtered.
        struct {
            ALfloat Sample;
            ALfloat Coeff;
        } Lp[4];

        /* Late reverb has 3 all-pass filters in series on each of the 4 lines.
         */
        struct {
            ALsizei Offsets[3];

            /* One delay line is used for all 3 all-pass filters. */
            DelayLine Delay;
        } Ap[4];

        // The feed-back and feed-forward all-pass coefficient.
        ALfloat ApFeedCoeff;

        // Mixing matrix coefficient.
        ALfloat MixCoeff;

        // Output gain for late reverb.
        ALfloat Gain;

        // The gain for each output channel based on 3D panning.
        ALfloat CurrentGain[4][MAX_OUTPUT_CHANNELS];
        ALfloat PanGain[4][MAX_OUTPUT_CHANNELS];
    } Late;

    struct {
        // Attenuation to compensate for the modal density and decay rate of
        // the echo line.
        ALfloat   DensityGain;

        // Echo delay and all-pass lines.
        struct {
            DelayLine Feedback;
            DelayLine Ap;
        } Delay[4];

        ALfloat   Coeff;
        ALfloat   ApFeedCoeff;

        ALsizei   Offset;
        ALsizei   ApOffset;

        // The echo line is 1-pole low-pass filtered.
        ALfloat   LpCoeff;
        ALfloat   LpSample[4];

        // Echo mixing coefficient.
        ALfloat   MixCoeff;
    } Echo; // EAX only

    // The current read offset for all delay lines.
    ALsizei Offset;

    /* Temporary storage used when processing. */
    alignas(16) ALfloat AFormatSamples[4][MAX_UPDATE_SAMPLES];
    alignas(16) ALfloat ReverbSamples[4][MAX_UPDATE_SAMPLES];
    alignas(16) ALfloat EarlySamples[4][MAX_UPDATE_SAMPLES];
} ALreverbState;

static ALvoid ALreverbState_Destruct(ALreverbState *State);
static ALboolean ALreverbState_deviceUpdate(ALreverbState *State, ALCdevice *Device);
static ALvoid ALreverbState_update(ALreverbState *State, const ALCdevice *Device, const ALeffectslot *Slot, const ALeffectProps *props);
static ALvoid ALreverbState_process(ALreverbState *State, ALuint SamplesToDo, const ALfloat (*restrict SamplesIn)[BUFFERSIZE], ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALuint NumChannels);
DECLARE_DEFAULT_ALLOCATORS(ALreverbState)

DEFINE_ALEFFECTSTATE_VTABLE(ALreverbState);


static void ALreverbState_Construct(ALreverbState *state)
{
    ALuint index, l;

    ALeffectState_Construct(STATIC_CAST(ALeffectState, state));
    SET_VTABLE2(ALreverbState, ALeffectState, state);

    state->IsEax = AL_FALSE;

    state->TotalSamples = 0;
    state->SampleBuffer = NULL;

    for(index = 0;index < 4;index++)
    {
        ALfilterState_clear(&state->Filter[index].Lp);
        ALfilterState_clear(&state->Filter[index].Hp);

        state->Mod.Delay[index].Mask = 0;
        state->Mod.Delay[index].Line = NULL;
    }

    state->Mod.Index = 0;
    state->Mod.Range = 1;
    state->Mod.Depth = 0.0f;
    state->Mod.Coeff = 0.0f;
    state->Mod.Filter = 0.0f;

    state->Delay.Mask = 0;
    state->Delay.Line = NULL;
    for(index = 0;index < 4;index++)
        state->EarlyDelayTap[index] = 0;
    for(index = 0;index < 4;index++)
        state->LateDelayTap[index] = 0;

    for(index = 0;index < 4;index++)
    {
        state->Early.Delay[index].Mask = 0;
        state->Early.Delay[index].Line = NULL;
        state->Early.Offset[index] = 0;
    }

    state->Late.Gain = 0.0f;
    state->Late.DensityGain = 0.0f;
    state->Late.ApFeedCoeff = 0.0f;
    state->Late.MixCoeff = 0.0f;
    for(index = 0;index < 4;index++)
    {
        ALuint k;
        for(k = 0;k < 3;k++)
            state->Late.Ap[index].Offsets[k] = 0;
        state->Late.Ap[index].Delay.Mask = 0;
        state->Late.Ap[index].Delay.Line = NULL;

        state->Late.Coeff[index] = 0.0f;
        state->Late.Delay[index].Mask = 0;
        state->Late.Delay[index].Line = NULL;
        state->Late.Offset[index] = 0;

        state->Late.Lp[index].Sample = 0.0f;
        state->Late.Lp[index].Coeff = 0.0f;
    }

    for(l = 0;l < 4;l++)
    {
        for(index = 0;index < MAX_OUTPUT_CHANNELS;index++)
        {
            state->Early.CurrentGain[l][index] = 0.0f;
            state->Early.PanGain[l][index] = 0.0f;
            state->Late.CurrentGain[l][index] = 0.0f;
            state->Late.PanGain[l][index] = 0.0f;
        }
    }

    state->Echo.DensityGain = 0.0f;
    for(l = 0;l < 4;l++)
    {
        state->Echo.Delay[l].Feedback.Mask = 0;
        state->Echo.Delay[l].Feedback.Line = NULL;
        state->Echo.Delay[l].Ap.Mask = 0;
        state->Echo.Delay[l].Ap.Line = NULL;
    }
    state->Echo.Coeff = 0.0f;
    state->Echo.ApFeedCoeff = 0.0f;
    state->Echo.Offset = 0;
    state->Echo.ApOffset = 0;
    state->Echo.LpCoeff = 0.0f;
    for(l = 0;l < 4;l++)
        state->Echo.LpSample[l] = 0.0f;
    state->Echo.MixCoeff = 0.0f;

    state->Offset = 0;
}

static ALvoid ALreverbState_Destruct(ALreverbState *State)
{
    al_free(State->SampleBuffer);
    State->SampleBuffer = NULL;

    ALeffectState_Destruct(STATIC_CAST(ALeffectState,State));
}

/* This is a user config option for modifying the overall output of the reverb
 * effect.
 */
ALfloat ReverbBoost = 1.0f;

/* Specifies whether to use a standard reverb effect in place of EAX reverb (no
 * high-pass, modulation, or echo).
 */
ALboolean EmulateEAXReverb = AL_FALSE;

/* This coefficient is used to define the maximum frequency range controlled
 * by the modulation depth.  The current value of 0.1 will allow it to swing
 * from 0.9x to 1.1x.  This value must be below 1.  At 1 it will cause the
 * sampler to stall on the downswing, and above 1 it will cause it to sample
 * backwards.
 */
static const ALfloat MODULATION_DEPTH_COEFF = 0.1f;

/* A filter is used to avoid the terrible distortion caused by changing
 * modulation time and/or depth.  To be consistent across different sample
 * rates, the coefficient must be raised to a constant divided by the sample
 * rate:  coeff^(constant / rate).
 */
static const ALfloat MODULATION_FILTER_COEFF = 0.048f;
static const ALfloat MODULATION_FILTER_CONST = 100000.0f;

// When diffusion is above 0, an all-pass filter is used to take the edge off
// the echo effect.  It uses the following line length (in seconds).
static const ALfloat ECHO_ALLPASS_LENGTH = 0.0133f;

/* Input into the early reflections and late reverb are decorrelated between
 * four channels. Their timings are dependent on a fraction and multiplier. See
 * the UpdateDelayLine() routine for the calculations involved.
 */
static const ALfloat DECO_FRACTION = 0.15f;
static const ALfloat DECO_MULTIPLIER = 2.0f;

// All delay line lengths are specified in seconds.

// The lengths of the early delay lines.
static const ALfloat EARLY_LINE_LENGTH[4] =
{
    0.0015f, 0.0045f, 0.0135f, 0.0405f
};

/* The lengths of the late delay lines. */
static const ALfloat LATE_LINE_LENGTH[4] =
{
    0.0211f, 0.0311f, 0.0461f, 0.0680f
};

/* The late delay lines have a variable length dependent on the effect's
 * density parameter (inverted for some reason) and this multiplier.
 */
static const ALfloat LATE_LINE_MULTIPLIER = 3.0f;


#if defined(_WIN32) && !defined (_M_X64) && !defined(_M_ARM)
/* HACK: Workaround for a modff bug in 32-bit Windows, which attempts to write
 * a 64-bit double to the 32-bit float parameter.
 */
static inline float hack_modff(float x, float *y)
{
    double di;
    double df = modf((double)x, &di);
    *y = (float)di;
    return (float)df;
}
#define modff hack_modff
#endif


/**************************************
 *  Device Update                     *
 **************************************/

// Given the allocated sample buffer, this function updates each delay line
// offset.
static inline ALvoid RealizeLineOffset(ALfloat *sampleBuffer, DelayLine *Delay)
{
    Delay->Line = &sampleBuffer[(ptrdiff_t)Delay->Line];
}

// Calculate the length of a delay line and store its mask and offset.
static ALuint CalcLineLength(ALfloat length, ptrdiff_t offset, ALuint frequency, ALuint extra, DelayLine *Delay)
{
    ALuint samples;

    // All line lengths are powers of 2, calculated from their lengths, with
    // an additional sample in case of rounding errors.
    samples = fastf2u(length*frequency) + extra;
    samples = NextPowerOf2(samples + 1);
    // All lines share a single sample buffer.
    Delay->Mask = samples - 1;
    Delay->Line = (ALfloat*)offset;
    // Return the sample count for accumulation.
    return samples;
}


static int FindClosestPrime(int desired, ALboolean *used)
{
    ALsizei curidx = 0;
    ALsizei count = COUNTOF(PrimeTable)-1;
    /* First, a binary search to find the closest prime that's not less than
     * the desired value (lower_bound).
     */
    while(count > 0)
    {
        ALsizei step = count>>1;
        ALsizei i = curidx+step;
        if(!(PrimeTable[i] < desired))
            count = step;
        else
        {
            curidx = i+1;
            count -= step+1;
        }
    }
    /* If the next lesser prime is closer to the desired value, use it. */
    if(curidx > 0 && abs(PrimeTable[curidx-1]-desired) < abs(PrimeTable[curidx]-desired))
        curidx--;

#define GET_BIT(arr, b) (!!(arr[(b)>>4]&(1<<((b)&7))))
#define SET_BIT(arr, b) ((void)(arr[(b)>>4] |= (1<<((b)&7))))
    if(GET_BIT(used, curidx))
    {
        ALsizei off1=0, off2=0;
        /* If this prime is already used, find the next unused larger and next
         * unused smaller one.
         */
        while(off1 < curidx && GET_BIT(used, curidx-off1))
            off1++;
        while(off2 < 1024-curidx && GET_BIT(used, curidx+off2))
            off2++;

        /* Select the closest unused prime to the desired value. */
        if(GET_BIT(used, curidx-off1))
            curidx += off2;
        else if(GET_BIT(used, curidx+off2))
            curidx -= off1;
        else
            curidx = (abs(PrimeTable[curidx-off1]-desired) <
                      abs(PrimeTable[curidx+off2]-desired)) ? (curidx-off1) : (curidx+off2);
    }
    /* Mark this prime as used. */
    SET_BIT(used, curidx);
#undef SET_BIT
#undef GET_BIT

    return PrimeTable[curidx];
}

/* The lengths of the late reverb all-pass filter series are roughly calculated
 * as: 15ms / (3**idx), where idx is the filter index of the series. On top of
 * that, the filter lengths (in samples) should be prime numbers so they don't
 * share any common factors.
 *
 * To accomplish this, a lookup table is used to search among the first 1024
 * primes, along with a packed bit table to mark used primes, which should be
 * enough to handle any reasonable sample rate.
 *
 * NOTE: The returned length is in *samples*, not seconds!
 */
static ALfloat CalcAllpassLength(ALuint idx, ALuint frequency, ALboolean *used)
{
    ALfloat samples = frequency*0.015f / powf(3.0f, (ALfloat)idx);

    return FindClosestPrime((int)floorf(samples + 0.5f), used);
}

/* Calculates the delay line metrics and allocates the shared sample buffer
 * for all lines given the sample rate (frequency).  If an allocation failure
 * occurs, it returns AL_FALSE.
 */
static ALboolean AllocLines(ALuint frequency, ALreverbState *State)
{
    ALboolean used_primes[COUNTOF(PrimeTable)>>4] = { 0 };
    ALuint totalSamples, index;
    ALfloat length;

    // All delay line lengths are calculated to accomodate the full range of
    // lengths given their respective paramters.
    totalSamples = 0;

    /* The modulator's line length is calculated from the maximum modulation
     * time and depth coefficient, and halfed for the low-to-high frequency
     * swing.  An additional sample is added to keep it stable when there is no
     * modulation.
     */
    length = (AL_EAXREVERB_MAX_MODULATION_TIME*MODULATION_DEPTH_COEFF/2.0f);
    for(index = 0;index < 4;index++)
        totalSamples += CalcLineLength(length, totalSamples, frequency, 1,
                                       &State->Mod.Delay[index]);

    /* The initial delay is the sum of the reflections and late reverb delays.
     * The decorrelator length is calculated from the lowest reverb density (a
     * parameter value of 1). This must include space for storing a loop
     * update.
     */
    length = AL_EAXREVERB_MAX_REFLECTIONS_DELAY +
             AL_EAXREVERB_MAX_LATE_REVERB_DELAY;
    length += (DECO_FRACTION * DECO_MULTIPLIER * DECO_MULTIPLIER) *
              LATE_LINE_LENGTH[0] * (1.0f + LATE_LINE_MULTIPLIER);
    /* Multiply length by 4, since we're storing 4 interleaved channels in the
     * main delay line.
     */
    totalSamples += CalcLineLength(length*4, totalSamples, frequency,
                                   MAX_UPDATE_SAMPLES*4, &State->Delay);

    // The early reflection lines.
    for(index = 0;index < 4;index++)
        totalSamples += CalcLineLength(EARLY_LINE_LENGTH[index], totalSamples,
                                       frequency, 0, &State->Early.Delay[index]);

    // The late delay lines are calculated from the lowest reverb density.
    for(index = 0;index < 4;index++)
    {
        length = LATE_LINE_LENGTH[index] * (1.0f + LATE_LINE_MULTIPLIER);
        totalSamples += CalcLineLength(length, totalSamples, frequency, 0,
                                       &State->Late.Delay[index]);
    }

    // The late all-pass lines.
    for(index = 0;index < 4;index++)
    {
        ALuint k;

        length = 0.0f;
        for(k = 0;k < 3;k++)
            length += CalcAllpassLength(k, frequency, used_primes);
        /* NOTE: Since 'length' is already the number of samples for the all-
         * pass series, pass a sample rate of 1 so the sample length remains
         * correct.
         */
        totalSamples += CalcLineLength(length, totalSamples, 1, 1,
                                       &State->Late.Ap[index].Delay);
    }

    // The echo all-pass and delay lines.
    for(index = 0;index < 4;index++)
    {
        totalSamples += CalcLineLength(ECHO_ALLPASS_LENGTH, totalSamples,
                                       frequency, 0, &State->Echo.Delay[index].Ap);
        totalSamples += CalcLineLength(AL_EAXREVERB_MAX_ECHO_TIME, totalSamples,
                                       frequency, 0, &State->Echo.Delay[index].Feedback);
    }

    if(totalSamples != State->TotalSamples)
    {
        ALfloat *newBuffer;

        TRACE("New reverb buffer length: %u samples\n", totalSamples);
        newBuffer = al_calloc(16, sizeof(ALfloat) * totalSamples);
        if(!newBuffer) return AL_FALSE;

        al_free(State->SampleBuffer);
        State->SampleBuffer = newBuffer;
        State->TotalSamples = totalSamples;
    }

    // Update all delays to reflect the new sample buffer.
    RealizeLineOffset(State->SampleBuffer, &State->Delay);
    for(index = 0;index < 4;index++)
    {
        RealizeLineOffset(State->SampleBuffer, &State->Mod.Delay[index]);

        RealizeLineOffset(State->SampleBuffer, &State->Early.Delay[index]);

        RealizeLineOffset(State->SampleBuffer, &State->Late.Ap[index].Delay);
        RealizeLineOffset(State->SampleBuffer, &State->Late.Delay[index]);

        RealizeLineOffset(State->SampleBuffer, &State->Echo.Delay[index].Ap);
        RealizeLineOffset(State->SampleBuffer, &State->Echo.Delay[index].Feedback);
    }

    // Clear the sample buffer.
    for(index = 0;index < State->TotalSamples;index++)
        State->SampleBuffer[index] = 0.0f;

    return AL_TRUE;
}

static ALboolean ALreverbState_deviceUpdate(ALreverbState *State, ALCdevice *Device)
{
    ALboolean used_primes[COUNTOF(PrimeTable)>>4] = { 0 };
    ALuint frequency = Device->Frequency, index;

    // Allocate the delay lines.
    if(!AllocLines(frequency, State))
        return AL_FALSE;

    // Calculate the modulation filter coefficient.  Notice that the exponent
    // is calculated given the current sample rate.  This ensures that the
    // resulting filter response over time is consistent across all sample
    // rates.
    State->Mod.Coeff = powf(MODULATION_FILTER_COEFF,
                            MODULATION_FILTER_CONST / frequency);

    // The early reflection and late all-pass filter line lengths are static,
    // so their offsets only need to be calculated once.
    for(index = 0;index < 4;index++)
    {
        ALuint k;

        State->Early.Offset[index] = fastf2u(EARLY_LINE_LENGTH[index] * frequency);
        for(k = 0;k < 3;k++)
            State->Late.Ap[index].Offsets[k] = (ALuint)CalcAllpassLength(
                k, frequency, used_primes
            );
        State->Late.Ap[index].Offsets[1] += State->Late.Ap[index].Offsets[0];
        State->Late.Ap[index].Offsets[2] += State->Late.Ap[index].Offsets[1];
        TRACE("Late all-pass %u: %u %u (%+d) %u (%+d)\n", index,
            State->Late.Ap[index].Offsets[0], State->Late.Ap[index].Offsets[1],
            (State->Late.Ap[index].Offsets[1] - State->Late.Ap[index].Offsets[0]),
            State->Late.Ap[index].Offsets[2],
            (State->Late.Ap[index].Offsets[2] - State->Late.Ap[index].Offsets[1])
        );
    }

    // The echo all-pass filter line length is static, so its offset only
    // needs to be calculated once.
    State->Echo.ApOffset = fastf2u(ECHO_ALLPASS_LENGTH * frequency);

    return AL_TRUE;
}

/**************************************
 *  Effect Update                     *
 **************************************/

// Calculate a decay coefficient given the length of each cycle and the time
// until the decay reaches -60 dB.
static inline ALfloat CalcDecayCoeff(ALfloat length, ALfloat decayTime)
{
    return powf(0.001f/*-60 dB*/, length/decayTime);
}

// Calculate a decay length from a coefficient and the time until the decay
// reaches -60 dB.
static inline ALfloat CalcDecayLength(ALfloat coeff, ALfloat decayTime)
{
    return log10f(coeff) * decayTime / log10f(0.001f)/*-60 dB*/;
}

// Calculate an attenuation to be applied to the input of any echo models to
// compensate for modal density and decay time.
static inline ALfloat CalcDensityGain(ALfloat a)
{
    /* The energy of a signal can be obtained by finding the area under the
     * squared signal.  This takes the form of Sum(x_n^2), where x is the
     * amplitude for the sample n.
     *
     * Decaying feedback matches exponential decay of the form Sum(a^n),
     * where a is the attenuation coefficient, and n is the sample.  The area
     * under this decay curve can be calculated as:  1 / (1 - a).
     *
     * Modifying the above equation to find the squared area under the curve
     * (for energy) yields:  1 / (1 - a^2).  Input attenuation can then be
     * calculated by inverting the square root of this approximation,
     * yielding:  1 / sqrt(1 / (1 - a^2)), simplified to: sqrt(1 - a^2).
     */
    return sqrtf(1.0f - (a * a));
}

// Calculate the mixing matrix coefficients given a diffusion factor.
static inline ALvoid CalcMatrixCoeffs(ALfloat diffusion, ALfloat *x, ALfloat *y)
{
    ALfloat n, t;

    // The matrix is of order 4, so n is sqrt (4 - 1).
    n = sqrtf(3.0f);
    t = diffusion * atanf(n);

    // Calculate the first mixing matrix coefficient.
    *x = cosf(t);
    // Calculate the second mixing matrix coefficient.
    *y = sinf(t) / n;
}

// Calculate the limited HF ratio for use with the late reverb low-pass
// filters.
static ALfloat CalcLimitedHfRatio(ALfloat hfRatio, ALfloat airAbsorptionGainHF, ALfloat decayTime)
{
    ALfloat limitRatio;

    /* Find the attenuation due to air absorption in dB (converting delay
     * time to meters using the speed of sound).  Then reversing the decay
     * equation, solve for HF ratio.  The delay length is cancelled out of
     * the equation, so it can be calculated once for all lines.
     */
    limitRatio = 1.0f / (CalcDecayLength(airAbsorptionGainHF, decayTime) *
                         SPEEDOFSOUNDMETRESPERSEC);
    /* Using the limit calculated above, apply the upper bound to the HF
     * ratio. Also need to limit the result to a minimum of 0.1, just like the
     * HF ratio parameter. */
    return clampf(limitRatio, 0.1f, hfRatio);
}

// Calculate the coefficient for a HF (and eventually LF) decay damping
// filter.
static inline ALfloat CalcDampingCoeff(ALfloat hfRatio, ALfloat length, ALfloat decayTime, ALfloat decayCoeff, ALfloat cw)
{
    ALfloat coeff, g;

    // Eventually this should boost the high frequencies when the ratio
    // exceeds 1.
    coeff = 0.0f;
    if (hfRatio < 1.0f)
    {
        // Calculate the low-pass coefficient by dividing the HF decay
        // coefficient by the full decay coefficient.
        g = CalcDecayCoeff(length, decayTime * hfRatio) / decayCoeff;

        // Damping is done with a 1-pole filter, so g needs to be squared.
        g *= g;
        if(g < 0.9999f) /* 1-epsilon */
        {
            /* Be careful with gains < 0.001, as that causes the coefficient
             * head towards 1, which will flatten the signal. */
            g = maxf(g, 0.001f);
            coeff = (1 - g*cw - sqrtf(2*g*(1-cw) - g*g*(1 - cw*cw))) /
                    (1 - g);
        }

        // Very low decay times will produce minimal output, so apply an
        // upper bound to the coefficient.
        coeff = minf(coeff, 0.98f);
    }
    return coeff;
}

// Update the EAX modulation index, range, and depth.  Keep in mind that this
// kind of vibrato is additive and not multiplicative as one may expect.  The
// downswing will sound stronger than the upswing.
static ALvoid UpdateModulator(ALfloat modTime, ALfloat modDepth, ALuint frequency, ALreverbState *State)
{
    ALuint range;

    /* Modulation is calculated in two parts.
     *
     * The modulation time effects the sinus applied to the change in
     * frequency.  An index out of the current time range (both in samples)
     * is incremented each sample.  The range is bound to a reasonable
     * minimum (1 sample) and when the timing changes, the index is rescaled
     * to the new range (to keep the sinus consistent).
     */
    range = maxu(fastf2u(modTime*frequency), 1);
    State->Mod.Index = (ALuint)(State->Mod.Index * (ALuint64)range /
                                State->Mod.Range);
    State->Mod.Range = range;

    /* The modulation depth effects the amount of frequency change over the
     * range of the sinus.  It needs to be scaled by the modulation time so
     * that a given depth produces a consistent change in frequency over all
     * ranges of time.  Since the depth is applied to a sinus value, it needs
     * to be halfed once for the sinus range and again for the sinus swing
     * in time (half of it is spent decreasing the frequency, half is spent
     * increasing it).
     */
    State->Mod.Depth = modDepth * MODULATION_DEPTH_COEFF * modTime / 2.0f /
                       2.0f * frequency;
}

// Update the offsets for the main effect delay line.
static ALvoid UpdateDelayLine(ALfloat earlyDelay, ALfloat lateDelay, ALfloat density, ALuint frequency, ALreverbState *State)
{
    ALfloat length;
    ALuint i;

    /* The early reflections and late reverb inputs are decorrelated to provide
     * time-varying reflections, smooth out the reverb tail, and reduce harsh
     * echoes. The first tap occurs immediately, while the remaining taps are
     * delayed by multiples of a fraction of the smallest delay time.
     *
     * offset[index] = (FRACTION (MULTIPLIER^(index-1))) smallest_delay
     *
     * for index = 1...max_lines
     */
    State->EarlyDelayTap[0] = fastf2u(earlyDelay*frequency + 0.5f);
    for(i = 1;i < 4;i++)
    {
        length = (DECO_FRACTION * powf(DECO_MULTIPLIER, (ALfloat)i-1.0f)) *
                 EARLY_LINE_LENGTH[0];
        State->EarlyDelayTap[i] = fastf2u(length*frequency + 0.5f) + State->EarlyDelayTap[0];
    }

    State->LateDelayTap[0] = fastf2u((earlyDelay + lateDelay)*frequency + 0.5f);
    for(i = 1;i < 4;i++)
    {
        length = (DECO_FRACTION * powf(DECO_MULTIPLIER, (ALfloat)i-1.0f)) *
                 LATE_LINE_LENGTH[0] * (1.0f + (density * LATE_LINE_MULTIPLIER));
        State->LateDelayTap[i] = fastf2u(length*frequency + 0.5f) + State->LateDelayTap[0];
    }
}

// Update the late reverb mix, line lengths, and line coefficients.
static ALvoid UpdateLateLines(ALfloat xMix, ALfloat density, ALfloat decayTime, ALfloat diffusion, ALfloat echoDepth, ALfloat hfRatio, ALfloat cw, ALuint frequency, ALreverbState *State)
{
    ALfloat length;
    ALsizei i;

    /* Calculate the late reverb gain. Since the output is tapped prior to the
     * application of the next delay line coefficients, the output needs to be
     * attenuated by the 'x' mixing matrix coefficient.  Also attenuate the
     * late reverb when echo depth is high and diffusion is low, so the echo is
     * slightly stronger than the decorrelated echos in the reverb tail.
     */
    State->Late.Gain = xMix * (1.0f - (echoDepth*0.5f*(1.0f - diffusion)));

    /* To compensate for changes in modal density and decay time of the late
     * reverb signal, the input is attenuated based on the maximal energy of
     * the outgoing signal.  This approximation is used to keep the apparent
     * energy of the signal equal for all ranges of density and decay time.
     *
     * The average length of the cyclcical delay lines is used to calculate
     * the attenuation coefficient.
     */
    length = (LATE_LINE_LENGTH[0] + LATE_LINE_LENGTH[1] +
              LATE_LINE_LENGTH[2] + LATE_LINE_LENGTH[3]) / 4.0f;
    length *= 1.0f + (density * LATE_LINE_MULTIPLIER);
    State->Late.DensityGain = CalcDensityGain(
        CalcDecayCoeff(length, decayTime)
    );

    // Calculate the all-pass feed-back and feed-forward coefficient.
    State->Late.ApFeedCoeff = sqrtf(0.5f) * powf(diffusion, 2.0f);

    for(i = 0;i < 4;i++)
    {
        // Calculate the length (in seconds) of each delay line.
        length = LATE_LINE_LENGTH[i] * (1.0f + (density*LATE_LINE_MULTIPLIER));

        // Calculate the delay offset for each delay line.
        State->Late.Offset[i] = fastf2u(length * frequency);

        // Calculate the gain (coefficient) for each line.
        State->Late.Coeff[i] = CalcDecayCoeff(length, decayTime);

        // Calculate the damping coefficient for each low-pass filter.
        State->Late.Lp[i].Coeff = CalcDampingCoeff(
            hfRatio, length, decayTime, State->Late.Coeff[i], cw
        );

        // Attenuate the line coefficients by the mixing coefficient (x).
        State->Late.Coeff[i] *= xMix;
    }
}

// Update the echo gain, line offset, line coefficients, and mixing
// coefficients.
static ALvoid UpdateEchoLine(ALfloat echoTime, ALfloat decayTime, ALfloat diffusion, ALfloat echoDepth, ALfloat hfRatio, ALfloat cw, ALuint frequency, ALreverbState *State)
{
    // Update the offset and coefficient for the echo delay line.
    State->Echo.Offset = fastf2u(echoTime * frequency);

    // Calculate the decay coefficient for the echo line.
    State->Echo.Coeff = CalcDecayCoeff(echoTime, decayTime);

    // Calculate the energy-based attenuation coefficient for the echo delay
    // line.
    State->Echo.DensityGain = CalcDensityGain(State->Echo.Coeff);

    // Calculate the echo all-pass feed coefficient.
    State->Echo.ApFeedCoeff = sqrtf(0.5f) * powf(diffusion, 2.0f);

    // Calculate the damping coefficient for each low-pass filter.
    State->Echo.LpCoeff = CalcDampingCoeff(hfRatio, echoTime, decayTime,
                                           State->Echo.Coeff, cw);

    /* Calculate the echo mixing coefficient. This is applied to the output mix
     * only, not the feedback.
     */
    State->Echo.MixCoeff = echoDepth;
}

/* Creates a transform matrix given a reverb vector. This works by creating a
 * Z-focus transform, then a rotate transform around X, then Y, to place the
 * focal point in the direction of the vector, using the vector length as a
 * focus strength.
 *
 * This isn't technically correct since the vector is supposed to define the
 * aperture and not rotate the perceived soundfield, but in practice it's
 * probably good enough.
 */
static aluMatrixf GetTransformFromVector(const ALfloat *vec)
{
    aluMatrixf zfocus, xrot, yrot;
    aluMatrixf tmp1, tmp2;
    ALfloat length;
    ALfloat sa, a;

    length = sqrtf(vec[0]*vec[0] + vec[1]*vec[1] + vec[2]*vec[2]);

    /* Define a Z-focus (X in Ambisonics) transform, given the panning vector
     * length.
     */
    sa = sinf(minf(length, 1.0f) * (F_PI/4.0f));
    aluMatrixfSet(&zfocus,
                     1.0f/(1.0f+sa),                       0.0f,                       0.0f, (sa/(1.0f+sa))/1.732050808f,
                               0.0f, sqrtf((1.0f-sa)/(1.0f+sa)),                       0.0f,                        0.0f,
                               0.0f,                       0.0f, sqrtf((1.0f-sa)/(1.0f+sa)),                        0.0f,
        (sa/(1.0f+sa))*1.732050808f,                       0.0f,                       0.0f,              1.0f/(1.0f+sa)
    );

    /* Define rotation around X (Y in Ambisonics) */
    a = atan2f(vec[1], sqrtf(vec[0]*vec[0] + vec[2]*vec[2]));
    aluMatrixfSet(&xrot,
        1.0f, 0.0f,     0.0f,    0.0f,
        0.0f, 1.0f,     0.0f,    0.0f,
        0.0f, 0.0f,  cosf(a), sinf(a),
        0.0f, 0.0f, -sinf(a), cosf(a)
    );

    /* Define rotation around Y (Z in Ambisonics). NOTE: EFX's reverb vectors
     * use a right-handled coordinate system, compared to the rest of OpenAL
     * which uses left-handed. This is fixed by negating Z, however it would
     * need to also be negated to get a proper Ambisonics angle, thus
     * cancelling it out.
     */
    a = atan2f(-vec[0], vec[2]);
    aluMatrixfSet(&yrot,
        1.0f,     0.0f, 0.0f,    0.0f,
        0.0f,  cosf(a), 0.0f, sinf(a),
        0.0f,     0.0f, 1.0f,    0.0f,
        0.0f, -sinf(a), 0.0f, cosf(a)
    );

#define MATRIX_MULT(_res, _m1, _m2) do {                                      \
    int row, col;                                                             \
    for(col = 0;col < 4;col++)                                                \
    {                                                                         \
        for(row = 0;row < 4;row++)                                            \
            _res.m[row][col] = _m1.m[row][0]*_m2.m[0][col] + _m1.m[row][1]*_m2.m[1][col] + \
                               _m1.m[row][2]*_m2.m[2][col] + _m1.m[row][3]*_m2.m[3][col];  \
    }                                                                         \
} while(0)
    /* Define a matrix that first focuses on Z, then rotates around X then Y to
     * focus the output in the direction of the vector.
     */
    MATRIX_MULT(tmp1, xrot, zfocus);
    MATRIX_MULT(tmp2, yrot, tmp1);
#undef MATRIX_MULT

    return tmp2;
}

// Update the early and late 3D panning gains.
static ALvoid Update3DPanning(const ALCdevice *Device, const ALfloat *ReflectionsPan, const ALfloat *LateReverbPan, ALfloat Gain, ALfloat EarlyGain, ALfloat LateGain, ALreverbState *State)
{
    /* Converts early reflections A-Format to B-Format (transposed). */
    static const aluMatrixf EarlyA2B = {{
        { 0.8660254038f,  0.8660254038f,  0.8660254038f,  0.8660254038f },
        { 0.8660254038f,  0.8660254038f, -0.8660254038f, -0.8660254038f },
        { 0.8660254038f, -0.8660254038f,  0.8660254038f, -0.8660254038f },
        { 0.8660254038f, -0.8660254038f, -0.8660254038f,  0.8660254038f }
    }};
    /* Converts late reverb A-Format to B-Format (transposed). */
    static const aluMatrixf LateA2B = {{
        { 0.8660254038f,  1.2247448714f,           0.0f,  0.8660254038f },
        { 0.8660254038f,           0.0f, -1.2247448714f, -0.8660254038f },
        { 0.8660254038f,           0.0f,  1.2247448714f, -0.8660254038f },
        { 0.8660254038f, -1.2247448714f,           0.0f,  0.8660254038f }
    }};
    aluMatrixf transform, rot;
    ALsizei i;

    STATIC_CAST(ALeffectState,State)->OutBuffer = Device->FOAOut.Buffer;
    STATIC_CAST(ALeffectState,State)->OutChannels = Device->FOAOut.NumChannels;

    /* Note: Both _m2 and _res are transposed. */
#define MATRIX_MULT(_res, _m1, _m2) do {                                      \
    int row, col;                                                             \
    for(col = 0;col < 4;col++)                                                \
    {                                                                         \
        for(row = 0;row < 4;row++)                                            \
            _res.m[col][row] = _m1.m[row][0]*_m2.m[col][0] + _m1.m[row][1]*_m2.m[col][1] + \
                               _m1.m[row][2]*_m2.m[col][2] + _m1.m[row][3]*_m2.m[col][3];  \
    }                                                                         \
} while(0)
    /* Create a matrix that first converts A-Format to B-Format, then rotates
     * the B-Format soundfield according to the panning vector.
     */
    rot = GetTransformFromVector(ReflectionsPan);
    MATRIX_MULT(transform, rot, EarlyA2B);
    memset(&State->Early.PanGain, 0, sizeof(State->Early.PanGain));
    for(i = 0;i < MAX_EFFECT_CHANNELS;i++)
        ComputeFirstOrderGains(Device->FOAOut, transform.m[i], Gain*EarlyGain, State->Early.PanGain[i]);

    rot = GetTransformFromVector(LateReverbPan);
    MATRIX_MULT(transform, rot, LateA2B);
    memset(&State->Late.PanGain, 0, sizeof(State->Late.PanGain));
    for(i = 0;i < MAX_EFFECT_CHANNELS;i++)
        ComputeFirstOrderGains(Device->FOAOut, transform.m[i], Gain*LateGain, State->Late.PanGain[i]);
#undef MATRIX_MULT
}

static ALvoid ALreverbState_update(ALreverbState *State, const ALCdevice *Device, const ALeffectslot *Slot, const ALeffectProps *props)
{
    ALuint frequency = Device->Frequency;
    ALfloat lfscale, hfscale, hfRatio;
    ALfloat gain, gainlf, gainhf;
    ALfloat cw, x, y;
    ALsizei i;

    if(Slot->Params.EffectType == AL_EFFECT_EAXREVERB && !EmulateEAXReverb)
        State->IsEax = AL_TRUE;
    else if(Slot->Params.EffectType == AL_EFFECT_REVERB || EmulateEAXReverb)
        State->IsEax = AL_FALSE;

    // Calculate the master filters
    hfscale = props->Reverb.HFReference / frequency;
    gainhf = maxf(props->Reverb.GainHF, 0.0625f); /* Limit -24dB */
    ALfilterState_setParams(&State->Filter[0].Lp, ALfilterType_HighShelf,
                            gainhf, hfscale, calc_rcpQ_from_slope(gainhf, 0.75f));
    lfscale = props->Reverb.LFReference / frequency;
    gainlf = maxf(props->Reverb.GainLF, 0.0625f);
    ALfilterState_setParams(&State->Filter[0].Hp, ALfilterType_LowShelf,
                            gainlf, lfscale, calc_rcpQ_from_slope(gainlf, 0.75f));
    for(i = 1;i < 4;i++)
    {
        State->Filter[i].Lp.b0 = State->Filter[0].Lp.b0;
        State->Filter[i].Lp.b1 = State->Filter[0].Lp.b1;
        State->Filter[i].Lp.b2 = State->Filter[0].Lp.b2;
        State->Filter[i].Lp.a1 = State->Filter[0].Lp.a1;
        State->Filter[i].Lp.a2 = State->Filter[0].Lp.a2;

        State->Filter[i].Hp.b0 = State->Filter[0].Hp.b0;
        State->Filter[i].Hp.b1 = State->Filter[0].Hp.b1;
        State->Filter[i].Hp.b2 = State->Filter[0].Hp.b2;
        State->Filter[i].Hp.a1 = State->Filter[0].Hp.a1;
        State->Filter[i].Hp.a2 = State->Filter[0].Hp.a2;
    }

    // Update the modulator line.
    UpdateModulator(props->Reverb.ModulationTime, props->Reverb.ModulationDepth,
                    frequency, State);

    // Update the main effect delay.
    UpdateDelayLine(props->Reverb.ReflectionsDelay, props->Reverb.LateReverbDelay,
                    props->Reverb.Density, frequency, State);

    // Get the mixing matrix coefficients (x and y).
    CalcMatrixCoeffs(props->Reverb.Diffusion, &x, &y);
    // Then divide x into y to simplify the matrix calculation.
    State->Late.MixCoeff = y / x;

    // If the HF limit parameter is flagged, calculate an appropriate limit
    // based on the air absorption parameter.
    hfRatio = props->Reverb.DecayHFRatio;
    if(props->Reverb.DecayHFLimit && props->Reverb.AirAbsorptionGainHF < 1.0f)
        hfRatio = CalcLimitedHfRatio(hfRatio, props->Reverb.AirAbsorptionGainHF,
                                     props->Reverb.DecayTime);

    cw = cosf(F_TAU * hfscale);
    // Update the late lines.
    UpdateLateLines(x, props->Reverb.Density, props->Reverb.DecayTime,
                    props->Reverb.Diffusion, props->Reverb.EchoDepth,
                    hfRatio, cw, frequency, State);

    // Update the echo line.
    UpdateEchoLine(props->Reverb.EchoTime, props->Reverb.DecayTime,
                   props->Reverb.Diffusion, props->Reverb.EchoDepth,
                   hfRatio, cw, frequency, State);

    gain = props->Reverb.Gain * Slot->Params.Gain * ReverbBoost;
    // Update early and late 3D panning.
    Update3DPanning(Device, props->Reverb.ReflectionsPan,
                    props->Reverb.LateReverbPan, gain,
                    props->Reverb.ReflectionsGain,
                    props->Reverb.LateReverbGain, State);
}


/**************************************
 *  Effect Processing                 *
 **************************************/

// Basic delay line input/output routines.
static inline ALfloat DelayLineOut(DelayLine *Delay, ALsizei offset)
{
    return Delay->Line[offset&Delay->Mask];
}

static inline ALvoid DelayLineIn(DelayLine *Delay, ALsizei offset, ALfloat in)
{
    Delay->Line[offset&Delay->Mask] = in;
}

static inline ALfloat DelayLineInOut(DelayLine *Delay, ALsizei offset, ALsizei outoffset, ALfloat in)
{
    Delay->Line[offset&Delay->Mask] = in;
    return Delay->Line[(offset-outoffset)&Delay->Mask];
}

static void CalcModulationDelays(ALreverbState *State, ALfloat *restrict delays, ALsizei todo)
{
    ALfloat sinus, range;
    ALsizei index, i;

    index = State->Mod.Index;
    range = State->Mod.Filter;
    for(i = 0;i < todo;i++)
    {
        /* Calculate the sinus rythm (dependent on modulation time and the
         * sampling rate).  The center of the sinus is moved to reduce the
         * delay of the effect when the time or depth are low.
         */
        sinus = 1.0f - cosf(F_TAU * index / State->Mod.Range);

        /* Step the modulation index forward, keeping it bound to its range. */
        index = (index+1) % State->Mod.Range;

        /* The depth determines the range over which to read the input samples
         * from, so it must be filtered to reduce the distortion caused by even
         * small parameter changes.
         */
        range = lerp(range, State->Mod.Depth, State->Mod.Coeff);

        /* Calculate the read offset with fraction. */
        delays[i] = range*sinus;
    }
    State->Mod.Index = index;
    State->Mod.Filter = range;
}

// Given some input samples, this function produces modulation for the late
// reverb.
static void EAXModulation(DelayLine *ModDelay, ALsizei offset, const ALfloat *restrict delays, ALfloat*restrict dst, const ALfloat*restrict src, ALsizei todo)
{
    ALfloat frac, fdelay;
    ALfloat out0, out1;
    ALsizei delay, i;

    for(i = 0;i < todo;i++)
    {
        /* Separate the integer offset and fraction between it and the next
         * sample.
         */
        frac = modff(delays[i], &fdelay);
        delay = fastf2u(fdelay);

        /* Add the incoming sample to the delay line, and get the two samples
         * crossed by the offset delay.
         */
        out0 = DelayLineInOut(ModDelay, offset, delay, src[i]);
        out1 = DelayLineOut(ModDelay, offset - delay - 1);
        offset++;

        /* The output is obtained by linearly interpolating the two samples
         * that were acquired above.
         */
        dst[i] = lerp(out0, out1, frac);
    }
}

/* Given some input samples from the main delay line, this function produces
 * four-channel outputs for the early reflections.
 */
static ALvoid EarlyReflection(ALreverbState *State, ALsizei todo, ALfloat (*restrict out)[MAX_UPDATE_SAMPLES])
{
    ALsizei offset = State->Offset;
    ALfloat d[4], v, f[4];
    ALsizei i;

    for(i = 0;i < todo;i++)
    {
        /* Obtain the first reflection samples from the main delay line. */
        f[0] = DelayLineOut(&State->Delay, (offset-State->EarlyDelayTap[0])*4 + 0);
        f[1] = DelayLineOut(&State->Delay, (offset-State->EarlyDelayTap[1])*4 + 1);
        f[2] = DelayLineOut(&State->Delay, (offset-State->EarlyDelayTap[2])*4 + 2);
        f[3] = DelayLineOut(&State->Delay, (offset-State->EarlyDelayTap[3])*4 + 3);

        /* The following is a Householder matrix that was derived from a
         * lossless scattering junction from waveguide theory.  In this case,
         * it's maximally diffuse scattering is used without feedback.
         *
         *          N
         *         ---
         *         \
         * v = 2/N /   d_i
         *         ---
         *         i=1
         */
        v = (f[0] + f[1] + f[2] + f[3]) * 0.5f;

        /* Calculate the values to pass through the delay lines. */
        d[0] = v - f[0];
        d[1] = v - f[1];
        d[2] = v - f[2];
        d[3] = v - f[3];

        /* Store the post-junction results in the main delay line, helping
         * compensate for the late reverb starting with a low echo density.
         */
        DelayLineIn(&State->Delay, (offset-State->EarlyDelayTap[0])*4 + 0, d[0]);
        DelayLineIn(&State->Delay, (offset-State->EarlyDelayTap[1])*4 + 1, d[1]);
        DelayLineIn(&State->Delay, (offset-State->EarlyDelayTap[2])*4 + 2, d[2]);
        DelayLineIn(&State->Delay, (offset-State->EarlyDelayTap[3])*4 + 3, d[3]);

        /* Feed the early delay lines, and load the delayed results. */
        f[0] += DelayLineInOut(&State->Early.Delay[0], offset, State->Early.Offset[0], d[0]);
        f[1] += DelayLineInOut(&State->Early.Delay[1], offset, State->Early.Offset[1], d[1]);
        f[2] += DelayLineInOut(&State->Early.Delay[2], offset, State->Early.Offset[2], d[2]);
        f[3] += DelayLineInOut(&State->Early.Delay[3], offset, State->Early.Offset[3], d[3]);
        offset++;

        /* Output the initial reflection taps and the results of the delayed
         * junction for all four channels.
         */
        out[0][i] = f[0];
        out[1][i] = f[1];
        out[2][i] = f[2];
        out[3][i] = f[3];
    }
}

// Basic attenuated all-pass input/output routine.
static inline ALfloat AllpassInOut(DelayLine *Delay, ALsizei outOffset, ALsizei inOffset, ALfloat in, ALfloat feedCoeff)
{
    ALfloat out, feed;

    out = DelayLineOut(Delay, outOffset);
    feed = feedCoeff * in;
    DelayLineIn(Delay, inOffset, in + feedCoeff*(out - feed));

    return out - feed;
}

// All-pass series input/output routine for late reverb.
static inline ALfloat LateAllPassInOut(ALreverbState *State, ALsizei offset, ALsizei index, ALfloat sample)
{
    ALsizei inOffset;
    ALsizei i;

    inOffset = offset;
    for(i = 0;i < 3;i++)
    {
        ALuint outOffset = offset - State->Late.Ap[index].Offsets[i];
        sample = AllpassInOut(&State->Late.Ap[index].Delay,
            outOffset, inOffset, sample, State->Late.ApFeedCoeff
        );
        inOffset = outOffset;
    }

    return sample;
}

// Low-pass filter input/output routine for late reverb.
static inline ALfloat LateLowPassInOut(ALreverbState *State, ALsizei index, ALfloat in)
{
    in = lerp(in, State->Late.Lp[index].Sample, State->Late.Lp[index].Coeff);
    State->Late.Lp[index].Sample = in;
    return in;
}

/* Given decorrelated input samples from the main delay line, this function
 * produces four-channel output for the late reverb.
 */
static ALvoid LateReverb(ALreverbState *State, ALsizei todo, ALfloat (*restrict out)[MAX_UPDATE_SAMPLES])
{
    ALfloat d[4], f[4];
    ALsizei offset;
    ALsizei i, j;

    offset = State->Offset;
    for(i = 0;i < todo;i++)
    {
        /* Obtain four decorrelated input samples. */
        for(j = 0;j < 4;j++)
            f[j] = DelayLineOut(&State->Delay, (offset-State->LateDelayTap[j])*4 + j) *
                   State->Late.DensityGain;

        /* Add the decayed results of the delay lines. */
        for(j = 0;j < 4;j++)
            f[j] += DelayLineOut(&State->Late.Delay[j], offset-State->Late.Offset[j]) *
                    State->Late.Coeff[j];

        /* Apply a low-pass filter to simulate surface absorption. */
        for(j = 0;j < 4;j++)
            f[j] = LateLowPassInOut(State, j, f[j]);

        /* To help increase diffusion, run each line through three all-pass
         * filters. This is where the feedback cycles from line 0 to 3 to 1 to
         * 2 and back to 0.
         */
        d[0] = LateAllPassInOut(State, offset, 2, f[2]);
        d[1] = LateAllPassInOut(State, offset, 3, f[3]);
        d[2] = LateAllPassInOut(State, offset, 1, f[1]);
        d[3] = LateAllPassInOut(State, offset, 0, f[0]);

        /* Late reverb is done with a modified feed-back delay network (FDN)
         * topology.  Four input lines are each fed through their own all-pass
         * filters and then into the mixing matrix.  The four outputs of the
         * mixing matrix are then cycled back to the inputs.
         *
         * The mixing matrix used is a 4D skew-symmetric rotation matrix
         * derived using a single unitary rotational parameter:
         *
         *  [  d,  a,  b,  c ]          1 = a^2 + b^2 + c^2 + d^2
         *  [ -a,  d,  c, -b ]
         *  [ -b, -c,  d,  a ]
         *  [ -c,  b, -a,  d ]
         *
         * The rotation is constructed from the effect's diffusion parameter,
         * yielding: 1 = x^2 + 3 y^2; where a, b, and c are the coefficient y
         * with differing signs, and d is the coefficient x.  The matrix is
         * thus:
         *
         *  [  x,  y, -y,  y ]          n = sqrt(matrix_order - 1)
         *  [ -y,  x,  y,  y ]          t = diffusion_parameter * atan(n)
         *  [  y, -y,  x,  y ]          x = cos(t)
         *  [ -y, -y, -y,  x ]          y = sin(t) / n
         *
         * To reduce the number of multiplies, the x coefficient is applied
         * with the delay line coefficients. Thus only the y coefficient
         * is applied when mixing, and is modified to be: y / x.
         */
        f[0] = d[0] + (State->Late.MixCoeff * (         d[1] + -d[2] + d[3]));
        f[1] = d[1] + (State->Late.MixCoeff * (-d[0]         +  d[2] + d[3]));
        f[2] = d[2] + (State->Late.MixCoeff * ( d[0] + -d[1]         + d[3]));
        f[3] = d[3] + (State->Late.MixCoeff * (-d[0] + -d[1] + -d[2]       ));

        /* Re-feed the delay lines. */
        for(j = 0;j < 4;j++)
            DelayLineIn(&State->Late.Delay[j], offset, f[j]);
        offset++;

        /* Output the results of the matrix for all four channels, attenuated
         * by the late reverb gain (which is attenuated by the 'x' mix
         * coefficient).
         */
        for(j = 0;j < 4;j++)
            out[j][i] = f[j] * State->Late.Gain;
    }
}

/* This function reads from the main delay line's late reverb tap, and mixes a
 * continuous echo feedback into the four-channel late reverb output.
 */
static ALvoid EAXEcho(ALreverbState *State, ALsizei todo, ALfloat (*restrict late)[MAX_UPDATE_SAMPLES])
{
    ALfloat feed;
    ALsizei offset;
    ALsizei c, i;

    for(c = 0;c < 4;c++)
    {
        offset = State->Offset;
        for(i = 0;i < todo;i++)
        {
            // Get the attenuated echo feedback sample for output.
            feed = DelayLineOut(&State->Echo.Delay[c].Feedback, offset-State->Echo.Offset) *
                   State->Echo.Coeff;

            // Write the output.
            late[c][i] += State->Echo.MixCoeff * feed;

            // Mix the energy-attenuated input with the output and pass it through
            // the echo low-pass filter.
            feed += DelayLineOut(&State->Delay, (offset-State->LateDelayTap[0])*4 + c) *
                    State->Echo.DensityGain;
            feed = lerp(feed, State->Echo.LpSample[c], State->Echo.LpCoeff);
            State->Echo.LpSample[c] = feed;

            // Then the echo all-pass filter.
            feed = AllpassInOut(&State->Echo.Delay[c].Ap, offset-State->Echo.ApOffset,
                                offset, feed, State->Echo.ApFeedCoeff);

            // Feed the delay with the mixed and filtered sample.
            DelayLineIn(&State->Echo.Delay[c].Feedback, offset, feed);
            offset++;
        }
    }
}

// Perform the non-EAX reverb pass on a given input sample, resulting in
// four-channel output.
static ALvoid VerbPass(ALreverbState *State, ALsizei todo, ALfloat (*restrict input)[MAX_UPDATE_SAMPLES], ALfloat (*restrict early)[MAX_UPDATE_SAMPLES], ALfloat (*restrict late)[MAX_UPDATE_SAMPLES])
{
    ALsizei i, c;

    for(c = 0;c < 4;c++)
    {
        /* Low-pass filter the incoming samples (use the early buffer as temp
         * storage).
         */
        ALfilterState_process(&State->Filter[c].Lp, &early[0][0], input[c], todo);
        for(i = 0;i < todo;i++)
            DelayLineIn(&State->Delay, (State->Offset+i)*4 + c, early[0][i]);
    }

    // Calculate the early reflection from the first delay tap.
    EarlyReflection(State, todo, early);

    // Calculate the late reverb from the decorrelator taps.
    LateReverb(State, todo, late);

    // Step all delays forward one sample.
    State->Offset += todo;
}

// Perform the EAX reverb pass on a given input sample, resulting in four-
// channel output.
static ALvoid EAXVerbPass(ALreverbState *State, ALsizei todo, ALfloat (*restrict input)[MAX_UPDATE_SAMPLES], ALfloat (*restrict early)[MAX_UPDATE_SAMPLES], ALfloat (*restrict late)[MAX_UPDATE_SAMPLES])
{
    ALsizei i, c;

    /* Perform any modulation on the input (use the early and late buffers as
     * temp storage).
     */
    CalcModulationDelays(State, &late[0][0], todo);
    for(c = 0;c < 4;c++)
    {
        EAXModulation(&State->Mod.Delay[c], State->Offset, &late[0][0],
                      &early[0][0], input[c], todo);

        /* Band-pass the incoming samples */
        ALfilterState_process(&State->Filter[c].Lp, &early[1][0], &early[0][0], todo);
        ALfilterState_process(&State->Filter[c].Hp, &early[2][0], &early[1][0], todo);

        /* Feed the initial delay line. */
        for(i = 0;i < todo;i++)
            DelayLineIn(&State->Delay, (State->Offset+i)*4 + c, early[2][i]);
    }

    // Calculate the early reflection from the first delay tap.
    EarlyReflection(State, todo, early);

    // Calculate the late reverb from the decorrelator taps.
    LateReverb(State, todo, late);

    // Calculate and mix in any echo.
    EAXEcho(State, todo, late);

    // Step all delays forward.
    State->Offset += todo;
}


static ALvoid ALreverbState_processStandard(ALreverbState *State, ALuint SamplesToDo, const ALfloat (*restrict SamplesIn)[BUFFERSIZE], ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALuint NumChannels)
{
    static const aluMatrixf B2A = {{
        { 0.288675134595f,  0.288675134595f,  0.288675134595f,  0.288675134595f },
        { 0.288675134595f,  0.288675134595f, -0.288675134595f, -0.288675134595f },
        { 0.288675134595f, -0.288675134595f,  0.288675134595f, -0.288675134595f },
        { 0.288675134595f, -0.288675134595f, -0.288675134595f,  0.288675134595f }
    }};
    ALfloat (*restrict afmt)[MAX_UPDATE_SAMPLES] = State->AFormatSamples;
    ALfloat (*restrict early)[MAX_UPDATE_SAMPLES] = State->EarlySamples;
    ALfloat (*restrict late)[MAX_UPDATE_SAMPLES] = State->ReverbSamples;
    ALuint base, c;

    /* Process reverb for these samples. */
    for(base = 0;base < SamplesToDo;)
    {
        ALsizei todo = minu(SamplesToDo-base, MAX_UPDATE_SAMPLES);

        /* Convert B-Format to A-Format for processing. */
        memset(afmt, 0, sizeof(*afmt)*4);
        for(c = 0;c < 4;c++)
            MixRowSamples(afmt[c], B2A.m[c],
                SamplesIn, MAX_EFFECT_CHANNELS, base, todo
            );

        VerbPass(State, todo, afmt, early, late);

        /* Mix the A-Format results to output, implicitly converting back to
         * B-Format.
         */
        for(c = 0;c < 4;c++)
            MixSamples(early[c], NumChannels, SamplesOut,
                State->Early.CurrentGain[c], State->Early.PanGain[c],
                SamplesToDo-base, base, todo
            );
        for(c = 0;c < 4;c++)
            MixSamples(late[c], NumChannels, SamplesOut,
                State->Late.CurrentGain[c], State->Late.PanGain[c],
                SamplesToDo-base, base, todo
            );

        base += todo;
    }
}

static ALvoid ALreverbState_processEax(ALreverbState *State, ALuint SamplesToDo, const ALfloat (*restrict SamplesIn)[BUFFERSIZE], ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALuint NumChannels)
{
    static const aluMatrixf B2A = {{
        { 0.288675134595f,  0.288675134595f,  0.288675134595f,  0.288675134595f },
        { 0.288675134595f,  0.288675134595f, -0.288675134595f, -0.288675134595f },
        { 0.288675134595f, -0.288675134595f,  0.288675134595f, -0.288675134595f },
        { 0.288675134595f, -0.288675134595f, -0.288675134595f,  0.288675134595f }
    }};
    ALfloat (*restrict afmt)[MAX_UPDATE_SAMPLES] = State->AFormatSamples;
    ALfloat (*restrict early)[MAX_UPDATE_SAMPLES] = State->EarlySamples;
    ALfloat (*restrict late)[MAX_UPDATE_SAMPLES] = State->ReverbSamples;
    ALuint base, c;

    /* Process reverb for these samples. */
    for(base = 0;base < SamplesToDo;)
    {
        ALsizei todo = minu(SamplesToDo-base, MAX_UPDATE_SAMPLES);

        memset(afmt, 0, 4*MAX_UPDATE_SAMPLES*sizeof(float));
        for(c = 0;c < 4;c++)
            MixRowSamples(afmt[c], B2A.m[c],
                SamplesIn, MAX_EFFECT_CHANNELS, base, todo
            );

        EAXVerbPass(State, todo, afmt, early, late);

        for(c = 0;c < 4;c++)
            MixSamples(early[c], NumChannels, SamplesOut,
                State->Early.CurrentGain[c], State->Early.PanGain[c],
                SamplesToDo-base, base, todo
            );
        for(c = 0;c < 4;c++)
            MixSamples(late[c], NumChannels, SamplesOut,
                State->Late.CurrentGain[c], State->Late.PanGain[c],
                SamplesToDo-base, base, todo
            );

        base += todo;
    }
}

static ALvoid ALreverbState_process(ALreverbState *State, ALuint SamplesToDo, const ALfloat (*restrict SamplesIn)[BUFFERSIZE], ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALuint NumChannels)
{
    if(State->IsEax)
        ALreverbState_processEax(State, SamplesToDo, SamplesIn, SamplesOut, NumChannels);
    else
        ALreverbState_processStandard(State, SamplesToDo, SamplesIn, SamplesOut, NumChannels);
}


typedef struct ALreverbStateFactory {
    DERIVE_FROM_TYPE(ALeffectStateFactory);
} ALreverbStateFactory;

static ALeffectState *ALreverbStateFactory_create(ALreverbStateFactory* UNUSED(factory))
{
    ALreverbState *state;

    alcall_once(&mixfunc_inited, init_mixfunc);

    NEW_OBJ0(state, ALreverbState)();
    if(!state) return NULL;

    return STATIC_CAST(ALeffectState, state);
}

DEFINE_ALEFFECTSTATEFACTORY_VTABLE(ALreverbStateFactory);

ALeffectStateFactory *ALreverbStateFactory_getFactory(void)
{
    static ALreverbStateFactory ReverbFactory = { { GET_VTABLE2(ALreverbStateFactory, ALeffectStateFactory) } };

    return STATIC_CAST(ALeffectStateFactory, &ReverbFactory);
}


void ALeaxreverb_setParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_EAXREVERB_DECAY_HFLIMIT:
            if(!(val >= AL_EAXREVERB_MIN_DECAY_HFLIMIT && val <= AL_EAXREVERB_MAX_DECAY_HFLIMIT))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.DecayHFLimit = val;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALeaxreverb_setParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{
    ALeaxreverb_setParami(effect, context, param, vals[0]);
}
void ALeaxreverb_setParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_EAXREVERB_DENSITY:
            if(!(val >= AL_EAXREVERB_MIN_DENSITY && val <= AL_EAXREVERB_MAX_DENSITY))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.Density = val;
            break;

        case AL_EAXREVERB_DIFFUSION:
            if(!(val >= AL_EAXREVERB_MIN_DIFFUSION && val <= AL_EAXREVERB_MAX_DIFFUSION))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.Diffusion = val;
            break;

        case AL_EAXREVERB_GAIN:
            if(!(val >= AL_EAXREVERB_MIN_GAIN && val <= AL_EAXREVERB_MAX_GAIN))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.Gain = val;
            break;

        case AL_EAXREVERB_GAINHF:
            if(!(val >= AL_EAXREVERB_MIN_GAINHF && val <= AL_EAXREVERB_MAX_GAINHF))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.GainHF = val;
            break;

        case AL_EAXREVERB_GAINLF:
            if(!(val >= AL_EAXREVERB_MIN_GAINLF && val <= AL_EAXREVERB_MAX_GAINLF))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.GainLF = val;
            break;

        case AL_EAXREVERB_DECAY_TIME:
            if(!(val >= AL_EAXREVERB_MIN_DECAY_TIME && val <= AL_EAXREVERB_MAX_DECAY_TIME))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.DecayTime = val;
            break;

        case AL_EAXREVERB_DECAY_HFRATIO:
            if(!(val >= AL_EAXREVERB_MIN_DECAY_HFRATIO && val <= AL_EAXREVERB_MAX_DECAY_HFRATIO))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.DecayHFRatio = val;
            break;

        case AL_EAXREVERB_DECAY_LFRATIO:
            if(!(val >= AL_EAXREVERB_MIN_DECAY_LFRATIO && val <= AL_EAXREVERB_MAX_DECAY_LFRATIO))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.DecayLFRatio = val;
            break;

        case AL_EAXREVERB_REFLECTIONS_GAIN:
            if(!(val >= AL_EAXREVERB_MIN_REFLECTIONS_GAIN && val <= AL_EAXREVERB_MAX_REFLECTIONS_GAIN))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.ReflectionsGain = val;
            break;

        case AL_EAXREVERB_REFLECTIONS_DELAY:
            if(!(val >= AL_EAXREVERB_MIN_REFLECTIONS_DELAY && val <= AL_EAXREVERB_MAX_REFLECTIONS_DELAY))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.ReflectionsDelay = val;
            break;

        case AL_EAXREVERB_LATE_REVERB_GAIN:
            if(!(val >= AL_EAXREVERB_MIN_LATE_REVERB_GAIN && val <= AL_EAXREVERB_MAX_LATE_REVERB_GAIN))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.LateReverbGain = val;
            break;

        case AL_EAXREVERB_LATE_REVERB_DELAY:
            if(!(val >= AL_EAXREVERB_MIN_LATE_REVERB_DELAY && val <= AL_EAXREVERB_MAX_LATE_REVERB_DELAY))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.LateReverbDelay = val;
            break;

        case AL_EAXREVERB_AIR_ABSORPTION_GAINHF:
            if(!(val >= AL_EAXREVERB_MIN_AIR_ABSORPTION_GAINHF && val <= AL_EAXREVERB_MAX_AIR_ABSORPTION_GAINHF))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.AirAbsorptionGainHF = val;
            break;

        case AL_EAXREVERB_ECHO_TIME:
            if(!(val >= AL_EAXREVERB_MIN_ECHO_TIME && val <= AL_EAXREVERB_MAX_ECHO_TIME))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.EchoTime = val;
            break;

        case AL_EAXREVERB_ECHO_DEPTH:
            if(!(val >= AL_EAXREVERB_MIN_ECHO_DEPTH && val <= AL_EAXREVERB_MAX_ECHO_DEPTH))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.EchoDepth = val;
            break;

        case AL_EAXREVERB_MODULATION_TIME:
            if(!(val >= AL_EAXREVERB_MIN_MODULATION_TIME && val <= AL_EAXREVERB_MAX_MODULATION_TIME))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.ModulationTime = val;
            break;

        case AL_EAXREVERB_MODULATION_DEPTH:
            if(!(val >= AL_EAXREVERB_MIN_MODULATION_DEPTH && val <= AL_EAXREVERB_MAX_MODULATION_DEPTH))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.ModulationDepth = val;
            break;

        case AL_EAXREVERB_HFREFERENCE:
            if(!(val >= AL_EAXREVERB_MIN_HFREFERENCE && val <= AL_EAXREVERB_MAX_HFREFERENCE))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.HFReference = val;
            break;

        case AL_EAXREVERB_LFREFERENCE:
            if(!(val >= AL_EAXREVERB_MIN_LFREFERENCE && val <= AL_EAXREVERB_MAX_LFREFERENCE))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.LFReference = val;
            break;

        case AL_EAXREVERB_ROOM_ROLLOFF_FACTOR:
            if(!(val >= AL_EAXREVERB_MIN_ROOM_ROLLOFF_FACTOR && val <= AL_EAXREVERB_MAX_ROOM_ROLLOFF_FACTOR))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.RoomRolloffFactor = val;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALeaxreverb_setParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_EAXREVERB_REFLECTIONS_PAN:
            if(!(isfinite(vals[0]) && isfinite(vals[1]) && isfinite(vals[2])))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.ReflectionsPan[0] = vals[0];
            props->Reverb.ReflectionsPan[1] = vals[1];
            props->Reverb.ReflectionsPan[2] = vals[2];
            break;
        case AL_EAXREVERB_LATE_REVERB_PAN:
            if(!(isfinite(vals[0]) && isfinite(vals[1]) && isfinite(vals[2])))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
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
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALeaxreverb_getParamiv(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{
    ALeaxreverb_getParami(effect, context, param, vals);
}
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
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
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
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.DecayHFLimit = val;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALreverb_setParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{
    ALreverb_setParami(effect, context, param, vals[0]);
}
void ALreverb_setParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_REVERB_DENSITY:
            if(!(val >= AL_REVERB_MIN_DENSITY && val <= AL_REVERB_MAX_DENSITY))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.Density = val;
            break;

        case AL_REVERB_DIFFUSION:
            if(!(val >= AL_REVERB_MIN_DIFFUSION && val <= AL_REVERB_MAX_DIFFUSION))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.Diffusion = val;
            break;

        case AL_REVERB_GAIN:
            if(!(val >= AL_REVERB_MIN_GAIN && val <= AL_REVERB_MAX_GAIN))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.Gain = val;
            break;

        case AL_REVERB_GAINHF:
            if(!(val >= AL_REVERB_MIN_GAINHF && val <= AL_REVERB_MAX_GAINHF))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.GainHF = val;
            break;

        case AL_REVERB_DECAY_TIME:
            if(!(val >= AL_REVERB_MIN_DECAY_TIME && val <= AL_REVERB_MAX_DECAY_TIME))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.DecayTime = val;
            break;

        case AL_REVERB_DECAY_HFRATIO:
            if(!(val >= AL_REVERB_MIN_DECAY_HFRATIO && val <= AL_REVERB_MAX_DECAY_HFRATIO))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.DecayHFRatio = val;
            break;

        case AL_REVERB_REFLECTIONS_GAIN:
            if(!(val >= AL_REVERB_MIN_REFLECTIONS_GAIN && val <= AL_REVERB_MAX_REFLECTIONS_GAIN))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.ReflectionsGain = val;
            break;

        case AL_REVERB_REFLECTIONS_DELAY:
            if(!(val >= AL_REVERB_MIN_REFLECTIONS_DELAY && val <= AL_REVERB_MAX_REFLECTIONS_DELAY))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.ReflectionsDelay = val;
            break;

        case AL_REVERB_LATE_REVERB_GAIN:
            if(!(val >= AL_REVERB_MIN_LATE_REVERB_GAIN && val <= AL_REVERB_MAX_LATE_REVERB_GAIN))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.LateReverbGain = val;
            break;

        case AL_REVERB_LATE_REVERB_DELAY:
            if(!(val >= AL_REVERB_MIN_LATE_REVERB_DELAY && val <= AL_REVERB_MAX_LATE_REVERB_DELAY))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.LateReverbDelay = val;
            break;

        case AL_REVERB_AIR_ABSORPTION_GAINHF:
            if(!(val >= AL_REVERB_MIN_AIR_ABSORPTION_GAINHF && val <= AL_REVERB_MAX_AIR_ABSORPTION_GAINHF))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.AirAbsorptionGainHF = val;
            break;

        case AL_REVERB_ROOM_ROLLOFF_FACTOR:
            if(!(val >= AL_REVERB_MIN_ROOM_ROLLOFF_FACTOR && val <= AL_REVERB_MAX_ROOM_ROLLOFF_FACTOR))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Reverb.RoomRolloffFactor = val;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALreverb_setParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{
    ALreverb_setParamf(effect, context, param, vals[0]);
}

void ALreverb_getParami(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *val)
{
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_REVERB_DECAY_HFLIMIT:
            *val = props->Reverb.DecayHFLimit;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALreverb_getParamiv(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{
    ALreverb_getParami(effect, context, param, vals);
}
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
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALreverb_getParamfv(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{
    ALreverb_getParamf(effect, context, param, vals);
}

DEFINE_ALEFFECT_VTABLE(ALreverb);
