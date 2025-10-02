/**
 * OpenAL cross platform audio library
 * Copyright (C) 2018 by authors.
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

#include "sdl2.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "alnumeric.h"
#include "core/device.h"
#include "gsl/gsl"
#include "pragmadefs.h"

DIAGNOSTIC_PUSH
std_pragma("GCC diagnostic ignored \"-Wold-style-cast\"")
#include "SDL.h"
DIAGNOSTIC_POP


namespace {

using namespace std::string_view_literals;

[[nodiscard]] constexpr auto getDefaultDeviceName() noexcept -> std::string_view
{ return "Default Device"sv; }

struct Sdl2Backend final : public BackendBase {
    explicit Sdl2Backend(gsl::not_null<DeviceBase*> device) noexcept : BackendBase{device} { }
    ~Sdl2Backend() override;

    void audioCallback(Uint8 *stream, int len) noexcept;

    void open(std::string_view name) override;
    bool reset() override;
    void start() override;
    void stop() override;

    std::string mSDLName;
    SDL_AudioDeviceID mDeviceID{0u};
    uint mFrameSize{0};
};

Sdl2Backend::~Sdl2Backend()
{
    if(mDeviceID)
        SDL_CloseAudioDevice(mDeviceID);
    mDeviceID = 0;
}

void Sdl2Backend::audioCallback(Uint8 *stream, int len) noexcept
{
    const auto ulen = gsl::narrow_cast<unsigned int>(len);
    mDevice->renderSamples(stream, ulen / mFrameSize, mDevice->channelsFromFmt());
}

void Sdl2Backend::open(std::string_view name)
{
    SDL_AudioSpec want{}, have{};

    want.freq = static_cast<int>(mDevice->mSampleRate);
    switch(mDevice->FmtType)
    {
    case DevFmtUByte: want.format = AUDIO_U8; break;
    case DevFmtByte: want.format = AUDIO_S8; break;
    case DevFmtUShort: want.format = AUDIO_U16SYS; break;
    case DevFmtShort: want.format = AUDIO_S16SYS; break;
    case DevFmtUInt: [[fallthrough]];
    case DevFmtInt: want.format = AUDIO_S32SYS; break;
    case DevFmtFloat: want.format = AUDIO_F32; break;
    }
    want.channels = al::saturate_cast<Uint8>(mDevice->channelsFromFmt());
    want.samples = static_cast<Uint16>(std::min(mDevice->mUpdateSize, 8192u));
    want.callback = [](void *ptr, Uint8 *stream, int len) noexcept
    { return static_cast<Sdl2Backend*>(ptr)->audioCallback(stream, len); };
    want.userdata = this;

    /* Passing nullptr to SDL_OpenAudioDevice opens a default, which isn't
     * necessarily the first in the list.
     */
    const auto defaultDeviceName = getDefaultDeviceName();
    if(name.empty() || name == defaultDeviceName)
    {
        name = defaultDeviceName;
        mSDLName.clear();
        mDeviceID = SDL_OpenAudioDevice(nullptr, SDL_FALSE, &want, &have,
            SDL_AUDIO_ALLOW_ANY_CHANGE);
    }
    else
    {
        mSDLName = name;
        mDeviceID = SDL_OpenAudioDevice(mSDLName.c_str(), SDL_FALSE, &want, &have,
            SDL_AUDIO_ALLOW_ANY_CHANGE);
    }
    if(!mDeviceID)
        throw al::backend_exception{al::backend_error::NoDevice, "{}", SDL_GetError()};

    DevFmtType devtype{};
    switch(have.format)
    {
    case AUDIO_U8:     devtype = DevFmtUByte;  break;
    case AUDIO_S8:     devtype = DevFmtByte;   break;
    case AUDIO_U16SYS: devtype = DevFmtUShort; break;
    case AUDIO_S16SYS: devtype = DevFmtShort;  break;
    case AUDIO_S32SYS: devtype = DevFmtInt;    break;
    case AUDIO_F32SYS: devtype = DevFmtFloat;  break;
    default:
        throw al::backend_exception{al::backend_error::DeviceError,
            "Unhandled SDL format: {:#04x}", have.format};
    }

    mFrameSize = BytesFromDevFmt(devtype) * have.channels;

    mDeviceName = name;
}

bool Sdl2Backend::reset()
{
    if(mDeviceID)
        SDL_CloseAudioDevice(mDeviceID);
    mDeviceID = 0;

    auto want = SDL_AudioSpec{};
    want.freq = static_cast<int>(mDevice->mSampleRate);
    switch(mDevice->FmtType)
    {
    case DevFmtUByte: want.format = AUDIO_U8; break;
    case DevFmtByte: want.format = AUDIO_S8; break;
    case DevFmtUShort: want.format = AUDIO_U16SYS; break;
    case DevFmtShort: want.format = AUDIO_S16SYS; break;
    case DevFmtUInt: [[fallthrough]];
    case DevFmtInt: want.format = AUDIO_S32SYS; break;
    case DevFmtFloat: want.format = AUDIO_F32; break;
    }
    want.channels = al::saturate_cast<Uint8>(mDevice->channelsFromFmt());
    want.samples = static_cast<Uint16>(std::min(mDevice->mUpdateSize, 8192u));
    want.callback = [](void *ptr, Uint8 *stream, int len) noexcept
    { return static_cast<Sdl2Backend*>(ptr)->audioCallback(stream, len); };
    want.userdata = this;

    auto have = SDL_AudioSpec{};
    if(mSDLName.empty())
    {
        mDeviceID = SDL_OpenAudioDevice(nullptr, SDL_FALSE, &want, &have,
            SDL_AUDIO_ALLOW_ANY_CHANGE);
    }
    else
    {
        mDeviceID = SDL_OpenAudioDevice(mSDLName.c_str(), SDL_FALSE, &want, &have,
            SDL_AUDIO_ALLOW_ANY_CHANGE);
    }
    if(!mDeviceID)
        throw al::backend_exception{al::backend_error::NoDevice, "{}", SDL_GetError()};

    if(have.channels != mDevice->channelsFromFmt())
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
                "Unhandled SDL channel count: {}", int{have.channels}};
        mDevice->mAmbiOrder = 0;
    }

    switch(have.format)
    {
    case AUDIO_U8:     mDevice->FmtType = DevFmtUByte;  break;
    case AUDIO_S8:     mDevice->FmtType = DevFmtByte;   break;
    case AUDIO_U16SYS: mDevice->FmtType = DevFmtUShort; break;
    case AUDIO_S16SYS: mDevice->FmtType = DevFmtShort;  break;
    case AUDIO_S32SYS: mDevice->FmtType = DevFmtInt;    break;
    case AUDIO_F32SYS: mDevice->FmtType = DevFmtFloat;  break;
    default:
        throw al::backend_exception{al::backend_error::DeviceError,
            "Unhandled SDL format: {:#04x}", have.format};
    }

    mFrameSize = BytesFromDevFmt(mDevice->FmtType) * have.channels;

    if(have.freq < int{MinOutputRate})
        throw al::backend_exception{al::backend_error::DeviceError,
            "Unhandled SDL sample rate: {}", have.freq};

    mDevice->mSampleRate = static_cast<uint>(have.freq);
    mDevice->mUpdateSize = have.samples;
    mDevice->mBufferSize = std::max(have.size/mFrameSize, mDevice->mUpdateSize*2u);

    setDefaultWFXChannelOrder();

    return true;
}

void Sdl2Backend::start()
{ SDL_PauseAudioDevice(mDeviceID, 0); }

void Sdl2Backend::stop()
{ SDL_PauseAudioDevice(mDeviceID, 1); }

} // namespace

BackendFactory &SDL2BackendFactory::getFactory()
{
    static SDL2BackendFactory factory{};
    return factory;
}

bool SDL2BackendFactory::init()
{ return (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0); }

bool SDL2BackendFactory::querySupport(BackendType type)
{ return type == BackendType::Playback; }

auto SDL2BackendFactory::enumerate(BackendType type) -> std::vector<std::string>
{
    std::vector<std::string> outnames;

    if(type != BackendType::Playback)
        return outnames;

    int num_devices{SDL_GetNumAudioDevices(SDL_FALSE)};
    if(num_devices <= 0)
        return outnames;

    outnames.reserve(gsl::narrow_cast<unsigned int>(num_devices)+1_uz);
    outnames.emplace_back(getDefaultDeviceName());
    for(int i{0};i < num_devices;++i)
    {
        if(const char *name = SDL_GetAudioDeviceName(i, SDL_FALSE))
            outnames.emplace_back(name);
        else
            outnames.emplace_back("Unknown Device Name #"+std::to_string(i));
    }
    return outnames;
}

auto SDL2BackendFactory::createBackend(gsl::not_null<DeviceBase*> device, BackendType type)
    -> BackendPtr
{
    if(type == BackendType::Playback)
        return BackendPtr{new Sdl2Backend{device}};
    return nullptr;
}
