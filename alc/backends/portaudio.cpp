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

#include "portaudio.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "albit.h"
#include "alc/alconfig.h"
#include "alnumeric.h"
#include "alstring.h"
#include "core/device.h"
#include "core/logging.h"
#include "dynload.h"
#include "ringbuffer.h"

#include <portaudio.h> /* NOLINT(*-duplicate-include) Not the same header. */


namespace {

using namespace std::string_view_literals;

[[nodiscard]] constexpr auto GetDefaultName() noexcept { return "PortAudio Default"sv; }


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
    PortPlayback(DeviceBase *device) noexcept : BackendBase{device} { }
    ~PortPlayback() override;

    int writeCallback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo *timeInfo, const PaStreamCallbackFlags statusFlags) noexcept;

    void open(std::string_view name) override;
    bool reset() override;
    void start() override;
    void stop() override;

    PaStream *mStream{nullptr};
    PaStreamParameters mParams{};
    uint mUpdateSize{0u};
};

PortPlayback::~PortPlayback()
{
    PaError err{mStream ? Pa_CloseStream(mStream) : paNoError};
    if(err != paNoError)
        ERR("Error closing stream: %s\n", Pa_GetErrorText(err));
    mStream = nullptr;
}


int PortPlayback::writeCallback(const void*, void *outputBuffer, unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo*, const PaStreamCallbackFlags) noexcept
{
    mDevice->renderSamples(outputBuffer, static_cast<uint>(framesPerBuffer),
        static_cast<uint>(mParams.channelCount));
    return 0;
}


void PortPlayback::open(std::string_view name)
{
    if(name.empty())
        name = GetDefaultName();
    else if(name != GetDefaultName())
        throw al::backend_exception{al::backend_error::NoDevice, "Device name \"%.*s\" not found",
            al::sizei(name), name.data()};

    PaStreamParameters params{};
    auto devidopt = ConfigValueInt({}, "port", "device");
    if(devidopt && *devidopt >= 0) params.device = *devidopt;
    else params.device = Pa_GetDefaultOutputDevice();
    params.suggestedLatency = mDevice->BufferSize / static_cast<double>(mDevice->Frequency);
    params.hostApiSpecificStreamInfo = nullptr;

    params.channelCount = ((mDevice->FmtChans == DevFmtMono) ? 1 : 2);

    switch(mDevice->FmtType)
    {
    case DevFmtByte:
        params.sampleFormat = paInt8;
        break;
    case DevFmtUByte:
        params.sampleFormat = paUInt8;
        break;
    case DevFmtUShort:
        /* fall-through */
    case DevFmtShort:
        params.sampleFormat = paInt16;
        break;
    case DevFmtUInt:
        /* fall-through */
    case DevFmtInt:
        params.sampleFormat = paInt32;
        break;
    case DevFmtFloat:
        params.sampleFormat = paFloat32;
        break;
    }

    static constexpr auto writeCallback = [](const void *inputBuffer, void *outputBuffer,
        unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *timeInfo,
        const PaStreamCallbackFlags statusFlags, void *userData) noexcept
    {
        return static_cast<PortPlayback*>(userData)->writeCallback(inputBuffer, outputBuffer,
            framesPerBuffer, timeInfo, statusFlags);
    };
    PaStream *stream{};
    while(PaError err{Pa_OpenStream(&stream, nullptr, &params, mDevice->Frequency,
        mDevice->UpdateSize, paNoFlag, writeCallback, this)})
    {
        if(params.sampleFormat != paFloat32)
            throw al::backend_exception{al::backend_error::NoDevice, "Failed to open stream: %s",
                Pa_GetErrorText(err)};
        params.sampleFormat = paInt16;
    }

    Pa_CloseStream(mStream);
    mStream = stream;
    mParams = params;
    mUpdateSize = mDevice->UpdateSize;

    mDevice->DeviceName = name;
}

bool PortPlayback::reset()
{
    const PaStreamInfo *streamInfo{Pa_GetStreamInfo(mStream)};
    mDevice->Frequency = static_cast<uint>(streamInfo->sampleRate);
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
        return false;
    }

    if(mParams.channelCount >= 2)
        mDevice->FmtChans = DevFmtStereo;
    else if(mParams.channelCount == 1)
        mDevice->FmtChans = DevFmtMono;
    else
    {
        ERR("Unexpected channel count: %u\n", mParams.channelCount);
        return false;
    }
    setDefaultChannelOrder();

    return true;
}

void PortPlayback::start()
{
    const PaError err{Pa_StartStream(mStream)};
    if(err != paNoError)
        throw al::backend_exception{al::backend_error::DeviceError, "Failed to start playback: %s",
            Pa_GetErrorText(err)};
}

void PortPlayback::stop()
{
    PaError err{Pa_StopStream(mStream)};
    if(err != paNoError)
        ERR("Error stopping stream: %s\n", Pa_GetErrorText(err));
}


struct PortCapture final : public BackendBase {
    PortCapture(DeviceBase *device) noexcept : BackendBase{device} { }
    ~PortCapture() override;

    int readCallback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo *timeInfo, const PaStreamCallbackFlags statusFlags) const noexcept;

    void open(std::string_view name) override;
    void start() override;
    void stop() override;
    void captureSamples(std::byte *buffer, uint samples) override;
    uint availableSamples() override;

    PaStream *mStream{nullptr};
    PaStreamParameters mParams{};

    RingBufferPtr mRing{nullptr};
};

PortCapture::~PortCapture()
{
    PaError err{mStream ? Pa_CloseStream(mStream) : paNoError};
    if(err != paNoError)
        ERR("Error closing stream: %s\n", Pa_GetErrorText(err));
    mStream = nullptr;
}


int PortCapture::readCallback(const void *inputBuffer, void*, unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo*, const PaStreamCallbackFlags) const noexcept
{
    std::ignore = mRing->write(inputBuffer, framesPerBuffer);
    return 0;
}


void PortCapture::open(std::string_view name)
{
    if(name.empty())
        name = GetDefaultName();
    else if(name != GetDefaultName())
        throw al::backend_exception{al::backend_error::NoDevice, "Device name \"%.*s\" not found",
            al::sizei(name), name.data()};

    const uint samples{std::max(mDevice->BufferSize, mDevice->Frequency/10u)};
    const uint frame_size{mDevice->frameSizeFromFmt()};

    mRing = RingBuffer::Create(samples, frame_size, false);

    auto devidopt = ConfigValueInt({}, "port", "capture");
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
        throw al::backend_exception{al::backend_error::DeviceError, "%s samples not supported",
            DevFmtTypeString(mDevice->FmtType)};
    }
    mParams.channelCount = static_cast<int>(mDevice->channelsFromFmt());

    static constexpr auto readCallback = [](const void *inputBuffer, void *outputBuffer,
        unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *timeInfo,
        const PaStreamCallbackFlags statusFlags, void *userData) noexcept
    {
        return static_cast<PortCapture*>(userData)->readCallback(inputBuffer, outputBuffer,
            framesPerBuffer, timeInfo, statusFlags);
    };
    PaError err{Pa_OpenStream(&mStream, &mParams, nullptr, mDevice->Frequency,
        paFramesPerBufferUnspecified, paNoFlag, readCallback, this)};
    if(err != paNoError)
        throw al::backend_exception{al::backend_error::NoDevice, "Failed to open stream: %s",
            Pa_GetErrorText(err)};

    mDevice->DeviceName = name;
}


void PortCapture::start()
{
    const PaError err{Pa_StartStream(mStream)};
    if(err != paNoError)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start recording: %s", Pa_GetErrorText(err)};
}

void PortCapture::stop()
{
    PaError err{Pa_StopStream(mStream)};
    if(err != paNoError)
        ERR("Error stopping stream: %s\n", Pa_GetErrorText(err));
}


uint PortCapture::availableSamples()
{ return static_cast<uint>(mRing->readSpace()); }

void PortCapture::captureSamples(std::byte *buffer, uint samples)
{ std::ignore = mRing->read(buffer, samples); }

} // namespace


bool PortBackendFactory::init()
{
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

        const PaError err{Pa_Initialize()};
        if(err != paNoError)
        {
            ERR("Pa_Initialize() returned an error: %s\n", Pa_GetErrorText(err));
            CloseLib(pa_handle);
            pa_handle = nullptr;
            return false;
        }
    }
#else
    const PaError err{Pa_Initialize()};
    if(err != paNoError)
    {
        ERR("Pa_Initialize() returned an error: %s\n", Pa_GetErrorText(err));
        return false;
    }
#endif
    return true;
}

bool PortBackendFactory::querySupport(BackendType type)
{ return (type == BackendType::Playback || type == BackendType::Capture); }

std::string PortBackendFactory::probe(BackendType type)
{
    switch(type)
    {
    case BackendType::Playback:
    case BackendType::Capture:
        /* Include null char. */
        return std::string{GetDefaultName()} + '\0';
    }
    return std::string{};
}

BackendPtr PortBackendFactory::createBackend(DeviceBase *device, BackendType type)
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
