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

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"

#include <sys/soundcard.h>

/*
 * The OSS documentation talks about SOUND_MIXER_READ, but the header
 * only contains MIXER_READ. Play safe. Same for WRITE.
 */
#ifndef SOUND_MIXER_READ
#define SOUND_MIXER_READ MIXER_READ
#endif
#ifndef SOUND_MIXER_WRITE
#define SOUND_MIXER_WRITE MIXER_WRITE
#endif

static char *oss_device;

typedef struct {
    int fd;
    int killNow;
    ALvoid *thread;

    ALubyte *mix_data;
    int data_size;
    int silence;
} oss_data;


static int log2i(ALCuint x)
{
    int y = 0;
    while (x > 1)
    {
        x >>= 1;
        y++;
    }
    return y;
}


static ALuint OSSProc(ALvoid *ptr)
{
    ALCdevice *pDevice = (ALCdevice*)ptr;
    oss_data *data = (oss_data*)pDevice->ExtraData;
    int remaining;
    int wrote;

    while(!data->killNow)
    {
        SuspendContext(NULL);
        aluMixData(pDevice->Context,data->mix_data,data->data_size,pDevice->Format);
        ProcessContext(NULL);

        remaining = data->data_size;
        while(remaining > 0)
        {
            wrote = write(data->fd, data->mix_data+data->data_size-remaining, remaining);
            if(wrote < 0)
            {
                AL_PRINT("write failed: %s\n", strerror(errno));
                break;
            }
            if(wrote == 0)
            {
                usleep(1000);
                continue;
            }
            remaining -= wrote;
        }
    }

    return 0;
}

static ALCboolean oss_open_playback(ALCdevice *device, const ALCchar *deviceName)
{
    int numFragmentsLogSize;
    int log2FragmentSize;
    unsigned int periods;
    audio_buf_info info;
    char driver[64];
    int numChannels;
    oss_data *data;
    int ossFormat;
    int ossSpeed;
    char *err;
    int i;

    strncpy(driver, GetConfigValue("oss", "device", "/dev/dsp"), sizeof(driver)-1);
    driver[sizeof(driver)-1] = 0;
    if(deviceName)
    {
        if(strcmp(deviceName, oss_device))
            return ALC_FALSE;
    }

    if(deviceName)
        strcpy(device->szDeviceName, deviceName);
    else
        strcpy(device->szDeviceName, oss_device);

    data = (oss_data*)calloc(1, sizeof(oss_data));
    data->killNow = 0;

    data->fd = open(driver, O_WRONLY);
    if(data->fd == -1)
    {
        free(data);
        AL_PRINT("Could not open %s: %s\n", driver, strerror(errno));
        return ALC_FALSE;
    }

    switch(device->Format)
    {
        case AL_FORMAT_MONO8:
        case AL_FORMAT_STEREO8:
        case AL_FORMAT_QUAD8:
            data->silence = 0x80;
            ossFormat = AFMT_U8;
            break;
        case AL_FORMAT_MONO16:
        case AL_FORMAT_STEREO16:
        case AL_FORMAT_QUAD16:
            data->silence = 0;
            ossFormat = AFMT_S16_NE;
            break;
        default:
            ossFormat = -1;
            AL_PRINT("Unknown format?! %x\n", device->Format);
    }

    periods = GetConfigValueInt("oss", "periods", 4);
    if((int)periods < 0)
        periods = 4;
    numChannels = device->Channels;
    ossSpeed = device->Frequency;
    log2FragmentSize = log2i(device->UpdateFreq * device->FrameSize / periods);

    /* according to the OSS spec, 16 bytes are the minimum */
    if (log2FragmentSize < 4)
        log2FragmentSize = 4;
    numFragmentsLogSize = (periods << 16) | log2FragmentSize;

#define ok(func, str) (i=(func),((i<0)?(err=(str)),0:1))
    if (!(ok(ioctl(data->fd, SNDCTL_DSP_SETFRAGMENT, &numFragmentsLogSize), "set fragment") &&
          ok(ioctl(data->fd, SNDCTL_DSP_SETFMT, &ossFormat), "set format") &&
          ok(ioctl(data->fd, SNDCTL_DSP_CHANNELS, &numChannels), "set channels") &&
          ok(ioctl(data->fd, SNDCTL_DSP_SPEED, &ossSpeed), "set speed") &&
          ok(ioctl(data->fd, SNDCTL_DSP_GETOSPACE, &info), "get space")))
    {
        AL_PRINT("%s failed: %s\n", err, strerror(i));
        close(data->fd);
        free(data);
        return ALC_FALSE;
    }
#undef ok

    device->Channels = numChannels;
    device->Frequency = ossSpeed;
    device->Format = 0;
    if(ossFormat == AFMT_U8)
    {
        if(device->Channels == 1)
        {
            device->Format = AL_FORMAT_MONO8;
            device->FrameSize = 1;
        }
        else if(device->Channels == 2)
        {
            device->Format = AL_FORMAT_STEREO8;
            device->FrameSize = 2;
        }
        else if(device->Channels == 4)
        {
            device->Format = AL_FORMAT_QUAD8;
            device->FrameSize = 4;
        }
    }
    else if(ossFormat == AFMT_S16_NE)
    {
        if(device->Channels == 1)
        {
            device->Format = AL_FORMAT_MONO16;
            device->FrameSize = 2;
        }
        else if(device->Channels == 2)
        {
            device->Format = AL_FORMAT_STEREO16;
            device->FrameSize = 4;
        }
        else if(device->Channels == 4)
        {
            device->Format = AL_FORMAT_QUAD16;
            device->FrameSize = 8;
        }
    }

    if(!device->Format)
    {
        AL_PRINT("returned unknown format: %#x %d!\n", ossFormat, numChannels);
        close(data->fd);
        free(data);
        return ALC_FALSE;
    }

    device->UpdateFreq = info.fragsize / device->FrameSize;

    data->data_size = device->UpdateFreq * device->FrameSize;
    data->mix_data = calloc(1, data->data_size);

    device->MaxNoOfSources = 256;

    device->ExtraData = data;
    data->thread = StartThread(OSSProc, device);
    if(data->thread == NULL)
    {
        device->ExtraData = NULL;
        free(data->mix_data);
        free(data);
        return ALC_FALSE;
    }

    return ALC_TRUE;
}

static void oss_close_playback(ALCdevice *device)
{
    oss_data *data = (oss_data*)device->ExtraData;
    data->killNow = 1;
    StopThread(data->thread);

    close(data->fd);

    free(data->mix_data);
    free(data);
    device->ExtraData = NULL;
}


static ALCboolean oss_open_capture(ALCdevice *pDevice, const ALCchar *deviceName, ALCuint frequency, ALCenum format, ALCsizei SampleSize)
{
    (void)pDevice;
    (void)deviceName;
    (void)frequency;
    (void)format;
    (void)SampleSize;
    return ALC_FALSE;
}

static void oss_close_capture(ALCdevice *pDevice)
{
    (void)pDevice;
}

static void oss_start_capture(ALCdevice *pDevice)
{
    (void)pDevice;
}

static void oss_stop_capture(ALCdevice *pDevice)
{
    (void)pDevice;
}

static void oss_capture_samples(ALCdevice *pDevice, ALCvoid *pBuffer, ALCuint lSamples)
{
    (void)pDevice;
    (void)pBuffer;
    (void)lSamples;
}

static ALCuint oss_available_samples(ALCdevice *pDevice)
{
    (void)pDevice;
    return 0;
}


BackendFuncs oss_funcs = {
    oss_open_playback,
    oss_close_playback,
    oss_open_capture,
    oss_close_capture,
    oss_start_capture,
    oss_stop_capture,
    oss_capture_samples,
    oss_available_samples
};

void alc_oss_init(BackendFuncs *func_list)
{
    *func_list = oss_funcs;

    oss_device = AppendDeviceList("OSS Software");
    AppendAllDeviceList(oss_device);
}
