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

#include "backends/portaudio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alMain.h"
#include "alu.h"
#include "alconfig.h"
#include "ringbuffer.h"
#include "compat.h"

#include <portaudio.h>


namespace {

constexpr ALCchar pa_device[] = "PortAudio Default";


#ifdef HAVE_DYNLOAD
void *pa_handle;
#define MAKE_FUNC(x) decltype(x) * p##x
MAKE_FUNC(Pa_Initialize);
MAKE_FUNC(Pa_Terminate);
MAKE_FUNC(Pa_GetErrorText);
MAKE_FUNC(Pa_StartStream);
MAKE_FUNC(Pa_StopStream);
MAKE_FUNC(Pa_OpenStream);
MAKE_FUNC(Pa_CloseStream);
MAKE_FUNC(Pa_GetDefaultOutputDevice);
MAKE_FUNC(Pa_GetDefaultInputDevice);
MAKE_FUNC(Pa_GetStreamInfo);
#undef MAKE_FUNC

#ifndef IN_IDE_PARSER
#define Pa_Initialize                  pPa_Initialize
#define Pa_Terminate                   pPa_Terminate
#define Pa_GetErrorText                pPa_GetErrorText
#define Pa_StartStream                 pPa_StartStream
#define Pa_StopStream                  pPa_StopStream
#define Pa_OpenStream                  pPa_OpenStream
#define Pa_CloseStream                 pPa_CloseStream
#define Pa_GetDefaultOutputDevice      pPa_GetDefaultOutputDevice
#define Pa_GetDefaultInputDevice       pPa_GetDefaultInputDevice
#define Pa_GetStreamInfo               pPa_GetStreamInfo
#endif
#endif

bool pa_load(void)
{
    PaError err;

#ifdef HAVE_DYNLOAD
    if(!pa_handle)
    {
#ifdef _WIN32
# define PALIB "portaudio.dll"
#elif defined(__APPLE__) && defined(__MACH__)
# define PALIB "libportaudio.2.dylib"
#elif defined(__OpenBSD__)
# define PALIB "libportaudio.so"
#else
# define PALIB "libportaudio.so.2"
#endif

        pa_handle = LoadLib(PALIB);
        if(!pa_handle)
            return false;

#define LOAD_FUNC(f) do {                                                     \
    p##f = reinterpret_cast<decltype(p##f)>(GetSymbol(pa_handle, #f));        \
    if(p##f == nullptr)                                                       \
    {                                                                         \
        CloseLib(pa_handle);                                                  \
        pa_handle = nullptr;                                                  \
        return false;                                                         \
    }                                                                         \
} while(0)
        LOAD_FUNC(Pa_Initialize);
        LOAD_FUNC(Pa_Terminate);
        LOAD_FUNC(Pa_GetErrorText);
        LOAD_FUNC(Pa_StartStream);
        LOAD_FUNC(Pa_StopStream);
        LOAD_FUNC(Pa_OpenStream);
        LOAD_FUNC(Pa_CloseStream);
        LOAD_FUNC(Pa_GetDefaultOutputDevice);
        LOAD_FUNC(Pa_GetDefaultInputDevice);
        LOAD_FUNC(Pa_GetStreamInfo);
#undef LOAD_FUNC

        if((err=Pa_Initialize()) != paNoError)
        {
            ERR("Pa_Initialize() returned an error: %s\n", Pa_GetErrorText(err));
            CloseLib(pa_handle);
            pa_handle = nullptr;
            return false;
        }
    }
#else
    if((err=Pa_Initialize()) != paNoError)
    {
        ERR("Pa_Initialize() returned an error: %s\n", Pa_GetErrorText(err));
        return false;
    }
#endif
    return true;
}


struct ALCportPlayback final : public ALCbackend {
    PaStream *stream;
    PaStreamParameters params;
    ALuint update_size;
};

int ALCportPlayback_WriteCallback(const void *inputBuffer, void *outputBuffer,
    unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *timeInfo,
    const PaStreamCallbackFlags statusFlags, void *userData);

void ALCportPlayback_Construct(ALCportPlayback *self, ALCdevice *device);
void ALCportPlayback_Destruct(ALCportPlayback *self);
ALCenum ALCportPlayback_open(ALCportPlayback *self, const ALCchar *name);
ALCboolean ALCportPlayback_reset(ALCportPlayback *self);
ALCboolean ALCportPlayback_start(ALCportPlayback *self);
void ALCportPlayback_stop(ALCportPlayback *self);
DECLARE_FORWARD2(ALCportPlayback, ALCbackend, ALCenum, captureSamples, ALCvoid*, ALCuint)
DECLARE_FORWARD(ALCportPlayback, ALCbackend, ALCuint, availableSamples)
DECLARE_FORWARD(ALCportPlayback, ALCbackend, ClockLatency, getClockLatency)
DECLARE_FORWARD(ALCportPlayback, ALCbackend, void, lock)
DECLARE_FORWARD(ALCportPlayback, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(ALCportPlayback)

DEFINE_ALCBACKEND_VTABLE(ALCportPlayback);


void ALCportPlayback_Construct(ALCportPlayback *self, ALCdevice *device)
{
    new (self) ALCportPlayback{};
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(ALCportPlayback, ALCbackend, self);

    self->stream = nullptr;
}

void ALCportPlayback_Destruct(ALCportPlayback *self)
{
    PaError err = self->stream ? Pa_CloseStream(self->stream) : paNoError;
    if(err != paNoError)
        ERR("Error closing stream: %s\n", Pa_GetErrorText(err));
    self->stream = nullptr;

    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
    self->~ALCportPlayback();
}


int ALCportPlayback_WriteCallback(const void *UNUSED(inputBuffer), void *outputBuffer,
    unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *UNUSED(timeInfo),
    const PaStreamCallbackFlags UNUSED(statusFlags), void *userData)
{
    ALCportPlayback *self = static_cast<ALCportPlayback*>(userData);

    ALCportPlayback_lock(self);
    aluMixData(STATIC_CAST(ALCbackend, self)->mDevice, outputBuffer, framesPerBuffer);
    ALCportPlayback_unlock(self);
    return 0;
}


ALCenum ALCportPlayback_open(ALCportPlayback *self, const ALCchar *name)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    PaError err;

    if(!name)
        name = pa_device;
    else if(strcmp(name, pa_device) != 0)
        return ALC_INVALID_VALUE;

    self->update_size = device->UpdateSize;

    self->params.device = -1;
    if(!ConfigValueInt(nullptr, "port", "device", &self->params.device) ||
       self->params.device < 0)
        self->params.device = Pa_GetDefaultOutputDevice();
    self->params.suggestedLatency = (device->UpdateSize*device->NumUpdates) /
                                    (float)device->Frequency;
    self->params.hostApiSpecificStreamInfo = nullptr;

    self->params.channelCount = ((device->FmtChans == DevFmtMono) ? 1 : 2);

    switch(device->FmtType)
    {
        case DevFmtByte:
            self->params.sampleFormat = paInt8;
            break;
        case DevFmtUByte:
            self->params.sampleFormat = paUInt8;
            break;
        case DevFmtUShort:
            /* fall-through */
        case DevFmtShort:
            self->params.sampleFormat = paInt16;
            break;
        case DevFmtUInt:
            /* fall-through */
        case DevFmtInt:
            self->params.sampleFormat = paInt32;
            break;
        case DevFmtFloat:
            self->params.sampleFormat = paFloat32;
            break;
    }

retry_open:
    err = Pa_OpenStream(&self->stream, nullptr, &self->params,
        device->Frequency, device->UpdateSize, paNoFlag,
        ALCportPlayback_WriteCallback, self
    );
    if(err != paNoError)
    {
        if(self->params.sampleFormat == paFloat32)
        {
            self->params.sampleFormat = paInt16;
            goto retry_open;
        }
        ERR("Pa_OpenStream() returned an error: %s\n", Pa_GetErrorText(err));
        return ALC_INVALID_VALUE;
    }

    al_free(device->DeviceName);
    device->DeviceName = alstrdup(name);

    return ALC_NO_ERROR;

}

ALCboolean ALCportPlayback_reset(ALCportPlayback *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    const PaStreamInfo *streamInfo;

    streamInfo = Pa_GetStreamInfo(self->stream);
    device->Frequency = streamInfo->sampleRate;
    device->UpdateSize = self->update_size;

    if(self->params.sampleFormat == paInt8)
        device->FmtType = DevFmtByte;
    else if(self->params.sampleFormat == paUInt8)
        device->FmtType = DevFmtUByte;
    else if(self->params.sampleFormat == paInt16)
        device->FmtType = DevFmtShort;
    else if(self->params.sampleFormat == paInt32)
        device->FmtType = DevFmtInt;
    else if(self->params.sampleFormat == paFloat32)
        device->FmtType = DevFmtFloat;
    else
    {
        ERR("Unexpected sample format: 0x%lx\n", self->params.sampleFormat);
        return ALC_FALSE;
    }

    if(self->params.channelCount == 2)
        device->FmtChans = DevFmtStereo;
    else if(self->params.channelCount == 1)
        device->FmtChans = DevFmtMono;
    else
    {
        ERR("Unexpected channel count: %u\n", self->params.channelCount);
        return ALC_FALSE;
    }
    SetDefaultChannelOrder(device);

    return ALC_TRUE;
}

ALCboolean ALCportPlayback_start(ALCportPlayback *self)
{
    PaError err;

    err = Pa_StartStream(self->stream);
    if(err != paNoError)
    {
        ERR("Pa_StartStream() returned an error: %s\n", Pa_GetErrorText(err));
        return ALC_FALSE;
    }

    return ALC_TRUE;
}

void ALCportPlayback_stop(ALCportPlayback *self)
{
    PaError err = Pa_StopStream(self->stream);
    if(err != paNoError)
        ERR("Error stopping stream: %s\n", Pa_GetErrorText(err));
}


struct ALCportCapture final : public ALCbackend {
    PaStream *stream;
    PaStreamParameters params;

    ll_ringbuffer_t *ring;
};

int ALCportCapture_ReadCallback(const void *inputBuffer, void *outputBuffer,
    unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *timeInfo,
    const PaStreamCallbackFlags statusFlags, void *userData);

void ALCportCapture_Construct(ALCportCapture *self, ALCdevice *device);
void ALCportCapture_Destruct(ALCportCapture *self);
ALCenum ALCportCapture_open(ALCportCapture *self, const ALCchar *name);
DECLARE_FORWARD(ALCportCapture, ALCbackend, ALCboolean, reset)
ALCboolean ALCportCapture_start(ALCportCapture *self);
void ALCportCapture_stop(ALCportCapture *self);
ALCenum ALCportCapture_captureSamples(ALCportCapture *self, ALCvoid *buffer, ALCuint samples);
ALCuint ALCportCapture_availableSamples(ALCportCapture *self);
DECLARE_FORWARD(ALCportCapture, ALCbackend, ClockLatency, getClockLatency)
DECLARE_FORWARD(ALCportCapture, ALCbackend, void, lock)
DECLARE_FORWARD(ALCportCapture, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(ALCportCapture)

DEFINE_ALCBACKEND_VTABLE(ALCportCapture);


void ALCportCapture_Construct(ALCportCapture *self, ALCdevice *device)
{
    new (self) ALCportCapture{};
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(ALCportCapture, ALCbackend, self);

    self->stream = nullptr;
    self->ring = nullptr;
}

void ALCportCapture_Destruct(ALCportCapture *self)
{
    PaError err = self->stream ? Pa_CloseStream(self->stream) : paNoError;
    if(err != paNoError)
        ERR("Error closing stream: %s\n", Pa_GetErrorText(err));
    self->stream = nullptr;

    ll_ringbuffer_free(self->ring);
    self->ring = nullptr;

    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
    self->~ALCportCapture();
}


int ALCportCapture_ReadCallback(const void *inputBuffer, void *UNUSED(outputBuffer),
    unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *UNUSED(timeInfo),
    const PaStreamCallbackFlags UNUSED(statusFlags), void *userData)
{
    ALCportCapture *self = static_cast<ALCportCapture*>(userData);
    size_t writable = ll_ringbuffer_write_space(self->ring);

    if(framesPerBuffer > writable) framesPerBuffer = writable;
    ll_ringbuffer_write(self->ring, static_cast<const char*>(inputBuffer), framesPerBuffer);
    return 0;
}


ALCenum ALCportCapture_open(ALCportCapture *self, const ALCchar *name)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    ALuint samples, frame_size;
    PaError err;

    if(!name)
        name = pa_device;
    else if(strcmp(name, pa_device) != 0)
        return ALC_INVALID_VALUE;

    samples = device->UpdateSize * device->NumUpdates;
    samples = maxu(samples, 100 * device->Frequency / 1000);
    frame_size = FrameSizeFromDevFmt(device->FmtChans, device->FmtType, device->mAmbiOrder);

    self->ring = ll_ringbuffer_create(samples, frame_size, false);
    if(self->ring == nullptr) return ALC_INVALID_VALUE;

    self->params.device = -1;
    if(!ConfigValueInt(nullptr, "port", "capture", &self->params.device) ||
       self->params.device < 0)
        self->params.device = Pa_GetDefaultInputDevice();
    self->params.suggestedLatency = 0.0f;
    self->params.hostApiSpecificStreamInfo = nullptr;

    switch(device->FmtType)
    {
        case DevFmtByte:
            self->params.sampleFormat = paInt8;
            break;
        case DevFmtUByte:
            self->params.sampleFormat = paUInt8;
            break;
        case DevFmtShort:
            self->params.sampleFormat = paInt16;
            break;
        case DevFmtInt:
            self->params.sampleFormat = paInt32;
            break;
        case DevFmtFloat:
            self->params.sampleFormat = paFloat32;
            break;
        case DevFmtUInt:
        case DevFmtUShort:
            ERR("%s samples not supported\n", DevFmtTypeString(device->FmtType));
            return ALC_INVALID_VALUE;
    }
    self->params.channelCount = ChannelsFromDevFmt(device->FmtChans, device->mAmbiOrder);

    err = Pa_OpenStream(&self->stream, &self->params, nullptr,
        device->Frequency, paFramesPerBufferUnspecified, paNoFlag,
        ALCportCapture_ReadCallback, self
    );
    if(err != paNoError)
    {
        ERR("Pa_OpenStream() returned an error: %s\n", Pa_GetErrorText(err));
        return ALC_INVALID_VALUE;
    }

    al_free(device->DeviceName);
    device->DeviceName = alstrdup(name);

    return ALC_NO_ERROR;
}


ALCboolean ALCportCapture_start(ALCportCapture *self)
{
    PaError err = Pa_StartStream(self->stream);
    if(err != paNoError)
    {
        ERR("Error starting stream: %s\n", Pa_GetErrorText(err));
        return ALC_FALSE;
    }
    return ALC_TRUE;
}

void ALCportCapture_stop(ALCportCapture *self)
{
    PaError err = Pa_StopStream(self->stream);
    if(err != paNoError)
        ERR("Error stopping stream: %s\n", Pa_GetErrorText(err));
}


ALCuint ALCportCapture_availableSamples(ALCportCapture *self)
{
    return ll_ringbuffer_read_space(self->ring);
}

ALCenum ALCportCapture_captureSamples(ALCportCapture *self, ALCvoid *buffer, ALCuint samples)
{
    ll_ringbuffer_read(self->ring, static_cast<char*>(buffer), samples);
    return ALC_NO_ERROR;
}

} // namespace


bool PortBackendFactory::init()
{ return pa_load(); }

void PortBackendFactory::deinit()
{
#ifdef HAVE_DYNLOAD
    if(pa_handle)
    {
        Pa_Terminate();
        CloseLib(pa_handle);
        pa_handle = nullptr;
    }
#else
    Pa_Terminate();
#endif
}

bool PortBackendFactory::querySupport(ALCbackend_Type type)
{ return (type == ALCbackend_Playback || type == ALCbackend_Capture); }

void PortBackendFactory::probe(enum DevProbe type, std::string *outnames)
{
    switch(type)
    {
        case ALL_DEVICE_PROBE:
        case CAPTURE_DEVICE_PROBE:
            /* Includes null char. */
            outnames->append(pa_device, sizeof(pa_device));
            break;
    }
}

ALCbackend *PortBackendFactory::createBackend(ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        ALCportPlayback *backend;
        NEW_OBJ(backend, ALCportPlayback)(device);
        if(!backend) return nullptr;
        return STATIC_CAST(ALCbackend, backend);
    }
    if(type == ALCbackend_Capture)
    {
        ALCportCapture *backend;
        NEW_OBJ(backend, ALCportCapture)(device);
        if(!backend) return nullptr;
        return STATIC_CAST(ALCbackend, backend);
    }

    return nullptr;
}

BackendFactory &PortBackendFactory::getFactory()
{
    static PortBackendFactory factory{};
    return factory;
}
