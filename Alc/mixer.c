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


static __inline ALfloat point32(ALfloat val1, ALfloat val2, ALint frac)
{
    return val1;
    (void)val2;
    (void)frac;
}
static __inline ALfloat lerp32(ALfloat val1, ALfloat val2, ALint frac)
{
    val1 += ((val2-val1) * (frac * (1.0/(1<<FRACTIONBITS))));
    return val1;
}
static __inline ALfloat cos_lerp32(ALfloat val1, ALfloat val2, ALint frac)
{
    val1 += ((val2-val1) * ((1.0-cos(frac * (1.0/(1<<FRACTIONBITS)) * M_PI)) * 0.5));
    return val1;
}

static __inline ALfloat point16(ALfloat val1, ALfloat val2, ALint frac)
{
    return val1 / 32767.0f;
    (void)val2;
    (void)frac;
}
static __inline ALfloat lerp16(ALfloat val1, ALfloat val2, ALint frac)
{
    val1 += (val2-val1) * (frac * (1.0/(1<<FRACTIONBITS)));
    return val1 / 32767.0f;
}
static __inline ALfloat cos_lerp16(ALfloat val1, ALfloat val2, ALint frac)
{
    val1 += (val2-val1) * ((1.0-cos(frac * (1.0/(1<<FRACTIONBITS)) * M_PI)) * 0.5);
    return val1 / 32767.0f;
}


#define DO_MIX_MONO(data,sampler) do {                                        \
    ALfloat (*DryBuffer)[OUTPUTCHANNELS];                                     \
    ALfloat *ClickRemoval, *PendingClicks;                                    \
    ALuint pos = DataPosInt;                                                  \
    ALuint frac = DataPosFrac;                                                \
    ALfloat DrySend[OUTPUTCHANNELS];                                          \
    FILTER *DryFilter;                                                        \
    ALuint BufferIdx;                                                         \
    ALuint i, out;                                                            \
    ALfloat value;                                                            \
                                                                              \
    DryBuffer = Device->DryBuffer;                                            \
    ClickRemoval = Device->ClickRemoval;                                      \
    PendingClicks = Device->PendingClicks;                                    \
    DryFilter = &Source->Params.iirFilter;                                    \
    for(i = 0;i < OUTPUTCHANNELS;i++)                                         \
        DrySend[i] = Source->Params.DryGains[i];                              \
                                                                              \
    if(j == 0)                                                                \
    {                                                                         \
        value = sampler(data[pos], data[pos+1], frac);                        \
                                                                              \
        value = lpFilter4PC(DryFilter, 0, value);                             \
        ClickRemoval[FRONT_LEFT]   -= value*DrySend[FRONT_LEFT];              \
        ClickRemoval[FRONT_RIGHT]  -= value*DrySend[FRONT_RIGHT];             \
        ClickRemoval[SIDE_LEFT]    -= value*DrySend[SIDE_LEFT];               \
        ClickRemoval[SIDE_RIGHT]   -= value*DrySend[SIDE_RIGHT];              \
        ClickRemoval[BACK_LEFT]    -= value*DrySend[BACK_LEFT];               \
        ClickRemoval[BACK_RIGHT]   -= value*DrySend[BACK_RIGHT];              \
        ClickRemoval[FRONT_CENTER] -= value*DrySend[FRONT_CENTER];            \
        ClickRemoval[BACK_CENTER]  -= value*DrySend[BACK_CENTER];             \
    }                                                                         \
    for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                     \
    {                                                                         \
        /* First order interpolator */                                        \
        value = sampler(data[pos], data[pos+1], frac);                        \
                                                                              \
        /* Direct path final mix buffer and panning */                        \
        value = lpFilter4P(DryFilter, 0, value);                              \
        DryBuffer[j][FRONT_LEFT]   += value*DrySend[FRONT_LEFT];              \
        DryBuffer[j][FRONT_RIGHT]  += value*DrySend[FRONT_RIGHT];             \
        DryBuffer[j][SIDE_LEFT]    += value*DrySend[SIDE_LEFT];               \
        DryBuffer[j][SIDE_RIGHT]   += value*DrySend[SIDE_RIGHT];              \
        DryBuffer[j][BACK_LEFT]    += value*DrySend[BACK_LEFT];               \
        DryBuffer[j][BACK_RIGHT]   += value*DrySend[BACK_RIGHT];              \
        DryBuffer[j][FRONT_CENTER] += value*DrySend[FRONT_CENTER];            \
        DryBuffer[j][BACK_CENTER]  += value*DrySend[BACK_CENTER];             \
                                                                              \
        frac += increment;                                                    \
        pos  += frac>>FRACTIONBITS;                                           \
        frac &= FRACTIONMASK;                                                 \
        j++;                                                                  \
    }                                                                         \
    if(j == SamplesToDo)                                                      \
    {                                                                         \
        ALuint p = pos;                                                       \
        ALuint f = frac;                                                      \
        if(p >= LoopEnd)                                                      \
        {                                                                     \
            ALuint64 pos64 = pos;                                             \
            pos64 <<= FRACTIONBITS;                                           \
            pos64 += frac;                                                    \
            pos64 -= increment;                                               \
            p = pos64>>FRACTIONBITS;                                          \
            f = pos64&FRACTIONMASK;                                           \
        }                                                                     \
        value = sampler(data[p], data[p+1], f);                               \
                                                                              \
        value = lpFilter4PC(DryFilter, 0, value);                             \
        PendingClicks[FRONT_LEFT]   += value*DrySend[FRONT_LEFT];             \
        PendingClicks[FRONT_RIGHT]  += value*DrySend[FRONT_RIGHT];            \
        PendingClicks[SIDE_LEFT]    += value*DrySend[SIDE_LEFT];              \
        PendingClicks[SIDE_RIGHT]   += value*DrySend[SIDE_RIGHT];             \
        PendingClicks[BACK_LEFT]    += value*DrySend[BACK_LEFT];              \
        PendingClicks[BACK_RIGHT]   += value*DrySend[BACK_RIGHT];             \
        PendingClicks[FRONT_CENTER] += value*DrySend[FRONT_CENTER];           \
        PendingClicks[BACK_CENTER]  += value*DrySend[BACK_CENTER];            \
    }                                                                         \
                                                                              \
    for(out = 0;out < Device->NumAuxSends;out++)                              \
    {                                                                         \
        ALfloat  WetSend;                                                     \
        ALfloat *WetBuffer;                                                   \
        ALfloat *WetClickRemoval;                                             \
        ALfloat *WetPendingClicks;                                            \
        FILTER  *WetFilter;                                                   \
                                                                              \
        if(!Source->Send[out].Slot ||                                         \
           Source->Send[out].Slot->effect.type == AL_EFFECT_NULL)             \
            continue;                                                         \
                                                                              \
        WetSend = Source->Params.WetGains[out];                               \
        WetBuffer = Source->Send[out].Slot->WetBuffer;                        \
        WetClickRemoval = Source->Send[out].Slot->ClickRemoval;               \
        WetPendingClicks = Source->Send[out].Slot->PendingClicks;             \
        WetFilter = &Source->Params.Send[out].iirFilter;                      \
                                                                              \
        pos = DataPosInt;                                                     \
        frac = DataPosFrac;                                                   \
        j -= BufferSize;                                                      \
                                                                              \
        if(j == 0)                                                            \
        {                                                                     \
            value = sampler(data[pos], data[pos+1], frac);                    \
                                                                              \
            value = lpFilter2PC(WetFilter, 0, value);                         \
            WetClickRemoval[0] -= value*WetSend;                              \
        }                                                                     \
        for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                 \
        {                                                                     \
            /* First order interpolator */                                    \
            value = sampler(data[pos], data[pos+1], frac);                    \
                                                                              \
            /* Room path final mix buffer and panning */                      \
            value = lpFilter2P(WetFilter, 0, value);                          \
            WetBuffer[j] += value*WetSend;                                    \
                                                                              \
            frac += increment;                                                \
            pos  += frac>>FRACTIONBITS;                                       \
            frac &= FRACTIONMASK;                                             \
            j++;                                                              \
        }                                                                     \
        if(j == SamplesToDo)                                                  \
        {                                                                     \
            ALuint p = pos;                                                   \
            ALuint f = frac;                                                  \
            if(p >= LoopEnd)                                                  \
            {                                                                 \
                ALuint64 pos64 = pos;                                         \
                pos64 <<= FRACTIONBITS;                                       \
                pos64 += frac;                                                \
                pos64 -= increment;                                           \
                p = pos64>>FRACTIONBITS;                                      \
                f = pos64&FRACTIONMASK;                                       \
            }                                                                 \
            value = sampler(data[p], data[p+1], f);                           \
                                                                              \
            value = lpFilter2PC(WetFilter, 0, value);                         \
            WetPendingClicks[0] += value*WetSend;                             \
        }                                                                     \
    }                                                                         \
    DataPosInt = pos;                                                         \
    DataPosFrac = frac;                                                       \
} while(0)

#define DO_MIX_STEREO(data,sampler) do {                                      \
    const ALfloat scaler = 1.0f/Channels;                                     \
    ALfloat (*DryBuffer)[OUTPUTCHANNELS];                                     \
    ALfloat *ClickRemoval, *PendingClicks;                                    \
    ALuint pos = DataPosInt;                                                  \
    ALuint frac = DataPosFrac;                                                \
    ALfloat DrySend[OUTPUTCHANNELS];                                          \
    FILTER *DryFilter;                                                        \
    ALuint BufferIdx;                                                         \
    ALuint i, out;                                                            \
    ALfloat value;                                                            \
                                                                              \
    DryBuffer = Device->DryBuffer;                                            \
    ClickRemoval = Device->ClickRemoval;                                      \
    PendingClicks = Device->PendingClicks;                                    \
    DryFilter = &Source->Params.iirFilter;                                    \
    for(i = 0;i < OUTPUTCHANNELS;i++)                                         \
        DrySend[i] = Source->Params.DryGains[i];                              \
                                                                              \
    if(j == 0)                                                                \
    {                                                                         \
        for(i = 0;i < Channels;i++)                                           \
        {                                                                     \
            value = sampler(data[pos*Channels + i],                           \
                            data[(pos+1)*Channels + i], frac);                \
                                                                              \
            value = lpFilter2PC(DryFilter, chans[i]*2, value);                \
            ClickRemoval[chans[i+0]] -= value*DrySend[chans[i+0]];            \
            ClickRemoval[chans[i+2]] -= value*DrySend[chans[i+2]];            \
            ClickRemoval[chans[i+4]] -= value*DrySend[chans[i+4]];            \
        }                                                                     \
    }                                                                         \
    for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                     \
    {                                                                         \
        for(i = 0;i < Channels;i++)                                           \
        {                                                                     \
            value = sampler(data[pos*Channels + i],                           \
                            data[(pos+1)*Channels + i], frac);                \
                                                                              \
            value = lpFilter2P(DryFilter, chans[i]*2, value);                 \
            DryBuffer[j][chans[i+0]] += value*DrySend[chans[i+0]];            \
            DryBuffer[j][chans[i+2]] += value*DrySend[chans[i+2]];            \
            DryBuffer[j][chans[i+4]] += value*DrySend[chans[i+4]];            \
        }                                                                     \
                                                                              \
        frac += increment;                                                    \
        pos  += frac>>FRACTIONBITS;                                           \
        frac &= FRACTIONMASK;                                                 \
        j++;                                                                  \
    }                                                                         \
    if(j == SamplesToDo)                                                      \
    {                                                                         \
        ALuint p = pos;                                                       \
        ALuint f = frac;                                                      \
        if(p >= LoopEnd)                                                      \
        {                                                                     \
            ALuint64 pos64 = pos;                                             \
            pos64 <<= FRACTIONBITS;                                           \
            pos64 += frac;                                                    \
            pos64 -= increment;                                               \
            p = pos64>>FRACTIONBITS;                                          \
            f = pos64&FRACTIONMASK;                                           \
        }                                                                     \
        for(i = 0;i < Channels;i++)                                           \
        {                                                                     \
            value = sampler(data[p*Channels + i],                             \
                            data[(p+1)*Channels + i], f);                     \
                                                                              \
            value = lpFilter2PC(DryFilter, chans[i]*2, value);                \
            PendingClicks[chans[i+0]] += value*DrySend[chans[i+0]];           \
            PendingClicks[chans[i+2]] += value*DrySend[chans[i+2]];           \
            PendingClicks[chans[i+4]] += value*DrySend[chans[i+4]];           \
        }                                                                     \
    }                                                                         \
                                                                              \
    for(out = 0;out < Device->NumAuxSends;out++)                              \
    {                                                                         \
        ALfloat  WetSend;                                                     \
        ALfloat *WetBuffer;                                                   \
        ALfloat *WetClickRemoval;                                             \
        ALfloat *WetPendingClicks;                                            \
        FILTER  *WetFilter;                                                   \
                                                                              \
        if(!Source->Send[out].Slot ||                                         \
           Source->Send[out].Slot->effect.type == AL_EFFECT_NULL)             \
            continue;                                                         \
                                                                              \
        WetSend = Source->Params.WetGains[out];                               \
        WetBuffer = Source->Send[out].Slot->WetBuffer;                        \
        WetClickRemoval = Source->Send[out].Slot->ClickRemoval;               \
        WetPendingClicks = Source->Send[out].Slot->PendingClicks;             \
        WetFilter = &Source->Params.Send[out].iirFilter;                      \
                                                                              \
        pos = DataPosInt;                                                     \
        frac = DataPosFrac;                                                   \
        j -= BufferSize;                                                      \
                                                                              \
        if(j == 0)                                                            \
        {                                                                     \
            for(i = 0;i < Channels;i++)                                       \
            {                                                                 \
                value = sampler(data[pos*Channels + i],                       \
                                data[(pos+1)*Channels + i], frac);            \
                                                                              \
                value = lpFilter1PC(WetFilter, chans[i], value);              \
                WetClickRemoval[0] -= value*WetSend * scaler;                 \
            }                                                                 \
        }                                                                     \
        for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                 \
        {                                                                     \
            for(i = 0;i < Channels;i++)                                       \
            {                                                                 \
                value = sampler(data[pos*Channels + i],                       \
                                data[(pos+1)*Channels + i], frac);            \
                                                                              \
                value = lpFilter1P(WetFilter, chans[i], value);               \
                WetBuffer[j] += value*WetSend * scaler;                       \
            }                                                                 \
                                                                              \
            frac += increment;                                                \
            pos  += frac>>FRACTIONBITS;                                       \
            frac &= FRACTIONMASK;                                             \
            j++;                                                              \
        }                                                                     \
        if(j == SamplesToDo)                                                  \
        {                                                                     \
            ALuint p = pos;                                                   \
            ALuint f = frac;                                                  \
            if(p >= LoopEnd)                                                  \
            {                                                                 \
                ALuint64 pos64 = pos;                                         \
                pos64 <<= FRACTIONBITS;                                       \
                pos64 += frac;                                                \
                pos64 -= increment;                                           \
                p = pos64>>FRACTIONBITS;                                      \
                f = pos64&FRACTIONMASK;                                       \
            }                                                                 \
            for(i = 0;i < Channels;i++)                                       \
            {                                                                 \
                value = sampler(data[p*Channels + i],                         \
                                data[(p+1)*Channels + i], f);                 \
                                                                              \
                value = lpFilter1PC(WetFilter, chans[i], value);              \
                WetPendingClicks[0] += value*WetSend * scaler;                \
            }                                                                 \
        }                                                                     \
    }                                                                         \
    DataPosInt = pos;                                                         \
    DataPosFrac = frac;                                                       \
} while(0)

#define DO_MIX_MC(data,sampler) do {                                          \
    const ALfloat scaler = 1.0f/Channels;                                     \
    ALfloat (*DryBuffer)[OUTPUTCHANNELS];                                     \
    ALfloat *ClickRemoval, *PendingClicks;                                    \
    ALuint pos = DataPosInt;                                                  \
    ALuint frac = DataPosFrac;                                                \
    ALfloat DrySend[OUTPUTCHANNELS];                                          \
    FILTER *DryFilter;                                                        \
    ALuint BufferIdx;                                                         \
    ALuint i, out;                                                            \
    ALfloat value;                                                            \
                                                                              \
    DryBuffer = Device->DryBuffer;                                            \
    ClickRemoval = Device->ClickRemoval;                                      \
    PendingClicks = Device->PendingClicks;                                    \
    DryFilter = &Source->Params.iirFilter;                                    \
    for(i = 0;i < OUTPUTCHANNELS;i++)                                         \
        DrySend[i] = Source->Params.DryGains[i];                              \
                                                                              \
    if(j == 0)                                                                \
    {                                                                         \
        for(i = 0;i < Channels;i++)                                           \
        {                                                                     \
            value = sampler(data[pos*Channels + i],                           \
                            data[(pos+1)*Channels + i], frac);                \
                                                                              \
            value = lpFilter2PC(DryFilter, chans[i]*2, value);                \
            ClickRemoval[chans[i]] -= value*DrySend[chans[i]];                \
        }                                                                     \
    }                                                                         \
    for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                     \
    {                                                                         \
        for(i = 0;i < Channels;i++)                                           \
        {                                                                     \
            value = sampler(data[pos*Channels + i],                           \
                            data[(pos+1)*Channels + i], frac);                \
                                                                              \
            value = lpFilter2P(DryFilter, chans[i]*2, value);                 \
            DryBuffer[j][chans[i]] += value*DrySend[chans[i]];                \
        }                                                                     \
                                                                              \
        frac += increment;                                                    \
        pos  += frac>>FRACTIONBITS;                                           \
        frac &= FRACTIONMASK;                                                 \
        j++;                                                                  \
    }                                                                         \
    if(j == SamplesToDo)                                                      \
    {                                                                         \
        ALuint p = pos;                                                       \
        ALuint f = frac;                                                      \
        if(p >= LoopEnd)                                                      \
        {                                                                     \
            ALuint64 pos64 = pos;                                             \
            pos64 <<= FRACTIONBITS;                                           \
            pos64 += frac;                                                    \
            pos64 -= increment;                                               \
            p = pos64>>FRACTIONBITS;                                          \
            f = pos64&FRACTIONMASK;                                           \
        }                                                                     \
        for(i = 0;i < Channels;i++)                                           \
        {                                                                     \
            value = sampler(data[p*Channels + i],                             \
                            data[(p+1)*Channels + i], f);                     \
                                                                              \
            value = lpFilter2PC(DryFilter, chans[i]*2, value);                \
            PendingClicks[chans[i]] += value*DrySend[chans[i]];               \
        }                                                                     \
    }                                                                         \
                                                                              \
    for(out = 0;out < Device->NumAuxSends;out++)                              \
    {                                                                         \
        ALfloat  WetSend;                                                     \
        ALfloat *WetBuffer;                                                   \
        ALfloat *WetClickRemoval;                                             \
        ALfloat *WetPendingClicks;                                            \
        FILTER  *WetFilter;                                                   \
                                                                              \
        if(!Source->Send[out].Slot ||                                         \
           Source->Send[out].Slot->effect.type == AL_EFFECT_NULL)             \
            continue;                                                         \
                                                                              \
        WetBuffer = Source->Send[out].Slot->WetBuffer;                        \
        WetClickRemoval = Source->Send[out].Slot->ClickRemoval;               \
        WetPendingClicks = Source->Send[out].Slot->PendingClicks;             \
        WetFilter = &Source->Params.Send[out].iirFilter;                      \
        WetSend = Source->Params.WetGains[out];                               \
                                                                              \
        pos = DataPosInt;                                                     \
        frac = DataPosFrac;                                                   \
        j -= BufferSize;                                                      \
                                                                              \
        if(j == 0)                                                            \
        {                                                                     \
            for(i = 0;i < Channels;i++)                                       \
            {                                                                 \
                value = sampler(data[pos*Channels + i],                       \
                                data[(pos+1)*Channels + i], frac);            \
                                                                              \
                value = lpFilter1PC(WetFilter, chans[i], value);              \
                WetClickRemoval[0] -= value*WetSend * scaler;                 \
            }                                                                 \
        }                                                                     \
        for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                 \
        {                                                                     \
            for(i = 0;i < Channels;i++)                                       \
            {                                                                 \
                value = sampler(data[pos*Channels + i],                       \
                                data[(pos+1)*Channels + i], frac);            \
                                                                              \
                value = lpFilter1P(WetFilter, chans[i], value);               \
                WetBuffer[j] += value*WetSend * scaler;                       \
            }                                                                 \
                                                                              \
            frac += increment;                                                \
            pos  += frac>>FRACTIONBITS;                                       \
            frac &= FRACTIONMASK;                                             \
            j++;                                                              \
        }                                                                     \
        if(j == SamplesToDo)                                                  \
        {                                                                     \
            ALuint p = pos;                                                   \
            ALuint f = frac;                                                  \
            if(p >= LoopEnd)                                                  \
            {                                                                 \
                ALuint64 pos64 = pos;                                         \
                pos64 <<= FRACTIONBITS;                                       \
                pos64 += frac;                                                \
                pos64 -= increment;                                           \
                p = pos64>>FRACTIONBITS;                                      \
                f = pos64&FRACTIONMASK;                                       \
            }                                                                 \
            for(i = 0;i < Channels;i++)                                       \
            {                                                                 \
                value = sampler(data[p*Channels + i],                         \
                                data[(p+1)*Channels + i], f);                 \
                                                                              \
                value = lpFilter1PC(WetFilter, chans[i], value);              \
                WetPendingClicks[0] += value*WetSend * scaler;                \
            }                                                                 \
        }                                                                     \
    }                                                                         \
    DataPosInt = pos;                                                         \
    DataPosFrac = frac;                                                       \
} while(0)


#define MIX_MONO(sampler) do {                                                \
    if(Bytes == 4)                                                            \
        DO_MIX_MONO(Data.p32,sampler##32);                                    \
    else if(Bytes == 2)                                                       \
        DO_MIX_MONO(Data.p16,sampler##16);                                    \
} while(0)

#define MIX_STEREO(sampler) do {                                              \
    const int chans[] = {                                                     \
        FRONT_LEFT, FRONT_RIGHT,                                              \
        SIDE_LEFT, SIDE_RIGHT,                                                \
        BACK_LEFT, BACK_RIGHT                                                 \
    };                                                                        \
                                                                              \
    if(Bytes == 4)                                                            \
        DO_MIX_STEREO(Data.p32,sampler##32);                                  \
    else if(Bytes == 2)                                                       \
        DO_MIX_STEREO(Data.p16,sampler##16);                                  \
} while(0)

#define MIX_MC(sampler,...) do {                                              \
    const int chans[] = { __VA_ARGS__ };                                      \
                                                                              \
    if(Bytes == 4)                                                            \
        DO_MIX_MC(Data.p32,sampler##32);                                      \
    else if(Bytes == 2)                                                       \
        DO_MIX_MC(Data.p16,sampler##16);                                      \
} while(0)


#define MIX(sampler) do {                                                     \
    switch(Channels)                                                          \
    {                                                                         \
    case 1: /* Mono */                                                        \
        MIX_MONO(sampler);                                                    \
        break;                                                                \
    case 2: /* Stereo */                                                      \
        MIX_STEREO(sampler);                                                  \
        break;                                                                \
    case 4: /* Quad */                                                        \
        MIX_MC(sampler, FRONT_LEFT, FRONT_RIGHT,                              \
                        BACK_LEFT,  BACK_RIGHT);                              \
        break;                                                                \
    case 6: /* 5.1 */                                                         \
        MIX_MC(sampler, FRONT_LEFT,   FRONT_RIGHT,                            \
                        FRONT_CENTER, LFE,                                    \
                        BACK_LEFT,    BACK_RIGHT);                            \
        break;                                                                \
    case 7: /* 6.1 */                                                         \
        MIX_MC(sampler, FRONT_LEFT,   FRONT_RIGHT,                            \
                        FRONT_CENTER, LFE,                                    \
                        BACK_CENTER,                                          \
                        SIDE_LEFT,    SIDE_RIGHT);                            \
        break;                                                                \
    case 8: /* 7.1 */                                                         \
        MIX_MC(sampler, FRONT_LEFT,   FRONT_RIGHT,                            \
                        FRONT_CENTER, LFE,                                    \
                        BACK_LEFT,    BACK_RIGHT,                             \
                        SIDE_LEFT,    SIDE_RIGHT);                            \
        break;                                                                \
    }                                                                         \
} while(0)


ALvoid MixSource(ALsource *Source, ALCdevice *Device, ALuint SamplesToDo)
{
    ALbufferlistitem *BufferListItem;
    ALint64 DataSize64,DataPos64;
    ALint increment;
    ALuint DataPosInt, DataPosFrac;
    ALuint BuffersPlayed;
    ALboolean Looping;
    ALenum State;
    ALuint i, j;

    /* Get source info */
    State         = Source->state;
    BuffersPlayed = Source->BuffersPlayed;
    DataPosInt    = Source->position;
    DataPosFrac   = Source->position_fraction;
    Looping       = Source->bLooping;

    /* Get current buffer queue item */
    BufferListItem = Source->queue;
    for(i = 0;i < BuffersPlayed;i++)
        BufferListItem = BufferListItem->next;

    j = 0;
    do {
        const ALbuffer *ALBuffer;
        union {
            ALfloat *p32;
            ALshort *p16;
            ALubyte *p8;
        } Data = { NULL };
        ALuint DataSize = 0;
        ALuint LoopStart = 0;
        ALuint LoopEnd = 0;
        ALuint Channels, Bytes;
        ALuint BufferSize;

        /* Get buffer info */
        if((ALBuffer=BufferListItem->buffer) != NULL)
        {
            Data.p8   = ALBuffer->data;
            DataSize  = ALBuffer->size;
            DataSize /= aluFrameSizeFromFormat(ALBuffer->format);
            Channels  = aluChannelsFromFormat(ALBuffer->format);
            Bytes     = aluBytesFromFormat(ALBuffer->format);

            LoopStart = 0;
            LoopEnd   = DataSize;
            if(Looping && Source->lSourceType == AL_STATIC)
            {
                /* If current pos is beyond the loop range, do not loop */
                if(DataPosInt >= LoopEnd)
                    Looping = AL_FALSE;
                else
                {
                    LoopStart = ALBuffer->LoopStart;
                    LoopEnd   = ALBuffer->LoopEnd;
                }
            }
        }

        if(DataPosInt >= DataSize)
            goto skipmix;

        memset(&Data.p8[DataSize*Channels*Bytes], 0, BUFFER_PADDING*Channels*Bytes);
        if(BufferListItem->next)
        {
            ALbuffer *NextBuf = BufferListItem->next->buffer;
            if(NextBuf && NextBuf->size)
            {
                ALint ulExtraSamples = BUFFER_PADDING*Channels*Bytes;
                ulExtraSamples = min(NextBuf->size, ulExtraSamples);
                memcpy(&Data.p8[DataSize*Channels*Bytes],
                       NextBuf->data, ulExtraSamples);
            }
        }
        else if(Looping)
        {
            ALbuffer *NextBuf = Source->queue->buffer;
            if(NextBuf && NextBuf->size)
            {
                ALint ulExtraSamples = BUFFER_PADDING*Channels*Bytes;
                ulExtraSamples = min(NextBuf->size, ulExtraSamples);
                memcpy(&Data.p8[DataSize*Channels*Bytes],
                       &((ALubyte*)NextBuf->data)[LoopStart*Channels*Bytes],
                       ulExtraSamples);
            }
        }

        /* Figure out how many samples we can mix. */
        increment = Source->Params.Step;
        DataSize64 = LoopEnd;
        DataSize64 <<= FRACTIONBITS;
        DataPos64 = DataPosInt;
        DataPos64 <<= FRACTIONBITS;
        DataPos64 += DataPosFrac;
        BufferSize = (ALuint)((DataSize64-DataPos64+(increment-1)) / increment);

        BufferSize = min(BufferSize, (SamplesToDo-j));

        switch(Source->Resampler)
        {
            case POINT_RESAMPLER:
            MIX(point); break;
            case LINEAR_RESAMPLER:
            MIX(lerp); break;
            case COSINE_RESAMPLER:
            MIX(cos_lerp); break;
            case RESAMPLER_MIN:
            case RESAMPLER_MAX:
            break;
        }

    skipmix:
        /* Handle looping sources */
        if(DataPosInt >= LoopEnd)
        {
            if(BufferListItem->next)
            {
                BufferListItem = BufferListItem->next;
                BuffersPlayed++;
                DataPosInt -= DataSize;
            }
            else if(Looping)
            {
                BufferListItem = Source->queue;
                BuffersPlayed = 0;
                if(Source->lSourceType == AL_STATIC)
                    DataPosInt = ((DataPosInt-LoopStart)%(LoopEnd-LoopStart)) + LoopStart;
                else
                    DataPosInt -= DataSize;
            }
            else
            {
                State = AL_STOPPED;
                BufferListItem = Source->queue;
                BuffersPlayed = Source->BuffersInQueue;
                DataPosInt = 0;
                DataPosFrac = 0;
            }
        }
    } while(State == AL_PLAYING && j < SamplesToDo);

    /* Update source info */
    Source->state             = State;
    Source->BuffersPlayed     = BuffersPlayed;
    Source->position          = DataPosInt;
    Source->position_fraction = DataPosFrac;
    Source->Buffer            = BufferListItem->buffer;
}
