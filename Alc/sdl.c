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

#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"

#include <string.h>
#include <stdlib.h>
#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

#include <SDL/SDL.h>

static void *sdl_handle;
#define MAKE_FUNC(x) static typeof(x) * p##x
MAKE_FUNC(SDL_PauseAudio);
MAKE_FUNC(SDL_CloseAudio);
MAKE_FUNC(SDL_OpenAudio);
MAKE_FUNC(SDL_InitSubSystem);
MAKE_FUNC(SDL_GetError);
MAKE_FUNC(SDL_LockAudio);
MAKE_FUNC(SDL_UnlockAudio);
#undef MAKE_FUNC

static char *sdl_device;
/* SDL audio can only be initialized once per process */
static int initialized;

typedef struct {
    SDL_AudioSpec audioSpec;
    volatile int killNow;
    ALvoid *thread;

    ALubyte *mix_data;
    int data_size;
    int data_read;
    int data_write;
} sdl_data;


static void SDLCALL fillAudio(void *userdata, Uint8 *stream, int len)
{
    sdl_data *data = (sdl_data*)userdata;
    int rem = data->data_size-data->data_read;
    if(len >= rem)
    {
        memcpy(stream, data->mix_data + data->data_read, rem);
        stream += rem;
        len -= rem;
        data->data_read = 0;
    }
    if(len > 0)
    {
        memcpy(stream, data->mix_data + data->data_read, len);
        data->data_read += len;
    }
}

static ALuint SDLProc(ALvoid *ptr)
{
    ALCdevice *pDevice = (ALCdevice*)ptr;
    sdl_data *data = (sdl_data*)pDevice->ExtraData;
    int len, rem;

    pSDL_PauseAudio(0);
    while(!data->killNow)
    {
        pSDL_LockAudio();

        len = (data->data_read-data->data_write+data->data_size)%data->data_size;
        if(len == 0)
        {
            pSDL_UnlockAudio();

            Sleep(1);
            continue;
        }

        rem = data->data_size - data->data_write;

        SuspendContext(NULL);
        if(len > rem)
        {
            aluMixData(pDevice->Context, data->mix_data+data->data_write, rem, pDevice->Format);
            aluMixData(pDevice->Context, data->mix_data, len-rem, pDevice->Format);
        }
        else
            aluMixData(pDevice->Context, data->mix_data+data->data_write, len, pDevice->Format);
        ProcessContext(NULL);

        data->data_write = data->data_read;

        pSDL_UnlockAudio();
    }
    pSDL_PauseAudio(1);

    return 0;
}

static ALCboolean sdl_open_playback(ALCdevice *device, const ALCchar *deviceName)
{
    SDL_AudioSpec sdlSpec;
    ALuint frameSize;
    sdl_data *data;

    if(initialized || !sdl_device)
        return ALC_FALSE;

    if(deviceName)
    {
        if(strcmp(deviceName, sdl_device))
            return ALC_FALSE;
        device->szDeviceName = sdl_device;
    }
    else
        device->szDeviceName = sdl_device;

    data = (sdl_data*)calloc(1, sizeof(sdl_data));
    data->killNow = 0;

    frameSize = aluBytesFromFormat(device->Format) *
                aluChannelsFromFormat(device->Format);

    sdlSpec.freq = device->Frequency;
    sdlSpec.channels = aluChannelsFromFormat(device->Format);
    switch(aluBytesFromFormat(device->Format))
    {
        case 1:
            sdlSpec.format = AUDIO_U8;
            break;
        case 2:
            sdlSpec.format = AUDIO_S16SYS;
            break;
        default:
            AL_PRINT("Unknown format?! %x\n", device->Format);
            free(data);
            return ALC_FALSE;
    }
    sdlSpec.samples = 1;
    while(sdlSpec.samples < device->UpdateSize)
        sdlSpec.samples <<= 1;
    sdlSpec.samples >>= 1;

    sdlSpec.callback = fillAudio;
    sdlSpec.userdata = data;

    if(pSDL_OpenAudio(&sdlSpec, &data->audioSpec) < 0)
    {
        AL_PRINT("Audio init failed: %s\n", pSDL_GetError());
        free(data);
        return ALC_FALSE;
    }

    if(!((data->audioSpec.format == AUDIO_U8 && aluBytesFromFormat(device->Format) == 1) ||
         (data->audioSpec.format == AUDIO_S16SYS && aluBytesFromFormat(device->Format) == 2)))
    {
        AL_PRINT("Could not set %d-bit, got format %#x instead\n", aluBytesFromFormat(device->Format), data->audioSpec.format);
        pSDL_CloseAudio();
        free(data);
        return ALC_FALSE;
    }
    if(aluChannelsFromFormat(device->Format) != data->audioSpec.channels)
    {
        AL_PRINT("Could not set %d channels, got %d instead\n", aluChannelsFromFormat(device->Format), data->audioSpec.channels);
        pSDL_CloseAudio();
        free(data);
        return ALC_FALSE;
    }

    device->Frequency = data->audioSpec.freq;
    device->UpdateSize = data->audioSpec.size / frameSize;

    data->data_size = device->UpdateSize * frameSize * 2;
    data->mix_data = malloc(data->data_size);
    if(data->mix_data == NULL)
    {
        AL_PRINT("Could not allocate %d bytes\n", data->data_size);
        pSDL_CloseAudio();
        free(data);
        return ALC_FALSE;
    }
    memset(data->mix_data, data->audioSpec.silence, data->data_size);

    device->ExtraData = data;
    data->thread = StartThread(SDLProc, device);
    if(data->thread == NULL)
    {
        pSDL_CloseAudio();
        device->ExtraData = NULL;
        free(data->mix_data);
        free(data);
        return ALC_FALSE;
    }

    initialized = 1;

    return ALC_TRUE;
}

static void sdl_close_playback(ALCdevice *device)
{
    sdl_data *data = (sdl_data*)device->ExtraData;
    data->killNow = 1;
    StopThread(data->thread);

    pSDL_CloseAudio();
    initialized = 0;

    free(data->mix_data);
    free(data);
    device->ExtraData = NULL;
}


static ALCboolean sdl_open_capture(ALCdevice *device, const ALCchar *deviceName, ALCuint frequency, ALCenum format, ALCsizei SampleSize)
{
    return ALC_FALSE;
    (void)device;
    (void)deviceName;
    (void)frequency;
    (void)format;
    (void)SampleSize;
}


BackendFuncs sdl_funcs = {
    sdl_open_playback,
    sdl_close_playback,
    sdl_open_capture,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

void alc_sdl_init(BackendFuncs *func_list)
{
    const char *str;

    *func_list = sdl_funcs;

#ifdef HAVE_DLFCN_H
#if defined(__APPLE__) && defined(__MACH__)
# define SDLLIB "SDL.framework/SDL"
#else
# define SDLLIB "libSDL.so"
#endif
    sdl_handle = dlopen(SDLLIB, RTLD_NOW);
    if(!sdl_handle)
        return;
    dlerror();

#define LOAD_FUNC(f) do { \
    p##f = (typeof(f)*)dlsym(sdl_handle, #f); \
    if((str=dlerror()) != NULL) \
    { \
        dlclose(sdl_handle); \
        sdl_handle = NULL; \
        AL_PRINT("Could not load %s from "SDLLIB": %s\n", #f, str); \
        return; \
    } \
} while(0)
#else
    str = NULL;
    sdl_handle = 0xDEADBEEF;
#define LOAD_FUNC(f) p##f = f
#endif

    LOAD_FUNC(SDL_PauseAudio);
    LOAD_FUNC(SDL_CloseAudio);
    LOAD_FUNC(SDL_OpenAudio);
    LOAD_FUNC(SDL_InitSubSystem);
    LOAD_FUNC(SDL_GetError);
    LOAD_FUNC(SDL_LockAudio);
    LOAD_FUNC(SDL_UnlockAudio);

#undef LOAD_FUNC

    if(pSDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
        return;

    sdl_device = AppendDeviceList("SDL Software");
    AppendAllDeviceList(sdl_device);
}
