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
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#include "alMain.h"
#include "alu.h"
#include "alError.h"
#include "alBuffer.h"
#include "alThunk.h"
#include "sample_cvt.h"


extern inline void LockBuffersRead(ALCdevice *device);
extern inline void UnlockBuffersRead(ALCdevice *device);
extern inline void LockBuffersWrite(ALCdevice *device);
extern inline void UnlockBuffersWrite(ALCdevice *device);
extern inline struct ALbuffer *LookupBuffer(ALCdevice *device, ALuint id);
extern inline struct ALbuffer *RemoveBuffer(ALCdevice *device, ALuint id);
extern inline ALsizei FrameSizeFromUserFmt(enum UserFmtChannels chans, enum UserFmtType type);
extern inline ALsizei FrameSizeFromFmt(enum FmtChannels chans, enum FmtType type);

static void LoadData(ALCcontext *context, ALbuffer *buffer, ALuint freq, ALsizei frames,
                     enum UserFmtChannels SrcChannels, enum UserFmtType SrcType,
                     const ALvoid *data, ALsizei align, ALbitfieldSOFT access);
static ALboolean DecomposeUserFormat(ALenum format, enum UserFmtChannels *chans, enum UserFmtType *type);
static ALsizei SanitizeAlignment(enum UserFmtType type, ALsizei align);


#define INVALID_STORAGE_MASK ~(AL_MAP_READ_BIT_SOFT | AL_MAP_WRITE_BIT_SOFT | AL_PRESERVE_DATA_BIT_SOFT | AL_MAP_PERSISTENT_BIT_SOFT)
#define MAP_READ_WRITE_FLAGS (AL_MAP_READ_BIT_SOFT | AL_MAP_WRITE_BIT_SOFT)
#define MAP_ACCESS_FLAGS (AL_MAP_READ_BIT_SOFT | AL_MAP_WRITE_BIT_SOFT | AL_MAP_PERSISTENT_BIT_SOFT)


AL_API ALvoid AL_APIENTRY alGenBuffers(ALsizei n, ALuint *buffers)
{
    ALCcontext *context;
    ALsizei cur = 0;

    context = GetContextRef();
    if(!context) return;

    if(!(n >= 0))
        SETERR_GOTO(context, AL_INVALID_VALUE, done, "Generating %d buffers", n);

    for(cur = 0;cur < n;cur++)
    {
        ALbuffer *buffer = NewBuffer(context);
        if(!buffer)
        {
            alDeleteBuffers(cur, buffers);
            break;
        }

        buffers[cur] = buffer->id;
    }

done:
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alDeleteBuffers(ALsizei n, const ALuint *buffers)
{
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer *ALBuf;
    ALsizei i;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;

    LockBuffersWrite(device);
    if(!(n >= 0))
        SETERR_GOTO(context, AL_INVALID_VALUE, done, "Deleting %d buffers", n);

    for(i = 0;i < n;i++)
    {
        if(!buffers[i])
            continue;

        /* Check for valid Buffer ID */
        if((ALBuf=LookupBuffer(device, buffers[i])) == NULL)
            SETERR_GOTO(context, AL_INVALID_NAME, done, "Invalid buffer ID %u", buffers[i]);
        if(ReadRef(&ALBuf->ref) != 0)
            SETERR_GOTO(context, AL_INVALID_OPERATION, done, "Deleting in-use buffer %u",
                        buffers[i]);
    }

    for(i = 0;i < n;i++)
    {
        if((ALBuf=LookupBuffer(device, buffers[i])) != NULL)
            DeleteBuffer(device, ALBuf);
    }

done:
    UnlockBuffersWrite(device);
    ALCcontext_DecRef(context);
}

AL_API ALboolean AL_APIENTRY alIsBuffer(ALuint buffer)
{
    ALCcontext *context;
    ALboolean ret;

    context = GetContextRef();
    if(!context) return AL_FALSE;

    LockBuffersRead(context->Device);
    ret = ((!buffer || LookupBuffer(context->Device, buffer)) ?
           AL_TRUE : AL_FALSE);
    UnlockBuffersRead(context->Device);

    ALCcontext_DecRef(context);

    return ret;
}


AL_API ALvoid AL_APIENTRY alBufferData(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq)
{ alBufferStorageSOFT(buffer, format, data, size, freq, 0); }

AL_API void AL_APIENTRY alBufferStorageSOFT(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq, ALbitfieldSOFT flags)
{
    enum UserFmtChannels srcchannels = UserFmtMono;
    enum UserFmtType srctype = UserFmtUByte;
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer *albuf;
    ALsizei framesize = 1;
    ALsizei align;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    LockBuffersRead(device);
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SETERR_GOTO(context, AL_INVALID_NAME, done, "Invalid buffer ID %u", buffer);
    if(!(size >= 0)) SETERR_GOTO(context, AL_INVALID_VALUE, done, "Negative storage size %d", size);
    if(!(freq > 0)) SETERR_GOTO(context, AL_INVALID_VALUE, done, "Invalid sample rate %d", freq);
    if((flags&INVALID_STORAGE_MASK) != 0)
        SETERR_GOTO(context, AL_INVALID_VALUE, done, "Invalid storage flags 0x%x",
                    flags&INVALID_STORAGE_MASK);
    if((flags&AL_MAP_PERSISTENT_BIT_SOFT) && !(flags&MAP_READ_WRITE_FLAGS))
        SETERR_GOTO(context, AL_INVALID_VALUE, done,
                    "Declaring persistently mapped storage without read or write access");
    if(DecomposeUserFormat(format, &srcchannels, &srctype) == AL_FALSE)
        SETERR_GOTO(context, AL_INVALID_ENUM, done, "Invalid format 0x%04x", format);

    align = SanitizeAlignment(srctype, ATOMIC_LOAD_SEQ(&albuf->UnpackAlign));
    if(align < 1) SETERR_GOTO(context, AL_INVALID_VALUE, done, "Invalid unpack alignment");

    switch(srctype)
    {
        case UserFmtUByte:
        case UserFmtShort:
        case UserFmtFloat:
        case UserFmtDouble:
        case UserFmtMulaw:
        case UserFmtAlaw:
            framesize = FrameSizeFromUserFmt(srcchannels, srctype) * align;
            break;

        case UserFmtIMA4:
            framesize = ((align-1)/2 + 4) * ChannelsFromUserFmt(srcchannels);
            break;

        case UserFmtMSADPCM:
            framesize = ((align-2)/2 + 7) * ChannelsFromUserFmt(srcchannels);
            break;
    }
    if((size%framesize) != 0)
        alSetError(context, AL_INVALID_VALUE, "Data size %d is not a multiple of frame size %d",
                   size, framesize);
    else
        LoadData(context, albuf, freq, size/framesize*align, srcchannels, srctype, data, align,
                 flags);

done:
    UnlockBuffersRead(device);
    ALCcontext_DecRef(context);
}

AL_API void* AL_APIENTRY alMapBufferSOFT(ALuint buffer, ALsizei offset, ALsizei length, ALbitfieldSOFT access)
{
    void *retval = NULL;
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer *albuf;

    context = GetContextRef();
    if(!context) return retval;

    device = context->Device;
    LockBuffersRead(device);
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SETERR_GOTO(context, AL_INVALID_NAME, done, "Invalid buffer ID %u", buffer);
    if((access&~MAP_ACCESS_FLAGS) != 0)
        SETERR_GOTO(context, AL_INVALID_VALUE, done, "Invalid map flags 0x%x",
                    access&~MAP_ACCESS_FLAGS);
    if(!(access&MAP_READ_WRITE_FLAGS))
        SETERR_GOTO(context, AL_INVALID_VALUE, done,
                    "Mapping buffer without read or write access");

    WriteLock(&albuf->lock);
    if(ReadRef(&albuf->ref) != 0 && !(access&AL_MAP_PERSISTENT_BIT_SOFT))
        SETERR_GOTO(context, AL_INVALID_OPERATION, unlock_done,
                    "Mapping in-use buffer without persistent mapping");
    if(albuf->MappedAccess != 0)
        SETERR_GOTO(context, AL_INVALID_OPERATION, unlock_done, "Mapping already-mapped buffer");
    if((access&AL_MAP_READ_BIT_SOFT) && !(albuf->Access&AL_MAP_READ_BIT_SOFT))
        SETERR_GOTO(context, AL_INVALID_VALUE, unlock_done,
                    "Mapping buffer for reading without read access");
    if((access&AL_MAP_WRITE_BIT_SOFT) && !(albuf->Access&AL_MAP_WRITE_BIT_SOFT))
        SETERR_GOTO(context, AL_INVALID_VALUE, unlock_done,
                    "Mapping buffer for writing without write access");
    if((access&AL_MAP_PERSISTENT_BIT_SOFT) && !(albuf->Access&AL_MAP_PERSISTENT_BIT_SOFT))
        SETERR_GOTO(context, AL_INVALID_VALUE, unlock_done,
                    "Mapping buffer persistently without persistent access");
    if(offset < 0 || offset >= albuf->OriginalSize ||
       length <= 0 || length > albuf->OriginalSize - offset)
        SETERR_GOTO(context, AL_INVALID_VALUE, unlock_done, "Mapping invalid range %d+%d",
                    offset, length);

    retval = (ALbyte*)albuf->data + offset;
    albuf->MappedAccess = access;
    albuf->MappedOffset = offset;
    albuf->MappedSize = length;

unlock_done:
    WriteUnlock(&albuf->lock);

done:
    UnlockBuffersRead(device);
    ALCcontext_DecRef(context);

    return retval;
}

AL_API void AL_APIENTRY alUnmapBufferSOFT(ALuint buffer)
{
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer *albuf;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    LockBuffersRead(device);
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SETERR_GOTO(context, AL_INVALID_NAME, done, "Invalid buffer ID %u", buffer);

    WriteLock(&albuf->lock);
    if(albuf->MappedAccess == 0)
        alSetError(context, AL_INVALID_OPERATION, "Unmapping an unmapped buffer %u", buffer);
    else
    {
        albuf->MappedAccess = 0;
        albuf->MappedOffset = 0;
        albuf->MappedSize = 0;
    }
    WriteUnlock(&albuf->lock);

done:
    UnlockBuffersRead(device);
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alFlushMappedBufferSOFT(ALuint buffer, ALsizei offset, ALsizei length)
{
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer *albuf;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    LockBuffersRead(device);
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SETERR_GOTO(context, AL_INVALID_NAME, done, "Invalid buffer ID %u", buffer);

    WriteLock(&albuf->lock);
    if(albuf->MappedAccess == 0 || !(albuf->MappedAccess&AL_MAP_WRITE_BIT_SOFT))
        alSetError(context, AL_INVALID_OPERATION, "Flushing buffer %u not mapped for writing",
                   buffer);
    else if(offset < albuf->MappedOffset || offset >= albuf->MappedOffset+albuf->MappedSize ||
            length <= 0 || length > albuf->MappedOffset+albuf->MappedSize-offset)
        alSetError(context, AL_INVALID_VALUE, "Flushing an invalid range %d+%d", offset, length);
    else
    {
        /* FIXME: Need to use some method of double-buffering for the mixer and
         * app to hold separate memory, which can be safely transfered
         * asynchronously. Currently we just say the app shouldn't write where
         * OpenAL's reading, and hope for the best...
         */
        ATOMIC_THREAD_FENCE(almemory_order_seq_cst);
    }
    WriteUnlock(&albuf->lock);

done:
    UnlockBuffersRead(device);
    ALCcontext_DecRef(context);
}

AL_API ALvoid AL_APIENTRY alBufferSubDataSOFT(ALuint buffer, ALenum format, const ALvoid *data, ALsizei offset, ALsizei length)
{
    enum UserFmtChannels srcchannels = UserFmtMono;
    enum UserFmtType srctype = UserFmtUByte;
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer *albuf;
    ALsizei byte_align;
    ALsizei frame_size;
    ALsizei num_chans;
    ALsizei align;
    void *dst;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    LockBuffersRead(device);
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SETERR_GOTO(context, AL_INVALID_NAME, done, "Invalid buffer ID %u", buffer);
    if(DecomposeUserFormat(format, &srcchannels, &srctype) == AL_FALSE)
        SETERR_GOTO(context, AL_INVALID_ENUM, done, "Invalid format 0x%04x", format);
    WriteLock(&albuf->lock);
    align = SanitizeAlignment(srctype, ATOMIC_LOAD_SEQ(&albuf->UnpackAlign));
    if(align < 1) SETERR_GOTO(context, AL_INVALID_VALUE, unlock_done, "Invalid unpack alignment");

    if((long)srcchannels != (long)albuf->FmtChannels || srctype != albuf->OriginalType)
        SETERR_GOTO(context, AL_INVALID_ENUM, unlock_done,
                    "Unpacking data with mismatched format");
    if(align != albuf->OriginalAlign)
        SETERR_GOTO(context, AL_INVALID_VALUE, unlock_done,
                    "Unpacking data with mismatched alignment");
    if(albuf->MappedAccess != 0)
        SETERR_GOTO(context, AL_INVALID_OPERATION, unlock_done,
                    "Unpacking data into mapped buffer");

    num_chans = ChannelsFromFmt(albuf->FmtChannels);
    frame_size = num_chans * BytesFromFmt(albuf->FmtType);
    if(albuf->OriginalType == UserFmtIMA4)
        byte_align = ((align-1)/2 + 4) * num_chans;
    else if(albuf->OriginalType == UserFmtMSADPCM)
        byte_align = ((align-2)/2 + 7) * num_chans;
    else
        byte_align = align * frame_size;

    if(offset < 0 || length < 0 || offset > albuf->OriginalSize ||
       length > albuf->OriginalSize-offset)
        SETERR_GOTO(context, AL_INVALID_VALUE, unlock_done, "Invalid data sub-range %d+%d",
                    offset, length);
    if((offset%byte_align) != 0 || (length%byte_align) != 0)
        SETERR_GOTO(context, AL_INVALID_VALUE, unlock_done, "Invalid sub-range alignment");

    /* offset -> byte offset, length -> sample count */
    offset = offset/byte_align * frame_size;
    length = length/byte_align * albuf->OriginalAlign;

    dst = (ALbyte*)albuf->data + offset;
    if(srctype == UserFmtIMA4 && albuf->FmtType == FmtShort)
        Convert_ALshort_ALima4(dst, data, num_chans, length, align);
    else if(srctype == UserFmtMSADPCM && albuf->FmtType == FmtShort)
        Convert_ALshort_ALmsadpcm(dst, data, num_chans, length, align);
    else
    {
        assert((long)srctype == (long)albuf->FmtType);
        memcpy(dst, data, length*frame_size);
    }

unlock_done:
    WriteUnlock(&albuf->lock);

done:
    UnlockBuffersRead(device);
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alBufferSamplesSOFT(ALuint UNUSED(buffer),
  ALuint UNUSED(samplerate), ALenum UNUSED(internalformat), ALsizei UNUSED(samples),
  ALenum UNUSED(channels), ALenum UNUSED(type), const ALvoid *UNUSED(data))
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    alSetError(context, AL_INVALID_OPERATION, "alBufferSamplesSOFT not supported");

    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alBufferSubSamplesSOFT(ALuint UNUSED(buffer),
  ALsizei UNUSED(offset), ALsizei UNUSED(samples),
  ALenum UNUSED(channels), ALenum UNUSED(type), const ALvoid *UNUSED(data))
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    alSetError(context, AL_INVALID_OPERATION, "alBufferSubSamplesSOFT not supported");

    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alGetBufferSamplesSOFT(ALuint UNUSED(buffer),
  ALsizei UNUSED(offset), ALsizei UNUSED(samples),
  ALenum UNUSED(channels), ALenum UNUSED(type), ALvoid *UNUSED(data))
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    alSetError(context, AL_INVALID_OPERATION, "alGetBufferSamplesSOFT not supported");

    ALCcontext_DecRef(context);
}

AL_API ALboolean AL_APIENTRY alIsBufferFormatSupportedSOFT(ALenum UNUSED(format))
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return AL_FALSE;

    alSetError(context, AL_INVALID_OPERATION, "alIsBufferFormatSupportedSOFT not supported");

    ALCcontext_DecRef(context);
    return AL_FALSE;
}


AL_API void AL_APIENTRY alBufferf(ALuint buffer, ALenum param, ALfloat UNUSED(value))
{
    ALCdevice *device;
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    LockBuffersRead(device);
    if(LookupBuffer(device, buffer) == NULL)
        SETERR_GOTO(context, AL_INVALID_NAME, done, "Invalid buffer ID %u", buffer);

    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid buffer float property 0x%04x", param);
    }

done:
    UnlockBuffersRead(device);
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alBuffer3f(ALuint buffer, ALenum param, ALfloat UNUSED(value1), ALfloat UNUSED(value2), ALfloat UNUSED(value3))
{
    ALCdevice *device;
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    LockBuffersRead(device);
    if(LookupBuffer(device, buffer) == NULL)
        SETERR_GOTO(context, AL_INVALID_NAME, done, "Invalid buffer ID %u", buffer);

    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid buffer 3-float property 0x%04x", param);
    }

done:
    UnlockBuffersRead(device);
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alBufferfv(ALuint buffer, ALenum param, const ALfloat *values)
{
    ALCdevice *device;
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    LockBuffersRead(device);
    if(LookupBuffer(device, buffer) == NULL)
        SETERR_GOTO(context, AL_INVALID_NAME, done, "Invalid buffer ID %u", buffer);

    if(!values) SETERR_GOTO(context, AL_INVALID_VALUE, done, "NULL pointer");
    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid buffer float-vector property 0x%04x", param);
    }

done:
    UnlockBuffersRead(device);
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alBufferi(ALuint buffer, ALenum param, ALint value)
{
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer *albuf;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    LockBuffersRead(device);
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SETERR_GOTO(context, AL_INVALID_NAME, done, "Invalid buffer ID %u", buffer);

    switch(param)
    {
    case AL_UNPACK_BLOCK_ALIGNMENT_SOFT:
        if(!(value >= 0))
            SETERR_GOTO(context, AL_INVALID_VALUE, done,
                        "Buffer unpack block alignment %d is invalid", value);
        ATOMIC_STORE_SEQ(&albuf->UnpackAlign, value);
        break;

    case AL_PACK_BLOCK_ALIGNMENT_SOFT:
        if(!(value >= 0))
            SETERR_GOTO(context, AL_INVALID_VALUE, done,
                        "Buffer pack block alignment %d is invalid", value);
        ATOMIC_STORE_SEQ(&albuf->PackAlign, value);
        break;

    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid buffer integer property 0x%04x", param);
    }

done:
    UnlockBuffersRead(device);
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alBuffer3i(ALuint buffer, ALenum param, ALint UNUSED(value1), ALint UNUSED(value2), ALint UNUSED(value3))
{
    ALCdevice *device;
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    if(LookupBuffer(device, buffer) == NULL)
        SETERR_GOTO(context, AL_INVALID_NAME, done, "Invalid buffer ID %u", buffer);

    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid buffer 3-integer property 0x%04x", param);
    }

done:
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alBufferiv(ALuint buffer, ALenum param, const ALint *values)
{
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer *albuf;

    if(values)
    {
        switch(param)
        {
            case AL_UNPACK_BLOCK_ALIGNMENT_SOFT:
            case AL_PACK_BLOCK_ALIGNMENT_SOFT:
                alBufferi(buffer, param, values[0]);
                return;
        }
    }

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    LockBuffersRead(device);
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SETERR_GOTO(context, AL_INVALID_NAME, done, "Invalid buffer ID %u", buffer);

    if(!values) SETERR_GOTO(context, AL_INVALID_VALUE, done, "NULL pointer");
    switch(param)
    {
    case AL_LOOP_POINTS_SOFT:
        WriteLock(&albuf->lock);
        if(ReadRef(&albuf->ref) != 0)
        {
            WriteUnlock(&albuf->lock);
            SETERR_GOTO(context, AL_INVALID_OPERATION, done,
                        "Modifying in-use buffer loop points");
        }
        if(values[0] >= values[1] || values[0] < 0 ||
           values[1] > albuf->SampleLen)
        {
            WriteUnlock(&albuf->lock);
            SETERR_GOTO(context, AL_INVALID_VALUE, done, "Invalid loop point range %d -> %d",
                        values[0], values[1]);
        }

        albuf->LoopStart = values[0];
        albuf->LoopEnd = values[1];
        WriteUnlock(&albuf->lock);
        break;

    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid buffer integer-vector property 0x%04x",
                   param);
    }

done:
    UnlockBuffersRead(device);
    ALCcontext_DecRef(context);
}


AL_API ALvoid AL_APIENTRY alGetBufferf(ALuint buffer, ALenum param, ALfloat *value)
{
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer *albuf;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    LockBuffersRead(device);
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SETERR_GOTO(context, AL_INVALID_NAME, done, "Invalid buffer ID %u", buffer);

    if(!value) SETERR_GOTO(context, AL_INVALID_VALUE, done, "NULL pointer");
    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid buffer float property 0x%04x", param);
    }

done:
    UnlockBuffersRead(device);
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alGetBuffer3f(ALuint buffer, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3)
{
    ALCdevice *device;
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    LockBuffersRead(device);
    if(LookupBuffer(device, buffer) == NULL)
        SETERR_GOTO(context, AL_INVALID_NAME, done, "Invalid buffer ID %u", buffer);

    if(!value1 || !value2 || !value3)
        SETERR_GOTO(context, AL_INVALID_VALUE, done, "NULL pointer");
    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid buffer 3-float property 0x%04x", param);
    }

done:
    UnlockBuffersRead(device);
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alGetBufferfv(ALuint buffer, ALenum param, ALfloat *values)
{
    ALCdevice *device;
    ALCcontext *context;

    switch(param)
    {
    case AL_SEC_LENGTH_SOFT:
        alGetBufferf(buffer, param, values);
        return;
    }

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    LockBuffersRead(device);
    if(LookupBuffer(device, buffer) == NULL)
        SETERR_GOTO(context, AL_INVALID_NAME, done, "Invalid buffer ID %u", buffer);

    if(!values) SETERR_GOTO(context, AL_INVALID_VALUE, done, "NULL pointer");
    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid buffer float-vector property 0x%04x", param);
    }

done:
    UnlockBuffersRead(device);
    ALCcontext_DecRef(context);
}


AL_API ALvoid AL_APIENTRY alGetBufferi(ALuint buffer, ALenum param, ALint *value)
{
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer *albuf;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    LockBuffersRead(device);
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SETERR_GOTO(context, AL_INVALID_NAME, done, "Invalid buffer ID %u", buffer);

    if(!value) SETERR_GOTO(context, AL_INVALID_VALUE, done, "NULL pointer");
    switch(param)
    {
    case AL_FREQUENCY:
        *value = albuf->Frequency;
        break;

    case AL_BITS:
        *value = BytesFromFmt(albuf->FmtType) * 8;
        break;

    case AL_CHANNELS:
        *value = ChannelsFromFmt(albuf->FmtChannels);
        break;

    case AL_SIZE:
        ReadLock(&albuf->lock);
        *value = albuf->SampleLen * FrameSizeFromFmt(albuf->FmtChannels,
                                                     albuf->FmtType);
        ReadUnlock(&albuf->lock);
        break;

    case AL_UNPACK_BLOCK_ALIGNMENT_SOFT:
        *value = ATOMIC_LOAD_SEQ(&albuf->UnpackAlign);
        break;

    case AL_PACK_BLOCK_ALIGNMENT_SOFT:
        *value = ATOMIC_LOAD_SEQ(&albuf->PackAlign);
        break;

    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid buffer integer property 0x%04x", param);
    }

done:
    UnlockBuffersRead(device);
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alGetBuffer3i(ALuint buffer, ALenum param, ALint *value1, ALint *value2, ALint *value3)
{
    ALCdevice *device;
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    LockBuffersRead(device);
    if(LookupBuffer(device, buffer) == NULL)
        SETERR_GOTO(context, AL_INVALID_NAME, done, "Invalid buffer ID %u", buffer);

    if(!value1 || !value2 || !value3)
        SETERR_GOTO(context, AL_INVALID_VALUE, done, "NULL pointer");
    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid buffer 3-integer property 0x%04x", param);
    }

done:
    UnlockBuffersRead(device);
    ALCcontext_DecRef(context);
}


AL_API void AL_APIENTRY alGetBufferiv(ALuint buffer, ALenum param, ALint *values)
{
    ALCdevice *device;
    ALCcontext *context;
    ALbuffer   *albuf;

    switch(param)
    {
    case AL_FREQUENCY:
    case AL_BITS:
    case AL_CHANNELS:
    case AL_SIZE:
    case AL_INTERNAL_FORMAT_SOFT:
    case AL_BYTE_LENGTH_SOFT:
    case AL_SAMPLE_LENGTH_SOFT:
    case AL_UNPACK_BLOCK_ALIGNMENT_SOFT:
    case AL_PACK_BLOCK_ALIGNMENT_SOFT:
        alGetBufferi(buffer, param, values);
        return;
    }

    context = GetContextRef();
    if(!context) return;

    device = context->Device;
    LockBuffersRead(device);
    if((albuf=LookupBuffer(device, buffer)) == NULL)
        SETERR_GOTO(context, AL_INVALID_NAME, done, "Invalid buffer ID %u", buffer);

    if(!values) SETERR_GOTO(context, AL_INVALID_VALUE, done, "NULL pointer");
    switch(param)
    {
    case AL_LOOP_POINTS_SOFT:
        ReadLock(&albuf->lock);
        values[0] = albuf->LoopStart;
        values[1] = albuf->LoopEnd;
        ReadUnlock(&albuf->lock);
        break;

    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid buffer integer-vector property 0x%04x",
                   param);
    }

done:
    UnlockBuffersRead(device);
    ALCcontext_DecRef(context);
}


/*
 * LoadData
 *
 * Loads the specified data into the buffer, using the specified format.
 */
static void LoadData(ALCcontext *context, ALbuffer *ALBuf, ALuint freq, ALsizei frames, enum UserFmtChannels SrcChannels, enum UserFmtType SrcType, const ALvoid *data, ALsizei align, ALbitfieldSOFT access)
{
    enum FmtChannels DstChannels = FmtMono;
    enum FmtType DstType = FmtUByte;
    ALsizei NumChannels, FrameSize;
    ALsizei newsize;

    /* Currently no channels need to be converted. */
    switch(SrcChannels)
    {
        case UserFmtMono: DstChannels = FmtMono; break;
        case UserFmtStereo: DstChannels = FmtStereo; break;
        case UserFmtRear: DstChannels = FmtRear; break;
        case UserFmtQuad: DstChannels = FmtQuad; break;
        case UserFmtX51: DstChannels = FmtX51; break;
        case UserFmtX61: DstChannels = FmtX61; break;
        case UserFmtX71: DstChannels = FmtX71; break;
        case UserFmtBFormat2D: DstChannels = FmtBFormat2D; break;
        case UserFmtBFormat3D: DstChannels = FmtBFormat3D; break;
    }
    if(UNLIKELY((long)SrcChannels != (long)DstChannels))
        SETERR_RETURN(context, AL_INVALID_ENUM,, "Invalid format");

    /* IMA4 and MSADPCM convert to 16-bit short. */
    switch(SrcType)
    {
        case UserFmtUByte: DstType = FmtUByte; break;
        case UserFmtShort: DstType = FmtShort; break;
        case UserFmtFloat: DstType = FmtFloat; break;
        case UserFmtDouble: DstType = FmtDouble; break;
        case UserFmtAlaw: DstType = FmtAlaw; break;
        case UserFmtMulaw: DstType = FmtMulaw; break;
        case UserFmtIMA4: DstType = FmtShort; break;
        case UserFmtMSADPCM: DstType = FmtShort; break;
    }

    if(access != 0)
    {
        if(UNLIKELY((long)SrcType != (long)DstType))
            SETERR_RETURN(context, AL_INVALID_VALUE,, "Format cannot be mapped or preserved");
    }

    NumChannels = ChannelsFromFmt(DstChannels);
    FrameSize = NumChannels * BytesFromFmt(DstType);

    if(UNLIKELY(frames > INT_MAX/FrameSize))
        SETERR_RETURN(context, AL_OUT_OF_MEMORY,,
                      "Buffer size overflow, %d frames x %d bytes per frame", frames, FrameSize);
    newsize = frames*FrameSize;

    WriteLock(&ALBuf->lock);
    if(UNLIKELY(ReadRef(&ALBuf->ref) != 0 || ALBuf->MappedAccess != 0))
    {
        WriteUnlock(&ALBuf->lock);
        SETERR_RETURN(context, AL_INVALID_OPERATION,, "Modifying storage for in-use buffer");
    }

    if((access&AL_PRESERVE_DATA_BIT_SOFT))
    {
        /* Can only preserve data with the same format and alignment. */
        if(UNLIKELY(ALBuf->FmtChannels != DstChannels || ALBuf->OriginalType != SrcType))
        {
            WriteUnlock(&ALBuf->lock);
            SETERR_RETURN(context, AL_INVALID_VALUE,, "Preserving data of mismatched format");
        }
        if(UNLIKELY(ALBuf->OriginalAlign != align))
        {
            WriteUnlock(&ALBuf->lock);
            SETERR_RETURN(context, AL_INVALID_VALUE,, "Preserving data of mismatched alignment");
        }
    }

    /* Round up to the next 16-byte multiple. This could reallocate only when
     * increasing or the new size is less than half the current, but then the
     * buffer's AL_SIZE would not be very reliable for accounting buffer memory
     * usage, and reporting the real size could cause problems for apps that
     * use AL_SIZE to try to get the buffer's play length.
     */
    if(newsize <= INT_MAX-15)
        newsize = (newsize+15) & ~0xf;
    if(newsize != ALBuf->BytesAlloc)
    {
        void *temp = al_malloc(16, (size_t)newsize);
        if(UNLIKELY(!temp && newsize))
        {
            WriteUnlock(&ALBuf->lock);
            SETERR_RETURN(context, AL_OUT_OF_MEMORY,, "Failed to allocate %d bytes of storage",
                          newsize);
        }
        if((access&AL_PRESERVE_DATA_BIT_SOFT))
        {
            ALsizei tocopy = mini(newsize, ALBuf->BytesAlloc);
            if(tocopy > 0) memcpy(temp, ALBuf->data, tocopy);
        }
        al_free(ALBuf->data);
        ALBuf->data = temp;
        ALBuf->BytesAlloc = newsize;
    }

    ALBuf->OriginalType = SrcType;
    if(SrcType == UserFmtIMA4)
    {
        ALsizei byte_align   = ((align-1)/2 + 4) * NumChannels;
        ALBuf->OriginalSize  = frames / align * byte_align;
        ALBuf->OriginalAlign = align;
        assert(DstType == FmtShort);
        if(data != NULL && ALBuf->data != NULL)
            Convert_ALshort_ALima4(ALBuf->data, data, NumChannels, frames, align);
    }
    else if(SrcType == UserFmtMSADPCM)
    {
        ALsizei byte_align   = ((align-2)/2 + 7) * NumChannels;
        ALBuf->OriginalSize  = frames / align * byte_align;
        ALBuf->OriginalAlign = align;
        assert(DstType == FmtShort);
        if(data != NULL && ALBuf->data != NULL)
            Convert_ALshort_ALmsadpcm(ALBuf->data, data, NumChannels, frames, align);
    }
    else
    {
        ALBuf->OriginalSize  = frames * FrameSize;
        ALBuf->OriginalAlign = 1;
        assert((long)SrcType == (long)DstType);
        if(data != NULL && ALBuf->data != NULL)
            memcpy(ALBuf->data, data, frames*FrameSize);
    }

    ALBuf->Frequency = freq;
    ALBuf->FmtChannels = DstChannels;
    ALBuf->FmtType = DstType;
    ALBuf->Access = access;

    ALBuf->SampleLen = frames;
    ALBuf->LoopStart = 0;
    ALBuf->LoopEnd = ALBuf->SampleLen;

    WriteUnlock(&ALBuf->lock);
}


ALsizei BytesFromUserFmt(enum UserFmtType type)
{
    switch(type)
    {
    case UserFmtUByte: return sizeof(ALubyte);
    case UserFmtShort: return sizeof(ALshort);
    case UserFmtFloat: return sizeof(ALfloat);
    case UserFmtDouble: return sizeof(ALdouble);
    case UserFmtMulaw: return sizeof(ALubyte);
    case UserFmtAlaw: return sizeof(ALubyte);
    case UserFmtIMA4: break; /* not handled here */
    case UserFmtMSADPCM: break; /* not handled here */
    }
    return 0;
}
ALsizei ChannelsFromUserFmt(enum UserFmtChannels chans)
{
    switch(chans)
    {
    case UserFmtMono: return 1;
    case UserFmtStereo: return 2;
    case UserFmtRear: return 2;
    case UserFmtQuad: return 4;
    case UserFmtX51: return 6;
    case UserFmtX61: return 7;
    case UserFmtX71: return 8;
    case UserFmtBFormat2D: return 3;
    case UserFmtBFormat3D: return 4;
    }
    return 0;
}
static ALboolean DecomposeUserFormat(ALenum format, enum UserFmtChannels *chans,
                                     enum UserFmtType *type)
{
    static const struct {
        ALenum format;
        enum UserFmtChannels channels;
        enum UserFmtType type;
    } list[] = {
        { AL_FORMAT_MONO8,             UserFmtMono, UserFmtUByte   },
        { AL_FORMAT_MONO16,            UserFmtMono, UserFmtShort   },
        { AL_FORMAT_MONO_FLOAT32,      UserFmtMono, UserFmtFloat   },
        { AL_FORMAT_MONO_DOUBLE_EXT,   UserFmtMono, UserFmtDouble  },
        { AL_FORMAT_MONO_IMA4,         UserFmtMono, UserFmtIMA4    },
        { AL_FORMAT_MONO_MSADPCM_SOFT, UserFmtMono, UserFmtMSADPCM },
        { AL_FORMAT_MONO_MULAW,        UserFmtMono, UserFmtMulaw   },
        { AL_FORMAT_MONO_ALAW_EXT,     UserFmtMono, UserFmtAlaw    },

        { AL_FORMAT_STEREO8,             UserFmtStereo, UserFmtUByte   },
        { AL_FORMAT_STEREO16,            UserFmtStereo, UserFmtShort   },
        { AL_FORMAT_STEREO_FLOAT32,      UserFmtStereo, UserFmtFloat   },
        { AL_FORMAT_STEREO_DOUBLE_EXT,   UserFmtStereo, UserFmtDouble  },
        { AL_FORMAT_STEREO_IMA4,         UserFmtStereo, UserFmtIMA4    },
        { AL_FORMAT_STEREO_MSADPCM_SOFT, UserFmtStereo, UserFmtMSADPCM },
        { AL_FORMAT_STEREO_MULAW,        UserFmtStereo, UserFmtMulaw   },
        { AL_FORMAT_STEREO_ALAW_EXT,     UserFmtStereo, UserFmtAlaw    },

        { AL_FORMAT_REAR8,      UserFmtRear, UserFmtUByte },
        { AL_FORMAT_REAR16,     UserFmtRear, UserFmtShort },
        { AL_FORMAT_REAR32,     UserFmtRear, UserFmtFloat },
        { AL_FORMAT_REAR_MULAW, UserFmtRear, UserFmtMulaw },

        { AL_FORMAT_QUAD8_LOKI,  UserFmtQuad, UserFmtUByte },
        { AL_FORMAT_QUAD16_LOKI, UserFmtQuad, UserFmtShort },

        { AL_FORMAT_QUAD8,      UserFmtQuad, UserFmtUByte },
        { AL_FORMAT_QUAD16,     UserFmtQuad, UserFmtShort },
        { AL_FORMAT_QUAD32,     UserFmtQuad, UserFmtFloat },
        { AL_FORMAT_QUAD_MULAW, UserFmtQuad, UserFmtMulaw },

        { AL_FORMAT_51CHN8,      UserFmtX51, UserFmtUByte },
        { AL_FORMAT_51CHN16,     UserFmtX51, UserFmtShort },
        { AL_FORMAT_51CHN32,     UserFmtX51, UserFmtFloat },
        { AL_FORMAT_51CHN_MULAW, UserFmtX51, UserFmtMulaw },

        { AL_FORMAT_61CHN8,      UserFmtX61, UserFmtUByte },
        { AL_FORMAT_61CHN16,     UserFmtX61, UserFmtShort },
        { AL_FORMAT_61CHN32,     UserFmtX61, UserFmtFloat },
        { AL_FORMAT_61CHN_MULAW, UserFmtX61, UserFmtMulaw },

        { AL_FORMAT_71CHN8,      UserFmtX71, UserFmtUByte },
        { AL_FORMAT_71CHN16,     UserFmtX71, UserFmtShort },
        { AL_FORMAT_71CHN32,     UserFmtX71, UserFmtFloat },
        { AL_FORMAT_71CHN_MULAW, UserFmtX71, UserFmtMulaw },

        { AL_FORMAT_BFORMAT2D_8,       UserFmtBFormat2D, UserFmtUByte },
        { AL_FORMAT_BFORMAT2D_16,      UserFmtBFormat2D, UserFmtShort },
        { AL_FORMAT_BFORMAT2D_FLOAT32, UserFmtBFormat2D, UserFmtFloat },
        { AL_FORMAT_BFORMAT2D_MULAW,   UserFmtBFormat2D, UserFmtMulaw },

        { AL_FORMAT_BFORMAT3D_8,       UserFmtBFormat3D, UserFmtUByte },
        { AL_FORMAT_BFORMAT3D_16,      UserFmtBFormat3D, UserFmtShort },
        { AL_FORMAT_BFORMAT3D_FLOAT32, UserFmtBFormat3D, UserFmtFloat },
        { AL_FORMAT_BFORMAT3D_MULAW,   UserFmtBFormat3D, UserFmtMulaw },
    };
    ALuint i;

    for(i = 0;i < COUNTOF(list);i++)
    {
        if(list[i].format == format)
        {
            *chans = list[i].channels;
            *type  = list[i].type;
            return AL_TRUE;
        }
    }

    return AL_FALSE;
}

ALsizei BytesFromFmt(enum FmtType type)
{
    switch(type)
    {
    case FmtUByte: return sizeof(ALubyte);
    case FmtShort: return sizeof(ALshort);
    case FmtFloat: return sizeof(ALfloat);
    case FmtDouble: return sizeof(ALdouble);
    case FmtMulaw: return sizeof(ALubyte);
    case FmtAlaw: return sizeof(ALubyte);
    }
    return 0;
}
ALsizei ChannelsFromFmt(enum FmtChannels chans)
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
    case FmtBFormat2D: return 3;
    case FmtBFormat3D: return 4;
    }
    return 0;
}

static ALsizei SanitizeAlignment(enum UserFmtType type, ALsizei align)
{
    if(align < 0)
        return 0;

    if(align == 0)
    {
        if(type == UserFmtIMA4)
        {
            /* Here is where things vary:
             * nVidia and Apple use 64+1 sample frames per block -> block_size=36 bytes per channel
             * Most PC sound software uses 2040+1 sample frames per block -> block_size=1024 bytes per channel
             */
            return 65;
        }
        if(type == UserFmtMSADPCM)
            return 64;
        return 1;
    }

    if(type == UserFmtIMA4)
    {
        /* IMA4 block alignment must be a multiple of 8, plus 1. */
        if((align&7) == 1) return align;
        return 0;
    }
    if(type == UserFmtMSADPCM)
    {
        /* MSADPCM block alignment must be a multiple of 2. */
        if((align&1) == 0) return align;
        return 0;
    }

    return align;
}


ALbuffer *NewBuffer(ALCcontext *context)
{
    ALCdevice *device = context->Device;
    ALbuffer *buffer;
    ALenum err;

    buffer = al_calloc(16, sizeof(ALbuffer));
    if(!buffer)
        SETERR_RETURN(context, AL_OUT_OF_MEMORY, NULL, "Failed to allocate buffer object");
    RWLockInit(&buffer->lock);
    buffer->Access = 0;
    buffer->MappedAccess = 0;

    err = NewThunkEntry(&buffer->id);
    if(err == AL_NO_ERROR)
        err = InsertUIntMapEntry(&device->BufferMap, buffer->id, buffer);
    if(err != AL_NO_ERROR)
    {
        FreeThunkEntry(buffer->id);
        memset(buffer, 0, sizeof(ALbuffer));
        al_free(buffer);

        SETERR_RETURN(context, err, NULL, "Failed to set buffer ID");
    }

    return buffer;
}

void DeleteBuffer(ALCdevice *device, ALbuffer *buffer)
{
    RemoveBuffer(device, buffer->id);
    FreeThunkEntry(buffer->id);

    al_free(buffer->data);

    memset(buffer, 0, sizeof(*buffer));
    al_free(buffer);
}


/*
 *    ReleaseALBuffers()
 *
 *    INTERNAL: Called to destroy any buffers that still exist on the device
 */
ALvoid ReleaseALBuffers(ALCdevice *device)
{
    ALsizei i;
    for(i = 0;i < device->BufferMap.size;i++)
    {
        ALbuffer *temp = device->BufferMap.values[i];
        device->BufferMap.values[i] = NULL;

        al_free(temp->data);

        FreeThunkEntry(temp->id);
        memset(temp, 0, sizeof(ALbuffer));
        al_free(temp);
    }
}
