/*
 * SDL_sound Decoder Helpers
 *
 * Copyright (c) 2013 by Chris Robinson <chris.kcat@gmail.com>
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

/* This file contains routines for helping to decode audio using SDL_sound.
 * There's very little OpenAL-specific code here.
 */
#include "sdl_sound.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>

#include <SDL_sound.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "alhelpers.h"


static int done_init = 0;

FilePtr openAudioFile(const char *fname, size_t buftime_ms)
{
    FilePtr file;
    ALuint rate;
    Uint32 bufsize;
    ALenum chans, type;

    /* We need to make sure SDL_sound is initialized. */
    if(!done_init)
    {
        Sound_Init();
        done_init = 1;
    }

    file = Sound_NewSampleFromFile(fname, NULL, 0);
    if(!file)
    {
        fprintf(stderr, "Failed to open %s: %s\n", fname, Sound_GetError());
        return NULL;
    }

    if(getAudioInfo(file, &rate, &chans, &type) != 0)
    {
        Sound_FreeSample(file);
        return NULL;
    }

    bufsize = FramesToBytes((ALsizei)(buftime_ms/1000.0*rate), chans, type);
    if(Sound_SetBufferSize(file, bufsize) == 0)
    {
        fprintf(stderr, "Failed to set buffer size to %u bytes: %s\n", bufsize, Sound_GetError());
        Sound_FreeSample(file);
        return NULL;
    }

    return file;
}

void closeAudioFile(FilePtr file)
{
    if(file)
        Sound_FreeSample(file);
}


int getAudioInfo(FilePtr file, ALuint *rate, ALenum *channels, ALenum *type)
{
    if(file->actual.channels == 1)
        *channels = AL_MONO_SOFT;
    else if(file->actual.channels == 2)
        *channels = AL_STEREO_SOFT;
    else
    {
        fprintf(stderr, "Unsupported channel count: %d\n", file->actual.channels);
        return 1;
    }

    if(file->actual.format == AUDIO_U8)
        *type = AL_UNSIGNED_BYTE_SOFT;
    else if(file->actual.format == AUDIO_S8)
        *type = AL_BYTE_SOFT;
    else if(file->actual.format == AUDIO_U16LSB || file->actual.format == AUDIO_U16MSB)
        *type = AL_UNSIGNED_SHORT_SOFT;
    else if(file->actual.format == AUDIO_S16LSB || file->actual.format == AUDIO_S16MSB)
        *type = AL_SHORT_SOFT;
    else
    {
        fprintf(stderr, "Unsupported sample format: 0x%04x\n", file->actual.format);
        return 1;
    }

    *rate = file->actual.rate;

    return 0;
}


uint8_t *getAudioData(FilePtr file, size_t *length)
{
    *length = Sound_Decode(file);
    if(*length == 0)
        return NULL;
    if((file->actual.format == AUDIO_U16LSB && AUDIO_U16LSB != AUDIO_U16SYS) ||
       (file->actual.format == AUDIO_U16MSB && AUDIO_U16MSB != AUDIO_U16SYS) ||
       (file->actual.format == AUDIO_S16LSB && AUDIO_S16LSB != AUDIO_S16SYS) ||
       (file->actual.format == AUDIO_S16MSB && AUDIO_S16MSB != AUDIO_S16SYS))
    {
        /* Swap bytes if the decoded endianness doesn't match the system. */
        char *buffer = file->buffer;
        size_t i;
        for(i = 0;i < *length;i+=2)
        {
            char b = buffer[i];
            buffer[i] = buffer[i+1];
            buffer[i+1] = b;
        }
    }
    return file->buffer;
}

void *decodeAudioStream(FilePtr file, size_t *length)
{
    Uint32 got;
    char *mem;

    got = Sound_DecodeAll(file);
    if(got == 0)
    {
        *length = 0;
        return NULL;
    }

    mem = malloc(got);
    memcpy(mem, file->buffer, got);

    *length = got;
    return mem;
}
