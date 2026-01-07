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

#include "oss.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "alc/alconfig.h"
#include "alformat.hpp"
#include "alnumeric.h"
#include "althrd_setname.h"
#include "core/device.h"
#include "core/helpers.h"
#include "core/logging.h"
#include "gsl/gsl"
#include "ringbuffer.h"

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

#if (defined(SOUND_VERSION) && (SOUND_VERSION < 0x040000)) || !defined(SNDCTL_AUDIOINFO)
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

using namespace std::string_literals;
using namespace std::string_view_literals;

[[nodiscard]] constexpr auto GetDefaultName() noexcept { return "OSS Default"sv; }

auto DefaultPlayback = "/dev/dsp"s; /* NOLINT(cert-err58-cpp) */
auto DefaultCapture = "/dev/dsp"s; /* NOLINT(cert-err58-cpp) */

struct DevMap {
    std::string name;
    std::string device_name;
};

std::vector<DevMap> PlaybackDevices;
std::vector<DevMap> CaptureDevices;


#ifdef ALC_OSS_COMPAT

#define DSP_CAP_OUTPUT 0x00020000
#define DSP_CAP_INPUT 0x00010000
void ALCossListPopulate(std::vector<DevMap> &devlist, int type)
{
    devlist.emplace_back(std::string{GetDefaultName()},
        (type==DSP_CAP_INPUT) ? DefaultCapture : DefaultPlayback);
}

#else

class FileHandle {
    int mFd{-1};

public:
    FileHandle() = default;
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    ~FileHandle() { if(mFd != -1) ::close(mFd); }

    template<typename ...Args>
    [[nodiscard]] auto open(gsl::czstring const fname, Args&& ...args) -> bool
    {
        close();
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) */
        mFd = ::open(fname, std::forward<Args>(args)...);
        return mFd != -1;
    }
    void close()
    {
        if(mFd != -1)
            ::close(mFd);
        mFd = -1;
    }

    template<typename ...Args>
    [[nodiscard]] auto ioctl(Args&& ...args)
    {
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) */
        return ::ioctl(mFd, std::forward<Args>(args)...);
    }
};

void ALCossListAppend(std::vector<DevMap> &list, std::string_view handle, std::string_view path)
{
#ifdef ALC_OSS_DEVNODE_TRUC
    for(auto pos = path.find('.'); pos < path.size(); pos = path.find('.', pos+1))
    {
        if(auto const endlen = path.size() - pos; handle.size() >= endlen)
        {
            if(handle.ends_with(path.substr(pos)))
                handle = handle.substr(0, handle.size() - endlen);
            path = path.substr(0, pos);
            break;
        }
    }
#endif
    if(path.empty())
        return;

    if(std::ranges::find(list, path, &DevMap::device_name) != list.end())
        return;

    if(handle.empty())
        handle = path;

    auto count = 1;
    auto newname = std::string{handle};
    while(std::ranges::find(list, newname, &DevMap::name) != list.end())
        newname = al::format("{} #{}", handle, ++count);

    const auto &entry = list.emplace_back(std::move(newname), std::string{path});
    TRACE(R"(Got device "{}", "{}")", entry.name, entry.device_name);
}

void ALCossListPopulate(std::vector<DevMap> &devlist, int const type_flag)
{
    /* Make sure to move the default device to the start of the devlist (or
     * adding the default if it doesn't exist) before returning.
     */
    auto _ = gsl::finally([&devlist, type_flag]
    {
        auto const &defdev = (type_flag == DSP_CAP_INPUT) ? DefaultCapture : DefaultPlayback;
        if(auto const iter = std::ranges::find(devlist, defdev, &DevMap::device_name);
            iter != devlist.end())
            std::ranges::rotate(devlist.begin(), iter, iter+1);
        else
            devlist.insert(devlist.begin(), DevMap{std::string{GetDefaultName()}, defdev});
        devlist.shrink_to_fit();
    });

    auto si = oss_sysinfo{};
    auto file = FileHandle{};
    if(!file.open("/dev/mixer", O_RDONLY))
    {
        TRACE("Could not open /dev/mixer: {}", std::generic_category().message(errno));
        return;
    }

    if(file.ioctl(SNDCTL_SYSINFO, &si) == -1)
    {
        TRACE("SNDCTL_SYSINFO failed: {}", std::generic_category().message(errno));
        return;
    }

    for(auto const i : std::views::iota(0, si.numaudios))
    {
        auto ai = oss_audioinfo{};
        ai.dev = i;
        if(file.ioctl(SNDCTL_AUDIOINFO, &ai) == -1)
        {
            ERR("SNDCTL_AUDIOINFO ({}) failed: {}", i, std::generic_category().message(errno));
            continue;
        }
        if(!(ai.caps&type_flag) || ai.devnode[0] == '\0')
            continue;

        auto const handle = std::invoke([&ai]() -> std::string_view
        {
            if(ai.handle[0] != '\0')
                return {std::data(ai.handle), strnlen(std::data(ai.handle), std::size(ai.handle))};
            return {std::data(ai.name), strnlen(std::data(ai.name), std::size(ai.name))};
        });
        auto const devnode = std::string_view{std::data(ai.devnode),
            strnlen(std::data(ai.devnode), std::size(ai.devnode))};

        ALCossListAppend(devlist, handle, devnode);
    }
}

#endif

constexpr auto log2i(u32 x) -> u32
{
    auto y = 0_u32;
    while(x > 1)
    {
        x >>= 1;
        y++;
    }
    return y;
}


struct OSSPlayback final : BackendBase {
    explicit OSSPlayback(gsl::not_null<DeviceBase*> const device) noexcept : BackendBase{device}
    { }
    ~OSSPlayback() override;

    void mixerProc();

    void open(std::string_view name) override;
    auto reset() -> bool override;
    void start() override;
    void stop() override;

    int mFd{-1};

    std::vector<std::byte> mMixData;

    std::atomic<bool> mKillNow{true};
    std::thread mThread;
};

OSSPlayback::~OSSPlayback()
{
    if(mFd != -1)
        ::close(mFd);
    mFd = -1;
}


void OSSPlayback::mixerProc()
{
    SetRTPriority();
    althrd_setname(GetMixerThreadName());

    const auto frame_step = usize{mDevice->channelsFromFmt()};
    const auto frame_size = usize{mDevice->frameSizeFromFmt()};

    while(!mKillNow.load(std::memory_order_acquire)
        && mDevice->Connected.load(std::memory_order_acquire))
    {
        auto pollitem = pollfd{};
        pollitem.fd = mFd;
        pollitem.events = POLLOUT;

        if(const auto pret = poll(&pollitem, 1, 1000); pret < 0)
        {
            if(errno == EINTR || errno == EAGAIN)
                continue;
            const auto errstr = std::generic_category().message(errno);
            ERR("poll failed: {}", errstr);
            mDevice->handleDisconnect("Failed waiting for playback buffer: {}", errstr);
            break;
        }
        else if(pret == 0) /* NOLINT(*-else-after-return) 'pret' is local to the if/else blocks */
        {
            WARN("poll timeout");
            continue;
        }

        auto write_buf = std::span{mMixData};
        mDevice->renderSamples(write_buf.data(),
            gsl::narrow_cast<u32>(write_buf.size()/frame_size), frame_step);
        while(!write_buf.empty() && !mKillNow.load(std::memory_order_acquire))
        {
            const auto wrote = write(mFd, write_buf.data(), write_buf.size());
            if(wrote < 0)
            {
                if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                    continue;
                const auto errstr = std::generic_category().message(errno);
                ERR("write failed: {}", errstr);
                mDevice->handleDisconnect("Failed writing playback samples: {}", errstr);
                break;
            }

            write_buf = write_buf.subspan(gsl::narrow_cast<usize>(wrote));
        }
    }
}


void OSSPlayback::open(std::string_view name)
{
    const auto *devname = DefaultPlayback.c_str();
    if(name.empty())
        name = GetDefaultName();
    else
    {
        if(PlaybackDevices.empty())
            ALCossListPopulate(PlaybackDevices, DSP_CAP_OUTPUT);

        const auto iter = std::ranges::find(PlaybackDevices, name, &DevMap::name);
        if(iter == PlaybackDevices.end())
            throw al::backend_exception{al::backend_error::NoDevice,
                "Device name \"{}\" not found", name};
        devname = iter->device_name.c_str();
    }

    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) */
    const auto fd = ::open(devname, O_WRONLY);
    if(fd == -1)
        throw al::backend_exception{al::backend_error::NoDevice, "Could not open {}: {}", devname,
            std::generic_category().message(errno)};

    if(mFd != -1)
        ::close(mFd);
    mFd = fd;

    mDeviceName = name;
}

auto OSSPlayback::reset() -> bool
{
    auto ossFormat = int{};
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
            [[fallthrough]];
        case DevFmtShort:
            ossFormat = AFMT_S16_NE;
            break;
    }

    auto numChannels = mDevice->channelsFromFmt();
    auto ossSpeed = mDevice->mSampleRate;
    auto frameSize = numChannels * mDevice->bytesFromFmt();
    /* Number of periods in the upper 16 bits. */
    auto numFragmentsLogSize = ((mDevice->mBufferSize + mDevice->mUpdateSize/2)
        / mDevice->mUpdateSize) << 16u;
    /* According to the OSS spec, 16 bytes is the minimum period size. */
    numFragmentsLogSize |= std::max(log2i(mDevice->mUpdateSize * frameSize), 4u);

    auto info = audio_buf_info{};
#define CHECKERR(func) if((func) < 0)                                         \
    throw al::backend_exception{al::backend_error::DeviceError, #func " failed: {}", \
        std::generic_category().message(errno)};

    /* NOLINTBEGIN(cppcoreguidelines-pro-type-vararg) */
    /* Don't fail if SETFRAGMENT fails. We can handle just about anything
     * that's reported back via GETOSPACE */
    ioctl(mFd, SNDCTL_DSP_SETFRAGMENT, &numFragmentsLogSize);
    CHECKERR(ioctl(mFd, SNDCTL_DSP_SETFMT, &ossFormat));
    CHECKERR(ioctl(mFd, SNDCTL_DSP_CHANNELS, &numChannels));
    CHECKERR(ioctl(mFd, SNDCTL_DSP_SPEED, &ossSpeed));
    CHECKERR(ioctl(mFd, SNDCTL_DSP_GETOSPACE, &info));
    /* NOLINTEND(cppcoreguidelines-pro-type-vararg) */
#undef CHECKERR

    if(mDevice->channelsFromFmt() != numChannels)
    {
        ERR("Failed to set {}, got {} channels instead", DevFmtChannelsString(mDevice->FmtChans),
            numChannels);
        return false;
    }

    if(!((ossFormat == AFMT_S8 && mDevice->FmtType == DevFmtByte) ||
         (ossFormat == AFMT_U8 && mDevice->FmtType == DevFmtUByte) ||
         (ossFormat == AFMT_S16_NE && mDevice->FmtType == DevFmtShort)))
    {
        ERR("Failed to set {} samples, got OSS format {:#x}", DevFmtTypeString(mDevice->FmtType),
            as_unsigned(ossFormat));
        return false;
    }

    mDevice->mSampleRate = ossSpeed;
    mDevice->mUpdateSize = gsl::narrow_cast<u32>(info.fragsize) / frameSize;
    mDevice->mBufferSize = gsl::narrow_cast<u32>(info.fragments) * mDevice->mUpdateSize;

    setDefaultChannelOrder();

    mMixData.resize(usize{mDevice->mUpdateSize} * mDevice->frameSizeFromFmt());

    return true;
}

void OSSPlayback::start()
{
    try {
        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{&OSSPlayback::mixerProc, this};
    }
    catch(std::exception& e) {
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start mixing thread: {}", e.what()};
    }
}

void OSSPlayback::stop()
{
    if(mKillNow.exchange(true, std::memory_order_acq_rel) || !mThread.joinable())
        return;
    mThread.join();

    if(ioctl(mFd, SNDCTL_DSP_RESET) != 0) /* NOLINT(cppcoreguidelines-pro-type-vararg) */
        ERR("Error resetting device: {}", std::generic_category().message(errno));
}


struct OSScapture final : public BackendBase {
    explicit OSScapture(gsl::not_null<DeviceBase*> device) noexcept : BackendBase{device} { }
    ~OSScapture() override;

    void recordProc() const;

    void open(std::string_view name) override;
    void start() override;
    void stop() override;
    void captureSamples(std::span<std::byte> outbuffer) override;
    auto availableSamples() -> usize override;

    int mFd{-1};

    RingBufferPtr<std::byte> mRing;

    std::atomic<bool> mKillNow{true};
    std::thread mThread;
};

OSScapture::~OSScapture()
{
    if(mFd != -1)
        close(mFd);
    mFd = -1;
}


void OSScapture::recordProc() const
{
    SetRTPriority();
    althrd_setname(GetRecordThreadName());

    auto const frame_size = usize{mDevice->frameSizeFromFmt()};
    while(!mKillNow.load(std::memory_order_acquire))
    {
        auto pollitem = pollfd{};
        pollitem.fd = mFd;
        pollitem.events = POLLIN;

        if(auto const pret = poll(&pollitem, 1, 1000); pret < 0)
        {
            if(errno == EINTR || errno == EAGAIN)
                continue;
            auto const errstr = std::generic_category().message(errno);
            ERR("poll failed: {}", errstr);
            mDevice->handleDisconnect("Failed to check capture samples: {}", errstr);
            break;
        }
        else if(pret == 0) /* NOLINT(*-else-after-return) 'pret' is local to the if/else blocks */
        {
            WARN("poll timeout");
            continue;
        }

        auto vec = mRing->getWriteVector();
        if(!vec[0].empty())
        {
            auto amt = read(mFd, vec[0].data(), vec[0].size());
            if(amt < 0)
            {
                auto const errstr = std::generic_category().message(errno);
                ERR("read failed: {}", errstr);
                mDevice->handleDisconnect("Failed reading capture samples: {}", errstr);
                break;
            }
            mRing->writeAdvance(gsl::narrow_cast<usize>(amt) / frame_size);
        }
    }
}


void OSScapture::open(std::string_view name)
{
    auto *devname = DefaultCapture.c_str();
    if(name.empty())
        name = GetDefaultName();
    else
    {
        if(CaptureDevices.empty())
            ALCossListPopulate(CaptureDevices, DSP_CAP_INPUT);

        auto iter = std::ranges::find(CaptureDevices, name, &DevMap::name);
        if(iter == CaptureDevices.end())
            throw al::backend_exception{al::backend_error::NoDevice,
                "Device name \"{}\" not found", name};
        devname = iter->device_name.c_str();
    }

    mFd = ::open(devname, O_RDONLY); /* NOLINT(cppcoreguidelines-pro-type-vararg) */
    if(mFd == -1)
        throw al::backend_exception{al::backend_error::NoDevice, "Could not open {}: {}", devname,
            std::generic_category().message(errno)};

    auto ossFormat = int{};
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
        throw al::backend_exception{al::backend_error::DeviceError,
            "{} capture samples not supported", DevFmtTypeString(mDevice->FmtType)};
    }

    auto numChannels = mDevice->channelsFromFmt();
    auto frameSize = numChannels * mDevice->bytesFromFmt();
    auto ossSpeed = mDevice->mSampleRate;
    /* according to the OSS spec, 16 bytes are the minimum */
    constexpr auto periods = 4u;
    const auto log2FragmentSize = std::max(log2i(mDevice->mBufferSize * frameSize / periods), 4u);
    auto numFragmentsLogSize = (periods << 16) | log2FragmentSize;

    auto info = audio_buf_info{};
#define CHECKERR(func) if((func) < 0) {                                       \
    throw al::backend_exception{al::backend_error::DeviceError, #func " failed: {}", \
        std::generic_category().message(errno)};                              \
}
    /* NOLINTBEGIN(cppcoreguidelines-pro-type-vararg) */
    CHECKERR(ioctl(mFd, SNDCTL_DSP_SETFRAGMENT, &numFragmentsLogSize));
    CHECKERR(ioctl(mFd, SNDCTL_DSP_SETFMT, &ossFormat));
    CHECKERR(ioctl(mFd, SNDCTL_DSP_CHANNELS, &numChannels));
    CHECKERR(ioctl(mFd, SNDCTL_DSP_SPEED, &ossSpeed));
    CHECKERR(ioctl(mFd, SNDCTL_DSP_GETISPACE, &info));
    /* NOLINTEND(cppcoreguidelines-pro-type-vararg) */
#undef CHECKERR

    if(mDevice->channelsFromFmt() != numChannels)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to set {}, got {} channels instead", DevFmtChannelsString(mDevice->FmtChans),
            numChannels};

    if(!((ossFormat == AFMT_S8 && mDevice->FmtType == DevFmtByte)
        || (ossFormat == AFMT_U8 && mDevice->FmtType == DevFmtUByte)
        || (ossFormat == AFMT_S16_NE && mDevice->FmtType == DevFmtShort)))
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to set {} samples, got OSS format {:#x}", DevFmtTypeString(mDevice->FmtType),
            as_unsigned(ossFormat)};

    mRing = RingBuffer<std::byte>::Create(mDevice->mBufferSize, frameSize, false);

    mDeviceName = name;
}

void OSScapture::start()
{
    try {
        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{&OSScapture::recordProc, this};
    }
    catch(std::exception& e) {
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start recording thread: {}", e.what()};
    }
}

void OSScapture::stop()
{
    if(mKillNow.exchange(true, std::memory_order_acq_rel) || !mThread.joinable())
        return;
    mThread.join();

    if(ioctl(mFd, SNDCTL_DSP_RESET) != 0) /* NOLINT(cppcoreguidelines-pro-type-vararg) */
        ERR("Error resetting device: {}", std::generic_category().message(errno));
}

void OSScapture::captureSamples(std::span<std::byte> outbuffer)
{ std::ignore = mRing->read(outbuffer); }

auto OSScapture::availableSamples() -> usize
{ return mRing->readSpace(); }

} // namespace

auto OSSBackendFactory::init() -> bool
{
    if(auto devopt = ConfigValueStr({}, "oss", "device"))
        DefaultPlayback = std::move(*devopt);
    if(auto capopt = ConfigValueStr({}, "oss", "capture"))
        DefaultCapture = std::move(*capopt);

    return true;
}

auto OSSBackendFactory::querySupport(BackendType const type) -> bool
{ return (type == BackendType::Playback || type == BackendType::Capture); }

auto OSSBackendFactory::enumerate(BackendType const type) -> std::vector<std::string>
{
    auto outnames = std::vector<std::string>{};
    auto add_device = [&outnames](const DevMap &entry) -> void
    {
        if(struct stat buf{}; stat(entry.device_name.c_str(), &buf) == 0)
            outnames.emplace_back(entry.name);
    };

    switch(type)
    {
    case BackendType::Playback:
        PlaybackDevices.clear();
        ALCossListPopulate(PlaybackDevices, DSP_CAP_OUTPUT);
        outnames.reserve(PlaybackDevices.size());
        std::ranges::for_each(PlaybackDevices, add_device);
        break;

    case BackendType::Capture:
        CaptureDevices.clear();
        ALCossListPopulate(CaptureDevices, DSP_CAP_INPUT);
        outnames.reserve(CaptureDevices.size());
        std::ranges::for_each(CaptureDevices, add_device);
        break;
    }

    return outnames;
}

auto OSSBackendFactory::createBackend(gsl::not_null<DeviceBase*> const device,
    BackendType const type) -> BackendPtr
{
    if(type == BackendType::Playback)
        return BackendPtr{new OSSPlayback{device}};
    if(type == BackendType::Capture)
        return BackendPtr{new OSScapture{device}};
    return nullptr;
}

auto OSSBackendFactory::getFactory() -> BackendFactory&
{
    static OSSBackendFactory factory{};
    return factory;
}
