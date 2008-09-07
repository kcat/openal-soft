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

#include <sys/audioio.h>


static char *solaris_device;

typedef struct {
    int fd;
    volatile int killNow;
    ALvoid *thread;

    ALubyte *mix_data;
    int data_size;
} solaris_data;


static ALuint SolarisProc(ALvoid *ptr)
{
    ALCdevice *pDevice = (ALCdevice*)ptr;
    solaris_data *data = (solaris_data*)pDevice->ExtraData;
    int remaining = 0;
    int wrote;

    while(!data->killNow)
    {
        int len = data->data_size - remaining;

        if(len > 0)
        {
            SuspendContext(NULL);
            aluMixData(pDevice->Context, data->mix_data+remaining, len, pDevice->Format);
            ProcessContext(NULL);
        }

        remaining += len;
        wrote = write(data->fd, data->mix_data, remaining);
        if(wrote < 0)
        {
            AL_PRINT("write failed: %s\n", strerror(errno));
            remaining = 0;
        }
        else if(wrote > 0)
        {
            remaining -= wrote;
            if(remaining > 0)
                memmove(data->mix_data, data->mix_data+wrote, remaining);
        }
        else
            Sleep(1);
    }

    return 0;
}


static ALCboolean solaris_open_playback(ALCdevice *device, const ALCchar *deviceName)
{
    audio_info_t info;
    ALuint frameSize;
    char driver[64];
    int numChannels;
    solaris_data *data;

    strncpy(driver, GetConfigValue("solaris", "device", "/dev/audio"), sizeof(driver)-1);
    driver[sizeof(driver)-1] = 0;
    if(deviceName)
    {
        if(strcmp(deviceName, solaris_device))
            return ALC_FALSE;
        device->szDeviceName = solaris_device;
    }
    else
        device->szDeviceName = solaris_device;

    data = (solaris_data*)calloc(1, sizeof(solaris_data));
    data->killNow = 0;

    data->fd = open(driver, O_WRONLY);
    if(data->fd == -1)
    {
        free(data);
        AL_PRINT("Could not open %s: %s\n", driver, strerror(errno));
        return ALC_FALSE;
    }

    numChannels = aluChannelsFromFormat(device->Format);
    AUDIO_INITINFO(&info);
    info.play.sample_rate = device->Frequency;
    info.play.channels = numChannels;

    switch(aluBytesFromFormat(device->Format))
    {
        case 1:
            info.play.precision = 8;
            info.play.encoding = AUDIO_ENCODING_LINEAR8;
            break;
        case 2:
            info.play.precision = 16;
            info.play.encoding = AUDIO_ENCODING_LINEAR;
            break;
        default:
            AL_PRINT("Unknown format?! %x\n", device->Format);
    }

    frameSize = numChannels * aluBytesFromFormat(device->Format);
    info.play.buffer_size = device->UpdateSize * frameSize;

    if(ioctl(data->fd, AUDIO_SETINFO, &info) < 0)
    {
        AL_PRINT("ioctl failed: %s\n", strerror(errno));
        close(data->fd);
        free(data);
        return ALC_FALSE;
    }

    device->Frequency = info.play.sample_rate;

    if(aluChannelsFromFormat(device->Format) != info.play.channels)
    {
        AL_PRINT("Could not set %d channels, got %d instead\n", aluChannelsFromFormat(device->Format), info.play.channels);
        close(data->fd);
        free(data);
        return ALC_FALSE;
    }

    if(!((info.play.precision == 8 && aluBytesFromFormat(device->Format) == 1) ||
         (info.play.precision == 16 && aluBytesFromFormat(device->Format) == 2)))
    {
        AL_PRINT("Could not set %d-bit output, got %d\n", aluBytesFromFormat(device->Format)*8, info.play.precision);
        close(data->fd);
        free(data);
        return ALC_FALSE;
    }

    device->UpdateSize = info.play.buffer_size / 4;

    data->data_size = device->UpdateSize * frameSize;
    data->mix_data = calloc(1, data->data_size);

    device->ExtraData = data;
    data->thread = StartThread(SolarisProc, device);
    if(data->thread == NULL)
    {
        device->ExtraData = NULL;
        free(data->mix_data);
        free(data);
        return ALC_FALSE;
    }

    return ALC_TRUE;
}

static void solaris_close_playback(ALCdevice *device)
{
    solaris_data *data = (solaris_data*)device->ExtraData;
    data->killNow = 1;
    StopThread(data->thread);

    close(data->fd);

    free(data->mix_data);
    free(data);
    device->ExtraData = NULL;
}


static ALCboolean solaris_open_capture(ALCdevice *device, const ALCchar *deviceName, ALCuint frequency, ALCenum format, ALCsizei SampleSize)
{
    (void)device;
    (void)deviceName;
    (void)frequency;
    (void)format;
    (void)SampleSize;
    return ALC_FALSE;
}

static void solaris_close_capture(ALCdevice *device)
{
    (void)device;
}

static void solaris_start_capture(ALCdevice *pDevice)
{
    (void)pDevice;
}

static void solaris_stop_capture(ALCdevice *pDevice)
{
    (void)pDevice;
}

static void solaris_capture_samples(ALCdevice *pDevice, ALCvoid *pBuffer, ALCuint lSamples)
{
    (void)pDevice;
    (void)pBuffer;
    (void)lSamples;
}

static ALCuint solaris_available_samples(ALCdevice *pDevice)
{
    (void)pDevice;
    return 0;
}


BackendFuncs solaris_funcs = {
    solaris_open_playback,
    solaris_close_playback,
    solaris_open_capture,
    solaris_close_capture,
    solaris_start_capture,
    solaris_stop_capture,
    solaris_capture_samples,
    solaris_available_samples
};

void alc_solaris_init(BackendFuncs *func_list)
{
    *func_list = solaris_funcs;

    solaris_device = AppendDeviceList("Solaris Software");
    AppendAllDeviceList(solaris_device);
}
