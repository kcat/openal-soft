/*
 * OpenAL Helpers
 *
 * Copyright (c) 2011 by Chris Robinson <chris.kcat@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* This file contains routines to help with some menial OpenAL-related tasks,
 * such as opening a device and setting up a context, closing the device and
 * destroying its context, converting between frame counts and byte lengths,
 * finding an appropriate buffer format, and getting readable strings for
 * channel configs and sample types. */

#include <stdio.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "alhelpers.h"


const char *ChannelsName(ALenum chans)
{
    switch(chans)
    {
    case AL_MONO: return "Mono";
    case AL_STEREO: return "Stereo";
    case AL_REAR: return "Rear";
    case AL_QUAD: return "Quadraphonic";
    case AL_5POINT1: return "5.1 Surround";
    case AL_6POINT1: return "6.1 Surround";
    case AL_7POINT1: return "7.1 Surround";
    }
    return "Unknown Channels";
}

const char *TypeName(ALenum type)
{
    switch(type)
    {
    case AL_BYTE: return "S8";
    case AL_UNSIGNED_BYTE: return "U8";
    case AL_SHORT: return "S16";
    case AL_UNSIGNED_SHORT: return "U16";
    case AL_INT: return "S32";
    case AL_UNSIGNED_INT: return "U32";
    case AL_FLOAT: return "Float32";
    case AL_DOUBLE: return "Float64";
    }
    return "Unknown Type";
}


ALsizei FramesToBytes(ALsizei size, ALenum channels, ALenum type)
{
    switch(channels)
    {
    case AL_MONO:    size *= 1; break;
    case AL_STEREO:  size *= 2; break;
    case AL_REAR:    size *= 2; break;
    case AL_QUAD:    size *= 4; break;
    case AL_5POINT1: size *= 6; break;
    case AL_6POINT1: size *= 7; break;
    case AL_7POINT1: size *= 8; break;
    }

    switch(type)
    {
    case AL_BYTE:           size *= sizeof(ALbyte); break;        
    case AL_UNSIGNED_BYTE:  size *= sizeof(ALubyte); break;
    case AL_SHORT:          size *= sizeof(ALshort); break;
    case AL_UNSIGNED_SHORT: size *= sizeof(ALushort); break;
    case AL_INT:            size *= sizeof(ALint); break;
    case AL_UNSIGNED_INT:   size *= sizeof(ALuint); break;
    case AL_FLOAT:          size *= sizeof(ALfloat); break;
    case AL_DOUBLE:         size *= sizeof(ALdouble); break;
    }

    return size;
}

ALsizei BytesToFrames(ALsizei size, ALenum channels, ALenum type)
{
    return size / FramesToBytes(1, channels, type);
}


ALenum GetFormat(ALenum channels, ALenum type)
{
    ALenum format = 0;

    /* We use the AL_EXT_MCFORMATS extension to provide output of Quad, 5.1,
     * and 7.1 channel configs, AL_EXT_FLOAT32 for 32-bit float samples, and
     * AL_EXT_DOUBLE for 64-bit float samples. */
    if(type == AL_UNSIGNED_BYTE)
    {
        if(channels == AL_MONO)
            format = AL_FORMAT_MONO8;
        else if(channels == AL_STEREO)
            format = AL_FORMAT_STEREO8;
        else if(alIsExtensionPresent("AL_EXT_MCFORMATS"))
        {
            if(channels == AL_QUAD)
                format = alGetEnumValue("AL_FORMAT_QUAD8");
            else if(channels == AL_5POINT1)
                format = alGetEnumValue("AL_FORMAT_51CHN8");
            else if(channels == AL_6POINT1)
                format = alGetEnumValue("AL_FORMAT_61CHN8");
            else if(channels == AL_7POINT1)
                format = alGetEnumValue("AL_FORMAT_71CHN8");
        }
    }
    else if(type == AL_SHORT)
    {
        if(channels == AL_MONO)
            format = AL_FORMAT_MONO16;
        else if(channels == AL_STEREO)
            format = AL_FORMAT_STEREO16;
        else if(alIsExtensionPresent("AL_EXT_MCFORMATS"))
        {
            if(channels == AL_QUAD)
                format = alGetEnumValue("AL_FORMAT_QUAD16");
            else if(channels == AL_5POINT1)
                format = alGetEnumValue("AL_FORMAT_51CHN16");
            else if(channels == AL_6POINT1)
                format = alGetEnumValue("AL_FORMAT_61CHN16");
            else if(channels == AL_7POINT1)
                format = alGetEnumValue("AL_FORMAT_71CHN16");
        }
    }
    else if(type == AL_FLOAT && alIsExtensionPresent("AL_EXT_FLOAT32"))
    {
        if(channels == AL_MONO)
            format = alGetEnumValue("AL_FORMAT_MONO_FLOAT32");
        else if(channels == AL_STEREO)
            format = alGetEnumValue("AL_FORMAT_STEREO_FLOAT32");
        else if(alIsExtensionPresent("AL_EXT_MCFORMATS"))
        {
            if(channels == AL_QUAD)
                format = alGetEnumValue("AL_FORMAT_QUAD32");
            else if(channels == AL_5POINT1)
                format = alGetEnumValue("AL_FORMAT_51CHN32");
            else if(channels == AL_6POINT1)
                format = alGetEnumValue("AL_FORMAT_61CHN32");
            else if(channels == AL_7POINT1)
                format = alGetEnumValue("AL_FORMAT_71CHN32");
        }
    }
    else if(type == AL_DOUBLE && alIsExtensionPresent("AL_EXT_DOUBLE"))
    {
        if(channels == AL_MONO)
            format = alGetEnumValue("AL_FORMAT_MONO_DOUBLE");
        else if(channels == AL_STEREO)
            format = alGetEnumValue("AL_FORMAT_STEREO_DOUBLE");
    }

    /* NOTE: It seems OSX returns -1 from alGetEnumValue for unknown enums, as
     * opposed to 0. Correct it. */
    if(format == -1)
        format = 0;

    return format;
}


int InitAL(void)
{
    ALCdevice *device;
    ALCcontext *ctx;

    /* Open and initialize a device with default settings */
    device = alcOpenDevice(NULL);
    if(!device)
    {
        fprintf(stderr, "Could not open a device!\n");
        return 1;
    }

    ctx = alcCreateContext(device, NULL);
    if(ctx == NULL || alcMakeContextCurrent(ctx) == ALC_FALSE)
    {
        if(ctx != NULL)
            alcDestroyContext(ctx);
        alcCloseDevice(device);
        fprintf(stderr, "Could not set a context!\n");
        return 1;
    }

    return 0;
}

void CloseAL(void)
{
    ALCdevice *device;
    ALCcontext *ctx;

    /* Close the device belonging to the current context, and destroy the
     * context. */
    ctx = alcGetCurrentContext();
    if(ctx == NULL)
        return;

    device = alcGetContextsDevice(ctx);

    alcMakeContextCurrent(NULL);
    alcDestroyContext(ctx);
    alcCloseDevice(device);
}
