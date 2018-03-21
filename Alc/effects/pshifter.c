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
#include "alFilter.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
#include "alu.h"

#define MAX_SIZE 2048

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

    /* Effect gains for each channel */
    ALfloat Gain[MAX_OUTPUT_CHANNELS];

    /* Effect parameters */
    ALsizei   count;
    ALsizei   STFT_size;
    ALsizei   step;
    ALsizei   FIFOLatency;
    ALsizei   oversamp;
    ALfloat   PitchShift;
    ALfloat   Frequency;

    /*Effects buffers*/
    ALfloat   InFIFO[MAX_SIZE];
    ALfloat   OutFIFO[MAX_SIZE];
    ALfloat   LastPhase[(MAX_SIZE>>1) +1];
    ALfloat   SumPhase[(MAX_SIZE>>1) +1];
    ALfloat   OutputAccum[MAX_SIZE<<1];
    ALfloat   window[MAX_SIZE];

    ALcomplex FFTbuffer[MAX_SIZE];

    ALfrequencyDomain Analysis_buffer[MAX_SIZE];
    ALfrequencyDomain Syntesis_buffer[MAX_SIZE];
} ALpshifterState;

static ALvoid ALpshifterState_Destruct(ALpshifterState *state);
static ALboolean ALpshifterState_deviceUpdate(ALpshifterState *state, ALCdevice *device);
static ALvoid ALpshifterState_update(ALpshifterState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props);
static ALvoid ALpshifterState_process(ALpshifterState *state, ALsizei SamplesToDo, const ALfloat (*restrict SamplesIn)[BUFFERSIZE], ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALsizei NumChannels);
DECLARE_DEFAULT_ALLOCATORS(ALpshifterState)

DEFINE_ALEFFECTSTATE_VTABLE(ALpshifterState);


/* Converts ALcomplex to ALphasor*/
static inline ALphasor rect2polar( ALcomplex number )
{
    ALphasor polar;

    polar.Amplitude =  sqrtf ( number.Real*number.Real + number.Imag*number.Imag );
    polar.Phase     =  atan2f( number.Imag , number.Real );

    return polar;
}

/* Converts ALphasor to ALcomplex*/
static inline ALcomplex polar2rect( ALphasor  number )
{
    ALcomplex cartesian;

    cartesian.Real   = number.Amplitude * cosf( number.Phase );
    cartesian.Imag   = number.Amplitude * sinf( number.Phase );

    return cartesian;
}

/* Addition of two complex numbers (ALcomplex format)*/
static inline ALcomplex complex_add( ALcomplex a, ALcomplex b )
{
    ALcomplex result;

    result.Real = ( a.Real + b.Real );
    result.Imag = ( a.Imag + b.Imag );

    return result;
}

/* Substraction of two complex numbers (ALcomplex format)*/
static inline ALcomplex complex_subst( ALcomplex a, ALcomplex b )
{
    ALcomplex result;

    result.Real = ( a.Real - b.Real );
    result.Imag = ( a.Imag - b.Imag );

    return result;
}

/* Multiplication of two complex numbers (ALcomplex format)*/
static inline ALcomplex complex_mult( ALcomplex a, ALcomplex b )
{
    ALcomplex result;

    result.Real = ( a.Real * b.Real - a.Imag * b.Imag );
    result.Imag = ( a.Imag * b.Real + a.Real * b.Imag );

    return result;
}

/* Iterative implementation of 2-radix FFT (In-place algorithm). Sign = -1 is FFT and 1 is
   iFFT (inverse). Fills FFTBuffer[0...FFTSize-1] with the Discrete Fourier Transform (DFT) 
   of the time domain data stored in FFTBuffer[0...FFTSize-1]. FFTBuffer is an array of
   complex numbers (ALcomplex), FFTSize MUST BE power of two.*/
static inline ALvoid FFT(ALcomplex *FFTBuffer, ALsizei FFTSize, ALint Sign)
{
    ALfloat arg;
    ALsizei i, j, k, mask, step, step2;
    ALcomplex temp, u, w;

    /*bit-reversal permutation applied to a sequence of FFTSize items*/
    for (i = 1; i < FFTSize-1; i++ )
    {
         for ( mask = 0x1, j = 0; mask < FFTSize; mask <<= 1 )
         {
              if ( ( i & mask ) != 0 ) j++;

              j <<= 1;
         }

         j >>= 1;

         if ( i < j )
         {
              temp         = FFTBuffer[i];
              FFTBuffer[i] = FFTBuffer[j];
              FFTBuffer[j] = temp;
         }
    }

    /* Iterative form of Danielson–Lanczos lemma */
    for ( i = 1, step = 2; i < FFTSize; i<<=1, step <<= 1 )
    {
         step2  = step >> 1;
         arg    = F_PI / step2;

         w.Real = cosf( arg );
         w.Imag = sinf( arg ) * Sign;

         u.Real = 1.0f;
         u.Imag = 0.0f;

         for ( j = 0; j < step2; j++ )
         {
             for ( k = j; k < FFTSize; k += step )
             {
                  temp               = complex_mult( FFTBuffer[k+step2], u );
                  FFTBuffer[k+step2] = complex_subst( FFTBuffer[k], temp );
                  FFTBuffer[k]       = complex_add( FFTBuffer[k], temp );
             }

             u = complex_mult(u,w);
         }
    }
}


static void ALpshifterState_Construct(ALpshifterState *state)
{
    ALsizei i;

    ALeffectState_Construct(STATIC_CAST(ALeffectState, state));
    SET_VTABLE2(ALpshifterState, ALeffectState, state);

    /*Initializing parameters and set to zero the buffers */
    state->STFT_size     =  MAX_SIZE>>1;
    state->oversamp      =  1<<2;

    state->step          =  state->STFT_size / state->oversamp ;
    state->FIFOLatency   =  state->step * ( state->oversamp-1 );
    state->count         =  state->FIFOLatency;

    memset(state->InFIFO,          0, sizeof(state->InFIFO));
    memset(state->OutFIFO,         0, sizeof(state->OutFIFO));
    memset(state->FFTbuffer,       0, sizeof(state->FFTbuffer));
    memset(state->LastPhase,       0, sizeof(state->LastPhase));
    memset(state->SumPhase,        0, sizeof(state->SumPhase));
    memset(state->OutputAccum,     0, sizeof(state->OutputAccum));
    memset(state->Analysis_buffer, 0, sizeof(state->Analysis_buffer));

    /* Create lockup table of the Hann window for the desired size, i.e. STFT_size */
    for ( i = 0; i < state->STFT_size>>1 ; i++ )
    {
         state->window[i] = state->window[state->STFT_size-(i+1)]                                 \
                          = 0.5f * ( 1 - cosf(F_TAU*(ALfloat)i/(ALfloat)(state->STFT_size-1)));
    }
}

static ALvoid ALpshifterState_Destruct(ALpshifterState *state)
{
    ALeffectState_Destruct(STATIC_CAST(ALeffectState,state));
}

static ALboolean ALpshifterState_deviceUpdate(ALpshifterState *UNUSED(state), ALCdevice *UNUSED(device))
{
    return AL_TRUE;
}

static ALvoid ALpshifterState_update(ALpshifterState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props)
{
    const ALCdevice *device = context->Device;
    ALfloat coeffs[MAX_AMBI_COEFFS];
    const ALfloat adjust = 0.707945784384f; /*-3dB adjust*/

    state->Frequency  = (ALfloat)device->Frequency;
    state->PitchShift = powf(2.0f,((ALfloat)props->Pshifter.CoarseTune + props->Pshifter.FineTune/100.0f)/12.0f);

    CalcAngleCoeffs(0.0f, 0.0f, 0.0f, coeffs);
    ComputeDryPanGains(&device->Dry, coeffs, slot->Params.Gain * adjust, state->Gain);
}

static ALvoid ALpshifterState_process(ALpshifterState *state, ALsizei SamplesToDo, const ALfloat (*restrict SamplesIn)[BUFFERSIZE], ALfloat (*restrict SamplesOut)[BUFFERSIZE], ALsizei NumChannels)
{
    /* Pitch shifter engine based on the work of Stephan Bernsee.
     * http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/
     */

    ALsizei i, j, k, STFT_half_size;
    ALfloat freq_bin, expected, tmp;
    ALfloat bufferOut[BUFFERSIZE];
    ALphasor component;

    STFT_half_size = state->STFT_size >> 1;
    freq_bin       = state->Frequency / (ALfloat)state->STFT_size;
    expected       = F_TAU / (ALfloat)state->oversamp;

    for (i = 0; i < SamplesToDo; i++)
    {
        /* Fill FIFO buffer with samples data */
        state->InFIFO[state->count] = SamplesIn[0][i];
        bufferOut[i]                = state->OutFIFO[state->count - state->FIFOLatency];

        state->count++;

        /* Check whether FIFO buffer is filled */
        if ( state->count >= state->STFT_size )
        {
            state->count = state->FIFOLatency;

            /* Real signal windowing and store in FFTbuffer */
            for ( k = 0; k < state->STFT_size; k++ )
            {
                state->FFTbuffer[k].Real = state->InFIFO[k] * state->window[k];
                state->FFTbuffer[k].Imag = 0.0f;
            }

            /* ANALYSIS */
            /* Apply FFT to FFTbuffer data */
            FFT( state->FFTbuffer, state->STFT_size, -1 );

            /* Analyze the obtained data. Since the real FFT is symmetric, only
             * STFT_half_size+1 samples are needed.
             */
            for ( k = 0; k <= STFT_half_size; k++ )
            {
                /* Compute amplitude and phase */
                component = rect2polar( state->FFTbuffer[k] );

                /* Compute phase difference and subtract expected phase difference */
                tmp = ( component.Phase - state->LastPhase[k] ) - (ALfloat)k*expected;

                /* Map delta phase into +/- Pi interval */
                tmp -= F_PI*(ALfloat)( fastf2i(tmp/F_PI) + fastf2i(tmp/F_PI) % 2 );

                /* Get deviation from bin frequency from the +/- Pi interval */
                tmp /= expected;

                /* Compute the k-th partials' true frequency, twice the
                 * amplitude for maintain the gain (because half of bins are
                 * used) and store amplitude and true frequency in analysis
                 * buffer.
                 */
                state->Analysis_buffer[k].Amplitude = 2.0f * component.Amplitude;
                state->Analysis_buffer[k].Frequency = ((ALfloat)k + tmp) * freq_bin;

                /* Store actual phase[k] for the calculations in the next frame*/
                state->LastPhase[k] = component.Phase;
            }

            /* PROCESSING */
            /* pitch shifting */
            memset(state->Syntesis_buffer, 0, state->STFT_size*sizeof(ALfrequencyDomain));

            for (k = 0; k <= STFT_half_size; k++)
            {
                j = fastf2i( (ALfloat)k*state->PitchShift );

                if ( j <= STFT_half_size )
                {
                    state->Syntesis_buffer[j].Amplitude += state->Analysis_buffer[k].Amplitude; 
                    state->Syntesis_buffer[j].Frequency  = state->Analysis_buffer[k].Frequency *
                                                           state->PitchShift;
                }
            }

            /* SYNTHESIS */
            /* Synthesis the processing data */
            for ( k = 0; k <= STFT_half_size; k++ )
            {
                /* Compute bin deviation from scaled freq */
                tmp = state->Syntesis_buffer[k].Frequency /freq_bin - (ALfloat)k;

                /* Calculate actual delta phase and accumulate it to get bin phase */
                state->SumPhase[k] += ((ALfloat)k + tmp) * expected;

                component.Amplitude = state->Syntesis_buffer[k].Amplitude;
                component.Phase     = state->SumPhase[k];

                /* Compute phasor component to cartesian complex number and storage it into FFTbuffer*/
                state->FFTbuffer[k] = polar2rect( component );
            }

            /* zero negative frequencies for recontruct a real signal */
            memset( &state->FFTbuffer[STFT_half_size+1], 0, (STFT_half_size-1) * sizeof(ALcomplex) );

            /* Apply iFFT to buffer data */
            FFT( state->FFTbuffer, state->STFT_size, 1 );

            /* Windowing and add to output */
            for( k=0; k < state->STFT_size; k++ )
            {
                state->OutputAccum[k] += 2.0f * state->window[k]*state->FFTbuffer[k].Real /
                                         (STFT_half_size * state->oversamp);
            }

            /* Shift accumulator, input & output FIFO */
            memmove(state->OutFIFO    , state->OutputAccum            , state->step       *sizeof(ALfloat));
            memmove(state->OutputAccum, state->OutputAccum+state->step, state->STFT_size  *sizeof(ALfloat));
            memmove(state->InFIFO     , state->InFIFO     +state->step, state->FIFOLatency*sizeof(ALfloat));
        }
    }

    /* Now, mix the processed sound data to the output*/
    for (j = 0; j < NumChannels; j++ )
    {
        ALfloat gain = state->Gain[j];

        if(!(fabsf(gain) > GAIN_SILENCE_THRESHOLD))
             continue;

        for(i = 0;i < SamplesToDo;i++)
            SamplesOut[j][i] += gain * bufferOut[i];
    }
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
