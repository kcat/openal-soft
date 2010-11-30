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
#include <limits.h>

#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"
#include "alError.h"
#include "alBuffer.h"
#include "alDatabuffer.h"
#include "alThunk.h"


static ALenum LoadData(ALbuffer *ALBuf, const ALvoid *data, ALsizei size, ALuint freq, ALenum OrigFormat, ALenum NewFormat);
static void ConvertData(ALvoid *dst, enum FmtType dstType, const ALvoid *src, enum SrcFmtType srcType, ALsizei len);
static void ConvertDataIMA4(ALvoid *dst, const ALvoid *src, ALint origChans, ALsizei len);

#define LookupBuffer(m, k) ((ALbuffer*)LookupUIntMapKey(&(m), (k)))


/*
* Global Variables
*/

static const long g_IMAStep_size[89]={            // IMA ADPCM Stepsize table
       7,    8,    9,   10,   11,   12,   13,   14,   16,   17,   19,   21,   23,   25,   28,   31,
      34,   37,   41,   45,   50,   55,   60,   66,   73,   80,   88,   97,  107,  118,  130,  143,
     157,  173,  190,  209,  230,  253,  279,  307,  337,  371,  408,  449,  494,  544,  598,  658,
     724,  796,  876,  963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
    3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493,10442,11487,12635,13899,
   15289,16818,18500,20350,22358,24633,27086,29794,32767
};

static const long g_IMACodeword_4[16]={            // IMA4 ADPCM Codeword decode table
    1, 3, 5, 7, 9, 11, 13, 15,
   -1,-3,-5,-7,-9,-11,-13,-15,
};

static const long g_IMAIndex_adjust_4[16]={        // IMA4 ADPCM Step index adjust decode table
   -1,-1,-1,-1, 2, 4, 6, 8,
   -1,-1,-1,-1, 2, 4, 6, 8
};

static const ALshort muLawDecompressionTable[256] = {
    -32124,-31100,-30076,-29052,-28028,-27004,-25980,-24956,
    -23932,-22908,-21884,-20860,-19836,-18812,-17788,-16764,
    -15996,-15484,-14972,-14460,-13948,-13436,-12924,-12412,
    -11900,-11388,-10876,-10364, -9852, -9340, -8828, -8316,
     -7932, -7676, -7420, -7164, -6908, -6652, -6396, -6140,
     -5884, -5628, -5372, -5116, -4860, -4604, -4348, -4092,
     -3900, -3772, -3644, -3516, -3388, -3260, -3132, -3004,
     -2876, -2748, -2620, -2492, -2364, -2236, -2108, -1980,
     -1884, -1820, -1756, -1692, -1628, -1564, -1500, -1436,
     -1372, -1308, -1244, -1180, -1116, -1052,  -988,  -924,
      -876,  -844,  -812,  -780,  -748,  -716,  -684,  -652,
      -620,  -588,  -556,  -524,  -492,  -460,  -428,  -396,
      -372,  -356,  -340,  -324,  -308,  -292,  -276,  -260,
      -244,  -228,  -212,  -196,  -180,  -164,  -148,  -132,
      -120,  -112,  -104,   -96,   -88,   -80,   -72,   -64,
       -56,   -48,   -40,   -32,   -24,   -16,    -8,     0,
     32124, 31100, 30076, 29052, 28028, 27004, 25980, 24956,
     23932, 22908, 21884, 20860, 19836, 18812, 17788, 16764,
     15996, 15484, 14972, 14460, 13948, 13436, 12924, 12412,
     11900, 11388, 10876, 10364,  9852,  9340,  8828,  8316,
      7932,  7676,  7420,  7164,  6908,  6652,  6396,  6140,
      5884,  5628,  5372,  5116,  4860,  4604,  4348,  4092,
      3900,  3772,  3644,  3516,  3388,  3260,  3132,  3004,
      2876,  2748,  2620,  2492,  2364,  2236,  2108,  1980,
      1884,  1820,  1756,  1692,  1628,  1564,  1500,  1436,
      1372,  1308,  1244,  1180,  1116,  1052,   988,   924,
       876,   844,   812,   780,   748,   716,   684,   652,
       620,   588,   556,   524,   492,   460,   428,   396,
       372,   356,   340,   324,   308,   292,   276,   260,
       244,   228,   212,   196,   180,   164,   148,   132,
       120,   112,   104,    96,    88,    80,    72,    64,
        56,    48,    40,    32,    24,    16,     8,     0
};

/*
 *    alGenBuffers(ALsizei n, ALuint *buffers)
 *
 *    Generates n AL Buffers, and stores the Buffers Names in the array pointed to by buffers
 */
AL_API ALvoid AL_APIENTRY alGenBuffers(ALsizei n, ALuint *buffers)
{
    ALCcontext *Context;
    ALsizei i=0;

    Context = GetContextSuspended();
    if(!Context) return;

    // Check that we are actually generating some Buffers
    if(n < 0 || IsBadWritePtr((void*)buffers, n * sizeof(ALuint)))
        alSetError(Context, AL_INVALID_VALUE);
    else
    {
        ALCdevice *device = Context->Device;
        ALenum err;

        // Create all the new Buffers
        while(i < n)
        {
            ALbuffer *buffer = calloc(1, sizeof(ALbuffer));
            if(!buffer)
            {
                alSetError(Context, AL_OUT_OF_MEMORY);
                alDeleteBuffers(i, buffers);
                break;
            }

            buffer->buffer = (ALuint)ALTHUNK_ADDENTRY(buffer);
            err = InsertUIntMapEntry(&device->BufferMap, buffer->buffer, buffer);
            if(err != AL_NO_ERROR)
            {
                ALTHUNK_REMOVEENTRY(buffer->buffer);
                memset(buffer, 0, sizeof(ALbuffer));
                free(buffer);

                alSetError(Context, err);
                alDeleteBuffers(i, buffers);
                break;
            }
            buffers[i++] = buffer->buffer;
        }
    }

    ProcessContext(Context);
}

/*
 *    alDeleteBuffers(ALsizei n, ALuint *buffers)
 *
 *    Deletes the n AL Buffers pointed to by buffers
 */
AL_API ALvoid AL_APIENTRY alDeleteBuffers(ALsizei n, const ALuint *buffers)
{
    ALCcontext *Context;
    ALCdevice *device;
    ALboolean Failed;
    ALbuffer *ALBuf;
    ALsizei i;

    Context = GetContextSuspended();
    if(!Context) return;

    Failed = AL_TRUE;
    device = Context->Device;
    /* Check we are actually Deleting some Buffers */
    if(n < 0)
        alSetError(Context, AL_INVALID_VALUE);
    else
    {
        Failed = AL_FALSE;

        /* Check that all the buffers are valid and can actually be deleted */
        for(i = 0;i < n;i++)
        {
            if(!buffers[i])
                continue;

            /* Check for valid Buffer ID */
            if((ALBuf=LookupBuffer(device->BufferMap, buffers[i])) == NULL)
            {
                alSetError(Context, AL_INVALID_NAME);
                Failed = AL_TRUE;
                break;
            }
            else if(ALBuf->refcount != 0)
            {
                /* Buffer still in use, cannot be deleted */
                alSetError(Context, AL_INVALID_OPERATION);
                Failed = AL_TRUE;
                break;
            }
        }
    }

    /* If all the Buffers were valid (and have Reference Counts of 0), then we can delete them */
    if(!Failed)
    {
        for(i = 0;i < n;i++)
        {
            if((ALBuf=LookupBuffer(device->BufferMap, buffers[i])) == NULL)
                continue;

            /* Release the memory used to store audio data */
            free(ALBuf->data);

            /* Release buffer structure */
            RemoveUIntMapKey(&device->BufferMap, ALBuf->buffer);
            ALTHUNK_REMOVEENTRY(ALBuf->buffer);

            memset(ALBuf, 0, sizeof(ALbuffer));
            free(ALBuf);
        }
    }

    ProcessContext(Context);
}

/*
 *    alIsBuffer(ALuint buffer)
 *
 *    Checks if buffer is a valid Buffer Name
 */
AL_API ALboolean AL_APIENTRY alIsBuffer(ALuint buffer)
{
    ALCcontext *Context;
    ALboolean  result;

    Context = GetContextSuspended();
    if(!Context) return AL_FALSE;

    result = ((!buffer || LookupBuffer(Context->Device->BufferMap, buffer)) ?
              AL_TRUE : AL_FALSE);

    ProcessContext(Context);

    return result;
}

/*
 *    alBufferData(ALuint buffer,ALenum format,const ALvoid *data,ALsizei size,ALsizei freq)
 *
 *    Fill buffer with audio data
 */
AL_API ALvoid AL_APIENTRY alBufferData(ALuint buffer,ALenum format,const ALvoid *data,ALsizei size,ALsizei freq)
{
    ALCcontext *Context;
    ALCdevice *device;
    ALbuffer *ALBuf;
    ALvoid *temp;
    ALenum err;

    Context = GetContextSuspended();
    if(!Context) return;

    if(Context->SampleSource)
    {
        ALintptrEXT offset;

        if(Context->SampleSource->state == MAPPED)
        {
            alSetError(Context, AL_INVALID_OPERATION);
            ProcessContext(Context);
            return;
        }

        offset = (const ALubyte*)data - (ALubyte*)NULL;
        data = Context->SampleSource->data + offset;
    }

    device = Context->Device;
    if((ALBuf=LookupBuffer(device->BufferMap, buffer)) == NULL)
        alSetError(Context, AL_INVALID_NAME);
    else if(size < 0 || freq < 0)
        alSetError(Context, AL_INVALID_VALUE);
    else if(ALBuf->refcount != 0)
        alSetError(Context, AL_INVALID_VALUE);
    else switch(format)
    {
        case AL_FORMAT_MONO8:
        case AL_FORMAT_MONO16:
        case AL_FORMAT_MONO_FLOAT32:
        case AL_FORMAT_STEREO8:
        case AL_FORMAT_STEREO16:
        case AL_FORMAT_STEREO_FLOAT32:
        case AL_FORMAT_QUAD8_LOKI:
        case AL_FORMAT_QUAD16_LOKI:
        case AL_FORMAT_QUAD8:
        case AL_FORMAT_QUAD16:
        case AL_FORMAT_QUAD32:
        case AL_FORMAT_REAR8:
        case AL_FORMAT_REAR16:
        case AL_FORMAT_REAR32:
        case AL_FORMAT_51CHN8:
        case AL_FORMAT_51CHN16:
        case AL_FORMAT_51CHN32:
        case AL_FORMAT_61CHN8:
        case AL_FORMAT_61CHN16:
        case AL_FORMAT_61CHN32:
        case AL_FORMAT_71CHN8:
        case AL_FORMAT_71CHN16:
        case AL_FORMAT_71CHN32:
            err = LoadData(ALBuf, data, size, freq, format, format);
            if(err != AL_NO_ERROR)
                alSetError(Context, err);
            break;

        case AL_FORMAT_MONO_DOUBLE_EXT:
            err = LoadData(ALBuf, data, size, freq, format, AL_FORMAT_MONO_FLOAT32);
            if(err != AL_NO_ERROR)
                alSetError(Context, err);
            break;
        case AL_FORMAT_STEREO_DOUBLE_EXT:
            err = LoadData(ALBuf, data, size, freq, format, AL_FORMAT_STEREO_FLOAT32);
            if(err != AL_NO_ERROR)
                alSetError(Context, err);
            break;

        case AL_FORMAT_MONO_MULAW:
        case AL_FORMAT_STEREO_MULAW:
        case AL_FORMAT_QUAD_MULAW:
        case AL_FORMAT_51CHN_MULAW:
        case AL_FORMAT_61CHN_MULAW:
        case AL_FORMAT_71CHN_MULAW: {
            ALuint Channels = ((format==AL_FORMAT_MONO_MULAW) ? 1 :
                               ((format==AL_FORMAT_STEREO_MULAW) ? 2 :
                                ((format==AL_FORMAT_QUAD_MULAW) ? 4 :
                                 ((format==AL_FORMAT_51CHN_MULAW) ? 6 :
                                  ((format==AL_FORMAT_61CHN_MULAW) ? 7 : 8)))));
            ALenum NewFormat = ((Channels==1) ? AL_FORMAT_MONO16 :
                                ((Channels==2) ? AL_FORMAT_STEREO16 :
                                 ((Channels==4) ? AL_FORMAT_QUAD16 :
                                  ((Channels==6) ? AL_FORMAT_51CHN16 :
                                   ((Channels==7) ? AL_FORMAT_61CHN16 :
                                                    AL_FORMAT_71CHN16)))));
            err = LoadData(ALBuf, data, size, freq, format, NewFormat);
            if(err != AL_NO_ERROR)
                alSetError(Context, err);
        }   break;

        case AL_FORMAT_REAR_MULAW:
            err = LoadData(ALBuf, data, size, freq, format, AL_FORMAT_REAR16);
            if(err != AL_NO_ERROR)
                alSetError(Context, err);
            break;

        case AL_FORMAT_MONO_IMA4:
        case AL_FORMAT_STEREO_IMA4: {
            ALuint Channels = ((format==AL_FORMAT_MONO_IMA4) ? 1 : 2);
            ALenum NewFormat = ((Channels==1) ? AL_FORMAT_MONO16 :
                                                AL_FORMAT_STEREO16);
            ALuint NewBytes = aluBytesFromFormat(NewFormat);
            ALuint64 newsize;

            /* Here is where things vary:
             * nVidia and Apple use 64+1 sample frames per block => block_size=36*chans bytes
             * Most PC sound software uses 2040+1 sample frames per block -> block_size=1024*chans bytes
             */
            if((size%(36*Channels)) != 0)
            {
                alSetError(Context, AL_INVALID_VALUE);
                break;
            }

            newsize = size / 36;
            newsize *= 65;
            newsize *= NewBytes;

            if(newsize > INT_MAX)
            {
                alSetError(Context, AL_OUT_OF_MEMORY);
                break;
            }
            temp = realloc(ALBuf->data, newsize);
            if(temp)
            {
                ALBuf->data = temp;
                ALBuf->size = newsize;

                ConvertDataIMA4(ALBuf->data, data, Channels, newsize/(65*Channels*NewBytes));

                ALBuf->Frequency = freq;
                DecomposeFormat(NewFormat, &ALBuf->FmtChannels, &ALBuf->FmtType);

                ALBuf->LoopStart = 0;
                ALBuf->LoopEnd = newsize / Channels / NewBytes;

                ALBuf->OriginalChannels = ((Channels==1) ? SrcFmtMono :
                                                           SrcFmtStereo);
                ALBuf->OriginalType     = SrcFmtIMA4;
                ALBuf->OriginalSize     = size;
                ALBuf->OriginalAlign    = 36 * Channels;
            }
            else
                alSetError(Context, AL_OUT_OF_MEMORY);
        }   break;

        default:
            alSetError(Context, AL_INVALID_ENUM);
            break;
    }

    ProcessContext(Context);
}

/*
 *    alBufferSubDataSOFT(ALuint buffer,ALenum format,const ALvoid *data,ALsizei offset,ALsizei length)
 *
 *    Update buffer's audio data
 */
AL_API ALvoid AL_APIENTRY alBufferSubDataSOFT(ALuint buffer,ALenum format,const ALvoid *data,ALsizei offset,ALsizei length)
{
    enum SrcFmtChannels SrcChannels;
    enum SrcFmtType SrcType;
    ALCcontext *Context;
    ALCdevice  *device;
    ALbuffer   *ALBuf;

    Context = GetContextSuspended();
    if(!Context) return;

    if(Context->SampleSource)
    {
        ALintptrEXT offset;

        if(Context->SampleSource->state == MAPPED)
        {
            alSetError(Context, AL_INVALID_OPERATION);
            ProcessContext(Context);
            return;
        }

        offset = (const ALubyte*)data - (ALubyte*)NULL;
        data = Context->SampleSource->data + offset;
    }

    device = Context->Device;
    if((ALBuf=LookupBuffer(device->BufferMap, buffer)) == NULL)
        alSetError(Context, AL_INVALID_NAME);
    else if(length < 0 || offset < 0 || (length > 0 && data == NULL))
        alSetError(Context, AL_INVALID_VALUE);
    else if(offset > ALBuf->OriginalSize ||
            length > ALBuf->OriginalSize-offset ||
            (offset%ALBuf->OriginalAlign) != 0 ||
            (length%ALBuf->OriginalAlign) != 0)
        alSetError(Context, AL_INVALID_VALUE);
    else switch(format)
    {
        case AL_FORMAT_MONO8:
        case AL_FORMAT_MONO16:
        case AL_FORMAT_MONO_FLOAT32:
        case AL_FORMAT_MONO_DOUBLE_EXT:
        case AL_FORMAT_MONO_MULAW:
        case AL_FORMAT_MONO_IMA4:
        case AL_FORMAT_STEREO8:
        case AL_FORMAT_STEREO16:
        case AL_FORMAT_STEREO_FLOAT32:
        case AL_FORMAT_STEREO_DOUBLE_EXT:
        case AL_FORMAT_STEREO_MULAW:
        case AL_FORMAT_STEREO_IMA4:
        case AL_FORMAT_QUAD8_LOKI:
        case AL_FORMAT_QUAD16_LOKI:
        case AL_FORMAT_QUAD8:
        case AL_FORMAT_QUAD16:
        case AL_FORMAT_QUAD32:
        case AL_FORMAT_QUAD_MULAW:
        case AL_FORMAT_REAR8:
        case AL_FORMAT_REAR16:
        case AL_FORMAT_REAR32:
        case AL_FORMAT_REAR_MULAW:
        case AL_FORMAT_51CHN8:
        case AL_FORMAT_51CHN16:
        case AL_FORMAT_51CHN32:
        case AL_FORMAT_51CHN_MULAW:
        case AL_FORMAT_61CHN8:
        case AL_FORMAT_61CHN16:
        case AL_FORMAT_61CHN32:
        case AL_FORMAT_61CHN_MULAW:
        case AL_FORMAT_71CHN8:
        case AL_FORMAT_71CHN16:
        case AL_FORMAT_71CHN32:
        case AL_FORMAT_71CHN_MULAW:
            DecomposeInputFormat(format, &SrcChannels, &SrcType);
            if(SrcChannels != ALBuf->OriginalChannels || SrcType != ALBuf->OriginalType)
                alSetError(Context, AL_INVALID_ENUM);
            else if(SrcType == SrcFmtIMA4)
            {
                ALuint Channels = ChannelsFromFmt(ALBuf->FmtChannels);
                ALuint Bytes = BytesFromFmt(ALBuf->FmtType);

                /* offset -> byte offset, length -> block count */
                offset /= 36;
                offset *= 65;
                offset *= Bytes;
                length /= ALBuf->OriginalAlign;

                ConvertDataIMA4(&((ALubyte*)ALBuf->data)[offset], data, Channels, length);
            }
            else
            {
                ALuint OldBytes = BytesFromFmt(SrcType);
                ALuint Bytes = BytesFromFmt(ALBuf->FmtType);

                offset /= OldBytes;
                offset *= Bytes;
                length /= OldBytes;

                ConvertData(&((ALubyte*)ALBuf->data)[offset], ALBuf->FmtType,
                            data, SrcType, length);
            }
            break;

        default:
            alSetError(Context, AL_INVALID_ENUM);
            break;
    }

    ProcessContext(Context);
}

AL_API ALvoid AL_APIENTRY alBufferSubDataEXT(ALuint buffer,ALenum format,const ALvoid *data,ALsizei offset,ALsizei length)
{
    alBufferSubDataSOFT(buffer, format, data, offset, length);
}


AL_API void AL_APIENTRY alBufferf(ALuint buffer, ALenum eParam, ALfloat flValue)
{
    ALCcontext    *pContext;
    ALCdevice     *device;

    (void)flValue;

    pContext = GetContextSuspended();
    if(!pContext) return;

    device = pContext->Device;
    if(LookupBuffer(device->BufferMap, buffer) == NULL)
        alSetError(pContext, AL_INVALID_NAME);
    else
    {
        switch(eParam)
        {
        default:
            alSetError(pContext, AL_INVALID_ENUM);
            break;
        }
    }

    ProcessContext(pContext);
}


AL_API void AL_APIENTRY alBuffer3f(ALuint buffer, ALenum eParam, ALfloat flValue1, ALfloat flValue2, ALfloat flValue3)
{
    ALCcontext    *pContext;
    ALCdevice     *device;

    (void)flValue1;
    (void)flValue2;
    (void)flValue3;

    pContext = GetContextSuspended();
    if(!pContext) return;

    device = pContext->Device;
    if(LookupBuffer(device->BufferMap, buffer) == NULL)
        alSetError(pContext, AL_INVALID_NAME);
    else
    {
        switch(eParam)
        {
        default:
            alSetError(pContext, AL_INVALID_ENUM);
            break;
        }
    }

    ProcessContext(pContext);
}


AL_API void AL_APIENTRY alBufferfv(ALuint buffer, ALenum eParam, const ALfloat* flValues)
{
    ALCcontext    *pContext;
    ALCdevice     *device;

    pContext = GetContextSuspended();
    if(!pContext) return;

    device = pContext->Device;
    if(!flValues)
        alSetError(pContext, AL_INVALID_VALUE);
    else if(LookupBuffer(device->BufferMap, buffer) == NULL)
        alSetError(pContext, AL_INVALID_NAME);
    else
    {
        switch(eParam)
        {
        default:
            alSetError(pContext, AL_INVALID_ENUM);
            break;
        }
    }

    ProcessContext(pContext);
}


AL_API void AL_APIENTRY alBufferi(ALuint buffer, ALenum eParam, ALint lValue)
{
    ALCcontext    *pContext;
    ALCdevice     *device;

    (void)lValue;

    pContext = GetContextSuspended();
    if(!pContext) return;

    device = pContext->Device;
    if(LookupBuffer(device->BufferMap, buffer) == NULL)
        alSetError(pContext, AL_INVALID_NAME);
    else
    {
        switch(eParam)
        {
        default:
            alSetError(pContext, AL_INVALID_ENUM);
            break;
        }
    }

    ProcessContext(pContext);
}


AL_API void AL_APIENTRY alBuffer3i( ALuint buffer, ALenum eParam, ALint lValue1, ALint lValue2, ALint lValue3)
{
    ALCcontext    *pContext;
    ALCdevice     *device;

    (void)lValue1;
    (void)lValue2;
    (void)lValue3;

    pContext = GetContextSuspended();
    if(!pContext) return;

    device = pContext->Device;
    if(LookupBuffer(device->BufferMap, buffer) == NULL)
        alSetError(pContext, AL_INVALID_NAME);
    else
    {
        switch(eParam)
        {
        default:
            alSetError(pContext, AL_INVALID_ENUM);
            break;
        }
    }

    ProcessContext(pContext);
}


AL_API void AL_APIENTRY alBufferiv(ALuint buffer, ALenum eParam, const ALint* plValues)
{
    ALCcontext    *pContext;
    ALCdevice     *device;
    ALbuffer      *ALBuf;

    pContext = GetContextSuspended();
    if(!pContext) return;

    device = pContext->Device;
    if(!plValues)
        alSetError(pContext, AL_INVALID_VALUE);
    else if((ALBuf=LookupBuffer(device->BufferMap, buffer)) == NULL)
        alSetError(pContext, AL_INVALID_NAME);
    else
    {
        switch(eParam)
        {
        case AL_LOOP_POINTS:
            if(ALBuf->refcount > 0)
                alSetError(pContext, AL_INVALID_OPERATION);
            else if(plValues[0] < 0 || plValues[1] < 0 ||
                    plValues[0] >= plValues[1] || ALBuf->size == 0)
                alSetError(pContext, AL_INVALID_VALUE);
            else
            {
                ALint maxlen = ALBuf->size /
                               FrameSizeFromFmt(ALBuf->FmtChannels, ALBuf->FmtType);
                if(plValues[0] > maxlen || plValues[1] > maxlen)
                    alSetError(pContext, AL_INVALID_VALUE);
                else
                {
                    ALBuf->LoopStart = plValues[0];
                    ALBuf->LoopEnd = plValues[1];
                }
            }
            break;

        default:
            alSetError(pContext, AL_INVALID_ENUM);
            break;
        }
    }

    ProcessContext(pContext);
}


AL_API ALvoid AL_APIENTRY alGetBufferf(ALuint buffer, ALenum eParam, ALfloat *pflValue)
{
    ALCcontext    *pContext;
    ALCdevice     *device;

    pContext = GetContextSuspended();
    if(!pContext) return;

    device = pContext->Device;
    if(!pflValue)
        alSetError(pContext, AL_INVALID_VALUE);
    else if(LookupBuffer(device->BufferMap, buffer) == NULL)
        alSetError(pContext, AL_INVALID_NAME);
    else
    {
        switch(eParam)
        {
        default:
            alSetError(pContext, AL_INVALID_ENUM);
            break;
        }
    }

    ProcessContext(pContext);
}


AL_API void AL_APIENTRY alGetBuffer3f(ALuint buffer, ALenum eParam, ALfloat* pflValue1, ALfloat* pflValue2, ALfloat* pflValue3)
{
    ALCcontext    *pContext;
    ALCdevice     *device;

    pContext = GetContextSuspended();
    if(!pContext) return;

    device = pContext->Device;
    if(!pflValue1 || !pflValue2 || !pflValue3)
        alSetError(pContext, AL_INVALID_VALUE);
    else if(LookupBuffer(device->BufferMap, buffer) == NULL)
        alSetError(pContext, AL_INVALID_NAME);
    else
    {
        switch(eParam)
        {
        default:
            alSetError(pContext, AL_INVALID_ENUM);
            break;
        }
    }

    ProcessContext(pContext);
}


AL_API void AL_APIENTRY alGetBufferfv(ALuint buffer, ALenum eParam, ALfloat* pflValues)
{
    ALCcontext    *pContext;
    ALCdevice     *device;

    pContext = GetContextSuspended();
    if(!pContext) return;

    device = pContext->Device;
    if(!pflValues)
        alSetError(pContext, AL_INVALID_VALUE);
    else if(LookupBuffer(device->BufferMap, buffer) == NULL)
        alSetError(pContext, AL_INVALID_NAME);
    else
    {
        switch(eParam)
        {
        default:
            alSetError(pContext, AL_INVALID_ENUM);
            break;
        }
    }

    ProcessContext(pContext);
}


AL_API ALvoid AL_APIENTRY alGetBufferi(ALuint buffer, ALenum eParam, ALint *plValue)
{
    ALCcontext    *pContext;
    ALbuffer      *pBuffer;
    ALCdevice     *device;

    pContext = GetContextSuspended();
    if(!pContext) return;

    device = pContext->Device;
    if(!plValue)
        alSetError(pContext, AL_INVALID_VALUE);
    else if((pBuffer=LookupBuffer(device->BufferMap, buffer)) == NULL)
        alSetError(pContext, AL_INVALID_NAME);
    else
    {
        switch(eParam)
        {
        case AL_FREQUENCY:
            *plValue = pBuffer->Frequency;
            break;

        case AL_BITS:
            *plValue = BytesFromFmt(pBuffer->FmtType) * 8;
            break;

        case AL_CHANNELS:
            *plValue = ChannelsFromFmt(pBuffer->FmtChannels);
            break;

        case AL_SIZE:
            *plValue = pBuffer->size;
            break;

        default:
            alSetError(pContext, AL_INVALID_ENUM);
            break;
        }
    }

    ProcessContext(pContext);
}


AL_API void AL_APIENTRY alGetBuffer3i(ALuint buffer, ALenum eParam, ALint* plValue1, ALint* plValue2, ALint* plValue3)
{
    ALCcontext    *pContext;
    ALCdevice     *device;

    pContext = GetContextSuspended();
    if(!pContext) return;

    device = pContext->Device;
    if(!plValue1 || !plValue2 || !plValue3)
        alSetError(pContext, AL_INVALID_VALUE);
    else if(LookupBuffer(device->BufferMap, buffer) == NULL)
        alSetError(pContext, AL_INVALID_NAME);
    else
    {
        switch(eParam)
        {
        default:
            alSetError(pContext, AL_INVALID_ENUM);
            break;
        }
    }

    ProcessContext(pContext);
}


AL_API void AL_APIENTRY alGetBufferiv(ALuint buffer, ALenum eParam, ALint* plValues)
{
    ALCcontext    *pContext;
    ALCdevice     *device;
    ALbuffer      *ALBuf;

    pContext = GetContextSuspended();
    if(!pContext) return;

    device = pContext->Device;
    if(!plValues)
        alSetError(pContext, AL_INVALID_VALUE);
    else if((ALBuf=LookupBuffer(device->BufferMap, buffer)) == NULL)
        alSetError(pContext, AL_INVALID_NAME);
    else
    {
        switch(eParam)
        {
        case AL_FREQUENCY:
        case AL_BITS:
        case AL_CHANNELS:
        case AL_SIZE:
            alGetBufferi(buffer, eParam, plValues);
            break;

        case AL_LOOP_POINTS:
            plValues[0] = ALBuf->LoopStart;
            plValues[1] = ALBuf->LoopEnd;
            break;

        default:
            alSetError(pContext, AL_INVALID_ENUM);
            break;
        }
    }

    ProcessContext(pContext);
}


static void ConvertDataIMA4(ALvoid *dst, const ALvoid *src, ALint chans, ALsizei len)
{
    const ALubyte *IMAData;
    ALint Sample[2],Index[2];
    ALuint IMACode[2];
    ALsizei i,j,k,c;

    if(src == NULL)
        return;

    IMAData = src;
    for(i = 0;i < len;i++)
    {
        for(c = 0;c < chans;c++)
        {
            Sample[c]  = *(IMAData++);
            Sample[c] |= *(IMAData++) << 8;
            Sample[c]  = (Sample[c]^0x8000) - 32768;
            Index[c]  = *(IMAData++);
            Index[c] |= *(IMAData++) << 8;
            Index[c]  = (Index[c]^0x8000) - 32768;

            Index[c] = ((Index[c]<0) ? 0 : Index[c]);
            Index[c] = ((Index[c]>88) ? 88 : Index[c]);

            ((ALshort*)dst)[i*65*chans + c] = Sample[c];
        }

        for(j = 1;j < 65;j += 8)
        {
            for(c = 0;c < chans;c++)
            {
                IMACode[c]  = *(IMAData++);
                IMACode[c] |= *(IMAData++) << 8;
                IMACode[c] |= *(IMAData++) << 16;
                IMACode[c] |= *(IMAData++) << 24;
            }

            for(k = 0;k < 8;k++)
            {
                for(c = 0;c < chans;c++)
                {
                    Sample[c] += ((g_IMAStep_size[Index[c]]*g_IMACodeword_4[IMACode[c]&15])/8);
                    Index[c] += g_IMAIndex_adjust_4[IMACode[c]&15];

                    if(Sample[c] < -32768) Sample[c] = -32768;
                    else if(Sample[c] > 32767) Sample[c] = 32767;

                    if(Index[c]<0) Index[c] = 0;
                    else if(Index[c]>88) Index[c] = 88;

                    ((ALshort*)dst)[(i*65+j+k)*chans + c] = Sample[c];
                    IMACode[c] >>= 4;
                }
            }
        }
    }
}


typedef ALubyte ALmulaw;

static __inline ALbyte Conv_ALbyte_ALbyte(ALbyte val)
{ return val; }
static __inline ALbyte Conv_ALbyte_ALubyte(ALubyte val)
{ return val^0x80; }
static __inline ALbyte Conv_ALbyte_ALshort(ALshort val)
{ return val>>8; }
static __inline ALbyte Conv_ALbyte_ALushort(ALushort val)
{ return (val>>8)-128; }
static __inline ALbyte Conv_ALbyte_ALint(ALint val)
{ return val>>24; }
static __inline ALbyte Conv_ALbyte_ALuint(ALuint val)
{ return (val>>24)-128; }
static __inline ALbyte Conv_ALbyte_ALfloat(ALfloat val)
{
    if(val >= 1.0f) return 127;
    if(val <= -1.0f) return -128;
    return (ALint)(val * 127.0f);
}
static __inline ALbyte Conv_ALbyte_ALdouble(ALdouble val)
{
    if(val >= 1.0) return 127;
    if(val <= -1.0) return -128;
    return (ALint)(val * 127.0);
}
static __inline ALbyte Conv_ALbyte_ALmulaw(ALmulaw val)
{ return muLawDecompressionTable[val]>>8; }

static __inline ALubyte Conv_ALubyte_ALbyte(ALbyte val)
{ return val^0x80; }
static __inline ALubyte Conv_ALubyte_ALubyte(ALubyte val)
{ return val; }
static __inline ALubyte Conv_ALubyte_ALshort(ALshort val)
{ return (val>>8)+128; }
static __inline ALubyte Conv_ALubyte_ALushort(ALushort val)
{ return val>>8; }
static __inline ALubyte Conv_ALubyte_ALint(ALint val)
{ return (val>>24)+128; }
static __inline ALubyte Conv_ALubyte_ALuint(ALuint val)
{ return val>>24; }
static __inline ALubyte Conv_ALubyte_ALfloat(ALfloat val)
{
    if(val >= 1.0f) return 255;
    if(val <= -1.0f) return 0;
    return (ALint)(val * 127.0f) + 128;
}
static __inline ALubyte Conv_ALubyte_ALdouble(ALdouble val)
{
    if(val >= 1.0) return 255;
    if(val <= -1.0) return 0;
    return (ALint)(val * 127.0) + 128;
}
static __inline ALubyte Conv_ALubyte_ALmulaw(ALmulaw val)
{ return (muLawDecompressionTable[val]>>8)+128; }

static __inline ALshort Conv_ALshort_ALbyte(ALbyte val)
{ return val<<8; }
static __inline ALshort Conv_ALshort_ALubyte(ALubyte val)
{ return (val-128)<<8; }
static __inline ALshort Conv_ALshort_ALshort(ALshort val)
{ return val; }
static __inline ALshort Conv_ALshort_ALushort(ALushort val)
{ return val^0x8000; }
static __inline ALshort Conv_ALshort_ALint(ALint val)
{ return val>>16; }
static __inline ALshort Conv_ALshort_ALuint(ALuint val)
{ return (val>>16)-32768; }
static __inline ALshort Conv_ALshort_ALfloat(ALfloat val)
{
    if(val >= 1.0f) return 32767;
    if(val <= -1.0f) return -32768;
    return (ALint)(val * 32767.0f);
}
static __inline ALshort Conv_ALshort_ALdouble(ALdouble val)
{
    if(val >= 1.0) return 32767;
    if(val <= -1.0) return -32768;
    return (ALint)(val * 32767.0);
}
static __inline ALshort Conv_ALshort_ALmulaw(ALmulaw val)
{ return muLawDecompressionTable[val]; }

static __inline ALushort Conv_ALushort_ALbyte(ALbyte val)
{ return (val+128)<<8; }
static __inline ALushort Conv_ALushort_ALubyte(ALubyte val)
{ return val<<8; }
static __inline ALushort Conv_ALushort_ALshort(ALshort val)
{ return val^0x8000; }
static __inline ALushort Conv_ALushort_ALushort(ALushort val)
{ return val; }
static __inline ALushort Conv_ALushort_ALint(ALint val)
{ return (val>>16)+32768; }
static __inline ALushort Conv_ALushort_ALuint(ALuint val)
{ return val>>16; }
static __inline ALushort Conv_ALushort_ALfloat(ALfloat val)
{
    if(val >= 1.0f) return 65535;
    if(val <= -1.0f) return 0;
    return (ALint)(val * 32767.0f) + 32768;
}
static __inline ALushort Conv_ALushort_ALdouble(ALdouble val)
{
    if(val >= 1.0) return 65535;
    if(val <= -1.0) return 0;
    return (ALint)(val * 32767.0) + 32768;
}
static __inline ALushort Conv_ALushort_ALmulaw(ALmulaw val)
{ return muLawDecompressionTable[val]^0x8000; }

static __inline ALint Conv_ALint_ALbyte(ALbyte val)
{ return val<<24; }
static __inline ALint Conv_ALint_ALubyte(ALubyte val)
{ return (val-128)<<24; }
static __inline ALint Conv_ALint_ALshort(ALshort val)
{ return val<<16; }
static __inline ALint Conv_ALint_ALushort(ALushort val)
{ return (val-32768)<<16; }
static __inline ALint Conv_ALint_ALint(ALint val)
{ return val; }
static __inline ALint Conv_ALint_ALuint(ALuint val)
{ return val-2147483648u; }
static __inline ALint Conv_ALint_ALfloat(ALfloat val)
{
    if(val >= 1.0f) return 2147483647;
    if(val <= -1.0f) return -2147483648u;
    return (ALint)(val * 2147483647.0);
}
static __inline ALint Conv_ALint_ALdouble(ALdouble val)
{
    if(val >= 1.0) return 2147483647;
    if(val <= -1.0) return -2147483648u;
    return (ALint)(val * 2147483647.0);
}
static __inline ALint Conv_ALint_ALmulaw(ALmulaw val)
{ return muLawDecompressionTable[val]<<16; }

static __inline ALuint Conv_ALuint_ALbyte(ALbyte val)
{ return (val+128)<<24; }
static __inline ALuint Conv_ALuint_ALubyte(ALubyte val)
{ return val<<24; }
static __inline ALuint Conv_ALuint_ALshort(ALshort val)
{ return (val+32768)<<16; }
static __inline ALuint Conv_ALuint_ALushort(ALushort val)
{ return val<<16; }
static __inline ALuint Conv_ALuint_ALint(ALint val)
{ return val+2147483648u; }
static __inline ALuint Conv_ALuint_ALuint(ALuint val)
{ return val; }
static __inline ALuint Conv_ALuint_ALfloat(ALfloat val)
{
    if(val >= 1.0f) return 4294967295u;
    if(val <= -1.0f) return 0;
    return (ALint)(val * 2147483647.0) + 2147483648u;
}
static __inline ALuint Conv_ALuint_ALdouble(ALdouble val)
{
    if(val >= 1.0) return 4294967295u;
    if(val <= -1.0) return 0;
    return (ALint)(val * 2147483647.0) + 2147483648u;
}
static __inline ALuint Conv_ALuint_ALmulaw(ALmulaw val)
{ return (muLawDecompressionTable[val]+32768)<<16; }

static __inline ALfloat Conv_ALfloat_ALbyte(ALbyte val)
{ return val * (1.0f/127.0f); }
static __inline ALfloat Conv_ALfloat_ALubyte(ALubyte val)
{ return (val-128) * (1.0f/127.0f); }
static __inline ALfloat Conv_ALfloat_ALshort(ALshort val)
{ return val * (1.0f/32767.0f); }
static __inline ALfloat Conv_ALfloat_ALushort(ALushort val)
{ return (val-32768) * (1.0f/32767.0f); }
static __inline ALfloat Conv_ALfloat_ALint(ALint val)
{ return val * (1.0/2147483647.0); }
static __inline ALfloat Conv_ALfloat_ALuint(ALuint val)
{ return (ALint)(val-2147483648u) * (1.0/2147483647.0); }
static __inline ALfloat Conv_ALfloat_ALfloat(ALfloat val)
{ return val; }
static __inline ALfloat Conv_ALfloat_ALdouble(ALdouble val)
{ return val; }
static __inline ALfloat Conv_ALfloat_ALmulaw(ALmulaw val)
{ return muLawDecompressionTable[val] * (1.0f/32767.0f); }

static __inline ALdouble Conv_ALdouble_ALbyte(ALbyte val)
{ return val * (1.0/127.0); }
static __inline ALdouble Conv_ALdouble_ALubyte(ALubyte val)
{ return (val-128) * (1.0/127.0); }
static __inline ALdouble Conv_ALdouble_ALshort(ALshort val)
{ return val * (1.0/32767.0); }
static __inline ALdouble Conv_ALdouble_ALushort(ALushort val)
{ return (val-32768) * (1.0/32767.0); }
static __inline ALdouble Conv_ALdouble_ALint(ALint val)
{ return val * (1.0/214748364.0); }
static __inline ALdouble Conv_ALdouble_ALuint(ALuint val)
{ return (ALint)(val-2147483648u) * (1.0/2147483647.0); }
static __inline ALdouble Conv_ALdouble_ALfloat(ALfloat val)
{ return val; }
static __inline ALdouble Conv_ALdouble_ALdouble(ALdouble val)
{ return val; }
static __inline ALdouble Conv_ALdouble_ALmulaw(ALmulaw val)
{ return muLawDecompressionTable[val] * (1.0/32767.0); }


#define DECL_TEMPLATE(T1, T2)                                                 \
static void Convert_##T1##_##T2(T1 *dst, const T2 *src, ALuint len)           \
{                                                                             \
    ALuint i;                                                                 \
    for(i = 0;i < len;i++)                                                    \
        *(dst++) = Conv_##T1##_##T2(*(src++));                                \
}

DECL_TEMPLATE(ALbyte, ALbyte)
DECL_TEMPLATE(ALbyte, ALubyte)
DECL_TEMPLATE(ALbyte, ALshort)
DECL_TEMPLATE(ALbyte, ALushort)
DECL_TEMPLATE(ALbyte, ALint)
DECL_TEMPLATE(ALbyte, ALuint)
DECL_TEMPLATE(ALbyte, ALfloat)
DECL_TEMPLATE(ALbyte, ALdouble)
DECL_TEMPLATE(ALbyte, ALmulaw)

DECL_TEMPLATE(ALubyte, ALbyte)
DECL_TEMPLATE(ALubyte, ALubyte)
DECL_TEMPLATE(ALubyte, ALshort)
DECL_TEMPLATE(ALubyte, ALushort)
DECL_TEMPLATE(ALubyte, ALint)
DECL_TEMPLATE(ALubyte, ALuint)
DECL_TEMPLATE(ALubyte, ALfloat)
DECL_TEMPLATE(ALubyte, ALdouble)
DECL_TEMPLATE(ALubyte, ALmulaw)

DECL_TEMPLATE(ALshort, ALbyte)
DECL_TEMPLATE(ALshort, ALubyte)
DECL_TEMPLATE(ALshort, ALshort)
DECL_TEMPLATE(ALshort, ALushort)
DECL_TEMPLATE(ALshort, ALint)
DECL_TEMPLATE(ALshort, ALuint)
DECL_TEMPLATE(ALshort, ALfloat)
DECL_TEMPLATE(ALshort, ALdouble)
DECL_TEMPLATE(ALshort, ALmulaw)

DECL_TEMPLATE(ALushort, ALbyte)
DECL_TEMPLATE(ALushort, ALubyte)
DECL_TEMPLATE(ALushort, ALshort)
DECL_TEMPLATE(ALushort, ALushort)
DECL_TEMPLATE(ALushort, ALint)
DECL_TEMPLATE(ALushort, ALuint)
DECL_TEMPLATE(ALushort, ALfloat)
DECL_TEMPLATE(ALushort, ALdouble)
DECL_TEMPLATE(ALushort, ALmulaw)

DECL_TEMPLATE(ALint, ALbyte)
DECL_TEMPLATE(ALint, ALubyte)
DECL_TEMPLATE(ALint, ALshort)
DECL_TEMPLATE(ALint, ALushort)
DECL_TEMPLATE(ALint, ALint)
DECL_TEMPLATE(ALint, ALuint)
DECL_TEMPLATE(ALint, ALfloat)
DECL_TEMPLATE(ALint, ALdouble)
DECL_TEMPLATE(ALint, ALmulaw)

DECL_TEMPLATE(ALuint, ALbyte)
DECL_TEMPLATE(ALuint, ALubyte)
DECL_TEMPLATE(ALuint, ALshort)
DECL_TEMPLATE(ALuint, ALushort)
DECL_TEMPLATE(ALuint, ALint)
DECL_TEMPLATE(ALuint, ALuint)
DECL_TEMPLATE(ALuint, ALfloat)
DECL_TEMPLATE(ALuint, ALdouble)
DECL_TEMPLATE(ALuint, ALmulaw)

DECL_TEMPLATE(ALfloat, ALbyte)
DECL_TEMPLATE(ALfloat, ALubyte)
DECL_TEMPLATE(ALfloat, ALshort)
DECL_TEMPLATE(ALfloat, ALushort)
DECL_TEMPLATE(ALfloat, ALint)
DECL_TEMPLATE(ALfloat, ALuint)
DECL_TEMPLATE(ALfloat, ALfloat)
DECL_TEMPLATE(ALfloat, ALdouble)
DECL_TEMPLATE(ALfloat, ALmulaw)

DECL_TEMPLATE(ALdouble, ALbyte)
DECL_TEMPLATE(ALdouble, ALubyte)
DECL_TEMPLATE(ALdouble, ALshort)
DECL_TEMPLATE(ALdouble, ALushort)
DECL_TEMPLATE(ALdouble, ALint)
DECL_TEMPLATE(ALdouble, ALuint)
DECL_TEMPLATE(ALdouble, ALfloat)
DECL_TEMPLATE(ALdouble, ALdouble)
DECL_TEMPLATE(ALdouble, ALmulaw)

#undef DECL_TEMPLATE

#define DECL_TEMPLATE(T)                                                      \
static void Convert_##T(T *dst, const ALvoid *src, enum SrcFmtType srcType,   \
                        ALsizei len)                                          \
{                                                                             \
    switch(srcType)                                                           \
    {                                                                         \
        case SrcFmtByte:                                                      \
            Convert_##T##_ALbyte(dst, src, len);                              \
            break;                                                            \
        case SrcFmtUByte:                                                     \
            Convert_##T##_ALubyte(dst, src, len);                             \
            break;                                                            \
        case SrcFmtShort:                                                     \
            Convert_##T##_ALshort(dst, src, len);                             \
            break;                                                            \
        case SrcFmtUShort:                                                    \
            Convert_##T##_ALushort(dst, src, len);                            \
            break;                                                            \
        case SrcFmtInt:                                                       \
            Convert_##T##_ALint(dst, src, len);                               \
            break;                                                            \
        case SrcFmtUInt:                                                      \
            Convert_##T##_ALuint(dst, src, len);                              \
            break;                                                            \
        case SrcFmtFloat:                                                     \
            Convert_##T##_ALfloat(dst, src, len);                             \
            break;                                                            \
        case SrcFmtDouble:                                                    \
            Convert_##T##_ALdouble(dst, src, len);                            \
            break;                                                            \
        case SrcFmtMulaw:                                                     \
            Convert_##T##_ALmulaw(dst, src, len);                             \
            break;                                                            \
        case SrcFmtIMA4:                                                      \
            break; /* not handled here */                                     \
    }                                                                         \
}

DECL_TEMPLATE(ALbyte)
DECL_TEMPLATE(ALubyte)
DECL_TEMPLATE(ALshort)
DECL_TEMPLATE(ALushort)
DECL_TEMPLATE(ALint)
DECL_TEMPLATE(ALuint)
DECL_TEMPLATE(ALfloat)
DECL_TEMPLATE(ALdouble)

#undef DECL_TEMPLATE


/*
 * LoadData
 *
 * Loads the specified data into the buffer, using the specified formats.
 * Currently, the new format must have the same channel configuration as the
 * original format. This does NOT handle compressed formats (eg. IMA4).
 */
static ALenum LoadData(ALbuffer *ALBuf, const ALvoid *data, ALsizei size, ALuint freq, ALenum OrigFormat, ALenum NewFormat)
{
    ALuint NewBytes = aluBytesFromFormat(NewFormat);
    ALuint NewChannels = aluChannelsFromFormat(NewFormat);
    ALuint OrigBytes = aluBytesFromFormat(OrigFormat);
    ALuint OrigChannels = aluChannelsFromFormat(OrigFormat);
    enum SrcFmtChannels SrcChannels;
    enum FmtChannels DstChannels;
    enum SrcFmtType SrcType;
    enum FmtType DstType;
    ALuint64 newsize;
    ALvoid *temp;

    assert(NewChannels == OrigChannels);

    DecomposeInputFormat(OrigFormat, &SrcChannels, &SrcType);
    DecomposeFormat(NewFormat, &DstChannels, &DstType);

    if((size%(OrigBytes*OrigChannels)) != 0)
        return AL_INVALID_VALUE;

    newsize = size / OrigBytes;
    newsize *= NewBytes;
    if(newsize > INT_MAX)
        return AL_OUT_OF_MEMORY;

    temp = realloc(ALBuf->data, newsize);
    if(!temp) return AL_OUT_OF_MEMORY;
    ALBuf->data = temp;
    ALBuf->size = newsize;

    if(data != NULL)
    {
        // Samples are converted here
        ConvertData(ALBuf->data, DstType, data, SrcType, newsize/NewBytes);
    }

    ALBuf->Frequency = freq;
    ALBuf->FmtType = DstType;
    ALBuf->FmtChannels = DstChannels;

    ALBuf->LoopStart = 0;
    ALBuf->LoopEnd = newsize / NewChannels / NewBytes;

    ALBuf->OriginalChannels = SrcChannels;
    ALBuf->OriginalType     = SrcType;
    ALBuf->OriginalSize     = size;
    ALBuf->OriginalAlign    = OrigBytes * OrigChannels;

    return AL_NO_ERROR;
}

static void ConvertData(ALvoid *dst, enum FmtType dstType, const ALvoid *src, enum SrcFmtType srcType, ALsizei len)
{
    switch(dstType)
    {
        (void)Convert_ALbyte;
        case FmtUByte:
            Convert_ALubyte(dst, src, srcType, len);
            break;
        case FmtShort:
            Convert_ALshort(dst, src, srcType, len);
            break;
        (void)Convert_ALushort;
        (void)Convert_ALint;
        (void)Convert_ALuint;
        case FmtFloat:
            Convert_ALfloat(dst, src, srcType, len);
            break;
        (void)Convert_ALdouble;
    }
}


ALuint BytesFromSrcFmt(enum SrcFmtType type)
{
    switch(type)
    {
    case SrcFmtByte: return sizeof(ALbyte);
    case SrcFmtUByte: return sizeof(ALubyte);
    case SrcFmtShort: return sizeof(ALshort);
    case SrcFmtUShort: return sizeof(ALushort);
    case SrcFmtInt: return sizeof(ALint);
    case SrcFmtUInt: return sizeof(ALuint);
    case SrcFmtFloat: return sizeof(ALfloat);
    case SrcFmtDouble: return sizeof(ALdouble);
    case SrcFmtMulaw: return sizeof(ALubyte);
    case SrcFmtIMA4: break; /* not handled here */
    }
    return 0;
}
ALuint ChannelsFromSrcFmt(enum SrcFmtChannels chans)
{
    switch(chans)
    {
    case SrcFmtMono: return 1;
    case SrcFmtStereo: return 2;
    case SrcFmtRear: return 2;
    case SrcFmtQuad: return 4;
    case SrcFmtX51: return 6;
    case SrcFmtX61: return 7;
    case SrcFmtX71: return 8;
    }
    return 0;
}
ALboolean DecomposeInputFormat(ALenum format, enum SrcFmtChannels *chans,
                               enum SrcFmtType *type)
{
    switch(format)
    {
        case AL_FORMAT_MONO8:
            *chans = SrcFmtMono;
            *type  = SrcFmtUByte;
            return AL_TRUE;
        case AL_FORMAT_MONO16:
            *chans = SrcFmtMono;
            *type  = SrcFmtShort;
            return AL_TRUE;
        case AL_FORMAT_MONO_FLOAT32:
            *chans = SrcFmtMono;
            *type  = SrcFmtFloat;
            return AL_TRUE;
        case AL_FORMAT_MONO_DOUBLE_EXT:
            *chans = SrcFmtMono;
            *type  = SrcFmtDouble;
            return AL_TRUE;
        case AL_FORMAT_MONO_IMA4:
            *chans = SrcFmtMono;
            *type  = SrcFmtIMA4;
            return AL_TRUE;
        case AL_FORMAT_STEREO8:
            *chans = SrcFmtStereo;
            *type  = SrcFmtUByte;
            return AL_TRUE;
        case AL_FORMAT_STEREO16:
            *chans = SrcFmtStereo;
            *type  = SrcFmtShort;
            return AL_TRUE;
        case AL_FORMAT_STEREO_FLOAT32:
            *chans = SrcFmtStereo;
            *type  = SrcFmtFloat;
            return AL_TRUE;
        case AL_FORMAT_STEREO_DOUBLE_EXT:
            *chans = SrcFmtStereo;
            *type  = SrcFmtDouble;
            return AL_TRUE;
        case AL_FORMAT_STEREO_IMA4:
            *chans = SrcFmtStereo;
            *type  = SrcFmtIMA4;
            return AL_TRUE;
        case AL_FORMAT_QUAD8_LOKI:
        case AL_FORMAT_QUAD8:
            *chans = SrcFmtQuad;
            *type  = SrcFmtUByte;
            return AL_TRUE;
        case AL_FORMAT_QUAD16_LOKI:
        case AL_FORMAT_QUAD16:
            *chans = SrcFmtQuad;
            *type  = SrcFmtShort;
            return AL_TRUE;
        case AL_FORMAT_QUAD32:
            *chans = SrcFmtQuad;
            *type  = SrcFmtFloat;
            return AL_TRUE;
        case AL_FORMAT_REAR8:
            *chans = SrcFmtRear;
            *type  = SrcFmtUByte;
            return AL_TRUE;
        case AL_FORMAT_REAR16:
            *chans = SrcFmtRear;
            *type  = SrcFmtShort;
            return AL_TRUE;
        case AL_FORMAT_REAR32:
            *chans = SrcFmtRear;
            *type  = SrcFmtFloat;
            return AL_TRUE;
        case AL_FORMAT_51CHN8:
            *chans = SrcFmtX51;
            *type  = SrcFmtUByte;
            return AL_TRUE;
        case AL_FORMAT_51CHN16:
            *chans = SrcFmtX51;
            *type  = SrcFmtShort;
            return AL_TRUE;
        case AL_FORMAT_51CHN32:
            *chans = SrcFmtX51;
            *type  = SrcFmtFloat;
            return AL_TRUE;
        case AL_FORMAT_61CHN8:
            *chans = SrcFmtX61;
            *type  = SrcFmtUByte;
            return AL_TRUE;
        case AL_FORMAT_61CHN16:
            *chans = SrcFmtX61;
            *type  = SrcFmtShort;
            return AL_TRUE;
        case AL_FORMAT_61CHN32:
            *chans = SrcFmtX61;
            *type  = SrcFmtFloat;
            return AL_TRUE;
        case AL_FORMAT_71CHN8:
            *chans = SrcFmtX71;
            *type  = SrcFmtUByte;
            return AL_TRUE;
        case AL_FORMAT_71CHN16:
            *chans = SrcFmtX71;
            *type  = SrcFmtShort;
            return AL_TRUE;
        case AL_FORMAT_71CHN32:
            *chans = SrcFmtX71;
            *type  = SrcFmtFloat;
            return AL_TRUE;
        case AL_FORMAT_MONO_MULAW:
            *chans = SrcFmtMono;
            *type  = SrcFmtMulaw;
            return AL_TRUE;
        case AL_FORMAT_STEREO_MULAW:
            *chans = SrcFmtStereo;
            *type  = SrcFmtMulaw;
            return AL_TRUE;
        case AL_FORMAT_QUAD_MULAW:
            *chans = SrcFmtQuad;
            *type  = SrcFmtMulaw;
            return AL_TRUE;
        case AL_FORMAT_REAR_MULAW:
            *chans = SrcFmtRear;
            *type  = SrcFmtMulaw;
            return AL_TRUE;
        case AL_FORMAT_51CHN_MULAW:
            *chans = SrcFmtX51;
            *type  = SrcFmtMulaw;
            return AL_TRUE;
        case AL_FORMAT_61CHN_MULAW:
            *chans = SrcFmtX61;
            *type  = SrcFmtMulaw;
            return AL_TRUE;
        case AL_FORMAT_71CHN_MULAW:
            *chans = SrcFmtX71;
            *type  = SrcFmtMulaw;
            return AL_TRUE;
    }
    return AL_FALSE;
}

ALuint BytesFromFmt(enum FmtType type)
{
    switch(type)
    {
    case FmtUByte: return sizeof(ALubyte);
    case FmtShort: return sizeof(ALshort);
    case FmtFloat: return sizeof(ALfloat);
    }
    return 0;
}
ALuint ChannelsFromFmt(enum FmtChannels chans)
{
    switch(chans)
    {
    case FmtMono: return 1;
    case FmtStereo: return 2;
    case FmtRear: return 2;
    case FmtQuad: return 4;
    case FmtX51: return 6;
    case FmtX61: return 7;
    case FmtX71: return 8;
    }
    return 0;
}
ALboolean DecomposeFormat(ALenum format, enum FmtChannels *chans, enum FmtType *type)
{
    switch(format)
    {
        case AL_FORMAT_MONO8:
            *chans = FmtMono;
            *type  = FmtUByte;
            return AL_TRUE;
        case AL_FORMAT_MONO16:
            *chans = FmtMono;
            *type  = FmtShort;
            return AL_TRUE;
        case AL_FORMAT_MONO_FLOAT32:
            *chans = FmtMono;
            *type  = FmtFloat;
            return AL_TRUE;
        case AL_FORMAT_STEREO8:
            *chans = FmtStereo;
            *type  = FmtUByte;
            return AL_TRUE;
        case AL_FORMAT_STEREO16:
            *chans = FmtStereo;
            *type  = FmtShort;
            return AL_TRUE;
        case AL_FORMAT_STEREO_FLOAT32:
            *chans = FmtStereo;
            *type  = FmtFloat;
            return AL_TRUE;
        case AL_FORMAT_QUAD8_LOKI:
        case AL_FORMAT_QUAD8:
            *chans = FmtQuad;
            *type  = FmtUByte;
            return AL_TRUE;
        case AL_FORMAT_QUAD16_LOKI:
        case AL_FORMAT_QUAD16:
            *chans = FmtQuad;
            *type  = FmtShort;
            return AL_TRUE;
        case AL_FORMAT_QUAD32:
            *chans = FmtQuad;
            *type  = FmtFloat;
            return AL_TRUE;
        case AL_FORMAT_REAR8:
            *chans = FmtRear;
            *type  = FmtUByte;
            return AL_TRUE;
        case AL_FORMAT_REAR16:
            *chans = FmtRear;
            *type  = FmtShort;
            return AL_TRUE;
        case AL_FORMAT_REAR32:
            *chans = FmtRear;
            *type  = FmtFloat;
            return AL_TRUE;
        case AL_FORMAT_51CHN8:
            *chans = FmtX51;
            *type  = FmtUByte;
            return AL_TRUE;
        case AL_FORMAT_51CHN16:
            *chans = FmtX51;
            *type  = FmtShort;
            return AL_TRUE;
        case AL_FORMAT_51CHN32:
            *chans = FmtX51;
            *type  = FmtFloat;
            return AL_TRUE;
        case AL_FORMAT_61CHN8:
            *chans = FmtX61;
            *type  = FmtUByte;
            return AL_TRUE;
        case AL_FORMAT_61CHN16:
            *chans = FmtX61;
            *type  = FmtShort;
            return AL_TRUE;
        case AL_FORMAT_61CHN32:
            *chans = FmtX61;
            *type  = FmtFloat;
            return AL_TRUE;
        case AL_FORMAT_71CHN8:
            *chans = FmtX71;
            *type  = FmtUByte;
            return AL_TRUE;
        case AL_FORMAT_71CHN16:
            *chans = FmtX71;
            *type  = FmtShort;
            return AL_TRUE;
        case AL_FORMAT_71CHN32:
            *chans = FmtX71;
            *type  = FmtFloat;
            return AL_TRUE;
    }
    return AL_FALSE;
}


/*
 *    ReleaseALBuffers()
 *
 *    INTERNAL FN : Called by alcCloseDevice to destroy any buffers that still exist
 */
ALvoid ReleaseALBuffers(ALCdevice *device)
{
    ALsizei i;
    for(i = 0;i < device->BufferMap.size;i++)
    {
        ALbuffer *temp = device->BufferMap.array[i].value;
        device->BufferMap.array[i].value = NULL;

        // Release sample data
        free(temp->data);

        // Release Buffer structure
        ALTHUNK_REMOVEENTRY(temp->buffer);
        memset(temp, 0, sizeof(ALbuffer));
        free(temp);
    }
}
