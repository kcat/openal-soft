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
#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif
#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"

#include <alsa/asoundlib.h>


typedef struct {
    snd_pcm_t *pcmHandle;

    ALvoid *buffer;
    ALsizei size;

    ALboolean doCapture;
    RingBuffer *ring;

    volatile int killNow;
    ALvoid *thread;
} alsa_data;

typedef struct {
    ALCchar *name;
    int card, dev;
} DevMap;

static void *alsa_handle;
#define MAKE_FUNC(f) static typeof(f) * p##f
MAKE_FUNC(snd_strerror);
MAKE_FUNC(snd_pcm_open);
MAKE_FUNC(snd_pcm_close);
MAKE_FUNC(snd_pcm_nonblock);
MAKE_FUNC(snd_pcm_frames_to_bytes);
MAKE_FUNC(snd_pcm_bytes_to_frames);
MAKE_FUNC(snd_pcm_hw_params_malloc);
MAKE_FUNC(snd_pcm_hw_params_free);
MAKE_FUNC(snd_pcm_hw_params_any);
MAKE_FUNC(snd_pcm_hw_params_set_access);
MAKE_FUNC(snd_pcm_hw_params_set_format);
MAKE_FUNC(snd_pcm_hw_params_set_channels);
MAKE_FUNC(snd_pcm_hw_params_set_periods_near);
MAKE_FUNC(snd_pcm_hw_params_set_rate_near);
MAKE_FUNC(snd_pcm_hw_params_set_rate);
MAKE_FUNC(snd_pcm_hw_params_set_rate_resample);
MAKE_FUNC(snd_pcm_hw_params_set_buffer_time_near);
MAKE_FUNC(snd_pcm_hw_params_set_period_time_near);
MAKE_FUNC(snd_pcm_hw_params_set_buffer_size_near);
MAKE_FUNC(snd_pcm_hw_params_set_period_size_near);
MAKE_FUNC(snd_pcm_hw_params_set_buffer_size_min);
MAKE_FUNC(snd_pcm_hw_params_get_buffer_size);
MAKE_FUNC(snd_pcm_hw_params_get_period_size);
MAKE_FUNC(snd_pcm_hw_params_get_access);
MAKE_FUNC(snd_pcm_hw_params_get_periods);
MAKE_FUNC(snd_pcm_hw_params);
MAKE_FUNC(snd_pcm_sw_params_malloc);
MAKE_FUNC(snd_pcm_sw_params_current);
MAKE_FUNC(snd_pcm_sw_params_set_avail_min);
MAKE_FUNC(snd_pcm_sw_params);
MAKE_FUNC(snd_pcm_sw_params_free);
MAKE_FUNC(snd_pcm_prepare);
MAKE_FUNC(snd_pcm_start);
MAKE_FUNC(snd_pcm_resume);
MAKE_FUNC(snd_pcm_wait);
MAKE_FUNC(snd_pcm_state);
MAKE_FUNC(snd_pcm_avail_update);
MAKE_FUNC(snd_pcm_areas_silence);
MAKE_FUNC(snd_pcm_mmap_begin);
MAKE_FUNC(snd_pcm_mmap_commit);
MAKE_FUNC(snd_pcm_readi);
MAKE_FUNC(snd_pcm_writei);
MAKE_FUNC(snd_pcm_drain);
MAKE_FUNC(snd_pcm_recover);
MAKE_FUNC(snd_pcm_info_malloc);
MAKE_FUNC(snd_pcm_info_free);
MAKE_FUNC(snd_pcm_info_set_device);
MAKE_FUNC(snd_pcm_info_set_subdevice);
MAKE_FUNC(snd_pcm_info_set_stream);
MAKE_FUNC(snd_pcm_info_get_name);
MAKE_FUNC(snd_ctl_pcm_next_device);
MAKE_FUNC(snd_ctl_pcm_info);
MAKE_FUNC(snd_ctl_open);
MAKE_FUNC(snd_ctl_close);
MAKE_FUNC(snd_ctl_card_info_malloc);
MAKE_FUNC(snd_ctl_card_info_free);
MAKE_FUNC(snd_ctl_card_info);
MAKE_FUNC(snd_ctl_card_info_get_name);
MAKE_FUNC(snd_card_next);
#undef MAKE_FUNC


static const ALCchar alsaDevice[] = "ALSA Default";
static DevMap *allDevNameMap;
static ALuint numDevNames;
static DevMap *allCaptureDevNameMap;
static ALuint numCaptureDevNames;


void *alsa_load(void)
{
    if(!alsa_handle)
    {
        char *str;

#ifdef HAVE_DLFCN_H
        alsa_handle = dlopen("libasound.so.2", RTLD_NOW);
        if(!alsa_handle)
            return NULL;
        dlerror();

#define LOAD_FUNC(f) do { \
    p##f = dlsym(alsa_handle, #f); \
    if((str=dlerror()) != NULL) \
    { \
        dlclose(alsa_handle); \
        alsa_handle = NULL; \
        AL_PRINT("Could not load %s from libasound.so.2: %s\n", #f, str); \
        return NULL; \
    } \
} while(0)
#else
        str = NULL;
        alsa_handle = (void*)0xDEADBEEF;
#define LOAD_FUNC(f) p##f = f
#endif

LOAD_FUNC(snd_strerror);
LOAD_FUNC(snd_pcm_open);
LOAD_FUNC(snd_pcm_close);
LOAD_FUNC(snd_pcm_nonblock);
LOAD_FUNC(snd_pcm_frames_to_bytes);
LOAD_FUNC(snd_pcm_bytes_to_frames);
LOAD_FUNC(snd_pcm_hw_params_malloc);
LOAD_FUNC(snd_pcm_hw_params_free);
LOAD_FUNC(snd_pcm_hw_params_any);
LOAD_FUNC(snd_pcm_hw_params_set_access);
LOAD_FUNC(snd_pcm_hw_params_set_format);
LOAD_FUNC(snd_pcm_hw_params_set_channels);
LOAD_FUNC(snd_pcm_hw_params_set_periods_near);
LOAD_FUNC(snd_pcm_hw_params_set_rate_near);
LOAD_FUNC(snd_pcm_hw_params_set_rate);
LOAD_FUNC(snd_pcm_hw_params_set_rate_resample);
LOAD_FUNC(snd_pcm_hw_params_set_buffer_time_near);
LOAD_FUNC(snd_pcm_hw_params_set_period_time_near);
LOAD_FUNC(snd_pcm_hw_params_set_buffer_size_near);
LOAD_FUNC(snd_pcm_hw_params_set_buffer_size_min);
LOAD_FUNC(snd_pcm_hw_params_set_period_size_near);
LOAD_FUNC(snd_pcm_hw_params_get_buffer_size);
LOAD_FUNC(snd_pcm_hw_params_get_period_size);
LOAD_FUNC(snd_pcm_hw_params_get_access);
LOAD_FUNC(snd_pcm_hw_params_get_periods);
LOAD_FUNC(snd_pcm_hw_params);
LOAD_FUNC(snd_pcm_sw_params_malloc);
LOAD_FUNC(snd_pcm_sw_params_current);
LOAD_FUNC(snd_pcm_sw_params_set_avail_min);
LOAD_FUNC(snd_pcm_sw_params);
LOAD_FUNC(snd_pcm_sw_params_free);
LOAD_FUNC(snd_pcm_prepare);
LOAD_FUNC(snd_pcm_start);
LOAD_FUNC(snd_pcm_resume);
LOAD_FUNC(snd_pcm_wait);
LOAD_FUNC(snd_pcm_state);
LOAD_FUNC(snd_pcm_avail_update);
LOAD_FUNC(snd_pcm_areas_silence);
LOAD_FUNC(snd_pcm_mmap_begin);
LOAD_FUNC(snd_pcm_mmap_commit);
LOAD_FUNC(snd_pcm_readi);
LOAD_FUNC(snd_pcm_writei);
LOAD_FUNC(snd_pcm_drain);
LOAD_FUNC(snd_pcm_recover);

LOAD_FUNC(snd_pcm_info_malloc);
LOAD_FUNC(snd_pcm_info_free);
LOAD_FUNC(snd_pcm_info_set_device);
LOAD_FUNC(snd_pcm_info_set_subdevice);
LOAD_FUNC(snd_pcm_info_set_stream);
LOAD_FUNC(snd_pcm_info_get_name);
LOAD_FUNC(snd_ctl_pcm_next_device);
LOAD_FUNC(snd_ctl_pcm_info);
LOAD_FUNC(snd_ctl_open);
LOAD_FUNC(snd_ctl_close);
LOAD_FUNC(snd_ctl_card_info_malloc);
LOAD_FUNC(snd_ctl_card_info_free);
LOAD_FUNC(snd_ctl_card_info);
LOAD_FUNC(snd_ctl_card_info_get_name);
LOAD_FUNC(snd_card_next);

#undef LOAD_FUNC
    }
    return alsa_handle;
}

static DevMap *probe_devices(snd_pcm_stream_t stream, ALuint *count)
{
    snd_ctl_t *handle;
    int card, err, dev, idx;
    snd_ctl_card_info_t *info;
    snd_pcm_info_t *pcminfo;
    DevMap *DevList;
    char name[1024];

    psnd_ctl_card_info_malloc(&info);
    psnd_pcm_info_malloc(&pcminfo);

    card = -1;
    if((err=psnd_card_next(&card)) < 0)
        AL_PRINT("Failed to find a card: %s\n", psnd_strerror(err));

    DevList = malloc(sizeof(DevMap) * 1);
    DevList[0].name = strdup("ALSA Default");
    idx = 1;
    while(card >= 0)
    {
        sprintf(name, "hw:%d", card);
        if((err = psnd_ctl_open(&handle, name, 0)) < 0)
        {
            AL_PRINT("control open (%i): %s\n", card, psnd_strerror(err));
            goto next_card;
        }
        if((err = psnd_ctl_card_info(handle, info)) < 0)
        {
            AL_PRINT("control hardware info (%i): %s\n", card, psnd_strerror(err));
            psnd_ctl_close(handle);
            goto next_card;
        }

        dev = -1;
        while(1)
        {
            const char *cname, *dname;
            void *temp;

            if(psnd_ctl_pcm_next_device(handle, &dev) < 0)
                AL_PRINT("snd_ctl_pcm_next_device failed\n");
            if(dev < 0)
                break;

            psnd_pcm_info_set_device(pcminfo, dev);
            psnd_pcm_info_set_subdevice(pcminfo, 0);
            psnd_pcm_info_set_stream(pcminfo, stream);
            if((err = psnd_ctl_pcm_info(handle, pcminfo)) < 0) {
                if(err != -ENOENT)
                    AL_PRINT("control digital audio info (%i): %s\n", card, psnd_strerror(err));
                continue;
            }

            temp = realloc(DevList, sizeof(DevMap) * (idx+1));
            if(temp)
            {
                DevList = temp;
                cname = psnd_ctl_card_info_get_name(info);
                dname = psnd_pcm_info_get_name(pcminfo);
                snprintf(name, sizeof(name), "%s [%s] (hw:%d,%d) via ALSA",
                         cname, dname, card, dev);
                DevList[idx].name = strdup(name);
                DevList[idx].card = card;
                DevList[idx].dev = dev;
                idx++;
            }
        }
        psnd_ctl_close(handle);
    next_card:
        if(psnd_card_next(&card) < 0) {
            AL_PRINT("snd_card_next failed\n");
            break;
        }
    }

    psnd_pcm_info_free(pcminfo);
    psnd_ctl_card_info_free(info);

    *count = idx;
    return DevList;
}


static int xrun_recovery(snd_pcm_t *handle, int err)
{
    err = psnd_pcm_recover(handle, err, 1);
    if(err < 0)
        AL_PRINT("recover failed: %s\n", psnd_strerror(err));
    return err;
}

static int verify_state(snd_pcm_t *handle)
{
    snd_pcm_state_t state = psnd_pcm_state(handle);
    if(state == SND_PCM_STATE_DISCONNECTED)
        return -ENODEV;
    if(state == SND_PCM_STATE_XRUN)
    {
        int err = xrun_recovery(handle, -EPIPE);
        if(err < 0) return err;
    }
    else if(state == SND_PCM_STATE_SUSPENDED)
    {
        int err = xrun_recovery(handle, -ESTRPIPE);
        if(err < 0) return err;
    }

    return state;
}


static ALuint ALSAProc(ALvoid *ptr)
{
    ALCdevice *pDevice = (ALCdevice*)ptr;
    alsa_data *data = (alsa_data*)pDevice->ExtraData;
    const snd_pcm_channel_area_t *areas = NULL;
    snd_pcm_sframes_t avail, commitres;
    snd_pcm_uframes_t offset, frames;
    char *WritePtr;
    int err;

    SetRTPriority();

    while(!data->killNow)
    {
        int state = verify_state(data->pcmHandle);
        if(state < 0)
        {
            AL_PRINT("Invalid state detected: %s\n", psnd_strerror(state));
            aluHandleDisconnect(pDevice);
            break;
        }

        avail = psnd_pcm_avail_update(data->pcmHandle);
        if(avail < 0)
        {
            AL_PRINT("available update failed: %s\n", psnd_strerror(avail));
            continue;
        }

        // make sure there's frames to process
        if((snd_pcm_uframes_t)avail < pDevice->UpdateSize)
        {
            if(state != SND_PCM_STATE_RUNNING)
            {
                err = psnd_pcm_start(data->pcmHandle);
                if(err < 0)
                {
                    AL_PRINT("start failed: %s\n", psnd_strerror(err));
                    continue;
                }
            }
            if(psnd_pcm_wait(data->pcmHandle, 1000) == 0)
                AL_PRINT("Wait timeout... buffer size too low?\n");
            continue;
        }
        avail -= avail%pDevice->UpdateSize;

        // it is possible that contiguous areas are smaller, thus we use a loop
        while(avail > 0)
        {
            frames = avail;

            err = psnd_pcm_mmap_begin(data->pcmHandle, &areas, &offset, &frames);
            if(err < 0)
            {
                AL_PRINT("mmap begin error: %s\n", psnd_strerror(err));
                break;
            }

            WritePtr = (char*)areas->addr + (offset * areas->step / 8);
            aluMixData(pDevice, WritePtr, frames);

            commitres = psnd_pcm_mmap_commit(data->pcmHandle, offset, frames);
            if(commitres < 0 || (commitres-frames) != 0)
            {
                AL_PRINT("mmap commit error: %s\n",
                         psnd_strerror(commitres >= 0 ? -EPIPE : commitres));
                break;
            }

            avail -= frames;
        }
    }

    return 0;
}

static ALuint ALSANoMMapProc(ALvoid *ptr)
{
    ALCdevice *pDevice = (ALCdevice*)ptr;
    alsa_data *data = (alsa_data*)pDevice->ExtraData;
    snd_pcm_sframes_t avail;
    char *WritePtr;

    SetRTPriority();

    while(!data->killNow)
    {
        int state = verify_state(data->pcmHandle);
        if(state < 0)
        {
            AL_PRINT("Invalid state detected: %s\n", psnd_strerror(state));
            aluHandleDisconnect(pDevice);
            break;
        }

        WritePtr = data->buffer;
        avail = data->size / psnd_pcm_frames_to_bytes(data->pcmHandle, 1);
        aluMixData(pDevice, WritePtr, avail);

        while(avail > 0)
        {
            int ret = psnd_pcm_writei(data->pcmHandle, WritePtr, avail);
            switch (ret)
            {
            case -EAGAIN:
                continue;
            case -ESTRPIPE:
            case -EPIPE:
            case -EINTR:
                ret = psnd_pcm_recover(data->pcmHandle, ret, 1);
                if(ret < 0)
                    avail = 0;
                break;
            default:
                if (ret >= 0)
                {
                    WritePtr += psnd_pcm_frames_to_bytes(data->pcmHandle, ret);
                    avail -= ret;
                }
                break;
            }
            if (ret < 0)
            {
                ret = psnd_pcm_prepare(data->pcmHandle);
                if(ret < 0)
                    break;
            }
        }
    }

    return 0;
}

static ALCboolean alsa_open_playback(ALCdevice *device, const ALCchar *deviceName)
{
    alsa_data *data;
    char driver[64];
    int i;

    if(!alsa_load())
        return ALC_FALSE;

    strncpy(driver, GetConfigValue("alsa", "device", "default"), sizeof(driver)-1);
    driver[sizeof(driver)-1] = 0;

    if(!deviceName)
        deviceName = alsaDevice;
    else if(strcmp(deviceName, alsaDevice) != 0)
    {
        size_t idx;

        if(!allDevNameMap)
            allDevNameMap = probe_devices(SND_PCM_STREAM_PLAYBACK, &numDevNames);

        for(idx = 0;idx < numDevNames;idx++)
        {
            if(allDevNameMap[idx].name &&
               strcmp(deviceName, allDevNameMap[idx].name) == 0)
            {
                if(idx > 0)
                    sprintf(driver, "hw:%d,%d", allDevNameMap[idx].card, allDevNameMap[idx].dev);
                break;
            }
        }
        if(idx == numDevNames)
            return ALC_FALSE;
    }

    data = (alsa_data*)calloc(1, sizeof(alsa_data));

    i = psnd_pcm_open(&data->pcmHandle, driver, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if(i >= 0)
    {
        i = psnd_pcm_nonblock(data->pcmHandle, 0);
        if(i < 0)
            psnd_pcm_close(data->pcmHandle);
    }
    if(i < 0)
    {
        free(data);
        AL_PRINT("Could not open playback device '%s': %s\n", driver, psnd_strerror(i));
        return ALC_FALSE;
    }

    device->szDeviceName = strdup(deviceName);
    device->ExtraData = data;
    return ALC_TRUE;
}

static void alsa_close_playback(ALCdevice *device)
{
    alsa_data *data = (alsa_data*)device->ExtraData;

    psnd_pcm_close(data->pcmHandle);
    free(data);
    device->ExtraData = NULL;
}

static ALCboolean alsa_reset_playback(ALCdevice *device)
{
    alsa_data *data = (alsa_data*)device->ExtraData;
    snd_pcm_uframes_t periodSizeInFrames;
    unsigned int periodLen, bufferLen;
    snd_pcm_sw_params_t *sp = NULL;
    snd_pcm_hw_params_t *p = NULL;
    snd_pcm_access_t access;
    snd_pcm_format_t format;
    unsigned int periods;
    unsigned int rate;
    int allowmmap;
    char *err;
    int i;


    format = -1;
    switch(device->FmtType)
    {
        case DevFmtByte:
            format = SND_PCM_FORMAT_S8;
            break;
        case DevFmtUByte:
            format = SND_PCM_FORMAT_U8;
            break;
        case DevFmtShort:
            format = SND_PCM_FORMAT_S16;
            break;
        case DevFmtUShort:
            format = SND_PCM_FORMAT_U16;
            break;
        case DevFmtFloat:
            format = SND_PCM_FORMAT_FLOAT;
            break;
    }

    allowmmap = GetConfigValueBool("alsa", "mmap", 1);
    periods = device->NumUpdates;
    periodLen = (ALuint64)device->UpdateSize * 1000000 / device->Frequency;
    bufferLen = periodLen * periods;
    rate = device->Frequency;

    err = NULL;
    psnd_pcm_hw_params_malloc(&p);

    if((i=psnd_pcm_hw_params_any(data->pcmHandle, p)) < 0)
        err = "any";
    /* set interleaved access */
    if(i >= 0 && (!allowmmap || (i=psnd_pcm_hw_params_set_access(data->pcmHandle, p, SND_PCM_ACCESS_MMAP_INTERLEAVED)) < 0))
    {
        if(periods > 2)
        {
            periods--;
            bufferLen = periodLen * periods;
        }
        if((i=psnd_pcm_hw_params_set_access(data->pcmHandle, p, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
            err = "set access";
    }
    /* set format (implicitly sets sample bits) */
    if(i >= 0 && (i=psnd_pcm_hw_params_set_format(data->pcmHandle, p, format)) < 0)
    {
        device->FmtType = DevFmtFloat;
        if(format == SND_PCM_FORMAT_FLOAT ||
           (i=psnd_pcm_hw_params_set_format(data->pcmHandle, p, SND_PCM_FORMAT_FLOAT)) < 0)
        {
            device->FmtType = DevFmtShort;
            if(format == SND_PCM_FORMAT_S16 ||
               (i=psnd_pcm_hw_params_set_format(data->pcmHandle, p, SND_PCM_FORMAT_S16)) < 0)
            {
                device->FmtType = DevFmtUByte;
                if(format == SND_PCM_FORMAT_U8 ||
                   (i=psnd_pcm_hw_params_set_format(data->pcmHandle, p, SND_PCM_FORMAT_U8)) < 0)
                    err = "set format";
            }
        }
    }
    /* set channels (implicitly sets frame bits) */
    if(i >= 0 && (i=psnd_pcm_hw_params_set_channels(data->pcmHandle, p, ChannelsFromDevFmt(device->FmtChans))) < 0)
    {
        if((i=psnd_pcm_hw_params_set_channels(data->pcmHandle, p, 2)) < 0)
        {
            if((i=psnd_pcm_hw_params_set_channels(data->pcmHandle, p, 1)) < 0)
                err = "set channels";
            else
            {
                if((device->Flags&DEVICE_CHANNELS_REQUEST))
                    AL_PRINT("Failed to set %s, got Mono instead\n", DevFmtChannelsString(device->FmtChans));
                device->FmtChans = DevFmtMono;
            }
        }
        else
        {
            if((device->Flags&DEVICE_CHANNELS_REQUEST))
                AL_PRINT("Failed to set %s, got Stereo instead\n", DevFmtChannelsString(device->FmtChans));
            device->FmtChans = DevFmtStereo;
        }
        device->Flags &= ~DEVICE_CHANNELS_REQUEST;
    }
    if(i >= 0 && (i=psnd_pcm_hw_params_set_rate_resample(data->pcmHandle, p, 0)) < 0)
    {
        AL_PRINT("Failed to disable ALSA resampler\n");
        i = 0;
    }
    /* set rate (implicitly constrains period/buffer parameters) */
    if(i >= 0 && (i=psnd_pcm_hw_params_set_rate_near(data->pcmHandle, p, &rate, NULL)) < 0)
        err = "set rate near";
    /* set buffer time (implicitly constrains period/buffer parameters) */
    if(i >= 0 && (i=psnd_pcm_hw_params_set_buffer_time_near(data->pcmHandle, p, &bufferLen, NULL)) < 0)
        err = "set buffer time near";
    /* set period time in frame units (implicitly sets buffer size/bytes/time and period size/bytes) */
    if(i >= 0 && (i=psnd_pcm_hw_params_set_period_time_near(data->pcmHandle, p, &periodLen, NULL)) < 0)
        err = "set period time near";
    /* install and prepare hardware configuration */
    if(i >= 0 && (i=psnd_pcm_hw_params(data->pcmHandle, p)) < 0)
        err = "set params";
    if(i >= 0 && (i=psnd_pcm_hw_params_get_access(p, &access)) < 0)
        err = "get access";
    if(i >= 0 && (i=psnd_pcm_hw_params_get_period_size(p, &periodSizeInFrames, NULL)) < 0)
        err = "get period size";
    if(i >= 0 && (i=psnd_pcm_hw_params_get_periods(p, &periods, NULL)) < 0)
        err = "get periods";
    if(i < 0)
    {
        AL_PRINT("%s failed: %s\n", err, psnd_strerror(i));
        psnd_pcm_hw_params_free(p);
        return ALC_FALSE;
    }

    psnd_pcm_hw_params_free(p);

    err = NULL;
    psnd_pcm_sw_params_malloc(&sp);

    if((i=psnd_pcm_sw_params_current(data->pcmHandle, sp)) != 0)
        err = "sw current";
    if(i == 0 && (i=psnd_pcm_sw_params_set_avail_min(data->pcmHandle, sp, periodSizeInFrames)) != 0)
        err = "sw set avail min";
    if(i == 0 && (i=psnd_pcm_sw_params(data->pcmHandle, sp)) != 0)
        err = "sw set params";
    if(i != 0)
    {
        AL_PRINT("%s failed: %s\n", err, psnd_strerror(i));
        psnd_pcm_sw_params_free(sp);
        return ALC_FALSE;
    }

    psnd_pcm_sw_params_free(sp);

    if(device->Frequency != rate)
    {
        if((device->Flags&DEVICE_FREQUENCY_REQUEST))
            AL_PRINT("Failed to set %dhz, got %dhz instead\n", device->Frequency, rate);
        device->Flags &= ~DEVICE_FREQUENCY_REQUEST;
        device->Frequency = rate;
    }

    SetDefaultChannelOrder(device);

    data->size = psnd_pcm_frames_to_bytes(data->pcmHandle, periodSizeInFrames);
    if(access == SND_PCM_ACCESS_RW_INTERLEAVED)
    {
        /* Increase periods by one, since the temp buffer counts as an extra
         * period */
        periods++;
        data->buffer = malloc(data->size);
        if(!data->buffer)
        {
            AL_PRINT("buffer malloc failed\n");
            return ALC_FALSE;
        }
        device->UpdateSize = periodSizeInFrames;
        device->NumUpdates = periods;
        data->thread = StartThread(ALSANoMMapProc, device);
    }
    else
    {
        i = psnd_pcm_prepare(data->pcmHandle);
        if(i < 0)
        {
            AL_PRINT("prepare error: %s\n", psnd_strerror(i));
            return ALC_FALSE;
        }
        device->UpdateSize = periodSizeInFrames;
        device->NumUpdates = periods;
        data->thread = StartThread(ALSAProc, device);
    }
    if(data->thread == NULL)
    {
        AL_PRINT("Could not create playback thread\n");
        free(data->buffer);
        data->buffer = NULL;
        return ALC_FALSE;
    }

    return ALC_TRUE;
}

static void alsa_stop_playback(ALCdevice *device)
{
    alsa_data *data = (alsa_data*)device->ExtraData;

    if(data->thread)
    {
        data->killNow = 1;
        StopThread(data->thread);
        data->thread = NULL;
    }
    data->killNow = 0;
    free(data->buffer);
    data->buffer = NULL;
}


static ALCboolean alsa_open_capture(ALCdevice *pDevice, const ALCchar *deviceName)
{
    snd_pcm_hw_params_t *p;
    snd_pcm_uframes_t bufferSizeInFrames;
    snd_pcm_format_t format;
    ALuint frameSize;
    alsa_data *data;
    char driver[64];
    char *err;
    int i;

    if(!alsa_load())
        return ALC_FALSE;

    strncpy(driver, GetConfigValue("alsa", "capture", "default"), sizeof(driver)-1);
    driver[sizeof(driver)-1] = 0;

    if(!allCaptureDevNameMap)
        allCaptureDevNameMap = probe_devices(SND_PCM_STREAM_CAPTURE, &numCaptureDevNames);

    if(!deviceName)
        deviceName = allCaptureDevNameMap[0].name;
    else
    {
        size_t idx;

        for(idx = 0;idx < numCaptureDevNames;idx++)
        {
            if(allCaptureDevNameMap[idx].name &&
               strcmp(deviceName, allCaptureDevNameMap[idx].name) == 0)
            {
                if(idx > 0)
                    sprintf(driver, "plughw:%d,%d", allCaptureDevNameMap[idx].card, allCaptureDevNameMap[idx].dev);
                break;
            }
        }
        if(idx == numCaptureDevNames)
            return ALC_FALSE;
    }

    data = (alsa_data*)calloc(1, sizeof(alsa_data));

    i = psnd_pcm_open(&data->pcmHandle, driver, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
    if(i < 0)
    {
        AL_PRINT("Could not open capture device '%s': %s\n", driver, psnd_strerror(i));
        free(data);
        return ALC_FALSE;
    }

    format = -1;
    switch(pDevice->FmtType)
    {
        case DevFmtByte:
            format = SND_PCM_FORMAT_S8;
            break;
        case DevFmtUByte:
            format = SND_PCM_FORMAT_U8;
            break;
        case DevFmtShort:
            format = SND_PCM_FORMAT_S16;
            break;
        case DevFmtUShort:
            format = SND_PCM_FORMAT_U16;
            break;
        case DevFmtFloat:
            format = SND_PCM_FORMAT_FLOAT;
            break;
    }

    err = NULL;
    bufferSizeInFrames = pDevice->UpdateSize * pDevice->NumUpdates;
    psnd_pcm_hw_params_malloc(&p);

    if((i=psnd_pcm_hw_params_any(data->pcmHandle, p)) < 0)
        err = "any";
    /* set interleaved access */
    if(i >= 0 && (i=psnd_pcm_hw_params_set_access(data->pcmHandle, p, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
        err = "set access";
    /* set format (implicitly sets sample bits) */
    if(i >= 0 && (i=psnd_pcm_hw_params_set_format(data->pcmHandle, p, format)) < 0)
        err = "set format";
    /* set channels (implicitly sets frame bits) */
    if(i >= 0 && (i=psnd_pcm_hw_params_set_channels(data->pcmHandle, p, ChannelsFromDevFmt(pDevice->FmtChans))) < 0)
        err = "set channels";
    /* set rate (implicitly constrains period/buffer parameters) */
    if(i >= 0 && (i=psnd_pcm_hw_params_set_rate(data->pcmHandle, p, pDevice->Frequency, 0)) < 0)
        err = "set rate near";
    /* set buffer size in frame units (implicitly sets period size/bytes/time and buffer time/bytes) */
    if(i >= 0 && (i=psnd_pcm_hw_params_set_buffer_size_near(data->pcmHandle, p, &bufferSizeInFrames)) < 0)
        err = "set buffer size near";
    /* install and prepare hardware configuration */
    if(i >= 0 && (i=psnd_pcm_hw_params(data->pcmHandle, p)) < 0)
        err = "set params";
    if(i < 0)
    {
        AL_PRINT("%s failed: %s\n", err, psnd_strerror(i));
        psnd_pcm_hw_params_free(p);
        goto error;
    }

    if((i=psnd_pcm_hw_params_get_period_size(p, &bufferSizeInFrames, NULL)) < 0)
    {
        AL_PRINT("get size failed: %s\n", psnd_strerror(i));
        psnd_pcm_hw_params_free(p);
        goto error;
    }

    psnd_pcm_hw_params_free(p);

    frameSize = FrameSizeFromDevFmt(pDevice->FmtChans, pDevice->FmtType);

    data->ring = CreateRingBuffer(frameSize, pDevice->UpdateSize*pDevice->NumUpdates);
    if(!data->ring)
    {
        AL_PRINT("ring buffer create failed\n");
        goto error;
    }

    data->size = psnd_pcm_frames_to_bytes(data->pcmHandle, bufferSizeInFrames);
    data->buffer = malloc(data->size);
    if(!data->buffer)
    {
        AL_PRINT("buffer malloc failed\n");
        goto error;
    }

    pDevice->szDeviceName = strdup(deviceName);

    pDevice->ExtraData = data;
    return ALC_TRUE;

error:
    free(data->buffer);
    DestroyRingBuffer(data->ring);
    psnd_pcm_close(data->pcmHandle);
    free(data);

    pDevice->ExtraData = NULL;
    return ALC_FALSE;
}

static void alsa_close_capture(ALCdevice *pDevice)
{
    alsa_data *data = (alsa_data*)pDevice->ExtraData;

    psnd_pcm_close(data->pcmHandle);
    DestroyRingBuffer(data->ring);

    free(data->buffer);
    free(data);
    pDevice->ExtraData = NULL;
}

static void alsa_start_capture(ALCdevice *Device)
{
    alsa_data *data = (alsa_data*)Device->ExtraData;
    int err;

    err = psnd_pcm_start(data->pcmHandle);
    if(err < 0)
    {
        AL_PRINT("start failed: %s\n", psnd_strerror(err));
        aluHandleDisconnect(Device);
    }
    else
        data->doCapture = AL_TRUE;
}

static void alsa_stop_capture(ALCdevice *Device)
{
    alsa_data *data = (alsa_data*)Device->ExtraData;
    psnd_pcm_drain(data->pcmHandle);
    data->doCapture = AL_FALSE;
}

static ALCuint alsa_available_samples(ALCdevice *Device)
{
    alsa_data *data = (alsa_data*)Device->ExtraData;
    snd_pcm_sframes_t avail;

    avail = (Device->Connected ? psnd_pcm_avail_update(data->pcmHandle) : 0);
    if(avail < 0)
    {
        AL_PRINT("avail update failed: %s\n", psnd_strerror(avail));

        if((avail=psnd_pcm_recover(data->pcmHandle, avail, 1)) >= 0)
        {
            if(data->doCapture)
                avail = psnd_pcm_start(data->pcmHandle);
            if(avail >= 0)
                avail = psnd_pcm_avail_update(data->pcmHandle);
        }
        if(avail < 0)
        {
            AL_PRINT("restore error: %s\n", psnd_strerror(avail));
            aluHandleDisconnect(Device);
        }
    }
    while(avail > 0)
    {
        snd_pcm_sframes_t amt;

        amt = psnd_pcm_bytes_to_frames(data->pcmHandle, data->size);
        if(avail < amt) amt = avail;

        amt = psnd_pcm_readi(data->pcmHandle, data->buffer, amt);
        if(amt < 0)
        {
            AL_PRINT("read error: %s\n", psnd_strerror(amt));

            if(amt == -EAGAIN)
                continue;
            if((amt=psnd_pcm_recover(data->pcmHandle, amt, 1)) >= 0)
            {
                if(data->doCapture)
                    amt = psnd_pcm_start(data->pcmHandle);
                if(amt >= 0)
                    amt = psnd_pcm_avail_update(data->pcmHandle);
            }
            if(amt < 0)
            {
                AL_PRINT("restore error: %s\n", psnd_strerror(amt));
                aluHandleDisconnect(Device);
                break;
            }
            avail = amt;
            continue;
        }

        WriteRingBuffer(data->ring, data->buffer, amt);
        avail -= amt;
    }

    return RingBufferSize(data->ring);
}

static void alsa_capture_samples(ALCdevice *Device, ALCvoid *Buffer, ALCuint Samples)
{
    alsa_data *data = (alsa_data*)Device->ExtraData;

    if(Samples <= alsa_available_samples(Device))
        ReadRingBuffer(data->ring, Buffer, Samples);
    else
        alcSetError(Device, ALC_INVALID_VALUE);
}


BackendFuncs alsa_funcs = {
    alsa_open_playback,
    alsa_close_playback,
    alsa_reset_playback,
    alsa_stop_playback,
    alsa_open_capture,
    alsa_close_capture,
    alsa_start_capture,
    alsa_stop_capture,
    alsa_capture_samples,
    alsa_available_samples
};

void alc_alsa_init(BackendFuncs *func_list)
{
    *func_list = alsa_funcs;
}

void alc_alsa_deinit(void)
{
    ALuint i;

    for(i = 0;i < numDevNames;++i)
        free(allDevNameMap[i].name);
    free(allDevNameMap);
    allDevNameMap = NULL;
    numDevNames = 0;

    for(i = 0;i < numCaptureDevNames;++i)
        free(allCaptureDevNameMap[i].name);
    free(allCaptureDevNameMap);
    allCaptureDevNameMap = NULL;
    numCaptureDevNames = 0;

    if(alsa_handle)
    {
#ifdef HAVE_DLFCN_H
        dlclose(alsa_handle);
#endif
        alsa_handle = NULL;
    }
}

void alc_alsa_probe(int type)
{
    ALuint i;

    if(!alsa_load())
        return;

    if(type == DEVICE_PROBE)
        AppendDeviceList(alsaDevice);
    else if(type == ALL_DEVICE_PROBE)
    {
        for(i = 0;i < numDevNames;++i)
            free(allDevNameMap[i].name);

        free(allDevNameMap);
        allDevNameMap = probe_devices(SND_PCM_STREAM_PLAYBACK, &numDevNames);

        for(i = 0;i < numDevNames;++i)
            AppendAllDeviceList(allDevNameMap[i].name);
    }
    else if(type == CAPTURE_DEVICE_PROBE)
    {
        for(i = 0;i < numCaptureDevNames;++i)
            free(allCaptureDevNameMap[i].name);

        free(allCaptureDevNameMap);
        allCaptureDevNameMap = probe_devices(SND_PCM_STREAM_CAPTURE, &numCaptureDevNames);

        for(i = 0;i < numCaptureDevNames;++i)
            AppendCaptureDeviceList(allCaptureDevNameMap[i].name);
    }
}
