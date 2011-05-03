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


static __inline ALdouble point32(const ALfloat *vals, ALint step, ALint frac)
{ return vals[0]; (void)step; (void)frac; }
static __inline ALdouble lerp32(const ALfloat *vals, ALint step, ALint frac)
{ return lerp(vals[0], vals[step], frac * (1.0/FRACTIONONE)); }
static __inline ALdouble cubic32(const ALfloat *vals, ALint step, ALint frac)
{ return cubic(vals[-step], vals[0], vals[step], vals[step+step],
               frac * (1.0/FRACTIONONE)); }

static __inline ALdouble point16(const ALshort *vals, ALint step, ALint frac)
{ return vals[0] * (1.0/32767.0); (void)step; (void)frac; }
static __inline ALdouble lerp16(const ALshort *vals, ALint step, ALint frac)
{ return lerp(vals[0], vals[step], frac * (1.0/FRACTIONONE)) * (1.0/32767.0); }
static __inline ALdouble cubic16(const ALshort *vals, ALint step, ALint frac)
{ return cubic(vals[-step], vals[0], vals[step], vals[step+step],
               frac * (1.0/FRACTIONONE)) * (1.0/32767.0); }

static __inline ALdouble point8(const ALubyte *vals, ALint step, ALint frac)
{ return (vals[0]-128.0) * (1.0/127.0); (void)step; (void)frac; }
static __inline ALdouble lerp8(const ALubyte *vals, ALint step, ALint frac)
{ return (lerp(vals[0], vals[step],
               frac * (1.0/FRACTIONONE))-128.0) * (1.0/127.0); }
static __inline ALdouble cubic8(const ALubyte *vals, ALint step, ALint frac)
{ return (cubic(vals[-step], vals[0], vals[step], vals[step+step],
                frac * (1.0/FRACTIONONE))-128.0) * (1.0/127.0); }

#ifdef __GNUC__
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

#define DECL_TEMPLATE(T, sampler)                                             \
void Mix_Hrtf_##T##_1_##sampler(ALsource *Source, ALCdevice *Device,          \
  const T *RESTRICT data, ALuint *DataPosInt, ALuint *DataPosFrac,            \
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)                       \
{                                                                             \
    ALfloat (*RESTRICT DryBuffer)[MAXCHANNELS];                               \
    ALfloat *RESTRICT ClickRemoval, *RESTRICT PendingClicks;                  \
    ALuint pos, frac;                                                         \
    ALfloat (*RESTRICT HrtfCoeffs)[2];                                        \
    ALfloat *RESTRICT HrtfHistory;                                            \
    ALuint HrtfOffset;                                                        \
    FILTER *DryFilter;                                                        \
    ALuint BufferIdx;                                                         \
    ALuint increment;                                                         \
    ALuint out, c;                                                            \
    ALfloat value;                                                            \
                                                                              \
    increment = Source->Params.Step;                                          \
                                                                              \
    DryBuffer = Device->DryBuffer;                                            \
    ClickRemoval = Device->ClickRemoval;                                      \
    PendingClicks = Device->PendingClicks;                                    \
    DryFilter = &Source->Params.iirFilter;                                    \
                                                                              \
    HrtfCoeffs = Source->Params.HrtfCoeffs[0];                                \
    HrtfHistory = Source->HrtfHistory[0];                                     \
    HrtfOffset = Source->HrtfOffset + OutPos;                                 \
                                                                              \
    pos = 0;                                                                  \
    frac = *DataPosFrac;                                                      \
                                                                              \
    if(LIKELY(OutPos == 0))                                                   \
    {                                                                         \
        value = sampler(data+pos, 1, frac);                                   \
        value = lpFilter2PC(DryFilter, 0, value);                             \
                                                                              \
        HrtfHistory[HrtfOffset&HRTF_LENGTH_MASK] = value;                     \
        for(c = 0;c < HRTF_LENGTH;c++)                                        \
        {                                                                     \
            ClickRemoval[FRONT_LEFT] -=                                       \
                               HrtfHistory[(HrtfOffset-c)&HRTF_LENGTH_MASK] * \
                               HrtfCoeffs[c][0];                              \
            ClickRemoval[FRONT_RIGHT] -=                                      \
                               HrtfHistory[(HrtfOffset-c)&HRTF_LENGTH_MASK] * \
                               HrtfCoeffs[c][1];                              \
        }                                                                     \
    }                                                                         \
    for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                     \
    {                                                                         \
        /* First order interpolator */                                        \
        value = sampler(data+pos, 1, frac);                                   \
        value = lpFilter2P(DryFilter, 0, value);                              \
                                                                              \
        /* Direct path final mix buffer and panning */                        \
        HrtfHistory[HrtfOffset&HRTF_LENGTH_MASK] = value;                     \
        for(c = 0;c < HRTF_LENGTH;c++)                                        \
        {                                                                     \
            DryBuffer[OutPos][FRONT_LEFT] +=                                  \
                               HrtfHistory[(HrtfOffset-c)&HRTF_LENGTH_MASK] * \
                               HrtfCoeffs[c][0];                              \
            DryBuffer[OutPos][FRONT_RIGHT] +=                                 \
                               HrtfHistory[(HrtfOffset-c)&HRTF_LENGTH_MASK] * \
                               HrtfCoeffs[c][1];                              \
        }                                                                     \
        HrtfOffset++;                                                         \
                                                                              \
        frac += increment;                                                    \
        pos  += frac>>FRACTIONBITS;                                           \
        frac &= FRACTIONMASK;                                                 \
        OutPos++;                                                             \
    }                                                                         \
    if(LIKELY(OutPos == SamplesToDo))                                         \
    {                                                                         \
        value = sampler(data+pos, 1, frac);                                   \
        value = lpFilter2PC(DryFilter, 0, value);                             \
                                                                              \
        HrtfHistory[HrtfOffset&HRTF_LENGTH_MASK] = value;                     \
        for(c = 0;c < HRTF_LENGTH;c++)                                        \
        {                                                                     \
            PendingClicks[FRONT_LEFT] +=                                      \
                               HrtfHistory[(HrtfOffset-c)&HRTF_LENGTH_MASK] * \
                               HrtfCoeffs[c][0];                              \
            PendingClicks[FRONT_RIGHT] +=                                     \
                               HrtfHistory[(HrtfOffset-c)&HRTF_LENGTH_MASK] * \
                               HrtfCoeffs[c][1];                              \
        }                                                                     \
    }                                                                         \
                                                                              \
    for(out = 0;out < Device->NumAuxSends;out++)                              \
    {                                                                         \
        ALfloat  WetSend;                                                     \
        ALfloat *RESTRICT WetBuffer;                                          \
        ALfloat *RESTRICT WetClickRemoval;                                    \
        ALfloat *RESTRICT WetPendingClicks;                                   \
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
        WetSend = Source->Params.Send[out].WetGain;                           \
                                                                              \
        pos = 0;                                                              \
        frac = *DataPosFrac;                                                  \
        OutPos -= BufferSize;                                                 \
                                                                              \
        if(LIKELY(OutPos == 0))                                               \
        {                                                                     \
            value = sampler(data+pos, 1, frac);                               \
            value = lpFilter1PC(WetFilter, 0, value);                         \
                                                                              \
            WetClickRemoval[0] -= value*WetSend;                              \
        }                                                                     \
        for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                 \
        {                                                                     \
            /* First order interpolator */                                    \
            value = sampler(data+pos, 1, frac);                               \
            value = lpFilter1P(WetFilter, 0, value);                          \
                                                                              \
            /* Room path final mix buffer and panning */                      \
            WetBuffer[OutPos] += value*WetSend;                               \
                                                                              \
            frac += increment;                                                \
            pos  += frac>>FRACTIONBITS;                                       \
            frac &= FRACTIONMASK;                                             \
            OutPos++;                                                         \
        }                                                                     \
        if(LIKELY(OutPos == SamplesToDo))                                     \
        {                                                                     \
            value = sampler(data+pos, 1, frac);                               \
            value = lpFilter1PC(WetFilter, 0, value);                         \
                                                                              \
            WetPendingClicks[0] += value*WetSend;                             \
        }                                                                     \
    }                                                                         \
    *DataPosInt += pos;                                                       \
    *DataPosFrac = frac;                                                      \
}

DECL_TEMPLATE(ALfloat, point32)
DECL_TEMPLATE(ALfloat, lerp32)
DECL_TEMPLATE(ALfloat, cubic32)

DECL_TEMPLATE(ALshort, point16)
DECL_TEMPLATE(ALshort, lerp16)
DECL_TEMPLATE(ALshort, cubic16)

DECL_TEMPLATE(ALubyte, point8)
DECL_TEMPLATE(ALubyte, lerp8)
DECL_TEMPLATE(ALubyte, cubic8)

#undef DECL_TEMPLATE


#define DECL_TEMPLATE(T, chnct, sampler)                                      \
static void Mix_Hrtf_##T##_##chnct##_##sampler(                               \
  ALsource *Source, ALCdevice *Device,                                        \
  const T *RESTRICT data, ALuint *DataPosInt, ALuint *DataPosFrac,            \
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)                       \
{                                                                             \
    const ALuint Channels = chnct;                                            \
    const ALfloat scaler = 1.0f/chnct;                                        \
    ALfloat (*RESTRICT DryBuffer)[MAXCHANNELS];                               \
    ALfloat *RESTRICT ClickRemoval, *RESTRICT PendingClicks;                  \
    ALfloat (*RESTRICT HrtfCoeffs)[HRTF_LENGTH][2];                           \
    ALfloat (*RESTRICT HrtfHistory)[HRTF_LENGTH];                             \
    ALuint HrtfOffset;                                                        \
    ALuint pos, frac;                                                         \
    FILTER *DryFilter;                                                        \
    ALuint BufferIdx;                                                         \
    ALuint increment;                                                         \
    ALuint i, out, c;                                                         \
    ALfloat value;                                                            \
                                                                              \
    increment = Source->Params.Step;                                          \
                                                                              \
    DryBuffer = Device->DryBuffer;                                            \
    ClickRemoval = Device->ClickRemoval;                                      \
    PendingClicks = Device->PendingClicks;                                    \
    DryFilter = &Source->Params.iirFilter;                                    \
                                                                              \
    HrtfCoeffs = Source->Params.HrtfCoeffs;                                   \
    HrtfHistory = Source->HrtfHistory;                                        \
    HrtfOffset = Source->HrtfOffset + OutPos;                                 \
                                                                              \
    pos = 0;                                                                  \
    frac = *DataPosFrac;                                                      \
                                                                              \
    if(LIKELY(OutPos == 0))                                                   \
    {                                                                         \
        for(i = 0;i < Channels;i++)                                           \
        {                                                                     \
            value = sampler(data + pos*Channels + i, Channels, frac);         \
            value = lpFilter2PC(DryFilter, i*2, value);                       \
                                                                              \
            HrtfHistory[i][HrtfOffset&HRTF_LENGTH_MASK] = value;              \
            for(c = 0;c < HRTF_LENGTH;c++)                                    \
            {                                                                 \
                ClickRemoval[FRONT_LEFT] -=                                   \
                            HrtfHistory[i][(HrtfOffset-c)&HRTF_LENGTH_MASK] * \
                            HrtfCoeffs[i][c][0];                              \
                ClickRemoval[FRONT_RIGHT] -=                                  \
                            HrtfHistory[i][(HrtfOffset-c)&HRTF_LENGTH_MASK] * \
                            HrtfCoeffs[i][c][1];                              \
            }                                                                 \
        }                                                                     \
    }                                                                         \
    for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                     \
    {                                                                         \
        for(i = 0;i < Channels;i++)                                           \
        {                                                                     \
            value = sampler(data + pos*Channels + i, Channels, frac);         \
            value = lpFilter2P(DryFilter, i*2, value);                        \
                                                                              \
            HrtfHistory[i][HrtfOffset&HRTF_LENGTH_MASK] = value;              \
            for(c = 0;c < HRTF_LENGTH;c++)                                    \
            {                                                                 \
                DryBuffer[OutPos][FRONT_LEFT] +=                              \
                            HrtfHistory[i][(HrtfOffset-c)&HRTF_LENGTH_MASK] * \
                            HrtfCoeffs[i][c][0];                              \
                DryBuffer[OutPos][FRONT_RIGHT] +=                             \
                            HrtfHistory[i][(HrtfOffset-c)&HRTF_LENGTH_MASK] * \
                            HrtfCoeffs[i][c][1];                              \
            }                                                                 \
        }                                                                     \
        HrtfOffset++;                                                         \
                                                                              \
        frac += increment;                                                    \
        pos  += frac>>FRACTIONBITS;                                           \
        frac &= FRACTIONMASK;                                                 \
        OutPos++;                                                             \
    }                                                                         \
    if(LIKELY(OutPos == SamplesToDo))                                         \
    {                                                                         \
        for(i = 0;i < Channels;i++)                                           \
        {                                                                     \
            value = sampler(data + pos*Channels + i, Channels, frac);         \
            value = lpFilter2PC(DryFilter, i*2, value);                       \
                                                                              \
            HrtfHistory[i][HrtfOffset&HRTF_LENGTH_MASK] = value;              \
            for(c = 0;c < HRTF_LENGTH;c++)                                    \
            {                                                                 \
                PendingClicks[FRONT_LEFT] +=                                  \
                            HrtfHistory[i][(HrtfOffset-c)&HRTF_LENGTH_MASK] * \
                            HrtfCoeffs[i][c][0];                              \
                PendingClicks[FRONT_RIGHT] +=                                 \
                            HrtfHistory[i][(HrtfOffset-c)&HRTF_LENGTH_MASK] * \
                            HrtfCoeffs[i][c][1];                              \
            }                                                                 \
        }                                                                     \
    }                                                                         \
                                                                              \
    for(out = 0;out < Device->NumAuxSends;out++)                              \
    {                                                                         \
        ALfloat  WetSend;                                                     \
        ALfloat *RESTRICT WetBuffer;                                          \
        ALfloat *RESTRICT WetClickRemoval;                                    \
        ALfloat *RESTRICT WetPendingClicks;                                   \
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
        WetSend = Source->Params.Send[out].WetGain;                           \
                                                                              \
        pos = 0;                                                              \
        frac = *DataPosFrac;                                                  \
        OutPos -= BufferSize;                                                 \
                                                                              \
        if(LIKELY(OutPos == 0))                                               \
        {                                                                     \
            for(i = 0;i < Channels;i++)                                       \
            {                                                                 \
                value = sampler(data + pos*Channels + i, Channels, frac);     \
                value = lpFilter1PC(WetFilter, i, value);                     \
                                                                              \
                WetClickRemoval[0] -= value*WetSend * scaler;                 \
            }                                                                 \
        }                                                                     \
        for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                 \
        {                                                                     \
            for(i = 0;i < Channels;i++)                                       \
            {                                                                 \
                value = sampler(data + pos*Channels + i, Channels, frac);     \
                value = lpFilter1P(WetFilter, i, value);                      \
                                                                              \
                WetBuffer[OutPos] += value*WetSend * scaler;                  \
            }                                                                 \
                                                                              \
            frac += increment;                                                \
            pos  += frac>>FRACTIONBITS;                                       \
            frac &= FRACTIONMASK;                                             \
            OutPos++;                                                         \
        }                                                                     \
        if(LIKELY(OutPos == SamplesToDo))                                     \
        {                                                                     \
            for(i = 0;i < Channels;i++)                                       \
            {                                                                 \
                value = sampler(data + pos*Channels + i, Channels, frac);     \
                value = lpFilter1PC(WetFilter, i, value);                     \
                                                                              \
                WetPendingClicks[0] += value*WetSend * scaler;                \
            }                                                                 \
        }                                                                     \
    }                                                                         \
    *DataPosInt += pos;                                                       \
    *DataPosFrac = frac;                                                      \
}

DECL_TEMPLATE(ALfloat, 2, point32)
DECL_TEMPLATE(ALfloat, 2, lerp32)
DECL_TEMPLATE(ALfloat, 2, cubic32)

DECL_TEMPLATE(ALshort, 2, point16)
DECL_TEMPLATE(ALshort, 2, lerp16)
DECL_TEMPLATE(ALshort, 2, cubic16)

DECL_TEMPLATE(ALubyte, 2, point8)
DECL_TEMPLATE(ALubyte, 2, lerp8)
DECL_TEMPLATE(ALubyte, 2, cubic8)


DECL_TEMPLATE(ALfloat, 4, point32)
DECL_TEMPLATE(ALfloat, 4, lerp32)
DECL_TEMPLATE(ALfloat, 4, cubic32)

DECL_TEMPLATE(ALshort, 4, point16)
DECL_TEMPLATE(ALshort, 4, lerp16)
DECL_TEMPLATE(ALshort, 4, cubic16)

DECL_TEMPLATE(ALubyte, 4, point8)
DECL_TEMPLATE(ALubyte, 4, lerp8)
DECL_TEMPLATE(ALubyte, 4, cubic8)


DECL_TEMPLATE(ALfloat, 6, point32)
DECL_TEMPLATE(ALfloat, 6, lerp32)
DECL_TEMPLATE(ALfloat, 6, cubic32)

DECL_TEMPLATE(ALshort, 6, point16)
DECL_TEMPLATE(ALshort, 6, lerp16)
DECL_TEMPLATE(ALshort, 6, cubic16)

DECL_TEMPLATE(ALubyte, 6, point8)
DECL_TEMPLATE(ALubyte, 6, lerp8)
DECL_TEMPLATE(ALubyte, 6, cubic8)


DECL_TEMPLATE(ALfloat, 7, point32)
DECL_TEMPLATE(ALfloat, 7, lerp32)
DECL_TEMPLATE(ALfloat, 7, cubic32)

DECL_TEMPLATE(ALshort, 7, point16)
DECL_TEMPLATE(ALshort, 7, lerp16)
DECL_TEMPLATE(ALshort, 7, cubic16)

DECL_TEMPLATE(ALubyte, 7, point8)
DECL_TEMPLATE(ALubyte, 7, lerp8)
DECL_TEMPLATE(ALubyte, 7, cubic8)


DECL_TEMPLATE(ALfloat, 8, point32)
DECL_TEMPLATE(ALfloat, 8, lerp32)
DECL_TEMPLATE(ALfloat, 8, cubic32)

DECL_TEMPLATE(ALshort, 8, point16)
DECL_TEMPLATE(ALshort, 8, lerp16)
DECL_TEMPLATE(ALshort, 8, cubic16)

DECL_TEMPLATE(ALubyte, 8, point8)
DECL_TEMPLATE(ALubyte, 8, lerp8)
DECL_TEMPLATE(ALubyte, 8, cubic8)

#undef DECL_TEMPLATE


#define DECL_TEMPLATE(T, sampler)                                             \
static void Mix_##T##_1_##sampler(ALsource *Source, ALCdevice *Device,        \
  const T *RESTRICT data, ALuint *DataPosInt, ALuint *DataPosFrac,            \
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)                       \
{                                                                             \
    ALfloat (*RESTRICT DryBuffer)[MAXCHANNELS];                               \
    ALfloat *RESTRICT ClickRemoval, *RESTRICT PendingClicks;                  \
    ALuint pos, frac;                                                         \
    ALfloat DrySend[MAXCHANNELS];                                             \
    FILTER *DryFilter;                                                        \
    ALuint BufferIdx;                                                         \
    ALuint increment;                                                         \
    ALuint out, c;                                                            \
    ALfloat value;                                                            \
                                                                              \
    increment = Source->Params.Step;                                          \
                                                                              \
    DryBuffer = Device->DryBuffer;                                            \
    ClickRemoval = Device->ClickRemoval;                                      \
    PendingClicks = Device->PendingClicks;                                    \
    DryFilter = &Source->Params.iirFilter;                                    \
    for(c = 0;c < MAXCHANNELS;c++)                                            \
        DrySend[c] = Source->Params.DryGains[0][c];                           \
                                                                              \
    pos = 0;                                                                  \
    frac = *DataPosFrac;                                                      \
                                                                              \
    if(OutPos == 0)                                                           \
    {                                                                         \
        value = sampler(data+pos, 1, frac);                                   \
                                                                              \
        value = lpFilter2PC(DryFilter, 0, value);                             \
        for(c = 0;c < MAXCHANNELS;c++)                                        \
            ClickRemoval[c] -= value*DrySend[c];                              \
    }                                                                         \
    for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                     \
    {                                                                         \
        /* First order interpolator */                                        \
        value = sampler(data+pos, 1, frac);                                   \
                                                                              \
        /* Direct path final mix buffer and panning */                        \
        value = lpFilter2P(DryFilter, 0, value);                              \
        for(c = 0;c < MAXCHANNELS;c++)                                        \
            DryBuffer[OutPos][c] += value*DrySend[c];                         \
                                                                              \
        frac += increment;                                                    \
        pos  += frac>>FRACTIONBITS;                                           \
        frac &= FRACTIONMASK;                                                 \
        OutPos++;                                                             \
    }                                                                         \
    if(OutPos == SamplesToDo)                                                 \
    {                                                                         \
        value = sampler(data+pos, 1, frac);                                   \
                                                                              \
        value = lpFilter2PC(DryFilter, 0, value);                             \
        for(c = 0;c < MAXCHANNELS;c++)                                        \
            PendingClicks[c] += value*DrySend[c];                             \
    }                                                                         \
                                                                              \
    for(out = 0;out < Device->NumAuxSends;out++)                              \
    {                                                                         \
        ALfloat  WetSend;                                                     \
        ALfloat *RESTRICT WetBuffer;                                          \
        ALfloat *RESTRICT WetClickRemoval;                                    \
        ALfloat *RESTRICT WetPendingClicks;                                   \
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
        WetSend = Source->Params.Send[out].WetGain;                           \
                                                                              \
        pos = 0;                                                              \
        frac = *DataPosFrac;                                                  \
        OutPos -= BufferSize;                                                 \
                                                                              \
        if(OutPos == 0)                                                       \
        {                                                                     \
            value = sampler(data+pos, 1, frac);                               \
                                                                              \
            value = lpFilter1PC(WetFilter, 0, value);                         \
            WetClickRemoval[0] -= value*WetSend;                              \
        }                                                                     \
        for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                 \
        {                                                                     \
            /* First order interpolator */                                    \
            value = sampler(data+pos, 1, frac);                               \
                                                                              \
            /* Room path final mix buffer and panning */                      \
            value = lpFilter1P(WetFilter, 0, value);                          \
            WetBuffer[OutPos] += value*WetSend;                               \
                                                                              \
            frac += increment;                                                \
            pos  += frac>>FRACTIONBITS;                                       \
            frac &= FRACTIONMASK;                                             \
            OutPos++;                                                         \
        }                                                                     \
        if(OutPos == SamplesToDo)                                             \
        {                                                                     \
            value = sampler(data+pos, 1, frac);                               \
                                                                              \
            value = lpFilter1PC(WetFilter, 0, value);                         \
            WetPendingClicks[0] += value*WetSend;                             \
        }                                                                     \
    }                                                                         \
    *DataPosInt += pos;                                                       \
    *DataPosFrac = frac;                                                      \
}

DECL_TEMPLATE(ALfloat, point32)
DECL_TEMPLATE(ALfloat, lerp32)
DECL_TEMPLATE(ALfloat, cubic32)

DECL_TEMPLATE(ALshort, point16)
DECL_TEMPLATE(ALshort, lerp16)
DECL_TEMPLATE(ALshort, cubic16)

DECL_TEMPLATE(ALubyte, point8)
DECL_TEMPLATE(ALubyte, lerp8)
DECL_TEMPLATE(ALubyte, cubic8)

#undef DECL_TEMPLATE


#define DECL_TEMPLATE(T, chnct, sampler)                                      \
static void Mix_##T##_##chnct##_##sampler(ALsource *Source, ALCdevice *Device,\
  const T *RESTRICT data, ALuint *DataPosInt, ALuint *DataPosFrac,            \
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)                       \
{                                                                             \
    const ALuint Channels = chnct;                                            \
    const ALfloat scaler = 1.0f/chnct;                                        \
    ALfloat (*DryBuffer)[MAXCHANNELS];                                        \
    ALfloat *ClickRemoval, *PendingClicks;                                    \
    ALuint pos, frac;                                                         \
    ALfloat DrySend[chnct][MAXCHANNELS];                                      \
    FILTER *DryFilter;                                                        \
    ALuint BufferIdx;                                                         \
    ALuint increment;                                                         \
    ALuint i, out, c;                                                         \
    ALfloat value;                                                            \
                                                                              \
    increment = Source->Params.Step;                                          \
                                                                              \
    DryBuffer = Device->DryBuffer;                                            \
    ClickRemoval = Device->ClickRemoval;                                      \
    PendingClicks = Device->PendingClicks;                                    \
    DryFilter = &Source->Params.iirFilter;                                    \
    for(i = 0;i < Channels;i++)                                               \
    {                                                                         \
        for(c = 0;c < MAXCHANNELS;c++)                                        \
            DrySend[i][c] = Source->Params.DryGains[i][c];                    \
    }                                                                         \
                                                                              \
    pos = 0;                                                                  \
    frac = *DataPosFrac;                                                      \
                                                                              \
    if(OutPos == 0)                                                           \
    {                                                                         \
        for(i = 0;i < Channels;i++)                                           \
        {                                                                     \
            value = sampler(data + pos*Channels + i, Channels, frac);         \
                                                                              \
            value = lpFilter2PC(DryFilter, i*2, value);                       \
            for(c = 0;c < MAXCHANNELS;c++)                                    \
                ClickRemoval[c] -= value*DrySend[i][c];                       \
        }                                                                     \
    }                                                                         \
    for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                     \
    {                                                                         \
        for(i = 0;i < Channels;i++)                                           \
        {                                                                     \
            value = sampler(data + pos*Channels + i, Channels, frac);         \
                                                                              \
            value = lpFilter2P(DryFilter, i*2, value);                        \
            for(c = 0;c < MAXCHANNELS;c++)                                    \
                DryBuffer[OutPos][c] += value*DrySend[i][c];                  \
        }                                                                     \
                                                                              \
        frac += increment;                                                    \
        pos  += frac>>FRACTIONBITS;                                           \
        frac &= FRACTIONMASK;                                                 \
        OutPos++;                                                             \
    }                                                                         \
    if(OutPos == SamplesToDo)                                                 \
    {                                                                         \
        for(i = 0;i < Channels;i++)                                           \
        {                                                                     \
            value = sampler(data + pos*Channels + i, Channels, frac);         \
                                                                              \
            value = lpFilter2PC(DryFilter, i*2, value);                       \
            for(c = 0;c < MAXCHANNELS;c++)                                    \
                PendingClicks[c] += value*DrySend[i][c];                      \
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
        WetSend = Source->Params.Send[out].WetGain;                           \
                                                                              \
        pos = 0;                                                              \
        frac = *DataPosFrac;                                                  \
        OutPos -= BufferSize;                                                 \
                                                                              \
        if(OutPos == 0)                                                       \
        {                                                                     \
            for(i = 0;i < Channels;i++)                                       \
            {                                                                 \
                value = sampler(data + pos*Channels + i, Channels, frac);     \
                                                                              \
                value = lpFilter1PC(WetFilter, i, value);                     \
                WetClickRemoval[0] -= value*WetSend * scaler;                 \
            }                                                                 \
        }                                                                     \
        for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                 \
        {                                                                     \
            for(i = 0;i < Channels;i++)                                       \
            {                                                                 \
                value = sampler(data + pos*Channels + i, Channels, frac);     \
                                                                              \
                value = lpFilter1P(WetFilter, i, value);                      \
                WetBuffer[OutPos] += value*WetSend * scaler;                  \
            }                                                                 \
                                                                              \
            frac += increment;                                                \
            pos  += frac>>FRACTIONBITS;                                       \
            frac &= FRACTIONMASK;                                             \
            OutPos++;                                                         \
        }                                                                     \
        if(OutPos == SamplesToDo)                                             \
        {                                                                     \
            for(i = 0;i < Channels;i++)                                       \
            {                                                                 \
                value = sampler(data + pos*Channels + i, Channels, frac);     \
                                                                              \
                value = lpFilter1PC(WetFilter, i, value);                     \
                WetPendingClicks[0] += value*WetSend * scaler;                \
            }                                                                 \
        }                                                                     \
    }                                                                         \
    *DataPosInt += pos;                                                       \
    *DataPosFrac = frac;                                                      \
}

DECL_TEMPLATE(ALfloat, 2, point32)
DECL_TEMPLATE(ALfloat, 2, lerp32)
DECL_TEMPLATE(ALfloat, 2, cubic32)

DECL_TEMPLATE(ALshort, 2, point16)
DECL_TEMPLATE(ALshort, 2, lerp16)
DECL_TEMPLATE(ALshort, 2, cubic16)

DECL_TEMPLATE(ALubyte, 2, point8)
DECL_TEMPLATE(ALubyte, 2, lerp8)
DECL_TEMPLATE(ALubyte, 2, cubic8)


DECL_TEMPLATE(ALfloat, 4, point32)
DECL_TEMPLATE(ALfloat, 4, lerp32)
DECL_TEMPLATE(ALfloat, 4, cubic32)

DECL_TEMPLATE(ALshort, 4, point16)
DECL_TEMPLATE(ALshort, 4, lerp16)
DECL_TEMPLATE(ALshort, 4, cubic16)

DECL_TEMPLATE(ALubyte, 4, point8)
DECL_TEMPLATE(ALubyte, 4, lerp8)
DECL_TEMPLATE(ALubyte, 4, cubic8)


DECL_TEMPLATE(ALfloat, 6, point32)
DECL_TEMPLATE(ALfloat, 6, lerp32)
DECL_TEMPLATE(ALfloat, 6, cubic32)

DECL_TEMPLATE(ALshort, 6, point16)
DECL_TEMPLATE(ALshort, 6, lerp16)
DECL_TEMPLATE(ALshort, 6, cubic16)

DECL_TEMPLATE(ALubyte, 6, point8)
DECL_TEMPLATE(ALubyte, 6, lerp8)
DECL_TEMPLATE(ALubyte, 6, cubic8)


DECL_TEMPLATE(ALfloat, 7, point32)
DECL_TEMPLATE(ALfloat, 7, lerp32)
DECL_TEMPLATE(ALfloat, 7, cubic32)

DECL_TEMPLATE(ALshort, 7, point16)
DECL_TEMPLATE(ALshort, 7, lerp16)
DECL_TEMPLATE(ALshort, 7, cubic16)

DECL_TEMPLATE(ALubyte, 7, point8)
DECL_TEMPLATE(ALubyte, 7, lerp8)
DECL_TEMPLATE(ALubyte, 7, cubic8)


DECL_TEMPLATE(ALfloat, 8, point32)
DECL_TEMPLATE(ALfloat, 8, lerp32)
DECL_TEMPLATE(ALfloat, 8, cubic32)

DECL_TEMPLATE(ALshort, 8, point16)
DECL_TEMPLATE(ALshort, 8, lerp16)
DECL_TEMPLATE(ALshort, 8, cubic16)

DECL_TEMPLATE(ALubyte, 8, point8)
DECL_TEMPLATE(ALubyte, 8, lerp8)
DECL_TEMPLATE(ALubyte, 8, cubic8)

#undef DECL_TEMPLATE


#define DECL_TEMPLATE(T, sampler)                                             \
static void Mix_##T##_##sampler(ALsource *Source, ALCdevice *Device,          \
  enum FmtChannels FmtChannels,                                               \
  const ALvoid *RESTRICT Data, ALuint *DataPosInt, ALuint *DataPosFrac,       \
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)                       \
{                                                                             \
    switch(FmtChannels)                                                       \
    {                                                                         \
    case FmtMono:                                                             \
        if((Device->Flags&DEVICE_USE_HRTF))                                   \
            Mix_Hrtf_##T##_1_##sampler(Source, Device, Data,                  \
                                       DataPosInt, DataPosFrac,               \
                                       OutPos, SamplesToDo, BufferSize);      \
        else                                                                  \
            Mix_##T##_1_##sampler(Source, Device, Data,                       \
                                  DataPosInt, DataPosFrac,                    \
                                  OutPos, SamplesToDo, BufferSize);           \
        break;                                                                \
    case FmtStereo:                                                           \
    case FmtRear:                                                             \
        if((Device->Flags&DEVICE_USE_HRTF))                                   \
            Mix_Hrtf_##T##_2_##sampler(Source, Device, Data,                  \
                                       DataPosInt, DataPosFrac,               \
                                       OutPos, SamplesToDo, BufferSize);      \
        else                                                                  \
            Mix_##T##_2_##sampler(Source, Device, Data,                       \
                                  DataPosInt, DataPosFrac,                    \
                                  OutPos, SamplesToDo, BufferSize);           \
        break;                                                                \
    case FmtQuad:                                                             \
        if((Device->Flags&DEVICE_USE_HRTF))                                   \
            Mix_Hrtf_##T##_4_##sampler(Source, Device, Data,                  \
                                       DataPosInt, DataPosFrac,               \
                                       OutPos, SamplesToDo, BufferSize);      \
        else                                                                  \
            Mix_##T##_4_##sampler(Source, Device, Data,                       \
                                  DataPosInt, DataPosFrac,                    \
                                  OutPos, SamplesToDo, BufferSize);           \
        break;                                                                \
    case FmtX51:                                                              \
        if((Device->Flags&DEVICE_USE_HRTF))                                   \
            Mix_Hrtf_##T##_6_##sampler(Source, Device, Data,                  \
                                       DataPosInt, DataPosFrac,               \
                                       OutPos, SamplesToDo, BufferSize);      \
        else                                                                  \
            Mix_##T##_6_##sampler(Source, Device, Data,                       \
                                  DataPosInt, DataPosFrac,                    \
                                  OutPos, SamplesToDo, BufferSize);           \
        break;                                                                \
    case FmtX61:                                                              \
        if((Device->Flags&DEVICE_USE_HRTF))                                   \
            Mix_Hrtf_##T##_7_##sampler(Source, Device, Data,                  \
                                       DataPosInt, DataPosFrac,               \
                                       OutPos, SamplesToDo, BufferSize);      \
        else                                                                  \
            Mix_##T##_7_##sampler(Source, Device, Data,                       \
                                  DataPosInt, DataPosFrac,                    \
                                  OutPos, SamplesToDo, BufferSize);           \
        break;                                                                \
    case FmtX71:                                                              \
        if((Device->Flags&DEVICE_USE_HRTF))                                   \
            Mix_Hrtf_##T##_8_##sampler(Source, Device, Data,                  \
                                       DataPosInt, DataPosFrac,               \
                                       OutPos, SamplesToDo, BufferSize);      \
        else                                                                  \
            Mix_##T##_8_##sampler(Source, Device, Data,                       \
                                  DataPosInt, DataPosFrac,                    \
                                  OutPos, SamplesToDo, BufferSize);           \
        break;                                                                \
    }                                                                         \
}

DECL_TEMPLATE(ALfloat, point32)
DECL_TEMPLATE(ALfloat, lerp32)
DECL_TEMPLATE(ALfloat, cubic32)

DECL_TEMPLATE(ALshort, point16)
DECL_TEMPLATE(ALshort, lerp16)
DECL_TEMPLATE(ALshort, cubic16)

DECL_TEMPLATE(ALubyte, point8)
DECL_TEMPLATE(ALubyte, lerp8)
DECL_TEMPLATE(ALubyte, cubic8)

#undef DECL_TEMPLATE


#define DECL_TEMPLATE(sampler)                                                \
static void Mix_##sampler(ALsource *Source, ALCdevice *Device,                \
  enum FmtChannels FmtChannels, enum FmtType FmtType,                         \
  const ALvoid *RESTRICT Data, ALuint *DataPosInt, ALuint *DataPosFrac,       \
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)                       \
{                                                                             \
    switch(FmtType)                                                           \
    {                                                                         \
    case FmtUByte:                                                            \
        Mix_ALubyte_##sampler##8(Source, Device, FmtChannels,                 \
                                 Data, DataPosInt, DataPosFrac,               \
                                 OutPos, SamplesToDo, BufferSize);            \
        break;                                                                \
                                                                              \
    case FmtShort:                                                            \
        Mix_ALshort_##sampler##16(Source, Device, FmtChannels,                \
                                  Data, DataPosInt, DataPosFrac,              \
                                  OutPos, SamplesToDo, BufferSize);           \
        break;                                                                \
                                                                              \
    case FmtFloat:                                                            \
        Mix_ALfloat_##sampler##32(Source, Device, FmtChannels,                \
                                  Data, DataPosInt, DataPosFrac,              \
                                  OutPos, SamplesToDo, BufferSize);           \
        break;                                                                \
    }                                                                         \
}

DECL_TEMPLATE(point)
DECL_TEMPLATE(lerp)
DECL_TEMPLATE(cubic)

#undef DECL_TEMPLATE


ALvoid MixSource(ALsource *Source, ALCdevice *Device, ALuint SamplesToDo)
{
    ALbufferlistitem *BufferListItem;
    ALuint DataPosInt, DataPosFrac;
    enum FmtChannels FmtChannels;
    enum FmtType FmtType;
    ALuint BuffersPlayed;
    ALboolean Looping;
    ALuint increment;
    resampler_t Resampler;
    ALenum State;
    ALuint OutPos;
    ALuint FrameSize;
    ALint64 DataSize64;
    ALuint i;

    /* Get source info */
    State         = Source->state;
    BuffersPlayed = Source->BuffersPlayed;
    DataPosInt    = Source->position;
    DataPosFrac   = Source->position_fraction;
    Looping       = Source->bLooping;
    increment     = Source->Params.Step;
    Resampler     = (increment == FRACTIONONE) ? POINT_RESAMPLER :
                                                 Source->Resampler;

    /* Get buffer info */
    FrameSize = 0;
    FmtChannels = FmtMono;
    FmtType = FmtUByte;
    BufferListItem = Source->queue;
    for(i = 0;i < Source->BuffersInQueue;i++)
    {
        const ALbuffer *ALBuffer;
        if((ALBuffer=BufferListItem->buffer) != NULL)
        {
            FmtChannels = ALBuffer->FmtChannels;
            FmtType = ALBuffer->FmtType;
            FrameSize = FrameSizeFromFmt(FmtChannels, FmtType);
            break;
        }
        BufferListItem = BufferListItem->next;
    }

    /* Get current buffer queue item */
    BufferListItem = Source->queue;
    for(i = 0;i < BuffersPlayed;i++)
        BufferListItem = BufferListItem->next;

    OutPos = 0;
    do {
        const ALuint BufferPrePadding = ResamplerPrePadding[Resampler];
        const ALuint BufferPadding = ResamplerPadding[Resampler];
        ALubyte StackData[STACK_DATA_SIZE];
        ALubyte *SrcData = StackData;
        ALuint SrcDataSize = 0;
        ALuint BufferSize;

        /* Figure out how many buffer bytes will be needed */
        DataSize64  = SamplesToDo-OutPos+1;
        DataSize64 *= increment;
        DataSize64 += DataPosFrac+FRACTIONMASK;
        DataSize64 >>= FRACTIONBITS;
        DataSize64 += BufferPadding+BufferPrePadding;
        DataSize64 *= FrameSize;

        BufferSize = min(DataSize64, STACK_DATA_SIZE);
        BufferSize -= BufferSize%FrameSize;

        if(Source->lSourceType == AL_STATIC)
        {
            const ALbuffer *ALBuffer = Source->Buffer;
            const ALubyte *Data = ALBuffer->data;
            ALuint DataSize;
            ALuint pos;

            /* If current pos is beyond the loop range, do not loop */
            if(Looping == AL_FALSE || DataPosInt >= (ALuint)ALBuffer->LoopEnd)
            {
                Looping = AL_FALSE;

                if(DataPosInt >= BufferPrePadding)
                    pos = (DataPosInt-BufferPrePadding)*FrameSize;
                else
                {
                    DataSize = (BufferPrePadding-DataPosInt)*FrameSize;
                    DataSize = min(BufferSize, DataSize);

                    memset(&SrcData[SrcDataSize], (FmtType==FmtUByte)?0x80:0, DataSize);
                    SrcDataSize += DataSize;
                    BufferSize -= DataSize;

                    pos = 0;
                }

                /* Copy what's left to play in the source buffer, and clear the
                 * rest of the temp buffer */
                DataSize = ALBuffer->size - pos;
                DataSize = min(BufferSize, DataSize);

                memcpy(&SrcData[SrcDataSize], &Data[pos], DataSize);
                SrcDataSize += DataSize;
                BufferSize -= DataSize;

                memset(&SrcData[SrcDataSize], (FmtType==FmtUByte)?0x80:0, BufferSize);
                SrcDataSize += BufferSize;
                BufferSize -= BufferSize;
            }
            else
            {
                ALuint LoopStart = ALBuffer->LoopStart;
                ALuint LoopEnd   = ALBuffer->LoopEnd;

                if(DataPosInt >= LoopStart)
                {
                    pos = DataPosInt-LoopStart;
                    while(pos < BufferPrePadding)
                        pos += LoopEnd-LoopStart;
                    pos -= BufferPrePadding;
                    pos += LoopStart;
                    pos *= FrameSize;
                }
                else if(DataPosInt >= BufferPrePadding)
                    pos = (DataPosInt-BufferPrePadding)*FrameSize;
                else
                {
                    DataSize = (BufferPrePadding-DataPosInt)*FrameSize;
                    DataSize = min(BufferSize, DataSize);

                    memset(&SrcData[SrcDataSize], (FmtType==FmtUByte)?0x80:0, DataSize);
                    SrcDataSize += DataSize;
                    BufferSize -= DataSize;

                    pos = 0;
                }

                /* Copy what's left of this loop iteration, then copy repeats
                 * of the loop section */
                DataSize = LoopEnd*FrameSize - pos;
                DataSize = min(BufferSize, DataSize);

                memcpy(&SrcData[SrcDataSize], &Data[pos], DataSize);
                SrcDataSize += DataSize;
                BufferSize -= DataSize;

                DataSize = (LoopEnd-LoopStart) * FrameSize;
                while(BufferSize > 0)
                {
                    DataSize = min(BufferSize, DataSize);

                    memcpy(&SrcData[SrcDataSize], &Data[LoopStart*FrameSize], DataSize);
                    SrcDataSize += DataSize;
                    BufferSize -= DataSize;
                }
            }
        }
        else
        {
            /* Crawl the buffer queue to fill in the temp buffer */
            ALbufferlistitem *BufferListIter = BufferListItem;
            ALuint pos;

            if(DataPosInt >= BufferPrePadding)
                pos = (DataPosInt-BufferPrePadding)*FrameSize;
            else
            {
                pos = (BufferPrePadding-DataPosInt)*FrameSize;
                while(pos > 0)
                {
                    if(!BufferListIter->prev && !Looping)
                    {
                        ALuint DataSize = min(BufferSize, pos);

                        memset(&SrcData[SrcDataSize], (FmtType==FmtUByte)?0x80:0, DataSize);
                        SrcDataSize += DataSize;
                        BufferSize -= DataSize;

                        pos = 0;
                        break;
                    }

                    if(BufferListIter->prev)
                        BufferListIter = BufferListIter->prev;
                    else
                    {
                        while(BufferListIter->next)
                            BufferListIter = BufferListIter->next;
                    }

                    if(BufferListIter->buffer)
                    {
                        if((ALuint)BufferListIter->buffer->size > pos)
                        {
                            pos = BufferListIter->buffer->size - pos;
                            break;
                        }
                        pos -= BufferListIter->buffer->size;
                    }
                }
            }

            while(BufferListIter && BufferSize > 0)
            {
                const ALbuffer *ALBuffer;
                if((ALBuffer=BufferListIter->buffer) != NULL)
                {
                    const ALubyte *Data = ALBuffer->data;
                    ALuint DataSize = ALBuffer->size;

                    /* Skip the data already played */
                    if(DataSize <= pos)
                        pos -= DataSize;
                    else
                    {
                        Data += pos;
                        DataSize -= pos;
                        pos -= pos;

                        DataSize = min(BufferSize, DataSize);
                        memcpy(&SrcData[SrcDataSize], Data, DataSize);
                        SrcDataSize += DataSize;
                        BufferSize -= DataSize;
                    }
                }
                BufferListIter = BufferListIter->next;
                if(!BufferListIter && Looping)
                    BufferListIter = Source->queue;
                else if(!BufferListIter)
                {
                    memset(&SrcData[SrcDataSize], (FmtType==FmtUByte)?0x80:0, BufferSize);
                    SrcDataSize += BufferSize;
                    BufferSize -= BufferSize;
                }
            }
        }

        /* Figure out how many samples we can mix. */
        DataSize64  = SrcDataSize / FrameSize;
        DataSize64 -= BufferPadding+BufferPrePadding;
        DataSize64 <<= FRACTIONBITS;
        DataSize64 -= increment;
        DataSize64 -= DataPosFrac;

        BufferSize = (ALuint)((DataSize64+(increment-1)) / increment);
        BufferSize = min(BufferSize, (SamplesToDo-OutPos));

        SrcData += BufferPrePadding*FrameSize;
        switch(Resampler)
        {
            case POINT_RESAMPLER:
                Mix_point(Source, Device, FmtChannels, FmtType,
                          SrcData, &DataPosInt, &DataPosFrac,
                          OutPos, SamplesToDo, BufferSize);
                break;
            case LINEAR_RESAMPLER:
                Mix_lerp(Source, Device, FmtChannels, FmtType,
                         SrcData, &DataPosInt, &DataPosFrac,
                         OutPos, SamplesToDo, BufferSize);
                break;
            case CUBIC_RESAMPLER:
                Mix_cubic(Source, Device, FmtChannels, FmtType,
                          SrcData, &DataPosInt, &DataPosFrac,
                          OutPos, SamplesToDo, BufferSize);
                break;
            case RESAMPLER_MIN:
            case RESAMPLER_MAX:
            break;
        }
        OutPos += BufferSize;

        /* Handle looping sources */
        while(1)
        {
            const ALbuffer *ALBuffer;
            ALuint DataSize = 0;
            ALuint LoopStart = 0;
            ALuint LoopEnd = 0;

            if((ALBuffer=BufferListItem->buffer) != NULL)
            {
                DataSize = ALBuffer->size / FrameSize;
                LoopStart = ALBuffer->LoopStart;
                LoopEnd = ALBuffer->LoopEnd;
                if(LoopEnd > DataPosInt)
                    break;
            }

            if(Looping && Source->lSourceType == AL_STATIC)
            {
                BufferListItem = Source->queue;
                DataPosInt = ((DataPosInt-LoopStart)%(LoopEnd-LoopStart)) + LoopStart;
                break;
            }

            if(DataSize > DataPosInt)
                break;

            if(BufferListItem->next)
            {
                BufferListItem = BufferListItem->next;
                BuffersPlayed++;
            }
            else if(Looping)
            {
                BufferListItem = Source->queue;
                BuffersPlayed = 0;
            }
            else
            {
                State = AL_STOPPED;
                BufferListItem = Source->queue;
                BuffersPlayed = Source->BuffersInQueue;
                DataPosInt = 0;
                DataPosFrac = 0;
                break;
            }

            DataPosInt -= DataSize;
        }
    } while(State == AL_PLAYING && OutPos < SamplesToDo);

    /* Update source info */
    Source->state             = State;
    Source->BuffersPlayed     = BuffersPlayed;
    Source->position          = DataPosInt;
    Source->position_fraction = DataPosFrac;
    Source->Buffer            = BufferListItem->buffer;
    Source->HrtfOffset       += OutPos;
}
