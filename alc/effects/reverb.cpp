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

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iterator>
#include <numeric>

#include "alc/effects/base.h"
#include "almalloc.h"
#include "alnumbers.h"
#include "alnumeric.h"
#include "alspan.h"
#include "core/ambidefs.h"
#include "core/bufferline.h"
#include "core/context.h"
#include "core/devformat.h"
#include "core/device.h"
#include "core/effectslot.h"
#include "core/filters/biquad.h"
#include "core/filters/splitter.h"
#include "core/mixer.h"
#include "core/mixer/defs.h"
#include "intrusive_ptr.h"
#include "opthelpers.h"
#include "vecmat.h"
#include "vector.h"

namespace {

using uint = unsigned int;

constexpr float MaxModulationTime{4.0f};
constexpr float DefaultModulationTime{0.25f};

#define MOD_FRACBITS 24
#define MOD_FRACONE  (1<<MOD_FRACBITS)
#define MOD_FRACMASK (MOD_FRACONE-1)


struct CubicFilter {
    static constexpr size_t sTableBits{8};
    static constexpr size_t sTableSteps{1 << sTableBits};
    static constexpr size_t sTableMask{sTableSteps - 1};

    std::array<float,sTableSteps*2 + 1> mFilter{};

    constexpr CubicFilter()
    {
        /* This creates a lookup table for a cubic spline filter, with 256
         * steps between samples. Only half the coefficients are needed, since
         * Coeff2 is just Coeff1 in reverse and Coeff3 is just Coeff0 in
         * reverse.
         */
        for(size_t i{0};i < sTableSteps;++i)
        {
            const double mu{static_cast<double>(i) / double{sTableSteps}};
            const double mu2{mu*mu}, mu3{mu2*mu};
            const double a0{-0.5*mu3 +      mu2 + -0.5*mu};
            const double a1{ 1.5*mu3 + -2.5*mu2           + 1.0f};
            mFilter[i] = static_cast<float>(a1);
            mFilter[sTableSteps+i] = static_cast<float>(a0);
        }
    }

    [[nodiscard]] constexpr auto getCoeff0(size_t i) const noexcept -> float
    { return mFilter[sTableSteps+i]; }
    [[nodiscard]] constexpr auto getCoeff1(size_t i) const noexcept -> float
    { return mFilter[i]; }
    [[nodiscard]] constexpr auto getCoeff2(size_t i) const noexcept -> float
    { return mFilter[sTableSteps-i]; }
    [[nodiscard]] constexpr auto getCoeff3(size_t i) const noexcept -> float
    { return mFilter[sTableSteps*2-i]; }
};
constexpr CubicFilter gCubicTable;


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


/* This coefficient is used to define the maximum frequency range controlled by
 * the modulation depth. The current value of 0.05 will allow it to swing from
 * 0.95x to 1.05x. This value must be below 1. At 1 it will cause the sampler
 * to stall on the downswing, and above 1 it will cause it to sample backwards.
 * The value 0.05 seems be nearest to Creative hardware behavior.
 */
constexpr float MODULATION_DEPTH_COEFF{0.05f};


/* The B-Format to (W-normalized) A-Format conversion matrix. This produces a
 * tetrahedral array of discrete signals (boosted by a factor of sqrt(3), to
 * reduce the error introduced in the conversion).
 */
alignas(16) constexpr std::array<std::array<float,NUM_LINES>,NUM_LINES> B2A{{
    {{ 0.5f,  0.5f,  0.5f,  0.5f }},
    {{ 0.5f, -0.5f, -0.5f,  0.5f }},
    {{ 0.5f,  0.5f, -0.5f, -0.5f }},
    {{ 0.5f, -0.5f,  0.5f, -0.5f }}
}};

/* Converts (W-normalized) A-Format to B-Format for early reflections (scaled
 * by 1/sqrt(3) to compensate for the boost in the B2A matrix).
 */
alignas(16) constexpr std::array<std::array<float,NUM_LINES>,NUM_LINES> EarlyA2B{{
    {{ 0.5f,  0.5f,  0.5f,  0.5f }},
    {{ 0.5f, -0.5f,  0.5f, -0.5f }},
    {{ 0.5f, -0.5f, -0.5f,  0.5f }},
    {{ 0.5f,  0.5f, -0.5f, -0.5f }}
}};

/* Converts (W-normalized) A-Format to B-Format for late reverb (scaled
 * by 1/sqrt(3) to compensate for the boost in the B2A matrix). The response
 * is rotated around Z (ambisonic X) so that the front lines are placed
 * horizontally in front, and the rear lines are placed vertically in back.
 */
constexpr auto InvSqrt2 = static_cast<float>(1.0/al::numbers::sqrt2);
alignas(16) constexpr std::array<std::array<float,NUM_LINES>,NUM_LINES> LateA2B{{
    {{ 0.5f,  0.5f,  0.5f,  0.5f }},
    {{ InvSqrt2, -InvSqrt2,  0.0f,  0.0f }},
    {{ 0.0f,  0.0f,  InvSqrt2, -InvSqrt2 }},
    {{ 0.5f,  0.5f, -0.5f, -0.5f }}
}};

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
constexpr float DENSITY_SCALE{125000.0f};

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
constexpr std::array<float,NUM_LINES> EARLY_TAP_LENGTHS{{
    0.0000000e+0f, 2.0213520e-4f, 4.2531060e-4f, 6.7171600e-4f
}};

/* The early all-pass filter lengths are based on the early tap lengths:
 *
 *     A_i = R_i / a
 *
 * Where a is the approximate maximum all-pass cycle limit (20).
 */
constexpr std::array<float,NUM_LINES> EARLY_ALLPASS_LENGTHS{{
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
constexpr std::array<float,NUM_LINES> EARLY_LINE_LENGTHS{{
    0.0000000e+0f, 4.9281100e-4f, 9.3916180e-4f, 1.3434322e-3f
}};

/* The late all-pass filter lengths are based on the late line lengths:
 *
 *     A_i = (5 / 3) L_i / r_1
 */
constexpr std::array<float,NUM_LINES> LATE_ALLPASS_LENGTHS{{
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
constexpr std::array<float,NUM_LINES> LATE_LINE_LENGTHS{{
    1.9419362e-3f, 2.4466860e-3f, 3.3791220e-3f, 3.8838720e-3f
}};


using ReverbUpdateLine = std::array<float,MAX_UPDATE_SAMPLES>;

struct DelayLineI {
    /* The delay lines use interleaved samples, with the lengths being powers
     * of 2 to allow the use of bit-masking instead of a modulus for wrapping.
     */
    size_t Mask{0u};
    std::array<float,NUM_LINES> *Line;

    /* Given the allocated sample buffer, this function updates each delay line
     * offset.
     */
    void realizeLineOffset(std::array<float,NUM_LINES> *sampleBuffer) noexcept
    { Line = sampleBuffer; }

    /* Calculate the length of a delay line and store its mask and offset. */
    size_t calcLineLength(const float length, const float frequency, const uint extra)
    {
        /* All line lengths are powers of 2, calculated from their lengths in
         * seconds, rounded up.
         */
        uint samples{float2uint(std::ceil(length*frequency))};
        samples = NextPowerOf2(samples + extra);

        /* All lines share a single sample buffer. */
        Mask = samples - 1;

        /* Return the sample count for accumulation. */
        return samples;
    }

    void write(size_t offset, const size_t c, const float *RESTRICT in, const size_t count) const noexcept
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

    /* Writes the given input lines to the delay buffer, applying a geometric
     * reflection. This effectively applies the matrix
     *
     * [ -1/2 +1/2 +1/2 +1/2 ]
     * [ +1/2 -1/2 +1/2 +1/2 ]
     * [ +1/2 +1/2 -1/2 +1/2 ]
     * [ +1/2 +1/2 +1/2 -1/2 ]
     *
     * to the four input lines when writing to the delay buffer. The effect on
     * the B-Format signal is negating X,Y,Z, moving each response to its
     * spatially opposite location.
     */
    void writeReflected(size_t offset, const al::span<const ReverbUpdateLine,NUM_LINES> in,
        const size_t count) const noexcept
    {
        ASSUME(count > 0);
        for(size_t i{0u};i < count;)
        {
            offset &= Mask;
            size_t td{minz(Mask+1 - offset, count - i)};
            do {
                const std::array src{in[0][i], in[1][i], in[2][i], in[3][i]};
                ++i;

                Line[offset][0] = (         src[1] + src[2] + src[3] - src[0]) * 0.5f;
                Line[offset][1] = (src[0] +          src[2] + src[3] - src[1]) * 0.5f;
                Line[offset][2] = (src[0] + src[1] +          src[3] - src[2]) * 0.5f;
                Line[offset][3] = (src[0] + src[1] + src[2]          - src[3]) * 0.5f;
                ++offset;
            } while(--td);
        }
    }
};

struct VecAllpass {
    DelayLineI Delay;
    float Coeff{0.0f};
    std::array<size_t,NUM_LINES> Offset{};

    void process(const al::span<ReverbUpdateLine,NUM_LINES> samples, size_t offset,
        const float xCoeff, const float yCoeff, const size_t todo);
};

struct T60Filter {
    /* Two filters are used to adjust the signal. One to control the low
     * frequencies, and one to control the high frequencies.
     */
    float MidGain{0.0f};
    BiquadFilter HFFilter, LFFilter;

    void calcCoeffs(const float length, const float lfDecayTime, const float mfDecayTime,
        const float hfDecayTime, const float lf0norm, const float hf0norm);

    /* Applies the two T60 damping filter sections. */
    void process(const al::span<float> samples)
    { DualBiquad{HFFilter, LFFilter}.process(samples, samples.data()); }

    void clear() noexcept { HFFilter.clear(); LFFilter.clear(); }
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
    std::array<size_t,NUM_LINES> Offset{};
    std::array<float,NUM_LINES> Coeff{};

    /* The gain for each output channel based on 3D panning. */
    std::array<std::array<float,MaxAmbiChannels>,NUM_LINES> CurrentGains{};
    std::array<std::array<float,MaxAmbiChannels>,NUM_LINES> TargetGains{};

    void updateLines(const float density_mult, const float diffusion, const float decayTime,
        const float frequency);
};


struct Modulation {
    /* The vibrato time is tracked with an index over a (MOD_FRACONE)
     * normalized range.
     */
    uint Index{}, Step{};

    /* The depth of frequency change, in samples. */
    float Depth{};

    std::array<float,MAX_UPDATE_SAMPLES> ModDelays{};

    void updateModulator(float modTime, float modDepth, float frequency);

    void calcDelays(size_t todo);
};

struct LateReverb {
    /* A recursive delay line is used fill in the reverb tail. */
    DelayLineI Delay;
    std::array<size_t,NUM_LINES> Offset{};

    /* Attenuation to compensate for the modal density and decay rate of the
     * late lines.
     */
    float DensityGain{0.0f};

    /* T60 decay filters are used to simulate absorption. */
    std::array<T60Filter,NUM_LINES> T60;

    Modulation Mod;

    /* A Gerzon vector all-pass filter is used to simulate diffusion. */
    VecAllpass VecAp;

    /* The gain for each output channel based on 3D panning. */
    std::array<std::array<float,MaxAmbiChannels>,NUM_LINES> CurrentGains{};
    std::array<std::array<float,MaxAmbiChannels>,NUM_LINES> TargetGains{};

    void updateLines(const float density_mult, const float diffusion, const float lfDecayTime,
        const float mfDecayTime, const float hfDecayTime, const float lf0norm,
        const float hf0norm, const float frequency);

    void clear() noexcept
    {
        for(auto &filter : T60)
            filter.clear();
    }
};

struct ReverbPipeline {
    /* Master effect filters */
    struct FilterPair {
        BiquadFilter Lp;
        BiquadFilter Hp;
    };
    std::array<FilterPair,NUM_LINES> mFilter;

    /* Core delay line (early reflections and late reverb tap from this). */
    DelayLineI mEarlyDelayIn;
    DelayLineI mLateDelayIn;

    /* Tap points for early reflection delay. */
    std::array<std::array<size_t,2>,NUM_LINES> mEarlyDelayTap{};
    std::array<float,NUM_LINES> mEarlyDelayCoeff{};

    /* Tap points for late reverb feed and delay. */
    std::array<std::array<size_t,2>,NUM_LINES> mLateDelayTap{};

    /* Coefficients for the all-pass and line scattering matrices. */
    float mMixX{0.0f};
    float mMixY{0.0f};

    EarlyReflections mEarly;

    LateReverb mLate;

    std::array<std::array<BandSplitter,NUM_LINES>,2> mAmbiSplitter;

    size_t mFadeSampleCount{1};

    void updateDelayLine(const float earlyDelay, const float lateDelay, const float density_mult,
        const float decayTime, const float frequency);
    void update3DPanning(const al::span<const float,3> ReflectionsPan,
        const al::span<const float,3> LateReverbPan, const float earlyGain, const float lateGain,
        const bool doUpmix, const MixParams *mainMix);

    void processEarly(size_t offset, const size_t samplesToDo,
        const al::span<ReverbUpdateLine,NUM_LINES> tempSamples,
        const al::span<FloatBufferLine,NUM_LINES> outSamples);
    void processLate(size_t offset, const size_t samplesToDo,
        const al::span<ReverbUpdateLine,NUM_LINES> tempSamples,
        const al::span<FloatBufferLine,NUM_LINES> outSamples);

    void clear() noexcept
    {
        for(auto &filter : mFilter)
        {
            filter.Lp.clear();
            filter.Hp.clear();
        }
        mLate.clear();
        for(auto &filters : mAmbiSplitter)
        {
            for(auto &filter : filters)
                filter.clear();
        }
    }
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
        float Density{1.0f};
        float Diffusion{1.0f};
        float DecayTime{1.49f};
        float HFDecayTime{0.83f * 1.49f};
        float LFDecayTime{1.0f * 1.49f};
        float ModulationTime{0.25f};
        float ModulationDepth{0.0f};
        float HFReference{5000.0f};
        float LFReference{250.0f};
    } mParams;

    enum PipelineState : uint8_t {
        DeviceClear,
        StartFade,
        Fading,
        Cleanup,
        Normal,
    };
    PipelineState mPipelineState{DeviceClear};
    bool mCurrentPipeline{false};

    std::array<ReverbPipeline,2> mPipelines;

    /* The current write offset for all delay lines. */
    size_t mOffset{};

    /* Temporary storage used when processing. */
    union {
        alignas(16) FloatBufferLine mTempLine{};
        alignas(16) std::array<ReverbUpdateLine,NUM_LINES> mTempSamples;
    };
    alignas(16) std::array<FloatBufferLine,NUM_LINES> mEarlySamples{};
    alignas(16) std::array<FloatBufferLine,NUM_LINES> mLateSamples{};

    std::array<float,MaxAmbiOrder+1> mOrderScales{};

    bool mUpmixOutput{false};


    void MixOutPlain(ReverbPipeline &pipeline, const al::span<FloatBufferLine> samplesOut,
        const size_t todo)
    {
        ASSUME(todo > 0);

        /* When not upsampling, the panning gains convert to B-Format and pan
         * at the same time.
         */
        for(size_t c{0u};c < NUM_LINES;c++)
        {
            const al::span<float> tmpspan{mEarlySamples[c].data(), todo};
            MixSamples(tmpspan, samplesOut, pipeline.mEarly.CurrentGains[c].data(),
                pipeline.mEarly.TargetGains[c].data(), todo, 0);
        }
        for(size_t c{0u};c < NUM_LINES;c++)
        {
            const al::span<float> tmpspan{mLateSamples[c].data(), todo};
            MixSamples(tmpspan, samplesOut, pipeline.mLate.CurrentGains[c].data(),
                pipeline.mLate.TargetGains[c].data(), todo, 0);
        }
    }

    void MixOutAmbiUp(ReverbPipeline &pipeline, const al::span<FloatBufferLine> samplesOut,
        const size_t todo)
    {
        ASSUME(todo > 0);

        auto DoMixRow = [](const al::span<float> OutBuffer, const al::span<const float,4> Gains,
            const float *InSamples, const size_t InStride)
        {
            std::fill(OutBuffer.begin(), OutBuffer.end(), 0.0f);
            for(const float gain : Gains)
            {
                const float *RESTRICT input{al::assume_aligned<16>(InSamples)};
                InSamples += InStride;

                if(!(std::fabs(gain) > GainSilenceThreshold))
                    continue;

                auto mix_sample = [gain](const float sample, const float in) noexcept -> float
                { return sample + in*gain; };
                std::transform(OutBuffer.begin(), OutBuffer.end(), input, OutBuffer.begin(),
                    mix_sample);
            }
        };

        /* When upsampling, the B-Format conversion needs to be done separately
         * so the proper HF scaling can be applied to each B-Format channel.
         * The panning gains then pan and upsample the B-Format channels.
         */
        const al::span<float> tmpspan{al::assume_aligned<16>(mTempLine.data()), todo};
        for(size_t c{0u};c < NUM_LINES;c++)
        {
            DoMixRow(tmpspan, EarlyA2B[c], mEarlySamples[0].data(), mEarlySamples[0].size());

            /* Apply scaling to the B-Format's HF response to "upsample" it to
             * higher-order output.
             */
            const float hfscale{(c==0) ? mOrderScales[0] : mOrderScales[1]};
            pipeline.mAmbiSplitter[0][c].processHfScale(tmpspan, hfscale);

            MixSamples(tmpspan, samplesOut, pipeline.mEarly.CurrentGains[c].data(),
                pipeline.mEarly.TargetGains[c].data(), todo, 0);
        }
        for(size_t c{0u};c < NUM_LINES;c++)
        {
            DoMixRow(tmpspan, LateA2B[c], mLateSamples[0].data(), mLateSamples[0].size());

            const float hfscale{(c==0) ? mOrderScales[0] : mOrderScales[1]};
            pipeline.mAmbiSplitter[1][c].processHfScale(tmpspan, hfscale);

            MixSamples(tmpspan, samplesOut, pipeline.mLate.CurrentGains[c].data(),
                pipeline.mLate.TargetGains[c].data(), todo, 0);
        }
    }

    void mixOut(ReverbPipeline &pipeline, const al::span<FloatBufferLine> samplesOut, const size_t todo)
    {
        if(mUpmixOutput)
            MixOutAmbiUp(pipeline, samplesOut, todo);
        else
            MixOutPlain(pipeline, samplesOut, todo);
    }

    void allocLines(const float frequency);

    void deviceUpdate(const DeviceBase *device, const BufferStorage *buffer) override;
    void update(const ContextBase *context, const EffectSlot *slot, const EffectProps *props,
        const EffectTarget target) override;
    void process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn,
        const al::span<FloatBufferLine> samplesOut) override;
};

/**************************************
 *  Device Update                     *
 **************************************/

inline float CalcDelayLengthMult(float density)
{ return maxf(5.0f, std::cbrt(density*DENSITY_SCALE)); }

/* Calculates the delay line metrics and allocates the shared sample buffer
 * for all lines given the sample rate (frequency).
 */
void ReverbState::allocLines(const float frequency)
{
    /* Multiplier for the maximum density value, i.e. density=1, which is
     * actually the least density...
     */
    const float multiplier{CalcDelayLengthMult(1.0f)};

    /* The modulator's line length is calculated from the maximum modulation
     * time and depth coefficient, and halfed for the low-to-high frequency
     * swing.
     */
    static constexpr float max_mod_delay{MaxModulationTime*MODULATION_DEPTH_COEFF / 2.0f};

    std::array<size_t,12> lineoffsets{};
    size_t oidx{0};

    size_t totalSamples{0u};
    for(auto &pipeline : mPipelines)
    {
        /* The main delay length includes the maximum early reflection delay,
         * the largest early tap width, the maximum late reverb delay, and the
         * largest late tap width.  Finally, it must also be extended by the
         * update size (BufferLineSize) for block processing.
         */
        float length{ReverbMaxReflectionsDelay + EARLY_TAP_LENGTHS.back()*multiplier};
        size_t count{pipeline.mEarlyDelayIn.calcLineLength(length, frequency, BufferLineSize)};
        lineoffsets[oidx++] = totalSamples;
        totalSamples += count;

        static constexpr float LateDiffAvg{(LATE_LINE_LENGTHS.back()-LATE_LINE_LENGTHS.front()) /
            float{NUM_LINES}};
        length = ReverbMaxLateReverbDelay + LateDiffAvg*multiplier;
        count = pipeline.mLateDelayIn.calcLineLength(length, frequency, BufferLineSize);
        lineoffsets[oidx++] = totalSamples;
        totalSamples += count;

        /* The early vector all-pass line. */
        length = EARLY_ALLPASS_LENGTHS.back() * multiplier;
        count = pipeline.mEarly.VecAp.Delay.calcLineLength(length, frequency, 0);
        lineoffsets[oidx++] = totalSamples;
        totalSamples += count;

        /* The early reflection line. */
        length = EARLY_LINE_LENGTHS.back() * multiplier;
        count = pipeline.mEarly.Delay.calcLineLength(length, frequency, MAX_UPDATE_SAMPLES);
        lineoffsets[oidx++] = totalSamples;
        totalSamples += count;

        /* The late vector all-pass line. */
        length = LATE_ALLPASS_LENGTHS.back() * multiplier;
        count = pipeline.mLate.VecAp.Delay.calcLineLength(length, frequency, 0);
        lineoffsets[oidx++] = totalSamples;
        totalSamples += count;

        /* The late delay lines are calculated from the largest maximum density
         * line length, and the maximum modulation delay. Four additional
         * samples are needed for resampling the modulator delay.
         */
        length = LATE_LINE_LENGTHS.back()*multiplier + max_mod_delay;
        count = pipeline.mLate.Delay.calcLineLength(length, frequency, 4);
        lineoffsets[oidx++] = totalSamples;
        totalSamples += count;
    }
    assert(oidx == lineoffsets.size());

    if(totalSamples != mSampleBuffer.size())
        decltype(mSampleBuffer)(totalSamples).swap(mSampleBuffer);

    /* Clear the sample buffer. */
    std::fill(mSampleBuffer.begin(), mSampleBuffer.end(), decltype(mSampleBuffer)::value_type{});

    /* Update all delays to reflect the new sample buffer. */
    oidx = 0;
    for(auto &pipeline : mPipelines)
    {
        pipeline.mEarlyDelayIn.realizeLineOffset(mSampleBuffer.data() + lineoffsets[oidx++]);
        pipeline.mLateDelayIn.realizeLineOffset(mSampleBuffer.data() + lineoffsets[oidx++]);
        pipeline.mEarly.VecAp.Delay.realizeLineOffset(mSampleBuffer.data() + lineoffsets[oidx++]);
        pipeline.mEarly.Delay.realizeLineOffset(mSampleBuffer.data() + lineoffsets[oidx++]);
        pipeline.mLate.VecAp.Delay.realizeLineOffset(mSampleBuffer.data() + lineoffsets[oidx++]);
        pipeline.mLate.Delay.realizeLineOffset(mSampleBuffer.data() + lineoffsets[oidx++]);
    }
}

void ReverbState::deviceUpdate(const DeviceBase *device, const BufferStorage*)
{
    const auto frequency = static_cast<float>(device->Frequency);

    /* Allocate the delay lines. */
    allocLines(frequency);

    for(auto &pipeline : mPipelines)
    {
        /* Clear filters and gain coefficients since the delay lines were all
         * just cleared (if not reallocated).
         */
        for(auto &filter : pipeline.mFilter)
        {
            filter.Lp.clear();
            filter.Hp.clear();
        }

        pipeline.mEarlyDelayCoeff.fill(0.0f);

        pipeline.mLate.DensityGain = 0.0f;
        for(auto &t60 : pipeline.mLate.T60)
        {
            t60.MidGain = 0.0f;
            t60.HFFilter.clear();
            t60.LFFilter.clear();
        }

        pipeline.mLate.Mod.Index = 0;
        pipeline.mLate.Mod.Step = 1;
        pipeline.mLate.Mod.Depth = 0.0f;

        for(auto &gains : pipeline.mEarly.CurrentGains)
            gains.fill(0.0f);
        for(auto &gains : pipeline.mEarly.TargetGains)
            gains.fill(0.0f);
        for(auto &gains : pipeline.mLate.CurrentGains)
            gains.fill(0.0f);
        for(auto &gains : pipeline.mLate.TargetGains)
            gains.fill(0.0f);
    }
    mPipelineState = DeviceClear;

    /* Reset offset base. */
    mOffset = 0;

    if(device->mAmbiOrder > 1)
    {
        mUpmixOutput = true;
        mOrderScales = AmbiScale::GetHFOrderScales(1, device->mAmbiOrder, device->m2DMixing);
    }
    else
    {
        mUpmixOutput = false;
        mOrderScales.fill(1.0f);
    }
    mPipelines[0].mAmbiSplitter[0][0].init(device->mXOverFreq / frequency);
    for(auto &pipeline : mPipelines)
    {
        std::fill(pipeline.mAmbiSplitter[0].begin(), pipeline.mAmbiSplitter[0].end(),
            pipeline.mAmbiSplitter[0][0]);
        std::fill(pipeline.mAmbiSplitter[1].begin(), pipeline.mAmbiSplitter[1].end(),
            pipeline.mAmbiSplitter[0][0]);
    }
}

/**************************************
 *  Effect Update                     *
 **************************************/

/* Calculate a decay coefficient given the length of each cycle and the time
 * until the decay reaches -60 dB.
 */
inline float CalcDecayCoeff(const float length, const float decayTime)
{ return std::pow(ReverbDecayGain, length/decayTime); }

/* Calculate a decay length from a coefficient and the time until the decay
 * reaches -60 dB.
 */
inline float CalcDecayLength(const float coeff, const float decayTime)
{
    constexpr float log10_decaygain{-3.0f/*std::log10(ReverbDecayGain)*/};
    return std::log10(coeff) * decayTime / log10_decaygain;
}

/* Calculate an attenuation to be applied to the input of any echo models to
 * compensate for modal density and decay time.
 */
inline float CalcDensityGain(const float a)
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
inline void CalcMatrixCoeffs(const float diffusion, float *x, float *y)
{
    /* The matrix is of order 4, so n is sqrt(4 - 1). */
    constexpr float n{al::numbers::sqrt3_v<float>};
    const float t{diffusion * std::atan(n)};

    /* Calculate the first mixing matrix coefficient. */
    *x = std::cos(t);
    /* Calculate the second mixing matrix coefficient. */
    *y = std::sin(t) / n;
}

/* Calculate the limited HF ratio for use with the late reverb low-pass
 * filters.
 */
float CalcLimitedHfRatio(const float hfRatio, const float airAbsorptionGainHF,
    const float decayTime)
{
    /* Find the attenuation due to air absorption in dB (converting delay
     * time to meters using the speed of sound).  Then reversing the decay
     * equation, solve for HF ratio.  The delay length is cancelled out of
     * the equation, so it can be calculated once for all lines.
     */
    float limitRatio{1.0f / SpeedOfSoundMetersPerSec /
        CalcDecayLength(airAbsorptionGainHF, decayTime)};

    /* Using the limit calculated above, apply the upper bound to the HF ratio. */
    return minf(limitRatio, hfRatio);
}


/* Calculates the 3-band T60 damping coefficients for a particular delay line
 * of specified length, using a combination of two shelf filter sections given
 * decay times for each band split at two reference frequencies.
 */
void T60Filter::calcCoeffs(const float length, const float lfDecayTime,
    const float mfDecayTime, const float hfDecayTime, const float lf0norm,
    const float hf0norm)
{
    const float mfGain{CalcDecayCoeff(length, mfDecayTime)};
    const float lfGain{CalcDecayCoeff(length, lfDecayTime) / mfGain};
    const float hfGain{CalcDecayCoeff(length, hfDecayTime) / mfGain};

    MidGain = mfGain;
    LFFilter.setParamsFromSlope(BiquadType::LowShelf, lf0norm, lfGain, 1.0f);
    HFFilter.setParamsFromSlope(BiquadType::HighShelf, hf0norm, hfGain, 1.0f);
}

/* Update the early reflection line lengths and gain coefficients. */
void EarlyReflections::updateLines(const float density_mult, const float diffusion,
    const float decayTime, const float frequency)
{
    /* Calculate the all-pass feed-back/forward coefficient. */
    VecAp.Coeff = diffusion*diffusion * InvSqrt2;

    for(size_t i{0u};i < NUM_LINES;i++)
    {
        /* Calculate the delay length of each all-pass line. */
        float length{EARLY_ALLPASS_LENGTHS[i] * density_mult};
        VecAp.Offset[i] = float2uint(length * frequency);

        /* Calculate the delay length of each delay line. */
        length = EARLY_LINE_LENGTHS[i] * density_mult;
        Offset[i] = float2uint(length * frequency);

        /* Calculate the gain (coefficient) for each line. */
        Coeff[i] = CalcDecayCoeff(length, decayTime);
    }
}

/* Update the EAX modulation step and depth. Keep in mind that this kind of
 * vibrato is additive and not multiplicative as one may expect. The downswing
 * will sound stronger than the upswing.
 */
void Modulation::updateModulator(float modTime, float modDepth, float frequency)
{
    /* Modulation is calculated in two parts.
     *
     * The modulation time effects the sinus rate, altering the speed of
     * frequency changes. An index is incremented for each sample with an
     * appropriate step size to generate an LFO, which will vary the feedback
     * delay over time.
     */
    Step = maxu(fastf2u(MOD_FRACONE / (frequency * modTime)), 1);

    /* The modulation depth effects the amount of frequency change over the
     * range of the sinus. It needs to be scaled by the modulation time so that
     * a given depth produces a consistent change in frequency over all ranges
     * of time. Since the depth is applied to a sinus value, it needs to be
     * halved once for the sinus range and again for the sinus swing in time
     * (half of it is spent decreasing the frequency, half is spent increasing
     * it).
     */
    if(modTime >= DefaultModulationTime)
    {
        /* To cancel the effects of a long period modulation on the late
         * reverberation, the amount of pitch should be varied (decreased)
         * according to the modulation time. The natural form is varying
         * inversely, in fact resulting in an invariant.
         */
        Depth = MODULATION_DEPTH_COEFF / 4.0f * DefaultModulationTime * modDepth * frequency;
    }
    else
        Depth = MODULATION_DEPTH_COEFF / 4.0f * modTime * modDepth * frequency;
}

/* Update the late reverb line lengths and T60 coefficients. */
void LateReverb::updateLines(const float density_mult, const float diffusion,
    const float lfDecayTime, const float mfDecayTime, const float hfDecayTime,
    const float lf0norm, const float hf0norm, const float frequency)
{
    /* Scaling factor to convert the normalized reference frequencies from
     * representing 0...freq to 0...max_reference.
     */
    constexpr float MaxHFReference{20000.0f};
    const float norm_weight_factor{frequency / MaxHFReference};

    const float late_allpass_avg{
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
    float length{std::accumulate(LATE_LINE_LENGTHS.begin(), LATE_LINE_LENGTHS.end(), 0.0f) /
        float{NUM_LINES} + late_allpass_avg};
    length *= density_mult;
    /* The density gain calculation uses an average decay time weighted by
     * approximate bandwidth. This attempts to compensate for losses of energy
     * that reduce decay time due to scattering into highly attenuated bands.
     */
    const float decayTimeWeighted{
        lf0norm*norm_weight_factor*lfDecayTime +
        (hf0norm - lf0norm)*norm_weight_factor*mfDecayTime +
        (1.0f - hf0norm*norm_weight_factor)*hfDecayTime};
    DensityGain = CalcDensityGain(CalcDecayCoeff(length, decayTimeWeighted));

    /* Calculate the all-pass feed-back/forward coefficient. */
    VecAp.Coeff = diffusion*diffusion * InvSqrt2;

    for(size_t i{0u};i < NUM_LINES;i++)
    {
        /* Calculate the delay length of each all-pass line. */
        length = LATE_ALLPASS_LENGTHS[i] * density_mult;
        VecAp.Offset[i] = float2uint(length * frequency);

        /* Calculate the delay length of each feedback delay line. A cubic
         * resampler is used for modulation on the feedback delay, which
         * includes one sample of delay. Reduce by one to compensate.
         */
        length = LATE_LINE_LENGTHS[i] * density_mult;
        Offset[i] = maxu(float2uint(length*frequency + 0.5f), 1u) - 1u;

        /* Approximate the absorption that the vector all-pass would exhibit
         * given the current diffusion so we don't have to process a full T60
         * filter for each of its four lines. Also include the average
         * modulation delay (depth is half the max delay in samples).
         */
        length += lerpf(LATE_ALLPASS_LENGTHS[i], late_allpass_avg, diffusion)*density_mult +
            Mod.Depth/frequency;

        /* Calculate the T60 damping coefficients for each line. */
        T60[i].calcCoeffs(length, lfDecayTime, mfDecayTime, hfDecayTime, lf0norm, hf0norm);
    }
}


/* Update the offsets for the main effect delay line. */
void ReverbPipeline::updateDelayLine(const float earlyDelay, const float lateDelay,
    const float density_mult, const float decayTime, const float frequency)
{
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
        float length{EARLY_TAP_LENGTHS[i]*density_mult};
        mEarlyDelayTap[i][1] = float2uint((earlyDelay+length) * frequency);
        mEarlyDelayCoeff[i] = CalcDecayCoeff(length, decayTime);

        /* Reduce the late delay tap by the shortest early delay line length to
         * compensate for the late line input being fed by the delayed early
         * output.
         */
        length = (LATE_LINE_LENGTHS[i] - LATE_LINE_LENGTHS.front())/float{NUM_LINES}*density_mult +
            lateDelay;
        mLateDelayTap[i][1] = float2uint(length * frequency);
    }
}

/* Creates a transform matrix given a reverb vector. The vector pans the reverb
 * reflections toward the given direction, using its magnitude (up to 1) as a
 * focal strength. This function results in a B-Format transformation matrix
 * that spatially focuses the signal in the desired direction.
 */
std::array<std::array<float,4>,4> GetTransformFromVector(const al::span<const float,3> vec)
{
    /* Normalize the panning vector according to the N3D scale, which has an
     * extra sqrt(3) term on the directional components. Converting from OpenAL
     * to B-Format also requires negating X (ACN 1) and Z (ACN 3). Note however
     * that the reverb panning vectors use left-handed coordinates, unlike the
     * rest of OpenAL which use right-handed. This is fixed by negating Z,
     * which cancels out with the B-Format Z negation.
     */
    std::array<float,3> norm;
    float mag{std::sqrt(vec[0]*vec[0] + vec[1]*vec[1] + vec[2]*vec[2])};
    if(mag > 1.0f)
    {
        const float scale{al::numbers::sqrt3_v<float> / mag};
        norm[0] = vec[0] * -scale;
        norm[1] = vec[1] * scale;
        norm[2] = vec[2] * scale;
        mag = 1.0f;
    }
    else
    {
        /* If the magnitude is less than or equal to 1, just apply the sqrt(3)
         * term. There's no need to renormalize the magnitude since it would
         * just be reapplied in the matrix.
         */
        norm[0] = vec[0] * -al::numbers::sqrt3_v<float>;
        norm[1] = vec[1] * al::numbers::sqrt3_v<float>;
        norm[2] = vec[2] * al::numbers::sqrt3_v<float>;
    }

    return std::array<std::array<float,4>,4>{{
        {{1.0f,   0.0f,    0.0f,   0.0f}},
        {{norm[0], 1.0f-mag, 0.0f, 0.0f}},
        {{norm[1], 0.0f, 1.0f-mag, 0.0f}},
        {{norm[2], 0.0f, 0.0f, 1.0f-mag}}
    }};
}

/* Update the early and late 3D panning gains. */
void ReverbPipeline::update3DPanning(const al::span<const float,3> ReflectionsPan,
    const al::span<const float,3> LateReverbPan, const float earlyGain, const float lateGain,
    const bool doUpmix, const MixParams *mainMix)
{
    /* Create matrices that transform a B-Format signal according to the
     * panning vectors.
     */
    const std::array<std::array<float,4>,4> earlymat{GetTransformFromVector(ReflectionsPan)};
    const std::array<std::array<float,4>,4> latemat{GetTransformFromVector(LateReverbPan)};

    if(doUpmix)
    {
        /* When upsampling, combine the early and late transforms with the
         * first-order upsample matrix. This results in panning gains that
         * apply the panning transform to first-order B-Format, which is then
         * upsampled.
         */
        auto mult_matrix = [](const al::span<const std::array<float,4>,4> mtx1)
        {
            auto&& mtx2 = AmbiScale::FirstOrderUp;
            std::array<std::array<float,MaxAmbiChannels>,NUM_LINES> res{};

            for(size_t i{0};i < mtx1[0].size();++i)
            {
                float *RESTRICT dst{res[i].data()};
                for(size_t k{0};k < mtx1.size();++k)
                {
                    const float *RESTRICT src{mtx2[k].data()};
                    const float a{mtx1[k][i]};
                    for(size_t j{0};j < mtx2[0].size();++j)
                        dst[j] += a * src[j];
                }
            }

            return res;
        };
        auto earlycoeffs = mult_matrix(earlymat);
        auto latecoeffs = mult_matrix(latemat);

        for(size_t i{0u};i < NUM_LINES;i++)
            ComputePanGains(mainMix, earlycoeffs[i], earlyGain, mEarly.TargetGains[i]);
        for(size_t i{0u};i < NUM_LINES;i++)
            ComputePanGains(mainMix, latecoeffs[i], lateGain, mLate.TargetGains[i]);
    }
    else
    {
        /* When not upsampling, combine the early and late A-to-B-Format
         * conversions with their respective transform. This results panning
         * gains that convert A-Format to B-Format, which is then panned.
         */
        auto mult_matrix = [](const al::span<const std::array<float,NUM_LINES>,4> mtx1,
            const al::span<const std::array<float,4>,4> mtx2)
        {
            std::array<std::array<float,MaxAmbiChannels>,NUM_LINES> res{};

            for(size_t i{0};i < mtx1[0].size();++i)
            {
                float *RESTRICT dst{res[i].data()};
                for(size_t k{0};k < mtx1.size();++k)
                {
                    const float a{mtx1[k][i]};
                    for(size_t j{0};j < mtx2.size();++j)
                        dst[j] += a * mtx2[j][k];
                }
            }

            return res;
        };
        auto earlycoeffs = mult_matrix(EarlyA2B, earlymat);
        auto latecoeffs = mult_matrix(LateA2B, latemat);

        for(size_t i{0u};i < NUM_LINES;i++)
            ComputePanGains(mainMix, earlycoeffs[i], earlyGain, mEarly.TargetGains[i]);
        for(size_t i{0u};i < NUM_LINES;i++)
            ComputePanGains(mainMix, latecoeffs[i], lateGain, mLate.TargetGains[i]);
    }
}

void ReverbState::update(const ContextBase *Context, const EffectSlot *Slot,
    const EffectProps *props_, const EffectTarget target)
{
    auto &props = std::get<ReverbProps>(*props_);
    const DeviceBase *Device{Context->mDevice};
    const auto frequency = static_cast<float>(Device->Frequency);

    /* If the HF limit parameter is flagged, calculate an appropriate limit
     * based on the air absorption parameter.
     */
    float hfRatio{props.DecayHFRatio};
    if(props.DecayHFLimit && props.AirAbsorptionGainHF < 1.0f)
        hfRatio = CalcLimitedHfRatio(hfRatio, props.AirAbsorptionGainHF, props.DecayTime);

    /* Calculate the LF/HF decay times. */
    constexpr float MinDecayTime{0.1f}, MaxDecayTime{20.0f};
    const float lfDecayTime{clampf(props.DecayTime*props.DecayLFRatio, MinDecayTime,MaxDecayTime)};
    const float hfDecayTime{clampf(props.DecayTime*hfRatio, MinDecayTime, MaxDecayTime)};

    /* Determine if a full update is required. */
    const bool fullUpdate{mPipelineState == DeviceClear ||
        /* Density is essentially a master control for the feedback delays, so
         * changes the offsets of many delay lines.
         */
        mParams.Density != props.Density ||
        /* Diffusion and decay times influences the decay rate (gain) of the
         * late reverb T60 filter.
         */
        mParams.Diffusion != props.Diffusion ||
        mParams.DecayTime != props.DecayTime ||
        mParams.HFDecayTime != hfDecayTime ||
        mParams.LFDecayTime != lfDecayTime ||
        /* Modulation time and depth both require fading the modulation delay. */
        mParams.ModulationTime != props.ModulationTime ||
        mParams.ModulationDepth != props.ModulationDepth ||
        /* HF/LF References control the weighting used to calculate the density
         * gain.
         */
        mParams.HFReference != props.HFReference ||
        mParams.LFReference != props.LFReference};
    if(fullUpdate)
    {
        mParams.Density = props.Density;
        mParams.Diffusion = props.Diffusion;
        mParams.DecayTime = props.DecayTime;
        mParams.HFDecayTime = hfDecayTime;
        mParams.LFDecayTime = lfDecayTime;
        mParams.ModulationTime = props.ModulationTime;
        mParams.ModulationDepth = props.ModulationDepth;
        mParams.HFReference = props.HFReference;
        mParams.LFReference = props.LFReference;

        mPipelineState = (mPipelineState != DeviceClear) ? StartFade : Normal;
        mCurrentPipeline = !mCurrentPipeline;
    }
    auto &pipeline = mPipelines[mCurrentPipeline];

    /* Update early and late 3D panning. */
    mOutTarget = target.Main->Buffer;
    const float gain{props.Gain * Slot->Gain * ReverbBoost};
    pipeline.update3DPanning(props.ReflectionsPan, props.LateReverbPan, props.ReflectionsGain*gain,
        props.LateReverbGain*gain, mUpmixOutput, target.Main);

    /* Calculate the master filters */
    float hf0norm{minf(props.HFReference/frequency, 0.49f)};
    pipeline.mFilter[0].Lp.setParamsFromSlope(BiquadType::HighShelf, hf0norm, props.GainHF, 1.0f);
    float lf0norm{minf(props.LFReference/frequency, 0.49f)};
    pipeline.mFilter[0].Hp.setParamsFromSlope(BiquadType::LowShelf, lf0norm, props.GainLF, 1.0f);
    for(size_t i{1u};i < NUM_LINES;i++)
    {
        pipeline.mFilter[i].Lp.copyParamsFrom(pipeline.mFilter[0].Lp);
        pipeline.mFilter[i].Hp.copyParamsFrom(pipeline.mFilter[0].Hp);
    }

    /* The density-based room size (delay length) multiplier. */
    const float density_mult{CalcDelayLengthMult(props.Density)};

    /* Update the main effect delay and associated taps. */
    pipeline.updateDelayLine(props.ReflectionsDelay, props.LateReverbDelay, density_mult,
        props.DecayTime, frequency);

    if(fullUpdate)
    {
        /* Update the early lines. */
        pipeline.mEarly.updateLines(density_mult, props.Diffusion, props.DecayTime, frequency);

        /* Get the mixing matrix coefficients. */
        CalcMatrixCoeffs(props.Diffusion, &pipeline.mMixX, &pipeline.mMixY);

        /* Update the modulator rate and depth. */
        pipeline.mLate.Mod.updateModulator(props.ModulationTime, props.ModulationDepth, frequency);

        /* Update the late lines. */
        pipeline.mLate.updateLines(density_mult, props.Diffusion, lfDecayTime, props.DecayTime,
            hfDecayTime, lf0norm, hf0norm, frequency);
    }

    /* Calculate the gain at the start of the late reverb stage, and the gain
     * difference from the decay target (0.001, or -60dB).
     */
    const float decayBase{props.ReflectionsGain * props.LateReverbGain};
    const float decayDiff{ReverbDecayGain / decayBase};

    if(decayDiff < 1.0f)
    {
        /* Given the DecayTime (the amount of time for the late reverb to decay
         * by -60dB), calculate the time to decay to -60dB from the start of
         * the late reverb.
         */
        const float diffTime{std::log10(decayDiff)*(20.0f / -60.0f) * props.DecayTime};

        const float decaySamples{(props.ReflectionsDelay+props.LateReverbDelay+diffTime)
            * frequency};
        /* Limit to 100,000 samples (a touch over 2 seconds at 48khz) to
         * avoid excessive double-processing.
         */
        pipeline.mFadeSampleCount = static_cast<size_t>(minf(decaySamples, 100'000.0f));
    }
    else
    {
        /* Otherwise, if the late reverb already starts at -60dB or less, only
         * include the time to get to the late reverb.
         */
        const float decaySamples{(props.ReflectionsDelay+props.LateReverbDelay) * frequency};
        pipeline.mFadeSampleCount = static_cast<size_t>(minf(decaySamples, 100'000.0f));
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
    const float xCoeff, const float yCoeff) -> std::array<float,NUM_LINES>
{
    return std::array<float,NUM_LINES>{{
        xCoeff*in[0] + yCoeff*(          in[1] + -in[2] + in[3]),
        xCoeff*in[1] + yCoeff*(-in[0]          +  in[2] + in[3]),
        xCoeff*in[2] + yCoeff*( in[0] + -in[1]          + in[3]),
        xCoeff*in[3] + yCoeff*(-in[0] + -in[1] + -in[2]        )
    }};
}

/* Utilizes the above, but also applies a geometric reflection on the input
 * channels.
 */
void VectorScatterRevDelayIn(const DelayLineI delay, size_t offset, const float xCoeff,
    const float yCoeff, const al::span<const ReverbUpdateLine,NUM_LINES> in, const size_t count)
{
    ASSUME(count > 0);

    for(size_t i{0u};i < count;)
    {
        offset &= delay.Mask;
        size_t td{minz(delay.Mask+1 - offset, count-i)};
        do {
            std::array src{in[0][i], in[1][i], in[2][i], in[3][i]};
            std::array f{
                (         src[1] + src[2] + src[3] - src[0]) * 0.5f,
                (src[0] +          src[2] + src[3] - src[1]) * 0.5f,
                (src[0] + src[1] +          src[3] - src[2]) * 0.5f,
                (src[0] + src[1] + src[2]          - src[3]) * 0.5f
            };
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
 */
void VecAllpass::process(const al::span<ReverbUpdateLine,NUM_LINES> samples, size_t offset,
    const float xCoeff, const float yCoeff, const size_t todo)
{
    const DelayLineI delay{Delay};
    const float feedCoeff{Coeff};

    ASSUME(todo > 0);

    std::array<size_t,NUM_LINES> vap_offset;
    for(size_t j{0u};j < NUM_LINES;j++)
        vap_offset[j] = offset - Offset[j];
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
                const float input{samples[j][i]};
                const float out{delay.Line[vap_offset[j]++][j] - feedCoeff*input};
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
 * The early lines are then reflected about the origin to create the secondary
 * reflections (those arriving from the opposite direction as the source).
 *
 * The early response is then completed by combining the primary reflections
 * with the delayed and attenuated output from the early lines.
 *
 * Finally, the early response is reflected, scattered (based on diffusion),
 * and fed into the late reverb section of the main delay line.
 */
void ReverbPipeline::processEarly(size_t offset, const size_t samplesToDo,
    const al::span<ReverbUpdateLine, NUM_LINES> tempSamples,
    const al::span<FloatBufferLine, NUM_LINES> outSamples)
{
    const DelayLineI early_delay{mEarly.Delay};
    const DelayLineI in_delay{mEarlyDelayIn};
    const float mixX{mMixX};
    const float mixY{mMixY};

    ASSUME(samplesToDo > 0);

    for(size_t base{0};base < samplesToDo;)
    {
        const size_t todo{minz(samplesToDo-base, MAX_UPDATE_SAMPLES)};

        /* First, load decorrelated samples from the main delay line as the
         * primary reflections.
         */
        const float fadeStep{1.0f / static_cast<float>(todo)};
        for(size_t j{0u};j < NUM_LINES;j++)
        {
            size_t early_delay_tap0{offset - mEarlyDelayTap[j][0]};
            size_t early_delay_tap1{offset - mEarlyDelayTap[j][1]};
            const float coeff{mEarlyDelayCoeff[j]};
            const float coeffStep{early_delay_tap0 != early_delay_tap1 ? coeff*fadeStep : 0.0f};
            float fadeCount{0.0f};

            for(size_t i{0u};i < todo;)
            {
                early_delay_tap0 &= in_delay.Mask;
                early_delay_tap1 &= in_delay.Mask;
                const size_t max_tap{maxz(early_delay_tap0, early_delay_tap1)};
                size_t td{minz(in_delay.Mask+1 - max_tap, todo-i)};
                do {
                    const float fade0{coeff - coeffStep*fadeCount};
                    const float fade1{coeffStep*fadeCount};
                    fadeCount += 1.0f;
                    tempSamples[j][i++] = in_delay.Line[early_delay_tap0++][j]*fade0 +
                        in_delay.Line[early_delay_tap1++][j]*fade1;
                } while(--td);
            }

            mEarlyDelayTap[j][0] = mEarlyDelayTap[j][1];
        }

        /* Apply a vector all-pass, to help color the initial reflections.
         * Don't apply diffusion-based scattering since these are still the
         * first reflections.
         */
        mEarly.VecAp.process(tempSamples, offset, 1.0f, 0.0f, todo);

        /* Apply a delay and bounce to generate secondary reflections, combine
         * with the primary reflections and write out the result for mixing.
         */
        early_delay.writeReflected(offset, tempSamples, todo);
        for(size_t j{0u};j < NUM_LINES;j++)
        {
            size_t feedb_tap{offset - mEarly.Offset[j]};
            const float feedb_coeff{mEarly.Coeff[j]};
            float *RESTRICT out{al::assume_aligned<16>(outSamples[j].data() + base)};

            for(size_t i{0u};i < todo;)
            {
                feedb_tap &= early_delay.Mask;
                size_t td{minz(early_delay.Mask+1 - feedb_tap, todo - i)};
                do {
                    float sample{early_delay.Line[feedb_tap++][j]};
                    out[i] = tempSamples[j][i] + sample*feedb_coeff;
                    tempSamples[j][i] = sample;
                    ++i;
                } while(--td);
            }
        }

        /* Finally, write the result to the late delay line input for the late
         * reverb stage to pick up at the appropriate time, applying a scatter
         * and bounce to improve the initial diffusion in the late reverb.
         */
        VectorScatterRevDelayIn(mLateDelayIn, offset, mixX, mixY, tempSamples, todo);

        base += todo;
        offset += todo;
    }
}

void Modulation::calcDelays(size_t todo)
{
    uint idx{Index};
    const uint step{Step};
    const float depth{Depth};
    for(size_t i{0};i < todo;++i)
    {
        idx += step;
        const float x{static_cast<float>(idx&MOD_FRACMASK) * (1.0f/MOD_FRACONE)};
        /* Approximate sin(x*2pi). As long as it roughly fits a sinusoid shape
         * and stays within [-1...+1], it needn't be perfect.
         */
        const float lfo{!(idx&(MOD_FRACONE>>1))
            ? ((-16.0f * x * x) + (8.0f * x))
            : ((16.0f * x * x) + (-8.0f * x) + (-16.0f * x) + 8.0f)};
        ModDelays[i] = (lfo+1.0f) * depth;
    }
    Index = idx;
}


/* This generates the reverb tail using a modified feed-back delay network
 * (FDN).
 *
 * Results from the early reflections are mixed with the output from the
 * modulated late delay lines.
 *
 * The late response is then completed by T60 and all-pass filtering the mix.
 *
 * Finally, the lines are reversed (so they feed their opposite directions)
 * and scattered with the FDN matrix before re-feeding the delay lines.
 */
void ReverbPipeline::processLate(size_t offset, const size_t samplesToDo,
    const al::span<ReverbUpdateLine, NUM_LINES> tempSamples,
    const al::span<FloatBufferLine, NUM_LINES> outSamples)
{
    const DelayLineI late_delay{mLate.Delay};
    const DelayLineI in_delay{mLateDelayIn};
    const float mixX{mMixX};
    const float mixY{mMixY};

    ASSUME(samplesToDo > 0);

    for(size_t base{0};base < samplesToDo;)
    {
        const size_t todo{minz(samplesToDo-base, minz(mLate.Offset[0], MAX_UPDATE_SAMPLES))};
        ASSUME(todo > 0);

        /* First, calculate the modulated delays for the late feedback. */
        mLate.Mod.calcDelays(todo);

        /* Now load samples from the feedback delay lines. Filter the signal to
         * apply its frequency-dependent decay.
         */
        for(size_t j{0u};j < NUM_LINES;++j)
        {
            size_t late_feedb_tap{offset - mLate.Offset[j]};
            const float midGain{mLate.T60[j].MidGain};

            for(size_t i{0u};i < todo;++i)
            {
                /* Calculate the read offset and offset between it and the next
                 * sample.
                 */
                const float fdelay{mLate.Mod.ModDelays[i]};
                const size_t idelay{float2uint(fdelay * float{gCubicTable.sTableSteps})};
                const size_t delay{late_feedb_tap - (idelay>>gCubicTable.sTableBits)};
                const size_t delayoffset{idelay & gCubicTable.sTableMask};
                ++late_feedb_tap;

                /* Get the samples around by the delayed offset. */
                const float out0{late_delay.Line[(delay  ) & late_delay.Mask][j]};
                const float out1{late_delay.Line[(delay-1) & late_delay.Mask][j]};
                const float out2{late_delay.Line[(delay-2) & late_delay.Mask][j]};
                const float out3{late_delay.Line[(delay-3) & late_delay.Mask][j]};

                /* The output is obtained by interpolating the four samples
                 * that were acquired above, and combined with the main delay
                 * tap.
                 */
                const float out{out0*gCubicTable.getCoeff0(delayoffset)
                    + out1*gCubicTable.getCoeff1(delayoffset)
                    + out2*gCubicTable.getCoeff2(delayoffset)
                    + out3*gCubicTable.getCoeff3(delayoffset)};
                tempSamples[j][i] = out * midGain;
            }

            mLate.T60[j].process({tempSamples[j].data(), todo});
        }

        /* Next load decorrelated samples from the main delay lines. */
        const float fadeStep{1.0f / static_cast<float>(todo)};
        for(size_t j{0u};j < NUM_LINES;++j)
        {
            size_t late_delay_tap0{offset - mLateDelayTap[j][0]};
            size_t late_delay_tap1{offset - mLateDelayTap[j][1]};
            const float densityGain{mLate.DensityGain};
            const float densityStep{late_delay_tap0 != late_delay_tap1 ?
                densityGain*fadeStep : 0.0f};
            float fadeCount{0.0f};

            for(size_t i{0u};i < todo;)
            {
                late_delay_tap0 &= in_delay.Mask;
                late_delay_tap1 &= in_delay.Mask;
                size_t td{minz(todo-i, in_delay.Mask+1 - maxz(late_delay_tap0, late_delay_tap1))};
                do {
                    const float fade0{densityGain - densityStep*fadeCount};
                    const float fade1{densityStep*fadeCount};
                    fadeCount += 1.0f;
                    tempSamples[j][i] += in_delay.Line[late_delay_tap0++][j]*fade0 +
                        in_delay.Line[late_delay_tap1++][j]*fade1;
                    ++i;
                } while(--td);
            }
            mLateDelayTap[j][0] = mLateDelayTap[j][1];
        }

        /* Apply a vector all-pass to improve micro-surface diffusion, and
         * write out the results for mixing.
         */
        mLate.VecAp.process(tempSamples, offset, mixX, mixY, todo);
        for(size_t j{0u};j < NUM_LINES;++j)
            std::copy_n(tempSamples[j].begin(), todo, outSamples[j].begin()+base);

        /* Finally, scatter and bounce the results to refeed the feedback buffer. */
        VectorScatterRevDelayIn(late_delay, offset, mixX, mixY, tempSamples, todo);

        base += todo;
        offset += todo;
    }
}

void ReverbState::process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut)
{
    const size_t offset{mOffset};

    ASSUME(samplesToDo > 0);

    auto &oldpipeline = mPipelines[!mCurrentPipeline];
    auto &pipeline = mPipelines[mCurrentPipeline];

    if(mPipelineState >= Fading)
    {
        /* Convert B-Format to A-Format for processing. */
        const size_t numInput{minz(samplesIn.size(), NUM_LINES)};
        const al::span<float> tmpspan{al::assume_aligned<16>(mTempLine.data()), samplesToDo};
        for(size_t c{0u};c < NUM_LINES;++c)
        {
            std::fill(tmpspan.begin(), tmpspan.end(), 0.0f);
            for(size_t i{0};i < numInput;++i)
            {
                const float gain{B2A[c][i]};
                const float *RESTRICT input{al::assume_aligned<16>(samplesIn[i].data())};

                auto mix_sample = [gain](const float sample, const float in) noexcept -> float
                { return sample + in*gain; };
                std::transform(tmpspan.begin(), tmpspan.end(), input, tmpspan.begin(),
                    mix_sample);
            }

            /* Band-pass the incoming samples and feed the initial delay line. */
            auto&& filter = DualBiquad{pipeline.mFilter[c].Lp, pipeline.mFilter[c].Hp};
            filter.process(tmpspan, tmpspan.data());
            pipeline.mEarlyDelayIn.write(offset, c, tmpspan.cbegin(), samplesToDo);
        }
        if(mPipelineState == Fading)
        {
            /* Give the old pipeline silence if it's still fading out. */
            for(size_t c{0u};c < NUM_LINES;c++)
            {
                std::fill(tmpspan.begin(), tmpspan.end(), 0.0f);

                auto&& filter = DualBiquad{oldpipeline.mFilter[c].Lp, oldpipeline.mFilter[c].Hp};
                filter.process(tmpspan, tmpspan.data());
                oldpipeline.mEarlyDelayIn.write(offset, c, tmpspan.cbegin(), samplesToDo);
            }
        }
    }
    else
    {
        /* At the start of a fade, fade in input for the current pipeline, and
         * fade out input for the old pipeline.
         */
        const size_t numInput{minz(samplesIn.size(), NUM_LINES)};
        const al::span<float> tmpspan{al::assume_aligned<16>(mTempLine.data()), samplesToDo};
        const float fadeStep{1.0f / static_cast<float>(samplesToDo)};

        for(size_t c{0u};c < NUM_LINES;++c)
        {
            std::fill(tmpspan.begin(), tmpspan.end(), 0.0f);
            for(size_t i{0};i < numInput;++i)
            {
                const float gain{B2A[c][i]};
                const float *RESTRICT input{al::assume_aligned<16>(samplesIn[i].data())};

                auto mix_sample = [gain](const float sample, const float in) noexcept -> float
                { return sample + in*gain; };
                std::transform(tmpspan.begin(), tmpspan.end(), input, tmpspan.begin(),
                    mix_sample);
            }
            float stepCount{0.0f};
            for(float &sample : tmpspan)
            {
                stepCount += 1.0f;
                sample *= stepCount*fadeStep;
            }

            auto&& filter = DualBiquad{pipeline.mFilter[c].Lp, pipeline.mFilter[c].Hp};
            filter.process(tmpspan, tmpspan.data());
            pipeline.mEarlyDelayIn.write(offset, c, tmpspan.cbegin(), samplesToDo);
        }
        for(size_t c{0u};c < NUM_LINES;++c)
        {
            std::fill(tmpspan.begin(), tmpspan.end(), 0.0f);
            for(size_t i{0};i < numInput;++i)
            {
                const float gain{B2A[c][i]};
                const float *RESTRICT input{al::assume_aligned<16>(samplesIn[i].data())};

                auto mix_sample = [gain](const float sample, const float in) noexcept -> float
                { return sample + in*gain; };
                std::transform(tmpspan.begin(), tmpspan.end(), input, tmpspan.begin(),
                    mix_sample);
            }
            float stepCount{0.0f};
            for(float &sample : tmpspan)
            {
                stepCount += 1.0f;
                sample *= 1.0f - stepCount*fadeStep;
            }

            auto&& filter = DualBiquad{oldpipeline.mFilter[c].Lp, oldpipeline.mFilter[c].Hp};
            filter.process(tmpspan, tmpspan.data());
            oldpipeline.mEarlyDelayIn.write(offset, c, tmpspan.cbegin(), samplesToDo);
        }
        mPipelineState = Fading;
    }

    /* Process reverb for these samples. and mix them to the output. */
    pipeline.processEarly(offset, samplesToDo, mTempSamples, mEarlySamples);
    pipeline.processLate(offset, samplesToDo, mTempSamples, mLateSamples);
    mixOut(pipeline, samplesOut, samplesToDo);

    if(mPipelineState != Normal)
    {
        if(mPipelineState == Cleanup)
        {
            size_t numSamples{mSampleBuffer.size()/2};
            size_t pipelineOffset{numSamples * (!mCurrentPipeline)};
            std::fill_n(mSampleBuffer.data()+pipelineOffset, numSamples,
                decltype(mSampleBuffer)::value_type{});

            oldpipeline.clear();
            mPipelineState = Normal;
        }
        else
        {
            /* If this is the final mix for this old pipeline, set the target
             * gains to 0 to ensure a complete fade out, and set the state to
             * Cleanup so the next invocation cleans up the delay buffers and
             * filters.
             */
            if(samplesToDo >= oldpipeline.mFadeSampleCount)
            {
                for(auto &gains : oldpipeline.mEarly.TargetGains)
                    std::fill(std::begin(gains), std::end(gains), 0.0f);
                for(auto &gains : oldpipeline.mLate.TargetGains)
                    std::fill(std::begin(gains), std::end(gains), 0.0f);
                oldpipeline.mFadeSampleCount = 0;
                mPipelineState = Cleanup;
            }
            else
                oldpipeline.mFadeSampleCount -= samplesToDo;

            /* Process the old reverb for these samples. */
            oldpipeline.processEarly(offset, samplesToDo, mTempSamples, mEarlySamples);
            oldpipeline.processLate(offset, samplesToDo, mTempSamples, mLateSamples);
            mixOut(oldpipeline, samplesOut, samplesToDo);
        }
    }

    mOffset = offset + samplesToDo;
}


struct ReverbStateFactory final : public EffectStateFactory {
    al::intrusive_ptr<EffectState> create() override
    { return al::intrusive_ptr<EffectState>{new ReverbState{}}; }
};

struct StdReverbStateFactory final : public EffectStateFactory {
    al::intrusive_ptr<EffectState> create() override
    { return al::intrusive_ptr<EffectState>{new ReverbState{}}; }
};

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
