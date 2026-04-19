/**
 * OpenAL cross platform audio library
 * Copyright (C) 2024 by authors.
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

#include "sdl3.h"

#include <concepts>
#include <cstddef>
#include <cstring>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "alformat.hpp"
#include "alnumeric.h"
#include "altypes.hpp"
#include "core/device.h"
#include "gsl/gsl"
#include "pragmadefs.h"
#include "ringbuffer.h"

DIAGNOSTIC_PUSH
std_pragma("GCC diagnostic ignored \"-Wold-style-cast\"")
#include "SDL3/SDL_audio.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_stdinc.h"

namespace {
constexpr auto DefaultPlaybackDeviceID = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
constexpr auto DefaultCaptureDeviceID = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
} /* namespace */
DIAGNOSTIC_POP;

#if HAVE_CXXMODULES
import logging;
#else
#include "core/logging.h"
#endif


namespace {

using namespace std::string_view_literals;

template<typename T>
using unique_sdl_ptr = std::unique_ptr<T, decltype([](gsl::owner<T*> ptr) { SDL_free(ptr); })>;


auto SDLCALL EventHandler(void* /*userptr*/, SDL_Event *const event) noexcept -> bool
{
    if(event->type == SDL_EVENT_AUDIO_DEVICE_ADDED)
    {
        auto &evt = event->adevice;
        auto const devtype = evt.recording ? alc::DeviceType::Capture : alc::DeviceType::Playback;

        auto const msg = al::format("Device ID added: {}", evt.which);
        alc::Event(alc::EventType::DeviceAdded, devtype, msg);
    }
    else if(event->type == SDL_EVENT_AUDIO_DEVICE_REMOVED)
    {
        auto &evt = event->adevice;
        auto const devtype = evt.recording ? alc::DeviceType::Capture : alc::DeviceType::Playback;

        auto const msg = al::format("Device ID removed: {}", evt.which);
        alc::Event(alc::EventType::DeviceRemoved, devtype, msg);
    }
    return true;
}

struct DeviceEntry {
    std::string mName;
    SDL_AudioDeviceID mPhysDeviceID{};
};

void EnumerateDevices(std::invocable<int*> auto&& get_devices, std::vector<DeviceEntry> &list)
    requires(std::same_as<std::invoke_result_t<decltype(get_devices), int*>, SDL_AudioDeviceID*>)
{
    auto numdevs = sys_int{};
    auto const devicelist = unique_sdl_ptr<SDL_AudioDeviceID>{get_devices(&numdevs.c_val)};
    if(!devicelist || numdevs < 0)
    {
        ERR("Failed to get playback devices: {}", SDL_GetError());
        return;
    }

    auto devids = std::span{devicelist.get(), numdevs.cast_to<usize>().c_val};
    auto newlist = std::vector<DeviceEntry>{};

    newlist.reserve(devids.size());
    std::ranges::transform(devids, std::back_inserter(newlist), [](SDL_AudioDeviceID const id)
    {
        auto *name = SDL_GetAudioDeviceName(id);
        if(!name) return DeviceEntry{};

        TRACE("Got device \"{}\", ID {}", name, id);
        return DeviceEntry{.mName = name, .mPhysDeviceID = id};
    });

    /* De-duplicate device names (append #2, #3, etc, as needed). */
    if(newlist.size() > 1)
    {
        for(auto const idx : std::views::iota(1_uz, newlist.size()))
        {
            auto &entry = newlist[idx];
            auto const namelist = newlist | std::views::take(idx)
                | std::views::transform(&DeviceEntry::mName);
            auto name_exists = [namelist](std::string_view const name) -> bool
            { return std::ranges::find(namelist, name) != namelist.end(); };

            if(name_exists(entry.mName))
            {
                auto count = 1u;
                auto newname = std::string{};
                do {
                    newname = al::format("{} #{}", entry.mName, ++count);
                } while(name_exists(newname));
                entry.mName = std::move(newname);
            }
        }
    }

    list.swap(newlist);
}

auto gPlaybackDevices = std::vector<DeviceEntry>{};
auto gCaptureDevices = std::vector<DeviceEntry>{};


[[nodiscard]] constexpr auto getDefaultDeviceName() noexcept -> std::string_view
{ return "Default Device"sv; }


struct Sdl3Playback final : BackendBase {
    explicit Sdl3Playback(gsl::not_null<DeviceBase*> const device) noexcept : BackendBase{device}
    { }
    ~Sdl3Playback() final;

    void audioCallback(SDL_AudioStream *stream, int additional_amount, int total_amount) noexcept;

    void open(std::string_view name) final;
    auto reset() -> bool final;
    void start() final;
    void stop() final;

    SDL_AudioDeviceID mDeviceID{0};
    SDL_AudioStream *mStream{nullptr};
    unsigned mNumChannels{0};
    unsigned mFrameSize{0};
    std::vector<std::byte> mBuffer;
};

Sdl3Playback::~Sdl3Playback()
{
    if(mStream)
        SDL_DestroyAudioStream(mStream);
    mStream = nullptr;
}

void Sdl3Playback::audioCallback(SDL_AudioStream *stream, int additional_amount, int total_amount)
    noexcept
{
    if(additional_amount < 0)
        additional_amount = total_amount;
    if(additional_amount <= 0)
        return;

    const auto ulen = gsl::narrow_cast<unsigned int>(additional_amount);
    if(ulen > mBuffer.size())
    {
        mBuffer.resize(ulen);
        std::ranges::fill(mBuffer, (mDevice->FmtType==DevFmtUByte) ? std::byte{0x80}:std::byte{});
    }

    mDevice->renderSamples(mBuffer.data(), ulen / mFrameSize, mNumChannels);
    SDL_PutAudioStreamData(stream, mBuffer.data(), additional_amount);
}

void Sdl3Playback::open(std::string_view name)
{
    const auto defaultDeviceName = getDefaultDeviceName();
    if(name.empty() || name == defaultDeviceName)
    {
        name = defaultDeviceName;
        mDeviceID = DefaultPlaybackDeviceID;
    }
    else
    {
        if(gPlaybackDevices.empty())
            EnumerateDevices(SDL_GetAudioPlaybackDevices, gPlaybackDevices);

        const auto iter = std::ranges::find(gPlaybackDevices, name, &DeviceEntry::mName);
        if(iter == gPlaybackDevices.end())
            throw al::backend_exception{al::backend_error::NoDevice, "No playback device named {}",
                name};

        mDeviceID = iter->mPhysDeviceID;
    }

    mStream = SDL_OpenAudioDeviceStream(mDeviceID, nullptr, nullptr, nullptr);
    if(!mStream)
        throw al::backend_exception{al::backend_error::NoDevice, "{}", SDL_GetError()};

    auto have = SDL_AudioSpec{};
    auto update_size = int{};
    if(SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(mStream), &have, &update_size))
    {
        auto devtype = mDevice->FmtType;
        switch(have.format)
        {
        case SDL_AUDIO_U8:  devtype = DevFmtUByte; break;
        case SDL_AUDIO_S8:  devtype = DevFmtByte;  break;
        case SDL_AUDIO_S16: devtype = DevFmtShort; break;
        case SDL_AUDIO_S32: devtype = DevFmtInt;   break;
        case SDL_AUDIO_F32: devtype = DevFmtFloat; break;
        default: break;
        }
        mDevice->FmtType = devtype;

        if(have.freq >= int{MinOutputRate} && have.freq <= int{MaxOutputRate})
            mDevice->mSampleRate = gsl::narrow_cast<unsigned>(have.freq);

        /* SDL guarantees these layouts for the given channel count. */
        if(have.channels == 8)
            mDevice->FmtChans = DevFmtX71;
        else if(have.channels == 7)
            mDevice->FmtChans = DevFmtX61;
        else if(have.channels == 6)
            mDevice->FmtChans = DevFmtX51;
        else if(have.channels == 4)
            mDevice->FmtChans = DevFmtQuad;
        else if(have.channels >= 2)
            mDevice->FmtChans = DevFmtStereo;
        else if(have.channels == 1)
            mDevice->FmtChans = DevFmtMono;
        mDevice->mAmbiOrder = 0;

        mNumChannels = gsl::narrow_cast<unsigned>(have.channels);
        mFrameSize = mDevice->bytesFromFmt() * mNumChannels;

        if(update_size >= 64)
        {
            /* We have to assume the total buffer size is just twice the update
             * size. SDL doesn't tell us the full end-to-end buffer latency.
             */
            mDevice->mUpdateSize = gsl::narrow_cast<unsigned>(update_size);
            mDevice->mBufferSize = mDevice->mUpdateSize*2u;
        }
        else
            ERR("Invalid update size from SDL stream: {}", update_size);
    }
    else
        ERR("Failed to get format from SDL stream: {}", SDL_GetError());

    mDeviceName = name;
}

auto Sdl3Playback::reset() -> bool
{
    static constexpr auto callback = [](void *ptr, SDL_AudioStream *stream, int additional_amount,
        int total_amount) noexcept
    {
        return static_cast<Sdl3Playback*>(ptr)->audioCallback(stream, additional_amount,
            total_amount);
    };

    if(mStream)
        SDL_DestroyAudioStream(mStream);
    mStream = nullptr;

    mBuffer.clear();
    mBuffer.shrink_to_fit();

    auto want = SDL_AudioSpec{};
    if(!SDL_GetAudioDeviceFormat(mDeviceID, &want, nullptr))
        ERR("Failed to get device format: {}", SDL_GetError());

    if(mDevice->mFlags.test(DeviceFlag::FrequencyRequest) || want.freq < int{MinOutputRate})
        want.freq = gsl::narrow_cast<int>(mDevice->mSampleRate);
    if(mDevice->mFlags.test(DeviceFlag::SampleTypeRequest)
        || !(want.format == SDL_AUDIO_U8 || want.format == SDL_AUDIO_S8
             || want.format == SDL_AUDIO_S16 || want.format == SDL_AUDIO_S32
             || want.format == SDL_AUDIO_F32))
    {
        switch(mDevice->FmtType)
        {
        case DevFmtUByte:  want.format = SDL_AUDIO_U8;  break;
        case DevFmtByte:   want.format = SDL_AUDIO_S8;  break;
        case DevFmtUShort: [[fallthrough]];
        case DevFmtShort:  want.format = SDL_AUDIO_S16; break;
        case DevFmtUInt:   [[fallthrough]];
        case DevFmtInt:    want.format = SDL_AUDIO_S32; break;
        case DevFmtFloat:  want.format = SDL_AUDIO_F32; break;
        }
    }
    if(mDevice->mFlags.test(DeviceFlag::ChannelsRequest) || want.channels < 1)
        want.channels = al::saturate_cast<int>(mDevice->channelsFromFmt());

    mStream = SDL_OpenAudioDeviceStream(mDeviceID, &want, callback, this);
    if(!mStream)
    {
        /* If creating the stream failed, try again without a specific format. */
        mStream = SDL_OpenAudioDeviceStream(mDeviceID, nullptr, callback, this);
        if(!mStream)
            throw al::backend_exception{al::backend_error::DeviceError,
                "Failed to recreate stream: {}", SDL_GetError()};
    }

    auto update_size = int{};
    auto have = SDL_AudioSpec{};
    SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(mStream), &have, &update_size);

    have = SDL_AudioSpec{};
    if(!SDL_GetAudioStreamFormat(mStream, &have, nullptr))
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to get stream format: {}", SDL_GetError()};

    if(!mDevice->mFlags.test(DeviceFlag::ChannelsRequest)
        || (std::cmp_not_equal(have.channels, mDevice->channelsFromFmt())
            && !(mDevice->FmtChans == DevFmtStereo && have.channels >= 2)))
    {
        /* SDL guarantees these layouts for the given channel count. */
        if(have.channels == 8)
            mDevice->FmtChans = DevFmtX71;
        else if(have.channels == 7)
            mDevice->FmtChans = DevFmtX61;
        else if(have.channels == 6)
            mDevice->FmtChans = DevFmtX51;
        else if(have.channels == 4)
            mDevice->FmtChans = DevFmtQuad;
        else if(have.channels >= 2)
            mDevice->FmtChans = DevFmtStereo;
        else if(have.channels == 1)
            mDevice->FmtChans = DevFmtMono;
        else
            throw al::backend_exception{al::backend_error::DeviceError,
                "Unhandled SDL channel count: {}", have.channels};
        mDevice->mAmbiOrder = 0;
    }
    mNumChannels = gsl::narrow_cast<unsigned>(have.channels);

    switch(have.format)
    {
    case SDL_AUDIO_U8:  mDevice->FmtType = DevFmtUByte; break;
    case SDL_AUDIO_S8:  mDevice->FmtType = DevFmtByte;  break;
    case SDL_AUDIO_S16: mDevice->FmtType = DevFmtShort; break;
    case SDL_AUDIO_S32: mDevice->FmtType = DevFmtInt;   break;
    case SDL_AUDIO_F32: mDevice->FmtType = DevFmtFloat; break;
    default:
        throw al::backend_exception{al::backend_error::DeviceError,
            "Unhandled SDL format: {:#04x}", al::to_underlying(have.format)};
    }

    mFrameSize = mDevice->bytesFromFmt() * mNumChannels;

    if(have.freq < int{MinOutputRate})
        throw al::backend_exception{al::backend_error::DeviceError,
            "Unhandled SDL sample rate: {}", have.freq};
    mDevice->mSampleRate = gsl::narrow_cast<unsigned>(have.freq);

    if(update_size >= 64)
    {
        mDevice->mUpdateSize = gsl::narrow_cast<unsigned>(update_size);
        mDevice->mBufferSize = mDevice->mUpdateSize * 2u;

        mBuffer.resize(std::size_t{mDevice->mUpdateSize} * mFrameSize);
        std::ranges::fill(mBuffer, mDevice->FmtType==DevFmtUByte ? std::byte{0x80} : std::byte{});
    }
    else
        ERR("Invalid update size from SDL stream: {}", update_size);

    setDefaultWFXChannelOrder();

    return true;
}

void Sdl3Playback::start()
{ SDL_ResumeAudioStreamDevice(mStream); }

void Sdl3Playback::stop()
{ SDL_PauseAudioStreamDevice(mStream); }


struct Sdl3Capture final : BackendBase {
    explicit Sdl3Capture(gsl::not_null<DeviceBase*> const device) : BackendBase{device} { }
    ~Sdl3Capture() final;

    void audioCallback(SDL_AudioStream *stream, int additional_amount, int total_amount) noexcept;

    void open(std::string_view name) override;
    void start() override;
    void stop() override;
    void captureSamples(std::span<std::byte> outbuffer) override;
    auto availableSamples() -> std::size_t override;

    SDL_AudioDeviceID mDeviceID{0};
    SDL_AudioStream *mStream{nullptr};
    std::vector<std::byte> mBuffer;
    RingBufferPtr<std::byte> mRing;
};

Sdl3Capture::~Sdl3Capture()
{
    if(mStream)
        SDL_DestroyAudioStream(mStream);
    mStream = nullptr;
}


auto Sdl3Capture::audioCallback(SDL_AudioStream *stream, int /*additional_amount*/,
    int /*total_amount*/) noexcept -> void
try {
    auto const avail = sys_int{SDL_GetAudioStreamAvailable(stream)};
    if(auto const uavail = avail.cast_to<sys_uint>(); uavail != mBuffer.size())
        mBuffer.resize(uavail.c_val);

    auto const got = sys_int{SDL_GetAudioStreamData(stream, mBuffer.data(), avail.c_val)};
    if(got != avail)
        mBuffer.resize(got.cast_to<sys_uint>().c_val);

    std::ignore = mRing->write(mBuffer);
}
catch(std::exception &e) {
    ERR("Caught exception in capture callback: {}", e.what());
}


auto Sdl3Capture::open(std::string_view name) -> void
{
    const auto defaultDeviceName = getDefaultDeviceName();
    if(name.empty() || name == defaultDeviceName)
    {
        name = defaultDeviceName;
        mDeviceID = DefaultCaptureDeviceID;
    }
    else
    {
        if(gCaptureDevices.empty())
            EnumerateDevices(SDL_GetAudioRecordingDevices, gCaptureDevices);

        const auto iter = std::ranges::find(gCaptureDevices, name, &DeviceEntry::mName);
        if(iter == gCaptureDevices.end())
            throw al::backend_exception{al::backend_error::NoDevice, "No capture device named {}",
                name};

        mDeviceID = iter->mPhysDeviceID;
    }

    auto want = SDL_AudioSpec{};
    want.freq = gsl::narrow<int>(mDevice->mSampleRate);
    switch(mDevice->FmtType)
    {
    case DevFmtUByte:  want.format = SDL_AUDIO_U8;  break;
    case DevFmtByte:   want.format = SDL_AUDIO_S8;  break;
    case DevFmtShort:  want.format = SDL_AUDIO_S16; break;
    case DevFmtInt:    want.format = SDL_AUDIO_S32; break;
    case DevFmtFloat:  want.format = SDL_AUDIO_F32; break;
    case DevFmtUShort:
    case DevFmtUInt:
        throw al::backend_exception{al::backend_error::DeviceError,
            "Format not supported for capture: {}", DevFmtTypeString(mDevice->FmtType)};
    }
    want.channels = gsl::narrow<int>(mDevice->channelsFromFmt());

    static constexpr auto callback = [](void *ptr, SDL_AudioStream *stream, int additional_amount,
        int total_amount) noexcept
    {
        return static_cast<Sdl3Capture*>(ptr)->audioCallback(stream, additional_amount,
            total_amount);
    };
    mStream = SDL_OpenAudioDeviceStream(mDeviceID, &want, callback, this);
    if(not mStream)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to create capture stream: {}", SDL_GetError()};

    setDefaultWFXChannelOrder();

    /* Ensure a minimum ringbuffer size of 100ms. */
    mRing = RingBuffer<std::byte>::Create(std::max(mDevice->mBufferSize, mDevice->mSampleRate/10u),
        mDevice->frameSizeFromFmt(), false);

    mDeviceName = name;
}

void Sdl3Capture::start()
{
    if(not SDL_ResumeAudioStreamDevice(mStream))
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start capture device: {}", SDL_GetError()};
}

void Sdl3Capture::stop()
{
    if(not SDL_PauseAudioStreamDevice(mStream))
        ERR("Failed to stop capture device: {}", SDL_GetError());
}

auto Sdl3Capture::availableSamples() -> std::size_t
{ return mRing->readSpace(); }

void Sdl3Capture::captureSamples(std::span<std::byte> const outbuffer)
{ std::ignore = mRing->read(outbuffer); }

} // namespace

auto SDL3BackendFactory::getFactory() -> BackendFactory&
{
    static auto factory = SDL3BackendFactory{};
    return factory;
}

auto SDL3BackendFactory::init() -> bool
{
    if(!SDL_InitSubSystem(SDL_INIT_AUDIO))
        return false;
    TRACE("Current SDL3 audio driver: \"{}\"", SDL_GetCurrentAudioDriver());

    if(not SDL_AddEventWatch(&EventHandler, nullptr))
        ERR("Failed to register SDL event handler: {}", SDL_GetError());

    return true;
}

auto SDL3BackendFactory::querySupport(BackendType const type) -> bool
{ return type == BackendType::Playback or type == BackendType::Capture; }

auto SDL3BackendFactory::queryEventSupport(alc::EventType const event, BackendType /*backend*/)
    -> alc::EventSupport
{
    switch(event)
    {
    case alc::EventType::DeviceAdded:
    case alc::EventType::DeviceRemoved:
        return alc::EventSupport::FullSupport;

    /* SDL3 doesn't report when the default device changes. This isn't too big
     * of a deal since we always report a separate "Default Device" as the
     * default, and SDL will automatically move between devices when using it.
     */
    case alc::EventType::DefaultDeviceChanged:
        break;
    }
    return alc::EventSupport::NoSupport;
}

auto SDL3BackendFactory::enumerate(BackendType const type) -> std::vector<std::string>
{
    auto outnames = std::vector<std::string>{};

    if(type == BackendType::Playback)
    {
        EnumerateDevices(SDL_GetAudioPlaybackDevices, gPlaybackDevices);
        outnames.reserve(gPlaybackDevices.size()+1);
        outnames.emplace_back(getDefaultDeviceName());
        std::ranges::transform(gPlaybackDevices, std::back_inserter(outnames),
            &DeviceEntry::mName);
    }
    else if(type == BackendType::Capture)
    {
        EnumerateDevices(SDL_GetAudioRecordingDevices, gCaptureDevices);
        outnames.reserve(gCaptureDevices.size()+1);
        outnames.emplace_back(getDefaultDeviceName());
        std::ranges::transform(gCaptureDevices, std::back_inserter(outnames),
            &DeviceEntry::mName);
    }

    return outnames;
}

auto SDL3BackendFactory::createBackend(gsl::not_null<DeviceBase*> const device,
    BackendType const type) -> BackendPtr
{
    if(type == BackendType::Playback)
        return BackendPtr{new Sdl3Playback{device}};
    if(type == BackendType::Capture)
        return BackendPtr{new Sdl3Capture{device}};
    return nullptr;
}
