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

#include "backends/sdl2.h"

#include <stdlib.h>
#include <SDL2/SDL.h>

#include <string>

#include "alMain.h"
#include "alu.h"
#include "threads.h"
#include "compat.h"


namespace {

#ifdef _WIN32
#define DEVNAME_PREFIX "OpenAL Soft on "
#else
#define DEVNAME_PREFIX ""
#endif

constexpr ALCchar defaultDeviceName[] = DEVNAME_PREFIX "Default Device";

struct ALCsdl2Backend final : public ALCbackend {
    ALCsdl2Backend(ALCdevice *device) noexcept : ALCbackend{device} { }
    ~ALCsdl2Backend() override;

    static void audioCallbackC(void *ptr, Uint8 *stream, int len);
    void audioCallback(Uint8 *stream, int len);

    SDL_AudioDeviceID mDeviceID{0u};
    ALsizei mFrameSize{0};

    ALuint mFrequency{0u};
    DevFmtChannels mFmtChans{};
    DevFmtType     mFmtType{};
    ALuint mUpdateSize{0u};
};

void ALCsdl2Backend_Construct(ALCsdl2Backend *self, ALCdevice *device);
void ALCsdl2Backend_Destruct(ALCsdl2Backend *self);
ALCenum ALCsdl2Backend_open(ALCsdl2Backend *self, const ALCchar *name);
ALCboolean ALCsdl2Backend_reset(ALCsdl2Backend *self);
ALCboolean ALCsdl2Backend_start(ALCsdl2Backend *self);
void ALCsdl2Backend_stop(ALCsdl2Backend *self);
DECLARE_FORWARD2(ALCsdl2Backend, ALCbackend, ALCenum, captureSamples, void*, ALCuint)
DECLARE_FORWARD(ALCsdl2Backend, ALCbackend, ALCuint, availableSamples)
DECLARE_FORWARD(ALCsdl2Backend, ALCbackend, ClockLatency, getClockLatency)
void ALCsdl2Backend_lock(ALCsdl2Backend *self);
void ALCsdl2Backend_unlock(ALCsdl2Backend *self);
DECLARE_DEFAULT_ALLOCATORS(ALCsdl2Backend)

DEFINE_ALCBACKEND_VTABLE(ALCsdl2Backend);

void ALCsdl2Backend_Construct(ALCsdl2Backend *self, ALCdevice *device)
{
    new (self) ALCsdl2Backend{device};
    SET_VTABLE2(ALCsdl2Backend, ALCbackend, self);
}

void ALCsdl2Backend_Destruct(ALCsdl2Backend *self)
{ self->~ALCsdl2Backend(); }

ALCsdl2Backend::~ALCsdl2Backend()
{
    if(mDeviceID)
        SDL_CloseAudioDevice(mDeviceID);
    mDeviceID = 0;
}

void ALCsdl2Backend::audioCallbackC(void *ptr, Uint8 *stream, int len)
{ static_cast<ALCsdl2Backend*>(ptr)->audioCallback(stream, len); }

void ALCsdl2Backend::audioCallback(Uint8 *stream, int len)
{
    assert((len % mFrameSize) == 0);
    aluMixData(mDevice, stream, len / mFrameSize);
}

ALCenum ALCsdl2Backend_open(ALCsdl2Backend *self, const ALCchar *name)
{
    ALCdevice *device{self->mDevice};
    SDL_AudioSpec want{}, have{};

    want.freq = device->Frequency;
    switch(device->FmtType)
    {
        case DevFmtUByte: want.format = AUDIO_U8; break;
        case DevFmtByte: want.format = AUDIO_S8; break;
        case DevFmtUShort: want.format = AUDIO_U16SYS; break;
        case DevFmtShort: want.format = AUDIO_S16SYS; break;
        case DevFmtUInt: /* fall-through */
        case DevFmtInt: want.format = AUDIO_S32SYS; break;
        case DevFmtFloat: want.format = AUDIO_F32; break;
    }
    want.channels = (device->FmtChans == DevFmtMono) ? 1 : 2;
    want.samples = device->UpdateSize;
    want.callback = &ALCsdl2Backend::audioCallbackC;
    want.userdata = self;

    /* Passing nullptr to SDL_OpenAudioDevice opens a default, which isn't
     * necessarily the first in the list.
     */
    if(!name || strcmp(name, defaultDeviceName) == 0)
        self->mDeviceID = SDL_OpenAudioDevice(nullptr, SDL_FALSE, &want, &have,
                                             SDL_AUDIO_ALLOW_ANY_CHANGE);
    else
    {
        const size_t prefix_len = strlen(DEVNAME_PREFIX);
        if(strncmp(name, DEVNAME_PREFIX, prefix_len) == 0)
            self->mDeviceID = SDL_OpenAudioDevice(name+prefix_len, SDL_FALSE, &want, &have,
                                                 SDL_AUDIO_ALLOW_ANY_CHANGE);
        else
            self->mDeviceID = SDL_OpenAudioDevice(name, SDL_FALSE, &want, &have,
                                                 SDL_AUDIO_ALLOW_ANY_CHANGE);
    }
    if(self->mDeviceID == 0)
        return ALC_INVALID_VALUE;

    device->Frequency = have.freq;
    if(have.channels == 1)
        device->FmtChans = DevFmtMono;
    else if(have.channels == 2)
        device->FmtChans = DevFmtStereo;
    else
    {
        ERR("Got unhandled SDL channel count: %d\n", (int)have.channels);
        return ALC_INVALID_VALUE;
    }
    switch(have.format)
    {
        case AUDIO_U8:     device->FmtType = DevFmtUByte;  break;
        case AUDIO_S8:     device->FmtType = DevFmtByte;   break;
        case AUDIO_U16SYS: device->FmtType = DevFmtUShort; break;
        case AUDIO_S16SYS: device->FmtType = DevFmtShort;  break;
        case AUDIO_S32SYS: device->FmtType = DevFmtInt;    break;
        case AUDIO_F32SYS: device->FmtType = DevFmtFloat;  break;
        default:
            ERR("Got unsupported SDL format: 0x%04x\n", have.format);
            return ALC_INVALID_VALUE;
    }
    device->UpdateSize = have.samples;
    device->NumUpdates = 2; /* SDL always (tries to) use two periods. */

    self->mFrameSize = device->frameSizeFromFmt();
    self->mFrequency = device->Frequency;
    self->mFmtChans = device->FmtChans;
    self->mFmtType = device->FmtType;
    self->mUpdateSize = device->UpdateSize;

    device->DeviceName = name ? name : defaultDeviceName;
    return ALC_NO_ERROR;
}

ALCboolean ALCsdl2Backend_reset(ALCsdl2Backend *self)
{
    ALCdevice *device{self->mDevice};
    device->Frequency = self->mFrequency;
    device->FmtChans = self->mFmtChans;
    device->FmtType = self->mFmtType;
    device->UpdateSize = self->mUpdateSize;
    device->NumUpdates = 2;
    SetDefaultWFXChannelOrder(device);
    return ALC_TRUE;
}

ALCboolean ALCsdl2Backend_start(ALCsdl2Backend *self)
{
    SDL_PauseAudioDevice(self->mDeviceID, 0);
    return ALC_TRUE;
}

void ALCsdl2Backend_stop(ALCsdl2Backend *self)
{
    SDL_PauseAudioDevice(self->mDeviceID, 1);
}

void ALCsdl2Backend_lock(ALCsdl2Backend *self)
{
    SDL_LockAudioDevice(self->mDeviceID);
}

void ALCsdl2Backend_unlock(ALCsdl2Backend *self)
{
    SDL_UnlockAudioDevice(self->mDeviceID);
}

} // namespace

BackendFactory &SDL2BackendFactory::getFactory()
{
    static SDL2BackendFactory factory{};
    return factory;
}

bool SDL2BackendFactory::init()
{
    return (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0);
}

void SDL2BackendFactory::deinit()
{
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool SDL2BackendFactory::querySupport(ALCbackend_Type type)
{
    return (type == ALCbackend_Playback);
}

void SDL2BackendFactory::probe(DevProbe type, std::string *outnames)
{
    if(type != ALL_DEVICE_PROBE)
        return;

    int num_devices{SDL_GetNumAudioDevices(SDL_FALSE)};

    /* Includes null char. */
    outnames->append(defaultDeviceName, sizeof(defaultDeviceName));
    for(int i{0};i < num_devices;++i)
    {
        std::string name{DEVNAME_PREFIX};
        name += SDL_GetAudioDeviceName(i, SDL_FALSE);
        if(!name.empty())
            outnames->append(name.c_str(), name.length()+1);
    }
}

ALCbackend *SDL2BackendFactory::createBackend(ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        ALCsdl2Backend *backend;
        NEW_OBJ(backend, ALCsdl2Backend)(device);
        return backend;
    }

    return nullptr;
}
