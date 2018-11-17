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

#define HIL_SIZE 1024
#define OVERSAMP (1<<2)

#define HIL_STEP     (HIL_SIZE / OVERSAMP)
#define FIFO_LATENCY (HIL_STEP * (OVERSAMP-1))

/* Define a Hann window, used to filter the HIL input and output. */
/* Making this constexpr seems to require C++14. */
std::array<ALdouble,HIL_SIZE> InitHannWindow(void)
{
    std::array<ALdouble,HIL_SIZE> ret;
    /* Create lookup table of the Hann window for the desired size, i.e. HIL_SIZE */
    for(ALsizei i{0};i < HIL_SIZE>>1;i++)
    {
        ALdouble val = std::sin(M_PI * (ALdouble)i / (ALdouble)(HIL_SIZE-1));
        ret[i] = ret[HIL_SIZE-1-i] = val * val;
    }
    return ret;
}
alignas(16) const std::array<ALdouble,HIL_SIZE> HannWindow = InitHannWindow();


struct ALfshifterState final : public ALeffectState {
    /* Effect parameters */
    ALsizei  count;
    ALsizei  PhaseStep;
    ALsizei  Phase;
    ALdouble ld_sign;

    /*Effects buffers*/ 
    ALfloat   InFIFO[HIL_SIZE];
    complex_d OutFIFO[HIL_SIZE];
    complex_d OutputAccum[HIL_SIZE];
    complex_d Analytic[HIL_SIZE];
    complex_d Outdata[BUFFERSIZE];

    alignas(16) ALfloat BufferOut[BUFFERSIZE];

    /* Effect gains for each output channel */
    ALfloat CurrentGains[MAX_OUTPUT_CHANNELS];
    ALfloat TargetGains[MAX_OUTPUT_CHANNELS];
};

ALvoid ALfshifterState_Destruct(ALfshifterState *state);
ALboolean ALfshifterState_deviceUpdate(ALfshifterState *state, ALCdevice *device);
ALvoid ALfshifterState_update(ALfshifterState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props);
ALvoid ALfshifterState_process(ALfshifterState *state, ALsizei SamplesToDo, const ALfloat (*RESTRICT SamplesIn)[BUFFERSIZE], ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE], ALsizei NumChannels);
DECLARE_DEFAULT_ALLOCATORS(ALfshifterState)

DEFINE_ALEFFECTSTATE_VTABLE(ALfshifterState);

void ALfshifterState_Construct(ALfshifterState *state)
{
    new (state) ALfshifterState{};
    ALeffectState_Construct(STATIC_CAST(ALeffectState, state));
    SET_VTABLE2(ALfshifterState, ALeffectState, state);
}

ALvoid ALfshifterState_Destruct(ALfshifterState *state)
{
    ALeffectState_Destruct(STATIC_CAST(ALeffectState,state));
    state->~ALfshifterState();
}

ALboolean ALfshifterState_deviceUpdate(ALfshifterState *state, ALCdevice *UNUSED(device))
{
    /* (Re-)initializing parameters and clear the buffers. */
    state->count     = FIFO_LATENCY;
    state->PhaseStep = 0;
    state->Phase     = 0;
    state->ld_sign   = 1.0;

    std::fill(std::begin(state->InFIFO),      std::end(state->InFIFO),      0.0f);
    std::fill(std::begin(state->OutFIFO),     std::end(state->OutFIFO),     complex_d{});
    std::fill(std::begin(state->OutputAccum), std::end(state->OutputAccum), complex_d{});
    std::fill(std::begin(state->Analytic),    std::end(state->Analytic),    complex_d{});

    std::fill(std::begin(state->CurrentGains), std::end(state->CurrentGains), 0.0f);
    std::fill(std::begin(state->TargetGains),  std::end(state->TargetGains),  0.0f);

    return AL_TRUE;
}

ALvoid ALfshifterState_update(ALfshifterState *state, const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props)
{
    const ALCdevice *device = context->Device;
    ALfloat coeffs[MAX_AMBI_COEFFS];
    ALfloat step;

    step = props->Fshifter.Frequency / (ALfloat)device->Frequency;
    state->PhaseStep = fastf2i(minf(step, 0.5f) * FRACTIONONE);

    switch(props->Fshifter.LeftDirection)
    {
        case AL_FREQUENCY_SHIFTER_DIRECTION_DOWN:
            state->ld_sign = -1.0;
            break;

        case AL_FREQUENCY_SHIFTER_DIRECTION_UP:
            state->ld_sign = 1.0;
            break;

        case AL_FREQUENCY_SHIFTER_DIRECTION_OFF:
            state->Phase = 0;
            state->PhaseStep = 0;
            break;
    }

    CalcAngleCoeffs(0.0f, 0.0f, 0.0f, coeffs);
    ComputePanGains(&device->Dry, coeffs, slot->Params.Gain, state->TargetGains);
}

ALvoid ALfshifterState_process(ALfshifterState *state, ALsizei SamplesToDo, const ALfloat (*RESTRICT SamplesIn)[BUFFERSIZE], ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE], ALsizei NumChannels)
{
    static const complex_d complex_zero{0.0, 0.0};
    ALfloat *RESTRICT BufferOut = state->BufferOut;
    ALsizei j, k, base;

    for(base = 0;base < SamplesToDo;)
    {
        ALsizei todo = mini(HIL_SIZE-state->count, SamplesToDo-base);

        ASSUME(todo > 0);

        /* Fill FIFO buffer with samples data */
        k = state->count;
        for(j = 0;j < todo;j++,k++)
        {
            state->InFIFO[k] = SamplesIn[0][base+j];
            state->Outdata[base+j]  = state->OutFIFO[k-FIFO_LATENCY];
        }
        state->count += todo;
        base += todo;

        /* Check whether FIFO buffer is filled */
        if(state->count < HIL_SIZE) continue;

        state->count = FIFO_LATENCY;

        /* Real signal windowing and store in Analytic buffer */
        for(k = 0;k < HIL_SIZE;k++)
        {
            state->Analytic[k].real(state->InFIFO[k] * HannWindow[k]);
            state->Analytic[k].imag(0.0);
        }

        /* Processing signal by Discrete Hilbert Transform (analytical signal). */
        complex_hilbert(state->Analytic, HIL_SIZE);

        /* Windowing and add to output accumulator */
        for(k = 0;k < HIL_SIZE;k++)
            state->OutputAccum[k] += 2.0/OVERSAMP*HannWindow[k]*state->Analytic[k];

        /* Shift accumulator, input & output FIFO */
        for(k = 0;k < HIL_STEP;k++) state->OutFIFO[k] = state->OutputAccum[k];
        for(j = 0;k < HIL_SIZE;k++,j++) state->OutputAccum[j] = state->OutputAccum[k];
        for(;j < HIL_SIZE;j++) state->OutputAccum[j] = complex_zero;
        for(k = 0;k < FIFO_LATENCY;k++)
            state->InFIFO[k] = state->InFIFO[k+HIL_STEP];
    }

    /* Process frequency shifter using the analytic signal obtained. */
    for(k = 0;k < SamplesToDo;k++)
    {
        double phase = state->Phase * ((1.0/FRACTIONONE) * 2.0*M_PI);
        BufferOut[k] = (float)(state->Outdata[k].real()*std::cos(phase) +
                               state->Outdata[k].imag()*std::sin(phase)*state->ld_sign);

        state->Phase += state->PhaseStep;
        state->Phase &= FRACTIONMASK;
    }

    /* Now, mix the processed sound data to the output. */
    MixSamples(BufferOut, NumChannels, SamplesOut, state->CurrentGains, state->TargetGains,
               maxi(SamplesToDo, 512), 0, SamplesToDo);
}

} // namespace

struct FshifterStateFactory final : public EffectStateFactory {
    FshifterStateFactory() noexcept;
};

static ALeffectState *FshifterStateFactory_create(FshifterStateFactory *UNUSED(factory))
{
    ALfshifterState *state;

    NEW_OBJ0(state, ALfshifterState)();
    if(!state) return NULL;

    return STATIC_CAST(ALeffectState, state);
}

DEFINE_EFFECTSTATEFACTORY_VTABLE(FshifterStateFactory);

FshifterStateFactory::FshifterStateFactory() noexcept
  : EffectStateFactory{GET_VTABLE2(FshifterStateFactory, EffectStateFactory)}
{
}

EffectStateFactory *FshifterStateFactory_getFactory(void)
{
    static FshifterStateFactory FshifterFactory{};
    return STATIC_CAST(EffectStateFactory, &FshifterFactory);
}

void ALfshifter_setParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_FREQUENCY_SHIFTER_FREQUENCY:
            if(!(val >= AL_FREQUENCY_SHIFTER_MIN_FREQUENCY && val <= AL_FREQUENCY_SHIFTER_MAX_FREQUENCY))
                SETERR_RETURN(context, AL_INVALID_VALUE,,"Frequency shifter frequency out of range");
            props->Fshifter.Frequency = val;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid frequency shifter float property 0x%04x", param);
    }
}

void ALfshifter_setParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{
    ALfshifter_setParamf(effect, context, param, vals[0]);
}

void ALfshifter_setParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_FREQUENCY_SHIFTER_LEFT_DIRECTION:
            if(!(val >= AL_FREQUENCY_SHIFTER_MIN_LEFT_DIRECTION && val <= AL_FREQUENCY_SHIFTER_MAX_LEFT_DIRECTION))
                SETERR_RETURN(context, AL_INVALID_VALUE,,"Frequency shifter left direction out of range");
            props->Fshifter.LeftDirection = val;
            break;

        case AL_FREQUENCY_SHIFTER_RIGHT_DIRECTION:
            if(!(val >= AL_FREQUENCY_SHIFTER_MIN_RIGHT_DIRECTION && val <= AL_FREQUENCY_SHIFTER_MAX_RIGHT_DIRECTION))
                SETERR_RETURN(context, AL_INVALID_VALUE,,"Frequency shifter right direction out of range");
            props->Fshifter.RightDirection = val;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid frequency shifter integer property 0x%04x", param);
    }
}
void ALfshifter_setParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{
    ALfshifter_setParami(effect, context, param, vals[0]);
}

void ALfshifter_getParami(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *val)
{
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_FREQUENCY_SHIFTER_LEFT_DIRECTION:
            *val = props->Fshifter.LeftDirection;
            break;
        case AL_FREQUENCY_SHIFTER_RIGHT_DIRECTION:
            *val = props->Fshifter.RightDirection;
            break;
        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid frequency shifter integer property 0x%04x", param);
    }
}
void ALfshifter_getParamiv(const ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{
    ALfshifter_getParami(effect, context, param, vals);
}

void ALfshifter_getParamf(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val)
{

    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_FREQUENCY_SHIFTER_FREQUENCY:
            *val = props->Fshifter.Frequency;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM, "Invalid frequency shifter float property 0x%04x", param);
    }

}

void ALfshifter_getParamfv(const ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{
    ALfshifter_getParamf(effect, context, param, vals);
}

DEFINE_ALEFFECT_VTABLE(ALfshifter);
