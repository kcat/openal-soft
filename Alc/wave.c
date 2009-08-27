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
#include <memory.h>
#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"


typedef struct {
    FILE *f;
    long DataStart;

    ALvoid *buffer;
    ALuint size;

    volatile int killNow;
    ALvoid *thread;
} wave_data;


static const ALCchar waveDevice[] = "Wave File Writer";


static ALuint WaveProc(ALvoid *ptr)
{
    ALCdevice *pDevice = (ALCdevice*)ptr;
    wave_data *data = (wave_data*)pDevice->ExtraData;
    ALuint frameSize;
    ALuint now, last;
    size_t WriteCnt;
    size_t fs;
    ALuint avail;
    union {
        short s;
        char b[sizeof(short)];
    } uSB;

    uSB.s = 1;
    frameSize = aluBytesFromFormat(pDevice->Format) *
                aluChannelsFromFormat(pDevice->Format);

    last = timeGetTime();
    while(!data->killNow)
    {
        now = timeGetTime();

        avail = (now-last) * pDevice->Frequency / 1000;
        if(avail < pDevice->UpdateSize)
        {
            Sleep(1);
            continue;
        }

        while(avail > 0)
        {
            SuspendContext(NULL);
            WriteCnt = min(data->size, avail);
            aluMixData(pDevice->Context, data->buffer, WriteCnt * frameSize,
                       pDevice->Format);
            ProcessContext(NULL);

            if(uSB.b[0] != 1 && aluBytesFromFormat(pDevice->Format) > 1)
            {
                ALubyte *bytes = data->buffer;
                ALuint i;

                for(i = 0;i < WriteCnt*frameSize;i++)
                    fputc(bytes[i^1], data->f);
            }
            else
                fs = fwrite(data->buffer, frameSize, WriteCnt, data->f);
            if(ferror(data->f))
            {
                AL_PRINT("Error writing to file\n");
                data->killNow = 1;
                break;
            }

            avail -= WriteCnt;
        }
        last = now;
    }

    return 0;
}

static ALCboolean wave_open_playback(ALCdevice *device, const ALCchar *deviceName)
{
    const char *devName = waveDevice;
    wave_data *data;
    const char *fname;

    fname = GetConfigValue("wave", "file", "");
    if(!fname[0])
        return ALC_FALSE;

    if(deviceName)
    {
        if(strcmp(deviceName, waveDevice) != 0)
            return ALC_FALSE;
        devName = waveDevice;
    }

    data = (wave_data*)calloc(1, sizeof(wave_data));

    data->f = fopen(fname, "wb");
    if(!data->f)
    {
        free(data);
        AL_PRINT("Could not open file '%s': %s\n", fname, strerror(errno));
        return ALC_FALSE;
    }

    device->szDeviceName = strdup(devName);
    device->ExtraData = data;
    return ALC_TRUE;
}

static void wave_close_playback(ALCdevice *device)
{
    wave_data *data = (wave_data*)device->ExtraData;
    ALuint dataLen;
    long size;

    data->killNow = 1;
    StopThread(data->thread);

    size = ftell(data->f);
    if(size > 0)
    {
        dataLen = size - data->DataStart;
        if(fseek(data->f, data->DataStart-4, SEEK_SET) == 0)
        {
            fputc(dataLen&0xff, data->f); // 'data' header len
            fputc((dataLen>>8)&0xff, data->f);
            fputc((dataLen>>16)&0xff, data->f);
            fputc((dataLen>>24)&0xff, data->f);
        }
        if(fseek(data->f, 4, SEEK_SET) == 0)
        {
            size -= 8;
            fputc(size&0xff, data->f); // 'WAVE' header len
            fputc((size>>8)&0xff, data->f);
            fputc((size>>16)&0xff, data->f);
            fputc((size>>24)&0xff, data->f);
        }
    }

    fclose(data->f);
    free(data);
    device->ExtraData = NULL;
}

static ALCboolean wave_start_context(ALCdevice *device, ALCcontext *Context)
{
    wave_data *data = (wave_data*)device->ExtraData;
    ALuint channels, bits, i;
    (void)Context;

    fseek(data->f, 0, SEEK_SET);
    clearerr(data->f);

    bits = aluBytesFromFormat(device->Format) * 8;
    channels = aluChannelsFromFormat(device->Format);
    switch(bits)
    {
        case 8:
        case 16:
        case 32:
            if(channels == 0)
            {
                AL_PRINT("Unknown format?! %x\n", device->Format);
                return ALC_FALSE;
            }
            break;

        default:
            AL_PRINT("Unknown format?! %x\n", device->Format);
            return ALC_FALSE;
    }

    fprintf(data->f, "RIFF");
    fputc(0, data->f); // 'RIFF' header len; filled in at close
    fputc(0, data->f);
    fputc(0, data->f);
    fputc(0, data->f);

    fprintf(data->f, "WAVE");

    fprintf(data->f, "fmt ");
    fputc(16, data->f); // 'fmt ' header len; 16 bytes for PCM
    fputc(0, data->f);
    fputc(0, data->f);
    fputc(0, data->f);
    // 16-bit val, format type id (PCM: 1)
    fputc(1, data->f);
    fputc(0, data->f);
    // 16-bit val, channel count
    fputc(channels&0xff, data->f);
    fputc((channels>>8)&0xff, data->f);
    // 32-bit val, frequency
    fputc(device->Frequency&0xff, data->f);
    fputc((device->Frequency>>8)&0xff, data->f);
    fputc((device->Frequency>>16)&0xff, data->f);
    fputc((device->Frequency>>24)&0xff, data->f);
    // 32-bit val, bytes per second
    i = device->Frequency * channels * bits / 8;
    fputc(i&0xff, data->f);
    fputc((i>>8)&0xff, data->f);
    fputc((i>>16)&0xff, data->f);
    fputc((i>>24)&0xff, data->f);
    // 16-bit val, frame size
    i = channels * bits / 8;
    fputc(i&0xff, data->f);
    fputc((i>>8)&0xff, data->f);
    // 16-bit val, bits per sample
    fputc(bits&0xff, data->f);
    fputc((bits>>8)&0xff, data->f);

    fprintf(data->f, "data");
    fputc(0, data->f); // 'data' header len; filled in at close
    fputc(0, data->f);
    fputc(0, data->f);
    fputc(0, data->f);

    if(ferror(data->f))
    {
        AL_PRINT("Error writing header: %s\n", strerror(errno));
        return ALC_FALSE;
    }

    data->DataStart = ftell(data->f);

    device->UpdateSize = device->BufferSize / 4;

    data->size = device->UpdateSize;
    data->buffer = malloc(data->size * channels * bits / 8);
    if(!data->buffer)
    {
        AL_PRINT("buffer malloc failed\n");
        return ALC_FALSE;
    }

    data->thread = StartThread(WaveProc, device);
    if(data->thread == NULL)
    {
        free(data->buffer);
        data->buffer = NULL;
        return ALC_FALSE;
    }

    return ALC_TRUE;
}

static void wave_stop_context(ALCdevice *device, ALCcontext *Context)
{
    wave_data *data = (wave_data*)device->ExtraData;
    ALuint dataLen;
    long size;
    (void)Context;

    if(!data->thread)
        return;

    data->killNow = 1;
    StopThread(data->thread);
    data->thread = NULL;

    free(data->buffer);
    data->buffer = NULL;

    size = ftell(data->f);
    if(size > 0)
    {
        dataLen = size - data->DataStart;
        if(fseek(data->f, data->DataStart-4, SEEK_SET) == 0)
        {
            fputc(dataLen&0xff, data->f); // 'data' header len
            fputc((dataLen>>8)&0xff, data->f);
            fputc((dataLen>>16)&0xff, data->f);
            fputc((dataLen>>24)&0xff, data->f);
        }
        if(fseek(data->f, 4, SEEK_SET) == 0)
        {
            size -= 8;
            fputc(size&0xff, data->f); // 'WAVE' header len
            fputc((size>>8)&0xff, data->f);
            fputc((size>>16)&0xff, data->f);
            fputc((size>>24)&0xff, data->f);
        }
    }
}


static ALCboolean wave_open_capture(ALCdevice *pDevice, const ALCchar *deviceName)
{
    (void)pDevice;
    (void)deviceName;
    return ALC_FALSE;
}

static void wave_close_capture(ALCdevice *pDevice)
{
    (void)pDevice;
}

static void wave_start_capture(ALCdevice *pDevice)
{
    (void)pDevice;
}

static void wave_stop_capture(ALCdevice *pDevice)
{
    (void)pDevice;
}

static void wave_capture_samples(ALCdevice *pDevice, ALCvoid *pBuffer, ALCuint lSamples)
{
    (void)pDevice;
    (void)pBuffer;
    (void)lSamples;
}

static ALCuint wave_available_samples(ALCdevice *pDevice)
{
    (void)pDevice;
    return 0;
}


BackendFuncs wave_funcs = {
    wave_open_playback,
    wave_close_playback,
    wave_start_context,
    wave_stop_context,
    wave_open_capture,
    wave_close_capture,
    wave_start_capture,
    wave_stop_capture,
    wave_capture_samples,
    wave_available_samples
};

void alc_wave_init(BackendFuncs *func_list)
{
    *func_list = wave_funcs;
}

void alc_wave_deinit(void)
{
}

void alc_wave_probe(int type)
{
    if(type == DEVICE_PROBE)
        AppendDeviceList(waveDevice);
    else if(type == ALL_DEVICE_PROBE)
        AppendAllDeviceList(waveDevice);
}
