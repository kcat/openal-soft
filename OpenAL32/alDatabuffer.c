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
#include <stdio.h>
#include <assert.h>
#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"
#include "alError.h"
#include "alDatabuffer.h"
#include "alThunk.h"


/*
*    alGenDatabuffersEXT(ALsizei n, ALuint *puiBuffers)
*
*    Generates n AL Databuffers, and stores the Databuffers Names in the array pointed to by puiBuffers
*/
ALvoid ALAPIENTRY alGenDatabuffersEXT(ALsizei n,ALuint *puiBuffers)
{
    ALCcontext *Context;
    ALsizei i=0;

    Context = GetContextSuspended();
    if(!Context) return;

    /* Check that we are actually generation some Databuffers */
    if(n > 0)
    {
        ALCdevice *device = Context->Device;

        /* Check the pointer is valid (and points to enough memory to store
         * Databuffer Names) */
        if(!IsBadWritePtr((void*)puiBuffers, n * sizeof(ALuint)))
        {
            ALdatabuffer **list = &device->Databuffers;
            while(*list)
                list = &(*list)->next;

            /* Create all the new Databuffers */
            while(i < n)
            {
                *list = calloc(1, sizeof(ALdatabuffer));
                if(!(*list))
                {
                    alDeleteDatabuffersEXT(i, puiBuffers);
                    alSetError(AL_OUT_OF_MEMORY);
                    break;
                }

                puiBuffers[i] = (ALuint)ALTHUNK_ADDENTRY(*list);
                (*list)->databuffer = puiBuffers[i];
                (*list)->state = UNMAPPED;
                device->DatabufferCount++;
                i++;

                list = &(*list)->next;
            }
        }
        else
            alSetError(AL_INVALID_VALUE);
    }

    ProcessContext(Context);
}

/*
*    alDatabeleteBuffersEXT(ALsizei n, ALuint *puiBuffers)
*
*    Deletes the n AL Databuffers pointed to by puiBuffers
*/
ALvoid ALAPIENTRY alDeleteDatabuffersEXT(ALsizei n, const ALuint *puiBuffers)
{
    ALCcontext *Context;
    ALdatabuffer *ALBuf;
    ALsizei i;
    ALboolean bFailed = AL_FALSE;

    Context = GetContextSuspended();
    if(!Context) return;

    /* Check we are actually Deleting some Databuffers */
    if(n >= 0)
    {
        ALCdevice *device = Context->Device;

        /* Check that all the databuffers are valid and can actually be
         * deleted */
        for(i = 0;i < n;i++)
        {
            /* Check for valid Buffer ID (can be NULL buffer) */
            if(alIsDatabufferEXT(puiBuffers[i]))
            {
                /* If not the NULL buffer, check that it's unmapped */
                ALBuf = ((ALdatabuffer *)ALTHUNK_LOOKUPENTRY(puiBuffers[i]));
                if(ALBuf)
                {
                    if(ALBuf->state != UNMAPPED)
                    {
                        /* Databuffer still in use, cannot be deleted */
                        alSetError(AL_INVALID_OPERATION);
                        bFailed = AL_TRUE;
                    }
                }
            }
            else
            {
                /* Invalid Databuffer */
                alSetError(AL_INVALID_NAME);
                bFailed = AL_TRUE;
            }
        }

        /* If all the Databuffers were valid (and unmapped), then we can
         * delete them */
        if(!bFailed)
        {
            for(i = 0;i < n;i++)
            {
                if(puiBuffers[i] && alIsDatabufferEXT(puiBuffers[i]))
                {
                    ALdatabuffer **list = &device->Databuffers;

                    ALBuf = (ALdatabuffer*)ALTHUNK_LOOKUPENTRY(puiBuffers[i]);
                    while(*list && *list != ALBuf)
                        list = &(*list)->next;

                    if(*list)
                        *list = (*list)->next;

                    if(ALBuf == Context->SampleSource)
                        Context->SampleSource = NULL;
                    if(ALBuf == Context->SampleSink)
                        Context->SampleSink = NULL;

                    // Release the memory used to store audio data
                    free(ALBuf->data);

                    // Release buffer structure
                    ALTHUNK_REMOVEENTRY(puiBuffers[i]);
                    memset(ALBuf, 0, sizeof(ALdatabuffer));
                    device->DatabufferCount--;
                    free(ALBuf);
                }
            }
        }
    }
    else
        alSetError(AL_INVALID_VALUE);

    ProcessContext(Context);
}

/*
*    alIsDatabufferEXT(ALuint uiBuffer)
*
*    Checks if ulBuffer is a valid Databuffer Name
*/
ALboolean ALAPIENTRY alIsDatabufferEXT(ALuint uiBuffer)
{
    ALCcontext *Context;
    ALdatabuffer *ALBuf;

    Context = GetContextSuspended();
    if(!Context) return AL_FALSE;

    /* Check through list of generated databuffers for uiBuffer */
    ALBuf = Context->Device->Databuffers;
    while(ALBuf && ALBuf->databuffer != uiBuffer)
        ALBuf = ALBuf->next;

    ProcessContext(Context);

    return ((ALBuf || !uiBuffer) ? AL_TRUE : AL_FALSE);
}

/*
*    alDatabufferDataEXT(ALuint buffer,ALvoid *data,ALsizei size,ALenum usage)
*
*    Fill databuffer with data
*/
ALvoid ALAPIENTRY alDatabufferDataEXT(ALuint buffer,const ALvoid *data,ALsizei size,ALenum usage)
{
    ALCcontext *Context;
    ALdatabuffer *ALBuf;
    ALvoid *temp;

    Context = GetContextSuspended();
    if(!Context) return;

    if(alIsDatabufferEXT(buffer) && buffer != 0)
    {
        ALBuf = (ALdatabuffer*)ALTHUNK_LOOKUPENTRY(buffer);
        if(ALBuf->state == UNMAPPED)
        {
            if(usage == AL_STREAM_WRITE_EXT || usage == AL_STREAM_READ_EXT ||
               usage == AL_STREAM_COPY_EXT || usage == AL_STATIC_WRITE_EXT ||
               usage == AL_STATIC_READ_EXT || usage == AL_STATIC_COPY_EXT ||
               usage == AL_DYNAMIC_WRITE_EXT || usage == AL_DYNAMIC_READ_EXT ||
               usage == AL_DYNAMIC_COPY_EXT)
            {
                /* (Re)allocate data */
                temp = realloc(ALBuf->data, size);
                if(temp)
                {
                    ALBuf->data = temp;
                    ALBuf->size = size;
                    ALBuf->usage = usage;
                    if(data)
                        memcpy(ALBuf->data, data, size);
                }
                else
                    alSetError(AL_OUT_OF_MEMORY);
            }
            else
                alSetError(AL_INVALID_ENUM);
        }
        else
            alSetError(AL_INVALID_OPERATION);
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(Context);
}

ALvoid ALAPIENTRY alDatabufferSubDataEXT(ALuint uiBuffer, ALuint start, ALsizei length, const ALvoid *data)
{
    ALCcontext    *pContext;
    ALdatabuffer  *pBuffer;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(alIsDatabufferEXT(uiBuffer) && uiBuffer != 0)
    {
        pBuffer = (ALdatabuffer*)ALTHUNK_LOOKUPENTRY(uiBuffer);

        if(length >= 0 && start+length <= pBuffer->size)
        {
            if(pBuffer->state == UNMAPPED)
                memcpy(pBuffer->data+start, data, length);
            else
                alSetError(AL_INVALID_OPERATION);
        }
        else
            alSetError(AL_INVALID_VALUE);
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(pContext);
}

ALvoid ALAPIENTRY alGetDatabufferSubDataEXT(ALuint uiBuffer, ALuint start, ALsizei length, ALvoid *data)
{
    ALCcontext    *pContext;
    ALdatabuffer  *pBuffer;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(alIsDatabufferEXT(uiBuffer) && uiBuffer != 0)
    {
        pBuffer = (ALdatabuffer*)ALTHUNK_LOOKUPENTRY(uiBuffer);

        if(length >= 0 && start+length <= pBuffer->size)
        {
            if(pBuffer->state == UNMAPPED)
                memcpy(data, pBuffer->data+start, length);
            else
                alSetError(AL_INVALID_OPERATION);
        }
        else
            alSetError(AL_INVALID_VALUE);
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(pContext);
}


ALvoid ALAPIENTRY alDatabufferfEXT(ALuint buffer, ALenum eParam, ALfloat flValue)
{
    ALCcontext    *pContext;

    (void)flValue;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(alIsDatabufferEXT(buffer) && buffer != 0)
    {
        switch(eParam)
        {
        default:
            alSetError(AL_INVALID_ENUM);
            break;
        }
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(pContext);
}

ALvoid ALAPIENTRY alDatabufferfvEXT(ALuint buffer, ALenum eParam, const ALfloat* flValues)
{
    ALCcontext    *pContext;

    (void)flValues;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(alIsDatabufferEXT(buffer) && buffer != 0)
    {
        switch(eParam)
        {
        default:
            alSetError(AL_INVALID_ENUM);
            break;
        }
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(pContext);
}


ALvoid ALAPIENTRY alDatabufferiEXT(ALuint buffer, ALenum eParam, ALint lValue)
{
    ALCcontext    *pContext;

    (void)lValue;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(alIsDatabufferEXT(buffer) && buffer != 0)
    {
        switch(eParam)
        {
        default:
            alSetError(AL_INVALID_ENUM);
            break;
        }
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(pContext);
}

ALvoid ALAPIENTRY alDatabufferivEXT(ALuint buffer, ALenum eParam, const ALint* plValues)
{
    ALCcontext    *pContext;

    (void)plValues;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(alIsDatabufferEXT(buffer) && buffer != 0)
    {
        switch(eParam)
        {
        default:
            alSetError(AL_INVALID_ENUM);
            break;
        }
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(pContext);
}


ALvoid ALAPIENTRY alGetDatabufferfEXT(ALuint buffer, ALenum eParam, ALfloat *pflValue)
{
    ALCcontext    *pContext;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(pflValue)
    {
        if(alIsDatabufferEXT(buffer) && buffer != 0)
        {
            switch(eParam)
            {
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

ALvoid ALAPIENTRY alGetDatabufferfvEXT(ALuint buffer, ALenum eParam, ALfloat* pflValues)
{
    ALCcontext    *pContext;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(pflValues)
    {
        if(alIsDatabufferEXT(buffer) && buffer != 0)
        {
            switch(eParam)
            {
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

ALvoid ALAPIENTRY alGetDatabufferiEXT(ALuint buffer, ALenum eParam, ALint *plValue)
{
    ALCcontext    *pContext;
    ALdatabuffer  *pBuffer;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(plValue)
    {
        if(alIsDatabufferEXT(buffer) && buffer != 0)
        {
            pBuffer = (ALdatabuffer*)ALTHUNK_LOOKUPENTRY(buffer);

            switch(eParam)
            {
            case AL_SIZE:
                *plValue = pBuffer->size;
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

ALvoid ALAPIENTRY alGetDatabufferivEXT(ALuint buffer, ALenum eParam, ALint* plValues)
{
    ALCcontext    *pContext;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(plValues)
    {
        if(alIsDatabufferEXT(buffer) && buffer != 0)
        {
            switch (eParam)
            {
            case AL_SIZE:
                alGetBufferi(buffer, eParam, plValues);
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


ALvoid ALAPIENTRY alSelectDatabufferEXT(ALenum target, ALuint uiBuffer)
{
    ALCcontext    *pContext;
    ALdatabuffer  *pBuffer;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(alIsDatabufferEXT(uiBuffer))
    {
        pBuffer = (ALdatabuffer*)(uiBuffer ? ALTHUNK_LOOKUPENTRY(uiBuffer) : NULL);
        if(target == AL_SAMPLE_SOURCE_EXT)
            pContext->SampleSource = pBuffer;
        else if(target == AL_SAMPLE_SINK_EXT)
            pContext->SampleSink = pBuffer;
        else
            alSetError(AL_INVALID_VALUE);
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(pContext);
}


ALvoid* ALAPIENTRY alMapDatabufferEXT(ALuint uiBuffer, ALuint start, ALsizei length, ALenum access)
{
    ALCcontext    *pContext;
    ALdatabuffer  *pBuffer;
    ALvoid        *ret = NULL;

    pContext = GetContextSuspended();
    if(!pContext) return NULL;

    if(alIsDatabufferEXT(uiBuffer) && uiBuffer != 0)
    {
        pBuffer = (ALdatabuffer*)ALTHUNK_LOOKUPENTRY(uiBuffer);

        if(length >= 0 && start+length <= pBuffer->size)
        {
            if(access == AL_READ_ONLY_EXT || access == AL_WRITE_ONLY_EXT ||
               access == AL_READ_WRITE_EXT)
            {
                if(pBuffer->state == UNMAPPED)
                {
                    ret = pBuffer->data + start;
                    pBuffer->state = MAPPED;
                }
                else
                    alSetError(AL_INVALID_OPERATION);
            }
            else
                alSetError(AL_INVALID_ENUM);
        }
        else
            alSetError(AL_INVALID_VALUE);
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(pContext);

    return ret;
}

ALvoid ALAPIENTRY alUnmapDatabufferEXT(ALuint uiBuffer)
{
    ALCcontext    *pContext;
    ALdatabuffer  *pBuffer;

    pContext = GetContextSuspended();
    if(!pContext) return;

    if(alIsDatabufferEXT(uiBuffer) && uiBuffer != 0)
    {
        pBuffer = (ALdatabuffer*)ALTHUNK_LOOKUPENTRY(uiBuffer);

        if(pBuffer->state == MAPPED)
            pBuffer->state = UNMAPPED;
        else
            alSetError(AL_INVALID_OPERATION);
    }
    else
        alSetError(AL_INVALID_NAME);

    ProcessContext(pContext);
}


/*
*    ReleaseALDatabuffers()
*
*    INTERNAL FN : Called by DLLMain on exit to destroy any buffers that still exist
*/
ALvoid ReleaseALDatabuffers(ALCdevice *device)
{
    ALdatabuffer *ALBuffer;
    ALdatabuffer *ALBufferTemp;

    ALBuffer = device->Databuffers;
    while(ALBuffer)
    {
        // Release sample data
        free(ALBuffer->data);

        // Release Buffer structure
        ALBufferTemp = ALBuffer;
        ALBuffer = ALBuffer->next;
        memset(ALBufferTemp, 0, sizeof(ALdatabuffer));
        free(ALBufferTemp);
    }
    device->Databuffers = NULL;
    device->DatabufferCount = 0;
}
