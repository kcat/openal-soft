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

#include <cstdio>
#include <cstdlib>
#include <cstring>

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


struct PortPlayback final : public BackendBase {
    PortPlayback(ALCdevice *device) noexcept : BackendBase{device} { }
    ~PortPlayback() override;

    static int writeCallbackC(const void *inputBuffer, void *outputBuffer,
        unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *timeInfo,
        const PaStreamCallbackFlags statusFlags, void *userData);
    int writeCallback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo *timeInfo, const PaStreamCallbackFlags statusFlags);

    ALCenum open(const ALCchar *name) override;
    ALCboolean reset() override;
    ALCboolean start() override;
    void stop() override;

    PaStream *mStream{nullptr};
    PaStreamParameters mParams{};
    ALuint mUpdateSize{0u};

    DEF_NEWDEL(PortPlayback)
};

PortPlayback::~PortPlayback()
{
    PaError err{mStream ? Pa_CloseStream(mStream) : paNoError};
    if(err != paNoError)
        ERR("Error closing stream: %s\n", Pa_GetErrorText(err));
    mStream = nullptr;
}


int PortPlayback::writeCallbackC(const void *inputBuffer, void *outputBuffer,
    unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *timeInfo,
    const PaStreamCallbackFlags statusFlags, void *userData)
{
    return static_cast<PortPlayback*>(userData)->writeCallback(inputBuffer, outputBuffer,
        framesPerBuffer, timeInfo, statusFlags);
}

int PortPlayback::writeCallback(const void* UNUSED(inputBuffer), void *outputBuffer,
    unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* UNUSED(timeInfo),
    const PaStreamCallbackFlags UNUSED(statusFlags))
{
    lock();
    aluMixData(mDevice, outputBuffer, framesPerBuffer);
    unlock();
    return 0;
}


ALCenum PortPlayback::open(const ALCchar *name)
{
    if(!name)
        name = pa_device;
    else if(strcmp(name, pa_device) != 0)
        return ALC_INVALID_VALUE;

    mUpdateSize = mDevice->UpdateSize;

    auto devidopt = ConfigValueInt(nullptr, "port", "device");
    if(devidopt && *devidopt >= 0) mParams.device = *devidopt;
    else mParams.device = Pa_GetDefaultOutputDevice();
    mParams.suggestedLatency = mDevice->BufferSize / static_cast<double>(mDevice->Frequency);
    mParams.hostApiSpecificStreamInfo = nullptr;

    mParams.channelCount = ((mDevice->FmtChans == DevFmtMono) ? 1 : 2);

    switch(mDevice->FmtType)
    {
        case DevFmtByte:
            mParams.sampleFormat = paInt8;
            break;
        case DevFmtUByte:
            mParams.sampleFormat = paUInt8;
            break;
        case DevFmtUShort:
            /* fall-through */
        case DevFmtShort:
            mParams.sampleFormat = paInt16;
            break;
        case DevFmtUInt:
            /* fall-through */
        case DevFmtInt:
            mParams.sampleFormat = paInt32;
            break;
        case DevFmtFloat:
            mParams.sampleFormat = paFloat32;
            break;
    }

retry_open:
    PaError err{Pa_OpenStream(&mStream, nullptr, &mParams, mDevice->Frequency, mDevice->UpdateSize,
        paNoFlag, &PortPlayback::writeCallbackC, this)};
    if(err != paNoError)
    {
        if(mParams.sampleFormat == paFloat32)
        {
            mParams.sampleFormat = paInt16;
            goto retry_open;
        }
        ERR("Pa_OpenStream() returned an error: %s\n", Pa_GetErrorText(err));
        return ALC_INVALID_VALUE;
    }

    mDevice->DeviceName = name;
    return ALC_NO_ERROR;

}

ALCboolean PortPlayback::reset()
{
    const PaStreamInfo *streamInfo{Pa_GetStreamInfo(mStream)};
    mDevice->Frequency = streamInfo->sampleRate;
    mDevice->UpdateSize = mUpdateSize;

    if(mParams.sampleFormat == paInt8)
        mDevice->FmtType = DevFmtByte;
    else if(mParams.sampleFormat == paUInt8)
        mDevice->FmtType = DevFmtUByte;
    else if(mParams.sampleFormat == paInt16)
        mDevice->FmtType = DevFmtShort;
    else if(mParams.sampleFormat == paInt32)
        mDevice->FmtType = DevFmtInt;
    else if(mParams.sampleFormat == paFloat32)
        mDevice->FmtType = DevFmtFloat;
    else
    {
        ERR("Unexpected sample format: 0x%lx\n", mParams.sampleFormat);
        return ALC_FALSE;
    }

    if(mParams.channelCount == 2)
        mDevice->FmtChans = DevFmtStereo;
    else if(mParams.channelCount == 1)
        mDevice->FmtChans = DevFmtMono;
    else
    {
        ERR("Unexpected channel count: %u\n", mParams.channelCount);
        return ALC_FALSE;
    }
    SetDefaultChannelOrder(mDevice);

    return ALC_TRUE;
}

ALCboolean PortPlayback::start()
{
    PaError err{Pa_StartStream(mStream)};
    if(err != paNoError)
    {
        ERR("Pa_StartStream() returned an error: %s\n", Pa_GetErrorText(err));
        return ALC_FALSE;
    }
    return ALC_TRUE;
}

void PortPlayback::stop()
{
    PaError err{Pa_StopStream(mStream)};
    if(err != paNoError)
        ERR("Error stopping stream: %s\n", Pa_GetErrorText(err));
}


struct PortCapture final : public BackendBase {
    PortCapture(ALCdevice *device) noexcept : BackendBase{device} { }
    ~PortCapture() override;

    static int readCallbackC(const void *inputBuffer, void *outputBuffer,
        unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *timeInfo,
        const PaStreamCallbackFlags statusFlags, void *userData);
    int readCallback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo *timeInfo, const PaStreamCallbackFlags statusFlags);

    ALCenum open(const ALCchar *name) override;
    ALCboolean start() override;
    void stop() override;
    ALCenum captureSamples(ALCvoid *buffer, ALCuint samples) override;
    ALCuint availableSamples() override;

    PaStream *mStream{nullptr};
    PaStreamParameters mParams;

    RingBufferPtr mRing{nullptr};

    DEF_NEWDEL(PortCapture)
};

PortCapture::~PortCapture()
{
    PaError err{mStream ? Pa_CloseStream(mStream) : paNoError};
    if(err != paNoError)
        ERR("Error closing stream: %s\n", Pa_GetErrorText(err));
    mStream = nullptr;
}


int PortCapture::readCallbackC(const void *inputBuffer, void *outputBuffer,
    unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *timeInfo,
    const PaStreamCallbackFlags statusFlags, void* userData)
{
    return static_cast<PortCapture*>(userData)->readCallback(inputBuffer, outputBuffer,
        framesPerBuffer, timeInfo, statusFlags);
}

int PortCapture::readCallback(const void *inputBuffer, void *UNUSED(outputBuffer),
    unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *UNUSED(timeInfo),
    const PaStreamCallbackFlags UNUSED(statusFlags))
{
    mRing->write(inputBuffer, framesPerBuffer);
    return 0;
}


ALCenum PortCapture::open(const ALCchar *name)
{
    if(!name)
        name = pa_device;
    else if(strcmp(name, pa_device) != 0)
        return ALC_INVALID_VALUE;

    ALuint samples{mDevice->BufferSize};
    samples = maxu(samples, 100 * mDevice->Frequency / 1000);
    ALsizei frame_size{mDevice->frameSizeFromFmt()};

    mRing = CreateRingBuffer(samples, frame_size, false);
    if(!mRing) return ALC_INVALID_VALUE;

    auto devidopt = ConfigValueInt(nullptr, "port", "capture");
    if(devidopt && *devidopt >= 0) mParams.device = *devidopt;
    else mParams.device = Pa_GetDefaultOutputDevice();
    mParams.suggestedLatency = 0.0f;
    mParams.hostApiSpecificStreamInfo = nullptr;

    switch(mDevice->FmtType)
    {
        case DevFmtByte:
            mParams.sampleFormat = paInt8;
            break;
        case DevFmtUByte:
            mParams.sampleFormat = paUInt8;
            break;
        case DevFmtShort:
            mParams.sampleFormat = paInt16;
            break;
        case DevFmtInt:
            mParams.sampleFormat = paInt32;
            break;
        case DevFmtFloat:
            mParams.sampleFormat = paFloat32;
            break;
        case DevFmtUInt:
        case DevFmtUShort:
            ERR("%s samples not supported\n", DevFmtTypeString(mDevice->FmtType));
            return ALC_INVALID_VALUE;
    }
    mParams.channelCount = mDevice->channelsFromFmt();

    PaError err{Pa_OpenStream(&mStream, &mParams, nullptr, mDevice->Frequency,
        paFramesPerBufferUnspecified, paNoFlag, &PortCapture::readCallbackC, this)};
    if(err != paNoError)
    {
        ERR("Pa_OpenStream() returned an error: %s\n", Pa_GetErrorText(err));
        return ALC_INVALID_VALUE;
    }

    mDevice->DeviceName = name;
    return ALC_NO_ERROR;
}


ALCboolean PortCapture::start()
{
    PaError err{Pa_StartStream(mStream)};
    if(err != paNoError)
    {
        ERR("Error starting stream: %s\n", Pa_GetErrorText(err));
        return ALC_FALSE;
    }
    return ALC_TRUE;
}

void PortCapture::stop()
{
    PaError err{Pa_StopStream(mStream)};
    if(err != paNoError)
        ERR("Error stopping stream: %s\n", Pa_GetErrorText(err));
}


ALCuint PortCapture::availableSamples()
{ return mRing->readSpace(); }

ALCenum PortCapture::captureSamples(ALCvoid *buffer, ALCuint samples)
{
    mRing->read(buffer, samples);
    return ALC_NO_ERROR;
}

} // namespace


bool PortBackendFactory::init()
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

bool PortBackendFactory::querySupport(BackendType type)
{ return (type == BackendType::Playback || type == BackendType::Capture); }

void PortBackendFactory::probe(DevProbe type, std::string *outnames)
{
    switch(type)
    {
        case DevProbe::Playback:
        case DevProbe::Capture:
            /* Includes null char. */
            outnames->append(pa_device, sizeof(pa_device));
            break;
    }
}

BackendPtr PortBackendFactory::createBackend(ALCdevice *device, BackendType type)
{
    if(type == BackendType::Playback)
        return BackendPtr{new PortPlayback{device}};
    if(type == BackendType::Capture)
        return BackendPtr{new PortCapture{device}};
    return nullptr;
}

BackendFactory &PortBackendFactory::getFactory()
{
    static PortBackendFactory factory{};
    return factory;
}
