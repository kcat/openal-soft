/*
 * OpenAL HRTF Example
 *
 * Copyright (c) 2015 by Chris Robinson <chris.kcat@gmail.com>
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

/* This file contains an example for selecting an HRTF. */

#include <stdio.h>
#include <assert.h>
#include <math.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "common/alhelpers.h"
#include "common/sdl_sound.h"


#ifndef M_PI
#define M_PI                         (3.14159265358979323846)
#endif

static LPALCGETSTRINGISOFT alcGetStringiSOFT;
static LPALCRESETDEVICESOFT alcResetDeviceSOFT;

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

    format = GetFormat(channels, type, NULL);
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
    alBufferData(buffer, format, data, datalen, rate);
    free(data);
    closeAudioFile(sound);

    /* Check if an error occured, and clean up if so. */
    err = alGetError();
    if(err != AL_NO_ERROR)
    {
        fprintf(stderr, "OpenAL Error: %s\n", alGetString(err));
        if(buffer && alIsBuffer(buffer))
            alDeleteBuffers(1, &buffer);
        return 0;
    }

    return buffer;
}


int main(int argc, char **argv)
{
    ALCdevice *device;
    ALuint source, buffer;
    const char *soundname;
    const char *hrtfname;
    ALCint hrtf_state;
    ALCint num_hrtf;
    ALdouble angle;
    ALenum state;

    /* Print out usage if no file was specified */
    if(argc < 2 || (strcmp(argv[1], "-hrtf") == 0 && argc < 4))
    {
        fprintf(stderr, "Usage: %s [-hrtf <name>] <soundfile>\n", argv[0]);
        return 1;
    }

    /* Initialize OpenAL with the default device, and check for HRTF support. */
    if(InitAL() != 0)
        return 1;

    if(strcmp(argv[1], "-hrtf") == 0)
    {
        hrtfname = argv[2];
        soundname = argv[3];
    }
    else
    {
        hrtfname = NULL;
        soundname = argv[1];
    }

    device = alcGetContextsDevice(alcGetCurrentContext());
    if(!alcIsExtensionPresent(device, "ALC_SOFT_HRTF"))
    {
        fprintf(stderr, "Error: ALC_SOFT_HRTF not supported\n");
        CloseAL();
        return 1;
    }

    /* Define a macro to help load the function pointers. */
#define LOAD_PROC(d, x)  ((x) = alcGetProcAddress((d), #x))
    LOAD_PROC(device, alcGetStringiSOFT);
    LOAD_PROC(device, alcResetDeviceSOFT);
#undef LOAD_PROC

    /* Enumerate available HRTFs, and reset the device using one. */
    alcGetIntegerv(device, ALC_NUM_HRTF_SPECIFIERS_SOFT, 1, &num_hrtf);
    if(!num_hrtf)
        printf("No HRTFs found\n");
    else
    {
        ALCint attr[5];
        ALCint index = -1;
        ALCint i;

        printf("Available HRTFs:\n");
        for(i = 0;i < num_hrtf;i++)
        {
            const ALCchar *name = alcGetStringiSOFT(device, ALC_HRTF_SPECIFIER_SOFT, i);
            printf("    %d: %s\n", i, name);

            /* Check if this is the HRTF the user requested. */
            if(hrtfname && strcmp(name, hrtfname) == 0)
                index = i;
        }

        if(index == -1)
        {
            if(hrtfname)
                printf("HRTF \"%s\" not found\n", hrtfname);
            index = 0;
        }
        printf("Selecting HRTF %d...\n", index);

        attr[0] = ALC_HRTF_SOFT;
        attr[1] = ALC_TRUE;
        attr[2] = ALC_HRTF_ID_SOFT;
        attr[3] = index;
        attr[4] = 0;

        if(!alcResetDeviceSOFT(device, attr))
            printf("Failed to reset device: %s\n", alcGetString(device, alcGetError(device)));
    }

    /* Check if HRTF is enabled, and show which is being used. */
    alcGetIntegerv(device, ALC_HRTF_SOFT, 1, &hrtf_state);
    if(!hrtf_state)
        printf("HRTF not enabled!\n");
    else
    {
        const ALchar *name = alcGetString(device, ALC_HRTF_SPECIFIER_SOFT);
        printf("HRTF enabled, using %s\n", name);
    }
    fflush(stdout);

    /* Load the sound into a buffer. */
    buffer = LoadSound(soundname);
    if(!buffer)
    {
        CloseAL();
        return 1;
    }

    /* Create the source to play the sound with. */
    source = 0;
    alGenSources(1, &source);
    alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(source, AL_POSITION, 0.0f, 0.0f, -1.0f);
    alSourcei(source, AL_BUFFER, buffer);
    assert(alGetError()==AL_NO_ERROR && "Failed to setup sound source");

    /* Play the sound until it finishes. */
    angle = 0.0;
    alSourcePlay(source);
    do {
        al_nssleep(10000000);

        /* Rotate the source around the listener by about 1/4 cycle per second.
         * Only affects mono sounds.
         */
        angle += 0.01 * M_PI * 0.5;
        alSource3f(source, AL_POSITION, (ALfloat)sin(angle), 0.0f, -(ALfloat)cos(angle));

        alGetSourcei(source, AL_SOURCE_STATE, &state);
    } while(alGetError() == AL_NO_ERROR && state == AL_PLAYING);

    /* All done. Delete resources, and close OpenAL. */
    alDeleteSources(1, &source);
    alDeleteBuffers(1, &buffer);

    CloseAL();

    return 0;
}
