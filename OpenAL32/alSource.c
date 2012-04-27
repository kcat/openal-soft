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

#include <stdlib.h>
#include <math.h>
#include <float.h>
#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"
#include "alError.h"
#include "alSource.h"
#include "alBuffer.h"
#include "alThunk.h"
#include "alAuxEffectSlot.h"


enum Resampler DefaultResampler = LinearResampler;
const ALsizei ResamplerPadding[ResamplerMax] = {
    0, /* Point */
    1, /* Linear */
    2, /* Cubic */
};
const ALsizei ResamplerPrePadding[ResamplerMax] = {
    0, /* Point */
    0, /* Linear */
    1, /* Cubic */
};


static ALvoid InitSourceParams(ALsource *Source);
static ALvoid GetSourceOffsets(ALsource *Source, ALenum name, ALdouble *offsets, ALdouble updateLen);
static ALint GetSampleOffset(ALsource *Source);


AL_API ALvoid AL_APIENTRY alGenSources(ALsizei n, ALuint *sources)
{
    ALCcontext *Context;
    ALsizei    cur = 0;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        ALenum err;

        CHECK_VALUE(Context, n >= 0);
        for(cur = 0;cur < n;cur++)
        {
            ALsource *source = calloc(1, sizeof(ALsource));
            if(!source)
                al_throwerr(Context, AL_OUT_OF_MEMORY);
            InitSourceParams(source);

            err = NewThunkEntry(&source->id);
            if(err == AL_NO_ERROR)
                err = InsertUIntMapEntry(&Context->SourceMap, source->id, source);
            if(err != AL_NO_ERROR)
            {
                FreeThunkEntry(source->id);
                memset(source, 0, sizeof(ALsource));
                free(source);

                al_throwerr(Context, err);
            }

            sources[cur] = source->id;
        }
    }
    al_catchany()
    {
        if(cur > 0)
            alDeleteSources(cur, sources);
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}


AL_API ALvoid AL_APIENTRY alDeleteSources(ALsizei n, const ALuint *sources)
{
    ALCcontext *Context;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        ALbufferlistitem *BufferList;
        ALsource *Source;
        ALsizei i, j;

        CHECK_VALUE(Context, n >= 0);

        /* Check that all Sources are valid */
        for(i = 0;i < n;i++)
        {
            if(LookupSource(Context, sources[i]) == NULL)
                al_throwerr(Context, AL_INVALID_NAME);
        }

        for(i = 0;i < n;i++)
        {
            ALsource **srclist, **srclistend;

            if((Source=RemoveSource(Context, sources[i])) == NULL)
                continue;
            FreeThunkEntry(Source->id);

            LockContext(Context);
            srclist = Context->ActiveSources;
            srclistend = srclist + Context->ActiveSourceCount;
            while(srclist != srclistend)
            {
                if(*srclist == Source)
                {
                    Context->ActiveSourceCount--;
                    *srclist = *(--srclistend);
                    break;
                }
                srclist++;
            }
            UnlockContext(Context);

            while(Source->queue != NULL)
            {
                BufferList = Source->queue;
                Source->queue = BufferList->next;

                if(BufferList->buffer != NULL)
                    DecrementRef(&BufferList->buffer->ref);
                free(BufferList);
            }

            for(j = 0;j < MAX_SENDS;++j)
            {
                if(Source->Send[j].Slot)
                    DecrementRef(&Source->Send[j].Slot->ref);
                Source->Send[j].Slot = NULL;
            }

            memset(Source, 0, sizeof(*Source));
            free(Source);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}


AL_API ALboolean AL_APIENTRY alIsSource(ALuint source)
{
    ALCcontext *Context;
    ALboolean  result;

    Context = GetContextRef();
    if(!Context) return AL_FALSE;

    result = (LookupSource(Context, source) ? AL_TRUE : AL_FALSE);

    ALCcontext_DecRef(Context);

    return result;
}


AL_API ALvoid AL_APIENTRY alSourcef(ALuint source, ALenum param, ALfloat value)
{
    ALCcontext *Context;
    ALsource   *Source;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        if((Source=LookupSource(Context, source)) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        switch(param)
        {
            case AL_PITCH:
                CHECK_VALUE(Context, value >= 0.0f);

                Source->Pitch = value;
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_CONE_INNER_ANGLE:
                CHECK_VALUE(Context, value >= 0.0f && value <= 360.0f);

                Source->InnerAngle = value;
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_CONE_OUTER_ANGLE:
                CHECK_VALUE(Context, value >= 0.0f && value <= 360.0f);

                Source->OuterAngle = value;
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_GAIN:
                CHECK_VALUE(Context, value >= 0.0f);

                Source->Gain = value;
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_MAX_DISTANCE:
                CHECK_VALUE(Context, value >= 0.0f);

                Source->MaxDistance = value;
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_ROLLOFF_FACTOR:
                CHECK_VALUE(Context, value >= 0.0f);

                Source->RollOffFactor = value;
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_REFERENCE_DISTANCE:
                CHECK_VALUE(Context, value >= 0.0f);

                Source->RefDistance = value;
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_MIN_GAIN:
                CHECK_VALUE(Context, value >= 0.0f && value <= 1.0f);

                Source->MinGain = value;
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_MAX_GAIN:
                CHECK_VALUE(Context, value >= 0.0f && value <= 1.0f);

                Source->MaxGain = value;
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_CONE_OUTER_GAIN:
                CHECK_VALUE(Context, value >= 0.0f && value <= 1.0f);

                Source->OuterGain = value;
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_CONE_OUTER_GAINHF:
                CHECK_VALUE(Context, value >= 0.0f && value <= 1.0f);

                Source->OuterGainHF = value;
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_AIR_ABSORPTION_FACTOR:
                CHECK_VALUE(Context, value >= 0.0f && value <= 10.0f);

                Source->AirAbsorptionFactor = value;
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_ROOM_ROLLOFF_FACTOR:
                CHECK_VALUE(Context, value >= 0.0f && value <= 10.0f);

                Source->RoomRolloffFactor = value;
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_DOPPLER_FACTOR:
                CHECK_VALUE(Context, value >= 0.0f && value <= 1.0f);

                Source->DopplerFactor = value;
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_SEC_OFFSET:
            case AL_SAMPLE_OFFSET:
            case AL_BYTE_OFFSET:
                CHECK_VALUE(Context, value >= 0.0f);

                LockContext(Context);
                Source->OffsetType = param;
                Source->Offset = value;

                if((Source->state == AL_PLAYING || Source->state == AL_PAUSED) &&
                   !Context->DeferUpdates)
                {
                    if(ApplyOffset(Source) == AL_FALSE)
                    {
                        UnlockContext(Context);
                        al_throwerr(Context, AL_INVALID_VALUE);
                    }
                }
                UnlockContext(Context);
                break;

            default:
                al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}


AL_API ALvoid AL_APIENTRY alSource3f(ALuint source, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3)
{
    ALCcontext *Context;
    ALsource   *Source;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        if((Source=LookupSource(Context, source)) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        switch(param)
        {
            case AL_POSITION:
                CHECK_VALUE(Context, isfinite(value1) && isfinite(value2) && isfinite(value3));

                LockContext(Context);
                Source->Position[0] = value1;
                Source->Position[1] = value2;
                Source->Position[2] = value3;
                UnlockContext(Context);
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_VELOCITY:
                CHECK_VALUE(Context, isfinite(value1) && isfinite(value2) && isfinite(value3));

                LockContext(Context);
                Source->Velocity[0] = value1;
                Source->Velocity[1] = value2;
                Source->Velocity[2] = value3;
                UnlockContext(Context);
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_DIRECTION:
                CHECK_VALUE(Context, isfinite(value1) && isfinite(value2) && isfinite(value3));

                LockContext(Context);
                Source->Orientation[0] = value1;
                Source->Orientation[1] = value2;
                Source->Orientation[2] = value3;
                UnlockContext(Context);
                Source->NeedsUpdate = AL_TRUE;
                break;

            default:
                al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}


AL_API ALvoid AL_APIENTRY alSourcefv(ALuint source, ALenum param, const ALfloat *values)
{
    ALCcontext *Context;

    if(values)
    {
        switch(param)
        {
            case AL_PITCH:
            case AL_CONE_INNER_ANGLE:
            case AL_CONE_OUTER_ANGLE:
            case AL_GAIN:
            case AL_MAX_DISTANCE:
            case AL_ROLLOFF_FACTOR:
            case AL_REFERENCE_DISTANCE:
            case AL_MIN_GAIN:
            case AL_MAX_GAIN:
            case AL_CONE_OUTER_GAIN:
            case AL_CONE_OUTER_GAINHF:
            case AL_SEC_OFFSET:
            case AL_SAMPLE_OFFSET:
            case AL_BYTE_OFFSET:
            case AL_AIR_ABSORPTION_FACTOR:
            case AL_ROOM_ROLLOFF_FACTOR:
                alSourcef(source, param, values[0]);
                return;

            case AL_POSITION:
            case AL_VELOCITY:
            case AL_DIRECTION:
                alSource3f(source, param, values[0], values[1], values[2]);
                return;
        }
    }

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        if(LookupSource(Context, source) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        CHECK_VALUE(Context, values);

        switch(param)
        {
            default:
                al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}


AL_API ALvoid AL_APIENTRY alSourcei(ALuint source, ALenum param, ALint value)
{
    ALCcontext *Context;
    ALsource   *Source;

    switch(param)
    {
        case AL_MAX_DISTANCE:
        case AL_ROLLOFF_FACTOR:
        case AL_CONE_INNER_ANGLE:
        case AL_CONE_OUTER_ANGLE:
        case AL_REFERENCE_DISTANCE:
            alSourcef(source, param, (ALfloat)value);
            return;
    }

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        ALCdevice *device = Context->Device;
        ALbuffer  *buffer = NULL;
        ALfilter  *filter = NULL;
        ALbufferlistitem *oldlist;

        if((Source=LookupSource(Context, source)) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        switch(param)
        {
            case AL_SOURCE_RELATIVE:
                CHECK_VALUE(Context, value == AL_FALSE || value == AL_TRUE);

                Source->HeadRelative = (ALboolean)value;
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_LOOPING:
                CHECK_VALUE(Context, value == AL_FALSE || value == AL_TRUE);

                Source->Looping = (ALboolean)value;
                break;

            case AL_BUFFER:
                CHECK_VALUE(Context, value == 0 ||
                                     (buffer=LookupBuffer(device, value)) != NULL);

                LockContext(Context);
                if(!(Source->state == AL_STOPPED || Source->state == AL_INITIAL))
                {
                    UnlockContext(Context);
                    al_throwerr(Context, AL_INVALID_OPERATION);
                }

                Source->BuffersInQueue = 0;
                Source->BuffersPlayed = 0;

                if(buffer != NULL)
                {
                    ALbufferlistitem *BufferListItem;

                    /* Source is now Static */
                    Source->SourceType = AL_STATIC;

                    /* Add the selected buffer to a one-item queue */
                    BufferListItem = malloc(sizeof(ALbufferlistitem));
                    BufferListItem->buffer = buffer;
                    BufferListItem->next = NULL;
                    BufferListItem->prev = NULL;
                    IncrementRef(&buffer->ref);

                    oldlist = ExchangePtr((XchgPtr*)&Source->queue, BufferListItem);
                    Source->BuffersInQueue = 1;

                    ReadLock(&buffer->lock);
                    Source->NumChannels = ChannelsFromFmt(buffer->FmtChannels);
                    Source->SampleSize  = BytesFromFmt(buffer->FmtType);
                    ReadUnlock(&buffer->lock);
                    if(buffer->FmtChannels == FmtMono)
                        Source->Update = CalcSourceParams;
                    else
                        Source->Update = CalcNonAttnSourceParams;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                {
                    /* Source is now Undetermined */
                    Source->SourceType = AL_UNDETERMINED;
                    oldlist = ExchangePtr((XchgPtr*)&Source->queue, NULL);
                }

                /* Delete all elements in the previous queue */
                while(oldlist != NULL)
                {
                    ALbufferlistitem *temp = oldlist;
                    oldlist = temp->next;

                    if(temp->buffer)
                        DecrementRef(&temp->buffer->ref);
                    free(temp);
                }
                UnlockContext(Context);
                break;

            case AL_SOURCE_STATE:
                /* Query only */
                al_throwerr(Context, AL_INVALID_OPERATION);

            case AL_SEC_OFFSET:
            case AL_SAMPLE_OFFSET:
            case AL_BYTE_OFFSET:
                CHECK_VALUE(Context, value >= 0);

                LockContext(Context);
                Source->OffsetType = param;
                Source->Offset = value;

                if((Source->state == AL_PLAYING || Source->state == AL_PAUSED) &&
                   !Context->DeferUpdates)
                {
                    if(ApplyOffset(Source) == AL_FALSE)
                    {
                        UnlockContext(Context);
                        al_throwerr(Context, AL_INVALID_VALUE);
                    }
                }
                UnlockContext(Context);
                break;

            case AL_DIRECT_FILTER:
                CHECK_VALUE(Context, value == 0 ||
                                     (filter=LookupFilter(device, value)) != NULL);

                LockContext(Context);
                if(!filter)
                {
                    Source->DirectGain = 1.0f;
                    Source->DirectGainHF = 1.0f;
                }
                else
                {
                    Source->DirectGain = filter->Gain;
                    Source->DirectGainHF = filter->GainHF;
                }
                UnlockContext(Context);
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_DIRECT_FILTER_GAINHF_AUTO:
                CHECK_VALUE(Context, value == AL_FALSE || value == AL_TRUE);

                Source->DryGainHFAuto = value;
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
                CHECK_VALUE(Context, value == AL_FALSE || value == AL_TRUE);

                Source->WetGainAuto = value;
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
                CHECK_VALUE(Context, value == AL_FALSE || value == AL_TRUE);

                Source->WetGainHFAuto = value;
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_DIRECT_CHANNELS_SOFT:
                CHECK_VALUE(Context, value == AL_FALSE || value == AL_TRUE);

                Source->DirectChannels = value;
                Source->NeedsUpdate = AL_TRUE;
                break;

            case AL_DISTANCE_MODEL:
                CHECK_VALUE(Context, value == AL_NONE ||
                                     value == AL_INVERSE_DISTANCE ||
                                     value == AL_INVERSE_DISTANCE_CLAMPED ||
                                     value == AL_LINEAR_DISTANCE ||
                                     value == AL_LINEAR_DISTANCE_CLAMPED ||
                                     value == AL_EXPONENT_DISTANCE ||
                                     value == AL_EXPONENT_DISTANCE_CLAMPED);

                Source->DistanceModel = value;
                if(Context->SourceDistanceModel)
                    Source->NeedsUpdate = AL_TRUE;
                break;

            default:
                al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}


AL_API void AL_APIENTRY alSource3i(ALuint source, ALenum param, ALint value1, ALint value2, ALint value3)
{
    ALCcontext   *Context;
    ALsource     *Source;

    switch(param)
    {
        case AL_POSITION:
        case AL_VELOCITY:
        case AL_DIRECTION:
            alSource3f(source, param, (ALfloat)value1, (ALfloat)value2, (ALfloat)value3);
            return;
    }

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        ALCdevice *device = Context->Device;
        ALeffectslot *slot = NULL;
        ALfilter *filter = NULL;

        if((Source=LookupSource(Context, source)) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        switch(param)
        {
            case AL_AUXILIARY_SEND_FILTER:
                LockContext(Context);
                if(!((ALuint)value2 < device->NumAuxSends &&
                     (value1 == 0 || (slot=LookupEffectSlot(Context, value1)) != NULL) &&
                     (value3 == 0 || (filter=LookupFilter(device, value3)) != NULL)))
                {
                    UnlockContext(Context);
                    al_throwerr(Context, AL_INVALID_VALUE);
                }

                /* Add refcount on the new slot, and release the previous slot */
                if(slot) IncrementRef(&slot->ref);
                slot = ExchangePtr((XchgPtr*)&Source->Send[value2].Slot, slot);
                if(slot) DecrementRef(&slot->ref);

                if(!filter)
                {
                    /* Disable filter */
                    Source->Send[value2].Gain = 1.0f;
                    Source->Send[value2].GainHF = 1.0f;
                }
                else
                {
                    Source->Send[value2].Gain = filter->Gain;
                    Source->Send[value2].GainHF = filter->GainHF;
                }
                Source->NeedsUpdate = AL_TRUE;
                UnlockContext(Context);
                break;

            default:
                al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}


AL_API void AL_APIENTRY alSourceiv(ALuint source, ALenum param, const ALint *values)
{
    ALCcontext *Context;

    if(values)
    {
        switch(param)
        {
            case AL_SOURCE_RELATIVE:
            case AL_CONE_INNER_ANGLE:
            case AL_CONE_OUTER_ANGLE:
            case AL_LOOPING:
            case AL_BUFFER:
            case AL_SOURCE_STATE:
            case AL_SEC_OFFSET:
            case AL_SAMPLE_OFFSET:
            case AL_BYTE_OFFSET:
            case AL_MAX_DISTANCE:
            case AL_ROLLOFF_FACTOR:
            case AL_REFERENCE_DISTANCE:
            case AL_DIRECT_FILTER:
            case AL_DIRECT_FILTER_GAINHF_AUTO:
            case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
            case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
            case AL_DISTANCE_MODEL:
            case AL_DIRECT_CHANNELS_SOFT:
                alSourcei(source, param, values[0]);
                return;

            case AL_POSITION:
            case AL_VELOCITY:
            case AL_DIRECTION:
            case AL_AUXILIARY_SEND_FILTER:
                alSource3i(source, param, values[0], values[1], values[2]);
                return;
        }
    }

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        if(LookupSource(Context, source) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        CHECK_VALUE(Context, values);
        switch(param)
        {
            default:
                al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}


AL_API ALvoid AL_APIENTRY alGetSourcef(ALuint source, ALenum param, ALfloat *value)
{
    ALCcontext *Context;
    ALsource   *Source;
    ALdouble   offsets[2];
    ALdouble   updateLen;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        if((Source=LookupSource(Context, source)) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        CHECK_VALUE(Context, value);
        switch(param)
        {
            case AL_PITCH:
                *value = Source->Pitch;
                break;

            case AL_GAIN:
                *value = Source->Gain;
                break;

            case AL_MIN_GAIN:
                *value = Source->MinGain;
                break;

            case AL_MAX_GAIN:
                *value = Source->MaxGain;
                break;

            case AL_MAX_DISTANCE:
                *value = Source->MaxDistance;
                break;

            case AL_ROLLOFF_FACTOR:
                *value = Source->RollOffFactor;
                break;

            case AL_CONE_OUTER_GAIN:
                *value = Source->OuterGain;
                break;

            case AL_CONE_OUTER_GAINHF:
                *value = Source->OuterGainHF;
                break;

            case AL_SEC_OFFSET:
            case AL_SAMPLE_OFFSET:
            case AL_BYTE_OFFSET:
                LockContext(Context);
                updateLen = (ALdouble)Context->Device->UpdateSize /
                            Context->Device->Frequency;
                GetSourceOffsets(Source, param, offsets, updateLen);
                UnlockContext(Context);
                *value = (ALfloat)offsets[0];
                break;

            case AL_CONE_INNER_ANGLE:
                *value = Source->InnerAngle;
                break;

            case AL_CONE_OUTER_ANGLE:
                *value = Source->OuterAngle;
                break;

            case AL_REFERENCE_DISTANCE:
                *value = Source->RefDistance;
                break;

            case AL_AIR_ABSORPTION_FACTOR:
                *value = Source->AirAbsorptionFactor;
                break;

            case AL_ROOM_ROLLOFF_FACTOR:
                *value = Source->RoomRolloffFactor;
                break;

            case AL_DOPPLER_FACTOR:
                *value = Source->DopplerFactor;
                break;

            default:
                al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}


AL_API ALvoid AL_APIENTRY alGetSource3f(ALuint source, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3)
{
    ALCcontext *Context;
    ALsource   *Source;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        if((Source=LookupSource(Context, source)) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        CHECK_VALUE(Context, value1 && value2 && value3);
        switch(param)
        {
            case AL_POSITION:
                LockContext(Context);
                *value1 = Source->Position[0];
                *value2 = Source->Position[1];
                *value3 = Source->Position[2];
                UnlockContext(Context);
                break;

            case AL_VELOCITY:
                LockContext(Context);
                *value1 = Source->Velocity[0];
                *value2 = Source->Velocity[1];
                *value3 = Source->Velocity[2];
                UnlockContext(Context);
                break;

            case AL_DIRECTION:
                LockContext(Context);
                *value1 = Source->Orientation[0];
                *value2 = Source->Orientation[1];
                *value3 = Source->Orientation[2];
                UnlockContext(Context);
                break;

            default:
                al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}


AL_API ALvoid AL_APIENTRY alGetSourcefv(ALuint source, ALenum param, ALfloat *values)
{
    ALCcontext *Context;
    ALsource   *Source;
    ALdouble   offsets[2];
    ALdouble   updateLen;

    switch(param)
    {
        case AL_PITCH:
        case AL_GAIN:
        case AL_MIN_GAIN:
        case AL_MAX_GAIN:
        case AL_MAX_DISTANCE:
        case AL_ROLLOFF_FACTOR:
        case AL_DOPPLER_FACTOR:
        case AL_CONE_OUTER_GAIN:
        case AL_SEC_OFFSET:
        case AL_SAMPLE_OFFSET:
        case AL_BYTE_OFFSET:
        case AL_CONE_INNER_ANGLE:
        case AL_CONE_OUTER_ANGLE:
        case AL_REFERENCE_DISTANCE:
        case AL_CONE_OUTER_GAINHF:
        case AL_AIR_ABSORPTION_FACTOR:
        case AL_ROOM_ROLLOFF_FACTOR:
            alGetSourcef(source, param, values);
            return;

        case AL_POSITION:
        case AL_VELOCITY:
        case AL_DIRECTION:
            alGetSource3f(source, param, values+0, values+1, values+2);
            return;
    }

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        if((Source=LookupSource(Context, source)) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        CHECK_VALUE(Context, values);
        switch(param)
        {
            case AL_SAMPLE_RW_OFFSETS_SOFT:
            case AL_BYTE_RW_OFFSETS_SOFT:
                LockContext(Context);
                updateLen = (ALdouble)Context->Device->UpdateSize /
                            Context->Device->Frequency;
                GetSourceOffsets(Source, param, offsets, updateLen);
                UnlockContext(Context);
                values[0] = (ALfloat)offsets[0];
                values[1] = (ALfloat)offsets[1];
                break;

            default:
                al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}


AL_API ALvoid AL_APIENTRY alGetSourcei(ALuint source, ALenum param, ALint *value)
{
    ALbufferlistitem *BufferList;
    ALCcontext *Context;
    ALsource   *Source;
    ALdouble   offsets[2];
    ALdouble   updateLen;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        if((Source=LookupSource(Context, source)) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        CHECK_VALUE(Context, value);
        switch(param)
        {
            case AL_MAX_DISTANCE:
                *value = (ALint)Source->MaxDistance;
                break;

            case AL_ROLLOFF_FACTOR:
                *value = (ALint)Source->RollOffFactor;
                break;

            case AL_REFERENCE_DISTANCE:
                *value = (ALint)Source->RefDistance;
                break;

            case AL_SOURCE_RELATIVE:
                *value = Source->HeadRelative;
                break;

            case AL_CONE_INNER_ANGLE:
                *value = (ALint)Source->InnerAngle;
                break;

            case AL_CONE_OUTER_ANGLE:
                *value = (ALint)Source->OuterAngle;
                break;

            case AL_LOOPING:
                *value = Source->Looping;
                break;

            case AL_BUFFER:
                LockContext(Context);
                BufferList = Source->queue;
                if(Source->SourceType != AL_STATIC)
                {
                    ALuint i = Source->BuffersPlayed;
                    while(i > 0)
                    {
                        BufferList = BufferList->next;
                        i--;
                    }
                }
                *value = ((BufferList && BufferList->buffer) ?
                          BufferList->buffer->id : 0);
                UnlockContext(Context);
                break;

            case AL_SOURCE_STATE:
                *value = Source->state;
                break;

            case AL_BUFFERS_QUEUED:
                *value = Source->BuffersInQueue;
                break;

            case AL_BUFFERS_PROCESSED:
                LockContext(Context);
                if(Source->Looping || Source->SourceType != AL_STREAMING)
                {
                    /* Buffers on a looping source are in a perpetual state of
                     * PENDING, so don't report any as PROCESSED */
                    *value = 0;
                }
                else
                    *value = Source->BuffersPlayed;
                UnlockContext(Context);
                break;

            case AL_SOURCE_TYPE:
                *value = Source->SourceType;
                break;

            case AL_SEC_OFFSET:
            case AL_SAMPLE_OFFSET:
            case AL_BYTE_OFFSET:
                LockContext(Context);
                updateLen = (ALdouble)Context->Device->UpdateSize /
                            Context->Device->Frequency;
                GetSourceOffsets(Source, param, offsets, updateLen);
                UnlockContext(Context);
                *value = (ALint)offsets[0];
                break;

            case AL_DIRECT_FILTER_GAINHF_AUTO:
                *value = Source->DryGainHFAuto;
                break;

            case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
                *value = Source->WetGainAuto;
                break;

            case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
                *value = Source->WetGainHFAuto;
                break;

            case AL_DOPPLER_FACTOR:
                *value = (ALint)Source->DopplerFactor;
                break;

            case AL_DIRECT_CHANNELS_SOFT:
                *value = Source->DirectChannels;
                break;

            case AL_DISTANCE_MODEL:
                *value = Source->DistanceModel;
                break;

            default:
                al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}


AL_API void AL_APIENTRY alGetSource3i(ALuint source, ALenum param, ALint *value1, ALint *value2, ALint *value3)
{
    ALCcontext *Context;
    ALsource   *Source;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        if((Source=LookupSource(Context, source)) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        CHECK_VALUE(Context, value1 && value2 && value3);
        switch(param)
        {
            case AL_POSITION:
                LockContext(Context);
                *value1 = (ALint)Source->Position[0];
                *value2 = (ALint)Source->Position[1];
                *value3 = (ALint)Source->Position[2];
                UnlockContext(Context);
                break;

            case AL_VELOCITY:
                LockContext(Context);
                *value1 = (ALint)Source->Velocity[0];
                *value2 = (ALint)Source->Velocity[1];
                *value3 = (ALint)Source->Velocity[2];
                UnlockContext(Context);
                break;

            case AL_DIRECTION:
                LockContext(Context);
                *value1 = (ALint)Source->Orientation[0];
                *value2 = (ALint)Source->Orientation[1];
                *value3 = (ALint)Source->Orientation[2];
                UnlockContext(Context);
                break;

            default:
                al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}


AL_API void AL_APIENTRY alGetSourceiv(ALuint source, ALenum param, ALint *values)
{
    ALCcontext *Context;
    ALsource   *Source;
    ALdouble   offsets[2];
    ALdouble   updateLen;

    switch(param)
    {
        case AL_SOURCE_RELATIVE:
        case AL_CONE_INNER_ANGLE:
        case AL_CONE_OUTER_ANGLE:
        case AL_LOOPING:
        case AL_BUFFER:
        case AL_SOURCE_STATE:
        case AL_BUFFERS_QUEUED:
        case AL_BUFFERS_PROCESSED:
        case AL_SEC_OFFSET:
        case AL_SAMPLE_OFFSET:
        case AL_BYTE_OFFSET:
        case AL_MAX_DISTANCE:
        case AL_ROLLOFF_FACTOR:
        case AL_DOPPLER_FACTOR:
        case AL_REFERENCE_DISTANCE:
        case AL_SOURCE_TYPE:
        case AL_DIRECT_FILTER:
        case AL_DIRECT_FILTER_GAINHF_AUTO:
        case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
        case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
        case AL_DISTANCE_MODEL:
        case AL_DIRECT_CHANNELS_SOFT:
            alGetSourcei(source, param, values);
            return;

        case AL_POSITION:
        case AL_VELOCITY:
        case AL_DIRECTION:
            alGetSource3i(source, param, values+0, values+1, values+2);
            return;
    }

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        if((Source=LookupSource(Context, source)) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);
        CHECK_VALUE(Context, values);
        switch(param)
        {
            case AL_SAMPLE_RW_OFFSETS_SOFT:
            case AL_BYTE_RW_OFFSETS_SOFT:
                LockContext(Context);
                updateLen = (ALdouble)Context->Device->UpdateSize /
                            Context->Device->Frequency;
                GetSourceOffsets(Source, param, offsets, updateLen);
                UnlockContext(Context);
                values[0] = (ALint)offsets[0];
                values[1] = (ALint)offsets[1];
                break;

            default:
                al_throwerr(Context, AL_INVALID_ENUM);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}


AL_API ALvoid AL_APIENTRY alSourcePlay(ALuint source)
{
    alSourcePlayv(1, &source);
}
AL_API ALvoid AL_APIENTRY alSourcePlayv(ALsizei n, const ALuint *sources)
{
    ALCcontext *Context;
    ALsource   *Source;
    ALsizei    i;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        CHECK_VALUE(Context, n >= 0);
        for(i = 0;i < n;i++)
        {
            if(!LookupSource(Context, sources[i]))
                al_throwerr(Context, AL_INVALID_NAME);
        }

        LockContext(Context);
        while(Context->MaxActiveSources-Context->ActiveSourceCount < n)
        {
            void *temp = NULL;
            ALsizei newcount;

            newcount = Context->MaxActiveSources << 1;
            if(newcount > 0)
                temp = realloc(Context->ActiveSources,
                               sizeof(*Context->ActiveSources) * newcount);
            if(!temp)
            {
                UnlockContext(Context);
                al_throwerr(Context, AL_OUT_OF_MEMORY);
            }

            Context->ActiveSources = temp;
            Context->MaxActiveSources = newcount;
        }

        for(i = 0;i < n;i++)
        {
            Source = LookupSource(Context, sources[i]);
            if(Context->DeferUpdates) Source->new_state = AL_PLAYING;
            else SetSourceState(Source, Context, AL_PLAYING);
        }
        UnlockContext(Context);
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alSourcePause(ALuint source)
{
    alSourcePausev(1, &source);
}
AL_API ALvoid AL_APIENTRY alSourcePausev(ALsizei n, const ALuint *sources)
{
    ALCcontext *Context;
    ALsource   *Source;
    ALsizei    i;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        CHECK_VALUE(Context, n >= 0);
        for(i = 0;i < n;i++)
        {
            if(!LookupSource(Context, sources[i]))
                al_throwerr(Context, AL_INVALID_NAME);
        }

        LockContext(Context);
        for(i = 0;i < n;i++)
        {
            Source = LookupSource(Context, sources[i]);
            if(Context->DeferUpdates) Source->new_state = AL_PAUSED;
            else SetSourceState(Source, Context, AL_PAUSED);
        }
        UnlockContext(Context);
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alSourceStop(ALuint source)
{
    alSourceStopv(1, &source);
}
AL_API ALvoid AL_APIENTRY alSourceStopv(ALsizei n, const ALuint *sources)
{
    ALCcontext *Context;
    ALsource   *Source;
    ALsizei    i;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        CHECK_VALUE(Context, n >= 0);
        for(i = 0;i < n;i++)
        {
            if(!LookupSource(Context, sources[i]))
                al_throwerr(Context, AL_INVALID_NAME);
        }

        LockContext(Context);
        for(i = 0;i < n;i++)
        {
            Source = LookupSource(Context, sources[i]);
            Source->new_state = AL_NONE;
            SetSourceState(Source, Context, AL_STOPPED);
        }
        UnlockContext(Context);
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alSourceRewind(ALuint source)
{
    alSourceRewindv(1, &source);
}
AL_API ALvoid AL_APIENTRY alSourceRewindv(ALsizei n, const ALuint *sources)
{
    ALCcontext *Context;
    ALsource   *Source;
    ALsizei    i;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        CHECK_VALUE(Context, n >= 0);
        for(i = 0;i < n;i++)
        {
            if(!LookupSource(Context, sources[i]))
                al_throwerr(Context, AL_INVALID_NAME);
        }

        LockContext(Context);
        for(i = 0;i < n;i++)
        {
            Source = LookupSource(Context, sources[i]);
            Source->new_state = AL_NONE;
            SetSourceState(Source, Context, AL_INITIAL);
        }
        UnlockContext(Context);
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}


AL_API ALvoid AL_APIENTRY alSourceQueueBuffers(ALuint source, ALsizei nb, const ALuint *buffers)
{
    ALCcontext *Context;
    ALsource   *Source;
    ALsizei    i;
    ALbufferlistitem *BufferListStart = NULL;
    ALbufferlistitem *BufferList;
    ALbuffer *BufferFmt;

    if(nb == 0)
        return;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        ALCdevice *device = Context->Device;

        CHECK_VALUE(Context, nb >= 0);

        if((Source=LookupSource(Context, source)) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);

        LockContext(Context);
        if(Source->SourceType == AL_STATIC)
        {
            UnlockContext(Context);
            /* Can't queue on a Static Source */
            al_throwerr(Context, AL_INVALID_OPERATION);
        }

        BufferFmt = NULL;

        /* Check for a valid Buffer, for its frequency and format */
        BufferList = Source->queue;
        while(BufferList)
        {
            if(BufferList->buffer)
            {
                BufferFmt = BufferList->buffer;
                break;
            }
            BufferList = BufferList->next;
        }

        for(i = 0;i < nb;i++)
        {
            ALbuffer *buffer = NULL;
            if(buffers[i] && (buffer=LookupBuffer(device, buffers[i])) == NULL)
            {
                UnlockContext(Context);
                al_throwerr(Context, AL_INVALID_NAME);
            }

            if(!BufferListStart)
            {
                BufferListStart = malloc(sizeof(ALbufferlistitem));
                BufferListStart->buffer = buffer;
                BufferListStart->next = NULL;
                BufferListStart->prev = NULL;
                BufferList = BufferListStart;
            }
            else
            {
                BufferList->next = malloc(sizeof(ALbufferlistitem));
                BufferList->next->buffer = buffer;
                BufferList->next->next = NULL;
                BufferList->next->prev = BufferList;
                BufferList = BufferList->next;
            }
            if(!buffer) continue;
            IncrementRef(&buffer->ref);

            ReadLock(&buffer->lock);
            if(BufferFmt == NULL)
            {
                BufferFmt = buffer;

                Source->NumChannels = ChannelsFromFmt(buffer->FmtChannels);
                Source->SampleSize  = BytesFromFmt(buffer->FmtType);
                if(buffer->FmtChannels == FmtMono)
                    Source->Update = CalcSourceParams;
                else
                    Source->Update = CalcNonAttnSourceParams;

                Source->NeedsUpdate = AL_TRUE;
            }
            else if(BufferFmt->Frequency != buffer->Frequency ||
                    BufferFmt->OriginalChannels != buffer->OriginalChannels ||
                    BufferFmt->OriginalType != buffer->OriginalType)
            {
                ReadUnlock(&buffer->lock);
                UnlockContext(Context);
                al_throwerr(Context, AL_INVALID_OPERATION);
            }
            ReadUnlock(&buffer->lock);
        }

        /* Source is now streaming */
        Source->SourceType = AL_STREAMING;

        if(Source->queue == NULL)
            Source->queue = BufferListStart;
        else
        {
            /* Append to the end of the queue */
            BufferList = Source->queue;
            while(BufferList->next != NULL)
                BufferList = BufferList->next;

            BufferListStart->prev = BufferList;
            BufferList->next = BufferListStart;
        }

        Source->BuffersInQueue += nb;

        UnlockContext(Context);
    }
    al_catchany()
    {
        while(BufferListStart)
        {
            BufferList = BufferListStart;
            BufferListStart = BufferList->next;

            if(BufferList->buffer)
                DecrementRef(&BufferList->buffer->ref);
            free(BufferList);
        }
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alSourceUnqueueBuffers(ALuint source, ALsizei nb, ALuint *buffers)
{
    ALCcontext *Context;
    ALsource   *Source;
    ALsizei    i;
    ALbufferlistitem *BufferList;

    if(nb == 0)
        return;

    Context = GetContextRef();
    if(!Context) return;

    al_try
    {
        CHECK_VALUE(Context, nb >= 0);

        if((Source=LookupSource(Context, source)) == NULL)
            al_throwerr(Context, AL_INVALID_NAME);

        LockContext(Context);
        if(Source->Looping || Source->SourceType != AL_STREAMING ||
           (ALuint)nb > Source->BuffersPlayed)
        {
            UnlockContext(Context);
            /* Trying to unqueue pending buffers, or a buffer that wasn't queued. */
            al_throwerr(Context, AL_INVALID_VALUE);
        }

        for(i = 0;i < nb;i++)
        {
            BufferList = Source->queue;
            Source->queue = BufferList->next;
            Source->BuffersInQueue--;
            Source->BuffersPlayed--;

            if(BufferList->buffer)
            {
                buffers[i] = BufferList->buffer->id;
                DecrementRef(&BufferList->buffer->ref);
            }
            else
                buffers[i] = 0;

            free(BufferList);
        }
        if(Source->queue)
            Source->queue->prev = NULL;
        UnlockContext(Context);
    }
    al_endtry;

    ALCcontext_DecRef(Context);
}


static ALvoid InitSourceParams(ALsource *Source)
{
    ALuint i;

    Source->InnerAngle = 360.0f;
    Source->OuterAngle = 360.0f;
    Source->Pitch = 1.0f;
    Source->Position[0] = 0.0f;
    Source->Position[1] = 0.0f;
    Source->Position[2] = 0.0f;
    Source->Orientation[0] = 0.0f;
    Source->Orientation[1] = 0.0f;
    Source->Orientation[2] = 0.0f;
    Source->Velocity[0] = 0.0f;
    Source->Velocity[1] = 0.0f;
    Source->Velocity[2] = 0.0f;
    Source->RefDistance = 1.0f;
    Source->MaxDistance = FLT_MAX;
    Source->RollOffFactor = 1.0f;
    Source->Looping = AL_FALSE;
    Source->Gain = 1.0f;
    Source->MinGain = 0.0f;
    Source->MaxGain = 1.0f;
    Source->OuterGain = 0.0f;
    Source->OuterGainHF = 1.0f;

    Source->DryGainHFAuto = AL_TRUE;
    Source->WetGainAuto = AL_TRUE;
    Source->WetGainHFAuto = AL_TRUE;
    Source->AirAbsorptionFactor = 0.0f;
    Source->RoomRolloffFactor = 0.0f;
    Source->DopplerFactor = 1.0f;
    Source->DirectChannels = AL_FALSE;

    Source->DistanceModel = DefaultDistanceModel;

    Source->Resampler = DefaultResampler;

    Source->state = AL_INITIAL;
    Source->new_state = AL_NONE;
    Source->SourceType = AL_UNDETERMINED;
    Source->Offset = -1.0;

    Source->DirectGain = 1.0f;
    Source->DirectGainHF = 1.0f;
    for(i = 0;i < MAX_SENDS;i++)
    {
        Source->Send[i].Gain = 1.0f;
        Source->Send[i].GainHF = 1.0f;
    }

    Source->NeedsUpdate = AL_TRUE;

    Source->Hrtf.Moving = AL_FALSE;
    Source->Hrtf.Counter = 0;
}


/* SetSourceState
 *
 * Sets the source's new play state given its current state.
 */
ALvoid SetSourceState(ALsource *Source, ALCcontext *Context, ALenum state)
{
    if(state == AL_PLAYING)
    {
        ALbufferlistitem *BufferList;
        ALsizei j, k;

        /* Check that there is a queue containing at least one valid, non zero
         * length Buffer. */
        BufferList = Source->queue;
        while(BufferList)
        {
            if(BufferList->buffer != NULL && BufferList->buffer->SampleLen)
                break;
            BufferList = BufferList->next;
        }

        if(Source->state != AL_PLAYING)
        {
            for(j = 0;j < MAXCHANNELS;j++)
            {
                for(k = 0;k < SRC_HISTORY_LENGTH;k++)
                    Source->Hrtf.History[j][k] = 0.0f;
                for(k = 0;k < HRIR_LENGTH;k++)
                {
                    Source->Hrtf.Values[j][k][0] = 0.0f;
                    Source->Hrtf.Values[j][k][1] = 0.0f;
                }
            }
        }

        if(Source->state != AL_PAUSED)
        {
            Source->state = AL_PLAYING;
            Source->position = 0;
            Source->position_fraction = 0;
            Source->BuffersPlayed = 0;
        }
        else
            Source->state = AL_PLAYING;

        // Check if an Offset has been set
        if(Source->Offset >= 0.0)
            ApplyOffset(Source);

        /* If there's nothing to play, or device is disconnected, go right to
         * stopped */
        if(!BufferList || !Context->Device->Connected)
        {
            SetSourceState(Source, Context, AL_STOPPED);
            return;
        }

        for(j = 0;j < Context->ActiveSourceCount;j++)
        {
            if(Context->ActiveSources[j] == Source)
                break;
        }
        if(j == Context->ActiveSourceCount)
            Context->ActiveSources[Context->ActiveSourceCount++] = Source;
    }
    else if(state == AL_PAUSED)
    {
        if(Source->state == AL_PLAYING)
        {
            Source->state = AL_PAUSED;
            Source->Hrtf.Moving = AL_FALSE;
            Source->Hrtf.Counter = 0;
        }
    }
    else if(state == AL_STOPPED)
    {
        if(Source->state != AL_INITIAL)
        {
            Source->state = AL_STOPPED;
            Source->BuffersPlayed = Source->BuffersInQueue;
            Source->Hrtf.Moving = AL_FALSE;
            Source->Hrtf.Counter = 0;
        }
        Source->Offset = -1.0;
    }
    else if(state == AL_INITIAL)
    {
        if(Source->state != AL_INITIAL)
        {
            Source->state = AL_INITIAL;
            Source->position = 0;
            Source->position_fraction = 0;
            Source->BuffersPlayed = 0;
            Source->Hrtf.Moving = AL_FALSE;
            Source->Hrtf.Counter = 0;
        }
        Source->Offset = -1.0;
    }
}

/* GetSourceOffsets
 *
 * Gets the current read and write offsets for the given Source, in the
 * appropriate format (Bytes, Samples or Seconds). The offsets are relative to
 * the start of the queue (not the start of the current buffer).
 */
static ALvoid GetSourceOffsets(ALsource *Source, ALenum name, ALdouble *offset, ALdouble updateLen)
{
    const ALbufferlistitem *BufferList;
    const ALbuffer         *Buffer = NULL;
    ALuint readPos, writePos;
    ALuint totalBufferLen;
    ALuint i;

    // Find the first valid Buffer in the Queue
    BufferList = Source->queue;
    while(BufferList)
    {
        if(BufferList->buffer)
        {
            Buffer = BufferList->buffer;
            break;
        }
        BufferList = BufferList->next;
    }

    if((Source->state != AL_PLAYING && Source->state != AL_PAUSED) || !Buffer)
    {
        offset[0] = 0.0;
        offset[1] = 0.0;
        return;
    }

    if(updateLen > 0.0 && updateLen < 0.015)
        updateLen = 0.015;

    /* NOTE: This is the offset into the *current* buffer, so add the length of
     * any played buffers */
    readPos = Source->position;
    totalBufferLen = 0;
    BufferList = Source->queue;
    for(i = 0;BufferList;i++)
    {
        if(BufferList->buffer)
        {
            if(i < Source->BuffersPlayed)
                readPos += BufferList->buffer->SampleLen;
            totalBufferLen += BufferList->buffer->SampleLen;
        }
        BufferList = BufferList->next;
    }
    if(Source->state == AL_PLAYING)
        writePos = readPos + (ALuint)(updateLen*Buffer->Frequency);
    else
        writePos = readPos;

    if(Source->Looping)
    {
        readPos %= totalBufferLen;
        writePos %= totalBufferLen;
    }
    else
    {
        /* Wrap positions back to 0 */
        if(readPos >= totalBufferLen)
            readPos = 0;
        if(writePos >= totalBufferLen)
            writePos = 0;
    }

    switch(name)
    {
        case AL_SEC_OFFSET:
            offset[0] = (ALdouble)readPos / Buffer->Frequency;
            offset[1] = (ALdouble)writePos / Buffer->Frequency;
            break;

        case AL_SAMPLE_OFFSET:
        case AL_SAMPLE_RW_OFFSETS_SOFT:
            offset[0] = (ALdouble)readPos;
            offset[1] = (ALdouble)writePos;
            break;

        case AL_BYTE_OFFSET:
        case AL_BYTE_RW_OFFSETS_SOFT:
            if(Buffer->OriginalType == UserFmtIMA4)
            {
                ALuint BlockSize = 36 * ChannelsFromFmt(Buffer->FmtChannels);
                ALuint FrameBlockSize = 65;

                /* Round down to nearest ADPCM block */
                offset[0] = (ALdouble)(readPos / FrameBlockSize * BlockSize);
                if(Source->state != AL_PLAYING)
                    offset[1] = offset[0];
                else
                {
                    /* Round up to nearest ADPCM block */
                    offset[1] = (ALdouble)((writePos+FrameBlockSize-1) /
                                           FrameBlockSize * BlockSize);
                }
            }
            else
            {
                ALuint FrameSize = FrameSizeFromUserFmt(Buffer->OriginalChannels, Buffer->OriginalType);
                offset[0] = (ALdouble)(readPos * FrameSize);
                offset[1] = (ALdouble)(writePos * FrameSize);
            }
            break;
    }
}


/* ApplyOffset
 *
 * Apply the stored playback offset to the Source. This function will update
 * the number of buffers "played" given the stored offset.
 */
ALboolean ApplyOffset(ALsource *Source)
{
    const ALbufferlistitem *BufferList;
    const ALbuffer         *Buffer;
    ALint bufferLen, totalBufferLen;
    ALint buffersPlayed;
    ALint offset;

    /* Get sample frame offset */
    offset = GetSampleOffset(Source);
    if(offset == -1)
        return AL_FALSE;

    buffersPlayed = 0;
    totalBufferLen = 0;

    BufferList = Source->queue;
    while(BufferList)
    {
        Buffer = BufferList->buffer;
        bufferLen = Buffer ? Buffer->SampleLen : 0;

        if(bufferLen <= offset-totalBufferLen)
        {
            /* Offset is past this buffer so increment to the next buffer */
            buffersPlayed++;
        }
        else if(totalBufferLen <= offset)
        {
            /* Offset is in this buffer */
            Source->BuffersPlayed = buffersPlayed;

            Source->position = offset - totalBufferLen;
            Source->position_fraction = 0;
            return AL_TRUE;
        }

        totalBufferLen += bufferLen;

        BufferList = BufferList->next;
    }

    /* Offset is out of range of the queue */
    return AL_FALSE;
}


/* GetSampleOffset
 *
 * Returns the sample offset into the Source's queue (from the Sample, Byte or
 * Second offset supplied by the application). This takes into account the fact
 * that the buffer format may have been modifed since.
 */
static ALint GetSampleOffset(ALsource *Source)
{
    const ALbuffer *Buffer = NULL;
    const ALbufferlistitem *BufferList;
    ALint Offset = -1;

    /* Find the first valid Buffer in the Queue */
    BufferList = Source->queue;
    while(BufferList)
    {
        if(BufferList->buffer)
        {
            Buffer = BufferList->buffer;
            break;
        }
        BufferList = BufferList->next;
    }

    if(!Buffer)
    {
        Source->Offset = -1.0;
        return -1;
    }

    switch(Source->OffsetType)
    {
    case AL_BYTE_OFFSET:
        /* Determine the ByteOffset (and ensure it is block aligned) */
        Offset = (ALint)Source->Offset;
        if(Buffer->OriginalType == UserFmtIMA4)
        {
            Offset /= 36 * ChannelsFromUserFmt(Buffer->OriginalChannels);
            Offset *= 65;
        }
        else
            Offset /= FrameSizeFromUserFmt(Buffer->OriginalChannels, Buffer->OriginalType);
        break;

    case AL_SAMPLE_OFFSET:
        Offset = (ALint)Source->Offset;
        break;

    case AL_SEC_OFFSET:
        Offset = (ALint)(Source->Offset * Buffer->Frequency);
        break;
    }
    Source->Offset = -1.0;

    return Offset;
}


/* ReleaseALSources
 *
 * Destroys all sources in the source map.
 */
ALvoid ReleaseALSources(ALCcontext *Context)
{
    ALsizei pos;
    ALuint j;
    for(pos = 0;pos < Context->SourceMap.size;pos++)
    {
        ALsource *temp = Context->SourceMap.array[pos].value;
        Context->SourceMap.array[pos].value = NULL;

        while(temp->queue != NULL)
        {
            ALbufferlistitem *BufferList = temp->queue;
            temp->queue = BufferList->next;

            if(BufferList->buffer != NULL)
                DecrementRef(&BufferList->buffer->ref);
            free(BufferList);
        }

        for(j = 0;j < MAX_SENDS;++j)
        {
            if(temp->Send[j].Slot)
                DecrementRef(&temp->Send[j].Slot->ref);
            temp->Send[j].Slot = NULL;
        }

        FreeThunkEntry(temp->id);
        memset(temp, 0, sizeof(*temp));
        free(temp);
    }
}
