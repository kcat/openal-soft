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

typedef enum ALEQFilterType {
    LOW_SHELF,
    HIGH_SHELF,
    PEAKING
} ALEQFilterType;

typedef struct ALEQFilter {
    ALEQFilterType type;
    ALfloat x[2]; /* History of two last input samples  */
    ALfloat y[2]; /* History of two last output samples */
    ALfloat a[3]; /* Transfer function coefficients "a" */
    ALfloat b[3]; /* Transfer function coefficients "b" */
} ALEQFilter;

typedef struct ALequalizerState {
    DERIVE_FROM_TYPE(ALeffectState);

    /* Effect gains for each channel */
    ALfloat Gain[MaxChannels];

    /* Effect parameters */
    ALEQFilter bandfilter[4];
} ALequalizerState;

static ALvoid ALequalizerState_Destroy(ALequalizerState *state)
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
        ALfloat gain;
        ALfloat filter_frequency;
        ALfloat bandwidth = 0.0f;
        ALfloat w0;
        ALfloat alpha = 0.0f;

        /* convert linear gains to filter gains */
        switch (it)
        {
            case 0: /* Low Shelf */
                 gain = powf(10.0f, (20.0f * log10f(slot->effect.Equalizer.LowGain)) / 40.0f);
                 filter_frequency = slot->effect.Equalizer.LowCutoff;
                 break;
            case 1: /* Peaking */
                 gain = powf(10.0f, (20.0f * log10f(slot->effect.Equalizer.Mid1Gain)) / 40.0f);
                 filter_frequency = slot->effect.Equalizer.Mid1Center;
                 bandwidth = slot->effect.Equalizer.Mid1Width;
                 break;
            case 2: /* Peaking */
                 gain = powf(10.0f, (20.0f * log10f(slot->effect.Equalizer.Mid2Gain)) / 40.0f);
                 filter_frequency = slot->effect.Equalizer.Mid2Center;
                 bandwidth = slot->effect.Equalizer.Mid2Width;
                 break;
            case 3: /* High Shelf */
                 gain = powf(10.0f, (20.0f * log10f(slot->effect.Equalizer.HighGain)) / 40.0f);
                 filter_frequency = slot->effect.Equalizer.HighCutoff;
                 break;
        }

        w0 = 2.0f*F_PI * filter_frequency / frequency;

        /* Calculate filter coefficients depending on filter type */
        switch(state->bandfilter[it].type)
        {
            case LOW_SHELF:
                 alpha = sinf(w0) / 2.0f * sqrtf((gain + 1.0f / gain) *
                                                 (1.0f / 0.75f - 1.0f) + 2.0f);
                 state->bandfilter[it].b[0] = gain * ((gain + 1.0f) -
                                                      (gain - 1.0f) * cosf(w0) +
                                                      2.0f * sqrtf(gain) * alpha);
                 state->bandfilter[it].b[1] = 2.0f * gain * ((gain - 1.0f) -
                                                             (gain + 1.0f) * cosf(w0));
                 state->bandfilter[it].b[2] = gain * ((gain + 1.0f) -
                                                      (gain - 1.0f) * cosf(w0) -
                                                      2.0f * sqrtf(gain) * alpha);
                 state->bandfilter[it].a[0] = (gain + 1.0f) +
                                              (gain - 1.0f) * cosf(w0) +
                                              2.0f * sqrtf(gain) * alpha;
                 state->bandfilter[it].a[1] = -2.0f * ((gain - 1.0f) +
                                              (gain + 1.0f) * cosf(w0));
                 state->bandfilter[it].a[2] = (gain + 1.0f) +
                                              (gain - 1.0f) * cosf(w0) -
                                              2.0f * sqrtf(gain) * alpha;
                 break;
            case HIGH_SHELF:
                 alpha = sinf(w0) / 2.0f * sqrtf((gain + 1.0f / gain) *
                                                 (1.0f / 0.75f - 1.0f) + 2.0f);
                 state->bandfilter[it].b[0] = gain * ((gain + 1.0f) +
                                                      (gain - 1.0f) * cosf(w0) +
                                                      2.0f * sqrtf(gain) * alpha);
                 state->bandfilter[it].b[1] = -2.0f * gain * ((gain - 1.0f) +
                                                              (gain + 1.0f) *
                                                              cosf(w0));
                 state->bandfilter[it].b[2] = gain * ((gain + 1.0f) +
                                                      (gain - 1.0f) * cosf(w0) -
                                                      2.0f * sqrtf(gain) * alpha);
                 state->bandfilter[it].a[0] = (gain + 1.0f) -
                                              (gain - 1.0f) * cosf(w0) +
                                              2.0f * sqrtf(gain) * alpha;
                 state->bandfilter[it].a[1] = 2.0f * ((gain - 1.0f) -
                                                      (gain + 1.0f) * cosf(w0));
                 state->bandfilter[it].a[2] = (gain + 1.0f) -
                                              (gain - 1.0f) * cosf(w0) -
                                              2.0f * sqrtf(gain) * alpha;
                 break;
            case PEAKING:
                 alpha = sinf(w0) * sinhf(logf(2.0f) / 2.0f * bandwidth * w0 / sinf(w0));
                 state->bandfilter[it].b[0] =  1.0f + alpha * gain;
                 state->bandfilter[it].b[1] = -2.0f * cosf(w0);
                 state->bandfilter[it].b[2] =  1.0f - alpha * gain;
                 state->bandfilter[it].a[0] =  1.0f + alpha / gain;
                 state->bandfilter[it].a[1] = -2.0f * cosf(w0);
                 state->bandfilter[it].a[2] =  1.0f - alpha / gain;
                 break;
        }
    }
}

static ALvoid ALequalizerState_Process(ALequalizerState *state, ALuint SamplesToDo, const ALfloat *RESTRICT SamplesIn, ALfloat (*RESTRICT SamplesOut)[BUFFERSIZE])
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
            ALfloat tempsmp;

            for(ft = 0;ft < 4;ft++)
            {
                ALEQFilter *filter = &state->bandfilter[ft];

                tempsmp = filter->b[0] / filter->a[0] * smp +
                          filter->b[1] / filter->a[0] * filter->x[0] +
                          filter->b[2] / filter->a[0] * filter->x[1] -
                          filter->a[1] / filter->a[0] * filter->y[0] -
                          filter->a[2] / filter->a[0] * filter->y[1];

                filter->x[1] = filter->x[0];
                filter->x[0] = smp;
                filter->y[1] = filter->y[0];
                filter->y[0] = tempsmp;
                smp = tempsmp;
            }

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

static ALeffectStateFactory *ALequalizerState_getCreator(void)
{
    return STATIC_CAST(ALeffectStateFactory, &EqualizerFactory);
}

DEFINE_ALEFFECTSTATE_VTABLE(ALequalizerState);


ALeffectState *ALequalizerStateFactory_create(void)
{
    ALequalizerState *state;
    int it;

    state = malloc(sizeof(*state));
    if(!state) return NULL;
    SET_VTABLE2(ALequalizerState, ALeffectState, state);

    state->bandfilter[0].type = LOW_SHELF;
    state->bandfilter[1].type = PEAKING;
    state->bandfilter[2].type = PEAKING;
    state->bandfilter[3].type = HIGH_SHELF;

    /* Initialize sample history only on filter creation to avoid */
    /* sound clicks if filter settings were changed in runtime.   */
    for(it = 0; it < 4; it++)
    {
        state->bandfilter[it].x[0] = 0.0f;
        state->bandfilter[it].x[1] = 0.0f;
        state->bandfilter[it].y[0] = 0.0f;
        state->bandfilter[it].y[1] = 0.0f;
    }

    return STATIC_CAST(ALeffectState, state);
}

static ALvoid ALequalizerStateFactory_destroy(ALeffectState *effect)
{
    ALequalizerState *state = STATIC_UPCAST(ALequalizerState, ALeffectState, effect);
    ALequalizerState_Destroy(state);
    free(state);
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


void equalizer_SetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val)
{
    effect=effect;
    val=val;

    switch(param)
    {
        default:
            alSetError(context, AL_INVALID_ENUM);
            break;
    }
}
void equalizer_SetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals)
{
    equalizer_SetParami(effect, context, param, vals[0]);
}
void equalizer_SetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val)
{
    switch(param)
    {
        case AL_EQUALIZER_LOW_GAIN:
            if(val >= AL_EQUALIZER_MIN_LOW_GAIN && val <= AL_EQUALIZER_MAX_LOW_GAIN)
                effect->Equalizer.LowGain = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_EQUALIZER_LOW_CUTOFF:
            if(val >= AL_EQUALIZER_MIN_LOW_CUTOFF && val <= AL_EQUALIZER_MAX_LOW_CUTOFF)
                effect->Equalizer.LowCutoff = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_EQUALIZER_MID1_GAIN:
            if(val >= AL_EQUALIZER_MIN_MID1_GAIN && val <= AL_EQUALIZER_MAX_MID1_GAIN)
                effect->Equalizer.Mid1Gain = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_EQUALIZER_MID1_CENTER:
            if(val >= AL_EQUALIZER_MIN_MID1_CENTER && val <= AL_EQUALIZER_MAX_MID1_CENTER)
                effect->Equalizer.Mid1Center = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_EQUALIZER_MID1_WIDTH:
            if(val >= AL_EQUALIZER_MIN_MID1_WIDTH && val <= AL_EQUALIZER_MAX_MID1_WIDTH)
                effect->Equalizer.Mid1Width = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_EQUALIZER_MID2_GAIN:
            if(val >= AL_EQUALIZER_MIN_MID2_GAIN && val <= AL_EQUALIZER_MAX_MID2_GAIN)
                effect->Equalizer.Mid2Gain = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_EQUALIZER_MID2_CENTER:
            if(val >= AL_EQUALIZER_MIN_MID2_CENTER && val <= AL_EQUALIZER_MAX_MID2_CENTER)
                effect->Equalizer.Mid2Center = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_EQUALIZER_MID2_WIDTH:
            if(val >= AL_EQUALIZER_MIN_MID2_WIDTH && val <= AL_EQUALIZER_MAX_MID2_WIDTH)
                effect->Equalizer.Mid2Width = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_EQUALIZER_HIGH_GAIN:
            if(val >= AL_EQUALIZER_MIN_HIGH_GAIN && val <= AL_EQUALIZER_MAX_HIGH_GAIN)
                effect->Equalizer.HighGain = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        case AL_EQUALIZER_HIGH_CUTOFF:
            if(val >= AL_EQUALIZER_MIN_HIGH_CUTOFF && val <= AL_EQUALIZER_MAX_HIGH_CUTOFF)
                effect->Equalizer.HighCutoff = val;
            else
                alSetError(context, AL_INVALID_VALUE);
            break;

        default:
            alSetError(context, AL_INVALID_ENUM);
            break;
    }
}
void equalizer_SetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals)
{
    equalizer_SetParamf(effect, context, param, vals[0]);
}

void equalizer_GetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint *val)
{
    effect=effect;
    val=val;

    switch(param)
    {
        default:
            alSetError(context, AL_INVALID_ENUM);
            break;
    }
}
void equalizer_GetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals)
{
    equalizer_GetParami(effect, context, param, vals);
}
void equalizer_GetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val)
{
    switch(param)
    {
        case AL_EQUALIZER_LOW_GAIN:
            *val = effect->Equalizer.LowGain;
            break;

        case AL_EQUALIZER_LOW_CUTOFF:
            *val = effect->Equalizer.LowCutoff;
            break;

        case AL_EQUALIZER_MID1_GAIN:
            *val = effect->Equalizer.Mid1Gain;
            break;

        case AL_EQUALIZER_MID1_CENTER:
            *val = effect->Equalizer.Mid1Center;
            break;

        case AL_EQUALIZER_MID1_WIDTH:
            *val = effect->Equalizer.Mid1Width;
            break;

        case AL_EQUALIZER_MID2_GAIN:
            *val = effect->Equalizer.Mid2Gain;
            break;

        case AL_EQUALIZER_MID2_CENTER:
            *val = effect->Equalizer.Mid2Center;
            break;

        case AL_EQUALIZER_MID2_WIDTH:
            *val = effect->Equalizer.Mid2Width;
            break;

        case AL_EQUALIZER_HIGH_GAIN:
            *val = effect->Equalizer.HighGain;
            break;

        case AL_EQUALIZER_HIGH_CUTOFF:
            *val = effect->Equalizer.HighCutoff;
            break;

        default:
            alSetError(context, AL_INVALID_ENUM);
            break;
    }
}
void equalizer_GetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals)
{
    equalizer_GetParamf(effect, context, param, vals);
}
