/*
 * OpenAL Source Latency Example
 *
 * Copyright (c) 2012 by Chris Robinson <chris.kcat@gmail.com>
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

/* This file contains an example for checking the latency of a sound. */

#include <stdio.h>
#include <assert.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "common/alhelpers.h"
#include "common/sdl_sound.h"


static LPALBUFFERSAMPLESSOFT alBufferSamplesSOFT = wrap_BufferSamples;
static LPALISBUFFERFORMATSUPPORTEDSOFT alIsBufferFormatSupportedSOFT;

static LPALSOURCEDSOFT alSourcedSOFT;
static LPALSOURCE3DSOFT alSource3dSOFT;
static LPALSOURCEDVSOFT alSourcedvSOFT;
static LPALGETSOURCEDSOFT alGetSourcedSOFT;
static LPALGETSOURCE3DSOFT alGetSource3dSOFT;
static LPALGETSOURCEDVSOFT alGetSourcedvSOFT;
static LPALSOURCEI64SOFT alSourcei64SOFT;
static LPALSOURCE3I64SOFT alSource3i64SOFT;
static LPALSOURCEI64VSOFT alSourcei64vSOFT;
static LPALGETSOURCEI64SOFT alGetSourcei64SOFT;
static LPALGETSOURCE3I64SOFT alGetSource3i64SOFT;
static LPALGETSOURCEI64VSOFT alGetSourcei64vSOFT;

/* LoadBuffer loads the named audio file into an OpenAL buffer object, and
 * returns the new buffer ID. */
static ALuint LoadSound(const char *filename)
{
    ALenum err, format, type, channels;
    ALuint rate, buffer;
    size_t datalen;
    void *data;
    FilePtr sound;

    /* Open the audio file */
    sound = openAudioFile(filename, 1000);
    if(!sound)
    {
        fprintf(stderr, "Could not open audio in %s\n", filename);
        closeAudioFile(sound);
        return 0;
    }

    /* Get the sound format, and figure out the OpenAL format */
    if(getAudioInfo(sound, &rate, &channels, &type) != 0)
    {
        fprintf(stderr, "Error getting audio info for %s\n", filename);
        closeAudioFile(sound);
        return 0;
    }

    format = GetFormat(channels, type, alIsBufferFormatSupportedSOFT);
    if(format == AL_NONE)
    {
        fprintf(stderr, "Unsupported format (%s, %s) for %s\n",
                ChannelsName(channels), TypeName(type), filename);
        closeAudioFile(sound);
        return 0;
    }

    /* Decode the whole audio stream to a buffer. */
    data = decodeAudioStream(sound, &datalen);
    if(!data)
    {
        fprintf(stderr, "Failed to read audio from %s\n", filename);
        closeAudioFile(sound);
        return 0;
    }

    /* Buffer the audio data into a new buffer object, then free the data and
     * close the file. */
    buffer = 0;
    alGenBuffers(1, &buffer);
    alBufferSamplesSOFT(buffer, rate, format, BytesToFrames(datalen, channels, type),
                        channels, type, data);
    free(data);
    closeAudioFile(sound);

    /* Check if an error occured, and clean up if so. */
    err = alGetError();
    if(err != AL_NO_ERROR)
    {
        fprintf(stderr, "OpenAL Error: %s\n", alGetString(err));
        if(alIsBuffer(buffer))
            alDeleteBuffers(1, &buffer);
        return 0;
    }

    return buffer;
}


int main(int argc, char **argv)
{
    ALuint source, buffer;
    ALdouble offsets[2];
    ALenum state;

    /* Print out usage if no file was specified */
    if(argc < 2)
    {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    /* Initialize OpenAL with the default device, and check for EFX support. */
    if(InitAL() != 0)
        return 1;

    if(!alIsExtensionPresent("AL_SOFT_source_latency"))
    {
        fprintf(stderr, "Error: AL_SOFT_source_latency not supported\n");
        CloseAL();
        return 1;
    }

    /* Define a macro to help load the function pointers. */
#define LOAD_PROC(x)  ((x) = alGetProcAddress(#x))
    LOAD_PROC(alSourcedSOFT);
    LOAD_PROC(alSource3dSOFT);
    LOAD_PROC(alSourcedvSOFT);
    LOAD_PROC(alGetSourcedSOFT);
    LOAD_PROC(alGetSource3dSOFT);
    LOAD_PROC(alGetSourcedvSOFT);
    LOAD_PROC(alSourcei64SOFT);
    LOAD_PROC(alSource3i64SOFT);
    LOAD_PROC(alSourcei64vSOFT);
    LOAD_PROC(alGetSourcei64SOFT);
    LOAD_PROC(alGetSource3i64SOFT);
    LOAD_PROC(alGetSourcei64vSOFT);

    if(alIsExtensionPresent("AL_SOFT_buffer_samples"))
    {
        LOAD_PROC(alBufferSamplesSOFT);
        LOAD_PROC(alIsBufferFormatSupportedSOFT);
    }
#undef LOAD_PROC

    /* Load the sound into a buffer. */
    buffer = LoadSound(argv[1]);
    if(!buffer)
    {
        CloseAL();
        return 1;
    }

    /* Create the source to play the sound with. */
    source = 0;
    alGenSources(1, &source);
    alSourcei(source, AL_BUFFER, buffer);
    assert(alGetError()==AL_NO_ERROR && "Failed to setup sound source");

    /* Play the sound until it finishes. */
    alSourcePlay(source);
    do {
        al_nssleep(10000000);
        alGetSourcei(source, AL_SOURCE_STATE, &state);

        /* Get the source offset and latency. AL_SEC_OFFSET_LATENCY_SOFT will
         * place the offset (in seconds) in offsets[0], and the time until that
         * offset will be heard (in seconds) in offsets[1]. */
        alGetSourcedvSOFT(source, AL_SEC_OFFSET_LATENCY_SOFT, offsets);
        printf("\rOffset: %f - Latency:%3u ms  ", offsets[0], (ALuint)(offsets[1]*1000));
        fflush(stdout);
    } while(alGetError() == AL_NO_ERROR && state == AL_PLAYING);
    printf("\n");

    /* All done. Delete resources, and close OpenAL. */
    alDeleteSources(1, &source);
    alDeleteBuffers(1, &buffer);

    CloseAL();

    return 0;
}
