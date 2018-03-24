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

#include <math.h>
#include <stdlib.h>

#include "alMain.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
#include "alu.h"
#include "filters/defs.h"


#define STFT_SIZE      1024
#define STFT_HALF_SIZE (STFT_SIZE>>1)
#define OVERSAMP       (1<<2)

#define STFT_STEP    (STFT_SIZE / OVERSAMP)
#define FIFO_LATENCY (STFT_STEP * (OVERSAMP-1))

typedef struct ALcomplex {
    ALfloat Real;
    ALfloat Imag;
} ALcomplex;

typedef struct ALphasor {
    ALfloat Amplitude;
    ALfloat Phase;
} ALphasor;

typedef struct ALFrequencyDomain {
    ALfloat Amplitude;
    ALfloat Frequency;
} ALfrequencyDomain;

typedef struct ALpshifterState {
    DERIVE_FROM_TYPE(ALeffectState);

    /* Effect parameters */
    ALsizei count;
    ALfloat PitchShift;
    ALfloat FreqPerBin;

    /*Effects buffers*/
    ALfloat InFIFO[STFT_SIZE];
    ALfloat OutFIFO[STFT_STEP];
    ALfloat LastPhase[STFT_HALF_SIZE+1];
    ALfloat SumPhase[STFT_HALF_SIZE+1];
    ALfloat OutputAccum[STFT_SIZE];

    ALcomplex FFTbuffer[STFT_SIZE];

    ALfrequencyDomain Analysis_buffer[STFT_HALF_SIZE+1];
    ALfrequencyDomain Syntesis_buffer[STFT_HALF_SIZE+1];

    alignas(16) ALfloat BufferOut[BUFFERSIZE];

    /* Effect gains for each output channel */
    ALfloat CurrentGains[MAX_OUTPUT_CHANNELS];
    ALfloat TargetGains[MAX_OUTPUT_CHANNELS];
} ALpshifterState;

static ALvoid ALpshifterState_Destruct(ALpshifterState *state);
static ALboolean ALpshifterState_deviceUpdate(ALpshifterState *state, ALCdevice *device);
static ALvoid ALpshifterState_update(ALpshifterState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props);
static ALvoid ALpshifterState_process(ALpshifterState *state, ALsizei SamplesToDo, const ALfloat (*restrict SamplesIn)[BUFFERSIZE], ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALsizei NumChannels);
DECLARE_DEFAULT_ALLOCATORS(ALpshifterState)

DEFINE_ALEFFECTSTATE_VTABLE(ALpshifterState);


/* Define a Hanning window, used to filter the STFT input and output. */
alignas(16) static ALfloat HanningWindow[STFT_SIZE];

static void InitHanningWindow(void)
{
    ALsizei i;

    /* Create lookup table of the Hanning window for the desired size, i.e. STFT_SIZE */
    for(i = 0;i < STFT_SIZE>>1;i++)
    {
        ALdouble val = 1.0 - cos((ALdouble)i / (ALdouble)(STFT_SIZE-1) * (M_PI*2.0));
        HanningWindow[i] = HanningWindow[STFT_SIZE-(i+1)] = (ALfloat)val * 0.5f;
    }
}
static alonce_flag HanningInitOnce = AL_ONCE_FLAG_INIT;


/* Converts ALcomplex to ALphasor */
static inline ALphasor rect2polar(ALcomplex number)
{
    ALphasor polar;

    polar.Amplitude = sqrtf(number.Real*number.Real + number.Imag*number.Imag);
    polar.Phase     = atan2f(number.Imag , number.Real);

    return polar;
}

/* Converts ALphasor to ALcomplex */
static inline ALcomplex polar2rect(ALphasor  number)
{
    ALcomplex cartesian;

    cartesian.Real = number.Amplitude * cosf(number.Phase);
    cartesian.Imag = number.Amplitude * sinf(number.Phase);

    return cartesian;
}

/* Addition of two complex numbers (ALcomplex format) */
static inline ALcomplex complex_add(ALcomplex a, ALcomplex b)
{
    ALcomplex result;

    result.Real = a.Real + b.Real;
    result.Imag = a.Imag + b.Imag;

    return result;
}

/* Subtraction of two complex numbers (ALcomplex format) */
static inline ALcomplex complex_sub(ALcomplex a, ALcomplex b)
{
    ALcomplex result;

    result.Real = a.Real - b.Real;
    result.Imag = a.Imag - b.Imag;

    return result;
}

/* Multiplication of two complex numbers (ALcomplex format) */
static inline ALcomplex complex_mult(ALcomplex a, ALcomplex b)
{
    ALcomplex result;

    result.Real = a.Real*b.Real - a.Imag*b.Imag;
    result.Imag = a.Imag*b.Real + a.Real*b.Imag;

    return result;
}

/* Iterative implementation of 2-radix FFT (In-place algorithm). Sign = -1 is
 * FFT and 1 is iFFT (inverse). Fills FFTBuffer[0...FFTSize-1] with the
 * Discrete Fourier Transform (DFT) of the time domain data stored in
 * FFTBuffer[0...FFTSize-1]. FFTBuffer is an array of complex numbers
 * (ALcomplex), FFTSize MUST BE power of two.
 */
static inline ALvoid FFT(ALcomplex *FFTBuffer, ALsizei FFTSize, ALfloat Sign)
{
    ALsizei i, j, k, mask, step, step2;
    ALcomplex temp, u, w;
    ALfloat arg;

    /* Bit-reversal permutation applied to a sequence of FFTSize items */
    for(i = 1;i < FFTSize-1;i++)
    {
        for(mask = 0x1, j = 0;mask < FFTSize;mask <<= 1)
        {
            if((i&mask) != 0)
                j++;
            j <<= 1;
        }
        j >>= 1;

        if(i < j)
        {
            temp         = FFTBuffer[i];
            FFTBuffer[i] = FFTBuffer[j];
            FFTBuffer[j] = temp;
        }
    }

    /* Iterative form of Danielson�Lanczos lemma */
    for(i = 1, step = 2;i < FFTSize;i<<=1, step<<=1)
    {
        step2 = step >> 1;
        arg   = F_PI / step2;

        w.Real = cosf(arg);
        w.Imag = sinf(arg) * Sign;

        u.Real = 1.0f;
        u.Imag = 0.0f;

        for(j = 0;j < step2;j++)
        {
            for(k = j;k < FFTSize;k+=step)
            {
                temp               = complex_mult(FFTBuffer[k+step2], u);
                FFTBuffer[k+step2] = complex_sub(FFTBuffer[k], temp);
                FFTBuffer[k]       = complex_add(FFTBuffer[k], temp);
            }

            u = complex_mult(u, w);
        }
    }
}


static void ALpshifterState_Construct(ALpshifterState *state)
{
    ALeffectState_Construct(STATIC_CAST(ALeffectState, state));
    SET_VTABLE2(ALpshifterState, ALeffectState, state);

    alcall_once(&HanningInitOnce, InitHanningWindow);
}

static ALvoid ALpshifterState_Destruct(ALpshifterState *state)
{
    ALeffectState_Destruct(STATIC_CAST(ALeffectState,state));
}

static ALboolean ALpshifterState_deviceUpdate(ALpshifterState *state, ALCdevice *device)
{
    /* (Re-)initializing parameters and clear the buffers. */
    state->count      = FIFO_LATENCY;
    state->PitchShift = 1.0f;
    state->FreqPerBin = device->Frequency / (ALfloat)STFT_SIZE;

    memset(state->InFIFO,          0, sizeof(state->InFIFO));
    memset(state->OutFIFO,         0, sizeof(state->OutFIFO));
    memset(state->FFTbuffer,       0, sizeof(state->FFTbuffer));
    memset(state->LastPhase,       0, sizeof(state->LastPhase));
    memset(state->SumPhase,        0, sizeof(state->SumPhase));
    memset(state->OutputAccum,     0, sizeof(state->OutputAccum));
    memset(state->Analysis_buffer, 0, sizeof(state->Analysis_buffer));
    memset(state->Syntesis_buffer, 0, sizeof(state->Syntesis_buffer));

    memset(state->CurrentGains, 0, sizeof(state->CurrentGains));
    memset(state->TargetGains,  0, sizeof(state->TargetGains));

    return AL_TRUE;
}

static ALvoid ALpshifterState_update(ALpshifterState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props)
{
    const ALCdevice *device = context->Device;
    ALfloat coeffs[MAX_AMBI_COEFFS];

    state->PitchShift = powf(2.0f,
        (ALfloat)(props->Pshifter.CoarseTune*100 + props->Pshifter.FineTune) / 1200.0f
    );

    CalcAngleCoeffs(0.0f, 0.0f, 0.0f, coeffs);
    ComputeDryPanGains(&device->Dry, coeffs, slot->Params.Gain, state->TargetGains);
}

static ALvoid ALpshifterState_process(ALpshifterState *state, ALsizei SamplesToDo, const ALfloat (*restrict SamplesIn)[BUFFERSIZE], ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALsizei NumChannels)
{
    /* Pitch shifter engine based on the work of Stephan Bernsee.
     * http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/
     */

    static const ALfloat expected = F_TAU / (ALfloat)OVERSAMP;
    const ALfloat freq_per_bin = state->FreqPerBin;
    ALfloat *restrict bufferOut = state->BufferOut;
    ALsizei count = state->count;
    ALsizei i, j, k;

    for(i = 0;i < SamplesToDo;)
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
        for(k = 0;k < STFT_SIZE;k++)
        {
            state->FFTbuffer[k].Real = state->InFIFO[k] * HanningWindow[k];
            state->FFTbuffer[k].Imag = 0.0f;
        }

        /* ANALYSIS */
        /* Apply FFT to FFTbuffer data */
        FFT(state->FFTbuffer, STFT_SIZE, -1.0f);

        /* Analyze the obtained data. Since the real FFT is symmetric, only
         * STFT_HALF_SIZE+1 samples are needed.
         */
        for(k = 0;k < STFT_HALF_SIZE+1;k++)
        {
            ALphasor component;
            ALfloat tmp;
            ALint qpd;

            /* Compute amplitude and phase */
            component = rect2polar(state->FFTbuffer[k]);

            /* Compute phase difference and subtract expected phase difference */
            tmp = (component.Phase - state->LastPhase[k]) - (ALfloat)k*expected;

            /* Map delta phase into +/- Pi interval */
            qpd = fastf2i(tmp / F_PI);
            tmp -= F_PI * (ALfloat)(qpd + (qpd%2));

            /* Get deviation from bin frequency from the +/- Pi interval */
            tmp /= expected;

            /* Compute the k-th partials' true frequency, twice the amplitude
             * for maintain the gain (because half of bins are used) and store
             * amplitude and true frequency in analysis buffer.
             */
            state->Analysis_buffer[k].Amplitude = 2.0f * component.Amplitude;
            state->Analysis_buffer[k].Frequency = ((ALfloat)k + tmp) * freq_per_bin;

            /* Store actual phase[k] for the calculations in the next frame*/
            state->LastPhase[k] = component.Phase;
        }

        /* PROCESSING */
        /* pitch shifting */
        for(k = 0;k < STFT_HALF_SIZE+1;k++)
        {
            state->Syntesis_buffer[k].Amplitude = 0.0f;
            state->Syntesis_buffer[k].Frequency = 0.0f;
        }

        for(k = 0;k < STFT_HALF_SIZE+1;k++)
        {
            j = fastf2i((ALfloat)k * state->PitchShift);
            if(j >= STFT_HALF_SIZE+1) break;

            state->Syntesis_buffer[j].Amplitude += state->Analysis_buffer[k].Amplitude;
            state->Syntesis_buffer[j].Frequency  = state->Analysis_buffer[k].Frequency *
                                                   state->PitchShift;
        }

        /* SYNTHESIS */
        /* Synthesis the processing data */
        for(k = 0;k < STFT_HALF_SIZE+1;k++)
        {
            ALphasor component;
            ALfloat tmp;

            /* Compute bin deviation from scaled freq */
            tmp = state->Syntesis_buffer[k].Frequency/freq_per_bin - (ALfloat)k;

            /* Calculate actual delta phase and accumulate it to get bin phase */
            state->SumPhase[k] += ((ALfloat)k + tmp) * expected;

            component.Amplitude = state->Syntesis_buffer[k].Amplitude;
            component.Phase     = state->SumPhase[k];

            /* Compute phasor component to cartesian complex number and storage it into FFTbuffer*/
            state->FFTbuffer[k] = polar2rect(component);
        }
        /* zero negative frequencies for recontruct a real signal */
        for(k = STFT_HALF_SIZE+1;k < STFT_SIZE;k++)
        {
            state->FFTbuffer[k].Real = 0.0f;
            state->FFTbuffer[k].Imag = 0.0f;
        }

        /* Apply iFFT to buffer data */
        FFT(state->FFTbuffer, STFT_SIZE, 1.0f);

        /* Windowing and add to output */
        for(k = 0;k < STFT_SIZE;k++)
            state->OutputAccum[k] += 2.0f * HanningWindow[k]*state->FFTbuffer[k].Real /
                                     (STFT_HALF_SIZE * OVERSAMP);

        /* Shift accumulator, input & output FIFO */
        for(k = 0;k < STFT_STEP;k++) state->OutFIFO[k] = state->OutputAccum[k];
        for(j = 0;k < STFT_SIZE;k++,j++) state->OutputAccum[j] = state->OutputAccum[k];
        for(;j < STFT_SIZE;j++) state->OutputAccum[j] = 0.0f;
        for(k = 0;k < FIFO_LATENCY;k++)
            state->InFIFO[k] = state->InFIFO[k+STFT_STEP];
    }
    state->count = count;

    /* Now, mix the processed sound data to the output. */
    MixSamples(bufferOut, NumChannels, SamplesOut, state->CurrentGains, state->TargetGains,
               maxi(SamplesToDo, 512), 0, SamplesToDo);
}

typedef struct PshifterStateFactory {
    DERIVE_FROM_TYPE(EffectStateFactory);
} PshifterStateFactory;

static ALeffectState *PshifterStateFactory_create(PshifterStateFactory *UNUSED(factory))
{
    ALpshifterState *state;

    NEW_OBJ0(state, ALpshifterState)();
    if(!state) return NULL;

    return STATIC_CAST(ALeffectState, state);
}

DEFINE_EFFECTSTATEFACTORY_VTABLE(PshifterStateFactory);

EffectStateFactory *PshifterStateFactory_getFactory(void)
{
    static PshifterStateFactory PshifterFactory = { { GET_VTABLE2(PshifterStateFactory, EffectStateFactory) } };

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
