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

#include <cmath>
#include <cstdlib>
#include <complex>
#include <algorithm>

#include "alMain.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
#include "alu.h"
#include "filters/defs.h"

#include "alcomplex.h"


namespace {

using complex_d = std::complex<double>;

#define STFT_SIZE      1024
#define STFT_HALF_SIZE (STFT_SIZE>>1)
#define OVERSAMP       (1<<2)

#define STFT_STEP    (STFT_SIZE / OVERSAMP)
#define FIFO_LATENCY (STFT_STEP * (OVERSAMP-1))

inline int double2int(double d)
{
#if ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)) && \
     !defined(__SSE2_MATH__)) || (defined(_MSC_VER) && defined(_M_IX86_FP) && _M_IX86_FP < 2)
    ALint sign, shift;
    ALint64 mant;
    union {
        ALdouble d;
        ALint64 i64;
    } conv;

    conv.d = d;
    sign = (conv.i64>>63) | 1;
    shift = ((conv.i64>>52)&0x7ff) - (1023+52);

    /* Over/underflow */
    if(UNLIKELY(shift >= 63 || shift < -52))
        return 0;

    mant = (conv.i64&I64(0xfffffffffffff)) | I64(0x10000000000000);
    if(LIKELY(shift < 0))
        return (ALint)(mant >> -shift) * sign;
    return (ALint)(mant << shift) * sign;

#else

    return (ALint)d;
#endif
}

/* Define a Hann window, used to filter the STFT input and output. */
/* Making this constexpr seems to require C++14. */
std::array<ALdouble,STFT_SIZE> InitHannWindow(void)
{
    std::array<ALdouble,STFT_SIZE> ret;
    /* Create lookup table of the Hann window for the desired size, i.e. HIL_SIZE */
    for(ALsizei i{0};i < STFT_SIZE>>1;i++)
    {
        ALdouble val = std::sin(M_PI * (ALdouble)i / (ALdouble)(STFT_SIZE-1));
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


struct ALpshifterState final : public ALeffectState {
    /* Effect parameters */
    ALsizei count;
    ALsizei PitchShiftI;
    ALfloat PitchShift;
    ALfloat FreqPerBin;

    /*Effects buffers*/
    ALfloat InFIFO[STFT_SIZE];
    ALfloat OutFIFO[STFT_STEP];
    ALdouble LastPhase[STFT_HALF_SIZE+1];
    ALdouble SumPhase[STFT_HALF_SIZE+1];
    ALdouble OutputAccum[STFT_SIZE];

    complex_d FFTbuffer[STFT_SIZE];

    ALfrequencyDomain Analysis_buffer[STFT_HALF_SIZE+1];
    ALfrequencyDomain Syntesis_buffer[STFT_HALF_SIZE+1];

    alignas(16) ALfloat BufferOut[BUFFERSIZE];

    /* Effect gains for each output channel */
    ALfloat CurrentGains[MAX_OUTPUT_CHANNELS];
    ALfloat TargetGains[MAX_OUTPUT_CHANNELS];
};

static ALvoid ALpshifterState_Destruct(ALpshifterState *state);
static ALboolean ALpshifterState_deviceUpdate(ALpshifterState *state, ALCdevice *device);
static ALvoid ALpshifterState_update(ALpshifterState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props);
static ALvoid ALpshifterState_process(ALpshifterState *state, ALsizei SamplesToDo, const ALfloat (*RESTRICT SamplesIn)[BUFFERSIZE], ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE], ALsizei NumChannels);
DECLARE_DEFAULT_ALLOCATORS(ALpshifterState)

DEFINE_ALEFFECTSTATE_VTABLE(ALpshifterState);

void ALpshifterState_Construct(ALpshifterState *state)
{
    new (state) ALpshifterState{};
    ALeffectState_Construct(STATIC_CAST(ALeffectState, state));
    SET_VTABLE2(ALpshifterState, ALeffectState, state);
}

ALvoid ALpshifterState_Destruct(ALpshifterState *state)
{
    ALeffectState_Destruct(STATIC_CAST(ALeffectState,state));
    state->~ALpshifterState();
}

ALboolean ALpshifterState_deviceUpdate(ALpshifterState *state, ALCdevice *device)
{
    /* (Re-)initializing parameters and clear the buffers. */
    state->count       = FIFO_LATENCY;
    state->PitchShiftI = FRACTIONONE;
    state->PitchShift  = 1.0f;
    state->FreqPerBin  = device->Frequency / (ALfloat)STFT_SIZE;

    std::fill(std::begin(state->InFIFO),          std::end(state->InFIFO),          0.0f);
    std::fill(std::begin(state->OutFIFO),         std::end(state->OutFIFO),         0.0f);
    std::fill(std::begin(state->LastPhase),       std::end(state->LastPhase),       0.0);
    std::fill(std::begin(state->SumPhase),        std::end(state->SumPhase),        0.0);
    std::fill(std::begin(state->OutputAccum),     std::end(state->OutputAccum),     0.0);
    std::fill(std::begin(state->FFTbuffer),       std::end(state->FFTbuffer),       complex_d{});
    std::fill(std::begin(state->Analysis_buffer), std::end(state->Analysis_buffer), ALfrequencyDomain{});
    std::fill(std::begin(state->Syntesis_buffer), std::end(state->Syntesis_buffer), ALfrequencyDomain{});

    std::fill(std::begin(state->CurrentGains), std::end(state->CurrentGains), 0.0f);
    std::fill(std::begin(state->TargetGains),  std::end(state->TargetGains),  0.0f);

    return AL_TRUE;
}

ALvoid ALpshifterState_update(ALpshifterState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props)
{
    const ALCdevice *device = context->Device;
    ALfloat coeffs[MAX_AMBI_COEFFS];
    float pitch;

    pitch = std::pow(2.0f,
        (ALfloat)(props->Pshifter.CoarseTune*100 + props->Pshifter.FineTune) / 1200.0f
    );
    state->PitchShiftI = fastf2i(pitch*FRACTIONONE);
    state->PitchShift  = state->PitchShiftI * (1.0f/FRACTIONONE);

    CalcAngleCoeffs(0.0f, 0.0f, 0.0f, coeffs);
    ComputePanGains(&device->Dry, coeffs, slot->Params.Gain, state->TargetGains);
}

ALvoid ALpshifterState_process(ALpshifterState *state, ALsizei SamplesToDo, const ALfloat (*RESTRICT SamplesIn)[BUFFERSIZE], ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE], ALsizei NumChannels)
{
    /* Pitch shifter engine based on the work of Stephan Bernsee.
     * http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/
     */

    static constexpr ALdouble expected{M_PI*2.0 / OVERSAMP};
    const ALdouble freq_per_bin{state->FreqPerBin};
    ALfloat *RESTRICT bufferOut{state->BufferOut};
    ALsizei count{state->count};

    for(ALsizei i{0};i < SamplesToDo;)
    {
        do {
            /* Fill FIFO buffer with samples data */
            state->InFIFO[count] = SamplesIn[0][i];
            bufferOut[i] = state->OutFIFO[count - FIFO_LATENCY];

            count++;
        } while(++i < SamplesToDo && count < STFT_SIZE);

        /* Check whether FIFO buffer is filled */
        if(count < STFT_SIZE) break;
        count = FIFO_LATENCY;

        /* Real signal windowing and store in FFTbuffer */
        for(ALsizei k{0};k < STFT_SIZE;k++)
        {
            state->FFTbuffer[k].real(state->InFIFO[k] * HannWindow[k]);
            state->FFTbuffer[k].imag(0.0);
        }

        /* ANALYSIS */
        /* Apply FFT to FFTbuffer data */
        complex_fft(state->FFTbuffer, STFT_SIZE, -1.0);

        /* Analyze the obtained data. Since the real FFT is symmetric, only
         * STFT_HALF_SIZE+1 samples are needed.
         */
        for(ALsizei k{0};k < STFT_HALF_SIZE+1;k++)
        {
            /* Compute amplitude and phase */
            ALphasor component{rect2polar(state->FFTbuffer[k])};

            /* Compute phase difference and subtract expected phase difference */
            double tmp{(component.Phase - state->LastPhase[k]) - k*expected};

            /* Map delta phase into +/- Pi interval */
            int qpd{double2int(tmp / M_PI)};
            tmp -= M_PI * (qpd + (qpd%2));

            /* Get deviation from bin frequency from the +/- Pi interval */
            tmp /= expected;

            /* Compute the k-th partials' true frequency, twice the amplitude
             * for maintain the gain (because half of bins are used) and store
             * amplitude and true frequency in analysis buffer.
             */
            state->Analysis_buffer[k].Amplitude = 2.0 * component.Amplitude;
            state->Analysis_buffer[k].Frequency = (k + tmp) * freq_per_bin;

            /* Store actual phase[k] for the calculations in the next frame*/
            state->LastPhase[k] = component.Phase;
        }

        /* PROCESSING */
        /* pitch shifting */
        for(ALsizei k{0};k < STFT_HALF_SIZE+1;k++)
        {
            state->Syntesis_buffer[k].Amplitude = 0.0;
            state->Syntesis_buffer[k].Frequency = 0.0;
        }

        for(ALsizei k{0};k < STFT_HALF_SIZE+1;k++)
        {
            ALsizei j{(k*state->PitchShiftI) >> FRACTIONBITS};
            if(j >= STFT_HALF_SIZE+1) break;

            state->Syntesis_buffer[j].Amplitude += state->Analysis_buffer[k].Amplitude;
            state->Syntesis_buffer[j].Frequency  = state->Analysis_buffer[k].Frequency *
                                                   state->PitchShift;
        }

        /* SYNTHESIS */
        /* Synthesis the processing data */
        for(ALsizei k{0};k < STFT_HALF_SIZE+1;k++)
        {
            ALphasor component;
            ALdouble tmp;

            /* Compute bin deviation from scaled freq */
            tmp = state->Syntesis_buffer[k].Frequency/freq_per_bin - k;

            /* Calculate actual delta phase and accumulate it to get bin phase */
            state->SumPhase[k] += (k + tmp) * expected;

            component.Amplitude = state->Syntesis_buffer[k].Amplitude;
            component.Phase     = state->SumPhase[k];

            /* Compute phasor component to cartesian complex number and storage it into FFTbuffer*/
            state->FFTbuffer[k] = polar2rect(component);
        }
        /* zero negative frequencies for recontruct a real signal */
        for(ALsizei k{STFT_HALF_SIZE+1};k < STFT_SIZE;k++)
            state->FFTbuffer[k] = complex_d{};

        /* Apply iFFT to buffer data */
        complex_fft(state->FFTbuffer, STFT_SIZE, 1.0);

        /* Windowing and add to output */
        for(ALsizei k{0};k < STFT_SIZE;k++)
            state->OutputAccum[k] += HannWindow[k] * state->FFTbuffer[k].real() /
                                     (0.5 * STFT_HALF_SIZE * OVERSAMP);

        /* Shift accumulator, input & output FIFO */
        ALsizei j, k;
        for(k = 0;k < STFT_STEP;k++) state->OutFIFO[k] = (ALfloat)state->OutputAccum[k];
        for(j = 0;k < STFT_SIZE;k++,j++) state->OutputAccum[j] = state->OutputAccum[k];
        for(;j < STFT_SIZE;j++) state->OutputAccum[j] = 0.0;
        for(k = 0;k < FIFO_LATENCY;k++)
            state->InFIFO[k] = state->InFIFO[k+STFT_STEP];
    }
    state->count = count;

    /* Now, mix the processed sound data to the output. */
    MixSamples(bufferOut, NumChannels, SamplesOut, state->CurrentGains, state->TargetGains,
               maxi(SamplesToDo, 512), 0, SamplesToDo);
}

} // namespace

struct PshifterStateFactory final : public EffectStateFactory {
    PshifterStateFactory() noexcept;
};

static ALeffectState *PshifterStateFactory_create(PshifterStateFactory *UNUSED(factory))
{
    ALpshifterState *state;

    NEW_OBJ0(state, ALpshifterState)();
    if(!state) return NULL;

    return STATIC_CAST(ALeffectState, state);
}

DEFINE_EFFECTSTATEFACTORY_VTABLE(PshifterStateFactory);


PshifterStateFactory::PshifterStateFactory() noexcept
  : EffectStateFactory{GET_VTABLE2(PshifterStateFactory, EffectStateFactory)}
{
}

EffectStateFactory *PshifterStateFactory_getFactory(void)
{
    static PshifterStateFactory PshifterFactory{};
    return STATIC_CAST(EffectStateFactory, &PshifterFactory);
}


void ALpshifter_setParamf(ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALfloat UNUSED(val))
{
    alSetError( context, AL_INVALID_ENUM, "Invalid pitch shifter float property 0x%04x", param );
}

void ALpshifter_setParamfv(ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, const ALfloat *UNUSED(vals))
{
    alSetError( context, AL_INVALID_ENUM, "Invalid pitch shifter float-vector property 0x%04x", param );
}

void ALpshifter_setParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val)
{
    ALeffectProps *props = &effect->Props;
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
            alSetError(context, AL_INVALID_ENUM, "Invalid pitch shifter integer property 0x%04x", param);
    }
}
void ALpshifter_setParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{
    ALpshifter_setParami(effect, context, param, vals[0]);
}

void ALpshifter_getParami(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *val)
{
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_PITCH_SHIFTER_COARSE_TUNE:
            *val = (ALint)props->Pshifter.CoarseTune;
            break;
        case AL_PITCH_SHIFTER_FINE_TUNE:
            *val = (ALint)props->Pshifter.FineTune;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid pitch shifter integer property 0x%04x", param);
    }
}
void ALpshifter_getParamiv(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{
    ALpshifter_getParami(effect, context, param, vals);
}

void ALpshifter_getParamf(const ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALfloat *UNUSED(val))
{
    alSetError(context, AL_INVALID_ENUM, "Invalid pitch shifter float property 0x%04x", param);
}

void ALpshifter_getParamfv(const ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALfloat *UNUSED(vals))
{
    alSetError(context, AL_INVALID_ENUM, "Invalid pitch shifter float vector-property 0x%04x", param);
}

DEFINE_ALEFFECT_VTABLE(ALpshifter);
