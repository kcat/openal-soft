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

    if(Value <= -1.0f) i = -32768;
    else if(Value >= 1.0f) i = 32767;
    else i = (ALint)(Value*32767.0f);

    return ((ALshort)i);
}

static __inline ALubyte aluF2UB(ALfloat Value)
{
    ALshort i = aluF2S(Value);
    return (i>>8)+128;
}


static __inline ALfloat point32(ALfloat val1, ALfloat val2, ALint frac)
{
    return val1;
    (void)val2;
    (void)frac;
}
static __inline ALfloat lerp32(ALfloat val1, ALfloat val2, ALint frac)
{
    return val1 + ((val2-val1)*(frac * (1.0f/(1<<FRACTIONBITS))));
}
static __inline ALfloat cos_lerp32(ALfloat val1, ALfloat val2, ALint frac)
{
    ALfloat mult = (1.0f-cos(frac * (1.0f/(1<<FRACTIONBITS)) * M_PI)) * 0.5f;
    return val1 + ((val2-val1)*mult);
}

static __inline ALfloat point16(ALfloat val1, ALfloat val2, ALint frac)
{
    return val1 / 32767.0f;
    (void)val2;
    (void)frac;
}
static __inline ALfloat lerp16(ALfloat val1, ALfloat val2, ALint frac)
{
    val1 += ((val2-val1)*(frac * (1.0f/(1<<FRACTIONBITS))));
    return val1 / 32767.0f;
}
static __inline ALfloat cos_lerp16(ALfloat val1, ALfloat val2, ALint frac)
{
    ALfloat mult = (1.0f-cos(frac * (1.0f/(1<<FRACTIONBITS)) * M_PI)) * 0.5f;
    val1 += ((val2-val1)*mult);
    return val1 / 32767.0f;
}


#define DO_MIX_MONO(S,sampler) do {                                           \
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
        value = sampler##S(Data.p##S[pos], Data.p##S[pos+1], frac);           \
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
        value = sampler##S(Data.p##S[pos], Data.p##S[pos+1], frac);           \
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
        value = sampler##S(Data.p##S[p], Data.p##S[p+1], f);                  \
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
    for(out = 0;out < MAX_SENDS;out++)                                        \
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
            value = sampler##S(Data.p##S[pos], Data.p##S[pos+1], frac);       \
                                                                              \
            value = lpFilter2PC(WetFilter, 0, value);                         \
            WetClickRemoval[0] -= value*WetSend;                              \
        }                                                                     \
        for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                 \
        {                                                                     \
            /* First order interpolator */                                    \
            value = sampler##S(Data.p##S[pos], Data.p##S[pos+1], frac);       \
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
            value = sampler##S(Data.p##S[p], Data.p##S[p+1], f);              \
                                                                              \
            value = lpFilter2PC(WetFilter, 0, value);                         \
            WetPendingClicks[0] += value*WetSend;                             \
        }                                                                     \
    }                                                                         \
    DataPosInt = pos;                                                         \
    DataPosFrac = frac;                                                       \
} while(0)

#define DO_MIX_STEREO(S,sampler) do {                                         \
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
            value = sampler##S(Data.p##S[pos*Channels + i],                   \
                               Data.p##S[(pos+1)*Channels + i], frac);        \
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
            value = sampler##S(Data.p##S[pos*Channels + i],                   \
                               Data.p##S[(pos+1)*Channels + i], frac);        \
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
            value = sampler##S(Data.p##S[p*Channels + i],                     \
                               Data.p##S[(p+1)*Channels + i], f);             \
                                                                              \
            value = lpFilter2PC(DryFilter, chans[i]*2, value);                \
            PendingClicks[chans[i+0]] += value*DrySend[chans[i+0]];           \
            PendingClicks[chans[i+2]] += value*DrySend[chans[i+2]];           \
            PendingClicks[chans[i+4]] += value*DrySend[chans[i+4]];           \
        }                                                                     \
    }                                                                         \
                                                                              \
    for(out = 0;out < MAX_SENDS;out++)                                        \
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
                value = sampler##S(Data.p##S[pos*Channels + i],               \
                                   Data.p##S[(pos+1)*Channels + i], frac);    \
                                                                              \
                value = lpFilter1PC(WetFilter, chans[i], value);              \
                WetClickRemoval[0] -= value*WetSend * scaler;                 \
            }                                                                 \
        }                                                                     \
        for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                 \
        {                                                                     \
            for(i = 0;i < Channels;i++)                                       \
            {                                                                 \
                value = sampler##S(Data.p##S[pos*Channels + i],               \
                                   Data.p##S[(pos+1)*Channels + i], frac);    \
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
                value = sampler##S(Data.p##S[p*Channels + i],                 \
                                   Data.p##S[(p+1)*Channels + i], f);         \
                                                                              \
                value = lpFilter1PC(WetFilter, chans[i], value);              \
                WetPendingClicks[0] += value*WetSend * scaler;                \
            }                                                                 \
        }                                                                     \
    }                                                                         \
    DataPosInt = pos;                                                         \
    DataPosFrac = frac;                                                       \
} while(0)

#define DO_MIX_MC(S,sampler) do {                                             \
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
            value = sampler##S(Data.p##S[pos*Channels + i],                   \
                               Data.p##S[(pos+1)*Channels + i], frac);        \
                                                                              \
            value = lpFilter2PC(DryFilter, chans[i]*2, value);                \
            ClickRemoval[chans[i]] -= value*DrySend[chans[i]];                \
        }                                                                     \
    }                                                                         \
    for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                     \
    {                                                                         \
        for(i = 0;i < Channels;i++)                                           \
        {                                                                     \
            value = sampler##S(Data.p##S[pos*Channels + i],                   \
                               Data.p##S[(pos+1)*Channels + i], frac);        \
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
            value = sampler##S(Data.p##S[p*Channels + i],                     \
                               Data.p##S[(p+1)*Channels + i], f);             \
                                                                              \
            value = lpFilter2PC(DryFilter, chans[i]*2, value);                \
            PendingClicks[chans[i]] += value*DrySend[chans[i]];               \
        }                                                                     \
    }                                                                         \
                                                                              \
    for(out = 0;out < MAX_SENDS;out++)                                        \
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
                value = sampler##S(Data.p##S[pos*Channels + i],               \
                                   Data.p##S[(pos+1)*Channels + i], frac);    \
                                                                              \
                value = lpFilter1PC(WetFilter, chans[i], value);              \
                WetClickRemoval[0] -= value*WetSend * scaler;                 \
            }                                                                 \
        }                                                                     \
        for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                 \
        {                                                                     \
            for(i = 0;i < Channels;i++)                                       \
            {                                                                 \
                value = sampler##S(Data.p##S[pos*Channels + i],               \
                                   Data.p##S[(pos+1)*Channels + i], frac);    \
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
                value = sampler##S(Data.p##S[p*Channels + i],                 \
                                   Data.p##S[(p+1)*Channels + i], f);         \
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
        DO_MIX_MONO(32,sampler);                                              \
    else if(Bytes == 2)                                                       \
        DO_MIX_MONO(16,sampler);                                              \
} while(0)

#define MIX_STEREO(sampler) do {                                              \
    const int chans[] = {                                                     \
        FRONT_LEFT, FRONT_RIGHT,                                              \
        SIDE_LEFT, SIDE_RIGHT,                                                \
        BACK_LEFT, BACK_RIGHT                                                 \
    };                                                                        \
                                                                              \
    if(Bytes == 4)                                                            \
        DO_MIX_STEREO(32,sampler);                                            \
    else if(Bytes == 2)                                                       \
        DO_MIX_STEREO(16,sampler);                                            \
} while(0)

#define MIX_MC(sampler,...) do {                                              \
    const int chans[] = { __VA_ARGS__ };                                      \
                                                                              \
    if(Bytes == 4)                                                            \
        DO_MIX_MC(32,sampler);                                                \
    else if(Bytes == 2)                                                       \
        DO_MIX_MC(16,sampler);                                                \
} while(0)


#define MIX(sampler) do {                                                     \
    if(Channels == 1) /* Mono */                                              \
        MIX_MONO(sampler);                                                    \
    else if(Channels == 2) /* Stereo */                                       \
        MIX_STEREO(sampler);                                                  \
    else if(Channels == 4) /* Quad */                                         \
        MIX_MC(sampler, FRONT_LEFT, FRONT_RIGHT,                              \
                        BACK_LEFT,  BACK_RIGHT);                              \
    else if(Channels == 6) /* 5.1 */                                          \
        MIX_MC(sampler, FRONT_LEFT,   FRONT_RIGHT,                            \
                        FRONT_CENTER, LFE,                                    \
                        BACK_LEFT,    BACK_RIGHT);                            \
    else if(Channels == 7) /* 6.1 */                                          \
        MIX_MC(sampler, FRONT_LEFT,   FRONT_RIGHT,                            \
                        FRONT_CENTER, LFE,                                    \
                        BACK_CENTER,                                          \
                        SIDE_LEFT,    SIDE_RIGHT);                            \
    else if(Channels == 8) /* 7.1 */                                          \
        MIX_MC(sampler, FRONT_LEFT,   FRONT_RIGHT,                            \
                        FRONT_CENTER, LFE,                                    \
                        BACK_LEFT,    BACK_RIGHT,                             \
                        SIDE_LEFT,    SIDE_RIGHT);                            \
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
        else
            memset(&Data.p8[DataSize*Channels*Bytes], 0, (BUFFER_PADDING*Channels*Bytes));

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


ALvoid aluMixData(ALCdevice *device, ALvoid *buffer, ALsizei size)
{
    ALuint SamplesToDo;
    ALeffectslot *ALEffectSlot;
    ALCcontext **ctx, **ctx_end;
    ALsource **src, **src_end;
    int fpuState;
    ALuint i, j, c;
    ALsizei e;

#if defined(HAVE_FESETROUND)
    fpuState = fegetround();
    fesetround(FE_TOWARDZERO);
#elif defined(HAVE__CONTROLFP)
    fpuState = _controlfp(_RC_CHOP, _MCW_RC);
#else
    (void)fpuState;
#endif

    while(size > 0)
    {
        /* Setup variables */
        SamplesToDo = min(size, BUFFERSIZE);

        /* Clear mixing buffer */
        memset(device->DryBuffer, 0, SamplesToDo*OUTPUTCHANNELS*sizeof(ALfloat));

        SuspendContext(NULL);
        ctx = device->Contexts;
        ctx_end = ctx + device->NumContexts;
        while(ctx != ctx_end)
        {
            SuspendContext(*ctx);

            src = (*ctx)->ActiveSources;
            src_end = src + (*ctx)->ActiveSourceCount;
            while(src != src_end)
            {
                if((*src)->state != AL_PLAYING)
                {
                    --((*ctx)->ActiveSourceCount);
                    *src = *(--src_end);
                    continue;
                }

                if((*src)->NeedsUpdate)
                {
                    ALsource_Update(*src, *ctx);
                    (*src)->NeedsUpdate = AL_FALSE;
                }

                ALsource_Mix(*src, device, SamplesToDo);
                src++;
            }

            /* effect slot processing */
            for(e = 0;e < (*ctx)->EffectSlotMap.size;e++)
            {
                ALEffectSlot = (*ctx)->EffectSlotMap.array[e].value;

                for(i = 0;i < SamplesToDo;i++)
                {
                    ALEffectSlot->ClickRemoval[0] -= ALEffectSlot->ClickRemoval[0] / 256.0f;
                    ALEffectSlot->WetBuffer[i] += ALEffectSlot->ClickRemoval[0];
                }
                for(i = 0;i < 1;i++)
                {
                    ALEffectSlot->ClickRemoval[i] += ALEffectSlot->PendingClicks[i];
                    ALEffectSlot->PendingClicks[i] = 0.0f;
                }

                ALEffect_Process(ALEffectSlot->EffectState, ALEffectSlot,
                                 SamplesToDo, ALEffectSlot->WetBuffer,
                                 device->DryBuffer);

                for(i = 0;i < SamplesToDo;i++)
                    ALEffectSlot->WetBuffer[i] = 0.0f;
            }

            ProcessContext(*ctx);
            ctx++;
        }
        device->SamplesPlayed += SamplesToDo;
        ProcessContext(NULL);

        //Post processing loop
        for(i = 0;i < SamplesToDo;i++)
        {
            for(c = 0;c < OUTPUTCHANNELS;c++)
            {
                device->ClickRemoval[c] -= device->ClickRemoval[c] / 256.0f;
                device->DryBuffer[i][c] += device->ClickRemoval[c];
            }
        }
        for(i = 0;i < OUTPUTCHANNELS;i++)
        {
            device->ClickRemoval[i] += device->PendingClicks[i];
            device->PendingClicks[i] = 0.0f;
        }

        switch(device->Format)
        {
#define DO_WRITE(T, func, N, ...) do {                                        \
    const Channel chans[] = {                                                 \
        __VA_ARGS__                                                           \
    };                                                                        \
    ALfloat (*DryBuffer)[OUTPUTCHANNELS] = device->DryBuffer;                 \
    ALfloat (*Matrix)[OUTPUTCHANNELS] = device->ChannelMatrix;                \
    const ALuint *ChanMap = device->DevChannels;                              \
                                                                              \
    for(i = 0;i < SamplesToDo;i++)                                            \
    {                                                                         \
        for(j = 0;j < N;j++)                                                  \
        {                                                                     \
            ALfloat samp = 0.0f;                                              \
            for(c = 0;c < OUTPUTCHANNELS;c++)                                 \
                samp += DryBuffer[i][c] * Matrix[c][chans[j]];                \
            ((T*)buffer)[ChanMap[chans[j]]] = func(samp);                     \
        }                                                                     \
        buffer = ((T*)buffer) + N;                                            \
    }                                                                         \
} while(0)

#define CHECK_WRITE_FORMAT(bits, T, func)                                     \
        case AL_FORMAT_MONO##bits:                                            \
            DO_WRITE(T, func, 1, FRONT_CENTER);                               \
            break;                                                            \
        case AL_FORMAT_STEREO##bits:                                          \
            if(device->Bs2b)                                                  \
            {                                                                 \
                ALfloat (*DryBuffer)[OUTPUTCHANNELS] = device->DryBuffer;     \
                ALfloat (*Matrix)[OUTPUTCHANNELS] = device->ChannelMatrix;    \
                const ALuint *ChanMap = device->DevChannels;                  \
                                                                              \
                for(i = 0;i < SamplesToDo;i++)                                \
                {                                                             \
                    float samples[2] = { 0.0f, 0.0f };                        \
                    for(c = 0;c < OUTPUTCHANNELS;c++)                         \
                    {                                                         \
                        samples[0] += DryBuffer[i][c]*Matrix[c][FRONT_LEFT];  \
                        samples[1] += DryBuffer[i][c]*Matrix[c][FRONT_RIGHT]; \
                    }                                                         \
                    bs2b_cross_feed(device->Bs2b, samples);                   \
                    ((T*)buffer)[ChanMap[FRONT_LEFT]]  = func(samples[0]);    \
                    ((T*)buffer)[ChanMap[FRONT_RIGHT]] = func(samples[1]);    \
                    buffer = ((T*)buffer) + 2;                                \
                }                                                             \
            }                                                                 \
            else                                                              \
                DO_WRITE(T, func, 2, FRONT_LEFT, FRONT_RIGHT);                \
            break;                                                            \
        case AL_FORMAT_QUAD##bits:                                            \
            DO_WRITE(T, func, 4, FRONT_LEFT, FRONT_RIGHT,                     \
                                 BACK_LEFT,  BACK_RIGHT);                     \
            break;                                                            \
        case AL_FORMAT_51CHN##bits:                                           \
            DO_WRITE(T, func, 6, FRONT_LEFT, FRONT_RIGHT,                     \
                                 FRONT_CENTER, LFE,                           \
                                 BACK_LEFT,  BACK_RIGHT);                     \
            break;                                                            \
        case AL_FORMAT_61CHN##bits:                                           \
            DO_WRITE(T, func, 7, FRONT_LEFT, FRONT_RIGHT,                     \
                                 FRONT_CENTER, LFE, BACK_CENTER,              \
                                 SIDE_LEFT,  SIDE_RIGHT);                     \
            break;                                                            \
        case AL_FORMAT_71CHN##bits:                                           \
            DO_WRITE(T, func, 8, FRONT_LEFT, FRONT_RIGHT,                     \
                                 FRONT_CENTER, LFE,                           \
                                 BACK_LEFT,  BACK_RIGHT,                      \
                                 SIDE_LEFT,  SIDE_RIGHT);                     \
            break;

#define AL_FORMAT_MONO32 AL_FORMAT_MONO_FLOAT32
#define AL_FORMAT_STEREO32 AL_FORMAT_STEREO_FLOAT32
            CHECK_WRITE_FORMAT(8,  ALubyte, aluF2UB)
            CHECK_WRITE_FORMAT(16, ALshort, aluF2S)
            CHECK_WRITE_FORMAT(32, ALfloat, aluF2F)
#undef AL_FORMAT_STEREO32
#undef AL_FORMAT_MONO32
#undef CHECK_WRITE_FORMAT
#undef DO_WRITE

            default:
                break;
        }

        size -= SamplesToDo;
    }

#if defined(HAVE_FESETROUND)
    fesetround(fpuState);
#elif defined(HAVE__CONTROLFP)
    _controlfp(fpuState, _MCW_RC);
#endif
}
