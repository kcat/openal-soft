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

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "almalloc.h"
#include "alnumeric.h"
#include "core/device.h"
#include "core/logging.h"

_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wold-style-cast\"")
#include "SDL.h"
_Pragma("GCC diagnostic pop")


namespace {

#ifdef _WIN32
#define DEVNAME_PREFIX "OpenAL Soft on "
#else
#define DEVNAME_PREFIX ""
#endif

constexpr auto getDevicePrefix() noexcept -> std::string_view { return DEVNAME_PREFIX; }
constexpr auto getDefaultDeviceName() noexcept -> std::string_view
{ return DEVNAME_PREFIX "Default Device"; }

struct Sdl2Backend final : public BackendBase {
    Sdl2Backend(DeviceBase *device) noexcept : BackendBase{device} { }
    ~Sdl2Backend() override;

    void audioCallback(Uint8 *stream, int len) noexcept;

    void open(std::string_view name) override;
    bool reset() override;
    void start() override;
    void stop() override;

    SDL_AudioDeviceID mDeviceID{0u};
    uint mFrameSize{0};

    uint mFrequency{0u};
    DevFmtChannels mFmtChans{};
    DevFmtType     mFmtType{};
    uint mUpdateSize{0u};
};

Sdl2Backend::~Sdl2Backend()
{
    if(mDeviceID)
        SDL_CloseAudioDevice(mDeviceID);
    mDeviceID = 0;
}

void Sdl2Backend::audioCallback(Uint8 *stream, int len) noexcept
{
    const auto ulen = static_cast<unsigned int>(len);
    assert((ulen % mFrameSize) == 0);
    mDevice->renderSamples(stream, ulen / mFrameSize, mDevice->channelsFromFmt());
}

void Sdl2Backend::open(std::string_view name)
{
    SDL_AudioSpec want{}, have{};

    want.freq = static_cast<int>(mDevice->Frequency);
    switch(mDevice->FmtType)
    {
    case DevFmtUByte: want.format = AUDIO_U8; break;
    case DevFmtByte: want.format = AUDIO_S8; break;
    case DevFmtUShort: want.format = AUDIO_U16SYS; break;
    case DevFmtShort: want.format = AUDIO_S16SYS; break;
    case DevFmtUInt: /* fall-through */
    case DevFmtInt: want.format = AUDIO_S32SYS; break;
    case DevFmtFloat: want.format = AUDIO_F32; break;
    }
    want.channels = (mDevice->FmtChans == DevFmtMono) ? 1 : 2;
    want.samples = static_cast<Uint16>(std::min(mDevice->UpdateSize, 8192u));
    want.callback = [](void *ptr, Uint8 *stream, int len) noexcept
    { return static_cast<Sdl2Backend*>(ptr)->audioCallback(stream, len); };
    want.userdata = this;

    /* Passing nullptr to SDL_OpenAudioDevice opens a default, which isn't
     * necessarily the first in the list.
     */
    const auto defaultDeviceName = getDefaultDeviceName();
    SDL_AudioDeviceID devid;
    if(name.empty() || name == defaultDeviceName)
    {
        name = defaultDeviceName;
        devid = SDL_OpenAudioDevice(nullptr, SDL_FALSE, &want, &have, SDL_AUDIO_ALLOW_ANY_CHANGE);
    }
    else
    {
        const auto namePrefix = getDevicePrefix();
        if(name.size() >= namePrefix.size() && name.substr(0, namePrefix.size()) == namePrefix)
        {
            /* Copy the string_view to a string to ensure it's null terminated
             * for this call.
             */
            const std::string devname{name.substr(namePrefix.size())};
            devid = SDL_OpenAudioDevice(devname.c_str(), SDL_FALSE, &want, &have,
                SDL_AUDIO_ALLOW_ANY_CHANGE);
        }
        else
        {
            const std::string devname{name};
            devid = SDL_OpenAudioDevice(devname.c_str(), SDL_FALSE, &want, &have,
                SDL_AUDIO_ALLOW_ANY_CHANGE);
        }
    }
    if(!devid)
        throw al::backend_exception{al::backend_error::NoDevice, "%s", SDL_GetError()};

    DevFmtChannels devchans{};
    if(have.channels >= 2)
        devchans = DevFmtStereo;
    else if(have.channels == 1)
        devchans = DevFmtMono;
    else
    {
        SDL_CloseAudioDevice(devid);
        throw al::backend_exception{al::backend_error::DeviceError,
            "Unhandled SDL channel count: %d", int{have.channels}};
    }

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
        SDL_CloseAudioDevice(devid);
        throw al::backend_exception{al::backend_error::DeviceError, "Unhandled SDL format: 0x%04x",
            have.format};
    }

    if(mDeviceID)
        SDL_CloseAudioDevice(mDeviceID);
    mDeviceID = devid;

    mFrameSize = BytesFromDevFmt(devtype) * have.channels;
    mFrequency = static_cast<uint>(have.freq);
    mFmtChans = devchans;
    mFmtType = devtype;
    mUpdateSize = have.samples;

    mDevice->DeviceName = name;
}

bool Sdl2Backend::reset()
{
    mDevice->Frequency = mFrequency;
    mDevice->FmtChans = mFmtChans;
    mDevice->FmtType = mFmtType;
    mDevice->UpdateSize = mUpdateSize;
    mDevice->BufferSize = mUpdateSize * 2; /* SDL always (tries to) use two periods. */
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

    outnames.reserve(static_cast<unsigned int>(num_devices));
    outnames.emplace_back(getDefaultDeviceName());
    for(int i{0};i < num_devices;++i)
    {
        std::string outname{getDevicePrefix()};
        if(const char *name = SDL_GetAudioDeviceName(i, SDL_FALSE))
            outname += name;
        else
            outname += "Unknown Device Name #"+std::to_string(i);
        outnames.emplace_back(std::move(outname));
    }
    return outnames;
}

BackendPtr SDL2BackendFactory::createBackend(DeviceBase *device, BackendType type)
{
    if(type == BackendType::Playback)
        return BackendPtr{new Sdl2Backend{device}};
    return nullptr;
}
