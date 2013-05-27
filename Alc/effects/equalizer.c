/**
 * OpenAL cross platform audio library
 * Copyright (C) 2013 by Mike Gorchak
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

#include "alMain.h"
#include "alFilter.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
#include "alu.h"


typedef struct ALequalizerStateFactory {
    DERIVE_FROM_TYPE(ALeffectStateFactory);
} ALequalizerStateFactory;

static ALequalizerStateFactory EqualizerFactory;


/*  The document  "Effects Extension Guide.pdf"  says that low and high  *
 *  frequencies are cutoff frequencies. This is not fully correct, they  *
 *  are corner frequencies for low and high shelf filters. If they were  *
 *  just cutoff frequencies, there would be no need in cutoff frequency  *
 *  gains, which are present.  Documentation for  "Creative Proteus X2"  *
 *  software describes  4-band equalizer functionality in a much better  *
 *  way.  This equalizer seems  to be a predecessor  of  OpenAL  4-band  *
 *  equalizer.  With low and high  shelf filters  we are able to cutoff  *
 *  frequencies below and/or above corner frequencies using attenuation  *
 *  gains (below 1.0) and amplify all low and/or high frequencies using  *
 *  gains above 1.0.                                                     *
 *                                                                       *
 *     Low-shelf       Low Mid Band      High Mid Band     High-shelf    *
 *      corner            center             center          corner      *
 *     frequency        frequency          frequency       frequency     *
 *    50Hz..800Hz     200Hz..3000Hz      1000Hz..8000Hz  4000Hz..16000Hz *
 *                                                                       *
 *          |               |                  |               |         *
 *          |               |                  |               |         *
 *   B -----+            /--+--\            /--+--\            +-----    *
 *   O      |\          |   |   |          |   |   |          /|         *
 *   O      | \        -    |    -        -    |    -        / |         *
 *   S +    |  \      |     |     |      |     |     |      /  |         *
 *   T      |   |    |      |      |    |      |      |    |   |         *
 * ---------+---------------+------------------+---------------+-------- *
 *   C      |   |    |      |      |    |      |      |    |   |         *
 *   U -    |  /      |     |     |      |     |     |      \  |         *
 *   T      | /        -    |    -        -    |    -        \ |         *
 *   O      |/          |   |   |          |   |   |          \|         *
 *   F -----+            \--+--/            \--+--/            +-----    *
 *   F      |               |                  |               |         *
 *          |               |                  |               |         *
 *                                                                       *
 * Gains vary from 0.126 up to 7.943, which means from -18dB attenuation *
 * up to +18dB amplification. Band width varies from 0.01 up to 1.0 in   *
 * octaves for two mid bands.                                            *
 *                                                                       *
 * Implementation is based on the "Cookbook formulae for audio EQ biquad *
 * filter coefficients" by Robert Bristow-Johnson                        *
 * http://www.musicdsp.org/files/Audio-EQ-Cookbook.txt                   */

typedef struct ALequalizerState {
    DERIVE_FROM_TYPE(ALeffectState);

    /* Effect gains for each channel */
    ALfloat Gain[MaxChannels];

    /* Effect parameters */
    ALfilterState filter[4];
} ALequalizerState;

static ALvoid ALequalizerState_Destruct(ALequalizerState *state)
{
    (void)state;
}

static ALboolean ALequalizerState_DeviceUpdate(ALequalizerState *state, ALCdevice *device)
{
    return AL_TRUE;
    (void)state;
    (void)device;
}

static ALvoid ALequalizerState_Update(ALequalizerState *state, ALCdevice *device, const ALeffectslot *slot)
{
    ALfloat frequency = (ALfloat)device->Frequency;
    ALfloat gain = sqrtf(1.0f / device->NumChan) * slot->Gain;
    ALuint it;

    for(it = 0;it < MaxChannels;it++)
        state->Gain[it] = 0.0f;
    for(it = 0; it < device->NumChan; it++)
    {
        enum Channel chan = device->Speaker2Chan[it];
        state->Gain[chan] = gain;
    }

    /* Calculate coefficients for the each type of filter */
    for(it = 0; it < 4; it++)
    {
        ALfilterType type = ALfilterType_Peaking;
        ALfloat filter_frequency;
        ALfloat bandwidth = 0.0f;
        ALfloat gain;

        /* convert linear gains to filter gains */
        switch (it)
        {
            case 0: /* Low Shelf */
                 gain = powf(10.0f, (20.0f * log10f(slot->EffectProps.Equalizer.LowGain)) / 40.0f);
                 filter_frequency = slot->EffectProps.Equalizer.LowCutoff;
                 type = ALfilterType_LowShelf;
                 break;
            case 1: /* Peaking */
                 gain = powf(10.0f, (20.0f * log10f(slot->EffectProps.Equalizer.Mid1Gain)) / 40.0f);
                 filter_frequency = slot->EffectProps.Equalizer.Mid1Center;
                 bandwidth = slot->EffectProps.Equalizer.Mid1Width;
                 type = ALfilterType_Peaking;
                 break;
            case 2: /* Peaking */
                 gain = powf(10.0f, (20.0f * log10f(slot->EffectProps.Equalizer.Mid2Gain)) / 40.0f);
                 filter_frequency = slot->EffectProps.Equalizer.Mid2Center;
                 bandwidth = slot->EffectProps.Equalizer.Mid2Width;
                 type = ALfilterType_Peaking;
                 break;
            case 3: /* High Shelf */
                 gain = powf(10.0f, (20.0f * log10f(slot->EffectProps.Equalizer.HighGain)) / 40.0f);
                 filter_frequency = slot->EffectProps.Equalizer.HighCutoff;
                 type = ALfilterType_HighShelf;
                 break;
        }

        ALfilterState_setParams(&state->filter[it], type, gain, filter_frequency/frequency, bandwidth);
    }
}

static ALvoid ALequalizerState_Process(ALequalizerState *state, ALuint SamplesToDo, const ALfloat *restrict SamplesIn, ALfloat (*restrict SamplesOut)[BUFFERSIZE])
{
    ALuint base;
    ALuint it;
    ALuint kt;
    ALuint ft;

    for(base = 0;base < SamplesToDo;)
    {
        ALfloat temps[64];
        ALuint td = minu(SamplesToDo-base, 64);

        for(it = 0;it < td;it++)
        {
            ALfloat smp = SamplesIn[base+it];

            for(ft = 0;ft < 4;ft++)
                smp = ALfilterState_processSingle(&state->filter[ft], smp);

            temps[it] = smp;
        }

        for(kt = 0;kt < MaxChannels;kt++)
        {
            ALfloat gain = state->Gain[kt];
            if(!(gain > 0.00001f))
                continue;

            for(it = 0;it < td;it++)
                SamplesOut[kt][base+it] += gain * temps[it];
        }

        base += td;
    }
}

static void ALequalizerState_Delete(ALequalizerState *state)
{
    free(state);
}

DEFINE_ALEFFECTSTATE_VTABLE(ALequalizerState);


ALeffectState *ALequalizerStateFactory_create(ALequalizerStateFactory *factory)
{
    ALequalizerState *state;
    int it;
    (void)factory;

    state = malloc(sizeof(*state));
    if(!state) return NULL;
    SET_VTABLE2(ALequalizerState, ALeffectState, state);

    /* Initialize sample history only on filter creation to avoid */
    /* sound clicks if filter settings were changed in runtime.   */
    for(it = 0; it < 4; it++)
        ALfilterState_clear(&state->filter[it]);

    return STATIC_CAST(ALeffectState, state);
}

DEFINE_ALEFFECTSTATEFACTORY_VTABLE(ALequalizerStateFactory);


static void init_equalizer_factory(void)
{
    SET_VTABLE2(ALequalizerStateFactory, ALeffectStateFactory, &EqualizerFactory);
}

ALeffectStateFactory *ALequalizerStateFactory_getFactory(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, init_equalizer_factory);
    return STATIC_CAST(ALeffectStateFactory, &EqualizerFactory);
}


void ALequalizer_SetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val)
{ SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM); (void)effect;(void)param;(void)val; }
void ALequalizer_SetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{
    ALequalizer_SetParami(effect, context, param, vals[0]);
}
void ALequalizer_SetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_EQUALIZER_LOW_GAIN:
            if(!(val >= AL_EQUALIZER_MIN_LOW_GAIN && val <= AL_EQUALIZER_MAX_LOW_GAIN))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Equalizer.LowGain = val;
            break;

        case AL_EQUALIZER_LOW_CUTOFF:
            if(!(val >= AL_EQUALIZER_MIN_LOW_CUTOFF && val <= AL_EQUALIZER_MAX_LOW_CUTOFF))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Equalizer.LowCutoff = val;
            break;

        case AL_EQUALIZER_MID1_GAIN:
            if(!(val >= AL_EQUALIZER_MIN_MID1_GAIN && val <= AL_EQUALIZER_MAX_MID1_GAIN))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Equalizer.Mid1Gain = val;
            break;

        case AL_EQUALIZER_MID1_CENTER:
            if(!(val >= AL_EQUALIZER_MIN_MID1_CENTER && val <= AL_EQUALIZER_MAX_MID1_CENTER))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Equalizer.Mid1Center = val;
            break;

        case AL_EQUALIZER_MID1_WIDTH:
            if(!(val >= AL_EQUALIZER_MIN_MID1_WIDTH && val <= AL_EQUALIZER_MAX_MID1_WIDTH))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Equalizer.Mid1Width = val;
            break;

        case AL_EQUALIZER_MID2_GAIN:
            if(!(val >= AL_EQUALIZER_MIN_MID2_GAIN && val <= AL_EQUALIZER_MAX_MID2_GAIN))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Equalizer.Mid2Gain = val;
            break;

        case AL_EQUALIZER_MID2_CENTER:
            if(!(val >= AL_EQUALIZER_MIN_MID2_CENTER && val <= AL_EQUALIZER_MAX_MID2_CENTER))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Equalizer.Mid2Center = val;
            break;

        case AL_EQUALIZER_MID2_WIDTH:
            if(!(val >= AL_EQUALIZER_MIN_MID2_WIDTH && val <= AL_EQUALIZER_MAX_MID2_WIDTH))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Equalizer.Mid2Width = val;
            break;

        case AL_EQUALIZER_HIGH_GAIN:
            if(!(val >= AL_EQUALIZER_MIN_HIGH_GAIN && val <= AL_EQUALIZER_MAX_HIGH_GAIN))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Equalizer.HighGain = val;
            break;

        case AL_EQUALIZER_HIGH_CUTOFF:
            if(!(val >= AL_EQUALIZER_MIN_HIGH_CUTOFF && val <= AL_EQUALIZER_MAX_HIGH_CUTOFF))
                SET_ERROR_AND_RETURN(context, AL_INVALID_VALUE);
            props->Equalizer.HighCutoff = val;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALequalizer_SetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{
    ALequalizer_SetParamf(effect, context, param, vals[0]);
}

void ALequalizer_GetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint *val)
{ SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM); (void)effect;(void)param;(void)val; }
void ALequalizer_GetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{
    ALequalizer_GetParami(effect, context, param, vals);
}
void ALequalizer_GetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val)
{
    const ALeffectProps *props = &effect->Props;
    switch(param)
    {
        case AL_EQUALIZER_LOW_GAIN:
            *val = props->Equalizer.LowGain;
            break;

        case AL_EQUALIZER_LOW_CUTOFF:
            *val = props->Equalizer.LowCutoff;
            break;

        case AL_EQUALIZER_MID1_GAIN:
            *val = props->Equalizer.Mid1Gain;
            break;

        case AL_EQUALIZER_MID1_CENTER:
            *val = props->Equalizer.Mid1Center;
            break;

        case AL_EQUALIZER_MID1_WIDTH:
            *val = props->Equalizer.Mid1Width;
            break;

        case AL_EQUALIZER_MID2_GAIN:
            *val = props->Equalizer.Mid2Gain;
            break;

        case AL_EQUALIZER_MID2_CENTER:
            *val = props->Equalizer.Mid2Center;
            break;

        case AL_EQUALIZER_MID2_WIDTH:
            *val = props->Equalizer.Mid2Width;
            break;

        case AL_EQUALIZER_HIGH_GAIN:
            *val = props->Equalizer.HighGain;
            break;

        case AL_EQUALIZER_HIGH_CUTOFF:
            *val = props->Equalizer.HighCutoff;
            break;

        default:
            SET_ERROR_AND_RETURN(context, AL_INVALID_ENUM);
    }
}
void ALequalizer_GetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{
    ALequalizer_GetParamf(effect, context, param, vals);
}

DEFINE_ALEFFECT_VTABLE(ALequalizer);
