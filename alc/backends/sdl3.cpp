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

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>

#include "almalloc.h"
#include "core/device.h"
#include "core/logging.h"

_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wold-style-cast\"")
#include "SDL3/SDL_audio.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_stdinc.h"
_Pragma("GCC diagnostic pop")


namespace {

using namespace std::string_view_literals;

_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wold-style-cast\"")
constexpr auto DefaultPlaybackDeviceID = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
_Pragma("GCC diagnostic pop")


template<typename T>
struct SdlDeleter {
    /* NOLINTNEXTLINE(cppcoreguidelines-no-malloc) */
    void operator()(gsl::owner<T*> ptr) const { SDL_free(ptr); }
};
template<typename T>
using unique_sdl_ptr = std::unique_ptr<T,SdlDeleter<T>>;


struct DeviceEntry {
    std::string mName;
    SDL_AudioDeviceID mPhysDeviceID{};
};

std::vector<DeviceEntry> gPlaybackDevices;

void EnumeratePlaybackDevices()
{
    auto numdevs = int{};
    auto devicelist = unique_sdl_ptr<SDL_AudioDeviceID>{SDL_GetAudioPlaybackDevices(&numdevs)};
    if(!devicelist || numdevs < 0)
    {
        ERR("Failed to get playback devices: {}", SDL_GetError());
        return;
    }

    auto devids = al::span{devicelist.get(), static_cast<uint>(numdevs)};
    auto newlist = std::vector<DeviceEntry>{};

    newlist.reserve(devids.size());
    std::transform(devids.begin(), devids.end(), std::back_inserter(newlist),
        [](SDL_AudioDeviceID id)
        {
            auto *name = SDL_GetAudioDeviceName(id);
            if(!name) return DeviceEntry{};
            TRACE("Got device \"{}\", ID {}", name, id);
            return DeviceEntry{name, id};
        });

    gPlaybackDevices.swap(newlist);
}

[[nodiscard]] constexpr auto getDefaultDeviceName() noexcept -> std::string_view
{ return "Default Device"sv; }


struct Sdl3Backend final : public BackendBase {
    explicit Sdl3Backend(DeviceBase *device) noexcept : BackendBase{device} { }
    ~Sdl3Backend() final;

    void audioCallback(SDL_AudioStream *stream, int additional_amount, int total_amount) noexcept;

    void open(std::string_view name) final;
    auto reset() -> bool final;
    void start() final;
    void stop() final;

    SDL_AudioDeviceID mDeviceID{0};
    SDL_AudioStream *mStream{nullptr};
    uint mNumChannels{0};
    uint mFrameSize{0};
    std::vector<std::byte> mBuffer;
};

Sdl3Backend::~Sdl3Backend()
{
    if(mStream)
        SDL_DestroyAudioStream(mStream);
    mStream = nullptr;
}

void Sdl3Backend::audioCallback(SDL_AudioStream *stream, int additional_amount, int total_amount)
    noexcept
{
    if(additional_amount < 0)
        additional_amount = total_amount;
    if(additional_amount <= 0)
        return;

    const auto ulen = static_cast<unsigned int>(additional_amount);
    assert((ulen % mFrameSize) == 0);

    if(ulen > mBuffer.size())
    {
        mBuffer.resize(ulen);
        std::fill(mBuffer.begin(), mBuffer.end(), (mDevice->FmtType == DevFmtUByte)
            ? std::byte{0x80} : std::byte{});
    }

    mDevice->renderSamples(mBuffer.data(), ulen / mFrameSize, mNumChannels);
    SDL_PutAudioStreamData(stream, mBuffer.data(), additional_amount);
}

void Sdl3Backend::open(std::string_view name)
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
            EnumeratePlaybackDevices();

        const auto iter = std::find_if(gPlaybackDevices.cbegin(), gPlaybackDevices.cend(),
            [name](const DeviceEntry &entry) { return name == entry.mName; });
        if(iter == gPlaybackDevices.cend())
            throw al::backend_exception{al::backend_error::NoDevice, "No device named {}", name};

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
            mDevice->mSampleRate = static_cast<uint>(have.freq);

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

        mNumChannels = static_cast<uint>(have.channels);
        mFrameSize = mDevice->bytesFromFmt() * mNumChannels;

        if(update_size >= 64)
        {
            /* We have to assume the total buffer size is just twice the update
             * size. SDL doesn't tell us the full end-to-end buffer latency.
             */
            mDevice->mUpdateSize = static_cast<uint>(update_size);
            mDevice->mBufferSize = mDevice->mUpdateSize*2u;
        }
        else
            ERR("Invalid update size from SDL stream: {}", update_size);
    }
    else
        ERR("Failed to get format from SDL stream: {}", SDL_GetError());

    mDeviceName = name;
}

auto Sdl3Backend::reset() -> bool
{
    static constexpr auto callback = [](void *ptr, SDL_AudioStream *stream, int additional_amount,
        int total_amount) noexcept
    {
        return static_cast<Sdl3Backend*>(ptr)->audioCallback(stream, additional_amount,
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

    if(mDevice->Flags.test(FrequencyRequest) || want.freq < int{MinOutputRate})
        want.freq = static_cast<int>(mDevice->mSampleRate);
    if(mDevice->Flags.test(SampleTypeRequest)
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
    if(mDevice->Flags.test(ChannelsRequest) || want.channels < 1)
        want.channels = static_cast<int>(std::min<uint>(mDevice->channelsFromFmt(),
            std::numeric_limits<int>::max()));

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

    if(!mDevice->Flags.test(ChannelsRequest)
        || (static_cast<uint>(have.channels) != mDevice->channelsFromFmt()
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
    mNumChannels = static_cast<uint>(have.channels);

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
    mDevice->mSampleRate = static_cast<uint>(have.freq);

    if(update_size >= 64)
    {
        mDevice->mUpdateSize = static_cast<uint>(update_size);
        mDevice->mBufferSize = mDevice->mUpdateSize*2u;

        mBuffer.resize(size_t{mDevice->mUpdateSize} * mFrameSize);
        std::fill(mBuffer.begin(), mBuffer.end(), (mDevice->FmtType == DevFmtUByte)
            ? std::byte{0x80} : std::byte{});
    }
    else
        ERR("Invalid update size from SDL stream: {}", update_size);

    setDefaultWFXChannelOrder();

    return true;
}

void Sdl3Backend::start()
{ SDL_ResumeAudioStreamDevice(mStream); }

void Sdl3Backend::stop()
{ SDL_PauseAudioStreamDevice(mStream); }

} // namespace

auto SDL3BackendFactory::getFactory() -> BackendFactory&
{
    static SDL3BackendFactory factory{};
    return factory;
}

auto SDL3BackendFactory::init() -> bool
{
    if(!SDL_InitSubSystem(SDL_INIT_AUDIO))
        return false;
    TRACE("Current SDL3 audio driver: \"{}\"", SDL_GetCurrentAudioDriver());
    return true;
}

auto SDL3BackendFactory::querySupport(BackendType type) -> bool
{ return type == BackendType::Playback; }

auto SDL3BackendFactory::enumerate(BackendType type) -> std::vector<std::string>
{
    auto outnames = std::vector<std::string>{};

    if(type != BackendType::Playback)
        return outnames;

    EnumeratePlaybackDevices();
    outnames.reserve(gPlaybackDevices.size()+1);
    outnames.emplace_back(getDefaultDeviceName());
    std::transform(gPlaybackDevices.begin(), gPlaybackDevices.end(), std::back_inserter(outnames),
        std::mem_fn(&DeviceEntry::mName));

    return outnames;
}

auto SDL3BackendFactory::createBackend(DeviceBase *device, BackendType type) -> BackendPtr
{
    if(type == BackendType::Playback)
        return BackendPtr{new Sdl3Backend{device}};
    return nullptr;
}
