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
static ALvoid GetSourceOffset(ALsource *Source, ALenum name, ALdouble *Offsets, ALdouble updateLen);
static ALint GetSampleOffset(ALsource *Source);


AL_API ALvoid AL_APIENTRY alGenSources(ALsizei n, ALuint *sources)
{
    ALCcontext *Context;

    Context = GetContextRef();
    if(!Context) return;

    if(n < 0 || IsBadWritePtr((void*)sources, n * sizeof(ALuint)))
        alSetError(Context, AL_INVALID_VALUE);
    else
    {
        ALenum err;
        ALsizei i;

        // Add additional sources to the list
        i = 0;
        while(i < n)
        {
            ALsource *source = calloc(1, sizeof(ALsource));
            if(!source)
            {
                alSetError(Context, AL_OUT_OF_MEMORY);
                alDeleteSources(i, sources);
                break;
            }
            InitSourceParams(source);

            err = NewThunkEntry(&source->source);
            if(err == AL_NO_ERROR)
                err = InsertUIntMapEntry(&Context->SourceMap, source->source, source);
            if(err != AL_NO_ERROR)
            {
                FreeThunkEntry(source->source);
                memset(source, 0, sizeof(ALsource));
                free(source);

                alSetError(Context, err);
                alDeleteSources(i, sources);
                break;
            }

            sources[i++] = source->source;
        }
    }

    ALCcontext_DecRef(Context);
}


AL_API ALvoid AL_APIENTRY alDeleteSources(ALsizei n, const ALuint *sources)
{
    ALCcontext *Context;
    ALsource *Source;
    ALsizei i, j;
    ALbufferlistitem *BufferList;

    Context = GetContextRef();
    if(!Context) return;

    if(n < 0)
        alSetError(Context, AL_INVALID_VALUE);
    else
    {
        // Check that all Sources are valid (and can therefore be deleted)
        for(i = 0;i < n;i++)
        {
            if(LookupSource(Context, sources[i]) == NULL)
            {
                alSetError(Context, AL_INVALID_NAME);
                n = 0;
                break;
            }
        }

        // All Sources are valid, and can be deleted
        for(i = 0;i < n;i++)
        {
            ALsource **srclist, **srclistend;

            // Remove Source from list of Sources
            if((Source=RemoveSource(Context, sources[i])) == NULL)
                continue;

            FreeThunkEntry(Source->source);

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

            // For each buffer in the source's queue...
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

            memset(Source,0,sizeof(ALsource));
            free(Source);
        }
    }

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

    if((Source=LookupSource(Context, source)) != NULL)
    {
        switch(param)
        {
            case AL_PITCH:
                if(value >= 0.0f)
                {
                    Source->Pitch = value;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_CONE_INNER_ANGLE:
                if(value >= 0.0f && value <= 360.0f)
                {
                    Source->InnerAngle = value;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_CONE_OUTER_ANGLE:
                if(value >= 0.0f && value <= 360.0f)
                {
                    Source->OuterAngle = value;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_GAIN:
                if(value >= 0.0f)
                {
                    Source->Gain = value;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_MAX_DISTANCE:
                if(value >= 0.0f)
                {
                    Source->MaxDistance = value;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_ROLLOFF_FACTOR:
                if(value >= 0.0f)
                {
                    Source->RollOffFactor = value;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_REFERENCE_DISTANCE:
                if(value >= 0.0f)
                {
                    Source->RefDistance = value;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_MIN_GAIN:
                if(value >= 0.0f && value <= 1.0f)
                {
                    Source->MinGain = value;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_MAX_GAIN:
                if(value >= 0.0f && value <= 1.0f)
                {
                    Source->MaxGain = value;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_CONE_OUTER_GAIN:
                if(value >= 0.0f && value <= 1.0f)
                {
                    Source->OuterGain = value;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_CONE_OUTER_GAINHF:
                if(value >= 0.0f && value <= 1.0f)
                {
                    Source->OuterGainHF = value;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_AIR_ABSORPTION_FACTOR:
                if(value >= 0.0f && value <= 10.0f)
                {
                    Source->AirAbsorptionFactor = value;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_ROOM_ROLLOFF_FACTOR:
                if(value >= 0.0f && value <= 10.0f)
                {
                    Source->RoomRolloffFactor = value;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_DOPPLER_FACTOR:
                if(value >= 0.0f && value <= 1.0f)
                {
                    Source->DopplerFactor = value;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_SEC_OFFSET:
            case AL_SAMPLE_OFFSET:
            case AL_BYTE_OFFSET:
                if(value >= 0.0f)
                {
                    LockContext(Context);
                    // Store Offset
                    Source->OffsetType = param;
                    Source->Offset = value;

                    if((Source->state == AL_PLAYING || Source->state == AL_PAUSED) &&
                       !Context->DeferUpdates)
                    {
                        if(ApplyOffset(Source) == AL_FALSE)
                            alSetError(Context, AL_INVALID_VALUE);
                    }
                    UnlockContext(Context);
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
        }
    }
    else
    {
        // Invalid Source Name
        alSetError(Context, AL_INVALID_NAME);
    }

    ALCcontext_DecRef(Context);
}


AL_API ALvoid AL_APIENTRY alSource3f(ALuint source, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3)
{
    ALCcontext *Context;
    ALsource   *Source;

    Context = GetContextRef();
    if(!Context) return;

    if((Source=LookupSource(Context, source)) != NULL)
    {
        switch(param)
        {
            case AL_POSITION:
                if(isfinite(value1) && isfinite(value2) && isfinite(value3))
                {
                    LockContext(Context);
                    Source->Position[0] = value1;
                    Source->Position[1] = value2;
                    Source->Position[2] = value3;
                    UnlockContext(Context);
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_VELOCITY:
                if(isfinite(value1) && isfinite(value2) && isfinite(value3))
                {
                    LockContext(Context);
                    Source->Velocity[0] = value1;
                    Source->Velocity[1] = value2;
                    Source->Velocity[2] = value3;
                    UnlockContext(Context);
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_DIRECTION:
                if(isfinite(value1) && isfinite(value2) && isfinite(value3))
                {
                    LockContext(Context);
                    Source->Orientation[0] = value1;
                    Source->Orientation[1] = value2;
                    Source->Orientation[2] = value3;
                    UnlockContext(Context);
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
        }
    }
    else
        alSetError(Context, AL_INVALID_NAME);

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

    if(values)
    {
        if(LookupSource(Context, source) != NULL)
        {
            switch(param)
            {
                default:
                    alSetError(Context, AL_INVALID_ENUM);
                    break;
            }
        }
        else
            alSetError(Context, AL_INVALID_NAME);
    }
    else
        alSetError(Context, AL_INVALID_VALUE);

    ALCcontext_DecRef(Context);
}


AL_API ALvoid AL_APIENTRY alSourcei(ALuint source, ALenum param, ALint value)
{
    ALCcontext       *Context;
    ALsource         *Source;
    ALbufferlistitem *BufferListItem;

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

    if((Source=LookupSource(Context, source)) != NULL)
    {
        ALCdevice *device = Context->Device;

        switch(param)
        {
            case AL_SOURCE_RELATIVE:
                if(value == AL_FALSE || value == AL_TRUE)
                {
                    Source->HeadRelative = (ALboolean)value;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_LOOPING:
                if(value == AL_FALSE || value == AL_TRUE)
                    Source->Looping = (ALboolean)value;
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_BUFFER:
                LockContext(Context);
                if(Source->state == AL_STOPPED || Source->state == AL_INITIAL)
                {
                    ALbufferlistitem *oldlist;
                    ALbuffer *buffer = NULL;

                    if(value == 0 || (buffer=LookupBuffer(device, value)) != NULL)
                    {
                        Source->BuffersInQueue = 0;
                        Source->BuffersPlayed = 0;

                        // Add the buffer to the queue (as long as it is NOT the NULL buffer)
                        if(buffer != NULL)
                        {
                            // Source is now in STATIC mode
                            Source->SourceType = AL_STATIC;

                            // Add the selected buffer to the queue
                            BufferListItem = malloc(sizeof(ALbufferlistitem));
                            BufferListItem->buffer = buffer;
                            BufferListItem->next = NULL;
                            BufferListItem->prev = NULL;
                            // Increment reference counter for buffer
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
                            // Source is now in UNDETERMINED mode
                            Source->SourceType = AL_UNDETERMINED;
                            oldlist = ExchangePtr((XchgPtr*)&Source->queue, NULL);
                        }

                        // Delete all previous elements in the queue
                        while(oldlist != NULL)
                        {
                            BufferListItem = oldlist;
                            oldlist = BufferListItem->next;

                            if(BufferListItem->buffer)
                                DecrementRef(&BufferListItem->buffer->ref);
                            free(BufferListItem);
                        }
                    }
                    else
                        alSetError(Context, AL_INVALID_VALUE);
                }
                else
                    alSetError(Context, AL_INVALID_OPERATION);
                UnlockContext(Context);
                break;

            case AL_SOURCE_STATE:
                // Query only
                alSetError(Context, AL_INVALID_OPERATION);
                break;

            case AL_SEC_OFFSET:
            case AL_SAMPLE_OFFSET:
            case AL_BYTE_OFFSET:
                if(value >= 0)
                {
                    LockContext(Context);
                    // Store Offset
                    Source->OffsetType = param;
                    Source->Offset = value;

                    if((Source->state == AL_PLAYING || Source->state == AL_PAUSED) &&
                       !Context->DeferUpdates)
                    {
                        if(ApplyOffset(Source) == AL_FALSE)
                            alSetError(Context, AL_INVALID_VALUE);
                    }
                    UnlockContext(Context);
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_DIRECT_FILTER: {
                ALfilter *filter = NULL;

                if(value == 0 || (filter=LookupFilter(Context->Device, value)) != NULL)
                {
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
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
            }   break;

            case AL_DIRECT_FILTER_GAINHF_AUTO:
                if(value == AL_TRUE || value == AL_FALSE)
                {
                    Source->DryGainHFAuto = value;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
                if(value == AL_TRUE || value == AL_FALSE)
                {
                    Source->WetGainAuto = value;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
                if(value == AL_TRUE || value == AL_FALSE)
                {
                    Source->WetGainHFAuto = value;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_DIRECT_CHANNELS_SOFT:
                if(value == AL_TRUE || value == AL_FALSE)
                {
                    Source->DirectChannels = value;
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            case AL_DISTANCE_MODEL:
                if(value == AL_NONE ||
                   value == AL_INVERSE_DISTANCE ||
                   value == AL_INVERSE_DISTANCE_CLAMPED ||
                   value == AL_LINEAR_DISTANCE ||
                   value == AL_LINEAR_DISTANCE_CLAMPED ||
                   value == AL_EXPONENT_DISTANCE ||
                   value == AL_EXPONENT_DISTANCE_CLAMPED)
                {
                    Source->DistanceModel = value;
                    if(Context->SourceDistanceModel)
                        Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
        }
    }
    else
        alSetError(Context, AL_INVALID_NAME);

    ALCcontext_DecRef(Context);
}


AL_API void AL_APIENTRY alSource3i(ALuint source, ALenum param, ALint value1, ALint value2, ALint value3)
{
    ALCcontext *Context;
    ALsource   *Source;

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

    if((Source=LookupSource(Context, source)) != NULL)
    {
        ALCdevice *device = Context->Device;

        switch(param)
        {
            case AL_AUXILIARY_SEND_FILTER: {
                ALeffectslot *ALEffectSlot = NULL;
                ALfilter     *ALFilter = NULL;

                LockContext(Context);
                if((ALuint)value2 < device->NumAuxSends &&
                   (value1 == 0 || (ALEffectSlot=LookupEffectSlot(Context, value1)) != NULL) &&
                   (value3 == 0 || (ALFilter=LookupFilter(device, value3)) != NULL))
                {
                    /* Release refcount on the previous slot, and add one for
                     * the new slot */
                    if(ALEffectSlot) IncrementRef(&ALEffectSlot->ref);
                    ALEffectSlot = ExchangePtr((XchgPtr*)&Source->Send[value2].Slot, ALEffectSlot);
                    if(ALEffectSlot) DecrementRef(&ALEffectSlot->ref);

                    if(!ALFilter)
                    {
                        /* Disable filter */
                        Source->Send[value2].WetGain = 1.0f;
                        Source->Send[value2].WetGainHF = 1.0f;
                    }
                    else
                    {
                        Source->Send[value2].WetGain = ALFilter->Gain;
                        Source->Send[value2].WetGainHF = ALFilter->GainHF;
                    }
                    Source->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(Context, AL_INVALID_VALUE);
                UnlockContext(Context);
            }   break;

            default:
                alSetError(Context, AL_INVALID_ENUM);
                break;
        }
    }
    else
        alSetError(Context, AL_INVALID_NAME);

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

    if(values)
    {
        if(LookupSource(Context, source) != NULL)
        {
            switch(param)
            {
                default:
                    alSetError(Context, AL_INVALID_ENUM);
                    break;
            }
        }
        else
            alSetError(Context, AL_INVALID_NAME);
    }
    else
        alSetError(Context, AL_INVALID_VALUE);

    ALCcontext_DecRef(Context);
}


AL_API ALvoid AL_APIENTRY alGetSourcef(ALuint source, ALenum param, ALfloat *value)
{
    ALCcontext  *Context;
    ALsource    *Source;
    ALdouble    Offsets[2];
    ALdouble    updateLen;

    Context = GetContextRef();
    if(!Context) return;

    if(value)
    {
        if((Source=LookupSource(Context, source)) != NULL)
        {
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
                    GetSourceOffset(Source, param, Offsets, updateLen);
                    UnlockContext(Context);
                    *value = (ALfloat)Offsets[0];
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
                    alSetError(Context, AL_INVALID_ENUM);
                    break;
            }
        }
        else
            alSetError(Context, AL_INVALID_NAME);
    }
    else
        alSetError(Context, AL_INVALID_VALUE);

    ALCcontext_DecRef(Context);
}


AL_API ALvoid AL_APIENTRY alGetSource3f(ALuint source, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3)
{
    ALCcontext *Context;
    ALsource   *Source;

    Context = GetContextRef();
    if(!Context) return;

    if(value1 && value2 && value3)
    {
        if((Source=LookupSource(Context, source)) != NULL)
        {
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
                    alSetError(Context, AL_INVALID_ENUM);
                    break;
            }
        }
        else
            alSetError(Context, AL_INVALID_NAME);
    }
    else
        alSetError(Context, AL_INVALID_VALUE);

    ALCcontext_DecRef(Context);
}


AL_API ALvoid AL_APIENTRY alGetSourcefv(ALuint source, ALenum param, ALfloat *values)
{
    ALCcontext  *Context;
    ALsource    *Source;
    ALdouble    Offsets[2];
    ALdouble    updateLen;

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

    if(values)
    {
        if((Source=LookupSource(Context, source)) != NULL)
        {
            switch(param)
            {
                case AL_SAMPLE_RW_OFFSETS_SOFT:
                case AL_BYTE_RW_OFFSETS_SOFT:
                    LockContext(Context);
                    updateLen = (ALdouble)Context->Device->UpdateSize /
                                Context->Device->Frequency;
                    GetSourceOffset(Source, param, Offsets, updateLen);
                    UnlockContext(Context);
                    values[0] = (ALfloat)Offsets[0];
                    values[1] = (ALfloat)Offsets[1];
                    break;

                default:
                    alSetError(Context, AL_INVALID_ENUM);
                    break;
            }
        }
        else
            alSetError(Context, AL_INVALID_NAME);
    }
    else
        alSetError(Context, AL_INVALID_VALUE);

    ALCcontext_DecRef(Context);
}


AL_API ALvoid AL_APIENTRY alGetSourcei(ALuint source, ALenum param, ALint *value)
{
    ALbufferlistitem *BufferList;
    ALCcontext *Context;
    ALsource   *Source;
    ALdouble   Offsets[2];
    ALdouble   updateLen;

    Context = GetContextRef();
    if(!Context) return;

    if(value)
    {
        if((Source=LookupSource(Context, source)) != NULL)
        {
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
                              BufferList->buffer->buffer : 0);
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
                        /* Buffers on a looping source are in a perpetual state
                         * of PENDING, so don't report any as PROCESSED */
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
                    GetSourceOffset(Source, param, Offsets, updateLen);
                    UnlockContext(Context);
                    *value = (ALint)Offsets[0];
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
                    alSetError(Context, AL_INVALID_ENUM);
                    break;
            }
        }
        else
            alSetError(Context, AL_INVALID_NAME);
    }
    else
        alSetError(Context, AL_INVALID_VALUE);

    ALCcontext_DecRef(Context);
}


AL_API void AL_APIENTRY alGetSource3i(ALuint source, ALenum param, ALint *value1, ALint *value2, ALint *value3)
{
    ALCcontext  *Context;
    ALsource    *Source;

    Context = GetContextRef();
    if(!Context) return;

    if(value1 && value2 && value3)
    {
        if((Source=LookupSource(Context, source)) != NULL)
        {
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
                    alSetError(Context, AL_INVALID_ENUM);
                    break;
            }
        }
        else
            alSetError(Context, AL_INVALID_NAME);
    }
    else
        alSetError(Context, AL_INVALID_VALUE);

    ALCcontext_DecRef(Context);
}


AL_API void AL_APIENTRY alGetSourceiv(ALuint source, ALenum param, ALint *values)
{
    ALCcontext  *Context;
    ALsource    *Source;
    ALdouble    offsets[2];
    ALdouble    updateLen;

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

    if(values)
    {
        if((Source=LookupSource(Context, source)) != NULL)
        {
            switch(param)
            {
                case AL_SAMPLE_RW_OFFSETS_SOFT:
                case AL_BYTE_RW_OFFSETS_SOFT:
                    LockContext(Context);
                    updateLen = (ALdouble)Context->Device->UpdateSize /
                                Context->Device->Frequency;
                    GetSourceOffset(Source, param, offsets, updateLen);
                    UnlockContext(Context);
                    values[0] = (ALint)offsets[0];
                    values[1] = (ALint)offsets[1];
                    break;

                default:
                    alSetError(Context, AL_INVALID_ENUM);
                    break;
            }
        }
        else
            alSetError(Context, AL_INVALID_NAME);
    }
    else
        alSetError(Context, AL_INVALID_VALUE);

    ALCcontext_DecRef(Context);
}


AL_API ALvoid AL_APIENTRY alSourcePlay(ALuint source)
{
    alSourcePlayv(1, &source);
}

AL_API ALvoid AL_APIENTRY alSourcePlayv(ALsizei n, const ALuint *sources)
{
    ALCcontext       *Context;
    ALsource         *Source;
    ALsizei          i;

    Context = GetContextRef();
    if(!Context) return;

    if(n < 0)
    {
        alSetError(Context, AL_INVALID_VALUE);
        goto done;
    }
    if(n > 0 && !sources)
    {
        alSetError(Context, AL_INVALID_VALUE);
        goto done;
    }

    // Check that all the Sources are valid
    for(i = 0;i < n;i++)
    {
        if(!LookupSource(Context, sources[i]))
        {
            alSetError(Context, AL_INVALID_NAME);
            goto done;
        }
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
            alSetError(Context, AL_OUT_OF_MEMORY);
            goto done;
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

done:
    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alSourcePause(ALuint source)
{
    alSourcePausev(1, &source);
}

AL_API ALvoid AL_APIENTRY alSourcePausev(ALsizei n, const ALuint *sources)
{
    ALCcontext *Context;
    ALsource *Source;
    ALsizei i;

    Context = GetContextRef();
    if(!Context) return;

    if(n < 0)
    {
        alSetError(Context, AL_INVALID_VALUE);
        goto done;
    }
    if(n > 0 && !sources)
    {
        alSetError(Context, AL_INVALID_VALUE);
        goto done;
    }

    // Check all the Sources are valid
    for(i = 0;i < n;i++)
    {
        if(!LookupSource(Context, sources[i]))
        {
            alSetError(Context, AL_INVALID_NAME);
            goto done;
        }
    }

    LockContext(Context);
    for(i = 0;i < n;i++)
    {
        Source = LookupSource(Context, sources[i]);
        if(Context->DeferUpdates) Source->new_state = AL_PAUSED;
        else SetSourceState(Source, Context, AL_PAUSED);
    }
    UnlockContext(Context);

done:
    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alSourceStop(ALuint source)
{
    alSourceStopv(1, &source);
}

AL_API ALvoid AL_APIENTRY alSourceStopv(ALsizei n, const ALuint *sources)
{
    ALCcontext *Context;
    ALsource *Source;
    ALsizei i;

    Context = GetContextRef();
    if(!Context) return;

    if(n < 0)
    {
        alSetError(Context, AL_INVALID_VALUE);
        goto done;
    }
    if(n > 0 && !sources)
    {
        alSetError(Context, AL_INVALID_VALUE);
        goto done;
    }

    // Check all the Sources are valid
    for(i = 0;i < n;i++)
    {
        if(!LookupSource(Context, sources[i]))
        {
            alSetError(Context, AL_INVALID_NAME);
            goto done;
        }
    }

    LockContext(Context);
    for(i = 0;i < n;i++)
    {
        Source = LookupSource(Context, sources[i]);
        Source->new_state = AL_NONE;
        SetSourceState(Source, Context, AL_STOPPED);
    }
    UnlockContext(Context);

done:
    ALCcontext_DecRef(Context);
}

AL_API ALvoid AL_APIENTRY alSourceRewind(ALuint source)
{
    alSourceRewindv(1, &source);
}

AL_API ALvoid AL_APIENTRY alSourceRewindv(ALsizei n, const ALuint *sources)
{
    ALCcontext *Context;
    ALsource *Source;
    ALsizei i;

    Context = GetContextRef();
    if(!Context) return;

    if(n < 0)
    {
        alSetError(Context, AL_INVALID_VALUE);
        goto done;
    }
    if(n > 0 && !sources)
    {
        alSetError(Context, AL_INVALID_VALUE);
        goto done;
    }

    // Check all the Sources are valid
    for(i = 0;i < n;i++)
    {
        if(!LookupSource(Context, sources[i]))
        {
            alSetError(Context, AL_INVALID_NAME);
            goto done;
        }
    }

    LockContext(Context);
    for(i = 0;i < n;i++)
    {
        Source = LookupSource(Context, sources[i]);
        Source->new_state = AL_NONE;
        SetSourceState(Source, Context, AL_INITIAL);
    }
    UnlockContext(Context);

done:
    ALCcontext_DecRef(Context);
}


AL_API ALvoid AL_APIENTRY alSourceQueueBuffers(ALuint source, ALsizei n, const ALuint *buffers)
{
    ALCcontext *Context;
    ALCdevice *device;
    ALsource *Source;
    ALsizei i;
    ALbufferlistitem *BufferListStart = NULL;
    ALbufferlistitem *BufferList;
    ALbuffer *BufferFmt;

    if(n == 0)
        return;

    Context = GetContextRef();
    if(!Context) return;

    if(n < 0)
    {
        alSetError(Context, AL_INVALID_VALUE);
        goto error;
    }

    // Check that all buffers are valid or zero and that the source is valid

    // Check that this is a valid source
    if((Source=LookupSource(Context, source)) == NULL)
    {
        alSetError(Context, AL_INVALID_NAME);
        goto error;
    }

    LockContext(Context);
    // Check that this is not a STATIC Source
    if(Source->SourceType == AL_STATIC)
    {
        UnlockContext(Context);
        // Invalid Source Type (can't queue on a Static Source)
        alSetError(Context, AL_INVALID_OPERATION);
        goto error;
    }

    device = Context->Device;

    BufferFmt = NULL;

    // Check existing Queue (if any) for a valid Buffers and get its frequency and format
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

    for(i = 0;i < n;i++)
    {
        ALbuffer *buffer = NULL;
        if(buffers[i] && (buffer=LookupBuffer(device, buffers[i])) == NULL)
        {
            UnlockContext(Context);
            alSetError(Context, AL_INVALID_NAME);
            goto error;
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

        // Increment reference counter for buffer
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
            alSetError(Context, AL_INVALID_OPERATION);
            goto error;
        }
        ReadUnlock(&buffer->lock);
    }

    // Change Source Type
    Source->SourceType = AL_STREAMING;

    if(Source->queue == NULL)
        Source->queue = BufferListStart;
    else
    {
        // Find end of queue
        BufferList = Source->queue;
        while(BufferList->next != NULL)
            BufferList = BufferList->next;

        BufferListStart->prev = BufferList;
        BufferList->next = BufferListStart;
    }

    // Update number of buffers in queue
    Source->BuffersInQueue += n;

    UnlockContext(Context);
    ALCcontext_DecRef(Context);
    return;

error:
    while(BufferListStart)
    {
        BufferList = BufferListStart;
        BufferListStart = BufferList->next;

        if(BufferList->buffer)
            DecrementRef(&BufferList->buffer->ref);
        free(BufferList);
    }
    ALCcontext_DecRef(Context);
}


// Implementation assumes that n is the number of buffers to be removed from the queue and buffers is
// an array of buffer IDs that are to be filled with the names of the buffers removed
AL_API ALvoid AL_APIENTRY alSourceUnqueueBuffers(ALuint source, ALsizei n, ALuint *buffers)
{
    ALCcontext *Context;
    ALsource *Source;
    ALsizei i;
    ALbufferlistitem *BufferList;

    if(n == 0)
        return;

    Context = GetContextRef();
    if(!Context) return;

    if(n < 0)
    {
        alSetError(Context, AL_INVALID_VALUE);
        goto done;
    }

    if((Source=LookupSource(Context, source)) == NULL)
    {
        alSetError(Context, AL_INVALID_NAME);
        goto done;
    }

    LockContext(Context);
    if(Source->Looping || Source->SourceType != AL_STREAMING ||
       (ALuint)n > Source->BuffersPlayed)
    {
        UnlockContext(Context);
        // Some buffers can't be unqueue because they have not been processed
        alSetError(Context, AL_INVALID_VALUE);
        goto done;
    }

    for(i = 0;i < n;i++)
    {
        BufferList = Source->queue;
        Source->queue = BufferList->next;
        Source->BuffersInQueue--;
        Source->BuffersPlayed--;

        if(BufferList->buffer)
        {
            // Record name of buffer
            buffers[i] = BufferList->buffer->buffer;
            // Decrement buffer reference counter
            DecrementRef(&BufferList->buffer->ref);
        }
        else
            buffers[i] = 0;

        // Release memory for buffer list item
        free(BufferList);
    }
    if(Source->queue)
        Source->queue->prev = NULL;
    UnlockContext(Context);

done:
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
        Source->Send[i].WetGain = 1.0f;
        Source->Send[i].WetGainHF = 1.0f;
    }

    Source->NeedsUpdate = AL_TRUE;

    Source->HrtfMoving = AL_FALSE;
    Source->HrtfCounter = 0;
}


/*
 * SetSourceState
 *
 * Sets the source's new play state given its current state
 */
ALvoid SetSourceState(ALsource *Source, ALCcontext *Context, ALenum state)
{
    if(state == AL_PLAYING)
    {
        ALbufferlistitem *BufferList;
        ALsizei j, k;

        /* Check that there is a queue containing at least one non-null, non zero length AL Buffer */
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
                    Source->HrtfHistory[j][k] = 0.0f;
                for(k = 0;k < HRIR_LENGTH;k++)
                {
                    Source->HrtfValues[j][k][0] = 0.0f;
                    Source->HrtfValues[j][k][1] = 0.0f;
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
            Source->HrtfMoving = AL_FALSE;
            Source->HrtfCounter = 0;
        }
    }
    else if(state == AL_STOPPED)
    {
        if(Source->state != AL_INITIAL)
        {
            Source->state = AL_STOPPED;
            Source->BuffersPlayed = Source->BuffersInQueue;
            Source->HrtfMoving = AL_FALSE;
            Source->HrtfCounter = 0;
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
            Source->HrtfMoving = AL_FALSE;
            Source->HrtfCounter = 0;
        }
        Source->Offset = -1.0;
    }
}

/*
    GetSourceOffset

    Gets the current playback position in the given Source, in the appropriate format (Bytes, Samples or MilliSeconds)
    The offset is relative to the start of the queue (not the start of the current buffer)
*/
static ALvoid GetSourceOffset(ALsource *Source, ALenum name, ALdouble *offset, ALdouble updateLen)
{
    const ALbufferlistitem *BufferList;
    const ALbuffer         *Buffer = NULL;
    ALuint  BufferFreq = 0;
    ALuint  readPos, writePos;
    ALuint  totalBufferLen;
    ALuint  i;

    // Find the first non-NULL Buffer in the Queue
    BufferList = Source->queue;
    while(BufferList)
    {
        if(BufferList->buffer)
        {
            Buffer = BufferList->buffer;
            BufferFreq = Buffer->Frequency;
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

    // Get Current SamplesPlayed (NOTE : This is the offset into the *current* buffer)
    readPos = Source->position;
    // Add length of any processed buffers in the queue
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
        writePos = readPos + (ALuint)(updateLen*BufferFreq);
    else
        writePos = readPos;

    if(Source->Looping)
    {
        readPos %= totalBufferLen;
        writePos %= totalBufferLen;
    }
    else
    {
        // Wrap positions back to 0
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
            // Take into account the original format of the Buffer
            if(Buffer->OriginalType == UserFmtIMA4)
            {
                ALuint BlockSize = 36 * ChannelsFromFmt(Buffer->FmtChannels);
                ALuint FrameBlockSize = 65;

                // Round down to nearest ADPCM block
                offset[0] = (ALdouble)(readPos / FrameBlockSize * BlockSize);
                if(Source->state != AL_PLAYING)
                    offset[1] = offset[0];
                else
                {
                    // Round up to nearest ADPCM block
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


/*
    ApplyOffset

    Apply a playback offset to the Source.  This function will update the queue (to correctly
    mark buffers as 'pending' or 'processed' depending upon the new offset.
*/
ALboolean ApplyOffset(ALsource *Source)
{
    const ALbufferlistitem *BufferList;
    const ALbuffer         *Buffer;
    ALint bufferLen, totalBufferLen;
    ALint buffersPlayed;
    ALint offset;

    // Get true byte offset
    offset = GetSampleOffset(Source);

    // If the offset is invalid, don't apply it
    if(offset == -1)
        return AL_FALSE;

    // Sort out the queue (pending and processed states)
    BufferList = Source->queue;
    totalBufferLen = 0;
    buffersPlayed = 0;

    while(BufferList)
    {
        Buffer = BufferList->buffer;
        bufferLen = Buffer ? Buffer->SampleLen : 0;

        if(bufferLen <= offset-totalBufferLen)
        {
            // Offset is past this buffer so increment BuffersPlayed
            buffersPlayed++;
        }
        else if(totalBufferLen <= offset)
        {
            // Offset is within this buffer
            Source->BuffersPlayed = buffersPlayed;

            // SW Mixer Positions are in Samples
            Source->position = offset - totalBufferLen;
            Source->position_fraction = 0;
            return AL_TRUE;
        }

        // Increment the TotalBufferSize
        totalBufferLen += bufferLen;

        // Move on to next buffer in the Queue
        BufferList = BufferList->next;
    }
    // Offset is out of range of the buffer queue
    return AL_FALSE;
}


/*
    GetSampleOffset

    Returns the sample offset into the Source's queue (from the Sample, Byte or Millisecond offset
    supplied by the application). This takes into account the fact that the buffer format may have
    been modifed by AL
*/
static ALint GetSampleOffset(ALsource *Source)
{
    const ALbuffer *Buffer = NULL;
    const ALbufferlistitem *BufferList;
    ALint Offset = -1;

    // Find the first non-NULL Buffer in the Queue
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

    // Determine the ByteOffset (and ensure it is block aligned)
    switch(Source->OffsetType)
    {
    case AL_BYTE_OFFSET:
        // Take into consideration the original format
        Offset = (ALint)Source->Offset;
        if(Buffer->OriginalType == UserFmtIMA4)
        {
            // Round down to nearest ADPCM block
            Offset /= 36 * ChannelsFromUserFmt(Buffer->OriginalChannels);
            // Multiply by compression rate (65 sample frames per block)
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
    // Clear Offset
    Source->Offset = -1.0;

    return Offset;
}


ALvoid ReleaseALSources(ALCcontext *Context)
{
    ALsizei pos;
    ALuint j;
    for(pos = 0;pos < Context->SourceMap.size;pos++)
    {
        ALsource *temp = Context->SourceMap.array[pos].value;
        Context->SourceMap.array[pos].value = NULL;

        // For each buffer in the source's queue, decrement its reference counter and remove it
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

        // Release source structure
        FreeThunkEntry(temp->source);
        memset(temp, 0, sizeof(ALsource));
        free(temp);
    }
}
