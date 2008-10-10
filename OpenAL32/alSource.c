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
static ALboolean GetSourceOffset(ALsource *pSource, ALenum eName, ALfloat *pflOffset);
static ALvoid ApplyOffset(ALsource *pSource, ALboolean bUpdateContext);
static ALint GetByteOffset(ALsource *pSource);

ALAPI ALvoid ALAPIENTRY alGenSources(ALsizei n,ALuint *sources)
{
    ALCcontext *Context;
    ALCdevice *Device;
    ALsizei i=0;

    Context = alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        if (n > 0)
        {
            Device = alcGetContextsDevice(Context);

            if (Device)
            {
                // Check that enough memory has been allocted in the 'sources' array for n Sources
                if (!IsBadWritePtr((void*)sources, n * sizeof(ALuint)))
                {
                    // Check that the requested number of sources can be generated
                    if ((Context->SourceCount + n) <= Device->MaxNoOfSources)
                    {
                        ALsource **list = &Context->Source;
                        while(*list)
                            list = &(*list)->next;

                        // Add additional sources to the list (Source->next points to the location for the next Source structure)
                        while(i < n)
                        {
                            *list = calloc(1, sizeof(ALsource));
                            if(!(*list))
                            {
                                alDeleteSources(i, sources);
                                alSetError(AL_OUT_OF_MEMORY);
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
                        alSetError(AL_INVALID_VALUE);
                    }
                }
                else
                {
                    // Bad pointer
                    alSetError(AL_INVALID_VALUE);
                }
            }
            else
            {
                // No Device created, or attached to Context
                alSetError(AL_INVALID_OPERATION);
            }
        }

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return;
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

    Context = alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        if (n >= 0)
        {
            Device = alcGetContextsDevice(Context);

            if (Device)
            {
                // Check that all Sources are valid (and can therefore be deleted)
                for (i = 0; i < n; i++)
                {
                    if (!alIsSource(sources[i]))
                    {
                        alSetError(AL_INVALID_NAME);
                        bSourcesValid = AL_FALSE;
                        break;
                    }
                }

                if (bSourcesValid)
                {
                    // All Sources are valid, and can be deleted
                    for (i = 0; i < n; i++)
                    {
                        // Recheck that the Source is valid, because there could be duplicated Source names
                        if (alIsSource(sources[i]))
                        {
                            ALSource=((ALsource *)ALTHUNK_LOOKUPENTRY(sources[i]));
                            alSourceStop((ALuint)ALSource->source);

                            // For each buffer in the source's queue, decrement its reference counter and remove it
                            while (ALSource->queue != NULL)
                            {
                                ALBufferList = ALSource->queue;
                                // Decrement buffer's reference counter
                                if (ALBufferList->buffer != 0)
                                    ((ALbuffer*)(ALTHUNK_LOOKUPENTRY(ALBufferList->buffer)))->refcount--;
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
                            list = &Context->Source;
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
            {
                // No Device created, or attached to Context
                alSetError(AL_INVALID_OPERATION);
            }
        }
        else
            alSetError(AL_INVALID_VALUE);

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return;
}


ALAPI ALboolean ALAPIENTRY alIsSource(ALuint source)
{
    ALboolean result=AL_FALSE;
    ALCcontext *Context;
    ALsource *Source;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        // To determine if this is a valid Source name, look through the list of generated Sources
        Source = Context->Source;
        while(Source)
        {
            if (Source == (ALsource*)ALTHUNK_LOOKUPENTRY(source))
            {
                result = AL_TRUE;
                break;
            }

            Source = Source->next;
        }

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return result;
}


ALAPI ALvoid ALAPIENTRY alSourcef(ALuint source, ALenum eParam, ALfloat flValue)
{
    ALCcontext    *pContext;
    ALsource    *pSource;

    pContext = alcGetCurrentContext();
    if (pContext)
    {
        SuspendContext(pContext);

        if (alIsSource(source))
        {
            pSource = ((ALsource *)ALTHUNK_LOOKUPENTRY(source));

            switch (eParam)
            {
            case AL_PITCH:
                if (flValue >= 0.0f)
                {
                    pSource->flPitch = flValue;
                    if(pSource->flPitch < 0.001f)
                        pSource->flPitch = 0.001f;
                }
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_CONE_INNER_ANGLE:
                if ((flValue >= 0.0f) && (flValue <= 360.0f))
                    pSource->flInnerAngle = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_CONE_OUTER_ANGLE:
                if ((flValue >= 0.0f) && (flValue <= 360.0f))
                    pSource->flOuterAngle = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_GAIN:
                if (flValue >= 0.0f)
                    pSource->flGain = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_MAX_DISTANCE:
                if (flValue >= 0.0f)
                    pSource->flMaxDistance = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_ROLLOFF_FACTOR:
                if (flValue >= 0.0f)
                    pSource->flRollOffFactor = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_REFERENCE_DISTANCE:
                if (flValue >= 0.0f)
                    pSource->flRefDistance = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_MIN_GAIN:
                if ((flValue >= 0.0f) && (flValue <= 1.0f))
                    pSource->flMinGain = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_MAX_GAIN:
                if ((flValue >= 0.0f) && (flValue <= 1.0f))
                    pSource->flMaxGain = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_CONE_OUTER_GAIN:
                if ((flValue >= 0.0f) && (flValue <= 1.0f))
                    pSource->flOuterGain = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_CONE_OUTER_GAINHF:
                if ((flValue >= 0.0f) && (flValue <= 1.0f))
                    pSource->OuterGainHF = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_AIR_ABSORPTION_FACTOR:
                if (flValue >= 0.0f && flValue <= 10.0f)
                    pSource->AirAbsorptionFactor = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_ROOM_ROLLOFF_FACTOR:
                if (flValue >= 0.0f && flValue <= 1.0f)
                    pSource->RoomRolloffFactor = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_DOPPLER_FACTOR:
                if (flValue >= 0.0f && flValue <= 1.0f)
                    pSource->DopplerFactor = flValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_SEC_OFFSET:
            case AL_SAMPLE_OFFSET:
            case AL_BYTE_OFFSET:
                if (flValue >= 0.0f)
                {
                    pSource->lOffsetType = eParam;

                    // Store Offset (convert Seconds into Milliseconds)
                    if (eParam == AL_SEC_OFFSET)
                        pSource->lOffset = (ALint)(flValue * 1000.0f);
                    else
                        pSource->lOffset = (ALint)flValue;

                    if ((pSource->state == AL_PLAYING) || (pSource->state == AL_PAUSED))
                        ApplyOffset(pSource, AL_TRUE);
                }
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            default:
                alSetError(AL_INVALID_ENUM);
                break;
            }
        }
        else
        {
            // Invalid Source Name
            alSetError(AL_INVALID_NAME);
        }

        ProcessContext(pContext);
    }
    else
    {
        // Invalid context
        alSetError(AL_INVALID_OPERATION);
    }

    return;
}


ALAPI ALvoid ALAPIENTRY alSource3f(ALuint source, ALenum eParam, ALfloat flValue1,ALfloat flValue2,ALfloat flValue3)
{
    ALCcontext    *pContext;
    ALsource    *pSource;

    pContext = alcGetCurrentContext();
    if (pContext)
    {
        SuspendContext(pContext);

        if (alIsSource(source))
        {
            pSource = ((ALsource *)ALTHUNK_LOOKUPENTRY(source));
            switch(eParam)
            {
            case AL_POSITION:
                pSource->vPosition[0] = flValue1;
                pSource->vPosition[1] = flValue2;
                pSource->vPosition[2] = flValue3;
                break;

            case AL_VELOCITY:
                pSource->vVelocity[0] = flValue1;
                pSource->vVelocity[1] = flValue2;
                pSource->vVelocity[2] = flValue3;
                break;

            case AL_DIRECTION:
                pSource->vOrientation[0] = flValue1;
                pSource->vOrientation[1] = flValue2;
                pSource->vOrientation[2] = flValue3;
                break;

            default:
                alSetError(AL_INVALID_ENUM);
                break;
            }
        }
        else
            alSetError(AL_INVALID_NAME);

        ProcessContext(pContext);
    }
    else
    {
        alSetError(AL_INVALID_OPERATION);
    }

    return;
}


ALAPI ALvoid ALAPIENTRY alSourcefv(ALuint source, ALenum eParam, const ALfloat *pflValues)
{
    ALCcontext    *pContext;

    pContext = alcGetCurrentContext();
    if (pContext)
    {
        SuspendContext(pContext);

        if (pflValues)
        {
            if (alIsSource(source))
            {
                switch (eParam)
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
                    alSetError(AL_INVALID_ENUM);
                    break;
                }
            }
            else
                alSetError(AL_INVALID_NAME);
        }
        else
            alSetError(AL_INVALID_VALUE);

        ProcessContext(pContext);
    }
    else
        alSetError(AL_INVALID_OPERATION);

    return;
}


ALAPI ALvoid ALAPIENTRY alSourcei(ALuint source,ALenum eParam,ALint lValue)
{
    ALCcontext          *pContext;
    ALsource            *pSource;
    ALbufferlistitem    *pALBufferListItem;
    ALint                Counter = 0;
    ALint                DataSize = 0;
    ALint                BufferSize;

    pContext = alcGetCurrentContext();
    if (pContext)
    {
        SuspendContext(pContext);

        if (alIsSource(source))
        {
            pSource = ((ALsource *)ALTHUNK_LOOKUPENTRY(source));

            switch(eParam)
            {
            case AL_MAX_DISTANCE:
            case AL_ROLLOFF_FACTOR:
            case AL_REFERENCE_DISTANCE:
                alSourcef(source, eParam, (ALfloat)lValue);
                break;

            case AL_SOURCE_RELATIVE:
                if ((lValue == AL_FALSE) || (lValue == AL_TRUE))
                    pSource->bHeadRelative = (ALboolean)lValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_CONE_INNER_ANGLE:
                if ((lValue >= 0) && (lValue <= 360))
                    pSource->flInnerAngle = (float)lValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_CONE_OUTER_ANGLE:
                if ((lValue >= 0) && (lValue <= 360))
                    pSource->flOuterAngle = (float)lValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_LOOPING:
                if ((lValue == AL_FALSE) || (lValue == AL_TRUE))
                    pSource->bLooping = (ALboolean)lValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_BUFFER:
                if ((pSource->state == AL_STOPPED) || (pSource->state == AL_INITIAL))
                {
                    if (alIsBuffer(lValue))
                    {
                        // Remove all elements in the queue
                        while (pSource->queue != NULL)
                        {
                            pALBufferListItem = pSource->queue;
                            pSource->queue = pALBufferListItem->next;
                            // Decrement reference counter for buffer
                            if (pALBufferListItem->buffer)
                                ((ALbuffer*)(ALTHUNK_LOOKUPENTRY(pALBufferListItem->buffer)))->refcount--;
                            // Record size of buffer
                            BufferSize = ((ALbuffer*)ALTHUNK_LOOKUPENTRY(pALBufferListItem->buffer))->size;
                            DataSize += BufferSize;
                            // Increment the number of buffers removed from queue
                            Counter++;
                            // Release memory for buffer list item
                            free(pALBufferListItem);
                            // Decrement the number of buffers in the queue
                            pSource->BuffersInQueue--;
                        }

                        // Add the buffer to the queue (as long as it is NOT the NULL buffer)
                        if (lValue != 0)
                        {
                            // Source is now in STATIC mode
                            pSource->lSourceType = AL_STATIC;

                            // Add the selected buffer to the queue
                            pALBufferListItem = malloc(sizeof(ALbufferlistitem));
                            pALBufferListItem->buffer = lValue;
                            pALBufferListItem->bufferstate = PENDING;
                            pALBufferListItem->flag = 0;
                            pALBufferListItem->next = NULL;

                            pSource->queue = pALBufferListItem;
                            pSource->BuffersInQueue = 1;

                            DataSize = ((ALbuffer*)ALTHUNK_LOOKUPENTRY(lValue))->size;

                            // Increment reference counter for buffer
                            ((ALbuffer*)(ALTHUNK_LOOKUPENTRY(lValue)))->refcount++;
                        }
                        else
                        {
                            // Source is now in UNDETERMINED mode
                            pSource->lSourceType = AL_UNDETERMINED;
                        }

                        // Update AL_BUFFER parameter
                        pSource->ulBufferID = lValue;
                    }
                    else
                        alSetError(AL_INVALID_VALUE);
                }
                else
                    alSetError(AL_INVALID_OPERATION);
                break;

            case AL_SOURCE_STATE:
                // Query only
                alSetError(AL_INVALID_OPERATION);
                break;

            case AL_SEC_OFFSET:
            case AL_SAMPLE_OFFSET:
            case AL_BYTE_OFFSET:
                if (lValue >= 0)
                {
                    pSource->lOffsetType = eParam;

                    // Store Offset (convert Seconds into Milliseconds)
                    if (eParam == AL_SEC_OFFSET)
                        pSource->lOffset = lValue * 1000;
                    else
                        pSource->lOffset = lValue;

                    if ((pSource->state == AL_PLAYING) || (pSource->state == AL_PAUSED))
                        ApplyOffset(pSource, AL_TRUE);
                }
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_DIRECT_FILTER:
                if(alIsFilter(lValue))
                {
                    ALfilter *filter = (ALfilter*)ALTHUNK_LOOKUPENTRY(lValue);
                    if(!filter)
                    {
                        pSource->DirectFilter.type = AL_FILTER_NULL;
                        pSource->DirectFilter.filter = 0;
                    }
                    else
                        memcpy(&pSource->DirectFilter, filter, sizeof(*filter));
                }
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_DIRECT_FILTER_GAINHF_AUTO:
                if(lValue == AL_TRUE || lValue == AL_FALSE)
                    pSource->DryGainHFAuto = lValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_AUXILIARY_SEND_FILTER_GAIN_AUTO:
                if(lValue == AL_TRUE || lValue == AL_FALSE)
                    pSource->WetGainAuto = lValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            case AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO:
                if(lValue == AL_TRUE || lValue == AL_FALSE)
                    pSource->WetGainHFAuto = lValue;
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            default:
                alSetError(AL_INVALID_ENUM);
                break;
            }
        }
        else
            alSetError(AL_INVALID_NAME);

        ProcessContext(pContext);
    }
    else
        alSetError(AL_INVALID_OPERATION);

    return;
}


ALAPI void ALAPIENTRY alSource3i(ALuint source, ALenum eParam, ALint lValue1, ALint lValue2, ALint lValue3)
{
    ALCcontext    *pContext;

    pContext = alcGetCurrentContext();
    if (pContext)
    {
        SuspendContext(pContext);

        if (alIsSource(source))
        {
            ALsource *pSource = ((ALsource *)ALTHUNK_LOOKUPENTRY(source));

            switch (eParam)
            {
            case AL_POSITION:
            case AL_VELOCITY:
            case AL_DIRECTION:
                alSource3f(source, eParam, (ALfloat)lValue1, (ALfloat)lValue2, (ALfloat)lValue3);
                break;

            case AL_AUXILIARY_SEND_FILTER:
                if(lValue2 >= 0 && lValue2 < MAX_SENDS &&
                   (alIsAuxiliaryEffectSlot(lValue1) || lValue1 == 0) &&
                   alIsFilter(lValue3))
                {
                    ALeffectslot *ALEffectSlot = (ALeffectslot*)ALTHUNK_LOOKUPENTRY(lValue1);
                    ALfilter     *ALFilter = (ALfilter*)ALTHUNK_LOOKUPENTRY(lValue3);

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
                }
                else
                    alSetError(AL_INVALID_VALUE);
                break;

            default:
                alSetError(AL_INVALID_ENUM);
                break;
            }
        }
        else
            alSetError(AL_INVALID_NAME);

        ProcessContext(pContext);
    }
    else
        alSetError(AL_INVALID_OPERATION);

    return;
}


ALAPI void ALAPIENTRY alSourceiv(ALuint source, ALenum eParam, const ALint* plValues)
{
    ALCcontext    *pContext;

    pContext = alcGetCurrentContext();
    if (pContext)
    {
        SuspendContext(pContext);

        if (plValues)
        {
            if (alIsSource(source))
            {
                switch (eParam)
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
                    alSourcei(source, eParam, plValues[0]);
                    break;

                case AL_POSITION:
                case AL_VELOCITY:
                case AL_DIRECTION:
                case AL_AUXILIARY_SEND_FILTER:
                    alSource3i(source, eParam, plValues[0], plValues[1], plValues[2]);
                    break;

                default:
                    alSetError(AL_INVALID_ENUM);
                    break;
                }
            }
            else
                alSetError(AL_INVALID_NAME);
        }
        else
            alSetError(AL_INVALID_VALUE);

        ProcessContext(pContext);
    }
    else
        alSetError(AL_INVALID_OPERATION);

    return;
}


ALAPI ALvoid ALAPIENTRY alGetSourcef(ALuint source, ALenum eParam, ALfloat *pflValue)
{
    ALCcontext    *pContext;
    ALsource    *pSource;
    ALfloat        flOffset;

    pContext = alcGetCurrentContext();
    if (pContext)
    {
        SuspendContext(pContext);

        if (pflValue)
        {
            if (alIsSource(source))
            {
                pSource = ((ALsource *)ALTHUNK_LOOKUPENTRY(source));

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
                    if (GetSourceOffset(pSource, eParam, &flOffset))
                        *pflValue = flOffset;
                    else
                        alSetError(AL_INVALID_OPERATION);
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
                    alSetError(AL_INVALID_ENUM);
                    break;
                }
            }
            else
                alSetError(AL_INVALID_NAME);
        }
        else
            alSetError(AL_INVALID_VALUE);

        ProcessContext(pContext);
    }
    else
        alSetError(AL_INVALID_OPERATION);

    return;
}


ALAPI ALvoid ALAPIENTRY alGetSource3f(ALuint source, ALenum eParam, ALfloat* pflValue1, ALfloat* pflValue2, ALfloat* pflValue3)
{
    ALCcontext    *pContext;
    ALsource    *pSource;

    pContext = alcGetCurrentContext();
    if (pContext)
    {
        SuspendContext(pContext);

        if ((pflValue1) && (pflValue2) && (pflValue3))
        {
            if (alIsSource(source))
            {
                pSource = ((ALsource *)ALTHUNK_LOOKUPENTRY(source));

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
                    alSetError(AL_INVALID_ENUM);
                    break;
                }
            }
            else
                alSetError(AL_INVALID_NAME);
        }
        else
            alSetError(AL_INVALID_VALUE);

        ProcessContext(pContext);
    }
    else
        alSetError(AL_INVALID_OPERATION);

    return;
}


ALAPI ALvoid ALAPIENTRY alGetSourcefv(ALuint source, ALenum eParam, ALfloat *pflValues)
{
    ALCcontext    *pContext;
    ALsource    *pSource;

    pContext = alcGetCurrentContext();
    if (pContext)
    {
        SuspendContext(pContext);

        if (pflValues)
        {
            if (alIsSource(source))
            {
                pSource = ((ALsource *)ALTHUNK_LOOKUPENTRY(source));

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
                    alSetError(AL_INVALID_ENUM);
                    break;
                }
            }
            else
                alSetError(AL_INVALID_NAME);
        }
        else
            alSetError(AL_INVALID_VALUE);

        ProcessContext(pContext);
    }
    else
        alSetError(AL_INVALID_OPERATION);

    return;
}


ALAPI ALvoid ALAPIENTRY alGetSourcei(ALuint source, ALenum eParam, ALint *plValue)
{
    ALCcontext *pContext;
    ALsource   *pSource;
    ALfloat     flOffset;

    pContext = alcGetCurrentContext();
    if (pContext)
    {
        SuspendContext(pContext);

        if (plValue)
        {
            if (alIsSource(source))
            {
                pSource = ((ALsource *)ALTHUNK_LOOKUPENTRY(source));

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
                    *plValue = pSource->ulBufferID;
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
                    if (GetSourceOffset(pSource, eParam, &flOffset))
                        *plValue = (ALint)flOffset;
                    else
                        alSetError(AL_INVALID_OPERATION);
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

                default:
                    alSetError(AL_INVALID_ENUM);
                    break;
                }
            }
            else
                alSetError(AL_INVALID_NAME);
        }
        else
            alSetError(AL_INVALID_VALUE);

        ProcessContext(pContext);
    }
    else
        alSetError(AL_INVALID_OPERATION);

    return;
}


ALAPI void ALAPIENTRY alGetSource3i(ALuint source, ALenum eParam, ALint* plValue1, ALint* plValue2, ALint* plValue3)
{
    ALCcontext  *pContext;
    ALsource    *pSource;

    pContext = alcGetCurrentContext();
    if (pContext)
    {
        SuspendContext(pContext);

        if ((plValue1) && (plValue2) && (plValue3))
        {
            if (alIsSource(source))
            {
                pSource = ((ALsource *)ALTHUNK_LOOKUPENTRY(source));

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
                    alSetError(AL_INVALID_ENUM);
                    break;
                }
            }
            else
                alSetError(AL_INVALID_NAME);
        }
        else
            alSetError(AL_INVALID_VALUE);

        ProcessContext(pContext);
    }
    else
        alSetError(AL_INVALID_OPERATION);

    return;
}


ALAPI void ALAPIENTRY alGetSourceiv(ALuint source, ALenum eParam, ALint* plValues)
{
    ALCcontext  *pContext;
    ALsource    *pSource;

    pContext = alcGetCurrentContext();
    if (pContext)
    {
        SuspendContext(pContext);

        if (plValues)
        {
            if (alIsSource(source))
            {
                pSource = ((ALsource *)ALTHUNK_LOOKUPENTRY(source));

                switch (eParam)
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
                    alGetSourcei(source, eParam, plValues);
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
                    alSetError(AL_INVALID_ENUM);
                    break;
                }
            }
            else
                alSetError(AL_INVALID_NAME);
        }
        else
            alSetError(AL_INVALID_VALUE);

        ProcessContext(pContext);
    }
    else
        alSetError(AL_INVALID_OPERATION);

    return;
}


ALAPI ALvoid ALAPIENTRY alSourcePlay(ALuint source)
{
    alSourcePlayv(1, &source);
    return;
}

ALAPI ALvoid ALAPIENTRY alSourcePlayv(ALsizei n, const ALuint *pSourceList)
{
    ALCcontext          *pContext;
    ALsource            *pSource;
    ALbufferlistitem    *ALBufferList;
    ALboolean            bSourcesValid = AL_TRUE;
    ALboolean            bPlay;
    ALsizei              i, j;

    pContext = alcGetCurrentContext();
    if (pContext)
    {
        SuspendContext(pContext);

        if (pSourceList)
        {
            // Check that all the Sources are valid
            for (i = 0; i < n; i++)
            {
                if (!alIsSource(pSourceList[i]))
                {
                    alSetError(AL_INVALID_NAME);
                    bSourcesValid = AL_FALSE;
                    break;
                }
            }

            if (bSourcesValid)
            {
                for (i = 0; i < n; i++)
                {
                    // Assume Source won't need to play
                    bPlay = AL_FALSE;

                    pSource = ((ALsource*)ALTHUNK_LOOKUPENTRY(pSourceList[i]));

                    // Check that there is a queue containing at least one non-null, non zero length AL Buffer
                    ALBufferList = pSource->queue;
                    while (ALBufferList)
                    {
                        if ((ALBufferList->buffer != 0) && (((ALbuffer*)ALTHUNK_LOOKUPENTRY(ALBufferList->buffer))->size))
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
                        pSource->WetGain = 0.0f;

                        if (pSource->state != AL_PAUSED)
                        {
                            pSource->state = AL_PLAYING;
                            pSource->inuse = AL_TRUE;
                            pSource->play = AL_TRUE;
                            pSource->position = 0;
                            pSource->position_fraction = 0;
                            pSource->BuffersPlayed = 0;

                            pSource->ulBufferID = pSource->queue->buffer;

                            // Make sure all the Buffers in the queue are marked as PENDING
                            ALBufferList = pSource->queue;
                            while (ALBufferList)
                            {
                                ALBufferList->bufferstate = PENDING;
                                ALBufferList = ALBufferList->next;
                            }
                        }
                        else
                        {
                            pSource->state = AL_PLAYING;
                            pSource->inuse = AL_TRUE;
                            pSource->play = AL_TRUE;
                        }

                        // Check if an Offset has been set
                        if (pSource->lOffset)
                            ApplyOffset(pSource, AL_FALSE);
                    }
                    else
                    {
                        // If there is a queue (must all be NULL or Zero length Buffers) mark them all as processed
                        ALBufferList = pSource->queue;
                        while (ALBufferList)
                        {
                            ALBufferList->bufferstate = PROCESSED;
                            ALBufferList = ALBufferList->next;
                        }

                        pSource->BuffersPlayed = pSource->BuffersInQueue;
                    }
                }
            }
        }
        else
        {
            // sources is a NULL pointer
            alSetError(AL_INVALID_VALUE);
        }

        ProcessContext(pContext);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return;
}

ALAPI ALvoid ALAPIENTRY alSourcePause(ALuint source)
{
    alSourcePausev(1, &source);
    return;
}

ALAPI ALvoid ALAPIENTRY alSourcePausev(ALsizei n, const ALuint *sources)
{
    ALCcontext *Context;
    ALsource *Source;
    ALsizei i;
    ALboolean bSourcesValid = AL_TRUE;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        if (sources)
        {
            // Check all the Sources are valid
            for (i=0;i<n;i++)
            {
                if (!alIsSource(sources[i]))
                {
                    alSetError(AL_INVALID_NAME);
                    bSourcesValid = AL_FALSE;
                    break;
                }
            }

            if (bSourcesValid)
            {
                for (i=0;i<n;i++)
                {
                    Source=((ALsource *)ALTHUNK_LOOKUPENTRY(sources[i]));
                    if (Source->state==AL_PLAYING)
                    {
                        Source->state=AL_PAUSED;
                        Source->inuse=AL_FALSE;
                    }
                }
            }
        }
        else
        {
            // sources is a NULL pointer
            alSetError(AL_INVALID_VALUE);
        }

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return;
}

ALAPI ALvoid ALAPIENTRY alSourceStop(ALuint source)
{
    alSourceStopv(1, &source);
    return;
}

ALAPI ALvoid ALAPIENTRY alSourceStopv(ALsizei n, const ALuint *sources)
{
    ALCcontext *Context;
    ALsource *Source;
    ALsizei i;
    ALbufferlistitem *ALBufferListItem;
    ALboolean bSourcesValid = AL_TRUE;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        if (sources)
        {
            // Check all the Sources are valid
            for (i=0;i<n;i++)
            {
                if (!alIsSource(sources[i]))
                {
                    alSetError(AL_INVALID_NAME);
                    bSourcesValid = AL_FALSE;
                    break;
                }
            }

            if (bSourcesValid)
            {
                for (i=0;i<n;i++)
                {
                    Source=((ALsource *)ALTHUNK_LOOKUPENTRY(sources[i]));
                    if (Source->state!=AL_INITIAL)
                    {
                        Source->state=AL_STOPPED;
                        Source->inuse=AL_FALSE;
                        Source->BuffersPlayed = Source->BuffersInQueue;
                        ALBufferListItem= Source->queue;
                        while (ALBufferListItem != NULL)
                        {
                            ALBufferListItem->bufferstate = PROCESSED;
                            ALBufferListItem = ALBufferListItem->next;
                        }
                    }
                    Source->lOffset = 0;
                }
            }
        }
        else
        {
            // sources is a NULL pointer
            alSetError(AL_INVALID_VALUE);
        }

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return;
}

ALAPI ALvoid ALAPIENTRY alSourceRewind(ALuint source)
{
    alSourceRewindv(1, &source);
    return;
}

ALAPI ALvoid ALAPIENTRY alSourceRewindv(ALsizei n, const ALuint *sources)
{
    ALCcontext *Context;
    ALsource *Source;
    ALsizei i;
    ALbufferlistitem *ALBufferListItem;
    ALboolean bSourcesValid = AL_TRUE;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        if (sources)
        {
            // Check all the Sources are valid
            for (i=0;i<n;i++)
            {
                if (!alIsSource(sources[i]))
                {
                    alSetError(AL_INVALID_NAME);
                    bSourcesValid = AL_FALSE;
                    break;
                }
            }

            if (bSourcesValid)
            {
                for (i=0;i<n;i++)
                {
                    Source=((ALsource *)ALTHUNK_LOOKUPENTRY(sources[i]));
                    if (Source->state!=AL_INITIAL)
                    {
                        Source->state=AL_INITIAL;
                        Source->inuse=AL_FALSE;
                        Source->position=0;
                        Source->position_fraction=0;
                        Source->BuffersPlayed = 0;
                        ALBufferListItem= Source->queue;
                        while (ALBufferListItem != NULL)
                        {
                            ALBufferListItem->bufferstate = PENDING;
                            ALBufferListItem = ALBufferListItem->next;
                        }
                        if (Source->queue)
                            Source->ulBufferID = Source->queue->buffer;
                    }
                    Source->lOffset = 0;
                }
            }
        }
        else
        {
            // sources is a NULL pointer
            alSetError(AL_INVALID_VALUE);
        }

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return;
}


ALAPI ALvoid ALAPIENTRY alSourceQueueBuffers( ALuint source, ALsizei n, const ALuint* buffers )
{
    ALCcontext *Context;
    ALsource *ALSource;
    ALsizei i;
    ALbufferlistitem *ALBufferList;
    ALbufferlistitem *ALBufferListStart;
    ALuint DataSize;
    ALuint BufferSize;
    ALint iFrequency;
    ALint iFormat;
    ALboolean bBuffersValid = AL_TRUE;

    if (n == 0)
        return;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        DataSize = 0;
        BufferSize = 0;

        // Check that all buffers are valid or zero and that the source is valid

        // Check that this is a valid source
        if (alIsSource(source))
        {
            ALSource = (ALsource*)ALTHUNK_LOOKUPENTRY(source);

            // Check that this is not a STATIC Source
            if (ALSource->lSourceType != AL_STATIC)
            {
                iFrequency = -1;
                iFormat = -1;

                // Check existing Queue (if any) for a valid Buffers and get its frequency and format
                ALBufferList = ALSource->queue;
                while (ALBufferList)
                {
                    if (ALBufferList->buffer)
                    {
                        iFrequency = ((ALbuffer*)(ALTHUNK_LOOKUPENTRY(ALBufferList->buffer)))->frequency;
                        iFormat = ((ALbuffer*)(ALTHUNK_LOOKUPENTRY(ALBufferList->buffer)))->format;
                        break;
                    }
                    ALBufferList = ALBufferList->next;
                }

                for (i = 0; i < n; i++)
                {
                    if (alIsBuffer(buffers[i]))
                    {
                        if (buffers[i])
                        {
                            if ((iFrequency == -1) && (iFormat == -1))
                            {
                                iFrequency = ((ALbuffer*)(ALTHUNK_LOOKUPENTRY(buffers[i])))->frequency;
                                iFormat = ((ALbuffer*)(ALTHUNK_LOOKUPENTRY(buffers[i])))->format;
                            }
                            else
                            {
                                if ((iFrequency != ((ALbuffer*)(ALTHUNK_LOOKUPENTRY(buffers[i])))->frequency) ||
                                    (iFormat != ((ALbuffer*)(ALTHUNK_LOOKUPENTRY(buffers[i])))->format))
                                {
                                    alSetError(AL_INVALID_OPERATION);
                                    bBuffersValid = AL_FALSE;
                                    break;
                                }
                            }
                        }
                    }
                    else
                    {
                        alSetError(AL_INVALID_NAME);
                        bBuffersValid = AL_FALSE;
                        break;
                    }
                }

                if (bBuffersValid)
                {
                    // Change Source Type
                    ALSource->lSourceType = AL_STREAMING;

                    // All buffers are valid - so add them to the list
                    ALBufferListStart = malloc(sizeof(ALbufferlistitem));
                    ALBufferListStart->buffer = buffers[0];
                    ALBufferListStart->bufferstate = PENDING;
                    ALBufferListStart->flag = 0;
                    ALBufferListStart->next = NULL;

                    if (buffers[0])
                        BufferSize = ((ALbuffer*)ALTHUNK_LOOKUPENTRY(buffers[0]))->size;
                    else
                        BufferSize = 0;

                    DataSize += BufferSize;

                    // Increment reference counter for buffer
                    if (buffers[0])
                        ((ALbuffer*)(ALTHUNK_LOOKUPENTRY(buffers[0])))->refcount++;

                    ALBufferList = ALBufferListStart;

                    for (i = 1; i < n; i++)
                    {
                        ALBufferList->next = malloc(sizeof(ALbufferlistitem));
                        ALBufferList->next->buffer = buffers[i];
                        ALBufferList->next->bufferstate = PENDING;
                        ALBufferList->next->flag = 0;
                        ALBufferList->next->next = NULL;

                        if (buffers[i])
                            BufferSize = ((ALbuffer*)ALTHUNK_LOOKUPENTRY(buffers[i]))->size;
                        else
                            BufferSize = 0;

                        DataSize += BufferSize;

                        // Increment reference counter for buffer
                        if (buffers[i])
                            ((ALbuffer*)(ALTHUNK_LOOKUPENTRY(buffers[i])))->refcount++;

                        ALBufferList = ALBufferList->next;
                    }

                    if (ALSource->queue == NULL)
                    {
                        ALSource->queue = ALBufferListStart;
                        // Update Current Buffer
                        ALSource->ulBufferID = ALBufferListStart->buffer;
                    }
                    else
                    {
                        // Find end of queue
                        ALBufferList = ALSource->queue;
                        while (ALBufferList->next != NULL)
                        {
                            ALBufferList = ALBufferList->next;
                        }

                        ALBufferList->next = ALBufferListStart;
                    }

                    // Update number of buffers in queue
                    ALSource->BuffersInQueue += n;
                }
            }
            else
            {
                // Invalid Source Type (can't queue on a Static Source)
                alSetError(AL_INVALID_OPERATION);
            }
        }
        else
        {
            // Invalid Source Name
            alSetError(AL_INVALID_NAME);
        }

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return;
}


// Implementation assumes that n is the number of buffers to be removed from the queue and buffers is
// an array of buffer IDs that are to be filled with the names of the buffers removed
ALAPI ALvoid ALAPIENTRY alSourceUnqueueBuffers( ALuint source, ALsizei n, ALuint* buffers )
{
    ALCcontext *Context;
    ALsource *ALSource;
    ALsizei i;
    ALbufferlistitem *ALBufferList;
    ALuint DataSize;
    ALuint BufferSize;
    ALuint BufferID;
    ALboolean bBuffersProcessed;

    if (n == 0)
        return;

    DataSize = 0;
    BufferSize = 0;
    bBuffersProcessed = AL_TRUE;

    Context=alcGetCurrentContext();
    if (Context)
    {
        SuspendContext(Context);

        if (alIsSource(source))
        {
            ALSource = (ALsource*)ALTHUNK_LOOKUPENTRY(source);

            // Check that all 'n' buffers have been processed
            ALBufferList = ALSource->queue;
            for (i = 0; i < n; i++)
            {
                if ((ALBufferList != NULL) && (ALBufferList->bufferstate == PROCESSED))
                {
                    ALBufferList = ALBufferList->next;
                }
                else
                {
                    bBuffersProcessed = AL_FALSE;
                    break;
                }
            }

            // If all 'n' buffers have been processed, remove them from the queue
            if (bBuffersProcessed)
            {
                for (i = 0; i < n; i++)
                {
                    ALBufferList = ALSource->queue;

                    ALSource->queue = ALBufferList->next;
                    // Record name of buffer
                    buffers[i] = ALBufferList->buffer;
                    // Decrement buffer reference counter
                    if (ALBufferList->buffer)
                        ((ALbuffer*)(ALTHUNK_LOOKUPENTRY(ALBufferList->buffer)))->refcount--;
                    // Record size of buffer
                    if (ALBufferList->buffer)
                        BufferSize = ((ALbuffer*)ALTHUNK_LOOKUPENTRY(ALBufferList->buffer))->size;
                    else
                        BufferSize = 0;

                    DataSize += BufferSize;
                    // Release memory for buffer list item
                    free(ALBufferList);
                    ALSource->BuffersInQueue--;
                }

                if (ALSource->state != AL_PLAYING)
                {
                    if (ALSource->queue)
                        BufferID = ALSource->queue->buffer;
                    else
                        BufferID = 0;

                    ALSource->ulBufferID = BufferID;
                }

                if((ALuint)n > ALSource->BuffersPlayed)
                    ALSource->BuffersPlayed = 0;
                else
                    ALSource->BuffersPlayed -= n;
            }
            else
            {
                // Some buffers can't be unqueue because they have not been processed
                alSetError(AL_INVALID_VALUE);
            }
        }
        else
        {
            // Invalid Source Name
            alSetError(AL_INVALID_NAME);
        }

        ProcessContext(Context);
    }
    else
    {
        // Invalid Context
        alSetError(AL_INVALID_OPERATION);
    }

    return;
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

    pSource->state = AL_INITIAL;
    pSource->lSourceType = AL_UNDETERMINED;

    pSource->ulBufferID= 0;
}


/*
    GetSourceOffset

    Gets the current playback position in the given Source, in the appropriate format (Bytes, Samples or MilliSeconds)
    The offset is relative to the start of the queue (not the start of the current buffer)
*/
static ALboolean GetSourceOffset(ALsource *pSource, ALenum eName, ALfloat *pflOffset)
{
    ALbufferlistitem *pBufferList;
    ALbuffer         *pBuffer;
    ALfloat        flBufferFreq;
    ALint        lBytesPlayed, lChannels;
    ALenum        eOriginalFormat;
    ALboolean    bReturn = AL_TRUE;
    ALint        lTotalBufferDataSize;

    if (((pSource->state == AL_PLAYING) || (pSource->state == AL_PAUSED)) && (pSource->ulBufferID))
    {
        pBuffer = ALTHUNK_LOOKUPENTRY(pSource->ulBufferID);
        // Get Current Buffer Size and frequency (in milliseconds)
        flBufferFreq = (ALfloat)pBuffer->frequency;
        eOriginalFormat = pBuffer->eOriginalFormat;
        lChannels = aluChannelsFromFormat(pBuffer->format);

        // Get Current BytesPlayed
        lBytesPlayed = pSource->position * lChannels * 2; // NOTE : This is the byte offset into the *current* buffer
        // Add byte length of any processed buffers in the queue
        pBufferList = pSource->queue;
        while ((pBufferList) && (pBufferList->bufferstate == PROCESSED))
        {
            lBytesPlayed += ((ALbuffer*)ALTHUNK_LOOKUPENTRY(pBufferList->buffer))->size;
            pBufferList = pBufferList->next;
        }

        lTotalBufferDataSize = 0;
        pBufferList = pSource->queue;
        while (pBufferList)
        {
            if (pBufferList->buffer)
                lTotalBufferDataSize += ((ALbuffer*)ALTHUNK_LOOKUPENTRY(pBufferList->buffer))->size;
            pBufferList = pBufferList->next;
        }

        if (pSource->bLooping)
        {
            if (lBytesPlayed < 0)
                lBytesPlayed = 0;
            else
                lBytesPlayed = lBytesPlayed % lTotalBufferDataSize;
        }
        else
        {
            // Clamp BytesPlayed to within 0 and lTotalBufferDataSize
            if(lBytesPlayed < 0)
                lBytesPlayed = 0;
            if(lBytesPlayed > lTotalBufferDataSize)
                lBytesPlayed = lTotalBufferDataSize;
        }

        switch (eName)
        {
        case AL_SEC_OFFSET:
            *pflOffset = ((ALfloat)lBytesPlayed / (lChannels * 2.0f * flBufferFreq));
            break;
        case AL_SAMPLE_OFFSET:
            *pflOffset = (ALfloat)(lBytesPlayed / (lChannels * 2));
            break;
        case AL_BYTE_OFFSET:
            // Take into account the original format of the Buffer
            if ((eOriginalFormat == AL_FORMAT_MONO_IMA4) ||
                (eOriginalFormat == AL_FORMAT_STEREO_IMA4))
            {
                // Compression rate of the ADPCM supported is 3.6111 to 1
                lBytesPlayed = (ALint)((ALfloat)lBytesPlayed / 3.6111f);
                // Round down to nearest ADPCM block
                *pflOffset = (ALfloat)((lBytesPlayed / (36 * lChannels)) * 36 * lChannels);
            }
            else if (eOriginalFormat == AL_FORMAT_REAR8)
            {
                *pflOffset = (ALfloat)(lBytesPlayed >> 2);
            }
            else if (eOriginalFormat == AL_FORMAT_REAR16)
            {
                *pflOffset = (ALfloat)(lBytesPlayed >> 1);
            }
            else if (aluBytesFromFormat(eOriginalFormat) == 1)
            {
                *pflOffset = (ALfloat)(lBytesPlayed >> 1);
            }
            else if (aluBytesFromFormat(eOriginalFormat) == 4)
            {
                *pflOffset = (ALfloat)(lBytesPlayed << 1);
            }
            else
            {
                *pflOffset = (ALfloat)lBytesPlayed;
            }
            break;
        }
    }
    else
    {
        *pflOffset = 0.0f;
    }

    return bReturn;
}


/*
    ApplyOffset

    Apply a playback offset to the Source.  This function will update the queue (to correctly
    mark buffers as 'pending' or 'processed' depending upon the new offset.
*/
static void ApplyOffset(ALsource *pSource, ALboolean bUpdateContext)
{
    ALbufferlistitem    *pBufferList;
    ALbuffer            *pBuffer;
    ALint                lBufferSize, lTotalBufferSize;
    ALint                lByteOffset;

    // Get true byte offset
    lByteOffset = GetByteOffset(pSource);

    // If this is a valid offset apply it
    if (lByteOffset != -1)
    {
        // Sort out the queue (pending and processed states)
        pBufferList = pSource->queue;
        lTotalBufferSize = 0;
        pSource->BuffersPlayed = 0;
        while (pBufferList)
        {
            pBuffer = ALTHUNK_LOOKUPENTRY(pBufferList->buffer);
            lBufferSize = pBuffer ? pBuffer->size : 0;

            if ((lTotalBufferSize + lBufferSize) <= lByteOffset)
            {
                // Offset is past this buffer so increment BuffersPlayed and if the Source is NOT looping
                // update the state to PROCESSED
                pSource->BuffersPlayed++;

                if (!pSource->bLooping)
                    pBufferList->bufferstate = PROCESSED;
            }
            else if (lTotalBufferSize <= lByteOffset)
            {
                // Offset is within this buffer
                pBufferList->bufferstate = PENDING;

                // Set Current Buffer ID
                pSource->ulBufferID = pBufferList->buffer;

                // SW Mixer Positions are in Samples
                pSource->position = (lByteOffset - lTotalBufferSize) /
                                    aluBytesFromFormat(pBuffer->format) /
                                    aluChannelsFromFormat(pBuffer->format);
            }
            else
            {
                // Offset is before this buffer, so mark as pending
                pBufferList->bufferstate = PENDING;
            }

            // Increment the TotalBufferSize
            lTotalBufferSize += lBufferSize;

            // Move on to next buffer in the Queue
            pBufferList = pBufferList->next;
        }
    }
    else
    {
        if (bUpdateContext)
            alSetError(AL_INVALID_VALUE);
    }

    // Clear Offset
    pSource->lOffset = 0;
}


/*
    GetByteOffset

    Returns the 'true' byte offset into the Source's queue (from the Sample, Byte or Millisecond
    offset supplied by the application).   This takes into account the fact that the buffer format
    may have been modifed by AL (e.g 8bit samples are converted to 16bit)
*/
static ALint GetByteOffset(ALsource *pSource)
{
    ALbuffer *pBuffer = NULL;
    ALbufferlistitem *pBufferList;
    ALfloat    flBufferFreq;
    ALint    lChannels;
    ALint    lByteOffset = -1;
    ALint    lTotalBufferDataSize;

    // Find the first non-NULL Buffer in the Queue
    pBufferList = pSource->queue;
    while (pBufferList)
    {
        if (pBufferList->buffer)
        {
            pBuffer = (ALbuffer*)ALTHUNK_LOOKUPENTRY(pBufferList->buffer);
            break;
        }
        pBufferList = pBufferList->next;
    }

    if (pBuffer)
    {
        flBufferFreq = ((ALfloat)pBuffer->frequency);
        lChannels = aluChannelsFromFormat(pBuffer->format);

        // Determine the ByteOffset (and ensure it is block aligned)
        switch (pSource->lOffsetType)
        {
        case AL_BYTE_OFFSET:
            // Take into consideration the original format
            if ((pBuffer->eOriginalFormat == AL_FORMAT_MONO_IMA4) ||
                (pBuffer->eOriginalFormat == AL_FORMAT_STEREO_IMA4))
            {
                // Round down to nearest ADPCM block
                lByteOffset = (pSource->lOffset / (36 * lChannels)) * 36 * lChannels;
                // Multiply by compression rate
                lByteOffset = (ALint)(3.6111f * (ALfloat)lByteOffset);
                lByteOffset -= (lByteOffset % (lChannels * 2));
            }
            else if (pBuffer->eOriginalFormat == AL_FORMAT_REAR8)
            {
                lByteOffset = pSource->lOffset * 4;
                lByteOffset -= (lByteOffset % (lChannels * 2));
            }
            else if (pBuffer->eOriginalFormat == AL_FORMAT_REAR16)
            {
                lByteOffset = pSource->lOffset * 2;
                lByteOffset -= (lByteOffset % (lChannels * 2));
            }
            else if (aluBytesFromFormat(pBuffer->eOriginalFormat) == 1)
            {
                lByteOffset = pSource->lOffset * 2;
                lByteOffset -= (lByteOffset % (lChannels * 2));
            }
            else if (aluBytesFromFormat(pBuffer->eOriginalFormat) == 4)
            {
                lByteOffset = pSource->lOffset / 2;
                lByteOffset -= (lByteOffset % (lChannels * 2));
            }
            else
            {
                lByteOffset = pSource->lOffset;
                lByteOffset -= (lByteOffset % (lChannels * 2));
            }
            break;

        case AL_SAMPLE_OFFSET:
            lByteOffset = pSource->lOffset * lChannels * 2;
            break;

        case AL_SEC_OFFSET:
            // Note - lOffset is internally stored as Milliseconds
            lByteOffset = (ALint)(pSource->lOffset * lChannels * 2.0f * flBufferFreq / 1000.0f);
            lByteOffset -= (lByteOffset % (lChannels * 2));
            break;
        }

        lTotalBufferDataSize = 0;
        pBufferList = pSource->queue;
        while (pBufferList)
        {
            if (pBufferList->buffer)
                lTotalBufferDataSize += ((ALbuffer*)ALTHUNK_LOOKUPENTRY(pBufferList->buffer))->size;
            pBufferList = pBufferList->next;
        }

        // Finally, if the ByteOffset is beyond the length of all the buffers in the queue, return -1
        if (lByteOffset >= lTotalBufferDataSize)
            lByteOffset = -1;
    }

    return lByteOffset;
}


ALvoid ReleaseALSources(ALCcontext *Context)
{
#ifdef _DEBUG
    if(Context->SourceCount > 0)
        AL_PRINT("alcDestroyContext(): deleting %d Source(s)\n", Context->SourceCount);
#endif

    while(Context->Source)
    {
        ALsource *temp = Context->Source;
        Context->Source = Context->Source->next;

        // Release source structure
        ALTHUNK_REMOVEENTRY(temp->source);
        memset(temp, 0, sizeof(ALsource));
        free(temp);
    }
    Context->SourceCount = 0;
}
