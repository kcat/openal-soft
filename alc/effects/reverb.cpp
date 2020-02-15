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

#include <cstdio>
#include <cstdlib>
#include <cmath>

#include <array>
#include <numeric>
#include <algorithm>
#include <functional>

#include "al/auxeffectslot.h"
#include "al/listener.h"
#include "alcmain.h"
#include "alcontext.h"
#include "alu.h"
#include "bformatdec.h"
#include "filters/biquad.h"
#include "vector.h"
#include "vecmat.h"

/* This is a user config option for modifying the overall output of the reverb
 * effect.
 */
ALfloat ReverbBoost = 1.0f;

namespace {

using namespace std::placeholders;

/* Max samples per process iteration. Used to limit the size needed for
 * temporary buffers. Must be a multiple of 4 for SIMD alignment.
 */
constexpr size_t MAX_UPDATE_SAMPLES{256};

/* The number of spatialized lines or channels to process. Four channels allows
 * for a 3D A-Format response. NOTE: This can't be changed without taking care
 * of the conversion matrices, and a few places where the length arrays are
 * assumed to have 4 elements.
 */
constexpr size_t NUM_LINES{4u};


/* The B-Format to A-Format conversion matrix. The arrangement of rows is
 * deliberately chosen to align the resulting lines to their spatial opposites
 * (0:above front left <-> 3:above back right, 1:below front right <-> 2:below
 * back left). It's not quite opposite, since the A-Format results in a
 * tetrahedron, but it's close enough. Should the model be extended to 8-lines
 * in the future, true opposites can be used.
 */
alignas(16) constexpr ALfloat B2A[NUM_LINES][MAX_AMBI_CHANNELS]{
    { 0.288675134595f,  0.288675134595f,  0.288675134595f,  0.288675134595f },
    { 0.288675134595f, -0.288675134595f, -0.288675134595f,  0.288675134595f },
    { 0.288675134595f,  0.288675134595f, -0.288675134595f, -0.288675134595f },
    { 0.288675134595f, -0.288675134595f,  0.288675134595f, -0.288675134595f }
};

/* Converts A-Format to B-Format. */
alignas(16) constexpr ALfloat A2B[NUM_LINES][NUM_LINES]{
    { 0.866025403785f,  0.866025403785f,  0.866025403785f,  0.866025403785f },
    { 0.866025403785f, -0.866025403785f,  0.866025403785f, -0.866025403785f },
    { 0.866025403785f, -0.866025403785f, -0.866025403785f,  0.866025403785f },
    { 0.866025403785f,  0.866025403785f, -0.866025403785f, -0.866025403785f }
};


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
 *     length_mult = max(5.0, cbrt(density*DENSITY_SCALE));
 *
 * The density scale below will result in a max line multiplier of 50, for an
 * effective size range of 5m to 50m.
 */
constexpr ALfloat DENSITY_SCALE{125000.0f};

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
constexpr std::array<ALfloat,NUM_LINES> EARLY_TAP_LENGTHS{{
    0.0000000e+0f, 2.0213520e-4f, 4.2531060e-4f, 6.7171600e-4f
}};

/* The early all-pass filter lengths are based on the early tap lengths:
 *
 *     A_i = R_i / a
 *
 * Where a is the approximate maximum all-pass cycle limit (20).
 */
constexpr std::array<ALfloat,NUM_LINES> EARLY_ALLPASS_LENGTHS{{
    9.7096800e-5f, 1.0720356e-4f, 1.1836234e-4f, 1.3068260e-4f
}};

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
constexpr std::array<ALfloat,NUM_LINES> EARLY_LINE_LENGTHS{{
    5.9850400e-4f, 1.0913150e-3f, 1.5376658e-3f, 1.9419362e-3f
}};

/* The late all-pass filter lengths are based on the late line lengths:
 *
 *     A_i = (5 / 3) L_i / r_1
 */
constexpr std::array<ALfloat,NUM_LINES> LATE_ALLPASS_LENGTHS{{
    1.6182800e-4f, 2.0389060e-4f, 2.8159360e-4f, 3.2365600e-4f
}};

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
constexpr std::array<ALfloat,NUM_LINES> LATE_LINE_LENGTHS{{
    1.9419362e-3f, 2.4466860e-3f, 3.3791220e-3f, 3.8838720e-3f
}};


using ReverbUpdateLine = std::array<float,MAX_UPDATE_SAMPLES>;

struct DelayLineI {
    /* The delay lines use interleaved samples, with the lengths being powers
     * of 2 to allow the use of bit-masking instead of a modulus for wrapping.
     */
    size_t Mask{0u};
    union {
        uintptr_t LineOffset{0u};
        std::array<float,NUM_LINES> *Line;
    };

    /* Given the allocated sample buffer, this function updates each delay line
     * offset.
     */
    void realizeLineOffset(std::array<float,NUM_LINES> *sampleBuffer) noexcept
    { Line = sampleBuffer + LineOffset; }

    /* Calculate the length of a delay line and store its mask and offset. */
    ALuint calcLineLength(const ALfloat length, const uintptr_t offset, const ALfloat frequency,
        const ALuint extra)
    {
        /* All line lengths are powers of 2, calculated from their lengths in
         * seconds, rounded up.
         */
        ALuint samples{float2uint(std::ceil(length*frequency))};
        samples = NextPowerOf2(samples + extra);

        /* All lines share a single sample buffer. */
        Mask = samples - 1;
        LineOffset = offset;

        /* Return the sample count for accumulation. */
        return samples;
    }

    void write(size_t offset, const size_t c, const ALfloat *RESTRICT in, const size_t count) const noexcept
    {
        ASSUME(count > 0);
        for(size_t i{0u};i < count;)
        {
            offset &= Mask;
            size_t td{minz(Mask+1 - offset, count - i)};
            do {
                Line[offset++][c] = in[i++];
            } while(--td);
        }
    }
};

struct VecAllpass {
    DelayLineI Delay;
    ALfloat Coeff{0.0f};
    size_t  Offset[NUM_LINES][2]{};

    void processFaded(const al::span<ReverbUpdateLine,NUM_LINES> samples, size_t offset,
        const ALfloat xCoeff, const ALfloat yCoeff, ALfloat fadeCount, const ALfloat fadeStep,
        const size_t todo);
    void processUnfaded(const al::span<ReverbUpdateLine,NUM_LINES> samples, size_t offset,
        const ALfloat xCoeff, const ALfloat yCoeff, const size_t todo);
};

struct T60Filter {
    /* Two filters are used to adjust the signal. One to control the low
     * frequencies, and one to control the high frequencies.
     */
    ALfloat MidGain[2]{0.0f, 0.0f};
    BiquadFilter HFFilter, LFFilter;

    void calcCoeffs(const ALfloat length, const ALfloat lfDecayTime, const ALfloat mfDecayTime,
        const ALfloat hfDecayTime, const ALfloat lf0norm, const ALfloat hf0norm);

    /* Applies the two T60 damping filter sections. */
    void process(const al::span<float> samples)
    {
        HFFilter.process(samples, samples.begin());
        LFFilter.process(samples, samples.begin());
    }
};

struct EarlyReflections {
    /* A Gerzon vector all-pass filter is used to simulate initial diffusion.
     * The spread from this filter also helps smooth out the reverb tail.
     */
    VecAllpass VecAp;

    /* An echo line is used to complete the second half of the early
     * reflections.
     */
    DelayLineI Delay;
    size_t     Offset[NUM_LINES][2]{};
    ALfloat    Coeff[NUM_LINES][2]{};

    /* The gain for each output channel based on 3D panning. */
    ALfloat CurrentGain[NUM_LINES][MAX_OUTPUT_CHANNELS]{};
    ALfloat PanGain[NUM_LINES][MAX_OUTPUT_CHANNELS]{};

    void updateLines(const ALfloat density, const ALfloat diffusion, const ALfloat decayTime,
        const ALfloat frequency);
};

struct LateReverb {
    /* A recursive delay line is used fill in the reverb tail. */
    DelayLineI Delay;
    size_t     Offset[NUM_LINES][2]{};

    /* Attenuation to compensate for the modal density and decay rate of the
     * late lines.
     */
    ALfloat DensityGain[2]{0.0f, 0.0f};

    /* T60 decay filters are used to simulate absorption. */
    T60Filter T60[NUM_LINES];

    /* A Gerzon vector all-pass filter is used to simulate diffusion. */
    VecAllpass VecAp;

    /* The gain for each output channel based on 3D panning. */
    ALfloat CurrentGain[NUM_LINES][MAX_OUTPUT_CHANNELS]{};
    ALfloat PanGain[NUM_LINES][MAX_OUTPUT_CHANNELS]{};

    void updateLines(const ALfloat density, const ALfloat diffusion, const ALfloat lfDecayTime,
        const ALfloat mfDecayTime, const ALfloat hfDecayTime, const ALfloat lf0norm,
        const ALfloat hf0norm, const ALfloat frequency);
};

struct ReverbState final : public EffectState {
    /* All delay lines are allocated as a single buffer to reduce memory
     * fragmentation and management code.
     */
    al::vector<std::array<float,NUM_LINES>,16> mSampleBuffer;

    struct {
        /* Calculated parameters which indicate if cross-fading is needed after
         * an update.
         */
        ALfloat Density{AL_EAXREVERB_DEFAULT_DENSITY};
        ALfloat Diffusion{AL_EAXREVERB_DEFAULT_DIFFUSION};
        ALfloat DecayTime{AL_EAXREVERB_DEFAULT_DECAY_TIME};
        ALfloat HFDecayTime{AL_EAXREVERB_DEFAULT_DECAY_HFRATIO * AL_EAXREVERB_DEFAULT_DECAY_TIME};
        ALfloat LFDecayTime{AL_EAXREVERB_DEFAULT_DECAY_LFRATIO * AL_EAXREVERB_DEFAULT_DECAY_TIME};
        ALfloat HFReference{AL_EAXREVERB_DEFAULT_HFREFERENCE};
        ALfloat LFReference{AL_EAXREVERB_DEFAULT_LFREFERENCE};
    } mParams;

    /* Master effect filters */
    struct {
        BiquadFilter Lp;
        BiquadFilter Hp;
    } mFilter[NUM_LINES];

    /* Core delay line (early reflections and late reverb tap from this). */
    DelayLineI mDelay;

    /* Tap points for early reflection delay. */
    size_t  mEarlyDelayTap[NUM_LINES][2]{};
    ALfloat mEarlyDelayCoeff[NUM_LINES][2]{};

    /* Tap points for late reverb feed and delay. */
    size_t mLateFeedTap{};
    size_t mLateDelayTap[NUM_LINES][2]{};

    /* Coefficients for the all-pass and line scattering matrices. */
    ALfloat mMixX{0.0f};
    ALfloat mMixY{0.0f};

    EarlyReflections mEarly;

    LateReverb mLate;

    bool mDoFading{};

    /* Maximum number of samples to process at once. */
    size_t mMaxUpdate[2]{MAX_UPDATE_SAMPLES, MAX_UPDATE_SAMPLES};

    /* The current write offset for all delay lines. */
    size_t mOffset{};

    /* Temporary storage used when processing. */
    union {
        alignas(16) FloatBufferLine mTempLine{};
        alignas(16) std::array<ReverbUpdateLine,NUM_LINES> mTempSamples;
    };
    alignas(16) std::array<ReverbUpdateLine,NUM_LINES> mEarlySamples{};
    alignas(16) std::array<ReverbUpdateLine,NUM_LINES> mLateSamples{};

    using MixOutT = void (ReverbState::*)(const al::span<FloatBufferLine> samplesOut,
        const size_t counter, const size_t offset, const size_t todo);

    MixOutT mMixOut{&ReverbState::MixOutPlain};
    std::array<ALfloat,MAX_AMBI_ORDER+1> mOrderScales{};
    std::array<std::array<BandSplitter,NUM_LINES>,2> mAmbiSplitter;


    void MixOutPlain(const al::span<FloatBufferLine> samplesOut, const size_t counter,
        const size_t offset, const size_t todo)
    {
        ASSUME(todo > 0);

        /* Convert back to B-Format, and mix the results to output. */
        const al::span<float> tmpspan{mTempLine.data(), todo};
        for(size_t c{0u};c < NUM_LINES;c++)
        {
            std::fill(tmpspan.begin(), tmpspan.end(), 0.0f);
            MixRowSamples(tmpspan, {A2B[c], NUM_LINES}, mEarlySamples[0].data(),
                mEarlySamples[0].size());
            MixSamples(tmpspan, samplesOut, mEarly.CurrentGain[c], mEarly.PanGain[c], counter,
                offset);
        }
        for(size_t c{0u};c < NUM_LINES;c++)
        {
            std::fill(tmpspan.begin(), tmpspan.end(), 0.0f);
            MixRowSamples(tmpspan, {A2B[c], NUM_LINES}, mLateSamples[0].data(),
                mLateSamples[0].size());
            MixSamples(tmpspan, samplesOut, mLate.CurrentGain[c], mLate.PanGain[c], counter,
                offset);
        }
    }

    void MixOutAmbiUp(const al::span<FloatBufferLine> samplesOut, const size_t counter,
        const size_t offset, const size_t todo)
    {
        ASSUME(todo > 0);

        const al::span<float> tmpspan{mTempLine.data(), todo};
        for(size_t c{0u};c < NUM_LINES;c++)
        {
            std::fill(tmpspan.begin(), tmpspan.end(), 0.0f);
            MixRowSamples(tmpspan, {A2B[c], NUM_LINES}, mEarlySamples[0].data(),
                mEarlySamples[0].size());

            /* Apply scaling to the B-Format's HF response to "upsample" it to
             * higher-order output.
             */
            const ALfloat hfscale{(c==0) ? mOrderScales[0] : mOrderScales[1]};
            mAmbiSplitter[0][c].applyHfScale(tmpspan, hfscale);

            MixSamples(tmpspan, samplesOut, mEarly.CurrentGain[c], mEarly.PanGain[c], counter,
                offset);
        }
        for(size_t c{0u};c < NUM_LINES;c++)
        {
            std::fill(tmpspan.begin(), tmpspan.end(), 0.0f);
            MixRowSamples(tmpspan, {A2B[c], NUM_LINES}, mLateSamples[0].data(),
                mLateSamples[0].size());

            const ALfloat hfscale{(c==0) ? mOrderScales[0] : mOrderScales[1]};
            mAmbiSplitter[1][c].applyHfScale(tmpspan, hfscale);

            MixSamples(tmpspan, samplesOut, mLate.CurrentGain[c], mLate.PanGain[c], counter,
                offset);
        }
    }

    bool allocLines(const ALfloat frequency);

    void updateDelayLine(const ALfloat earlyDelay, const ALfloat lateDelay, const ALfloat density,
        const ALfloat decayTime, const ALfloat frequency);
    void update3DPanning(const ALfloat *ReflectionsPan, const ALfloat *LateReverbPan,
        const ALfloat earlyGain, const ALfloat lateGain, const EffectTarget &target);

    void earlyUnfaded(const size_t offset, const size_t todo);
    void earlyFaded(const size_t offset, const size_t todo, const ALfloat fade,
        const ALfloat fadeStep);

    void lateUnfaded(const size_t offset, const size_t todo);
    void lateFaded(const size_t offset, const size_t todo, const ALfloat fade,
        const ALfloat fadeStep);

    ALboolean deviceUpdate(const ALCdevice *device) override;
    void update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target) override;
    void process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut) override;

    DEF_NEWDEL(ReverbState)
};

/**************************************
 *  Device Update                     *
 **************************************/

inline ALfloat CalcDelayLengthMult(ALfloat density)
{ return maxf(5.0f, std::cbrt(density*DENSITY_SCALE)); }

/* Calculates the delay line metrics and allocates the shared sample buffer
 * for all lines given the sample rate (frequency).  If an allocation failure
 * occurs, it returns AL_FALSE.
 */
bool ReverbState::allocLines(const ALfloat frequency)
{
    /* All delay line lengths are calculated to accomodate the full range of
     * lengths given their respective paramters.
     */
    size_t totalSamples{0u};

    /* Multiplier for the maximum density value, i.e. density=1, which is
     * actually the least density...
     */
    ALfloat multiplier{CalcDelayLengthMult(AL_EAXREVERB_MAX_DENSITY)};

    /* The main delay length includes the maximum early reflection delay, the
     * largest early tap width, the maximum late reverb delay, and the
     * largest late tap width.  Finally, it must also be extended by the
     * update size (BUFFERSIZE) for block processing.
     */
    ALfloat length{AL_EAXREVERB_MAX_REFLECTIONS_DELAY + EARLY_TAP_LENGTHS.back()*multiplier +
        AL_EAXREVERB_MAX_LATE_REVERB_DELAY +
        (LATE_LINE_LENGTHS.back() - LATE_LINE_LENGTHS.front())/float{NUM_LINES}*multiplier};
    totalSamples += mDelay.calcLineLength(length, totalSamples, frequency, BUFFERSIZE);

    /* The early vector all-pass line. */
    length = EARLY_ALLPASS_LENGTHS.back() * multiplier;
    totalSamples += mEarly.VecAp.Delay.calcLineLength(length, totalSamples, frequency, 0);

    /* The early reflection line. */
    length = EARLY_LINE_LENGTHS.back() * multiplier;
    totalSamples += mEarly.Delay.calcLineLength(length, totalSamples, frequency, 0);

    /* The late vector all-pass line. */
    length = LATE_ALLPASS_LENGTHS.back() * multiplier;
    totalSamples += mLate.VecAp.Delay.calcLineLength(length, totalSamples, frequency, 0);

    /* The late delay lines are calculated from the largest maximum density
     * line length.
     */
    length = LATE_LINE_LENGTHS.back() * multiplier;
    totalSamples += mLate.Delay.calcLineLength(length, totalSamples, frequency, 0);

    if(totalSamples != mSampleBuffer.size())
    {
        mSampleBuffer.resize(totalSamples);
        mSampleBuffer.shrink_to_fit();
    }

    /* Clear the sample buffer. */
    std::fill(mSampleBuffer.begin(), mSampleBuffer.end(), std::array<float,NUM_LINES>{});

    /* Update all delays to reflect the new sample buffer. */
    mDelay.realizeLineOffset(mSampleBuffer.data());
    mEarly.VecAp.Delay.realizeLineOffset(mSampleBuffer.data());
    mEarly.Delay.realizeLineOffset(mSampleBuffer.data());
    mLate.VecAp.Delay.realizeLineOffset(mSampleBuffer.data());
    mLate.Delay.realizeLineOffset(mSampleBuffer.data());

    return true;
}

ALboolean ReverbState::deviceUpdate(const ALCdevice *device)
{
    const auto frequency = static_cast<ALfloat>(device->Frequency);

    /* Allocate the delay lines. */
    if(!allocLines(frequency))
        return AL_FALSE;

    const ALfloat multiplier{CalcDelayLengthMult(AL_EAXREVERB_MAX_DENSITY)};

    /* The late feed taps are set a fixed position past the latest delay tap. */
    mLateFeedTap = float2uint(
        (AL_EAXREVERB_MAX_REFLECTIONS_DELAY + EARLY_TAP_LENGTHS.back()*multiplier) * frequency);

    /* Clear filters and gain coefficients since the delay lines were all just
     * cleared (if not reallocated).
     */
    for(auto &filter : mFilter)
    {
        filter.Lp.clear();
        filter.Hp.clear();
    }

    for(auto &coeff : mEarlyDelayCoeff)
        std::fill(std::begin(coeff), std::end(coeff), 0.0f);
    for(auto &coeff : mEarly.Coeff)
        std::fill(std::begin(coeff), std::end(coeff), 0.0f);

    mLate.DensityGain[0] = 0.0f;
    mLate.DensityGain[1] = 0.0f;
    for(auto &t60 : mLate.T60)
    {
        t60.MidGain[0] = 0.0f;
        t60.MidGain[1] = 0.0f;
        t60.HFFilter.clear();
        t60.LFFilter.clear();
    }

    for(auto &gains : mEarly.CurrentGain)
        std::fill(std::begin(gains), std::end(gains), 0.0f);
    for(auto &gains : mEarly.PanGain)
        std::fill(std::begin(gains), std::end(gains), 0.0f);
    for(auto &gains : mLate.CurrentGain)
        std::fill(std::begin(gains), std::end(gains), 0.0f);
    for(auto &gains : mLate.PanGain)
        std::fill(std::begin(gains), std::end(gains), 0.0f);

    /* Reset fading and offset base. */
    mDoFading = true;
    std::fill(std::begin(mMaxUpdate), std::end(mMaxUpdate), MAX_UPDATE_SAMPLES);
    mOffset = 0;

    if(device->mAmbiOrder > 1)
    {
        mMixOut = &ReverbState::MixOutAmbiUp;
        mOrderScales = BFormatDec::GetHFOrderScales(1, device->mAmbiOrder);
    }
    else
    {
        mMixOut = &ReverbState::MixOutPlain;
        mOrderScales.fill(1.0f);
    }
    mAmbiSplitter[0][0].init(400.0f / frequency);
    std::fill(mAmbiSplitter[0].begin()+1, mAmbiSplitter[0].end(), mAmbiSplitter[0][0]);
    std::fill(mAmbiSplitter[1].begin(), mAmbiSplitter[1].end(), mAmbiSplitter[0][0]);

    return AL_TRUE;
}

/**************************************
 *  Effect Update                     *
 **************************************/

/* Calculate a decay coefficient given the length of each cycle and the time
 * until the decay reaches -60 dB.
 */
inline ALfloat CalcDecayCoeff(const ALfloat length, const ALfloat decayTime)
{ return std::pow(REVERB_DECAY_GAIN, length/decayTime); }

/* Calculate a decay length from a coefficient and the time until the decay
 * reaches -60 dB.
 */
inline ALfloat CalcDecayLength(const ALfloat coeff, const ALfloat decayTime)
{ return std::log10(coeff) * decayTime / std::log10(REVERB_DECAY_GAIN); }

/* Calculate an attenuation to be applied to the input of any echo models to
 * compensate for modal density and decay time.
 */
inline ALfloat CalcDensityGain(const ALfloat a)
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
    return std::sqrt(1.0f - a*a);
}

/* Calculate the scattering matrix coefficients given a diffusion factor. */
inline ALvoid CalcMatrixCoeffs(const ALfloat diffusion, ALfloat *x, ALfloat *y)
{
    /* The matrix is of order 4, so n is sqrt(4 - 1). */
    ALfloat n{std::sqrt(3.0f)};
    ALfloat t{diffusion * std::atan(n)};

    /* Calculate the first mixing matrix coefficient. */
    *x = std::cos(t);
    /* Calculate the second mixing matrix coefficient. */
    *y = std::sin(t) / n;
}

/* Calculate the limited HF ratio for use with the late reverb low-pass
 * filters.
 */
ALfloat CalcLimitedHfRatio(const ALfloat hfRatio, const ALfloat airAbsorptionGainHF,
    const ALfloat decayTime)
{
    /* Find the attenuation due to air absorption in dB (converting delay
     * time to meters using the speed of sound).  Then reversing the decay
     * equation, solve for HF ratio.  The delay length is cancelled out of
     * the equation, so it can be calculated once for all lines.
     */
    ALfloat limitRatio{1.0f /
        (CalcDecayLength(airAbsorptionGainHF, decayTime) * SPEEDOFSOUNDMETRESPERSEC)};

    /* Using the limit calculated above, apply the upper bound to the HF ratio.
     */
    return minf(limitRatio, hfRatio);
}


/* Calculates the 3-band T60 damping coefficients for a particular delay line
 * of specified length, using a combination of two shelf filter sections given
 * decay times for each band split at two reference frequencies.
 */
void T60Filter::calcCoeffs(const ALfloat length, const ALfloat lfDecayTime,
    const ALfloat mfDecayTime, const ALfloat hfDecayTime, const ALfloat lf0norm,
    const ALfloat hf0norm)
{
    const float mfGain{CalcDecayCoeff(length, mfDecayTime)};
    const float lfGain{CalcDecayCoeff(length, lfDecayTime) / mfGain};
    const float hfGain{CalcDecayCoeff(length, hfDecayTime) / mfGain};

    MidGain[1] = mfGain;
    LFFilter.setParamsFromSlope(BiquadType::LowShelf, lf0norm, lfGain, 1.0f);
    HFFilter.setParamsFromSlope(BiquadType::HighShelf, hf0norm, hfGain, 1.0f);
}

/* Update the early reflection line lengths and gain coefficients. */
void EarlyReflections::updateLines(const ALfloat density, const ALfloat diffusion,
    const ALfloat decayTime, const ALfloat frequency)
{
    const ALfloat multiplier{CalcDelayLengthMult(density)};

    /* Calculate the all-pass feed-back/forward coefficient. */
    VecAp.Coeff = std::sqrt(0.5f) * std::pow(diffusion, 2.0f);

    for(size_t i{0u};i < NUM_LINES;i++)
    {
        /* Calculate the length (in seconds) of each all-pass line. */
        ALfloat length{EARLY_ALLPASS_LENGTHS[i] * multiplier};

        /* Calculate the delay offset for each all-pass line. */
        VecAp.Offset[i][1] = float2uint(length * frequency);

        /* Calculate the length (in seconds) of each delay line. */
        length = EARLY_LINE_LENGTHS[i] * multiplier;

        /* Calculate the delay offset for each delay line. */
        Offset[i][1] = float2uint(length * frequency);

        /* Calculate the gain (coefficient) for each line. */
        Coeff[i][1] = CalcDecayCoeff(length, decayTime);
    }
}

/* Update the late reverb line lengths and T60 coefficients. */
void LateReverb::updateLines(const ALfloat density, const ALfloat diffusion,
    const ALfloat lfDecayTime, const ALfloat mfDecayTime, const ALfloat hfDecayTime,
    const ALfloat lf0norm, const ALfloat hf0norm, const ALfloat frequency)
{
    /* Scaling factor to convert the normalized reference frequencies from
     * representing 0...freq to 0...max_reference.
     */
    const ALfloat norm_weight_factor{frequency / AL_EAXREVERB_MAX_HFREFERENCE};

    const ALfloat late_allpass_avg{
        std::accumulate(LATE_ALLPASS_LENGTHS.begin(), LATE_ALLPASS_LENGTHS.end(), 0.0f) /
        float{NUM_LINES}};

    /* To compensate for changes in modal density and decay time of the late
     * reverb signal, the input is attenuated based on the maximal energy of
     * the outgoing signal.  This approximation is used to keep the apparent
     * energy of the signal equal for all ranges of density and decay time.
     *
     * The average length of the delay lines is used to calculate the
     * attenuation coefficient.
     */
    const ALfloat multiplier{CalcDelayLengthMult(density)};
    ALfloat length{std::accumulate(LATE_LINE_LENGTHS.begin(), LATE_LINE_LENGTHS.end(), 0.0f) /
        float{NUM_LINES} * multiplier};
    length += late_allpass_avg * multiplier;
    /* The density gain calculation uses an average decay time weighted by
     * approximate bandwidth. This attempts to compensate for losses of energy
     * that reduce decay time due to scattering into highly attenuated bands.
     */
    const ALfloat decayTimeWeighted{
        (lf0norm*norm_weight_factor)*lfDecayTime +
        (hf0norm*norm_weight_factor - lf0norm*norm_weight_factor)*mfDecayTime +
        (1.0f - hf0norm*norm_weight_factor)*hfDecayTime};
    DensityGain[1] = CalcDensityGain(CalcDecayCoeff(length, decayTimeWeighted));

    /* Calculate the all-pass feed-back/forward coefficient. */
    VecAp.Coeff = std::sqrt(0.5f) * std::pow(diffusion, 2.0f);

    for(size_t i{0u};i < NUM_LINES;i++)
    {
        /* Calculate the length (in seconds) of each all-pass line. */
        length = LATE_ALLPASS_LENGTHS[i] * multiplier;

        /* Calculate the delay offset for each all-pass line. */
        VecAp.Offset[i][1] = float2uint(length * frequency);

        /* Calculate the length (in seconds) of each delay line. */
        length = LATE_LINE_LENGTHS[i] * multiplier;

        /* Calculate the delay offset for each delay line. */
        Offset[i][1] = float2uint(length*frequency + 0.5f);

        /* Approximate the absorption that the vector all-pass would exhibit
         * given the current diffusion so we don't have to process a full T60
         * filter for each of its four lines.
         */
        length += lerp(LATE_ALLPASS_LENGTHS[i], late_allpass_avg, diffusion) * multiplier;

        /* Calculate the T60 damping coefficients for each line. */
        T60[i].calcCoeffs(length, lfDecayTime, mfDecayTime, hfDecayTime, lf0norm, hf0norm);
    }
}


/* Update the offsets for the main effect delay line. */
void ReverbState::updateDelayLine(const ALfloat earlyDelay, const ALfloat lateDelay,
    const ALfloat density, const ALfloat decayTime, const ALfloat frequency)
{
    const ALfloat multiplier{CalcDelayLengthMult(density)};

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
    for(size_t i{0u};i < NUM_LINES;i++)
    {
        ALfloat length{earlyDelay + EARLY_TAP_LENGTHS[i]*multiplier};
        mEarlyDelayTap[i][1] = float2uint(length * frequency);

        length = EARLY_TAP_LENGTHS[i]*multiplier;
        mEarlyDelayCoeff[i][1] = CalcDecayCoeff(length, decayTime);

        length = (LATE_LINE_LENGTHS[i] - LATE_LINE_LENGTHS.front())/float{NUM_LINES}*multiplier +
            lateDelay;
        mLateDelayTap[i][1] = mLateFeedTap + float2uint(length * frequency);
    }
}

/* Creates a transform matrix given a reverb vector. The vector pans the reverb
 * reflections toward the given direction, using its magnitude (up to 1) as a
 * focal strength. This function results in a B-Format transformation matrix
 * that spatially focuses the signal in the desired direction.
 */
alu::Matrix GetTransformFromVector(const ALfloat *vec)
{
    constexpr float sqrt_3{1.73205080756887719318f};

    /* Normalize the panning vector according to the N3D scale, which has an
     * extra sqrt(3) term on the directional components. Converting from OpenAL
     * to B-Format also requires negating X (ACN 1) and Z (ACN 3). Note however
     * that the reverb panning vectors use left-handed coordinates, unlike the
     * rest of OpenAL which use right-handed. This is fixed by negating Z,
     * which cancels out with the B-Format Z negation.
     */
    ALfloat norm[3];
    ALfloat mag{std::sqrt(vec[0]*vec[0] + vec[1]*vec[1] + vec[2]*vec[2])};
    if(mag > 1.0f)
    {
        norm[0] = vec[0] / mag * -sqrt_3;
        norm[1] = vec[1] / mag * sqrt_3;
        norm[2] = vec[2] / mag * sqrt_3;
        mag = 1.0f;
    }
    else
    {
        /* If the magnitude is less than or equal to 1, just apply the sqrt(3)
         * term. There's no need to renormalize the magnitude since it would
         * just be reapplied in the matrix.
         */
        norm[0] = vec[0] * -sqrt_3;
        norm[1] = vec[1] * sqrt_3;
        norm[2] = vec[2] * sqrt_3;
    }

    return alu::Matrix{
        1.0f,   0.0f,    0.0f,   0.0f,
        norm[0], 1.0f-mag, 0.0f, 0.0f,
        norm[1], 0.0f, 1.0f-mag, 0.0f,
        norm[2], 0.0f, 0.0f, 1.0f-mag
    };
}

/* Update the early and late 3D panning gains. */
void ReverbState::update3DPanning(const ALfloat *ReflectionsPan, const ALfloat *LateReverbPan,
    const ALfloat earlyGain, const ALfloat lateGain, const EffectTarget &target)
{
    /* Create matrices that transform a B-Format signal according to the
     * panning vectors.
     */
    const alu::Matrix earlymat{GetTransformFromVector(ReflectionsPan)};
    const alu::Matrix latemat{GetTransformFromVector(LateReverbPan)};

    mOutTarget = target.Main->Buffer;
    for(size_t i{0u};i < NUM_LINES;i++)
    {
        const ALfloat coeffs[MAX_AMBI_CHANNELS]{earlymat[0][i], earlymat[1][i], earlymat[2][i],
            earlymat[3][i]};
        ComputePanGains(target.Main, coeffs, earlyGain, mEarly.PanGain[i]);
    }
    for(size_t i{0u};i < NUM_LINES;i++)
    {
        const ALfloat coeffs[MAX_AMBI_CHANNELS]{latemat[0][i], latemat[1][i], latemat[2][i],
            latemat[3][i]};
        ComputePanGains(target.Main, coeffs, lateGain, mLate.PanGain[i]);
    }
}

void ReverbState::update(const ALCcontext *Context, const ALeffectslot *Slot, const EffectProps *props, const EffectTarget target)
{
    const ALCdevice *Device{Context->mDevice.get()};
    const auto frequency = static_cast<ALfloat>(Device->Frequency);

    /* Calculate the master filters */
    float hf0norm{minf(props->Reverb.HFReference/frequency, 0.49f)};
    mFilter[0].Lp.setParamsFromSlope(BiquadType::HighShelf, hf0norm, props->Reverb.GainHF, 1.0f);
    float lf0norm{minf(props->Reverb.LFReference/frequency, 0.49f)};
    mFilter[0].Hp.setParamsFromSlope(BiquadType::LowShelf, lf0norm, props->Reverb.GainLF, 1.0f);
    for(size_t i{1u};i < NUM_LINES;i++)
    {
        mFilter[i].Lp.copyParamsFrom(mFilter[0].Lp);
        mFilter[i].Hp.copyParamsFrom(mFilter[0].Hp);
    }

    /* Update the main effect delay and associated taps. */
    updateDelayLine(props->Reverb.ReflectionsDelay, props->Reverb.LateReverbDelay,
                    props->Reverb.Density, props->Reverb.DecayTime, frequency);

    /* Update the early lines. */
    mEarly.updateLines(props->Reverb.Density, props->Reverb.Diffusion, props->Reverb.DecayTime,
        frequency);

    /* Get the mixing matrix coefficients. */
    CalcMatrixCoeffs(props->Reverb.Diffusion, &mMixX, &mMixY);

    /* If the HF limit parameter is flagged, calculate an appropriate limit
     * based on the air absorption parameter.
     */
    ALfloat hfRatio{props->Reverb.DecayHFRatio};
    if(props->Reverb.DecayHFLimit && props->Reverb.AirAbsorptionGainHF < 1.0f)
        hfRatio = CalcLimitedHfRatio(hfRatio, props->Reverb.AirAbsorptionGainHF,
            props->Reverb.DecayTime);

    /* Calculate the LF/HF decay times. */
    const ALfloat lfDecayTime{clampf(props->Reverb.DecayTime * props->Reverb.DecayLFRatio,
        AL_EAXREVERB_MIN_DECAY_TIME, AL_EAXREVERB_MAX_DECAY_TIME)};
    const ALfloat hfDecayTime{clampf(props->Reverb.DecayTime * hfRatio,
        AL_EAXREVERB_MIN_DECAY_TIME, AL_EAXREVERB_MAX_DECAY_TIME)};

    /* Update the late lines. */
    mLate.updateLines(props->Reverb.Density, props->Reverb.Diffusion, lfDecayTime,
        props->Reverb.DecayTime, hfDecayTime, lf0norm, hf0norm, frequency);

    /* Update early and late 3D panning. */
    const ALfloat gain{props->Reverb.Gain * Slot->Params.Gain * ReverbBoost};
    update3DPanning(props->Reverb.ReflectionsPan, props->Reverb.LateReverbPan,
        props->Reverb.ReflectionsGain*gain, props->Reverb.LateReverbGain*gain, target);

    /* Calculate the max update size from the smallest relevant delay. */
    mMaxUpdate[1] = minz(MAX_UPDATE_SAMPLES, minz(mEarly.Offset[0][1], mLate.Offset[0][1]));

    /* Determine if delay-line cross-fading is required. Density is essentially
     * a master control for the feedback delays, so changes the offsets of many
     * delay lines.
     */
    mDoFading |= (mParams.Density != props->Reverb.Density ||
        /* Diffusion and decay times influences the decay rate (gain) of the
         * late reverb T60 filter.
         */
       mParams.Diffusion != props->Reverb.Diffusion ||
       mParams.DecayTime != props->Reverb.DecayTime ||
       mParams.HFDecayTime != hfDecayTime ||
       mParams.LFDecayTime != lfDecayTime ||
       /* HF/LF References control the weighting used to calculate the density
        * gain.
        */
       mParams.HFReference != props->Reverb.HFReference ||
       mParams.LFReference != props->Reverb.LFReference);
    if(mDoFading)
    {
        mParams.Density = props->Reverb.Density;
        mParams.Diffusion = props->Reverb.Diffusion;
        mParams.DecayTime = props->Reverb.DecayTime;
        mParams.HFDecayTime = hfDecayTime;
        mParams.LFDecayTime = lfDecayTime;
        mParams.HFReference = props->Reverb.HFReference;
        mParams.LFReference = props->Reverb.LFReference;
    }
}


/**************************************
 *  Effect Processing                 *
 **************************************/

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
inline auto VectorPartialScatter(const std::array<float,NUM_LINES> &RESTRICT in,
    const ALfloat xCoeff, const ALfloat yCoeff) -> std::array<float,NUM_LINES>
{
    std::array<float,NUM_LINES> out;
    out[0] = xCoeff*in[0] + yCoeff*(          in[1] + -in[2] + in[3]);
    out[1] = xCoeff*in[1] + yCoeff*(-in[0]          +  in[2] + in[3]);
    out[2] = xCoeff*in[2] + yCoeff*( in[0] + -in[1]          + in[3]);
    out[3] = xCoeff*in[3] + yCoeff*(-in[0] + -in[1] + -in[2]        );
    return out;
}

/* Utilizes the above, but reverses the input channels. */
void VectorScatterRevDelayIn(const DelayLineI delay, size_t offset, const ALfloat xCoeff,
    const ALfloat yCoeff, const al::span<const ReverbUpdateLine,NUM_LINES> in, const size_t count)
{
    ASSUME(count > 0);

    for(size_t i{0u};i < count;)
    {
        offset &= delay.Mask;
        size_t td{minz(delay.Mask+1 - offset, count-i)};
        do {
            std::array<float,NUM_LINES> f;
            for(size_t j{0u};j < NUM_LINES;j++)
                f[NUM_LINES-1-j] = in[j][i];
            ++i;

            delay.Line[offset++] = VectorPartialScatter(f, xCoeff, yCoeff);
        } while(--td);
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
void VecAllpass::processUnfaded(const al::span<ReverbUpdateLine,NUM_LINES> samples, size_t offset,
    const ALfloat xCoeff, const ALfloat yCoeff, const size_t todo)
{
    const DelayLineI delay{Delay};
    const ALfloat feedCoeff{Coeff};

    ASSUME(todo > 0);

    size_t vap_offset[NUM_LINES];
    for(size_t j{0u};j < NUM_LINES;j++)
        vap_offset[j] = offset - Offset[j][0];
    for(size_t i{0u};i < todo;)
    {
        for(size_t j{0u};j < NUM_LINES;j++)
            vap_offset[j] &= delay.Mask;
        offset &= delay.Mask;

        size_t maxoff{offset};
        for(size_t j{0u};j < NUM_LINES;j++)
            maxoff = maxz(maxoff, vap_offset[j]);
        size_t td{minz(delay.Mask+1 - maxoff, todo - i)};

        do {
            std::array<float,NUM_LINES> f;
            for(size_t j{0u};j < NUM_LINES;j++)
            {
                const ALfloat input{samples[j][i]};
                const ALfloat out{delay.Line[vap_offset[j]++][j] - feedCoeff*input};
                f[j] = input + feedCoeff*out;

                samples[j][i] = out;
            }
            ++i;

            delay.Line[offset++] = VectorPartialScatter(f, xCoeff, yCoeff);
        } while(--td);
    }
}
void VecAllpass::processFaded(const al::span<ReverbUpdateLine,NUM_LINES> samples, size_t offset,
    const ALfloat xCoeff, const ALfloat yCoeff, ALfloat fadeCount, const ALfloat fadeStep,
    const size_t todo)
{
    const DelayLineI delay{Delay};
    const ALfloat feedCoeff{Coeff};

    ASSUME(todo > 0);

    size_t vap_offset[NUM_LINES][2];
    for(size_t j{0u};j < NUM_LINES;j++)
    {
        vap_offset[j][0] = offset - Offset[j][0];
        vap_offset[j][1] = offset - Offset[j][1];
    }
    for(size_t i{0u};i < todo;)
    {
        for(size_t j{0u};j < NUM_LINES;j++)
        {
            vap_offset[j][0] &= delay.Mask;
            vap_offset[j][1] &= delay.Mask;
        }
        offset &= delay.Mask;

        size_t maxoff{offset};
        for(size_t j{0u};j < NUM_LINES;j++)
            maxoff = maxz(maxoff, maxz(vap_offset[j][0], vap_offset[j][1]));
        size_t td{minz(delay.Mask+1 - maxoff, todo - i)};

        do {
            fadeCount += 1.0f;
            const float fade{fadeCount * fadeStep};

            std::array<float,NUM_LINES> f;
            for(size_t j{0u};j < NUM_LINES;j++)
                f[j] = delay.Line[vap_offset[j][0]++][j]*(1.0f-fade) +
                    delay.Line[vap_offset[j][1]++][j]*fade;

            for(size_t j{0u};j < NUM_LINES;j++)
            {
                const ALfloat input{samples[j][i]};
                const ALfloat out{f[j] - feedCoeff*input};
                f[j] = input + feedCoeff*out;

                samples[j][i] = out;
            }
            ++i;

            delay.Line[offset++] = VectorPartialScatter(f, xCoeff, yCoeff);
        } while(--td);
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
void ReverbState::earlyUnfaded(const size_t offset, const size_t todo)
{
    const DelayLineI early_delay{mEarly.Delay};
    const DelayLineI main_delay{mDelay};
    const ALfloat mixX{mMixX};
    const ALfloat mixY{mMixY};

    ASSUME(todo > 0);

    /* First, load decorrelated samples from the main delay line as the primary
     * reflections.
     */
    for(size_t j{0u};j < NUM_LINES;j++)
    {
        size_t early_delay_tap{offset - mEarlyDelayTap[j][0]};
        const ALfloat coeff{mEarlyDelayCoeff[j][0]};
        for(size_t i{0u};i < todo;)
        {
            early_delay_tap &= main_delay.Mask;
            size_t td{minz(main_delay.Mask+1 - early_delay_tap, todo - i)};
            do {
                mTempSamples[j][i++] = main_delay.Line[early_delay_tap++][j] * coeff;
            } while(--td);
        }
    }

    /* Apply a vector all-pass, to help color the initial reflections based on
     * the diffusion strength.
     */
    mEarly.VecAp.processUnfaded(mTempSamples, offset, mixX, mixY, todo);

    /* Apply a delay and bounce to generate secondary reflections, combine with
     * the primary reflections and write out the result for mixing.
     */
    for(size_t j{0u};j < NUM_LINES;j++)
    {
        size_t feedb_tap{offset - mEarly.Offset[j][0]};
        const ALfloat feedb_coeff{mEarly.Coeff[j][0]};
        float *out = mEarlySamples[j].data();

        for(size_t i{0u};i < todo;)
        {
            feedb_tap &= early_delay.Mask;
            size_t td{minz(early_delay.Mask+1 - feedb_tap, todo - i)};
            do {
                out[i] = mTempSamples[j][i] + early_delay.Line[feedb_tap++][j]*feedb_coeff;
                ++i;
            } while(--td);
        }
    }
    for(size_t j{0u};j < NUM_LINES;j++)
        early_delay.write(offset, NUM_LINES-1-j, mTempSamples[j].data(), todo);

    /* Also write the result back to the main delay line for the late reverb
     * stage to pick up at the appropriate time, appplying a scatter and
     * bounce to improve the initial diffusion in the late reverb.
     */
    const size_t late_feed_tap{offset - mLateFeedTap};
    VectorScatterRevDelayIn(main_delay, late_feed_tap, mixX, mixY, mEarlySamples, todo);
}
void ReverbState::earlyFaded(const size_t offset, const size_t todo, const ALfloat fade,
    const ALfloat fadeStep)
{
    const DelayLineI early_delay{mEarly.Delay};
    const DelayLineI main_delay{mDelay};
    const ALfloat mixX{mMixX};
    const ALfloat mixY{mMixY};

    ASSUME(todo > 0);

    for(size_t j{0u};j < NUM_LINES;j++)
    {
        size_t early_delay_tap0{offset - mEarlyDelayTap[j][0]};
        size_t early_delay_tap1{offset - mEarlyDelayTap[j][1]};
        const ALfloat oldCoeff{mEarlyDelayCoeff[j][0]};
        const ALfloat oldCoeffStep{-oldCoeff * fadeStep};
        const ALfloat newCoeffStep{mEarlyDelayCoeff[j][1] * fadeStep};
        ALfloat fadeCount{fade};

        for(size_t i{0u};i < todo;)
        {
            early_delay_tap0 &= main_delay.Mask;
            early_delay_tap1 &= main_delay.Mask;
            size_t td{minz(main_delay.Mask+1 - maxz(early_delay_tap0, early_delay_tap1), todo-i)};
            do {
                fadeCount += 1.0f;
                const ALfloat fade0{oldCoeff + oldCoeffStep*fadeCount};
                const ALfloat fade1{newCoeffStep*fadeCount};
                mTempSamples[j][i++] =
                    main_delay.Line[early_delay_tap0++][j]*fade0 +
                    main_delay.Line[early_delay_tap1++][j]*fade1;
            } while(--td);
        }
    }

    mEarly.VecAp.processFaded(mTempSamples, offset, mixX, mixY, fade, fadeStep, todo);

    for(size_t j{0u};j < NUM_LINES;j++)
    {
        size_t feedb_tap0{offset - mEarly.Offset[j][0]};
        size_t feedb_tap1{offset - mEarly.Offset[j][1]};
        const ALfloat feedb_oldCoeff{mEarly.Coeff[j][0]};
        const ALfloat feedb_oldCoeffStep{-feedb_oldCoeff * fadeStep};
        const ALfloat feedb_newCoeffStep{mEarly.Coeff[j][1] * fadeStep};
        float *out = mEarlySamples[j].data();
        ALfloat fadeCount{fade};

        for(size_t i{0u};i < todo;)
        {
            feedb_tap0 &= early_delay.Mask;
            feedb_tap1 &= early_delay.Mask;
            size_t td{minz(early_delay.Mask+1 - maxz(feedb_tap0, feedb_tap1), todo - i)};

            do {
                fadeCount += 1.0f;
                const ALfloat fade0{feedb_oldCoeff + feedb_oldCoeffStep*fadeCount};
                const ALfloat fade1{feedb_newCoeffStep*fadeCount};
                out[i] = mTempSamples[j][i] +
                    early_delay.Line[feedb_tap0++][j]*fade0 +
                    early_delay.Line[feedb_tap1++][j]*fade1;
                ++i;
            } while(--td);
        }
    }
    for(size_t j{0u};j < NUM_LINES;j++)
        early_delay.write(offset, NUM_LINES-1-j, mTempSamples[j].data(), todo);

    const size_t late_feed_tap{offset - mLateFeedTap};
    VectorScatterRevDelayIn(main_delay, late_feed_tap, mixX, mixY, mEarlySamples, todo);
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
void ReverbState::lateUnfaded(const size_t offset, const size_t todo)
{
    const DelayLineI late_delay{mLate.Delay};
    const DelayLineI main_delay{mDelay};
    const ALfloat mixX{mMixX};
    const ALfloat mixY{mMixY};

    ASSUME(todo > 0);

    /* First, load decorrelated samples from the main and feedback delay lines.
     * Filter the signal to apply its frequency-dependent decay.
     */
    for(size_t j{0u};j < NUM_LINES;j++)
    {
        size_t late_delay_tap{offset - mLateDelayTap[j][0]};
        size_t late_feedb_tap{offset - mLate.Offset[j][0]};
        const ALfloat midGain{mLate.T60[j].MidGain[0]};
        const ALfloat densityGain{mLate.DensityGain[0] * midGain};
        for(size_t i{0u};i < todo;)
        {
            late_delay_tap &= main_delay.Mask;
            late_feedb_tap &= late_delay.Mask;
            size_t td{minz(todo - i,
                minz(main_delay.Mask+1 - late_delay_tap, late_delay.Mask+1 - late_feedb_tap))};
            do {
                mTempSamples[j][i++] =
                    main_delay.Line[late_delay_tap++][j]*densityGain +
                    late_delay.Line[late_feedb_tap++][j]*midGain;
            } while(--td);
        }
        mLate.T60[j].process({mTempSamples[j].data(), todo});
    }

    /* Apply a vector all-pass to improve micro-surface diffusion, and write
     * out the results for mixing.
     */
    mLate.VecAp.processUnfaded(mTempSamples, offset, mixX, mixY, todo);
    for(size_t j{0u};j < NUM_LINES;j++)
        std::copy_n(mTempSamples[j].begin(), todo, mLateSamples[j].begin());

    /* Finally, scatter and bounce the results to refeed the feedback buffer. */
    VectorScatterRevDelayIn(late_delay, offset, mixX, mixY, mTempSamples, todo);
}
void ReverbState::lateFaded(const size_t offset, const size_t todo, const ALfloat fade,
    const ALfloat fadeStep)
{
    const DelayLineI late_delay{mLate.Delay};
    const DelayLineI main_delay{mDelay};
    const ALfloat mixX{mMixX};
    const ALfloat mixY{mMixY};

    ASSUME(todo > 0);

    for(size_t j{0u};j < NUM_LINES;j++)
    {
        const ALfloat oldMidGain{mLate.T60[j].MidGain[0]};
        const ALfloat midGain{mLate.T60[j].MidGain[1]};
        const ALfloat oldMidStep{-oldMidGain * fadeStep};
        const ALfloat midStep{midGain * fadeStep};
        const ALfloat oldDensityGain{mLate.DensityGain[0] * oldMidGain};
        const ALfloat densityGain{mLate.DensityGain[1] * midGain};
        const ALfloat oldDensityStep{-oldDensityGain * fadeStep};
        const ALfloat densityStep{densityGain * fadeStep};
        size_t late_delay_tap0{offset - mLateDelayTap[j][0]};
        size_t late_delay_tap1{offset - mLateDelayTap[j][1]};
        size_t late_feedb_tap0{offset - mLate.Offset[j][0]};
        size_t late_feedb_tap1{offset - mLate.Offset[j][1]};
        ALfloat fadeCount{fade};

        for(size_t i{0u};i < todo;)
        {
            late_delay_tap0 &= main_delay.Mask;
            late_delay_tap1 &= main_delay.Mask;
            late_feedb_tap0 &= late_delay.Mask;
            late_feedb_tap1 &= late_delay.Mask;
            size_t td{minz(todo - i,
                minz(main_delay.Mask+1 - maxz(late_delay_tap0, late_delay_tap1),
                    late_delay.Mask+1 - maxz(late_feedb_tap0, late_feedb_tap1)))};
            do {
                fadeCount += 1.0f;
                const ALfloat fade0{oldDensityGain + oldDensityStep*fadeCount};
                const ALfloat fade1{densityStep*fadeCount};
                const ALfloat gfade0{oldMidGain + oldMidStep*fadeCount};
                const ALfloat gfade1{midStep*fadeCount};
                mTempSamples[j][i++] =
                    main_delay.Line[late_delay_tap0++][j]*fade0 +
                    main_delay.Line[late_delay_tap1++][j]*fade1 +
                    late_delay.Line[late_feedb_tap0++][j]*gfade0 +
                    late_delay.Line[late_feedb_tap1++][j]*gfade1;
            } while(--td);
        }
        mLate.T60[j].process({mTempSamples[j].data(), todo});
    }

    mLate.VecAp.processFaded(mTempSamples, offset, mixX, mixY, fade, fadeStep, todo);
    for(size_t j{0u};j < NUM_LINES;j++)
        std::copy_n(mTempSamples[j].begin(), todo, mLateSamples[j].begin());

    VectorScatterRevDelayIn(late_delay, offset, mixX, mixY, mTempSamples, todo);
}

void ReverbState::process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut)
{
    size_t offset{mOffset};

    ASSUME(samplesToDo > 0);

    /* Convert B-Format to A-Format for processing. */
    const size_t numInput{samplesIn.size()};
    const al::span<float> tmpspan{mTempLine.data(), samplesToDo};
    for(size_t c{0u};c < NUM_LINES;c++)
    {
        std::fill(tmpspan.begin(), tmpspan.end(), 0.0f);
        MixRowSamples(tmpspan, {B2A[c], numInput}, samplesIn[0].data(), samplesIn[0].size());

        /* Band-pass the incoming samples and feed the initial delay line. */
        mFilter[c].Lp.process(tmpspan, tmpspan.begin());
        mFilter[c].Hp.process(tmpspan, tmpspan.begin());
        mDelay.write(offset, c, tmpspan.cbegin(), samplesToDo);
    }

    /* Process reverb for these samples. */
    if LIKELY(!mDoFading)
    {
        for(size_t base{0};base < samplesToDo;)
        {
            /* Calculate the number of samples we can do this iteration. */
            size_t todo{minz(samplesToDo - base, mMaxUpdate[0])};
            /* Some mixers require maintaining a 4-sample alignment, so ensure
             * that if it's not the last iteration.
             */
            if(base+todo < samplesToDo) todo &= ~size_t{3};
            ASSUME(todo > 0);

            /* Generate non-faded early reflections and late reverb. */
            earlyUnfaded(offset, todo);
            lateUnfaded(offset, todo);

            /* Finally, mix early reflections and late reverb. */
            (this->*mMixOut)(samplesOut, samplesToDo-base, base, todo);

            offset += todo;
            base += todo;
        }
    }
    else
    {
        const float fadeStep{1.0f / static_cast<float>(samplesToDo)};
        for(size_t base{0};base < samplesToDo;)
        {
            size_t todo{minz(samplesToDo - base, minz(mMaxUpdate[0], mMaxUpdate[1]))};
            if(base+todo < samplesToDo) todo &= ~size_t{3};
            ASSUME(todo > 0);

            /* Generate cross-faded early reflections and late reverb. */
            auto fadeCount = static_cast<ALfloat>(base);
            earlyFaded(offset, todo, fadeCount, fadeStep);
            lateFaded(offset, todo, fadeCount, fadeStep);

            (this->*mMixOut)(samplesOut, samplesToDo-base, base, todo);

            offset += todo;
            base += todo;
        }

        /* Update the cross-fading delay line taps. */
        for(size_t c{0u};c < NUM_LINES;c++)
        {
            mEarlyDelayTap[c][0] = mEarlyDelayTap[c][1];
            mEarlyDelayCoeff[c][0] = mEarlyDelayCoeff[c][1];
            mEarly.VecAp.Offset[c][0] = mEarly.VecAp.Offset[c][1];
            mEarly.Offset[c][0] = mEarly.Offset[c][1];
            mEarly.Coeff[c][0] = mEarly.Coeff[c][1];
            mLateDelayTap[c][0] = mLateDelayTap[c][1];
            mLate.VecAp.Offset[c][0] = mLate.VecAp.Offset[c][1];
            mLate.Offset[c][0] = mLate.Offset[c][1];
            mLate.T60[c].MidGain[0] = mLate.T60[c].MidGain[1];
        }
        mLate.DensityGain[0] = mLate.DensityGain[1];
        mMaxUpdate[0] = mMaxUpdate[1];
        mDoFading = false;
    }
    mOffset = offset;
}


void EAXReverb_setParami(EffectProps *props, ALCcontext *context, ALenum param, ALint val)
{
    switch(param)
    {
        case AL_EAXREVERB_DECAY_HFLIMIT:
            if(!(val >= AL_EAXREVERB_MIN_DECAY_HFLIMIT && val <= AL_EAXREVERB_MAX_DECAY_HFLIMIT))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb decay hflimit out of range");
            props->Reverb.DecayHFLimit = val != AL_FALSE;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid EAX reverb integer property 0x%04x",
                param);
    }
}
void EAXReverb_setParamiv(EffectProps *props, ALCcontext *context, ALenum param, const ALint *vals)
{ EAXReverb_setParami(props, context, param, vals[0]); }
void EAXReverb_setParamf(EffectProps *props, ALCcontext *context, ALenum param, ALfloat val)
{
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
            context->setError(AL_INVALID_ENUM, "Invalid EAX reverb float property 0x%04x", param);
    }
}
void EAXReverb_setParamfv(EffectProps *props, ALCcontext *context, ALenum param, const ALfloat *vals)
{
    switch(param)
    {
        case AL_EAXREVERB_REFLECTIONS_PAN:
            if(!(std::isfinite(vals[0]) && std::isfinite(vals[1]) && std::isfinite(vals[2])))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb reflections pan out of range");
            props->Reverb.ReflectionsPan[0] = vals[0];
            props->Reverb.ReflectionsPan[1] = vals[1];
            props->Reverb.ReflectionsPan[2] = vals[2];
            break;
        case AL_EAXREVERB_LATE_REVERB_PAN:
            if(!(std::isfinite(vals[0]) && std::isfinite(vals[1]) && std::isfinite(vals[2])))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "EAX Reverb late reverb pan out of range");
            props->Reverb.LateReverbPan[0] = vals[0];
            props->Reverb.LateReverbPan[1] = vals[1];
            props->Reverb.LateReverbPan[2] = vals[2];
            break;

        default:
            EAXReverb_setParamf(props, context, param, vals[0]);
            break;
    }
}

void EAXReverb_getParami(const EffectProps *props, ALCcontext *context, ALenum param, ALint *val)
{
    switch(param)
    {
        case AL_EAXREVERB_DECAY_HFLIMIT:
            *val = props->Reverb.DecayHFLimit;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid EAX reverb integer property 0x%04x",
                param);
    }
}
void EAXReverb_getParamiv(const EffectProps *props, ALCcontext *context, ALenum param, ALint *vals)
{ EAXReverb_getParami(props, context, param, vals); }
void EAXReverb_getParamf(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *val)
{
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
            context->setError(AL_INVALID_ENUM, "Invalid EAX reverb float property 0x%04x", param);
    }
}
void EAXReverb_getParamfv(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *vals)
{
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
            EAXReverb_getParamf(props, context, param, vals);
            break;
    }
}

DEFINE_ALEFFECT_VTABLE(EAXReverb);


struct ReverbStateFactory final : public EffectStateFactory {
    EffectState *create() override { return new ReverbState{}; }
    EffectProps getDefaultProps() const noexcept override;
    const EffectVtable *getEffectVtable() const noexcept override { return &EAXReverb_vtable; }
};

EffectProps ReverbStateFactory::getDefaultProps() const noexcept
{
    EffectProps props{};
    props.Reverb.Density   = AL_EAXREVERB_DEFAULT_DENSITY;
    props.Reverb.Diffusion = AL_EAXREVERB_DEFAULT_DIFFUSION;
    props.Reverb.Gain   = AL_EAXREVERB_DEFAULT_GAIN;
    props.Reverb.GainHF = AL_EAXREVERB_DEFAULT_GAINHF;
    props.Reverb.GainLF = AL_EAXREVERB_DEFAULT_GAINLF;
    props.Reverb.DecayTime    = AL_EAXREVERB_DEFAULT_DECAY_TIME;
    props.Reverb.DecayHFRatio = AL_EAXREVERB_DEFAULT_DECAY_HFRATIO;
    props.Reverb.DecayLFRatio = AL_EAXREVERB_DEFAULT_DECAY_LFRATIO;
    props.Reverb.ReflectionsGain   = AL_EAXREVERB_DEFAULT_REFLECTIONS_GAIN;
    props.Reverb.ReflectionsDelay  = AL_EAXREVERB_DEFAULT_REFLECTIONS_DELAY;
    props.Reverb.ReflectionsPan[0] = AL_EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ;
    props.Reverb.ReflectionsPan[1] = AL_EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ;
    props.Reverb.ReflectionsPan[2] = AL_EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ;
    props.Reverb.LateReverbGain   = AL_EAXREVERB_DEFAULT_LATE_REVERB_GAIN;
    props.Reverb.LateReverbDelay  = AL_EAXREVERB_DEFAULT_LATE_REVERB_DELAY;
    props.Reverb.LateReverbPan[0] = AL_EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ;
    props.Reverb.LateReverbPan[1] = AL_EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ;
    props.Reverb.LateReverbPan[2] = AL_EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ;
    props.Reverb.EchoTime  = AL_EAXREVERB_DEFAULT_ECHO_TIME;
    props.Reverb.EchoDepth = AL_EAXREVERB_DEFAULT_ECHO_DEPTH;
    props.Reverb.ModulationTime  = AL_EAXREVERB_DEFAULT_MODULATION_TIME;
    props.Reverb.ModulationDepth = AL_EAXREVERB_DEFAULT_MODULATION_DEPTH;
    props.Reverb.AirAbsorptionGainHF = AL_EAXREVERB_DEFAULT_AIR_ABSORPTION_GAINHF;
    props.Reverb.HFReference = AL_EAXREVERB_DEFAULT_HFREFERENCE;
    props.Reverb.LFReference = AL_EAXREVERB_DEFAULT_LFREFERENCE;
    props.Reverb.RoomRolloffFactor = AL_EAXREVERB_DEFAULT_ROOM_ROLLOFF_FACTOR;
    props.Reverb.DecayHFLimit = AL_EAXREVERB_DEFAULT_DECAY_HFLIMIT;
    return props;
}


void StdReverb_setParami(EffectProps *props, ALCcontext *context, ALenum param, ALint val)
{
    switch(param)
    {
        case AL_REVERB_DECAY_HFLIMIT:
            if(!(val >= AL_REVERB_MIN_DECAY_HFLIMIT && val <= AL_REVERB_MAX_DECAY_HFLIMIT))
                SETERR_RETURN(context, AL_INVALID_VALUE,, "Reverb decay hflimit out of range");
            props->Reverb.DecayHFLimit = val != AL_FALSE;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid reverb integer property 0x%04x", param);
    }
}
void StdReverb_setParamiv(EffectProps *props, ALCcontext *context, ALenum param, const ALint *vals)
{ StdReverb_setParami(props, context, param, vals[0]); }
void StdReverb_setParamf(EffectProps *props, ALCcontext *context, ALenum param, ALfloat val)
{
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
            context->setError(AL_INVALID_ENUM, "Invalid reverb float property 0x%04x", param);
    }
}
void StdReverb_setParamfv(EffectProps *props, ALCcontext *context, ALenum param, const ALfloat *vals)
{ StdReverb_setParamf(props, context, param, vals[0]); }

void StdReverb_getParami(const EffectProps *props, ALCcontext *context, ALenum param, ALint *val)
{
    switch(param)
    {
        case AL_REVERB_DECAY_HFLIMIT:
            *val = props->Reverb.DecayHFLimit;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid reverb integer property 0x%04x", param);
    }
}
void StdReverb_getParamiv(const EffectProps *props, ALCcontext *context, ALenum param, ALint *vals)
{ StdReverb_getParami(props, context, param, vals); }
void StdReverb_getParamf(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *val)
{
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
            context->setError(AL_INVALID_ENUM, "Invalid reverb float property 0x%04x", param);
    }
}
void StdReverb_getParamfv(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *vals)
{ StdReverb_getParamf(props, context, param, vals); }

DEFINE_ALEFFECT_VTABLE(StdReverb);


struct StdReverbStateFactory final : public EffectStateFactory {
    EffectState *create() override { return new ReverbState{}; }
    EffectProps getDefaultProps() const noexcept override;
    const EffectVtable *getEffectVtable() const noexcept override { return &StdReverb_vtable; }
};

EffectProps StdReverbStateFactory::getDefaultProps() const noexcept
{
    EffectProps props{};
    props.Reverb.Density   = AL_REVERB_DEFAULT_DENSITY;
    props.Reverb.Diffusion = AL_REVERB_DEFAULT_DIFFUSION;
    props.Reverb.Gain   = AL_REVERB_DEFAULT_GAIN;
    props.Reverb.GainHF = AL_REVERB_DEFAULT_GAINHF;
    props.Reverb.GainLF = 1.0f;
    props.Reverb.DecayTime    = AL_REVERB_DEFAULT_DECAY_TIME;
    props.Reverb.DecayHFRatio = AL_REVERB_DEFAULT_DECAY_HFRATIO;
    props.Reverb.DecayLFRatio = 1.0f;
    props.Reverb.ReflectionsGain   = AL_REVERB_DEFAULT_REFLECTIONS_GAIN;
    props.Reverb.ReflectionsDelay  = AL_REVERB_DEFAULT_REFLECTIONS_DELAY;
    props.Reverb.ReflectionsPan[0] = 0.0f;
    props.Reverb.ReflectionsPan[1] = 0.0f;
    props.Reverb.ReflectionsPan[2] = 0.0f;
    props.Reverb.LateReverbGain   = AL_REVERB_DEFAULT_LATE_REVERB_GAIN;
    props.Reverb.LateReverbDelay  = AL_REVERB_DEFAULT_LATE_REVERB_DELAY;
    props.Reverb.LateReverbPan[0] = 0.0f;
    props.Reverb.LateReverbPan[1] = 0.0f;
    props.Reverb.LateReverbPan[2] = 0.0f;
    props.Reverb.EchoTime  = 0.25f;
    props.Reverb.EchoDepth = 0.0f;
    props.Reverb.ModulationTime  = 0.25f;
    props.Reverb.ModulationDepth = 0.0f;
    props.Reverb.AirAbsorptionGainHF = AL_REVERB_DEFAULT_AIR_ABSORPTION_GAINHF;
    props.Reverb.HFReference = 5000.0f;
    props.Reverb.LFReference = 250.0f;
    props.Reverb.RoomRolloffFactor = AL_REVERB_DEFAULT_ROOM_ROLLOFF_FACTOR;
    props.Reverb.DecayHFLimit = AL_REVERB_DEFAULT_DECAY_HFLIMIT;
    return props;
}

} // namespace

EffectStateFactory *ReverbStateFactory_getFactory()
{
    static ReverbStateFactory ReverbFactory{};
    return &ReverbFactory;
}

EffectStateFactory *StdReverbStateFactory_getFactory()
{
    static StdReverbStateFactory ReverbFactory{};
    return &ReverbFactory;
}
