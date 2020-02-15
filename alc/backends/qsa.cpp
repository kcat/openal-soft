/**
 * OpenAL cross platform audio library
 * Copyright (C) 2011-2013 by authors.
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

#include "backends/qsa.h"

#include <stdlib.h>
#include <stdio.h>
#include <sched.h>
#include <errno.h>
#include <memory.h>
#include <poll.h>

#include <thread>
#include <memory>
#include <algorithm>

#include "alcmain.h"
#include "alexcpt.h"
#include "alu.h"
#include "threads.h"

#include <sys/asoundlib.h>
#include <sys/neutrino.h>


namespace {

struct qsa_data {
    snd_pcm_t* pcmHandle{nullptr};
    int audio_fd{-1};

    snd_pcm_channel_setup_t  csetup{};
    snd_pcm_channel_params_t cparams{};

    ALvoid* buffer{nullptr};
    ALsizei size{0};

    std::atomic<ALenum> mKillNow{AL_TRUE};
    std::thread mThread;
};

struct DevMap {
    ALCchar* name;
    int card;
    int dev;
};

al::vector<DevMap> DeviceNameMap;
al::vector<DevMap> CaptureNameMap;

constexpr ALCchar qsaDevice[] = "QSA Default";

constexpr struct {
    int32_t format;
} formatlist[] = {
    {SND_PCM_SFMT_FLOAT_LE},
    {SND_PCM_SFMT_S32_LE},
    {SND_PCM_SFMT_U32_LE},
    {SND_PCM_SFMT_S16_LE},
    {SND_PCM_SFMT_U16_LE},
    {SND_PCM_SFMT_S8},
    {SND_PCM_SFMT_U8},
    {0},
};

constexpr struct {
    int32_t rate;
} ratelist[] = {
    {192000},
    {176400},
    {96000},
    {88200},
    {48000},
    {44100},
    {32000},
    {24000},
    {22050},
    {16000},
    {12000},
    {11025},
    {8000},
    {0},
};

constexpr struct {
    int32_t channels;
} channellist[] = {
    {8},
    {7},
    {6},
    {4},
    {2},
    {1},
    {0},
};

void deviceList(int type, al::vector<DevMap> *devmap)
{
    snd_ctl_t* handle;
    snd_pcm_info_t pcminfo;
    int max_cards, card, err, dev;
    DevMap entry;
    char name[1024];
    snd_ctl_hw_info info;

    max_cards = snd_cards();
    if(max_cards < 0)
        return;

    std::for_each(devmap->begin(), devmap->end(),
        [](const DevMap &entry) -> void
        { free(entry.name); }
    );
    devmap->clear();

    entry.name = strdup(qsaDevice);
    entry.card = 0;
    entry.dev = 0;
    devmap->push_back(entry);

    for(card = 0;card < max_cards;card++)
    {
        if((err=snd_ctl_open(&handle, card)) < 0)
            continue;

        if((err=snd_ctl_hw_info(handle, &info)) < 0)
        {
            snd_ctl_close(handle);
            continue;
        }

        for(dev = 0;dev < (int)info.pcmdevs;dev++)
        {
            if((err=snd_ctl_pcm_info(handle, dev, &pcminfo)) < 0)
                continue;

            if((type==SND_PCM_CHANNEL_PLAYBACK && (pcminfo.flags&SND_PCM_INFO_PLAYBACK)) ||
               (type==SND_PCM_CHANNEL_CAPTURE && (pcminfo.flags&SND_PCM_INFO_CAPTURE)))
            {
                snprintf(name, sizeof(name), "%s [%s] (hw:%d,%d)", info.name, pcminfo.name, card, dev);
                entry.name = strdup(name);
                entry.card = card;
                entry.dev = dev;

                devmap->push_back(entry);
                TRACE("Got device \"%s\", card %d, dev %d\n", name, card, dev);
            }
        }
        snd_ctl_close(handle);
    }
}


/* Wrappers to use an old-style backend with the new interface. */
struct PlaybackWrapper final : public BackendBase {
    PlaybackWrapper(ALCdevice *device) noexcept : BackendBase{device} { }
    ~PlaybackWrapper() override;

    void open(const ALCchar *name) override;
    bool reset() override;
    bool start() override;
    void stop() override;

    std::unique_ptr<qsa_data> mExtraData;

    DEF_NEWDEL(PlaybackWrapper)
};


FORCE_ALIGN static int qsa_proc_playback(void *ptr)
{
    PlaybackWrapper *self = static_cast<PlaybackWrapper*>(ptr);
    ALCdevice *device = self->mDevice;
    qsa_data *data = self->mExtraData.get();
    snd_pcm_channel_status_t status;
    sched_param param;
    char* write_ptr;
    ALint len;
    int sret;

    SetRTPriority();
    althrd_setname(MIXER_THREAD_NAME);

    /* Increase default 10 priority to 11 to avoid jerky sound */
    SchedGet(0, 0, &param);
    param.sched_priority=param.sched_curpriority+1;
    SchedSet(0, 0, SCHED_NOCHANGE, &param);

    const ALint frame_size = device->frameSizeFromFmt();

    std::unique_lock<PlaybackWrapper> dlock{*self};
    while(!data->mKillNow.load(std::memory_order_acquire))
    {
        pollfd pollitem{};
        pollitem.fd = data->audio_fd;
        pollitem.events = POLLOUT;

        /* Select also works like time slice to OS */
        dlock.unlock();
        sret = poll(&pollitem, 1, 2000);
        dlock.lock();
        if(sret == -1)
        {
            if(errno == EINTR || errno == EAGAIN)
                continue;
            ERR("poll error: %s\n", strerror(errno));
            aluHandleDisconnect(device, "Failed waiting for playback buffer: %s", strerror(errno));
            break;
        }
        if(sret == 0)
        {
            ERR("poll timeout\n");
            continue;
        }

        len = data->size;
        write_ptr = static_cast<char*>(data->buffer);
        aluMixData(device, write_ptr, len/frame_size);
        while(len>0 && !data->mKillNow.load(std::memory_order_acquire))
        {
            int wrote = snd_pcm_plugin_write(data->pcmHandle, write_ptr, len);
            if(wrote <= 0)
            {
                if(errno==EAGAIN || errno==EWOULDBLOCK)
                    continue;

                memset(&status, 0, sizeof(status));
                status.channel = SND_PCM_CHANNEL_PLAYBACK;

                snd_pcm_plugin_status(data->pcmHandle, &status);

                /* we need to reinitialize the sound channel if we've underrun the buffer */
                if(status.status == SND_PCM_STATUS_UNDERRUN ||
                   status.status == SND_PCM_STATUS_READY)
                {
                    if(snd_pcm_plugin_prepare(data->pcmHandle, SND_PCM_CHANNEL_PLAYBACK) < 0)
                    {
                        aluHandleDisconnect(device, "Playback recovery failed");
                        break;
                    }
                }
            }
            else
            {
                write_ptr += wrote;
                len -= wrote;
            }
        }
    }

    return 0;
}

/************/
/* Playback */
/************/

static ALCenum qsa_open_playback(PlaybackWrapper *self, const ALCchar* deviceName)
{
    ALCdevice *device = self->mDevice;
    int card, dev;
    int status;

    std::unique_ptr<qsa_data> data{new qsa_data{}};
    data->mKillNow.store(AL_TRUE, std::memory_order_relaxed);

    if(!deviceName)
        deviceName = qsaDevice;

    if(strcmp(deviceName, qsaDevice) == 0)
        status = snd_pcm_open_preferred(&data->pcmHandle, &card, &dev, SND_PCM_OPEN_PLAYBACK);
    else
    {
        if(DeviceNameMap.empty())
            deviceList(SND_PCM_CHANNEL_PLAYBACK, &DeviceNameMap);

        auto iter = std::find_if(DeviceNameMap.begin(), DeviceNameMap.end(),
            [deviceName](const DevMap &entry) -> bool
            { return entry.name && strcmp(deviceName, entry.name) == 0; }
        );
        if(iter == DeviceNameMap.cend())
            return ALC_INVALID_DEVICE;

        status = snd_pcm_open(&data->pcmHandle, iter->card, iter->dev, SND_PCM_OPEN_PLAYBACK);
    }

    if(status < 0)
        return ALC_INVALID_DEVICE;

    data->audio_fd = snd_pcm_file_descriptor(data->pcmHandle, SND_PCM_CHANNEL_PLAYBACK);
    if(data->audio_fd < 0)
    {
        snd_pcm_close(data->pcmHandle);
        return ALC_INVALID_DEVICE;
    }

    device->DeviceName = deviceName;
    self->mExtraData = std::move(data);

    return ALC_NO_ERROR;
}

static void qsa_close_playback(PlaybackWrapper *self)
{
    qsa_data *data = self->mExtraData.get();

    if (data->buffer!=NULL)
    {
        free(data->buffer);
        data->buffer=NULL;
    }

    snd_pcm_close(data->pcmHandle);

    self->mExtraData = nullptr;
}

static ALCboolean qsa_reset_playback(PlaybackWrapper *self)
{
    ALCdevice *device = self->mDevice;
    qsa_data *data = self->mExtraData.get();
    int32_t format=-1;

    switch(device->FmtType)
    {
        case DevFmtByte:
             format=SND_PCM_SFMT_S8;
             break;
        case DevFmtUByte:
             format=SND_PCM_SFMT_U8;
             break;
        case DevFmtShort:
             format=SND_PCM_SFMT_S16_LE;
             break;
        case DevFmtUShort:
             format=SND_PCM_SFMT_U16_LE;
             break;
        case DevFmtInt:
             format=SND_PCM_SFMT_S32_LE;
             break;
        case DevFmtUInt:
             format=SND_PCM_SFMT_U32_LE;
             break;
        case DevFmtFloat:
             format=SND_PCM_SFMT_FLOAT_LE;
             break;
    }

    /* we actually don't want to block on writes */
    snd_pcm_nonblock_mode(data->pcmHandle, 1);
    /* Disable mmap to control data transfer to the audio device */
    snd_pcm_plugin_set_disable(data->pcmHandle, PLUGIN_DISABLE_MMAP);
    snd_pcm_plugin_set_disable(data->pcmHandle, PLUGIN_DISABLE_BUFFER_PARTIAL_BLOCKS);

    // configure a sound channel
    memset(&data->cparams, 0, sizeof(data->cparams));
    data->cparams.channel=SND_PCM_CHANNEL_PLAYBACK;
    data->cparams.mode=SND_PCM_MODE_BLOCK;
    data->cparams.start_mode=SND_PCM_START_FULL;
    data->cparams.stop_mode=SND_PCM_STOP_STOP;

    data->cparams.buf.block.frag_size=device->UpdateSize * device->frameSizeFromFmt();
    data->cparams.buf.block.frags_max=device->BufferSize / device->UpdateSize;
    data->cparams.buf.block.frags_min=data->cparams.buf.block.frags_max;

    data->cparams.format.interleave=1;
    data->cparams.format.rate=device->Frequency;
    data->cparams.format.voices=device->channelsFromFmt();
    data->cparams.format.format=format;

    if ((snd_pcm_plugin_params(data->pcmHandle, &data->cparams))<0)
    {
        int original_rate=data->cparams.format.rate;
        int original_voices=data->cparams.format.voices;
        int original_format=data->cparams.format.format;
        int it;
        int jt;

        for (it=0; it<1; it++)
        {
            /* Check for second pass */
            if (it==1)
            {
                original_rate=ratelist[0].rate;
                original_voices=channellist[0].channels;
                original_format=formatlist[0].format;
            }

            do {
                /* At first downgrade sample format */
                jt=0;
                do {
                    if (formatlist[jt].format==data->cparams.format.format)
                    {
                        data->cparams.format.format=formatlist[jt+1].format;
                        break;
                    }
                    if (formatlist[jt].format==0)
                    {
                        data->cparams.format.format=0;
                        break;
                    }
                    jt++;
                } while(1);

                if (data->cparams.format.format==0)
                {
                    data->cparams.format.format=original_format;

                    /* At secod downgrade sample rate */
                    jt=0;
                    do {
                        if (ratelist[jt].rate==data->cparams.format.rate)
                        {
                            data->cparams.format.rate=ratelist[jt+1].rate;
                            break;
                        }
                        if (ratelist[jt].rate==0)
                        {
                            data->cparams.format.rate=0;
                            break;
                        }
                        jt++;
                    } while(1);

                    if (data->cparams.format.rate==0)
                    {
                        data->cparams.format.rate=original_rate;
                        data->cparams.format.format=original_format;

                        /* At third downgrade channels number */
                        jt=0;
                        do {
                            if(channellist[jt].channels==data->cparams.format.voices)
                            {
                                data->cparams.format.voices=channellist[jt+1].channels;
                                break;
                            }
                            if (channellist[jt].channels==0)
                            {
                                data->cparams.format.voices=0;
                                break;
                            }
                           jt++;
                        } while(1);
                    }

                    if (data->cparams.format.voices==0)
                    {
                        break;
                    }
                }

                data->cparams.buf.block.frag_size=device->UpdateSize*
                    data->cparams.format.voices*
                    snd_pcm_format_width(data->cparams.format.format)/8;
                data->cparams.buf.block.frags_max=device->NumUpdates;
                data->cparams.buf.block.frags_min=device->NumUpdates;
                if ((snd_pcm_plugin_params(data->pcmHandle, &data->cparams))<0)
                {
                    continue;
                }
                else
                {
                    break;
                }
            } while(1);

            if (data->cparams.format.voices!=0)
            {
                break;
            }
        }

        if (data->cparams.format.voices==0)
        {
            return ALC_FALSE;
        }
    }

    if ((snd_pcm_plugin_prepare(data->pcmHandle, SND_PCM_CHANNEL_PLAYBACK))<0)
    {
        return ALC_FALSE;
    }

    memset(&data->csetup, 0, sizeof(data->csetup));
    data->csetup.channel=SND_PCM_CHANNEL_PLAYBACK;
    if (snd_pcm_plugin_setup(data->pcmHandle, &data->csetup)<0)
    {
        return ALC_FALSE;
    }

    /* now fill back to the our AL device */
    device->Frequency=data->cparams.format.rate;

    switch (data->cparams.format.voices)
    {
        case 1:
             device->FmtChans=DevFmtMono;
             break;
        case 2:
             device->FmtChans=DevFmtStereo;
             break;
        case 4:
             device->FmtChans=DevFmtQuad;
             break;
        case 6:
             device->FmtChans=DevFmtX51;
             break;
        case 7:
             device->FmtChans=DevFmtX61;
             break;
        case 8:
             device->FmtChans=DevFmtX71;
             break;
        default:
             device->FmtChans=DevFmtMono;
             break;
    }

    switch (data->cparams.format.format)
    {
        case SND_PCM_SFMT_S8:
             device->FmtType=DevFmtByte;
             break;
        case SND_PCM_SFMT_U8:
             device->FmtType=DevFmtUByte;
             break;
        case SND_PCM_SFMT_S16_LE:
             device->FmtType=DevFmtShort;
             break;
        case SND_PCM_SFMT_U16_LE:
             device->FmtType=DevFmtUShort;
             break;
        case SND_PCM_SFMT_S32_LE:
             device->FmtType=DevFmtInt;
             break;
        case SND_PCM_SFMT_U32_LE:
             device->FmtType=DevFmtUInt;
             break;
        case SND_PCM_SFMT_FLOAT_LE:
             device->FmtType=DevFmtFloat;
             break;
        default:
             device->FmtType=DevFmtShort;
             break;
    }

    SetDefaultChannelOrder(device);

    device->UpdateSize=data->csetup.buf.block.frag_size / device->frameSizeFromFmt();
    device->NumUpdates=data->csetup.buf.block.frags;

    data->size=data->csetup.buf.block.frag_size;
    data->buffer=malloc(data->size);
    if (!data->buffer)
    {
        return ALC_FALSE;
    }

    return ALC_TRUE;
}

static ALCboolean qsa_start_playback(PlaybackWrapper *self)
{
    qsa_data *data = self->mExtraData.get();

    try {
        data->mKillNow.store(AL_FALSE, std::memory_order_release);
        data->mThread = std::thread(qsa_proc_playback, self);
        return ALC_TRUE;
    }
    catch(std::exception& e) {
        ERR("Could not create playback thread: %s\n", e.what());
    }
    catch(...) {
    }
    return ALC_FALSE;
}

static void qsa_stop_playback(PlaybackWrapper *self)
{
    qsa_data *data = self->mExtraData.get();

    if(data->mKillNow.exchange(AL_TRUE, std::memory_order_acq_rel) || !data->mThread.joinable())
        return;
    data->mThread.join();
}


PlaybackWrapper::~PlaybackWrapper()
{
    if(mExtraData)
        qsa_close_playback(this);
}

void PlaybackWrapper::open(const ALCchar *name)
{
    if(auto err = qsa_open_playback(this, name))
        throw al::backend_exception{ALC_INVALID_VALUE, "%d", err};
}

bool PlaybackWrapper::reset()
{
    if(!qsa_reset_playback(this))
        throw al::backend_exception{ALC_INVALID_VALUE, ""};
    return true;
}

bool PlaybackWrapper::start()
{ return qsa_start_playback(this); }

void PlaybackWrapper::stop()
{ qsa_stop_playback(this); }


/***********/
/* Capture */
/***********/

struct CaptureWrapper final : public BackendBase {
    CaptureWrapper(ALCdevice *device) noexcept : BackendBase{device} { }
    ~CaptureWrapper() override;

    void open(const ALCchar *name) override;
    bool start() override;
    void stop() override;
    ALCenum captureSamples(al::byte *buffer, ALCuint samples) override;
    ALCuint availableSamples() override;

    std::unique_ptr<qsa_data> mExtraData;

    DEF_NEWDEL(CaptureWrapper)
};

static ALCenum qsa_open_capture(CaptureWrapper *self, const ALCchar *deviceName)
{
    ALCdevice *device = self->mDevice;
    int card, dev;
    int format=-1;
    int status;

    std::unique_ptr<qsa_data> data{new qsa_data{}};

    if(!deviceName)
        deviceName = qsaDevice;

    if(strcmp(deviceName, qsaDevice) == 0)
        status = snd_pcm_open_preferred(&data->pcmHandle, &card, &dev, SND_PCM_OPEN_CAPTURE);
    else
    {
        if(CaptureNameMap.empty())
            deviceList(SND_PCM_CHANNEL_CAPTURE, &CaptureNameMap);

        auto iter = std::find_if(CaptureNameMap.cbegin(), CaptureNameMap.cend(),
            [deviceName](const DevMap &entry) -> bool
            { return entry.name && strcmp(deviceName, entry.name) == 0; }
        );
        if(iter == CaptureNameMap.cend())
            return ALC_INVALID_DEVICE;

        status = snd_pcm_open(&data->pcmHandle, iter->card, iter->dev, SND_PCM_OPEN_CAPTURE);
    }

    if(status < 0)
        return ALC_INVALID_DEVICE;

    data->audio_fd = snd_pcm_file_descriptor(data->pcmHandle, SND_PCM_CHANNEL_CAPTURE);
    if(data->audio_fd < 0)
    {
        snd_pcm_close(data->pcmHandle);
        return ALC_INVALID_DEVICE;
    }

    device->DeviceName = deviceName;

    switch (device->FmtType)
    {
        case DevFmtByte:
             format=SND_PCM_SFMT_S8;
             break;
        case DevFmtUByte:
             format=SND_PCM_SFMT_U8;
             break;
        case DevFmtShort:
             format=SND_PCM_SFMT_S16_LE;
             break;
        case DevFmtUShort:
             format=SND_PCM_SFMT_U16_LE;
             break;
        case DevFmtInt:
             format=SND_PCM_SFMT_S32_LE;
             break;
        case DevFmtUInt:
             format=SND_PCM_SFMT_U32_LE;
             break;
        case DevFmtFloat:
             format=SND_PCM_SFMT_FLOAT_LE;
             break;
    }

    /* we actually don't want to block on reads */
    snd_pcm_nonblock_mode(data->pcmHandle, 1);
    /* Disable mmap to control data transfer to the audio device */
    snd_pcm_plugin_set_disable(data->pcmHandle, PLUGIN_DISABLE_MMAP);

    /* configure a sound channel */
    memset(&data->cparams, 0, sizeof(data->cparams));
    data->cparams.mode=SND_PCM_MODE_BLOCK;
    data->cparams.channel=SND_PCM_CHANNEL_CAPTURE;
    data->cparams.start_mode=SND_PCM_START_GO;
    data->cparams.stop_mode=SND_PCM_STOP_STOP;

    data->cparams.buf.block.frag_size=device->UpdateSize * device->frameSizeFromFmt();
    data->cparams.buf.block.frags_max=device->NumUpdates;
    data->cparams.buf.block.frags_min=device->NumUpdates;

    data->cparams.format.interleave=1;
    data->cparams.format.rate=device->Frequency;
    data->cparams.format.voices=device->channelsFromFmt();
    data->cparams.format.format=format;

    if(snd_pcm_plugin_params(data->pcmHandle, &data->cparams) < 0)
    {
        snd_pcm_close(data->pcmHandle);
        return ALC_INVALID_VALUE;
    }

    self->mExtraData = std::move(data);

    return ALC_NO_ERROR;
}

static void qsa_close_capture(CaptureWrapper *self)
{
    qsa_data *data = self->mExtraData.get();

    if (data->pcmHandle!=nullptr)
        snd_pcm_close(data->pcmHandle);
    data->pcmHandle = nullptr;

    self->mExtraData = nullptr;
}

static void qsa_start_capture(CaptureWrapper *self)
{
    qsa_data *data = self->mExtraData.get();
    int rstatus;

    if ((rstatus=snd_pcm_plugin_prepare(data->pcmHandle, SND_PCM_CHANNEL_CAPTURE))<0)
    {
        ERR("capture prepare failed: %s\n", snd_strerror(rstatus));
        return;
    }

    memset(&data->csetup, 0, sizeof(data->csetup));
    data->csetup.channel=SND_PCM_CHANNEL_CAPTURE;
    if ((rstatus=snd_pcm_plugin_setup(data->pcmHandle, &data->csetup))<0)
    {
        ERR("capture setup failed: %s\n", snd_strerror(rstatus));
        return;
    }

    snd_pcm_capture_go(data->pcmHandle);
}

static void qsa_stop_capture(CaptureWrapper *self)
{
    qsa_data *data = self->mExtraData.get();
    snd_pcm_capture_flush(data->pcmHandle);
}

static ALCuint qsa_available_samples(CaptureWrapper *self)
{
    ALCdevice *device = self->mDevice;
    qsa_data *data = self->mExtraData.get();
    snd_pcm_channel_status_t status;
    ALint frame_size = device->frameSizeFromFmt();
    ALint free_size;
    int rstatus;

    memset(&status, 0, sizeof (status));
    status.channel=SND_PCM_CHANNEL_CAPTURE;
    snd_pcm_plugin_status(data->pcmHandle, &status);
    if ((status.status==SND_PCM_STATUS_OVERRUN) ||
        (status.status==SND_PCM_STATUS_READY))
    {
        if ((rstatus=snd_pcm_plugin_prepare(data->pcmHandle, SND_PCM_CHANNEL_CAPTURE))<0)
        {
            ERR("capture prepare failed: %s\n", snd_strerror(rstatus));
            aluHandleDisconnect(device, "Failed capture recovery: %s", snd_strerror(rstatus));
            return 0;
        }

        snd_pcm_capture_go(data->pcmHandle);
        return 0;
    }

    free_size=data->csetup.buf.block.frag_size*data->csetup.buf.block.frags;
    free_size-=status.free;

    return free_size/frame_size;
}

static ALCenum qsa_capture_samples(CaptureWrapper *self, ALCvoid *buffer, ALCuint samples)
{
    ALCdevice *device = self->mDevice;
    qsa_data *data = self->mExtraData.get();
    char* read_ptr;
    snd_pcm_channel_status_t status;
    int selectret;
    int bytes_read;
    ALint frame_size=device->frameSizeFromFmt();
    ALint len=samples*frame_size;
    int rstatus;

    read_ptr = static_cast<char*>(buffer);

    while (len>0)
    {
        pollfd pollitem{};
        pollitem.fd = data->audio_fd;
        pollitem.events = POLLOUT;

        /* Select also works like time slice to OS */
        bytes_read=0;
        selectret = poll(&pollitem, 1, 2000);
        switch (selectret)
        {
            case -1:
                 aluHandleDisconnect(device, "Failed to check capture samples");
                 return ALC_INVALID_DEVICE;
            case 0:
                 break;
            default:
                 bytes_read=snd_pcm_plugin_read(data->pcmHandle, read_ptr, len);
                 break;
        }

        if (bytes_read<=0)
        {
            if ((errno==EAGAIN) || (errno==EWOULDBLOCK))
            {
                continue;
            }

            memset(&status, 0, sizeof (status));
            status.channel=SND_PCM_CHANNEL_CAPTURE;
            snd_pcm_plugin_status(data->pcmHandle, &status);

            /* we need to reinitialize the sound channel if we've overrun the buffer */
            if ((status.status==SND_PCM_STATUS_OVERRUN) ||
                (status.status==SND_PCM_STATUS_READY))
            {
                if ((rstatus=snd_pcm_plugin_prepare(data->pcmHandle, SND_PCM_CHANNEL_CAPTURE))<0)
                {
                    ERR("capture prepare failed: %s\n", snd_strerror(rstatus));
                    aluHandleDisconnect(device, "Failed capture recovery: %s",
                                        snd_strerror(rstatus));
                    return ALC_INVALID_DEVICE;
                }
                snd_pcm_capture_go(data->pcmHandle);
            }
        }
        else
        {
            read_ptr+=bytes_read;
            len-=bytes_read;
        }
    }

    return ALC_NO_ERROR;
}


CaptureWrapper::~CaptureWrapper()
{
    if(mExtraData)
        qsa_close_capture(this);
}

void CaptureWrapper::open(const ALCchar *name)
{
    if(auto err = qsa_open_capture(this, name))
        throw al::backend_exception{ALC_INVALID_VALUE, "%d", err};
}

bool CaptureWrapper::start()
{ qsa_start_capture(this); return true; }

void CaptureWrapper::stop()
{ qsa_stop_capture(this); }

ALCenum CaptureWrapper::captureSamples(al::byte *buffer, ALCuint samples)
{ return qsa_capture_samples(this, buffer, samples); }

ALCuint CaptureWrapper::availableSamples()
{ return qsa_available_samples(this); }

} // namespace


bool QSABackendFactory::init()
{ return true; }

bool QSABackendFactory::querySupport(BackendType type)
{ return (type == BackendType::Playback || type == BackendType::Capture); }

void QSABackendFactory::probe(DevProbe type, std::string *outnames)
{
    auto add_device = [outnames](const DevMap &entry) -> void
    {
        const char *n = entry.name;
        if(n && n[0])
            outnames->append(n, strlen(n)+1);
    };

    switch (type)
    {
        case DevProbe::Playback:
            deviceList(SND_PCM_CHANNEL_PLAYBACK, &DeviceNameMap);
            std::for_each(DeviceNameMap.cbegin(), DeviceNameMap.cend(), add_device);
            break;
        case DevProbe::Capture:
            deviceList(SND_PCM_CHANNEL_CAPTURE, &CaptureNameMap);
            std::for_each(CaptureNameMap.cbegin(), CaptureNameMap.cend(), add_device);
            break;
    }
}

BackendPtr QSABackendFactory::createBackend(ALCdevice *device, BackendType type)
{
    if(type == BackendType::Playback)
        return BackendPtr{new PlaybackWrapper{device}};
    if(type == BackendType::Capture)
        return BackendPtr{new CaptureWrapper{device}};
    return nullptr;
}

BackendFactory &QSABackendFactory::getFactory()
{
    static QSABackendFactory factory{};
    return factory;
}
