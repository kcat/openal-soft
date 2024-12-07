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
#include <string>
#include <string_view>

#include "core/device.h"
#include "core/logging.h"

_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wold-style-cast\"")
#include "SDL3/SDL_audio.h"
#include "SDL3/SDL_init.h"
_Pragma("GCC diagnostic pop")


namespace {

using namespace std::string_view_literals;

_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wold-style-cast\"")
constexpr auto DefaultPlaybackDeviceID = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
_Pragma("GCC diagnostic pop")

[[nodiscard]] constexpr auto getDefaultDeviceName() noexcept -> std::string_view
{ return "Default Device"sv; }

struct Sdl3Backend final : public BackendBase {
    Sdl3Backend(DeviceBase *device) noexcept : BackendBase{device} { }
    ~Sdl3Backend() override;

    void audioCallback(SDL_AudioStream *stream, int additional_amount, int total_amount) noexcept;

    void open(std::string_view name) override;
    bool reset() override;
    void start() override;
    void stop() override;

    std::string mSDLName;
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
        mBuffer.resize(ulen);

    mDevice->renderSamples(mBuffer.data(), ulen / mFrameSize, mNumChannels);
    SDL_PutAudioStreamData(stream, mBuffer.data(), additional_amount);
}

void Sdl3Backend::open(std::string_view name)
{
    const auto defaultDeviceName = getDefaultDeviceName();
    if(name.empty() || name == defaultDeviceName)
    {
        name = defaultDeviceName;
        mSDLName.clear();

        mStream = SDL_OpenAudioDeviceStream(DefaultPlaybackDeviceID, nullptr, nullptr, nullptr);
        if(!mStream)
            throw al::backend_exception{al::backend_error::NoDevice, "{}", SDL_GetError()};
    }
    else
        throw al::backend_exception{al::backend_error::NoDevice, "No device named {}", name};

    auto have = SDL_AudioSpec{};
    auto update_size = int{};
    if(SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(mStream), &have, &update_size))
    {
        auto devtype = mDevice->FmtType;
        switch(have.format)
        {
        case SDL_AUDIO_U8:  devtype = DevFmtUByte;  break;
        case SDL_AUDIO_S8:  devtype = DevFmtByte;   break;
        case SDL_AUDIO_S16: devtype = DevFmtShort;  break;
        case SDL_AUDIO_S32: devtype = DevFmtInt;    break;
        case SDL_AUDIO_F32: devtype = DevFmtFloat;  break;
        default: break;
        }
        mDevice->FmtType = devtype;

        if(have.freq >= int{MinOutputRate} && have.freq <= int{MaxOutputRate})
            mDevice->Frequency = static_cast<uint>(have.freq);

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
        mFrameSize = BytesFromDevFmt(devtype) * mNumChannels;

        if(update_size >= 64)
        {
            /* We have to assume the total buffer size is just twice the update
             * size. SDL doesn't tell us the full end-to-end buffer latency.
             */
            mDevice->UpdateSize = static_cast<uint>(update_size);
            mDevice->BufferSize = mDevice->UpdateSize*2u;
        }
        else
            ERR("Invalid update size from SDL stream: {}", update_size);
    }
    else
        ERR("Failed to get format from SDL stream: {}", SDL_GetError());

    mDeviceName = name;
}

bool Sdl3Backend::reset()
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

    auto want = SDL_AudioSpec{};
    want.freq = static_cast<int>(mDevice->Frequency);
    switch(mDevice->FmtType)
    {
    case DevFmtUByte: want.format = SDL_AUDIO_U8; break;
    case DevFmtByte: want.format = SDL_AUDIO_S8; break;
    case DevFmtUShort: [[fallthrough]];
    case DevFmtShort: want.format = SDL_AUDIO_S16; break;
    case DevFmtUInt: [[fallthrough]];
    case DevFmtInt: want.format = SDL_AUDIO_S32; break;
    case DevFmtFloat: want.format = SDL_AUDIO_F32; break;
    }
    want.channels = static_cast<int>(std::min<uint>(mDevice->channelsFromFmt(),
        std::numeric_limits<int>::max()));

    /* Only request a format if any of these properties are requested.
     * Alternatively, query the desired device's current/default format, and
     * only replace requested properties (falling back to the default if
     * opening fails).
     */
    auto *wantptr = (mDevice->Flags.test(FrequencyRequest)
        || mDevice->Flags.test(ChannelsRequest) || mDevice->Flags.test(SampleTypeRequest))
        ? &want : nullptr;

    if(mSDLName.empty())
        mStream = SDL_OpenAudioDeviceStream(DefaultPlaybackDeviceID, wantptr, callback, this);
    if(!mStream)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to recreate stream: {}", SDL_GetError()};

    auto have = SDL_AudioSpec{};
    auto update_size = int{};
    if(!SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(mStream), &have, &update_size))
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
    case SDL_AUDIO_U8:  mDevice->FmtType = DevFmtUByte;  break;
    case SDL_AUDIO_S8:  mDevice->FmtType = DevFmtByte;   break;
    case SDL_AUDIO_S16: mDevice->FmtType = DevFmtShort;  break;
    case SDL_AUDIO_S32: mDevice->FmtType = DevFmtInt;    break;
    case SDL_AUDIO_F32: mDevice->FmtType = DevFmtFloat;  break;
    default:
        throw al::backend_exception{al::backend_error::DeviceError,
            "Unhandled SDL format: 0x{:04x}", al::to_underlying(have.format)};
    }

    mFrameSize = BytesFromDevFmt(mDevice->FmtType) * mNumChannels;

    if(have.freq < int{MinOutputRate})
        throw al::backend_exception{al::backend_error::DeviceError,
            "Unhandled SDL sample rate: {}", have.freq};

    mDevice->Frequency = static_cast<uint>(have.freq);

    if(update_size > 0)
    {
        mDevice->UpdateSize = static_cast<uint>(update_size);
        mDevice->BufferSize = mDevice->UpdateSize*2u;
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

BackendFactory &SDL3BackendFactory::getFactory()
{
    static SDL3BackendFactory factory{};
    return factory;
}

bool SDL3BackendFactory::init()
{ return SDL_InitSubSystem(SDL_INIT_AUDIO); }

bool SDL3BackendFactory::querySupport(BackendType type)
{ return type == BackendType::Playback; }

auto SDL3BackendFactory::enumerate(BackendType type) -> std::vector<std::string>
{
    auto outnames = std::vector<std::string>{};

    if(type != BackendType::Playback)
        return outnames;

    outnames.emplace_back(getDefaultDeviceName());
    return outnames;
}

BackendPtr SDL3BackendFactory::createBackend(DeviceBase *device, BackendType type)
{
    if(type == BackendType::Playback)
        return BackendPtr{new Sdl3Backend{device}};
    return nullptr;
}
