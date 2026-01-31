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

#include "portaudio.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include "alc/alconfig.h"
#include "core/device.h"
#include "core/logging.h"
#include "dynload.h"
#include "ringbuffer.h"

#include <portaudio.h>


namespace {

#if HAVE_DYNLOAD
void *pa_handle;
#define MAKE_FUNC(x) decltype(x) * p##x
MAKE_FUNC(Pa_Initialize);
MAKE_FUNC(Pa_Terminate);
MAKE_FUNC(Pa_GetErrorText);
MAKE_FUNC(Pa_StartStream);
MAKE_FUNC(Pa_StopStream);
MAKE_FUNC(Pa_OpenStream);
MAKE_FUNC(Pa_CloseStream);
MAKE_FUNC(Pa_GetDeviceCount);
MAKE_FUNC(Pa_GetDeviceInfo);
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
#define Pa_GetDeviceCount              pPa_GetDeviceCount
#define Pa_GetDeviceInfo               pPa_GetDeviceInfo
#define Pa_GetDefaultOutputDevice      pPa_GetDefaultOutputDevice
#define Pa_GetDefaultInputDevice       pPa_GetDefaultInputDevice
#define Pa_GetStreamInfo               pPa_GetStreamInfo
#endif
#endif

struct DeviceEntry {
    std::string mName;
    unsigned mPlaybackChannels{};
    unsigned mCaptureChannels{};
};
std::vector<DeviceEntry> DeviceNames;

void EnumerateDevices()
{
    const auto devcount = Pa_GetDeviceCount();
    if(devcount < 0)
    {
        ERR("Error getting device count: {}", Pa_GetErrorText(devcount));
        return;
    }

    std::vector<DeviceEntry>(gsl::narrow_cast<unsigned>(devcount)).swap(DeviceNames);
    auto idx = PaDeviceIndex{0};
    for(auto &entry : DeviceNames)
    {
        if(auto const info = Pa_GetDeviceInfo(idx); info && info->name)
        {
            entry.mName = info->name;
            entry.mPlaybackChannels = gsl::narrow_cast<unsigned>(std::max(info->maxOutputChannels, 0));
            entry.mCaptureChannels = gsl::narrow_cast<unsigned>(std::max(info->maxInputChannels, 0));
            TRACE("Device {} \"{}\": {} playback, {} capture channels", idx, entry.mName,
                info->maxOutputChannels, info->maxInputChannels);
        }
        ++idx;
    }
}

struct StreamParamsExt : PaStreamParameters { unsigned updateSize; };

struct PortPlayback final : BackendBase {
    explicit PortPlayback(gsl::not_null<DeviceBase*> const device) noexcept : BackendBase{device}
    { }
    ~PortPlayback() override;

    auto writeCallback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo *timeInfo, const PaStreamCallbackFlags statusFlags) noexcept
        -> int;

    void createStream(PaDeviceIndex deviceid);

    void open(std::string_view name) override;
    auto reset() -> bool override;
    void start() override;
    void stop() override;

    PaStream *mStream{nullptr};
    StreamParamsExt mParams{};
    PaDeviceIndex mDeviceIdx{-1};
};

PortPlayback::~PortPlayback()
{
    if(const auto err = mStream ? Pa_CloseStream(mStream) : paNoError; err != paNoError)
        ERR("Error closing stream: {}", Pa_GetErrorText(err));
    mStream = nullptr;
}


auto PortPlayback::writeCallback(const void*, void *const outputBuffer,
    unsigned long const framesPerBuffer, PaStreamCallbackTimeInfo const*, PaStreamCallbackFlags)
    noexcept -> int
{
    mDevice->renderSamples(outputBuffer, gsl::narrow_cast<unsigned>(framesPerBuffer),
        gsl::narrow_cast<unsigned>(mParams.channelCount));
    return 0;
}


void PortPlayback::createStream(PaDeviceIndex const deviceid)
{
    auto const &devinfo = gsl::at(DeviceNames, deviceid);

    auto params = StreamParamsExt{};
    params.device = deviceid;
    params.suggestedLatency = mDevice->mBufferSize
        / gsl::narrow_cast<double>(mDevice->mSampleRate);
    params.hostApiSpecificStreamInfo = nullptr;
    params.channelCount = gsl::narrow_cast<int>(std::min(devinfo.mPlaybackChannels,
        mDevice->channelsFromFmt()));
    switch(mDevice->FmtType)
    {
    case DevFmtByte: params.sampleFormat = paInt8; break;
    case DevFmtUByte: params.sampleFormat = paUInt8; break;
    case DevFmtUShort: [[fallthrough]];
    case DevFmtShort: params.sampleFormat = paInt16; break;
    case DevFmtUInt: [[fallthrough]];
    case DevFmtInt: params.sampleFormat = paInt32; break;
    case DevFmtFloat: params.sampleFormat = paFloat32; break;
    }
    params.updateSize = mDevice->mUpdateSize;

    auto srate = mDevice->mSampleRate;

    static constexpr auto writeCallback = [](void const *const inputBuffer,
        void *const outputBuffer, unsigned long const framesPerBuffer,
        PaStreamCallbackTimeInfo const *const timeInfo, PaStreamCallbackFlags const statusFlags,
        void *const userData) noexcept -> int
    {
        return static_cast<PortPlayback*>(userData)->writeCallback(inputBuffer, outputBuffer,
            framesPerBuffer, timeInfo, statusFlags);
    };
    while(const auto err = Pa_OpenStream(&mStream, nullptr, &params, srate, params.updateSize,
        paNoFlag, writeCallback, this))
    {
        if(params.updateSize != DefaultUpdateSize)
            params.updateSize = DefaultUpdateSize;
        else if(srate != 48000u)
            srate = (srate != 44100u) ? 44100u : 48000u;
        else if(params.sampleFormat != paInt16)
            params.sampleFormat = paInt16;
        else if(params.channelCount != 2)
            params.channelCount = 2;
        else
            throw al::backend_exception{al::backend_error::NoDevice, "Failed to open stream: {}",
                Pa_GetErrorText(err)};
    }

    mParams = params;
}

void PortPlayback::open(std::string_view name)
{
    if(DeviceNames.empty())
        EnumerateDevices();

    auto deviceid = PaDeviceIndex{-1};
    if(name.empty())
    {
        if(const auto devidopt = ConfigValueI32({}, "port", "device"))
            deviceid = *devidopt;
        if(deviceid < 0 || std::cmp_greater_equal(deviceid, DeviceNames.size()))
            deviceid = Pa_GetDefaultOutputDevice();
        name = gsl::at(DeviceNames, deviceid).mName;
    }
    else
    {
        const auto iter = std::ranges::find_if(DeviceNames, [name](const DeviceEntry &entry)
        { return entry.mPlaybackChannels > 0 && name == entry.mName; });
        if(iter == DeviceNames.end())
            throw al::backend_exception{al::backend_error::NoDevice,
                "Device name \"{}\" not found", name};
        deviceid = gsl::narrow_cast<PaDeviceIndex>(std::distance(DeviceNames.begin(), iter));
    }

    createStream(deviceid);
    mDeviceIdx = deviceid;

    mDeviceName = name;
}

bool PortPlayback::reset()
{
    if(mStream)
    {
        if(const auto err = Pa_CloseStream(mStream); err != paNoError)
            ERR("Error closing stream: {}", Pa_GetErrorText(err));
        mStream = nullptr;
    }

    createStream(mDeviceIdx);

    switch(mParams.sampleFormat)
    {
    case paFloat32: mDevice->FmtType = DevFmtFloat; break;
    case paInt32: mDevice->FmtType = DevFmtInt; break;
    case paInt16: mDevice->FmtType = DevFmtShort; break;
    case paInt8: mDevice->FmtType = DevFmtByte; break;
    case paUInt8: mDevice->FmtType = DevFmtUByte; break;
    default:
        ERR("Unexpected PortAudio sample format: {}", mParams.sampleFormat);
        throw al::backend_exception{al::backend_error::NoDevice, "Invalid sample format: {}",
            mParams.sampleFormat};
    }

    if(mParams.channelCount != gsl::narrow_cast<int>(mDevice->channelsFromFmt()))
    {
        if(mParams.channelCount >= 2)
            mDevice->FmtChans = DevFmtStereo;
        else if(mParams.channelCount == 1)
            mDevice->FmtChans = DevFmtMono;
        mDevice->mAmbiOrder = 0;
    }

    const auto *streamInfo = Pa_GetStreamInfo(mStream);
    mDevice->mSampleRate = gsl::narrow_cast<unsigned>(std::lround(streamInfo->sampleRate));
    mDevice->mUpdateSize = mParams.updateSize;
    mDevice->mBufferSize = mDevice->mUpdateSize * 2u;
    if(streamInfo->outputLatency > 0.0f)
    {
        const auto sampleLatency = streamInfo->outputLatency * streamInfo->sampleRate;
        TRACE("Reported stream latency: {:f} sec ({:f} samples)", streamInfo->outputLatency,
            sampleLatency);
        mDevice->mBufferSize = gsl::narrow_cast<unsigned>(std::clamp(sampleLatency,
            gsl::narrow_cast<double>(mDevice->mBufferSize),
            double{std::numeric_limits<int>::max()}));
    }

    setDefaultChannelOrder();

    return true;
}

void PortPlayback::start()
{
    if(const auto err = Pa_StartStream(mStream); err != paNoError)
        throw al::backend_exception{al::backend_error::DeviceError, "Failed to start playback: {}",
            Pa_GetErrorText(err)};
}

void PortPlayback::stop()
{
    if(const auto err = Pa_StopStream(mStream); err != paNoError)
        ERR("Error stopping stream: {}", Pa_GetErrorText(err));
}


struct PortCapture final : public BackendBase {
    explicit PortCapture(gsl::not_null<DeviceBase*> device) noexcept : BackendBase{device} { }
    ~PortCapture() override;

    auto readCallback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo *timeInfo, const PaStreamCallbackFlags statusFlags) const
        noexcept -> int;

    void open(std::string_view name) override;
    void start() override;
    void stop() override;
    void captureSamples(std::span<std::byte> outbuffer) override;
    auto availableSamples() -> usize override;

    PaStream *mStream{nullptr};
    PaStreamParameters mParams{};

    RingBufferPtr<std::byte> mRing;
};

PortCapture::~PortCapture()
{
    if(const auto err = mStream ? Pa_CloseStream(mStream) : paNoError; err != paNoError)
        ERR("Error closing stream: {}", Pa_GetErrorText(err));
    mStream = nullptr;
}


auto PortCapture::readCallback(void const *const inputBuffer, void*,
    unsigned long const framesPerBuffer, PaStreamCallbackTimeInfo const*,
    PaStreamCallbackFlags) const noexcept -> int
{
    std::ignore = mRing->write(std::span{static_cast<const std::byte*>(inputBuffer),
        framesPerBuffer*mRing->getElemSize()});
    return 0;
}


void PortCapture::open(std::string_view name)
{
    if(DeviceNames.empty())
        EnumerateDevices();

    auto deviceid = PaDeviceIndex{};
    if(name.empty())
    {
        if(auto const devidopt = ConfigValueI32({}, "port", "capture"))
            deviceid = *devidopt;
        if(deviceid < 0 || std::cmp_greater_equal(deviceid, DeviceNames.size()))
            deviceid = Pa_GetDefaultInputDevice();
        name = gsl::at(DeviceNames, deviceid).mName;
    }
    else
    {
        auto const iter = std::ranges::find_if(DeviceNames, [name](DeviceEntry const &entry)
        { return entry.mCaptureChannels > 0 && name == entry.mName; });
        if(iter == DeviceNames.end())
            throw al::backend_exception{al::backend_error::NoDevice,
                "Device name \"{}\" not found", name};
        deviceid = gsl::narrow_cast<PaDeviceIndex>(std::distance(DeviceNames.begin(), iter));
    }

    const auto samples = std::max(mDevice->mBufferSize, mDevice->mSampleRate/10u);
    const auto frame_size = mDevice->frameSizeFromFmt();

    mRing = RingBuffer<std::byte>::Create(samples, frame_size, false);

    mParams.device = deviceid;
    mParams.suggestedLatency = 0.0f;
    mParams.hostApiSpecificStreamInfo = nullptr;

    switch(mDevice->FmtType)
    {
    case DevFmtByte: mParams.sampleFormat = paInt8; break;
    case DevFmtUByte: mParams.sampleFormat = paUInt8; break;
    case DevFmtShort: mParams.sampleFormat = paInt16; break;
    case DevFmtInt: mParams.sampleFormat = paInt32; break;
    case DevFmtFloat: mParams.sampleFormat = paFloat32; break;
    case DevFmtUInt:
    case DevFmtUShort:
        throw al::backend_exception{al::backend_error::DeviceError, "{} samples not supported",
            DevFmtTypeString(mDevice->FmtType)};
    }
    mParams.channelCount = gsl::narrow_cast<int>(mDevice->channelsFromFmt());

    static constexpr auto readCallback = [](const void *inputBuffer, void *outputBuffer,
        unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *timeInfo,
        const PaStreamCallbackFlags statusFlags, void *userData) noexcept -> int
    {
        return static_cast<PortCapture*>(userData)->readCallback(inputBuffer, outputBuffer,
            framesPerBuffer, timeInfo, statusFlags);
    };
    const auto err = Pa_OpenStream(&mStream, &mParams, nullptr, mDevice->mSampleRate,
        paFramesPerBufferUnspecified, paNoFlag, readCallback, this);
    if(err != paNoError)
        throw al::backend_exception{al::backend_error::NoDevice, "Failed to open stream: {}",
            Pa_GetErrorText(err)};

    mDeviceName = name;
}


void PortCapture::start()
{
    if(const auto err = Pa_StartStream(mStream); err != paNoError)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start recording: {}", Pa_GetErrorText(err)};
}

void PortCapture::stop()
{
    if(const auto err = Pa_StopStream(mStream); err != paNoError)
        ERR("Error stopping stream: {}", Pa_GetErrorText(err));
}


auto PortCapture::availableSamples() -> usize
{ return mRing->readSpace(); }

void PortCapture::captureSamples(std::span<std::byte> const outbuffer)
{ std::ignore = mRing->read(outbuffer); }

#ifdef _WIN32
# define PA_LIB "portaudio.dll"
#elif defined(__APPLE__) && defined(__MACH__)
# define PA_LIB "libportaudio.2.dylib"
#elif defined(__OpenBSD__)
# define PA_LIB "libportaudio.so"
#else
# define PA_LIB "libportaudio.so.2"
#endif

#if HAVE_DYNLOAD
OAL_ELF_NOTE_DLOPEN(
    "backend-portaudio",
    "Support for the PortAudio backend",
    OAL_ELF_NOTE_DLOPEN_PRIORITY_SUGGESTED,
    PA_LIB
);
#endif

} // namespace

auto PortBackendFactory::init() -> bool
{
#if HAVE_DYNLOAD
    if(!pa_handle)
    {
        auto *const pa_lib = gsl::czstring{PA_LIB};
        if(auto const libresult = LoadLib(pa_lib))
            pa_handle = libresult.value();
        else
        {
            WARN("Failed to load {}: {}", pa_lib, libresult.error());
            return false;
        }

        static constexpr auto load_func = [](auto *&func, gsl::czstring const name) -> bool
        {
            using func_t = std::remove_reference_t<decltype(func)>;
            auto const funcresult = GetSymbol(pa_handle, name);
            if(!funcresult)
            {
                WARN("Failed to load function {}: {}", name, funcresult.error());
                return false;
            }
            /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
            func = reinterpret_cast<func_t>(funcresult.value());
            return true;
        };
        auto ok = true;
#define LOAD_FUNC(f) ok &= load_func(p##f, #f)
        LOAD_FUNC(Pa_Initialize);
        LOAD_FUNC(Pa_Terminate);
        LOAD_FUNC(Pa_GetErrorText);
        LOAD_FUNC(Pa_StartStream);
        LOAD_FUNC(Pa_StopStream);
        LOAD_FUNC(Pa_OpenStream);
        LOAD_FUNC(Pa_CloseStream);
        LOAD_FUNC(Pa_GetDeviceCount);
        LOAD_FUNC(Pa_GetDeviceInfo);
        LOAD_FUNC(Pa_GetDefaultOutputDevice);
        LOAD_FUNC(Pa_GetDefaultInputDevice);
        LOAD_FUNC(Pa_GetStreamInfo);
#undef LOAD_FUNC
        if(!ok)
        {
            CloseLib(pa_handle);
            pa_handle = nullptr;
            return false;
        }

        if(const auto err = Pa_Initialize(); err != paNoError)
        {
            ERR("Pa_Initialize() returned an error: {}", Pa_GetErrorText(err));
            CloseLib(pa_handle);
            pa_handle = nullptr;
            return false;
        }
    }

#else

    if(const auto err = Pa_Initialize(); err != paNoError)
    {
        ERR("Pa_Initialize() returned an error: {}", Pa_GetErrorText(err));
        return false;
    }
#endif
    return true;
}

auto PortBackendFactory::querySupport(BackendType const type) -> bool
{ return (type == BackendType::Playback || type == BackendType::Capture); }

auto PortBackendFactory::enumerate(BackendType const type) -> std::vector<std::string>
{
    std::vector<std::string> devices;

    EnumerateDevices();
    auto defaultid = PaDeviceIndex{-1};
    switch(type)
    {
    case BackendType::Playback:
        defaultid = Pa_GetDefaultOutputDevice();
        if(auto const devidopt = ConfigValueI32({}, "port", "device"); devidopt && *devidopt >= 0
            && std::cmp_less(*devidopt, DeviceNames.size()))
            defaultid = *devidopt;

        for(auto const i : std::views::iota(0_uz, DeviceNames.size()))
        {
            if(DeviceNames[i].mPlaybackChannels > 0)
            {
                if(std::cmp_equal(defaultid, i))
                    devices.emplace(devices.cbegin(), DeviceNames[i].mName);
                else
                    devices.emplace_back(DeviceNames[i].mName);
            }
        }
        break;

    case BackendType::Capture:
        defaultid = Pa_GetDefaultInputDevice();
        if(auto const devidopt = ConfigValueI32({}, "port", "capture"); devidopt && *devidopt >= 0
            && std::cmp_less(*devidopt, DeviceNames.size()))
            defaultid = *devidopt;

        for(auto const i : std::views::iota(0_uz, DeviceNames.size()))
        {
            if(DeviceNames[i].mCaptureChannels > 0)
            {
                if(std::cmp_equal(defaultid, i))
                    devices.emplace(devices.cbegin(), DeviceNames[i].mName);
                else
                    devices.emplace_back(DeviceNames[i].mName);
            }
        }
        break;
    }

    return devices;
}

auto PortBackendFactory::createBackend(gsl::not_null<DeviceBase*> const device,
    BackendType const type) -> BackendPtr
{
    if(type == BackendType::Playback)
        return BackendPtr{new PortPlayback{device}};
    if(type == BackendType::Capture)
        return BackendPtr{new PortCapture{device}};
    return nullptr;
}

auto PortBackendFactory::getFactory() -> BackendFactory&
{
    static PortBackendFactory factory{};
    return factory;
}
