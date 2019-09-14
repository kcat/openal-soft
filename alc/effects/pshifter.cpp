/**
 * OpenAL cross platform audio library
 * Copyright (C) 2018 by Raul Herraiz.
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

#ifdef HAVE_SSE_INTRINSICS
#include <emmintrin.h>
#endif

#include <cmath>
#include <cstdlib>
#include <array>
#include <complex>
#include <algorithm>

#include "al/auxeffectslot.h"
#include "alcmain.h"
#include "alcomplex.h"
#include "alcontext.h"
#include "alnumeric.h"
#include "alu.h"


namespace {

using complex_d = std::complex<double>;

#define STFT_SIZE      1024
#define STFT_HALF_SIZE (STFT_SIZE>>1)
#define OVERSAMP       (1<<2)

#define STFT_STEP    (STFT_SIZE / OVERSAMP)
#define FIFO_LATENCY (STFT_STEP * (OVERSAMP-1))

/* Define a Hann window, used to filter the STFT input and output. */
/* Making this constexpr seems to require C++14. */
std::array<ALdouble,STFT_SIZE> InitHannWindow()
{
    std::array<ALdouble,STFT_SIZE> ret;
    /* Create lookup table of the Hann window for the desired size, i.e. HIL_SIZE */
    for(size_t i{0};i < STFT_SIZE>>1;i++)
    {
        constexpr double scale{al::MathDefs<double>::Pi() / double{STFT_SIZE-1}};
        const double val{std::sin(static_cast<double>(i) * scale)};
        ret[i] = ret[STFT_SIZE-1-i] = val * val;
    }
    return ret;
}
alignas(16) const std::array<ALdouble,STFT_SIZE> HannWindow = InitHannWindow();


struct ALphasor {
    ALdouble Amplitude;
    ALdouble Phase;
};

struct ALfrequencyDomain {
    ALdouble Amplitude;
    ALdouble Frequency;
};


/* Converts complex to ALphasor */
inline ALphasor rect2polar(const complex_d &number)
{
    ALphasor polar;
    polar.Amplitude = std::abs(number);
    polar.Phase     = std::arg(number);
    return polar;
}

/* Converts ALphasor to complex */
inline complex_d polar2rect(const ALphasor &number)
{ return std::polar<double>(number.Amplitude, number.Phase); }


struct PshifterState final : public EffectState {
    /* Effect parameters */
    size_t  mCount;
    ALuint  mPitchShiftI;
    ALfloat mPitchShift;
    ALfloat mFreqPerBin;

    /* Effects buffers */
    ALfloat mInFIFO[STFT_SIZE];
    ALfloat mOutFIFO[STFT_STEP];
    ALdouble mLastPhase[STFT_HALF_SIZE+1];
    ALdouble mSumPhase[STFT_HALF_SIZE+1];
    ALdouble mOutputAccum[STFT_SIZE];

    complex_d mFFTbuffer[STFT_SIZE];

    ALfrequencyDomain mAnalysis_buffer[STFT_HALF_SIZE+1];
    ALfrequencyDomain mSyntesis_buffer[STFT_HALF_SIZE+1];

    alignas(16) ALfloat mBufferOut[BUFFERSIZE];

    /* Effect gains for each output channel */
    ALfloat mCurrentGains[MAX_OUTPUT_CHANNELS];
    ALfloat mTargetGains[MAX_OUTPUT_CHANNELS];


    ALboolean deviceUpdate(const ALCdevice *device) override;
    void update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target) override;
    void process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut) override;

    DEF_NEWDEL(PshifterState)
};

ALboolean PshifterState::deviceUpdate(const ALCdevice *device)
{
    /* (Re-)initializing parameters and clear the buffers. */
    mCount       = FIFO_LATENCY;
    mPitchShiftI = FRACTIONONE;
    mPitchShift  = 1.0f;
    mFreqPerBin  = static_cast<float>(device->Frequency) / float{STFT_SIZE};

    std::fill(std::begin(mInFIFO),          std::end(mInFIFO),          0.0f);
    std::fill(std::begin(mOutFIFO),         std::end(mOutFIFO),         0.0f);
    std::fill(std::begin(mLastPhase),       std::end(mLastPhase),       0.0);
    std::fill(std::begin(mSumPhase),        std::end(mSumPhase),        0.0);
    std::fill(std::begin(mOutputAccum),     std::end(mOutputAccum),     0.0);
    std::fill(std::begin(mFFTbuffer),       std::end(mFFTbuffer),       complex_d{});
    std::fill(std::begin(mAnalysis_buffer), std::end(mAnalysis_buffer), ALfrequencyDomain{});
    std::fill(std::begin(mSyntesis_buffer), std::end(mSyntesis_buffer), ALfrequencyDomain{});

    std::fill(std::begin(mCurrentGains), std::end(mCurrentGains), 0.0f);
    std::fill(std::begin(mTargetGains),  std::end(mTargetGains),  0.0f);

    return AL_TRUE;
}

void PshifterState::update(const ALCcontext*, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target)
{
    const float pitch{std::pow(2.0f,
        static_cast<ALfloat>(props->Pshifter.CoarseTune*100 + props->Pshifter.FineTune) / 1200.0f
    )};
    mPitchShiftI = fastf2u(pitch*FRACTIONONE);
    mPitchShift  = static_cast<float>(mPitchShiftI) * (1.0f/FRACTIONONE);

    ALfloat coeffs[MAX_AMBI_CHANNELS];
    CalcDirectionCoeffs({0.0f, 0.0f, -1.0f}, 0.0f, coeffs);

    mOutTarget = target.Main->Buffer;
    ComputePanGains(target.Main, coeffs, slot->Params.Gain, mTargetGains);
}

void PshifterState::process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut)
{
    /* Pitch shifter engine based on the work of Stephan Bernsee.
     * http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/
     */

    static constexpr ALdouble expected{al::MathDefs<double>::Tau() / OVERSAMP};
    const ALdouble freq_per_bin{mFreqPerBin};
    ALfloat *RESTRICT bufferOut{mBufferOut};
    size_t count{mCount};

    for(size_t i{0u};i < samplesToDo;)
    {
        do {
            /* Fill FIFO buffer with samples data */
            mInFIFO[count] = samplesIn[0][i];
            bufferOut[i] = mOutFIFO[count - FIFO_LATENCY];

            count++;
        } while(++i < samplesToDo && count < STFT_SIZE);

        /* Check whether FIFO buffer is filled */
        if(count < STFT_SIZE) break;
        count = FIFO_LATENCY;

        /* Real signal windowing and store in FFTbuffer */
        for(ALuint k{0u};k < STFT_SIZE;k++)
        {
            mFFTbuffer[k].real(mInFIFO[k] * HannWindow[k]);
            mFFTbuffer[k].imag(0.0);
        }

        /* ANALYSIS */
        /* Apply FFT to FFTbuffer data */
        complex_fft(mFFTbuffer, -1.0);

        /* Analyze the obtained data. Since the real FFT is symmetric, only
         * STFT_HALF_SIZE+1 samples are needed.
         */
        for(ALuint k{0u};k < STFT_HALF_SIZE+1;k++)
        {
            /* Compute amplitude and phase */
            ALphasor component{rect2polar(mFFTbuffer[k])};

            /* Compute phase difference and subtract expected phase difference */
            double tmp{(component.Phase - mLastPhase[k]) - k*expected};

            /* Map delta phase into +/- Pi interval */
            int qpd{double2int(tmp / al::MathDefs<double>::Pi())};
            tmp -= al::MathDefs<double>::Pi() * (qpd + (qpd%2));

            /* Get deviation from bin frequency from the +/- Pi interval */
            tmp /= expected;

            /* Compute the k-th partials' true frequency, twice the amplitude
             * for maintain the gain (because half of bins are used) and store
             * amplitude and true frequency in analysis buffer.
             */
            mAnalysis_buffer[k].Amplitude = 2.0 * component.Amplitude;
            mAnalysis_buffer[k].Frequency = (k + tmp) * freq_per_bin;

            /* Store actual phase[k] for the calculations in the next frame*/
            mLastPhase[k] = component.Phase;
        }

        /* PROCESSING */
        /* pitch shifting */
        for(ALuint k{0u};k < STFT_HALF_SIZE+1;k++)
        {
            mSyntesis_buffer[k].Amplitude = 0.0;
            mSyntesis_buffer[k].Frequency = 0.0;
        }

        for(size_t k{0u};k < STFT_HALF_SIZE+1;k++)
        {
            size_t j{(k*mPitchShiftI) >> FRACTIONBITS};
            if(j >= STFT_HALF_SIZE+1) break;

            mSyntesis_buffer[j].Amplitude += mAnalysis_buffer[k].Amplitude;
            mSyntesis_buffer[j].Frequency  = mAnalysis_buffer[k].Frequency * mPitchShift;
        }

        /* SYNTHESIS */
        /* Synthesis the processing data */
        for(ALuint k{0u};k < STFT_HALF_SIZE+1;k++)
        {
            ALphasor component;
            ALdouble tmp;

            /* Compute bin deviation from scaled freq */
            tmp = mSyntesis_buffer[k].Frequency/freq_per_bin - k;

            /* Calculate actual delta phase and accumulate it to get bin phase */
            mSumPhase[k] += (k + tmp) * expected;

            component.Amplitude = mSyntesis_buffer[k].Amplitude;
            component.Phase     = mSumPhase[k];

            /* Compute phasor component to cartesian complex number and storage it into FFTbuffer*/
            mFFTbuffer[k] = polar2rect(component);
        }
        /* zero negative frequencies for recontruct a real signal */
        for(ALuint k{STFT_HALF_SIZE+1};k < STFT_SIZE;k++)
            mFFTbuffer[k] = complex_d{};

        /* Apply iFFT to buffer data */
        complex_fft(mFFTbuffer, 1.0);

        /* Windowing and add to output */
        for(ALuint k{0u};k < STFT_SIZE;k++)
            mOutputAccum[k] += HannWindow[k] * mFFTbuffer[k].real() /
                               (0.5 * STFT_HALF_SIZE * OVERSAMP);

        /* Shift accumulator, input & output FIFO */
        size_t j, k;
        for(k = 0;k < STFT_STEP;k++) mOutFIFO[k] = static_cast<ALfloat>(mOutputAccum[k]);
        for(j = 0;k < STFT_SIZE;k++,j++) mOutputAccum[j] = mOutputAccum[k];
        for(;j < STFT_SIZE;j++) mOutputAccum[j] = 0.0;
        for(k = 0;k < FIFO_LATENCY;k++)
            mInFIFO[k] = mInFIFO[k+STFT_STEP];
    }
    mCount = count;

    /* Now, mix the processed sound data to the output. */
    MixSamples({bufferOut, samplesToDo}, samplesOut, mCurrentGains, mTargetGains,
        maxz(samplesToDo, 512), 0);
}


void Pshifter_setParamf(EffectProps*, ALCcontext *context, ALenum param, ALfloat)
{ context->setError(AL_INVALID_ENUM, "Invalid pitch shifter float property 0x%04x", param); }
void Pshifter_setParamfv(EffectProps*, ALCcontext *context, ALenum param, const ALfloat*)
{ context->setError(AL_INVALID_ENUM, "Invalid pitch shifter float-vector property 0x%04x", param); }

void Pshifter_setParami(EffectProps *props, ALCcontext *context, ALenum param, ALint val)
{
    switch(param)
    {
        case AL_PITCH_SHIFTER_COARSE_TUNE:
            if(!(val >= AL_PITCH_SHIFTER_MIN_COARSE_TUNE && val <= AL_PITCH_SHIFTER_MAX_COARSE_TUNE))
                SETERR_RETURN(context, AL_INVALID_VALUE,,"Pitch shifter coarse tune out of range");
            props->Pshifter.CoarseTune = val;
            break;

        case AL_PITCH_SHIFTER_FINE_TUNE:
            if(!(val >= AL_PITCH_SHIFTER_MIN_FINE_TUNE && val <= AL_PITCH_SHIFTER_MAX_FINE_TUNE))
                SETERR_RETURN(context, AL_INVALID_VALUE,,"Pitch shifter fine tune out of range");
            props->Pshifter.FineTune = val;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid pitch shifter integer property 0x%04x",
                param);
    }
}
void Pshifter_setParamiv(EffectProps *props, ALCcontext *context, ALenum param, const ALint *vals)
{ Pshifter_setParami(props, context, param, vals[0]); }

void Pshifter_getParami(const EffectProps *props, ALCcontext *context, ALenum param, ALint *val)
{
    switch(param)
    {
        case AL_PITCH_SHIFTER_COARSE_TUNE:
            *val = props->Pshifter.CoarseTune;
            break;
        case AL_PITCH_SHIFTER_FINE_TUNE:
            *val = props->Pshifter.FineTune;
            break;

        default:
            context->setError(AL_INVALID_ENUM, "Invalid pitch shifter integer property 0x%04x",
                param);
    }
}
void Pshifter_getParamiv(const EffectProps *props, ALCcontext *context, ALenum param, ALint *vals)
{ Pshifter_getParami(props, context, param, vals); }

void Pshifter_getParamf(const EffectProps*, ALCcontext *context, ALenum param, ALfloat*)
{ context->setError(AL_INVALID_ENUM, "Invalid pitch shifter float property 0x%04x", param); }
void Pshifter_getParamfv(const EffectProps*, ALCcontext *context, ALenum param, ALfloat*)
{ context->setError(AL_INVALID_ENUM, "Invalid pitch shifter float vector-property 0x%04x", param); }

DEFINE_ALEFFECT_VTABLE(Pshifter);


struct PshifterStateFactory final : public EffectStateFactory {
    EffectState *create() override;
    EffectProps getDefaultProps() const noexcept override;
    const EffectVtable *getEffectVtable() const noexcept override { return &Pshifter_vtable; }
};

EffectState *PshifterStateFactory::create()
{ return new PshifterState{}; }

EffectProps PshifterStateFactory::getDefaultProps() const noexcept
{
    EffectProps props{};
    props.Pshifter.CoarseTune = AL_PITCH_SHIFTER_DEFAULT_COARSE_TUNE;
    props.Pshifter.FineTune   = AL_PITCH_SHIFTER_DEFAULT_FINE_TUNE;
    return props;
}

} // namespace

EffectStateFactory *PshifterStateFactory_getFactory()
{
    static PshifterStateFactory PshifterFactory{};
    return &PshifterFactory;
}
