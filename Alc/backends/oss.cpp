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
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include "backends/oss.h"

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory.h>
#include <unistd.h>
#include <cerrno>
#include <poll.h>
#include <cmath>

#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include <algorithm>
#include <functional>

#include "alMain.h"
#include "alu.h"
#include "alconfig.h"
#include "ringbuffer.h"
#include "compat.h"

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

#if defined(SOUND_VERSION) && (SOUND_VERSION < 0x040000)
#define ALC_OSS_COMPAT
#endif
#ifndef SNDCTL_AUDIOINFO
#define ALC_OSS_COMPAT
#endif

/*
 * FreeBSD strongly discourages the use of specific devices,
 * such as those returned in oss_audioinfo.devnode
 */
#ifdef __FreeBSD__
#define ALC_OSS_DEVNODE_TRUC
#endif

namespace {

constexpr char DefaultName[] = "OSS Default";
std::string DefaultPlayback{"/dev/dsp"};
std::string DefaultCapture{"/dev/dsp"};

struct DevMap {
    std::string name;
    std::string device_name;
};

bool checkName(const al::vector<DevMap> &list, const std::string &name)
{
    return std::find_if(list.cbegin(), list.cend(),
        [&name](const DevMap &entry) -> bool
        { return entry.name == name; }
    ) != list.cend();
}

al::vector<DevMap> PlaybackDevices;
al::vector<DevMap> CaptureDevices;


#ifdef ALC_OSS_COMPAT

#define DSP_CAP_OUTPUT 0x00020000
#define DSP_CAP_INPUT 0x00010000
void ALCossListPopulate(al::vector<DevMap> *devlist, int type)
{
    devlist->emplace_back(DevMap{DefaultName, (type==DSP_CAP_INPUT) ? DefaultCapture : DefaultPlayback});
}

#else

void ALCossListAppend(al::vector<DevMap> *list, const char *handle, size_t hlen, const char *path, size_t plen)
{
#ifdef ALC_OSS_DEVNODE_TRUC
    for(size_t i{0};i < plen;i++)
    {
        if(path[i] == '.')
        {
            if(strncmp(path + i, handle + hlen + i - plen, plen - i) == 0)
                hlen = hlen + i - plen;
            plen = i;
        }
    }
#endif
    if(handle[0] == '\0')
    {
        handle = path;
        hlen = plen;
    }

    std::string basename{handle, hlen};
    basename.erase(std::find(basename.begin(), basename.end(), '\0'), basename.end());
    std::string devname{path, plen};
    devname.erase(std::find(devname.begin(), devname.end(), '\0'), devname.end());

    auto iter = std::find_if(list->cbegin(), list->cend(),
        [&devname](const DevMap &entry) -> bool
        { return entry.device_name == devname; }
    );
    if(iter != list->cend())
        return;

    int count{1};
    std::string newname{basename};
    while(checkName(PlaybackDevices, newname))
    {
        newname = basename;
        newname += " #";
        newname += std::to_string(++count);
    }

    list->emplace_back(DevMap{std::move(newname), std::move(devname)});
    const DevMap &entry = list->back();

    TRACE("Got device \"%s\", \"%s\"\n", entry.name.c_str(), entry.device_name.c_str());
}

void ALCossListPopulate(al::vector<DevMap> *devlist, int type_flag)
{
    int fd{open("/dev/mixer", O_RDONLY)};
    if(fd < 0)
    {
        TRACE("Could not open /dev/mixer: %s\n", strerror(errno));
        goto done;
    }

    oss_sysinfo si;
    if(ioctl(fd, SNDCTL_SYSINFO, &si) == -1)
    {
        TRACE("SNDCTL_SYSINFO failed: %s\n", strerror(errno));
        goto done;
    }

    for(int i{0};i < si.numaudios;i++)
    {
        oss_audioinfo ai;
        ai.dev = i;
        if(ioctl(fd, SNDCTL_AUDIOINFO, &ai) == -1)
        {
            ERR("SNDCTL_AUDIOINFO (%d) failed: %s\n", i, strerror(errno));
            continue;
        }
        if(!(ai.caps&type_flag) || ai.devnode[0] == '\0')
            continue;

        const char *handle;
        size_t len;
        if(ai.handle[0] != '\0')
        {
            len = strnlen(ai.handle, sizeof(ai.handle));
            handle = ai.handle;
        }
        else
        {
            len = strnlen(ai.name, sizeof(ai.name));
            handle = ai.name;
        }

        ALCossListAppend(devlist, handle, len, ai.devnode,
                         strnlen(ai.devnode, sizeof(ai.devnode)));
    }

done:
    if(fd >= 0)
        close(fd);
    fd = -1;

    const char *defdev{((type_flag==DSP_CAP_INPUT) ? DefaultCapture : DefaultPlayback).c_str()};
    auto iter = std::find_if(devlist->cbegin(), devlist->cend(),
        [defdev](const DevMap &entry) -> bool
        { return entry.device_name == defdev; }
    );
    if(iter == devlist->cend())
        devlist->insert(devlist->begin(), DevMap{DefaultName, defdev});
    else
    {
        DevMap entry{std::move(*iter)};
        devlist->erase(iter);
        devlist->insert(devlist->begin(), std::move(entry));
    }
    devlist->shrink_to_fit();
}

#endif

int log2i(ALCuint x)
{
    int y = 0;
    while (x > 1)
    {
        x >>= 1;
        y++;
    }
    return y;
}


struct OSSPlayback final : public BackendBase {
    OSSPlayback(ALCdevice *device) noexcept : BackendBase{device} { }
    ~OSSPlayback() override;

    int mixerProc();

    ALCenum open(const ALCchar *name) override;
    ALCboolean reset() override;
    ALCboolean start() override;
    void stop() override;

    int mFd{-1};

    al::vector<ALubyte> mMixData;

    std::atomic<bool> mKillNow{true};
    std::thread mThread;

    DEF_NEWDEL(OSSPlayback)
};

OSSPlayback::~OSSPlayback()
{
    if(mFd != -1)
        close(mFd);
    mFd = -1;
}


int OSSPlayback::mixerProc()
{
    SetRTPriority();
    althrd_setname(MIXER_THREAD_NAME);

    const int frame_size{mDevice->frameSizeFromFmt()};

    lock();
    while(!mKillNow.load(std::memory_order_acquire) &&
          mDevice->Connected.load(std::memory_order_acquire))
    {
        pollfd pollitem{};
        pollitem.fd = mFd;
        pollitem.events = POLLOUT;

        unlock();
        int pret{poll(&pollitem, 1, 1000)};
        lock();
        if(pret < 0)
        {
            if(errno == EINTR || errno == EAGAIN)
                continue;
            ERR("poll failed: %s\n", strerror(errno));
            aluHandleDisconnect(mDevice, "Failed waiting for playback buffer: %s", strerror(errno));
            break;
        }
        else if(pret == 0)
        {
            WARN("poll timeout\n");
            continue;
        }

        ALubyte *write_ptr{mMixData.data()};
        size_t to_write{mMixData.size()};
        aluMixData(mDevice, write_ptr, to_write/frame_size);
        while(to_write > 0 && !mKillNow.load(std::memory_order_acquire))
        {
            ssize_t wrote{write(mFd, write_ptr, to_write)};
            if(wrote < 0)
            {
                if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                    continue;
                ERR("write failed: %s\n", strerror(errno));
                aluHandleDisconnect(mDevice, "Failed writing playback samples: %s",
                                    strerror(errno));
                break;
            }

            to_write -= wrote;
            write_ptr += wrote;
        }
    }
    unlock();

    return 0;
}


ALCenum OSSPlayback::open(const ALCchar *name)
{
    const char *devname{DefaultPlayback.c_str()};
    if(!name)
        name = DefaultName;
    else
    {
        if(PlaybackDevices.empty())
            ALCossListPopulate(&PlaybackDevices, DSP_CAP_OUTPUT);

        auto iter = std::find_if(PlaybackDevices.cbegin(), PlaybackDevices.cend(),
            [&name](const DevMap &entry) -> bool
            { return entry.name == name; }
        );
        if(iter == PlaybackDevices.cend())
            return ALC_INVALID_VALUE;
        devname = iter->device_name.c_str();
    }

    mFd = ::open(devname, O_WRONLY);
    if(mFd == -1)
    {
        ERR("Could not open %s: %s\n", devname, strerror(errno));
        return ALC_INVALID_VALUE;
    }

    mDevice->DeviceName = name;
    return ALC_NO_ERROR;
}

ALCboolean OSSPlayback::reset()
{
    int numFragmentsLogSize;
    int log2FragmentSize;
    unsigned int periods;
    audio_buf_info info;
    ALuint frameSize;
    int numChannels;
    int ossFormat;
    int ossSpeed;
    const char *err;

    switch(mDevice->FmtType)
    {
        case DevFmtByte:
            ossFormat = AFMT_S8;
            break;
        case DevFmtUByte:
            ossFormat = AFMT_U8;
            break;
        case DevFmtUShort:
        case DevFmtInt:
        case DevFmtUInt:
        case DevFmtFloat:
            mDevice->FmtType = DevFmtShort;
            /* fall-through */
        case DevFmtShort:
            ossFormat = AFMT_S16_NE;
            break;
    }

    periods = mDevice->BufferSize / mDevice->UpdateSize;
    numChannels = mDevice->channelsFromFmt();
    ossSpeed = mDevice->Frequency;
    frameSize = numChannels * mDevice->bytesFromFmt();
    /* According to the OSS spec, 16 bytes (log2(16)) is the minimum. */
    log2FragmentSize = maxi(log2i(mDevice->UpdateSize*frameSize), 4);
    numFragmentsLogSize = (periods << 16) | log2FragmentSize;

#define CHECKERR(func) if((func) < 0) {                                       \
    err = #func;                                                              \
    goto err;                                                                 \
}
    /* Don't fail if SETFRAGMENT fails. We can handle just about anything
     * that's reported back via GETOSPACE */
    ioctl(mFd, SNDCTL_DSP_SETFRAGMENT, &numFragmentsLogSize);
    CHECKERR(ioctl(mFd, SNDCTL_DSP_SETFMT, &ossFormat));
    CHECKERR(ioctl(mFd, SNDCTL_DSP_CHANNELS, &numChannels));
    CHECKERR(ioctl(mFd, SNDCTL_DSP_SPEED, &ossSpeed));
    CHECKERR(ioctl(mFd, SNDCTL_DSP_GETOSPACE, &info));
    if(0)
    {
    err:
        ERR("%s failed: %s\n", err, strerror(errno));
        return ALC_FALSE;
    }
#undef CHECKERR

    if(mDevice->channelsFromFmt() != numChannels)
    {
        ERR("Failed to set %s, got %d channels instead\n", DevFmtChannelsString(mDevice->FmtChans),
            numChannels);
        return ALC_FALSE;
    }

    if(!((ossFormat == AFMT_S8 && mDevice->FmtType == DevFmtByte) ||
         (ossFormat == AFMT_U8 && mDevice->FmtType == DevFmtUByte) ||
         (ossFormat == AFMT_S16_NE && mDevice->FmtType == DevFmtShort)))
    {
        ERR("Failed to set %s samples, got OSS format %#x\n", DevFmtTypeString(mDevice->FmtType),
            ossFormat);
        return ALC_FALSE;
    }

    mDevice->Frequency = ossSpeed;
    mDevice->UpdateSize = info.fragsize / frameSize;
    mDevice->BufferSize = info.fragments * mDevice->UpdateSize;

    SetDefaultChannelOrder(mDevice);

    mMixData.resize(mDevice->UpdateSize * mDevice->frameSizeFromFmt());

    return ALC_TRUE;
}

ALCboolean OSSPlayback::start()
{
    try {
        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{std::mem_fn(&OSSPlayback::mixerProc), this};
        return ALC_TRUE;
    }
    catch(std::exception& e) {
        ERR("Could not create playback thread: %s\n", e.what());
    }
    catch(...) {
    }
    return ALC_FALSE;
}

void OSSPlayback::stop()
{
    if(mKillNow.exchange(true, std::memory_order_acq_rel) || !mThread.joinable())
        return;
    mThread.join();

    if(ioctl(mFd, SNDCTL_DSP_RESET) != 0)
        ERR("Error resetting device: %s\n", strerror(errno));
}


struct OSScapture final : public BackendBase {
    OSScapture(ALCdevice *device) noexcept : BackendBase{device} { }
    ~OSScapture() override;

    int recordProc();

    ALCenum open(const ALCchar *name) override;
    ALCboolean start() override;
    void stop() override;
    ALCenum captureSamples(ALCvoid *buffer, ALCuint samples) override;
    ALCuint availableSamples() override;

    int mFd{-1};

    RingBufferPtr mRing{nullptr};

    std::atomic<bool> mKillNow{true};
    std::thread mThread;

    DEF_NEWDEL(OSScapture)
};

OSScapture::~OSScapture()
{
    if(mFd != -1)
        close(mFd);
    mFd = -1;
}


int OSScapture::recordProc()
{
    SetRTPriority();
    althrd_setname(RECORD_THREAD_NAME);

    const int frame_size{mDevice->frameSizeFromFmt()};
    while(!mKillNow.load(std::memory_order_acquire))
    {
        pollfd pollitem{};
        pollitem.fd = mFd;
        pollitem.events = POLLIN;

        int sret{poll(&pollitem, 1, 1000)};
        if(sret < 0)
        {
            if(errno == EINTR || errno == EAGAIN)
                continue;
            ERR("poll failed: %s\n", strerror(errno));
            aluHandleDisconnect(mDevice, "Failed to check capture samples: %s", strerror(errno));
            break;
        }
        else if(sret == 0)
        {
            WARN("poll timeout\n");
            continue;
        }

        auto vec = mRing->getWriteVector();
        if(vec.first.len > 0)
        {
            ssize_t amt{read(mFd, vec.first.buf, vec.first.len*frame_size)};
            if(amt < 0)
            {
                ERR("read failed: %s\n", strerror(errno));
                aluHandleDisconnect(mDevice, "Failed reading capture samples: %s",
                    strerror(errno));
                break;
            }
            mRing->writeAdvance(amt/frame_size);
        }
    }

    return 0;
}


ALCenum OSScapture::open(const ALCchar *name)
{
    const char *devname{DefaultCapture.c_str()};
    if(!name)
        name = DefaultName;
    else
    {
        if(CaptureDevices.empty())
            ALCossListPopulate(&CaptureDevices, DSP_CAP_INPUT);

        auto iter = std::find_if(CaptureDevices.cbegin(), CaptureDevices.cend(),
            [&name](const DevMap &entry) -> bool
            { return entry.name == name; }
        );
        if(iter == CaptureDevices.cend())
            return ALC_INVALID_VALUE;
        devname = iter->device_name.c_str();
    }

    mFd = ::open(devname, O_RDONLY);
    if(mFd == -1)
    {
        ERR("Could not open %s: %s\n", devname, strerror(errno));
        return ALC_INVALID_VALUE;
    }

    int ossFormat{};
    switch(mDevice->FmtType)
    {
        case DevFmtByte:
            ossFormat = AFMT_S8;
            break;
        case DevFmtUByte:
            ossFormat = AFMT_U8;
            break;
        case DevFmtShort:
            ossFormat = AFMT_S16_NE;
            break;
        case DevFmtUShort:
        case DevFmtInt:
        case DevFmtUInt:
        case DevFmtFloat:
            ERR("%s capture samples not supported\n", DevFmtTypeString(mDevice->FmtType));
            return ALC_INVALID_VALUE;
    }

    int periods{4};
    int numChannels{mDevice->channelsFromFmt()};
    int frameSize{numChannels * mDevice->bytesFromFmt()};
    int ossSpeed{static_cast<int>(mDevice->Frequency)};
    int log2FragmentSize{log2i(mDevice->BufferSize * frameSize / periods)};

    /* according to the OSS spec, 16 bytes are the minimum */
    log2FragmentSize = std::max(log2FragmentSize, 4);
    int numFragmentsLogSize{(periods << 16) | log2FragmentSize};

    audio_buf_info info;
    const char *err;
#define CHECKERR(func) if((func) < 0) {                                       \
    err = #func;                                                              \
    goto err;                                                                 \
}
    CHECKERR(ioctl(mFd, SNDCTL_DSP_SETFRAGMENT, &numFragmentsLogSize));
    CHECKERR(ioctl(mFd, SNDCTL_DSP_SETFMT, &ossFormat));
    CHECKERR(ioctl(mFd, SNDCTL_DSP_CHANNELS, &numChannels));
    CHECKERR(ioctl(mFd, SNDCTL_DSP_SPEED, &ossSpeed));
    CHECKERR(ioctl(mFd, SNDCTL_DSP_GETISPACE, &info));
    if(0)
    {
    err:
        ERR("%s failed: %s\n", err, strerror(errno));
        close(mFd);
        mFd = -1;
        return ALC_INVALID_VALUE;
    }
#undef CHECKERR

    if(mDevice->channelsFromFmt() != numChannels)
    {
        ERR("Failed to set %s, got %d channels instead\n", DevFmtChannelsString(mDevice->FmtChans),
            numChannels);
        close(mFd);
        mFd = -1;
        return ALC_INVALID_VALUE;
    }

    if(!((ossFormat == AFMT_S8 && mDevice->FmtType == DevFmtByte) ||
         (ossFormat == AFMT_U8 && mDevice->FmtType == DevFmtUByte) ||
         (ossFormat == AFMT_S16_NE && mDevice->FmtType == DevFmtShort)))
    {
        ERR("Failed to set %s samples, got OSS format %#x\n", DevFmtTypeString(mDevice->FmtType), ossFormat);
        close(mFd);
        mFd = -1;
        return ALC_INVALID_VALUE;
    }

    mRing = CreateRingBuffer(mDevice->BufferSize, frameSize, false);
    if(!mRing)
    {
        ERR("Ring buffer create failed\n");
        close(mFd);
        mFd = -1;
        return ALC_OUT_OF_MEMORY;
    }

    mDevice->DeviceName = name;
    return ALC_NO_ERROR;
}

ALCboolean OSScapture::start()
{
    try {
        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{std::mem_fn(&OSScapture::recordProc), this};
        return ALC_TRUE;
    }
    catch(std::exception& e) {
        ERR("Could not create record thread: %s\n", e.what());
    }
    catch(...) {
    }
    return ALC_FALSE;
}

void OSScapture::stop()
{
    if(mKillNow.exchange(true, std::memory_order_acq_rel) || !mThread.joinable())
        return;
    mThread.join();

    if(ioctl(mFd, SNDCTL_DSP_RESET) != 0)
        ERR("Error resetting device: %s\n", strerror(errno));
}

ALCenum OSScapture::captureSamples(ALCvoid *buffer, ALCuint samples)
{
    mRing->read(buffer, samples);
    return ALC_NO_ERROR;
}

ALCuint OSScapture::availableSamples()
{ return mRing->readSpace(); }

} // namespace


BackendFactory &OSSBackendFactory::getFactory()
{
    static OSSBackendFactory factory{};
    return factory;
}

bool OSSBackendFactory::init()
{
    if(auto devopt = ConfigValueStr(nullptr, "oss", "device"))
        DefaultPlayback = std::move(*devopt);
    if(auto capopt = ConfigValueStr(nullptr, "oss", "capture"))
        DefaultCapture = std::move(*capopt);

    return true;
}

bool OSSBackendFactory::querySupport(BackendType type)
{ return (type == BackendType::Playback || type == BackendType::Capture); }

void OSSBackendFactory::probe(DevProbe type, std::string *outnames)
{
    auto add_device = [outnames](const DevMap &entry) -> void
    {
#ifdef HAVE_STAT
        struct stat buf;
        if(stat(entry.device_name.c_str(), &buf) == 0)
#endif
        {
            /* Includes null char. */
            outnames->append(entry.name.c_str(), entry.name.length()+1);
        }
    };

    switch(type)
    {
        case DevProbe::Playback:
            PlaybackDevices.clear();
            ALCossListPopulate(&PlaybackDevices, DSP_CAP_OUTPUT);
            std::for_each(PlaybackDevices.cbegin(), PlaybackDevices.cend(), add_device);
            break;

        case DevProbe::Capture:
            CaptureDevices.clear();
            ALCossListPopulate(&CaptureDevices, DSP_CAP_INPUT);
            std::for_each(CaptureDevices.cbegin(), CaptureDevices.cend(), add_device);
            break;
    }
}

BackendPtr OSSBackendFactory::createBackend(ALCdevice *device, BackendType type)
{
    if(type == BackendType::Playback)
        return BackendPtr{new OSSPlayback{device}};
    if(type == BackendType::Capture)
        return BackendPtr{new OSScapture{device}};
    return nullptr;
}
