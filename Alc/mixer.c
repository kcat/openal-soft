/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
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
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"
#include "alSource.h"
#include "alBuffer.h"
#include "alListener.h"
#include "alAuxEffectSlot.h"
#include "alu.h"
#include "bs2b.h"


static __inline ALfloat aluF2F(ALfloat Value)
{
    return Value;
}

static __inline ALshort aluF2S(ALfloat Value)
{
    ALint i;

    if(Value < 0.0f)
    {
        i = (ALint)(Value*32768.0f);
        i = max(-32768, i);
    }
    else
    {
        i = (ALint)(Value*32767.0f);
        i = min( 32767, i);
    }
    return ((ALshort)i);
}

static __inline ALubyte aluF2UB(ALfloat Value)
{
    ALshort i = aluF2S(Value);
    return (i>>8)+128;
}


static __inline ALfloat point(ALfloat val1, ALfloat val2, ALint frac)
{
    return val1;
    (void)val2;
    (void)frac;
}
static __inline ALfloat lerp(ALfloat val1, ALfloat val2, ALint frac)
{
    return val1 + ((val2-val1)*(frac * (1.0f/(1<<FRACTIONBITS))));
}
static __inline ALfloat cos_lerp(ALfloat val1, ALfloat val2, ALint frac)
{
    ALfloat mult = (1.0f-cos(frac * (1.0f/(1<<FRACTIONBITS)) * M_PI)) * 0.5f;
    return val1 + ((val2-val1)*mult);
}


static void MixSource(ALsource *ALSource, ALCcontext *ALContext,
                      float (*DryBuffer)[OUTPUTCHANNELS], ALuint SamplesToDo)
{
    static float DummyBuffer[BUFFERSIZE];
    static ALfloat DummyClickRemoval[OUTPUTCHANNELS];
    ALfloat *WetBuffer[MAX_SENDS];
    ALfloat DrySend[OUTPUTCHANNELS];
    ALfloat dryGainDiff[OUTPUTCHANNELS];
    ALfloat wetGainDiff[MAX_SENDS];
    ALboolean UpdateClick;
    ALfloat *WetClickRemoval[MAX_SENDS];
    ALfloat *ClickRemoval;
    ALuint i, j, out;
    ALfloat value, outsamp;
    ALbufferlistitem *BufferListItem;
    ALint64 DataSize64,DataPos64;
    FILTER *DryFilter, *WetFilter[MAX_SENDS];
    ALfloat WetSend[MAX_SENDS];
    ALint increment;
    ALuint DataPosInt, DataPosFrac;
    resampler_t Resampler;
    ALuint BuffersPlayed;
    ALboolean Looping;
    ALenum State;

    for(i = 0;i < OUTPUTCHANNELS;i++)
        DrySend[i] = ALSource->Params.DryGains[i];
    for(i = 0;i < MAX_SENDS;i++)
        WetSend[i] = ALSource->Params.WetGains[i];

    UpdateClick = (ALSource->FirstStart || ALSource->NeedsUpdate);
    ClickRemoval = ALContext->Device->ClickRemoval;

    if(ALSource->NeedsUpdate)
    {
        ALsource_Update(ALSource, ALContext);
        ALSource->NeedsUpdate = AL_FALSE;
        UpdateClick = AL_TRUE;
    }

    /* Get source info */
    Resampler     = ALSource->Resampler;
    State         = ALSource->state;
    BuffersPlayed = ALSource->BuffersPlayed;
    DataPosInt    = ALSource->position;
    DataPosFrac   = ALSource->position_fraction;
    Looping       = ALSource->bLooping;

    for(i = 0;i < OUTPUTCHANNELS;i++)
    {
        dryGainDiff[i] = DrySend[i] - ALSource->Params.DryGains[i];
        DrySend[i] = ALSource->Params.DryGains[i];
    }
    for(i = 0;i < MAX_SENDS;i++)
    {
        wetGainDiff[i] = WetSend[i] - ALSource->Params.WetGains[i];
        WetSend[i] = ALSource->Params.WetGains[i];
    }

    /* Get fixed point step */
    increment = ALSource->Params.Step;

    DryFilter = &ALSource->Params.iirFilter;
    for(i = 0;i < MAX_SENDS;i++)
    {
        WetFilter[i] = &ALSource->Params.Send[i].iirFilter;
        WetBuffer[i] = (ALSource->Send[i].Slot ?
                        ALSource->Send[i].Slot->WetBuffer :
                        DummyBuffer);
        WetClickRemoval[i] = (ALSource->Send[i].Slot ?
                              ALSource->Send[i].Slot->ClickRemoval :
                              DummyClickRemoval);
    }

    /* Get current buffer queue item */
    BufferListItem = ALSource->queue;
    for(i = 0;i < BuffersPlayed;i++)
        BufferListItem = BufferListItem->next;

    j = 0;
    do {
        const ALbuffer *ALBuffer;
        ALfloat *Data = NULL;
        ALuint DataSize = 0;
        ALuint LoopStart = 0;
        ALuint LoopEnd = 0;
        ALuint Channels, Bytes;
        ALuint BufferSize;

        /* Get buffer info */
        if((ALBuffer=BufferListItem->buffer) != NULL)
        {
            Data      = ALBuffer->data;
            DataSize  = ALBuffer->size;
            DataSize /= aluFrameSizeFromFormat(ALBuffer->format);
            LoopStart = ALBuffer->LoopStart;
            LoopEnd   = ALBuffer->LoopEnd;
            Channels  = aluChannelsFromFormat(ALBuffer->format);
            Bytes     = aluBytesFromFormat(ALBuffer->format);
        }

        if(Looping && ALSource->lSourceType == AL_STATIC)
        {
            /* If current offset is beyond the loop range, do not loop */
            if(DataPosInt >= LoopEnd)
                Looping = AL_FALSE;
        }
        if(!Looping || ALSource->lSourceType != AL_STATIC)
        {
            /* Non-looping and non-static sources ignore loop points */
            LoopStart = 0;
            LoopEnd = DataSize;
        }

        if(DataPosInt >= DataSize)
            goto skipmix;

        if(BufferListItem->next)
        {
            ALbuffer *NextBuf = BufferListItem->next->buffer;
            if(NextBuf && NextBuf->size)
            {
                ALint ulExtraSamples = BUFFER_PADDING*Channels*Bytes;
                ulExtraSamples = min(NextBuf->size, ulExtraSamples);
                memcpy(&Data[DataSize*Channels], NextBuf->data, ulExtraSamples);
            }
        }
        else if(Looping)
        {
            ALbuffer *NextBuf = ALSource->queue->buffer;
            if(NextBuf && NextBuf->size)
            {
                ALint ulExtraSamples = BUFFER_PADDING*Channels*Bytes;
                ulExtraSamples = min(NextBuf->size, ulExtraSamples);
                memcpy(&Data[DataSize*Channels], &NextBuf->data[LoopStart*Channels], ulExtraSamples);
            }
        }
        else
            memset(&Data[DataSize*Channels], 0, (BUFFER_PADDING*Channels*Bytes));

        /* Figure out how many samples we can mix. */
        DataSize64 = LoopEnd;
        DataSize64 <<= FRACTIONBITS;
        DataPos64 = DataPosInt;
        DataPos64 <<= FRACTIONBITS;
        DataPos64 += DataPosFrac;
        BufferSize = (ALuint)((DataSize64-DataPos64+(increment-1)) / increment);

        BufferSize = min(BufferSize, (SamplesToDo-j));

        /* Actual sample mixing loops */
        if(Channels == 1) /* Mono */
        {
#define DO_MIX(resampler) do {                                                \
    if(j == 0 && UpdateClick)                                                 \
    {                                                                         \
        const ALfloat Starter = (ALSource->FirstStart ? 1.0f : 0.0f);         \
                                                                              \
        value = (resampler)(Data[DataPosInt], Data[DataPosInt+1],             \
                            DataPosFrac);                                     \
        outsamp = lpFilter4PC(DryFilter, 0, value);                           \
        ClickRemoval[FRONT_LEFT]   += outsamp*dryGainDiff[FRONT_LEFT];        \
        ClickRemoval[FRONT_RIGHT]  += outsamp*dryGainDiff[FRONT_RIGHT];       \
        ClickRemoval[SIDE_LEFT]    += outsamp*dryGainDiff[SIDE_LEFT];         \
        ClickRemoval[SIDE_RIGHT]   += outsamp*dryGainDiff[SIDE_RIGHT];        \
        ClickRemoval[BACK_LEFT]    += outsamp*dryGainDiff[BACK_LEFT];         \
        ClickRemoval[BACK_RIGHT]   += outsamp*dryGainDiff[BACK_RIGHT];        \
        ClickRemoval[FRONT_CENTER] += outsamp*dryGainDiff[FRONT_CENTER];      \
        ClickRemoval[BACK_CENTER]  += outsamp*dryGainDiff[BACK_CENTER];       \
                                                                              \
        ClickRemoval[FRONT_LEFT]   -= outsamp*Starter*DrySend[FRONT_LEFT];    \
        ClickRemoval[FRONT_RIGHT]  -= outsamp*Starter*DrySend[FRONT_RIGHT];   \
        ClickRemoval[SIDE_LEFT]    -= outsamp*Starter*DrySend[SIDE_LEFT];     \
        ClickRemoval[SIDE_RIGHT]   -= outsamp*Starter*DrySend[SIDE_RIGHT];    \
        ClickRemoval[BACK_LEFT]    -= outsamp*Starter*DrySend[BACK_LEFT];     \
        ClickRemoval[BACK_RIGHT]   -= outsamp*Starter*DrySend[BACK_RIGHT];    \
        ClickRemoval[FRONT_CENTER] -= outsamp*Starter*DrySend[FRONT_CENTER];  \
        ClickRemoval[BACK_CENTER]  -= outsamp*Starter*DrySend[BACK_CENTER];   \
                                                                              \
        for(out = 0;out < MAX_SENDS;out++)                                    \
        {                                                                     \
            outsamp = lpFilter2PC(WetFilter[out], 0, value);                  \
            WetClickRemoval[out][0] += outsamp*wetGainDiff[BACK_CENTER];      \
            WetClickRemoval[out][0] -= outsamp*Starter*WetSend[out];          \
        }                                                                     \
    }                                                                         \
    while(BufferSize--)                                                       \
    {                                                                         \
        /* First order interpolator */                                        \
        value = (resampler)(Data[DataPosInt], Data[DataPosInt+1],             \
                            DataPosFrac);                                     \
                                                                              \
        /* Direct path final mix buffer and panning */                        \
        outsamp = lpFilter4P(DryFilter, 0, value);                            \
        DryBuffer[j][FRONT_LEFT]   += outsamp*DrySend[FRONT_LEFT];            \
        DryBuffer[j][FRONT_RIGHT]  += outsamp*DrySend[FRONT_RIGHT];           \
        DryBuffer[j][SIDE_LEFT]    += outsamp*DrySend[SIDE_LEFT];             \
        DryBuffer[j][SIDE_RIGHT]   += outsamp*DrySend[SIDE_RIGHT];            \
        DryBuffer[j][BACK_LEFT]    += outsamp*DrySend[BACK_LEFT];             \
        DryBuffer[j][BACK_RIGHT]   += outsamp*DrySend[BACK_RIGHT];            \
        DryBuffer[j][FRONT_CENTER] += outsamp*DrySend[FRONT_CENTER];          \
        DryBuffer[j][BACK_CENTER]  += outsamp*DrySend[BACK_CENTER];           \
                                                                              \
        /* Room path final mix buffer and panning */                          \
        for(i = 0;i < MAX_SENDS;i++)                                          \
        {                                                                     \
            outsamp = lpFilter2P(WetFilter[i], 0, value);                     \
            WetBuffer[i][j] += outsamp*WetSend[i];                            \
        }                                                                     \
                                                                              \
        DataPosFrac += increment;                                             \
        DataPosInt += DataPosFrac>>FRACTIONBITS;                              \
        DataPosFrac &= FRACTIONMASK;                                          \
        j++;                                                                  \
    }                                                                         \
} while(0)

            switch(Resampler)
            {
                case POINT_RESAMPLER:
                DO_MIX(point); break;
                case LINEAR_RESAMPLER:
                DO_MIX(lerp); break;
                case COSINE_RESAMPLER:
                DO_MIX(cos_lerp); break;
                case RESAMPLER_MIN:
                case RESAMPLER_MAX:
                break;
            }
#undef DO_MIX
        }
        else if(Channels == 2) /* Stereo */
        {
            const int chans[] = {
                FRONT_LEFT, FRONT_RIGHT
            };
            const int chans2[] = {
                BACK_LEFT, SIDE_LEFT, BACK_RIGHT, SIDE_RIGHT
            };

#define DO_MIX(resampler) do {                                                \
    const ALfloat scaler = 1.0f/Channels;                                     \
    if(j == 0 && UpdateClick)                                                 \
    {                                                                         \
        const ALfloat Starter = (ALSource->FirstStart ? 1.0f : 0.0f);         \
        for(i = 0;i < Channels;i++)                                           \
        {                                                                     \
            value = (resampler)(Data[DataPosInt*Channels + i],                \
                                Data[(DataPosInt+1)*Channels + i],            \
                                DataPosFrac);                                 \
                                                                              \
            outsamp = lpFilter2PC(DryFilter, chans[i]*2, value);              \
            ClickRemoval[chans[i]] += outsamp*dryGainDiff[chans[i]];          \
            ClickRemoval[chans2[i*2+0]] += outsamp*dryGainDiff[chans2[i*2+0]];\
            ClickRemoval[chans2[i*2+1]] += outsamp*dryGainDiff[chans2[i*2+1]];\
                                                                              \
            ClickRemoval[chans[i]] -= outsamp*Starter*DrySend[chans[i]];      \
            ClickRemoval[chans2[i*2+0]] -= outsamp*Starter*DrySend[chans2[i*2+0]];\
            ClickRemoval[chans2[i*2+1]] -= outsamp*Starter*DrySend[chans2[i*2+1]];\
                                                                              \
            for(out = 0;out < MAX_SENDS;out++)                                \
            {                                                                 \
                outsamp = lpFilter1PC(WetFilter[out], chans[out], value) * scaler;\
                WetClickRemoval[out][0] += outsamp*wetGainDiff[out];          \
                WetClickRemoval[out][0] -= outsamp*Starter*WetSend[out];      \
            }                                                                 \
        }                                                                     \
    }                                                                         \
    while(BufferSize--)                                                       \
    {                                                                         \
        for(i = 0;i < Channels;i++)                                           \
        {                                                                     \
            value = (resampler)(Data[DataPosInt*Channels + i],                \
                                Data[(DataPosInt+1)*Channels + i],            \
                                DataPosFrac);                                 \
                                                                              \
            outsamp = lpFilter2P(DryFilter, chans[i]*2, value);               \
            DryBuffer[j][chans[i]] += outsamp*DrySend[chans[i]];              \
            DryBuffer[j][chans2[i*2+0]] += outsamp*DrySend[chans2[i*2+0]];    \
            DryBuffer[j][chans2[i*2+1]] += outsamp*DrySend[chans2[i*2+1]];    \
                                                                              \
            for(out = 0;out < MAX_SENDS;out++)                                \
            {                                                                 \
                outsamp = lpFilter1P(WetFilter[out], chans[i], value);        \
                WetBuffer[out][j] += outsamp*WetSend[out]*scaler;             \
            }                                                                 \
        }                                                                     \
                                                                              \
        DataPosFrac += increment;                                             \
        DataPosInt += DataPosFrac>>FRACTIONBITS;                              \
        DataPosFrac &= FRACTIONMASK;                                          \
        j++;                                                                  \
    }                                                                         \
} while(0)

            switch(Resampler)
            {
                case POINT_RESAMPLER:
                DO_MIX(point); break;
                case LINEAR_RESAMPLER:
                DO_MIX(lerp); break;
                case COSINE_RESAMPLER:
                DO_MIX(cos_lerp); break;
                case RESAMPLER_MIN:
                case RESAMPLER_MAX:
                break;
            }
#undef DO_MIX
        }
        else if(Channels == 4) /* Quad */
        {
            const int chans[] = {
                FRONT_LEFT, FRONT_RIGHT,
                BACK_LEFT,  BACK_RIGHT
            };

#define DO_MIX(resampler) do {                                                \
    const ALfloat scaler = 1.0f/Channels;                                     \
    if(j == 0 && UpdateClick)                                                 \
    {                                                                         \
        const ALfloat Starter = (ALSource->FirstStart ? 1.0f : 0.0f);         \
        for(i = 0;i < Channels;i++)                                           \
        {                                                                     \
            value = (resampler)(Data[DataPosInt*Channels + i],                \
                                Data[(DataPosInt+1)*Channels + i],            \
                                DataPosFrac);                                 \
                                                                              \
            outsamp = lpFilter2PC(DryFilter, chans[i]*2, value);              \
            ClickRemoval[chans[i]] += outsamp*dryGainDiff[chans[i]];          \
            ClickRemoval[chans[i]] -= outsamp*Starter*DrySend[chans[i]];      \
                                                                              \
            for(out = 0;out < MAX_SENDS;out++)                                \
            {                                                                 \
                outsamp = lpFilter1PC(WetFilter[out], chans[out], value) * scaler;\
                WetClickRemoval[out][0] += outsamp*wetGainDiff[out];          \
                WetClickRemoval[out][0] -= outsamp*Starter*WetSend[out];      \
            }                                                                 \
        }                                                                     \
    }                                                                         \
    while(BufferSize--)                                                       \
    {                                                                         \
        for(i = 0;i < Channels;i++)                                           \
        {                                                                     \
            value = (resampler)(Data[DataPosInt*Channels + i],                \
                                Data[(DataPosInt+1)*Channels + i],            \
                                DataPosFrac);                                 \
                                                                              \
            outsamp = lpFilter2P(DryFilter, chans[i]*2, value);               \
            DryBuffer[j][chans[i]] += outsamp*DrySend[chans[i]];              \
                                                                              \
            for(out = 0;out < MAX_SENDS;out++)                                \
            {                                                                 \
                outsamp = lpFilter1P(WetFilter[out], chans[i], value);        \
                WetBuffer[out][j] += outsamp*WetSend[out]*scaler;             \
            }                                                                 \
        }                                                                     \
                                                                              \
        DataPosFrac += increment;                                             \
        DataPosInt += DataPosFrac>>FRACTIONBITS;                              \
        DataPosFrac &= FRACTIONMASK;                                          \
        j++;                                                                  \
    }                                                                         \
} while(0)

            switch(Resampler)
            {
                case POINT_RESAMPLER:
                DO_MIX(point); break;
                case LINEAR_RESAMPLER:
                DO_MIX(lerp); break;
                case COSINE_RESAMPLER:
                DO_MIX(cos_lerp); break;
                case RESAMPLER_MIN:
                case RESAMPLER_MAX:
                break;
            }
        }
        else if(Channels == 6) /* 5.1 */
        {
            const int chans[] = {
                FRONT_LEFT,   FRONT_RIGHT,
                FRONT_CENTER, LFE,
                BACK_LEFT,    BACK_RIGHT
            };

            switch(Resampler)
            {
                case POINT_RESAMPLER:
                DO_MIX(point); break;
                case LINEAR_RESAMPLER:
                DO_MIX(lerp); break;
                case COSINE_RESAMPLER:
                DO_MIX(cos_lerp); break;
                case RESAMPLER_MIN:
                case RESAMPLER_MAX:
                break;
            }
        }
        else if(Channels == 7) /* 6.1 */
        {
            const int chans[] = {
                FRONT_LEFT,   FRONT_RIGHT,
                FRONT_CENTER, LFE,
                BACK_CENTER,
                SIDE_LEFT,    SIDE_RIGHT
            };

            switch(Resampler)
            {
                case POINT_RESAMPLER:
                DO_MIX(point); break;
                case LINEAR_RESAMPLER:
                DO_MIX(lerp); break;
                case COSINE_RESAMPLER:
                DO_MIX(cos_lerp); break;
                case RESAMPLER_MIN:
                case RESAMPLER_MAX:
                break;
            }
        }
        else if(Channels == 8) /* 7.1 */
        {
            const int chans[] = {
                FRONT_LEFT,   FRONT_RIGHT,
                FRONT_CENTER, LFE,
                BACK_LEFT,    BACK_RIGHT,
                SIDE_LEFT,    SIDE_RIGHT
            };

            switch(Resampler)
            {
                case POINT_RESAMPLER:
                DO_MIX(point); break;
                case LINEAR_RESAMPLER:
                DO_MIX(lerp); break;
                case COSINE_RESAMPLER:
                DO_MIX(cos_lerp); break;
                case RESAMPLER_MIN:
                case RESAMPLER_MAX:
                break;
            }
#undef DO_MIX
        }
        else /* Unknown? */
        {
            while(BufferSize--)
            {
                DataPosFrac += increment;
                DataPosInt += DataPosFrac>>FRACTIONBITS;
                DataPosFrac &= FRACTIONMASK;
                j++;
            }
        }

    skipmix:
        /* Handle looping sources */
        if(DataPosInt >= LoopEnd)
        {
            if(BuffersPlayed < (ALSource->BuffersInQueue-1))
            {
                BufferListItem = BufferListItem->next;
                BuffersPlayed++;
                DataPosInt -= DataSize;
            }
            else if(Looping)
            {
                BufferListItem = ALSource->queue;
                BuffersPlayed = 0;
                if(ALSource->lSourceType == AL_STATIC)
                    DataPosInt = ((DataPosInt-LoopStart)%(LoopEnd-LoopStart)) + LoopStart;
                else
                    DataPosInt -= DataSize;
            }
            else
            {
                State = AL_STOPPED;
                BufferListItem = ALSource->queue;
                BuffersPlayed = ALSource->BuffersInQueue;
                DataPosInt = 0;
                DataPosFrac = 0;
            }
        }
    } while(State == AL_PLAYING && j < SamplesToDo);

    /* Update source info */
    ALSource->state             = State;
    ALSource->BuffersPlayed     = BuffersPlayed;
    ALSource->position          = DataPosInt;
    ALSource->position_fraction = DataPosFrac;
    ALSource->Buffer            = BufferListItem->buffer;

    ALSource->FirstStart = AL_FALSE;
}

ALvoid aluMixData(ALCdevice *device, ALvoid *buffer, ALsizei size)
{
    float (*DryBuffer)[OUTPUTCHANNELS];
    ALfloat (*Matrix)[OUTPUTCHANNELS];
    ALfloat *ClickRemoval;
    const ALuint *ChanMap;
    ALuint SamplesToDo;
    ALeffectslot *ALEffectSlot;
    ALCcontext *ALContext;
    ALfloat samp;
    int fpuState;
    ALuint i, j, c;
    ALsizei e, s;

#if defined(HAVE_FESETROUND)
    fpuState = fegetround();
    fesetround(FE_TOWARDZERO);
#elif defined(HAVE__CONTROLFP)
    fpuState = _controlfp(0, 0);
    _controlfp(_RC_CHOP, _MCW_RC);
#else
    (void)fpuState;
#endif

    DryBuffer = device->DryBuffer;
    while(size > 0)
    {
        /* Setup variables */
        SamplesToDo = min(size, BUFFERSIZE);

        /* Clear mixing buffer */
        memset(DryBuffer, 0, SamplesToDo*OUTPUTCHANNELS*sizeof(ALfloat));

        SuspendContext(NULL);
        for(c = 0;c < device->NumContexts;c++)
        {
            ALContext = device->Contexts[c];
            SuspendContext(ALContext);

            s = 0;
            while(s < ALContext->ActiveSourceCount)
            {
                ALsource *Source = ALContext->ActiveSources[s];
                if(Source->state != AL_PLAYING)
                {
                    ALsizei end = --(ALContext->ActiveSourceCount);
                    ALContext->ActiveSources[s] = ALContext->ActiveSources[end];
                    continue;
                }
                MixSource(Source, ALContext, DryBuffer, SamplesToDo);
                s++;
            }

            /* effect slot processing */
            for(e = 0;e < ALContext->EffectSlotMap.size;e++)
            {
                ALEffectSlot = ALContext->EffectSlotMap.array[e].value;

                ClickRemoval = ALEffectSlot->ClickRemoval;
                for(i = 0;i < SamplesToDo;i++)
                {
                    ClickRemoval[0] -= ClickRemoval[0] / 256.0f;
                    ALEffectSlot->WetBuffer[i] += ClickRemoval[0];
                }

                ALEffect_Process(ALEffectSlot->EffectState, ALEffectSlot, SamplesToDo, ALEffectSlot->WetBuffer, DryBuffer);

                for(i = 0;i < SamplesToDo;i++)
                    ALEffectSlot->WetBuffer[i] = 0.0f;
            }
            ProcessContext(ALContext);
        }
        device->SamplesPlayed += SamplesToDo;
        ProcessContext(NULL);

        //Post processing loop
        ClickRemoval = device->ClickRemoval;
        for(i = 0;i < SamplesToDo;i++)
        {
            for(c = 0;c < OUTPUTCHANNELS;c++)
            {
                ClickRemoval[c] -= ClickRemoval[c] / 256.0f;
                DryBuffer[i][c] += ClickRemoval[c];
            }
        }

        ChanMap = device->DevChannels;
        Matrix = device->ChannelMatrix;
        switch(device->Format)
        {
#define CHECK_WRITE_FORMAT(bits, type, func)                                  \
        case AL_FORMAT_MONO##bits:                                            \
            for(i = 0;i < SamplesToDo;i++)                                    \
            {                                                                 \
                samp = 0.0f;                                                  \
                for(c = 0;c < OUTPUTCHANNELS;c++)                             \
                    samp += DryBuffer[i][c] * Matrix[c][FRONT_CENTER];        \
                ((type*)buffer)[ChanMap[FRONT_CENTER]] = (func)(samp);        \
                buffer = ((type*)buffer) + 1;                                 \
            }                                                                 \
            break;                                                            \
        case AL_FORMAT_STEREO##bits:                                          \
            if(device->Bs2b)                                                  \
            {                                                                 \
                for(i = 0;i < SamplesToDo;i++)                                \
                {                                                             \
                    float samples[2] = { 0.0f, 0.0f };                        \
                    for(c = 0;c < OUTPUTCHANNELS;c++)                         \
                    {                                                         \
                        samples[0] += DryBuffer[i][c]*Matrix[c][FRONT_LEFT];  \
                        samples[1] += DryBuffer[i][c]*Matrix[c][FRONT_RIGHT]; \
                    }                                                         \
                    bs2b_cross_feed(device->Bs2b, samples);                   \
                    ((type*)buffer)[ChanMap[FRONT_LEFT]] = (func)(samples[0]);\
                    ((type*)buffer)[ChanMap[FRONT_RIGHT]]= (func)(samples[1]);\
                    buffer = ((type*)buffer) + 2;                             \
                }                                                             \
            }                                                                 \
            else                                                              \
            {                                                                 \
                for(i = 0;i < SamplesToDo;i++)                                \
                {                                                             \
                    static const Channel chans[] = {                          \
                        FRONT_LEFT, FRONT_RIGHT                               \
                    };                                                        \
                    for(j = 0;j < 2;j++)                                      \
                    {                                                         \
                        samp = 0.0f;                                          \
                        for(c = 0;c < OUTPUTCHANNELS;c++)                     \
                            samp += DryBuffer[i][c] * Matrix[c][chans[j]];    \
                        ((type*)buffer)[ChanMap[chans[j]]] = (func)(samp);    \
                    }                                                         \
                    buffer = ((type*)buffer) + 2;                             \
                }                                                             \
            }                                                                 \
            break;                                                            \
        case AL_FORMAT_QUAD##bits:                                            \
            for(i = 0;i < SamplesToDo;i++)                                    \
            {                                                                 \
                static const Channel chans[] = {                              \
                    FRONT_LEFT, FRONT_RIGHT,                                  \
                    BACK_LEFT,  BACK_RIGHT,                                   \
                };                                                            \
                for(j = 0;j < 4;j++)                                          \
                {                                                             \
                    samp = 0.0f;                                              \
                    for(c = 0;c < OUTPUTCHANNELS;c++)                         \
                        samp += DryBuffer[i][c] * Matrix[c][chans[j]];        \
                    ((type*)buffer)[ChanMap[chans[j]]] = (func)(samp);        \
                }                                                             \
                buffer = ((type*)buffer) + 4;                                 \
            }                                                                 \
            break;                                                            \
        case AL_FORMAT_51CHN##bits:                                           \
            for(i = 0;i < SamplesToDo;i++)                                    \
            {                                                                 \
                static const Channel chans[] = {                              \
                    FRONT_LEFT, FRONT_RIGHT,                                  \
                    FRONT_CENTER, LFE,                                        \
                    BACK_LEFT,  BACK_RIGHT,                                   \
                };                                                            \
                for(j = 0;j < 6;j++)                                          \
                {                                                             \
                    samp = 0.0f;                                              \
                    for(c = 0;c < OUTPUTCHANNELS;c++)                         \
                        samp += DryBuffer[i][c] * Matrix[c][chans[j]];        \
                    ((type*)buffer)[ChanMap[chans[j]]] = (func)(samp);        \
                }                                                             \
                buffer = ((type*)buffer) + 6;                                 \
            }                                                                 \
            break;                                                            \
        case AL_FORMAT_61CHN##bits:                                           \
            for(i = 0;i < SamplesToDo;i++)                                    \
            {                                                                 \
                static const Channel chans[] = {                              \
                    FRONT_LEFT, FRONT_RIGHT,                                  \
                    FRONT_CENTER, LFE, BACK_CENTER,                           \
                    SIDE_LEFT,  SIDE_RIGHT,                                   \
                };                                                            \
                for(j = 0;j < 7;j++)                                          \
                {                                                             \
                    samp = 0.0f;                                              \
                    for(c = 0;c < OUTPUTCHANNELS;c++)                         \
                        samp += DryBuffer[i][c] * Matrix[c][chans[j]];        \
                    ((type*)buffer)[ChanMap[chans[j]]] = (func)(samp);        \
                }                                                             \
                buffer = ((type*)buffer) + 7;                                 \
            }                                                                 \
            break;                                                            \
        case AL_FORMAT_71CHN##bits:                                           \
            for(i = 0;i < SamplesToDo;i++)                                    \
            {                                                                 \
                static const Channel chans[] = {                              \
                    FRONT_LEFT, FRONT_RIGHT,                                  \
                    FRONT_CENTER, LFE,                                        \
                    BACK_LEFT,  BACK_RIGHT,                                   \
                    SIDE_LEFT,  SIDE_RIGHT                                    \
                };                                                            \
                for(j = 0;j < 8;j++)                                          \
                {                                                             \
                    samp = 0.0f;                                              \
                    for(c = 0;c < OUTPUTCHANNELS;c++)                         \
                        samp += DryBuffer[i][c] * Matrix[c][chans[j]];        \
                    ((type*)buffer)[ChanMap[chans[j]]] = (func)(samp);        \
                }                                                             \
                buffer = ((type*)buffer) + 8;                                 \
            }                                                                 \
            break;

#define AL_FORMAT_MONO32 AL_FORMAT_MONO_FLOAT32
#define AL_FORMAT_STEREO32 AL_FORMAT_STEREO_FLOAT32
            CHECK_WRITE_FORMAT(8,  ALubyte, aluF2UB)
            CHECK_WRITE_FORMAT(16, ALshort, aluF2S)
            CHECK_WRITE_FORMAT(32, ALfloat, aluF2F)
#undef AL_FORMAT_STEREO32
#undef AL_FORMAT_MONO32
#undef CHECK_WRITE_FORMAT

            default:
                break;
        }

        size -= SamplesToDo;
    }

#if defined(HAVE_FESETROUND)
    fesetround(fpuState);
#elif defined(HAVE__CONTROLFP)
    _controlfp(fpuState, 0xfffff);
#endif
}
