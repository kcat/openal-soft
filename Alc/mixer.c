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

static __inline ALdouble point8(const ALbyte *vals, ALint step, ALint frac)
{ return vals[0] * (1.0/127.0); (void)step; (void)frac; }
static __inline ALdouble lerp8(const ALbyte *vals, ALint step, ALint frac)
{ return lerp(vals[0], vals[step], frac * (1.0/FRACTIONONE)) * (1.0/127.0); }
static __inline ALdouble cubic8(const ALbyte *vals, ALint step, ALint frac)
{ return cubic(vals[-step], vals[0], vals[step], vals[step+step],
               frac * (1.0/FRACTIONONE)) * (1.0/127.0); }

#ifdef __GNUC__
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

#define DECL_TEMPLATE(T, sampler)                                             \
static void Mix_Hrtf_##T##_##sampler(ALsource *Source, ALCdevice *Device,     \
  const ALvoid *srcdata, ALuint *DataPosInt, ALuint *DataPosFrac,             \
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)                       \
{                                                                             \
    const ALuint NumChannels = Source->NumChannels;                           \
    const T *RESTRICT data = srcdata;                                         \
    ALfloat (*RESTRICT DryBuffer)[MAXCHANNELS];                               \
    ALfloat *RESTRICT ClickRemoval, *RESTRICT PendingClicks;                  \
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
    pos = 0;                                                                  \
    frac = *DataPosFrac;                                                      \
                                                                              \
    for(i = 0;i < NumChannels;i++)                                            \
    {                                                                         \
        ALfloat (*RESTRICT Coeffs)[2] = Source->Params.HrtfCoeffs[i];         \
        const ALuint *RESTRICT Delay = Source->Params.HrtfDelay[i];           \
        ALfloat *RESTRICT History = Source->HrtfHistory[i];                   \
        ALfloat (*RESTRICT Values)[2] = Source->HrtfValues[i];                \
        ALuint Offset = Source->HrtfOffset + OutPos;                          \
        ALfloat left, right;                                                  \
                                                                              \
        pos = 0;                                                              \
        frac = *DataPosFrac;                                                  \
                                                                              \
        if(LIKELY(OutPos == 0))                                               \
        {                                                                     \
            value = sampler(data + pos*NumChannels + i, NumChannels, frac);   \
            value = lpFilter2PC(DryFilter, i, value);                         \
                                                                              \
            History[Offset&SRC_HISTORY_MASK] = value;                         \
            left = History[(Offset-Delay[0])&SRC_HISTORY_MASK];               \
            right = History[(Offset-Delay[1])&SRC_HISTORY_MASK];              \
                                                                              \
            ClickRemoval[FRONT_LEFT]  -= Values[(Offset+1)&HRIR_MASK][0] +    \
                                         Coeffs[0][0] * left;                 \
            ClickRemoval[FRONT_RIGHT] -= Values[(Offset+1)&HRIR_MASK][1] +    \
                                         Coeffs[0][1] * right;                \
        }                                                                     \
        for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                 \
        {                                                                     \
            value = sampler(data + pos*NumChannels + i, NumChannels, frac);   \
            value = lpFilter2P(DryFilter, i, value);                          \
                                                                              \
            History[Offset&SRC_HISTORY_MASK] = value;                         \
            left = History[(Offset-Delay[0])&SRC_HISTORY_MASK];               \
            right = History[(Offset-Delay[1])&SRC_HISTORY_MASK];              \
                                                                              \
            Values[Offset&HRIR_MASK][0] = 0.0f;                               \
            Values[Offset&HRIR_MASK][1] = 0.0f;                               \
            Offset++;                                                         \
                                                                              \
            for(c = 0;c < HRIR_LENGTH;c++)                                    \
            {                                                                 \
                const ALuint off = (Offset+c)&HRIR_MASK;                      \
                Values[off][0] += Coeffs[c][0] * left;                        \
                Values[off][1] += Coeffs[c][1] * right;                       \
            }                                                                 \
                                                                              \
            DryBuffer[OutPos][FRONT_LEFT]  += Values[Offset&HRIR_MASK][0];    \
            DryBuffer[OutPos][FRONT_RIGHT] += Values[Offset&HRIR_MASK][1];    \
                                                                              \
            frac += increment;                                                \
            pos  += frac>>FRACTIONBITS;                                       \
            frac &= FRACTIONMASK;                                             \
            OutPos++;                                                         \
        }                                                                     \
        if(LIKELY(OutPos == SamplesToDo))                                     \
        {                                                                     \
            value = sampler(data + pos*NumChannels + i, NumChannels, frac);   \
            value = lpFilter2PC(DryFilter, i, value);                         \
                                                                              \
            History[Offset&SRC_HISTORY_MASK] = value;                         \
            left = History[(Offset-Delay[0])&SRC_HISTORY_MASK];               \
            right = History[(Offset-Delay[1])&SRC_HISTORY_MASK];              \
                                                                              \
            PendingClicks[FRONT_LEFT]  += Values[(Offset+1)&HRIR_MASK][0] +   \
                                          Coeffs[0][0] * left;                \
            PendingClicks[FRONT_RIGHT] += Values[(Offset+1)&HRIR_MASK][1] +   \
                                          Coeffs[0][1] * right;               \
        }                                                                     \
        OutPos -= BufferSize;                                                 \
    }                                                                         \
                                                                              \
    for(out = 0;out < Device->NumAuxSends;out++)                              \
    {                                                                         \
        ALeffectslot *Slot = Source->Params.Send[i].Slot;                     \
        ALfloat  WetSend;                                                     \
        ALfloat *RESTRICT WetBuffer;                                          \
        ALfloat *RESTRICT WetClickRemoval;                                    \
        ALfloat *RESTRICT WetPendingClicks;                                   \
        FILTER  *WetFilter;                                                   \
                                                                              \
        if(!Slot || Slot->effect.type == AL_EFFECT_NULL)                      \
            continue;                                                         \
                                                                              \
        WetBuffer = Slot->WetBuffer;                                          \
        WetClickRemoval = Slot->ClickRemoval;                                 \
        WetPendingClicks = Slot->PendingClicks;                               \
        WetFilter = &Source->Params.Send[out].iirFilter;                      \
        WetSend = Source->Params.Send[out].WetGain;                           \
                                                                              \
        for(i = 0;i < NumChannels;i++)                                        \
        {                                                                     \
            pos = 0;                                                          \
            frac = *DataPosFrac;                                              \
                                                                              \
            if(LIKELY(OutPos == 0))                                           \
            {                                                                 \
                value = sampler(data + pos*NumChannels + i, NumChannels,frac);\
                value = lpFilter1PC(WetFilter, i, value);                     \
                                                                              \
                WetClickRemoval[0] -= value * WetSend;                        \
            }                                                                 \
            for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)             \
            {                                                                 \
                value = sampler(data + pos*NumChannels + i, NumChannels,frac);\
                value = lpFilter1P(WetFilter, i, value);                      \
                                                                              \
                WetBuffer[OutPos] += value * WetSend;                         \
                                                                              \
                frac += increment;                                            \
                pos  += frac>>FRACTIONBITS;                                   \
                frac &= FRACTIONMASK;                                         \
                OutPos++;                                                     \
            }                                                                 \
            if(LIKELY(OutPos == SamplesToDo))                                 \
            {                                                                 \
                value = sampler(data + pos*NumChannels + i, NumChannels,frac);\
                value = lpFilter1PC(WetFilter, i, value);                     \
                                                                              \
                WetPendingClicks[0] += value * WetSend;                       \
            }                                                                 \
            OutPos -= BufferSize;                                             \
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

DECL_TEMPLATE(ALbyte, point8)
DECL_TEMPLATE(ALbyte, lerp8)
DECL_TEMPLATE(ALbyte, cubic8)

#undef DECL_TEMPLATE


#define DECL_TEMPLATE(T, sampler)                                             \
static void Mix_##T##_##sampler(ALsource *Source, ALCdevice *Device,          \
  const ALvoid *srcdata, ALuint *DataPosInt, ALuint *DataPosFrac,             \
  ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize)                       \
{                                                                             \
    const ALuint NumChannels = Source->NumChannels;                           \
    const T *RESTRICT data = srcdata;                                         \
    ALfloat (*DryBuffer)[MAXCHANNELS];                                        \
    ALfloat *ClickRemoval, *PendingClicks;                                    \
    ALuint pos, frac;                                                         \
    ALfloat DrySend[MAXCHANNELS][MAXCHANNELS];                                \
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
    for(i = 0;i < NumChannels;i++)                                            \
    {                                                                         \
        for(c = 0;c < MAXCHANNELS;c++)                                        \
            DrySend[i][c] = Source->Params.DryGains[i][c];                    \
    }                                                                         \
                                                                              \
    pos = 0;                                                                  \
    frac = *DataPosFrac;                                                      \
                                                                              \
    for(i = 0;i < NumChannels;i++)                                            \
    {                                                                         \
        pos = 0;                                                              \
        frac = *DataPosFrac;                                                  \
                                                                              \
        if(OutPos == 0)                                                       \
        {                                                                     \
            value = sampler(data + pos*NumChannels + i, NumChannels, frac);   \
                                                                              \
            value = lpFilter2PC(DryFilter, i, value);                         \
            for(c = 0;c < MAXCHANNELS;c++)                                    \
                ClickRemoval[c] -= value*DrySend[i][c];                       \
        }                                                                     \
        for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)                 \
        {                                                                     \
            value = sampler(data + pos*NumChannels + i, NumChannels, frac);   \
                                                                              \
            value = lpFilter2P(DryFilter, i, value);                          \
            for(c = 0;c < MAXCHANNELS;c++)                                    \
                DryBuffer[OutPos][c] += value*DrySend[i][c];                  \
                                                                              \
            frac += increment;                                                \
            pos  += frac>>FRACTIONBITS;                                       \
            frac &= FRACTIONMASK;                                             \
            OutPos++;                                                         \
        }                                                                     \
        if(OutPos == SamplesToDo)                                             \
        {                                                                     \
            value = sampler(data + pos*NumChannels + i, NumChannels, frac);   \
                                                                              \
            value = lpFilter2PC(DryFilter, i, value);                         \
            for(c = 0;c < MAXCHANNELS;c++)                                    \
                PendingClicks[c] += value*DrySend[i][c];                      \
        }                                                                     \
        OutPos -= BufferSize;                                                 \
    }                                                                         \
                                                                              \
    for(out = 0;out < Device->NumAuxSends;out++)                              \
    {                                                                         \
        ALeffectslot *Slot = Source->Params.Send[i].Slot;                     \
        ALfloat  WetSend;                                                     \
        ALfloat *WetBuffer;                                                   \
        ALfloat *WetClickRemoval;                                             \
        ALfloat *WetPendingClicks;                                            \
        FILTER  *WetFilter;                                                   \
                                                                              \
        if(!Slot || Slot->effect.type == AL_EFFECT_NULL)                      \
            continue;                                                         \
                                                                              \
        WetBuffer = Slot->WetBuffer;                                          \
        WetClickRemoval = Slot->ClickRemoval;                                 \
        WetPendingClicks = Slot->PendingClicks;                               \
        WetFilter = &Source->Params.Send[out].iirFilter;                      \
        WetSend = Source->Params.Send[out].WetGain;                           \
                                                                              \
        for(i = 0;i < NumChannels;i++)                                        \
        {                                                                     \
            pos = 0;                                                          \
            frac = *DataPosFrac;                                              \
                                                                              \
            if(OutPos == 0)                                                   \
            {                                                                 \
                value = sampler(data + pos*NumChannels + i, NumChannels,frac);\
                                                                              \
                value = lpFilter1PC(WetFilter, i, value);                     \
                WetClickRemoval[0] -= value * WetSend;                        \
            }                                                                 \
            for(BufferIdx = 0;BufferIdx < BufferSize;BufferIdx++)             \
            {                                                                 \
                value = sampler(data + pos*NumChannels + i, NumChannels,frac);\
                                                                              \
                value = lpFilter1P(WetFilter, i, value);                      \
                WetBuffer[OutPos] += value * WetSend;                         \
                                                                              \
                frac += increment;                                            \
                pos  += frac>>FRACTIONBITS;                                   \
                frac &= FRACTIONMASK;                                         \
                OutPos++;                                                     \
            }                                                                 \
            if(OutPos == SamplesToDo)                                         \
            {                                                                 \
                value = sampler(data + pos*NumChannels + i, NumChannels,frac);\
                                                                              \
                value = lpFilter1PC(WetFilter, i, value);                     \
                WetPendingClicks[0] += value * WetSend;                       \
            }                                                                 \
            OutPos -= BufferSize;                                             \
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

DECL_TEMPLATE(ALbyte, point8)
DECL_TEMPLATE(ALbyte, lerp8)
DECL_TEMPLATE(ALbyte, cubic8)

#undef DECL_TEMPLATE


#define DECL_TEMPLATE(sampler)                                                \
static MixerFunc Select_##sampler(enum FmtType FmtType)                       \
{                                                                             \
    switch(FmtType)                                                           \
    {                                                                         \
    case FmtByte:                                                             \
        return Mix_ALbyte_##sampler##8;                                       \
    case FmtShort:                                                            \
        return Mix_ALshort_##sampler##16;                                     \
    case FmtFloat:                                                            \
        return Mix_ALfloat_##sampler##32;                                     \
    }                                                                         \
    return NULL;                                                              \
}

DECL_TEMPLATE(point)
DECL_TEMPLATE(lerp)
DECL_TEMPLATE(cubic)

#undef DECL_TEMPLATE

MixerFunc SelectMixer(ALbuffer *Buffer, enum Resampler Resampler)
{
    switch(Resampler)
    {
        case POINT_RESAMPLER:
            return Select_point(Buffer->FmtType);
        case LINEAR_RESAMPLER:
            return Select_lerp(Buffer->FmtType);
        case CUBIC_RESAMPLER:
            return Select_cubic(Buffer->FmtType);
        case RESAMPLER_MIN:
        case RESAMPLER_MAX:
            break;
    }
    return NULL;
}

#define DECL_TEMPLATE(sampler)                                                \
static MixerFunc Select_Hrtf_##sampler(enum FmtType FmtType)                  \
{                                                                             \
    switch(FmtType)                                                           \
    {                                                                         \
    case FmtByte:                                                             \
        return Mix_Hrtf_ALbyte_##sampler##8;                                  \
    case FmtShort:                                                            \
        return Mix_Hrtf_ALshort_##sampler##16;                                \
    case FmtFloat:                                                            \
        return Mix_Hrtf_ALfloat_##sampler##32;                                \
    }                                                                         \
    return NULL;                                                              \
}

DECL_TEMPLATE(point)
DECL_TEMPLATE(lerp)
DECL_TEMPLATE(cubic)

#undef DECL_TEMPLATE

MixerFunc SelectHrtfMixer(ALbuffer *Buffer, enum Resampler Resampler)
{
    switch(Resampler)
    {
        case POINT_RESAMPLER:
            return Select_Hrtf_point(Buffer->FmtType);
        case LINEAR_RESAMPLER:
            return Select_Hrtf_lerp(Buffer->FmtType);
        case CUBIC_RESAMPLER:
            return Select_Hrtf_cubic(Buffer->FmtType);
        case RESAMPLER_MIN:
        case RESAMPLER_MAX:
            break;
    }
    return NULL;
}


ALvoid MixSource(ALsource *Source, ALCdevice *Device, ALuint SamplesToDo)
{
    ALbufferlistitem *BufferListItem;
    ALuint DataPosInt, DataPosFrac;
    ALuint BuffersPlayed;
    ALboolean Looping;
    ALuint increment;
    enum Resampler Resampler;
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
    Resampler     = Source->Resampler;
    FrameSize     = Source->NumChannels * Source->SampleSize;

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

                    memset(&SrcData[SrcDataSize], 0, DataSize);
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

                memset(&SrcData[SrcDataSize], 0, BufferSize);
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

                    memset(&SrcData[SrcDataSize], 0, DataSize);
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

                        memset(&SrcData[SrcDataSize], 0, DataSize);
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
                    memset(&SrcData[SrcDataSize], 0, BufferSize);
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
        Source->Params.DoMix(Source, Device, SrcData, &DataPosInt, &DataPosFrac,
                             OutPos, SamplesToDo, BufferSize);
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
