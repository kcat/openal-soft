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

static ALvoid InitSourceParams(ALsource *pSource);
static ALboolean GetSourceOffset(ALsource *pSource, ALenum eName, ALfloat *pflOffset, ALfloat updateLen);
static ALboolean ApplyOffset(ALsource *pSource);
static ALint GetByteOffset(ALsource *pSource);

DECL_VERIFIER(Source, ALsource, source)
DECL_VERIFIER(Buffer, ALbuffer, buffer)
DECL_VERIFIER(Filter, ALfilter, filter)
DECL_VERIFIER(EffectSlot, ALeffectslot, effectslot)

ALAPI ALvoid ALAPIENTRY alGenSources(ALsizei n,ALuint *sources)
{
    ALCcontext *Context;
    ALCdevice *Device;
    ALsizei i=0;

    Context = GetContextSuspended();
    if(!Context) return;

    if(n > 0)
    {
        Device = Context->Device;

        // Check that enough memory has been allocted in the 'sources' array for n Sources
        if(!IsBadWritePtr((void*)sources, n * sizeof(ALuint)))
        {
            // Check that the requested number of sources can be generated
            if((Context->SourceCount + n) <= Device->MaxNoOfSources)
            {
                ALsource **list = &Context->SourceList;
                while(*list)
                    list = &(*list)->next;

                // Add additional sources to the list (Source->next points to the location for the next Source structure)
                while(i < n)
                {
                    *list = calloc(1, sizeof(ALsource));
                    if(!(*list))
                    {
                        alDeleteSources(i, sources);
                        alSetError(Context, AL_OUT_OF_MEMORY);
                        break;
                    }

                    sources[i] = (ALuint)ALTHUNK_ADDENTRY(*list);
                    (*list)->source = sources[i];

                    InitSourceParams(*list);
                    Context->SourceCount++;
                    i++;

                    list = &(*list)->next;
                }
            }
            else
            {
                // Not enough resources to create the Sources
                alSetError(Context, AL_INVALID_VALUE);
            }
        }
        else
        {
            // Bad pointer
            alSetError(Context, AL_INVALID_VALUE);
        }
    }

    ProcessContext(Context);
}


ALAPI ALvoid ALAPIENTRY alDeleteSources(ALsizei n, const ALuint *sources)
{
    ALCcontext *Context;
    ALCdevice  *Device;
    ALsource *ALSource;
    ALsource **list;
    ALsizei i, j;
    ALbufferlistitem *ALBufferList;
    ALboolean bSourcesValid = AL_TRUE;

    Context = GetContextSuspended();
    if(!Context) return;

    if(n >= 0)
    {
        Device = Context->Device;

        // Check that all Sources are valid (and can therefore be deleted)
        for (i = 0; i < n; i++)
        {
            if(VerifySource(Context->SourceList, sources[i]) == NULL)
            {
                alSetError(Context, AL_INVALID_NAME);
                bSourcesValid = AL_FALSE;
                break;
            }
        }

        if(bSourcesValid)
        {
            // All Sources are valid, and can be deleted
            for(i = 0; i < n; i++)
            {
                // Recheck that the Source is valid, because there could be duplicated Source names
                if((ALSource=VerifySource(Context->SourceList, sources[i])) != NULL)
                {
                    alSourceStop((ALuint)ALSource->source);

                    // For each buffer in the source's queue, decrement its reference counter and remove it
                    while (ALSource->queue != NULL)
                    {
                        ALBufferList = ALSource->queue;
                        // Decrement buffer's reference counter
                        if(ALBufferList->buffer != NULL)
                            ALBufferList->buffer->refcount--;
                        // Update queue to point to next element in list
                        ALSource->queue = ALBufferList->next;
                        // Release memory allocated for buffer list item
                        free(ALBufferList);
                    }

                    for(j = 0;j < MAX_SENDS;++j)
                    {
                        if(ALSource->Send[j].Slot)
                            ALSource->Send[j].Slot->refcount--;
                        ALSource->Send[j].Slot = NULL;
                    }

                    // Decrement Source count
                    Context->SourceCount--;

                    // Remove Source from list of Sources
                    list = &Context->SourceList;
                    while(*list && *list != ALSource)
                        list = &(*list)->next;

                    if(*list)
                        *list = (*list)->next;
                    ALTHUNK_REMOVEENTRY(ALSource->source);

                    memset(ALSource,0,sizeof(ALsource));
                    free(ALSource);
                }
            }
        }
    }
    else
        alSetError(Context, AL_INVALID_VALUE);

    ProcessContext(Context);
}


ALAPI ALboolean ALAPIENTRY alIsSource(ALuint source)
{
    ALCcontext *Context;
    ALboolean  result;

    Context = GetContextSuspended();
    if(!Context) return AL_FALSE;

    result = (VerifySource(Context->SourceList, source) ? AL_TRUE : AL_FALSE);

    ProcessContext(Context);

    return result;
}


ALAPI ALvoid ALAPIENTRY alSourcef(ALuint source, ALenum eParam, ALfloat flValue)
{
    ALCcontext    *pContext;
    ALsource    *pSource;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if((pSource=VerifySource(pContext->SourceList, source)) != NULL)
    {
        switch(eParam)
        {
            case AL_PITCH:
                if(flValue >= 0.0f)
                {
                    pSource->flPitch = flValue;
                    if(pSource->flPitch < 0.001f)
                        pSource->flPitch = 0.001f;
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_CONE_INNER_ANGLE:
                if(flValue >= 0.0f && flValue <= 360.0f)
                {
                    pSource->flInnerAngle = flValue;
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_CONE_OUTER_ANGLE:
                if(flValue >= 0.0f && flValue <= 360.0f)
                {
                    pSource->flOuterAngle = flValue;
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_GAIN:
                if(flValue >= 0.0f)
                {
                    pSource->flGain = flValue;
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_MAX_DISTANCE:
                if(flValue >= 0.0f)
                {
                    pSource->flMaxDistance = flValue;
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_ROLLOFF_FACTOR:
                if(flValue >= 0.0f)
                {
                    pSource->flRollOffFactor = flValue;
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_REFERENCE_DISTANCE:
                if(flValue >= 0.0f)
                {
                    pSource->flRefDistance = flValue;
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_MIN_GAIN:
                if(flValue >= 0.0f && flValue <= 1.0f)
                {
                    pSource->flMinGain = flValue;
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_MAX_GAIN:
                if(flValue >= 0.0f && flValue <= 1.0f)
                {
                    pSource->flMaxGain = flValue;
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_CONE_OUTER_GAIN:
                if(flValue >= 0.0f && flValue <= 1.0f)
                {
                    pSource->flOuterGain = flValue;
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_CONE_OUTER_GAINHF:
                if(flValue >= 0.0f && flValue <= 1.0f)
                {
                    pSource->OuterGainHF = flValue;
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_AIR_ABSORPTION_FACTOR:
                if(flValue >= 0.0f && flValue <= 10.0f)
                {
                    pSource->AirAbsorptionFactor = flValue;
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_ROOM_ROLLOFF_FACTOR:
                if(flValue >= 0.0f && flValue <= 10.0f)
                {
                    pSource->RoomRolloffFactor = flValue;
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_DOPPLER_FACTOR:
                if(flValue >= 0.0f && flValue <= 1.0f)
                {
                    pSource->DopplerFactor = flValue;
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_SEC_OFFSET:
            case AL_SAMPLE_OFFSET:
            case AL_BYTE_OFFSET:
                if(flValue >= 0.0f)
                {
                    pSource->lOffsetType = eParam;

                    // Store Offset (convert Seconds into Milliseconds)
                    if(eParam == AL_SEC_OFFSET)
                        pSource->lOffset = (ALint)(flValue * 1000.0f);
                    else
                        pSource->lOffset = (ALint)flValue;

                    if ((pSource->state == AL_PLAYING) || (pSource->state == AL_PAUSED))
                    {
                        if(ApplyOffset(pSource) == AL_FALSE)
                            alSetError(pContext, AL_INVALID_VALUE);
                    }
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            default:
                alSetError(pContext, AL_INVALID_ENUM);
                break;
        }
    }
    else
    {
        // Invalid Source Name
        alSetError(pContext, AL_INVALID_NAME);
    }

    ProcessContext(pContext);
}


ALAPI ALvoid ALAPIENTRY alSource3f(ALuint source, ALenum eParam, ALfloat flValue1,ALfloat flValue2,ALfloat flValue3)
{
    ALCcontext    *pContext;
    ALsource    *pSource;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if((pSource=VerifySource(pContext->SourceList, source)) != NULL)
    {
        switch(eParam)
        {
            case AL_POSITION:
                pSource->vPosition[0] = flValue1;
                pSource->vPosition[1] = flValue2;
                pSource->vPosition[2] = flValue3;
                pSource->NeedsUpdate = AL_TRUE;
                break;

            case AL_VELOCITY:
                pSource->vVelocity[0] = flValue1;
                pSource->vVelocity[1] = flValue2;
                pSource->vVelocity[2] = flValue3;
                pSource->NeedsUpdate = AL_TRUE;
                break;

            case AL_DIRECTION:
                pSource->vOrientation[0] = flValue1;
                pSource->vOrientation[1] = flValue2;
                pSource->vOrientation[2] = flValue3;
                pSource->NeedsUpdate = AL_TRUE;
                break;

            default:
                alSetError(pContext, AL_INVALID_ENUM);
                break;
        }
    }
    else
        alSetError(pContext, AL_INVALID_NAME);

    ProcessContext(pContext);
}


ALAPI ALvoid ALAPIENTRY alSourcefv(ALuint source, ALenum eParam, const ALfloat *pflValues)
{
    ALCcontext    *pContext;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(pflValues)
    {
        if(VerifySource(pContext->SourceList, source) != NULL)
        {
            switch(eParam)
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
                    alSourcef(source, eParam, pflValues[0]);
                    break;

                case AL_POSITION:
                case AL_VELOCITY:
                case AL_DIRECTION:
                    alSource3f(source, eParam, pflValues[0], pflValues[1], pflValues[2]);
                    break;

                default:
                    alSetError(pContext, AL_INVALID_ENUM);
                    break;
            }
        }
        else
            alSetError(pContext, AL_INVALID_NAME);
    }
    else
        alSetError(pContext, AL_INVALID_VALUE);

    ProcessContext(pContext);
}


ALAPI ALvoid ALAPIENTRY alSourcei(ALuint source,ALenum eParam,ALint lValue)
{
    ALCcontext          *pContext;
    ALsource            *pSource;
    ALbufferlistitem    *pALBufferListItem;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if((pSource=VerifySource(pContext->SourceList, source)) != NULL)
    {
        ALCdevice *device = pContext->Device;

        switch(eParam)
        {
            case AL_MAX_DISTANCE:
            case AL_ROLLOFF_FACTOR:
            case AL_CONE_INNER_ANGLE:
            case AL_CONE_OUTER_ANGLE:
            case AL_REFERENCE_DISTANCE:
                alSourcef(source, eParam, (ALfloat)lValue);
                break;

            case AL_SOURCE_RELATIVE:
                if(lValue == AL_FALSE || lValue == AL_TRUE)
                {
                    pSource->bHeadRelative = (ALboolean)lValue;
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_LOOPING:
                if(lValue == AL_FALSE || lValue == AL_TRUE)
                    pSource->bLooping = (ALboolean)lValue;
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_BUFFER:
                if(pSource->state == AL_STOPPED || pSource->state == AL_INITIAL)
                {
                    ALbuffer *buffer = NULL;

                    if(lValue == 0 ||
                       (buffer=VerifyBuffer(device->BufferList, lValue)) != NULL)
                    {
                        // Remove all elements in the queue
                        while(pSource->queue != NULL)
                        {
                            pALBufferListItem = pSource->queue;
                            pSource->queue = pALBufferListItem->next;
                            // Decrement reference counter for buffer
                            if(pALBufferListItem->buffer)
                                pALBufferListItem->buffer->refcount--;
                            // Release memory for buffer list item
                            free(pALBufferListItem);
                            // Decrement the number of buffers in the queue
                            pSource->BuffersInQueue--;
                        }

                        // Add the buffer to the queue (as long as it is NOT the NULL buffer)
                        if(lValue != 0)
                        {
                            // Source is now in STATIC mode
                            pSource->lSourceType = AL_STATIC;

                            // Add the selected buffer to the queue
                            pALBufferListItem = malloc(sizeof(ALbufferlistitem));
                            pALBufferListItem->buffer = buffer;
                            pALBufferListItem->next = NULL;

                            pSource->queue = pALBufferListItem;
                            pSource->BuffersInQueue = 1;

                            // Increment reference counter for buffer
                            buffer->refcount++;
                        }
                        else
                        {
                            // Source is now in UNDETERMINED mode
                            pSource->lSourceType = AL_UNDETERMINED;
                            pSource->BuffersPlayed = 0;
                        }

                        // Update AL_BUFFER parameter
                        pSource->Buffer = buffer;
                        pSource->NeedsUpdate = AL_TRUE;
                    }
                    else
                        alSetError(pContext, AL_INVALID_VALUE);
                }
                else
                    alSetError(pContext, AL_INVALID_OPERATION);
                break;

            case AL_SOURCE_STATE:
                // Query only
                alSetError(pContext, AL_INVALID_OPERATION);
                break;

            case AL_SEC_OFFSET:
            case AL_SAMPLE_OFFSET:
            case AL_BYTE_OFFSET:
                if(lValue >= 0)
                {
                    pSource->lOffsetType = eParam;

                    // Store Offset (convert Seconds into Milliseconds)
                    if(eParam == AL_SEC_OFFSET)
                        pSource->lOffset = lValue * 1000;
                    else
                        pSource->lOffset = lValue;

                    if(pSource->state == AL_PLAYING || pSource->state == AL_PAUSED)
                    {
                        if(ApplyOffset(pSource) == AL_FALSE)
                            alSetError(pContext, AL_INVALID_VALUE);
                    }
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_DIRECT_FILTER: {
                ALfilter *filter = NULL;

                if(lValue == 0 ||
                   (filter=VerifyFilter(pContext->Device->FilterList, lValue)) != NULL)
                {
                    if(!filter)
                    {
                        pSource->DirectFilter.type = AL_FILTER_NULL;
                        pSource->DirectFilter.filter = 0;
                    }
                    else
                        memcpy(&pSource->DirectFilter, filter, sizeof(*filter));
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
            }   break;

            case AL_DIRECT_FILTER_GAINHF_AUTO:
                if(lValue == AL_TRUE || lValue == AL_FALSE)
                {
                    pSource->DryGainHFAuto = lValue;
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
                if(lValue == AL_TRUE || lValue == AL_FALSE)
                {
                    pSource->WetGainAuto = lValue;
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
                if(lValue == AL_TRUE || lValue == AL_FALSE)
                {
                    pSource->WetGainHFAuto = lValue;
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            case AL_DISTANCE_MODEL:
                if(lValue == AL_NONE ||
                   lValue == AL_INVERSE_DISTANCE ||
                   lValue == AL_INVERSE_DISTANCE_CLAMPED ||
                   lValue == AL_LINEAR_DISTANCE ||
                   lValue == AL_LINEAR_DISTANCE_CLAMPED ||
                   lValue == AL_EXPONENT_DISTANCE ||
                   lValue == AL_EXPONENT_DISTANCE_CLAMPED)
                {
                    pSource->DistanceModel = lValue;
                    if(pContext->SourceDistanceModel)
                        pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
                break;

            default:
                alSetError(pContext, AL_INVALID_ENUM);
                break;
        }
    }
    else
        alSetError(pContext, AL_INVALID_NAME);

    ProcessContext(pContext);
}


ALAPI void ALAPIENTRY alSource3i(ALuint source, ALenum eParam, ALint lValue1, ALint lValue2, ALint lValue3)
{
    ALCcontext *pContext;
    ALsource   *pSource;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if((pSource=VerifySource(pContext->SourceList, source)) != NULL)
    {
        ALCdevice *device = pContext->Device;

        switch (eParam)
        {
            case AL_POSITION:
            case AL_VELOCITY:
            case AL_DIRECTION:
                alSource3f(source, eParam, (ALfloat)lValue1, (ALfloat)lValue2, (ALfloat)lValue3);
                break;

            case AL_AUXILIARY_SEND_FILTER: {
                ALeffectslot *ALEffectSlot = NULL;
                ALfilter     *ALFilter = NULL;

                if((ALuint)lValue2 < device->NumAuxSends &&
                   (lValue1 == 0 ||
                    (ALEffectSlot=VerifyEffectSlot(pContext->EffectSlotList, lValue1)) != NULL) &&
                   (lValue3 == 0 ||
                    (ALFilter=VerifyFilter(device->FilterList, lValue3)) != NULL))
                {
                    /* Release refcount on the previous slot, and add one for
                     * the new slot */
                    if(pSource->Send[lValue2].Slot)
                        pSource->Send[lValue2].Slot->refcount--;
                    pSource->Send[lValue2].Slot = ALEffectSlot;
                    if(pSource->Send[lValue2].Slot)
                        pSource->Send[lValue2].Slot->refcount++;

                    if(!ALFilter)
                    {
                        /* Disable filter */
                        pSource->Send[lValue2].WetFilter.type = 0;
                        pSource->Send[lValue2].WetFilter.filter = 0;
                    }
                    else
                        memcpy(&pSource->Send[lValue2].WetFilter, ALFilter, sizeof(*ALFilter));
                    pSource->NeedsUpdate = AL_TRUE;
                }
                else
                    alSetError(pContext, AL_INVALID_VALUE);
            }    break;

            default:
                alSetError(pContext, AL_INVALID_ENUM);
                break;
        }
    }
    else
        alSetError(pContext, AL_INVALID_NAME);

    ProcessContext(pContext);
}


ALAPI void ALAPIENTRY alSourceiv(ALuint source, ALenum eParam, const ALint* plValues)
{
    ALCcontext    *pContext;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(plValues)
    {
        if(VerifySource(pContext->SourceList, source) != NULL)
        {
            switch(eParam)
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
                    alSourcei(source, eParam, plValues[0]);
                    break;

                case AL_POSITION:
                case AL_VELOCITY:
                case AL_DIRECTION:
                case AL_AUXILIARY_SEND_FILTER:
                    alSource3i(source, eParam, plValues[0], plValues[1], plValues[2]);
                    break;

                default:
                    alSetError(pContext, AL_INVALID_ENUM);
                    break;
            }
        }
        else
            alSetError(pContext, AL_INVALID_NAME);
    }
    else
        alSetError(pContext, AL_INVALID_VALUE);

    ProcessContext(pContext);
}


ALAPI ALvoid ALAPIENTRY alGetSourcef(ALuint source, ALenum eParam, ALfloat *pflValue)
{
    ALCcontext  *pContext;
    ALsource    *pSource;
    ALfloat      flOffset[2];
    ALfloat      updateLen;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(pflValue)
    {
        if((pSource=VerifySource(pContext->SourceList, source)) != NULL)
        {
            switch(eParam)
            {
                case AL_PITCH:
                    *pflValue = pSource->flPitch;
                    break;

                case AL_GAIN:
                    *pflValue = pSource->flGain;
                    break;

                case AL_MIN_GAIN:
                    *pflValue = pSource->flMinGain;
                    break;

                case AL_MAX_GAIN:
                    *pflValue = pSource->flMaxGain;
                    break;

                case AL_MAX_DISTANCE:
                    *pflValue = pSource->flMaxDistance;
                    break;

                case AL_ROLLOFF_FACTOR:
                    *pflValue = pSource->flRollOffFactor;
                    break;

                case AL_CONE_OUTER_GAIN:
                    *pflValue = pSource->flOuterGain;
                    break;

                case AL_CONE_OUTER_GAINHF:
                    *pflValue = pSource->OuterGainHF;
                    break;

                case AL_SEC_OFFSET:
                case AL_SAMPLE_OFFSET:
                case AL_BYTE_OFFSET:
                    updateLen = (ALfloat)pContext->Device->UpdateSize /
                                pContext->Device->Frequency;
                    if(GetSourceOffset(pSource, eParam, flOffset, updateLen))
                        *pflValue = flOffset[0];
                    else
                        alSetError(pContext, AL_INVALID_OPERATION);
                    break;

                case AL_CONE_INNER_ANGLE:
                    *pflValue = pSource->flInnerAngle;
                    break;

                case AL_CONE_OUTER_ANGLE:
                    *pflValue = pSource->flOuterAngle;
                    break;

                case AL_REFERENCE_DISTANCE:
                    *pflValue = pSource->flRefDistance;
                    break;

                case AL_AIR_ABSORPTION_FACTOR:
                    *pflValue = pSource->AirAbsorptionFactor;
                    break;

                case AL_ROOM_ROLLOFF_FACTOR:
                    *pflValue = pSource->RoomRolloffFactor;
                    break;

                case AL_DOPPLER_FACTOR:
                    *pflValue = pSource->DopplerFactor;
                    break;

                default:
                    alSetError(pContext, AL_INVALID_ENUM);
                    break;
            }
        }
        else
            alSetError(pContext, AL_INVALID_NAME);
    }
    else
        alSetError(pContext, AL_INVALID_VALUE);

    ProcessContext(pContext);
}


ALAPI ALvoid ALAPIENTRY alGetSource3f(ALuint source, ALenum eParam, ALfloat* pflValue1, ALfloat* pflValue2, ALfloat* pflValue3)
{
    ALCcontext    *pContext;
    ALsource    *pSource;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(pflValue1 && pflValue2 && pflValue3)
    {
        if((pSource=VerifySource(pContext->SourceList, source)) != NULL)
        {
            switch(eParam)
            {
                case AL_POSITION:
                    *pflValue1 = pSource->vPosition[0];
                    *pflValue2 = pSource->vPosition[1];
                    *pflValue3 = pSource->vPosition[2];
                    break;

                case AL_VELOCITY:
                    *pflValue1 = pSource->vVelocity[0];
                    *pflValue2 = pSource->vVelocity[1];
                    *pflValue3 = pSource->vVelocity[2];
                    break;

                case AL_DIRECTION:
                    *pflValue1 = pSource->vOrientation[0];
                    *pflValue2 = pSource->vOrientation[1];
                    *pflValue3 = pSource->vOrientation[2];
                    break;

                default:
                    alSetError(pContext, AL_INVALID_ENUM);
                    break;
            }
        }
        else
            alSetError(pContext, AL_INVALID_NAME);
    }
    else
        alSetError(pContext, AL_INVALID_VALUE);

    ProcessContext(pContext);
}


ALAPI ALvoid ALAPIENTRY alGetSourcefv(ALuint source, ALenum eParam, ALfloat *pflValues)
{
    ALCcontext  *pContext;
    ALsource    *pSource;
    ALfloat     flOffset[2];
    ALfloat     updateLen;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(pflValues)
    {
        if((pSource=VerifySource(pContext->SourceList, source)) != NULL)
        {
            switch(eParam)
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
                    alGetSourcef(source, eParam, pflValues);
                    break;

                case AL_SAMPLE_RW_OFFSETS_EXT:
                case AL_BYTE_RW_OFFSETS_EXT:
                    updateLen = (ALfloat)pContext->Device->UpdateSize /
                                pContext->Device->Frequency;
                    if(GetSourceOffset(pSource, eParam, flOffset, updateLen))
                    {
                        pflValues[0] = flOffset[0];
                        pflValues[1] = flOffset[1];
                    }
                    else
                        alSetError(pContext, AL_INVALID_OPERATION);
                    break;

                case AL_POSITION:
                    pflValues[0] = pSource->vPosition[0];
                    pflValues[1] = pSource->vPosition[1];
                    pflValues[2] = pSource->vPosition[2];
                    break;

                case AL_VELOCITY:
                    pflValues[0] = pSource->vVelocity[0];
                    pflValues[1] = pSource->vVelocity[1];
                    pflValues[2] = pSource->vVelocity[2];
                    break;

                case AL_DIRECTION:
                    pflValues[0] = pSource->vOrientation[0];
                    pflValues[1] = pSource->vOrientation[1];
                    pflValues[2] = pSource->vOrientation[2];
                    break;

                default:
                    alSetError(pContext, AL_INVALID_ENUM);
                    break;
            }
        }
        else
            alSetError(pContext, AL_INVALID_NAME);
    }
    else
        alSetError(pContext, AL_INVALID_VALUE);

    ProcessContext(pContext);
}


ALAPI ALvoid ALAPIENTRY alGetSourcei(ALuint source, ALenum eParam, ALint *plValue)
{
    ALCcontext *pContext;
    ALsource   *pSource;
    ALfloat     flOffset[2];
    ALfloat     updateLen;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(plValue)
    {
        if((pSource=VerifySource(pContext->SourceList, source)) != NULL)
        {
            switch(eParam)
            {
                case AL_MAX_DISTANCE:
                    *plValue = (ALint)pSource->flMaxDistance;
                    break;

                case AL_ROLLOFF_FACTOR:
                    *plValue = (ALint)pSource->flRollOffFactor;
                    break;

                case AL_REFERENCE_DISTANCE:
                    *plValue = (ALint)pSource->flRefDistance;
                    break;

                case AL_SOURCE_RELATIVE:
                    *plValue = pSource->bHeadRelative;
                    break;

                case AL_CONE_INNER_ANGLE:
                    *plValue = (ALint)pSource->flInnerAngle;
                    break;

                case AL_CONE_OUTER_ANGLE:
                    *plValue = (ALint)pSource->flOuterAngle;
                    break;

                case AL_LOOPING:
                    *plValue = pSource->bLooping;
                    break;

                case AL_BUFFER:
                    *plValue = (pSource->Buffer ? pSource->Buffer->buffer : 0);
                    break;

                case AL_SOURCE_STATE:
                    *plValue = pSource->state;
                    break;

                case AL_BUFFERS_QUEUED:
                    *plValue = pSource->BuffersInQueue;
                    break;

                case AL_BUFFERS_PROCESSED:
                    if(pSource->bLooping)
                    {
                        /* Buffers on a looping source are in a perpetual state
                         * of PENDING, so don't report any as PROCESSED */
                        *plValue = 0;
                    }
                    else
                        *plValue = pSource->BuffersPlayed;
                    break;

                case AL_SOURCE_TYPE:
                    *plValue = pSource->lSourceType;
                    break;

                case AL_SEC_OFFSET:
                case AL_SAMPLE_OFFSET:
                case AL_BYTE_OFFSET:
                    updateLen = (ALfloat)pContext->Device->UpdateSize /
                                pContext->Device->Frequency;
                    if(GetSourceOffset(pSource, eParam, flOffset, updateLen))
                        *plValue = (ALint)flOffset[0];
                    else
                        alSetError(pContext, AL_INVALID_OPERATION);
                    break;

                case AL_DIRECT_FILTER:
                    *plValue = pSource->DirectFilter.filter;
                    break;

                case AL_DIRECT_FILTER_GAINHF_AUTO:
                    *plValue = pSource->DryGainHFAuto;
                    break;

                case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
                    *plValue = pSource->WetGainAuto;
                    break;

                case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
                    *plValue = pSource->WetGainHFAuto;
                    break;

                case AL_DOPPLER_FACTOR:
                    *plValue = (ALint)pSource->DopplerFactor;
                    break;

                case AL_DISTANCE_MODEL:
                    *plValue = pSource->DistanceModel;
                    break;

                default:
                    alSetError(pContext, AL_INVALID_ENUM);
                    break;
            }
        }
        else
            alSetError(pContext, AL_INVALID_NAME);
    }
    else
        alSetError(pContext, AL_INVALID_VALUE);

    ProcessContext(pContext);
}


ALAPI void ALAPIENTRY alGetSource3i(ALuint source, ALenum eParam, ALint* plValue1, ALint* plValue2, ALint* plValue3)
{
    ALCcontext  *pContext;
    ALsource    *pSource;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(plValue1 && plValue2 && plValue3)
    {
        if((pSource=VerifySource(pContext->SourceList, source)) != NULL)
        {
            switch(eParam)
            {
                case AL_POSITION:
                    *plValue1 = (ALint)pSource->vPosition[0];
                    *plValue2 = (ALint)pSource->vPosition[1];
                    *plValue3 = (ALint)pSource->vPosition[2];
                    break;

                case AL_VELOCITY:
                    *plValue1 = (ALint)pSource->vVelocity[0];
                    *plValue2 = (ALint)pSource->vVelocity[1];
                    *plValue3 = (ALint)pSource->vVelocity[2];
                    break;

                case AL_DIRECTION:
                    *plValue1 = (ALint)pSource->vOrientation[0];
                    *plValue2 = (ALint)pSource->vOrientation[1];
                    *plValue3 = (ALint)pSource->vOrientation[2];
                    break;

                default:
                    alSetError(pContext, AL_INVALID_ENUM);
                    break;
            }
        }
        else
            alSetError(pContext, AL_INVALID_NAME);
    }
    else
        alSetError(pContext, AL_INVALID_VALUE);

    ProcessContext(pContext);
}


ALAPI void ALAPIENTRY alGetSourceiv(ALuint source, ALenum eParam, ALint* plValues)
{
    ALCcontext  *pContext;
    ALsource    *pSource;
    ALfloat     flOffset[2];
    ALfloat     updateLen;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(plValues)
    {
        if((pSource=VerifySource(pContext->SourceList, source)) != NULL)
        {
            switch(eParam)
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
                    alGetSourcei(source, eParam, plValues);
                    break;

                case AL_SAMPLE_RW_OFFSETS_EXT:
                case AL_BYTE_RW_OFFSETS_EXT:
                    updateLen = (ALfloat)pContext->Device->UpdateSize /
                                pContext->Device->Frequency;
                    if(GetSourceOffset(pSource, eParam, flOffset, updateLen))
                    {
                        plValues[0] = (ALint)flOffset[0];
                        plValues[1] = (ALint)flOffset[1];
                    }
                    else
                        alSetError(pContext, AL_INVALID_OPERATION);
                    break;

                case AL_POSITION:
                    plValues[0] = (ALint)pSource->vPosition[0];
                    plValues[1] = (ALint)pSource->vPosition[1];
                    plValues[2] = (ALint)pSource->vPosition[2];
                    break;

                case AL_VELOCITY:
                    plValues[0] = (ALint)pSource->vVelocity[0];
                    plValues[1] = (ALint)pSource->vVelocity[1];
                    plValues[2] = (ALint)pSource->vVelocity[2];
                    break;

                case AL_DIRECTION:
                    plValues[0] = (ALint)pSource->vOrientation[0];
                    plValues[1] = (ALint)pSource->vOrientation[1];
                    plValues[2] = (ALint)pSource->vOrientation[2];
                    break;

                default:
                    alSetError(pContext, AL_INVALID_ENUM);
                    break;
            }
        }
        else
            alSetError(pContext, AL_INVALID_NAME);
    }
    else
        alSetError(pContext, AL_INVALID_VALUE);

    ProcessContext(pContext);
}


ALAPI ALvoid ALAPIENTRY alSourcePlay(ALuint source)
{
    alSourcePlayv(1, &source);
}

ALAPI ALvoid ALAPIENTRY alSourcePlayv(ALsizei n, const ALuint *pSourceList)
{
    ALCcontext          *pContext;
    ALsource            *pSource;
    ALbufferlistitem    *ALBufferList;
    ALboolean            bSourcesValid = AL_TRUE;
    ALboolean            bPlay;
    ALsizei              i, j;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(pSourceList)
    {
        // Check that all the Sources are valid
        for(i = 0; i < n; i++)
        {
            if(!VerifySource(pContext->SourceList, pSourceList[i]))
            {
                alSetError(pContext, AL_INVALID_NAME);
                bSourcesValid = AL_FALSE;
                break;
            }
        }

        if(bSourcesValid)
        {
            for(i = 0; i < n; i++)
            {
                // Assume Source won't need to play
                bPlay = AL_FALSE;

                pSource = (ALsource*)ALTHUNK_LOOKUPENTRY(pSourceList[i]);

                // Check that there is a queue containing at least one non-null, non zero length AL Buffer
                ALBufferList = pSource->queue;
                while(ALBufferList)
                {
                    if(ALBufferList->buffer != NULL && ALBufferList->buffer->size)
                    {
                        bPlay = AL_TRUE;
                        break;
                    }
                    ALBufferList = ALBufferList->next;
                }

                if (bPlay)
                {
                    for(j = 0;j < OUTPUTCHANNELS;j++)
                        pSource->DryGains[j] = 0.0f;
                    for(j = 0;j < MAX_SENDS;j++)
                        pSource->WetGains[j] = 0.0f;

                    if(pSource->state != AL_PAUSED)
                    {
                        pSource->state = AL_PLAYING;
                        pSource->position = 0;
                        pSource->position_fraction = 0;
                        pSource->BuffersPlayed = 0;

                        pSource->Buffer = pSource->queue->buffer;
                    }
                    else
                        pSource->state = AL_PLAYING;

                    // Check if an Offset has been set
                    if(pSource->lOffset)
                        ApplyOffset(pSource);

                    if(pSource->BuffersPlayed == 0 && pSource->position == 0 &&
                       pSource->position_fraction == 0)
                        pSource->FirstStart = AL_TRUE;
                    else
                        pSource->FirstStart = AL_FALSE;

                    // If device is disconnected, go right to stopped
                    if(!pContext->Device->Connected)
                    {
                        pSource->state = AL_STOPPED;
                        pSource->BuffersPlayed = pSource->BuffersInQueue;
                        pSource->position = 0;
                        pSource->position_fraction = 0;
                    }
                }
                else
                    pSource->BuffersPlayed = pSource->BuffersInQueue;
            }
        }
    }
    else
    {
        // sources is a NULL pointer
        alSetError(pContext, AL_INVALID_VALUE);
    }

    ProcessContext(pContext);
}

ALAPI ALvoid ALAPIENTRY alSourcePause(ALuint source)
{
    alSourcePausev(1, &source);
}

ALAPI ALvoid ALAPIENTRY alSourcePausev(ALsizei n, const ALuint *sources)
{
    ALCcontext *Context;
    ALsource *Source;
    ALsizei i;
    ALboolean bSourcesValid = AL_TRUE;

    Context = GetContextSuspended();
    if(!Context) return;

    if(sources)
    {
        // Check all the Sources are valid
        for(i=0;i<n;i++)
        {
            if(!VerifySource(Context->SourceList, sources[i]))
            {
                alSetError(Context, AL_INVALID_NAME);
                bSourcesValid = AL_FALSE;
                break;
            }
        }

        if(bSourcesValid)
        {
            for(i = 0;i < n;i++)
            {
                Source = (ALsource*)ALTHUNK_LOOKUPENTRY(sources[i]);
                if(Source->state == AL_PLAYING)
                    Source->state = AL_PAUSED;
            }
        }
    }
    else
    {
        // sources is a NULL pointer
        alSetError(Context, AL_INVALID_VALUE);
    }

    ProcessContext(Context);
}

ALAPI ALvoid ALAPIENTRY alSourceStop(ALuint source)
{
    alSourceStopv(1, &source);
}

ALAPI ALvoid ALAPIENTRY alSourceStopv(ALsizei n, const ALuint *sources)
{
    ALCcontext *Context;
    ALsource *Source;
    ALsizei i;
    ALboolean bSourcesValid = AL_TRUE;

    Context = GetContextSuspended();
    if(!Context) return;

    if(sources)
    {
        // Check all the Sources are valid
        for(i = 0;i < n;i++)
        {
            if(!VerifySource(Context->SourceList, sources[i]))
            {
                alSetError(Context, AL_INVALID_NAME);
                bSourcesValid = AL_FALSE;
                break;
            }
        }

        if(bSourcesValid)
        {
            for(i = 0;i < n;i++)
            {
                Source = (ALsource*)ALTHUNK_LOOKUPENTRY(sources[i]);
                if(Source->state != AL_INITIAL)
                {
                    Source->state = AL_STOPPED;
                    Source->BuffersPlayed = Source->BuffersInQueue;
                }
                Source->lOffset = 0;
            }
        }
    }
    else
    {
        // sources is a NULL pointer
        alSetError(Context, AL_INVALID_VALUE);
    }

    ProcessContext(Context);
}

ALAPI ALvoid ALAPIENTRY alSourceRewind(ALuint source)
{
    alSourceRewindv(1, &source);
}

ALAPI ALvoid ALAPIENTRY alSourceRewindv(ALsizei n, const ALuint *sources)
{
    ALCcontext *Context;
    ALsource *Source;
    ALsizei i;
    ALboolean bSourcesValid = AL_TRUE;

    Context = GetContextSuspended();
    if(!Context) return;

    if(sources)
    {
        // Check all the Sources are valid
        for(i = 0;i < n;i++)
        {
            if(!VerifySource(Context->SourceList, sources[i]))
            {
                alSetError(Context, AL_INVALID_NAME);
                bSourcesValid = AL_FALSE;
                break;
            }
        }

        if(bSourcesValid)
        {
            for(i = 0;i < n;i++)
            {
                Source = (ALsource*)ALTHUNK_LOOKUPENTRY(sources[i]);
                if(Source->state != AL_INITIAL)
                {
                    Source->state = AL_INITIAL;
                    Source->position = 0;
                    Source->position_fraction = 0;
                    Source->BuffersPlayed = 0;
                    if(Source->queue)
                        Source->Buffer = Source->queue->buffer;
                }
                Source->lOffset = 0;
            }
        }
    }
    else
    {
        // sources is a NULL pointer
        alSetError(Context, AL_INVALID_VALUE);
    }

    ProcessContext(Context);
}


ALAPI ALvoid ALAPIENTRY alSourceQueueBuffers( ALuint source, ALsizei n, const ALuint* buffers )
{
    ALCcontext *Context;
    ALsource *ALSource;
    ALsizei i;
    ALbufferlistitem *ALBufferList;
    ALbufferlistitem *ALBufferListStart;
    ALint iFrequency;
    ALint iFormat;
    ALboolean bBuffersValid = AL_TRUE;
    ALboolean hadFormat = AL_FALSE;

    if (n == 0)
        return;

    Context = GetContextSuspended();
    if(!Context) return;

    // Check that all buffers are valid or zero and that the source is valid

    // Check that this is a valid source
    if((ALSource=VerifySource(Context->SourceList, source)) != NULL)
    {
        // Check that this is not a STATIC Source
        if(ALSource->lSourceType != AL_STATIC)
        {
            ALCdevice *device = Context->Device;

            iFrequency = -1;
            iFormat = -1;

            // Check existing Queue (if any) for a valid Buffers and get its frequency and format
            ALBufferList = ALSource->queue;
            while(ALBufferList)
            {
                if (ALBufferList->buffer)
                {
                    iFrequency = ALBufferList->buffer->frequency;
                    iFormat = ALBufferList->buffer->format;
                    hadFormat = AL_TRUE;
                    break;
                }
                ALBufferList = ALBufferList->next;
            }

            for(i = 0; i < n; i++)
            {
                ALbuffer *buffer;

                if(!buffers[i])
                    continue;

                if((buffer=VerifyBuffer(device->BufferList, buffers[i])) == NULL)
                {
                    alSetError(Context, AL_INVALID_NAME);
                    bBuffersValid = AL_FALSE;
                    break;
                }

                if(iFrequency == -1 && iFormat == -1)
                {
                    iFrequency = buffer->frequency;
                    iFormat = buffer->format;
                }
                else if(iFrequency != buffer->frequency ||
                        iFormat != buffer->format)
                {
                    alSetError(Context, AL_INVALID_OPERATION);
                    bBuffersValid = AL_FALSE;
                    break;
                }
            }

            if(bBuffersValid)
            {
                ALbuffer *buffer;

                // Change Source Type
                ALSource->lSourceType = AL_STREAMING;

                buffer = (ALbuffer*)ALTHUNK_LOOKUPENTRY(buffers[0]);

                // All buffers are valid - so add them to the list
                ALBufferListStart = malloc(sizeof(ALbufferlistitem));
                ALBufferListStart->buffer = buffer;
                ALBufferListStart->next = NULL;

                // Increment reference counter for buffer
                if(buffer) buffer->refcount++;

                ALBufferList = ALBufferListStart;

                for(i = 1; i < n; i++)
                {
                    buffer = (ALbuffer*)ALTHUNK_LOOKUPENTRY(buffers[i]);

                    ALBufferList->next = malloc(sizeof(ALbufferlistitem));
                    ALBufferList->next->buffer = buffer;
                    ALBufferList->next->next = NULL;

                    // Increment reference counter for buffer
                    if(buffer) buffer->refcount++;

                    ALBufferList = ALBufferList->next;
                }

                if(ALSource->queue == NULL)
                {
                    ALSource->queue = ALBufferListStart;
                    // Update Current Buffer
                    ALSource->Buffer = ALBufferListStart->buffer;
                }
                else
                {
                    // Find end of queue
                    ALBufferList = ALSource->queue;
                    while(ALBufferList->next != NULL)
                        ALBufferList = ALBufferList->next;

                    ALBufferList->next = ALBufferListStart;
                }

                // Update number of buffers in queue
                ALSource->BuffersInQueue += n;
                // If no previous format, mark the source dirty now that it may
                // have one
                if(!hadFormat)
                    ALSource->NeedsUpdate = AL_TRUE;
            }
        }
        else
        {
            // Invalid Source Type (can't queue on a Static Source)
            alSetError(Context, AL_INVALID_OPERATION);
        }
    }
    else
    {
        // Invalid Source Name
        alSetError(Context, AL_INVALID_NAME);
    }

    ProcessContext(Context);
}


// Implementation assumes that n is the number of buffers to be removed from the queue and buffers is
// an array of buffer IDs that are to be filled with the names of the buffers removed
ALAPI ALvoid ALAPIENTRY alSourceUnqueueBuffers( ALuint source, ALsizei n, ALuint* buffers )
{
    ALCcontext *Context;
    ALsource *ALSource;
    ALsizei i;
    ALbufferlistitem *ALBufferList;

    if (n == 0)
        return;

    Context = GetContextSuspended();
    if(!Context) return;

    if((ALSource=VerifySource(Context->SourceList, source)) != NULL)
    {
        // If all 'n' buffers have been processed, remove them from the queue
        if(!ALSource->bLooping && (ALuint)n <= ALSource->BuffersPlayed)
        {
            for(i = 0; i < n; i++)
            {
                ALBufferList = ALSource->queue;

                ALSource->queue = ALBufferList->next;
                if(ALBufferList->buffer)
                {
                    // Record name of buffer
                    buffers[i] = ALBufferList->buffer->buffer;
                    // Decrement buffer reference counter
                    ALBufferList->buffer->refcount--;
                }
                else
                    buffers[i] = 0;

                // Release memory for buffer list item
                free(ALBufferList);
                ALSource->BuffersInQueue--;
            }

            if(ALSource->state != AL_PLAYING)
            {
                if(ALSource->queue)
                    ALSource->Buffer = ALSource->queue->buffer;
                else
                    ALSource->Buffer = NULL;
            }

            ALSource->BuffersPlayed -= n;
        }
        else
        {
            // Some buffers can't be unqueue because they have not been processed
            alSetError(Context, AL_INVALID_VALUE);
        }
    }
    else
    {
        // Invalid Source Name
        alSetError(Context, AL_INVALID_NAME);
    }

    ProcessContext(Context);
}


static ALvoid InitSourceParams(ALsource *pSource)
{
    pSource->flInnerAngle = 360.0f;
    pSource->flOuterAngle = 360.0f;
    pSource->flPitch = 1.0f;
    pSource->vPosition[0] = 0.0f;
    pSource->vPosition[1] = 0.0f;
    pSource->vPosition[2] = 0.0f;
    pSource->vOrientation[0] = 0.0f;
    pSource->vOrientation[1] = 0.0f;
    pSource->vOrientation[2] = 0.0f;
    pSource->vVelocity[0] = 0.0f;
    pSource->vVelocity[1] = 0.0f;
    pSource->vVelocity[2] = 0.0f;
    pSource->flRefDistance = 1.0f;
    pSource->flMaxDistance = FLT_MAX;
    pSource->flRollOffFactor = 1.0f;
    pSource->bLooping = AL_FALSE;
    pSource->flGain = 1.0f;
    pSource->flMinGain = 0.0f;
    pSource->flMaxGain = 1.0f;
    pSource->flOuterGain = 0.0f;
    pSource->OuterGainHF = 1.0f;

    pSource->DryGainHFAuto = AL_TRUE;
    pSource->WetGainAuto = AL_TRUE;
    pSource->WetGainHFAuto = AL_TRUE;
    pSource->AirAbsorptionFactor = 0.0f;
    pSource->RoomRolloffFactor = 0.0f;
    pSource->DopplerFactor = 1.0f;

    pSource->DistanceModel = AL_INVERSE_DISTANCE_CLAMPED;

    pSource->Resampler = DefaultResampler;

    pSource->state = AL_INITIAL;
    pSource->lSourceType = AL_UNDETERMINED;

    pSource->NeedsUpdate = AL_TRUE;

    pSource->Buffer = NULL;
}


/*
    GetSourceOffset

    Gets the current playback position in the given Source, in the appropriate format (Bytes, Samples or MilliSeconds)
    The offset is relative to the start of the queue (not the start of the current buffer)
*/
static ALboolean GetSourceOffset(ALsource *pSource, ALenum eName, ALfloat *pflOffset, ALfloat updateLen)
{
    ALbufferlistitem *pBufferList;
    ALbuffer         *pBuffer;
    ALfloat        flBufferFreq;
    ALint        lChannels, lBytes;
    ALint        readPos, writePos;
    ALenum        eOriginalFormat;
    ALboolean    bReturn = AL_TRUE;
    ALint        lTotalBufferDataSize;
    ALuint       i;

    if((pSource->state == AL_PLAYING || pSource->state == AL_PAUSED) && pSource->Buffer)
    {
        pBuffer = pSource->Buffer;
        // Get Current Buffer Size and frequency (in milliseconds)
        flBufferFreq = (ALfloat)pBuffer->frequency;
        eOriginalFormat = pBuffer->eOriginalFormat;
        lChannels = aluChannelsFromFormat(pBuffer->format);
        lBytes = aluBytesFromFormat(pBuffer->format);

        // Get Current BytesPlayed
        readPos = pSource->position * lChannels * lBytes; // NOTE : This is the byte offset into the *current* buffer
        // Add byte length of any processed buffers in the queue
        pBufferList = pSource->queue;
        for(i = 0;i < pSource->BuffersPlayed && pBufferList;i++)
        {
            readPos += pBufferList->buffer->size;
            pBufferList = pBufferList->next;
        }

        if(pSource->state == AL_PLAYING)
            writePos = readPos + ((ALuint)(updateLen*flBufferFreq) * lChannels * lBytes);
        else
            writePos = readPos;

        lTotalBufferDataSize = 0;
        pBufferList = pSource->queue;
        while (pBufferList)
        {
            if (pBufferList->buffer)
                lTotalBufferDataSize += pBufferList->buffer->size;
            pBufferList = pBufferList->next;
        }

        if (pSource->bLooping)
        {
            if(readPos < 0)
                readPos = 0;
            else
                readPos %= lTotalBufferDataSize;
            if(writePos < 0)
                writePos = 0;
            else
                writePos %= lTotalBufferDataSize;
        }
        else
        {
            // Clamp BytesPlayed to within 0 and lTotalBufferDataSize
            if(readPos < 0)
                readPos = 0;
            else if(readPos > lTotalBufferDataSize)
                readPos = lTotalBufferDataSize;
            if(writePos < 0)
                writePos = 0;
            else if(writePos > lTotalBufferDataSize)
                writePos = lTotalBufferDataSize;
        }

        switch (eName)
        {
        case AL_SEC_OFFSET:
            pflOffset[0] = (ALfloat)readPos / (lChannels * lBytes * flBufferFreq);
            pflOffset[1] = (ALfloat)writePos / (lChannels * lBytes * flBufferFreq);
            break;
        case AL_SAMPLE_OFFSET:
        case AL_SAMPLE_RW_OFFSETS_EXT:
            pflOffset[0] = (ALfloat)(readPos / (lChannels * lBytes));
            pflOffset[1] = (ALfloat)(writePos / (lChannels * lBytes));
            break;
        case AL_BYTE_OFFSET:
        case AL_BYTE_RW_OFFSETS_EXT:
            // Take into account the original format of the Buffer
            if ((eOriginalFormat == AL_FORMAT_MONO_IMA4) ||
                (eOriginalFormat == AL_FORMAT_STEREO_IMA4))
            {
                // Round down to nearest ADPCM block
                pflOffset[0] = (ALfloat)((readPos / (65 * lBytes * lChannels)) * 36 * lChannels);
                if(pSource->state == AL_PLAYING)
                {
                    // Round up to nearest ADPCM block
                    pflOffset[1] = (ALfloat)(((writePos + (65 * lBytes * lChannels) - 1) / (65 * lBytes * lChannels)) * 36 * lChannels);
                }
                else
                    pflOffset[1] = pflOffset[0];
            }
            else if (eOriginalFormat == AL_FORMAT_REAR8)
            {
                pflOffset[0] = (ALfloat)(readPos / 2 / lBytes * 1);
                pflOffset[1] = (ALfloat)(writePos / 2 / lBytes * 1);
            }
            else if (eOriginalFormat == AL_FORMAT_REAR16)
            {
                pflOffset[0] = (ALfloat)(readPos / 2 / lBytes * 2);
                pflOffset[1] = (ALfloat)(writePos / 2 / lBytes * 2);
            }
            else if (eOriginalFormat == AL_FORMAT_REAR32)
            {
                pflOffset[0] = (ALfloat)(readPos / 2 / lBytes * 4);
                pflOffset[1] = (ALfloat)(writePos / 2 / lBytes * 4);
            }
            else
            {
                ALuint OrigBytes = aluBytesFromFormat(eOriginalFormat);
                pflOffset[0] = (ALfloat)(readPos / lBytes * OrigBytes);
                pflOffset[1] = (ALfloat)(writePos / lBytes * OrigBytes);
            }
            break;
        }
    }
    else
    {
        pflOffset[0] = 0.0f;
        pflOffset[1] = 0.0f;
    }

    return bReturn;
}


/*
    ApplyOffset

    Apply a playback offset to the Source.  This function will update the queue (to correctly
    mark buffers as 'pending' or 'processed' depending upon the new offset.
*/
static ALboolean ApplyOffset(ALsource *pSource)
{
    ALbufferlistitem    *pBufferList;
    ALbuffer            *pBuffer;
    ALint                lBufferSize, lTotalBufferSize;
    ALint                lByteOffset;

    // Get true byte offset
    lByteOffset = GetByteOffset(pSource);

    // If the offset is invalid, don't apply it
    if(lByteOffset == -1)
        return AL_FALSE;

    // Sort out the queue (pending and processed states)
    pBufferList = pSource->queue;
    lTotalBufferSize = 0;
    pSource->BuffersPlayed = 0;

    while(pBufferList)
    {
        pBuffer = pBufferList->buffer;
        lBufferSize = pBuffer ? pBuffer->size : 0;

        if(lTotalBufferSize+lBufferSize <= lByteOffset)
        {
            // Offset is past this buffer so increment BuffersPlayed
            pSource->BuffersPlayed++;
        }
        else if(lTotalBufferSize <= lByteOffset)
        {
            // Offset is within this buffer
            // Set Current Buffer
            pSource->Buffer = pBufferList->buffer;

            // SW Mixer Positions are in Samples
            pSource->position = (lByteOffset - lTotalBufferSize) /
                                aluBytesFromFormat(pBuffer->format) /
                                aluChannelsFromFormat(pBuffer->format);
            break;
        }

        // Increment the TotalBufferSize
        lTotalBufferSize += lBufferSize;

        // Move on to next buffer in the Queue
        pBufferList = pBufferList->next;
    }

    return AL_TRUE;
}


/*
    GetByteOffset

    Returns the 'true' byte offset into the Source's queue (from the Sample, Byte or Millisecond
    offset supplied by the application).   This takes into account the fact that the buffer format
    may have been modifed by AL (e.g 8bit samples are converted to float)
*/
static ALint GetByteOffset(ALsource *pSource)
{
    ALbuffer *pBuffer = NULL;
    ALbufferlistitem *pBufferList;
    ALfloat    flBufferFreq;
    ALint    lChannels, lBytes;
    ALint    lByteOffset = -1;
    ALint    lTotalBufferDataSize;
    ALenum   OriginalFormat;

    // Find the first non-NULL Buffer in the Queue
    pBufferList = pSource->queue;
    while (pBufferList)
    {
        if (pBufferList->buffer)
        {
            pBuffer = pBufferList->buffer;
            break;
        }
        pBufferList = pBufferList->next;
    }

    if (pBuffer)
    {
        flBufferFreq = ((ALfloat)pBuffer->frequency);
        lChannels = aluChannelsFromFormat(pBuffer->format);
        lBytes = aluBytesFromFormat(pBuffer->format);
        OriginalFormat = pBuffer->eOriginalFormat;

        // Determine the ByteOffset (and ensure it is block aligned)
        switch (pSource->lOffsetType)
        {
        case AL_BYTE_OFFSET:
            // Take into consideration the original format
            if(OriginalFormat == AL_FORMAT_MONO_IMA4 ||
               OriginalFormat == AL_FORMAT_STEREO_IMA4)
            {
                // Round down to nearest ADPCM block
                lByteOffset = pSource->lOffset / (36 * lChannels);
                // Multiply by compression rate
                lByteOffset = lByteOffset * 65 * lChannels * lBytes;
                lByteOffset -= (lByteOffset % (lChannels * lBytes));
            }
            else if(OriginalFormat == AL_FORMAT_REAR8)
            {
                lByteOffset = pSource->lOffset / 1 * lBytes * 2;
                lByteOffset -= (lByteOffset % (lChannels * lBytes));
            }
            else if(OriginalFormat == AL_FORMAT_REAR16)
            {
                lByteOffset = pSource->lOffset / 2 * lBytes * 2;
                lByteOffset -= (lByteOffset % (lChannels * lBytes));
            }
            else if(OriginalFormat == AL_FORMAT_REAR32)
            {
                lByteOffset = pSource->lOffset / 4 * lBytes * 2;
                lByteOffset -= (lByteOffset % (lChannels * lBytes));
            }
            else
            {
                ALuint OrigBytes = aluBytesFromFormat(OriginalFormat);
                lByteOffset = pSource->lOffset / OrigBytes * lBytes;
                lByteOffset -= (lByteOffset % (lChannels * lBytes));
            }
            break;

        case AL_SAMPLE_OFFSET:
            lByteOffset = pSource->lOffset * lChannels * lBytes;
            break;

        case AL_SEC_OFFSET:
            // Note - lOffset is internally stored as Milliseconds
            lByteOffset = (ALint)(pSource->lOffset / 1000.0f * flBufferFreq);
            lByteOffset *= lChannels * lBytes;
            break;
        }

        lTotalBufferDataSize = 0;
        pBufferList = pSource->queue;
        while (pBufferList)
        {
            if (pBufferList->buffer)
                lTotalBufferDataSize += pBufferList->buffer->size;
            pBufferList = pBufferList->next;
        }

        // Finally, if the ByteOffset is beyond the length of all the buffers in the queue, return -1
        if (lByteOffset >= lTotalBufferDataSize)
            lByteOffset = -1;
    }

    // Clear Offset
    pSource->lOffset = 0;

    return lByteOffset;
}


ALvoid ReleaseALSources(ALCcontext *Context)
{
    ALuint j;

    while(Context->SourceList)
    {
        ALsource *temp = Context->SourceList;
        Context->SourceList = temp->next;

        // For each buffer in the source's queue, decrement its reference counter and remove it
        while(temp->queue != NULL)
        {
            ALbufferlistitem *ALBufferList = temp->queue;
            // Decrement buffer's reference counter
            if(ALBufferList->buffer != NULL)
                ALBufferList->buffer->refcount--;
            // Update queue to point to next element in list
            temp->queue = ALBufferList->next;
            // Release memory allocated for buffer list item
            free(ALBufferList);
        }

        for(j = 0;j < MAX_SENDS;++j)
        {
            if(temp->Send[j].Slot)
                temp->Send[j].Slot->refcount--;
        }

        // Release source structure
        ALTHUNK_REMOVEENTRY(temp->source);
        memset(temp, 0, sizeof(ALsource));
        free(temp);
    }
    Context->SourceCount = 0;
}
