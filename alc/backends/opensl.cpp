/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* This is an OpenAL backend for Android using the native audio APIs based on
 * OpenSL ES 1.0.1. It is based on source code for the native-audio sample app
 * bundled with NDK.
 */

#include "config.h"

#include "opensl.h"

#include <jni.h>

#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <ranges>
#include <thread>
#include <functional>

#include "alnumeric.h"
#include "alstring.h"
#include "althrd_setname.h"
#include "core/device.h"
#include "core/helpers.h"
#include "core/logging.h"
#include "dynload.h"
#include "gsl/gsl"
#include "opthelpers.h"
#include "ringbuffer.h"

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <SLES/OpenSLES_AndroidConfiguration.h>


namespace {

using namespace std::string_view_literals;


#if HAVE_DYNLOAD
#define SLES_SYMBOLS(MAGIC)                 \
    MAGIC(slCreateEngine);                  \
    MAGIC(SL_IID_ANDROIDCONFIGURATION);     \
    MAGIC(SL_IID_ANDROIDSIMPLEBUFFERQUEUE); \
    MAGIC(SL_IID_ENGINE);                   \
    MAGIC(SL_IID_PLAY);                     \
    MAGIC(SL_IID_RECORD);

void *sles_handle;
#define MAKE_SYMBOL(f) decltype(f) * p##f
SLES_SYMBOLS(MAKE_SYMBOL)
#undef MAKE_SYMBOL

#ifndef IN_IDE_PARSER
#define slCreateEngine (*pslCreateEngine)
#define SL_IID_ANDROIDCONFIGURATION (*pSL_IID_ANDROIDCONFIGURATION)
#define SL_IID_ANDROIDSIMPLEBUFFERQUEUE (*pSL_IID_ANDROIDSIMPLEBUFFERQUEUE)
#define SL_IID_ENGINE (*pSL_IID_ENGINE)
#define SL_IID_PLAY (*pSL_IID_PLAY)
#define SL_IID_RECORD (*pSL_IID_RECORD)
#endif
#endif


/* Helper macros */
#define EXTRACT_VCALL_ARGS(...)  __VA_ARGS__))
#define VCALL(obj, func)  ((*(obj))->func((obj), EXTRACT_VCALL_ARGS
#define VCALL0(obj, func)  ((*(obj))->func((obj) EXTRACT_VCALL_ARGS


[[nodiscard]] constexpr auto GetDeviceName() noexcept { return "OpenSL"sv; }

[[nodiscard]]
constexpr auto GetChannelMask(DevFmtChannels chans) noexcept -> SLuint32
{
    switch(chans)
    {
    case DevFmtMono: return SL_SPEAKER_FRONT_CENTER;
    case DevFmtStereo: return SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
    case DevFmtQuad: return SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT |
        SL_SPEAKER_BACK_LEFT | SL_SPEAKER_BACK_RIGHT;
    case DevFmtX51: return SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT |
        SL_SPEAKER_FRONT_CENTER | SL_SPEAKER_LOW_FREQUENCY | SL_SPEAKER_SIDE_LEFT |
        SL_SPEAKER_SIDE_RIGHT;
    case DevFmtX61: return SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT |
        SL_SPEAKER_FRONT_CENTER | SL_SPEAKER_LOW_FREQUENCY | SL_SPEAKER_BACK_CENTER |
        SL_SPEAKER_SIDE_LEFT | SL_SPEAKER_SIDE_RIGHT;
    case DevFmtX71:
    case DevFmtX3D71: return SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT |
        SL_SPEAKER_FRONT_CENTER | SL_SPEAKER_LOW_FREQUENCY | SL_SPEAKER_BACK_LEFT |
        SL_SPEAKER_BACK_RIGHT | SL_SPEAKER_SIDE_LEFT | SL_SPEAKER_SIDE_RIGHT;
    case DevFmtX714: return SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT |
        SL_SPEAKER_FRONT_CENTER | SL_SPEAKER_LOW_FREQUENCY | SL_SPEAKER_BACK_LEFT |
        SL_SPEAKER_BACK_RIGHT | SL_SPEAKER_SIDE_LEFT | SL_SPEAKER_SIDE_RIGHT |
        SL_SPEAKER_TOP_FRONT_LEFT | SL_SPEAKER_TOP_FRONT_RIGHT | SL_SPEAKER_TOP_BACK_LEFT |
        SL_SPEAKER_TOP_BACK_RIGHT;
    case DevFmtX7144:
    case DevFmtAmbi3D:
        break;
    }
    return 0;
}

#ifdef SL_ANDROID_DATAFORMAT_PCM_EX
constexpr auto GetTypeRepresentation(DevFmtType type) noexcept -> SLuint32
{
    switch(type)
    {
    case DevFmtUByte:
    case DevFmtUShort:
    case DevFmtUInt:
        return SL_ANDROID_PCM_REPRESENTATION_UNSIGNED_INT;
    case DevFmtByte:
    case DevFmtShort:
    case DevFmtInt:
        return SL_ANDROID_PCM_REPRESENTATION_SIGNED_INT;
    case DevFmtFloat:
        return SL_ANDROID_PCM_REPRESENTATION_FLOAT;
    }
    return 0;
}
#endif

constexpr auto GetByteOrderEndianness() noexcept -> SLuint32
{
    if constexpr(std::endian::native == std::endian::little)
        return SL_BYTEORDER_LITTLEENDIAN;
    return SL_BYTEORDER_BIGENDIAN;
}

constexpr auto res_str(SLresult result) noexcept -> const char*
{
    switch(result)
    {
    case SL_RESULT_SUCCESS: return "Success";
    case SL_RESULT_PRECONDITIONS_VIOLATED: return "Preconditions violated";
    case SL_RESULT_PARAMETER_INVALID: return "Parameter invalid";
    case SL_RESULT_MEMORY_FAILURE: return "Memory failure";
    case SL_RESULT_RESOURCE_ERROR: return "Resource error";
    case SL_RESULT_RESOURCE_LOST: return "Resource lost";
    case SL_RESULT_IO_ERROR: return "I/O error";
    case SL_RESULT_BUFFER_INSUFFICIENT: return "Buffer insufficient";
    case SL_RESULT_CONTENT_CORRUPTED: return "Content corrupted";
    case SL_RESULT_CONTENT_UNSUPPORTED: return "Content unsupported";
    case SL_RESULT_CONTENT_NOT_FOUND: return "Content not found";
    case SL_RESULT_PERMISSION_DENIED: return "Permission denied";
    case SL_RESULT_FEATURE_UNSUPPORTED: return "Feature unsupported";
    case SL_RESULT_INTERNAL_ERROR: return "Internal error";
    case SL_RESULT_UNKNOWN_ERROR: return "Unknown error";
    case SL_RESULT_OPERATION_ABORTED: return "Operation aborted";
    case SL_RESULT_CONTROL_LOST: return "Control lost";
#ifdef SL_RESULT_READONLY
    case SL_RESULT_READONLY: return "ReadOnly";
#endif
#ifdef SL_RESULT_ENGINEOPTION_UNSUPPORTED
    case SL_RESULT_ENGINEOPTION_UNSUPPORTED: return "Engine option unsupported";
#endif
#ifdef SL_RESULT_SOURCE_SINK_INCOMPATIBLE
    case SL_RESULT_SOURCE_SINK_INCOMPATIBLE: return "Source/Sink incompatible";
#endif
    }
    return "Unknown error code";
}

inline void PrintErr(SLresult res, const char *str)
{
    if(res != SL_RESULT_SUCCESS) [[unlikely]]
        ERR("{}: {}", str, res_str(res));
}


struct OpenSLPlayback final : public BackendBase {
    explicit OpenSLPlayback(gsl::not_null<DeviceBase*> device) noexcept : BackendBase{device} { }
    ~OpenSLPlayback() override;

    void process(SLAndroidSimpleBufferQueueItf bq) noexcept;

    void mixerProc();

    void open(std::string_view name) override;
    auto reset() -> bool override;
    void start() override;
    void stop() override;
    auto getClockLatency() -> ClockLatency override;

    /* engine interfaces */
    SLObjectItf mEngineObj{nullptr};
    SLEngineItf mEngine{nullptr};

    /* output mix interfaces */
    SLObjectItf mOutputMix{nullptr};

    /* buffer queue player interfaces */
    SLObjectItf mBufferQueueObj{nullptr};

    RingBufferPtr<std::byte> mRing;
    std::atomic<bool> mSignal;

    std::mutex mMutex;

    u32 mFrameSize{0};

    std::atomic<bool> mKillNow{true};
    std::thread mThread;
};

OpenSLPlayback::~OpenSLPlayback()
{
    if(mBufferQueueObj)
        VCALL0(mBufferQueueObj,Destroy)();
    mBufferQueueObj = nullptr;

    if(mOutputMix)
        VCALL0(mOutputMix,Destroy)();
    mOutputMix = nullptr;

    if(mEngineObj)
        VCALL0(mEngineObj,Destroy)();
    mEngineObj = nullptr;
    mEngine = nullptr;
}


/* this callback handler is called every time a buffer finishes playing */
void OpenSLPlayback::process(SLAndroidSimpleBufferQueueItf) noexcept
{
    /* A note on the ringbuffer usage: The buffer queue seems to hold on to the
     * pointer passed to the Enqueue method, rather than copying the audio.
     * Consequently, the ringbuffer contains the audio that is currently queued
     * and waiting to play. This process() callback is called when a buffer is
     * finished, so we simply move the read pointer up to indicate the space is
     * available for writing again, and wake up the mixer thread to mix and
     * queue more audio.
     */
    mRing->readAdvance(1);

    mSignal.store(true, std::memory_order_release);
    mSignal.notify_all();
}

void OpenSLPlayback::mixerProc()
{
    SetRTPriority();
    althrd_setname(GetMixerThreadName());

    auto player = SLPlayItf{};
    auto bufferQueue = SLAndroidSimpleBufferQueueItf{};
    auto result = VCALL(mBufferQueueObj,GetInterface)(SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
        static_cast<void*>(&bufferQueue));
    PrintErr(result, "bufferQueue->GetInterface SL_IID_ANDROIDSIMPLEBUFFERQUEUE");
    if(SL_RESULT_SUCCESS == result)
    {
        result = VCALL(mBufferQueueObj,GetInterface)(SL_IID_PLAY, static_cast<void*>(&player));
        PrintErr(result, "bufferQueue->GetInterface SL_IID_PLAY");
    }

    const auto frame_step = usize{mDevice->channelsFromFmt()};

    if(SL_RESULT_SUCCESS != result)
        mDevice->handleDisconnect("Failed to get playback buffer: {:#08x}", result);

    while(SL_RESULT_SUCCESS == result && !mKillNow.load(std::memory_order_acquire)
        && mDevice->Connected.load(std::memory_order_acquire))
    {
        if(mRing->writeSpace() == 0)
        {
            auto state = SLuint32{0u};

            result = VCALL(player,GetPlayState)(&state);
            PrintErr(result, "player->GetPlayState");
            if(SL_RESULT_SUCCESS == result && state != SL_PLAYSTATE_PLAYING)
            {
                result = VCALL(player,SetPlayState)(SL_PLAYSTATE_PLAYING);
                PrintErr(result, "player->SetPlayState");
            }
            if(SL_RESULT_SUCCESS != result)
            {
                mDevice->handleDisconnect("Failed to start playback: {:#08x}", result);
                break;
            }

            if(mRing->writeSpace() == 0)
            {
                mSignal.wait(false, std::memory_order_acquire);
                mSignal.store(false, std::memory_order_release);
                continue;
            }
        }

        auto dlock = std::unique_lock{mMutex};
        auto data = mRing->getWriteVector();
        mDevice->renderSamples(data[0].data(), gsl::narrow_cast<u32>(data[0].size()/mFrameSize),
            frame_step);
        if(!data[1].empty())
            mDevice->renderSamples(data[1].data(),
                gsl::narrow_cast<u32>(data[1].size()/mFrameSize), frame_step);

        const auto updatebytes = mRing->getElemSize();
        const auto todo = usize{data[0].size() + data[1].size()} / updatebytes;
        mRing->writeAdvance(todo);
        dlock.unlock();

        for(usize i{0};i < todo;++i)
        {
            if(data[0].empty())
            {
                data[0] = data[1];
                data[1] = {};
            }

            result = VCALL(bufferQueue,Enqueue)(data[0].data(), updatebytes);
            PrintErr(result, "bufferQueue->Enqueue");
            if(SL_RESULT_SUCCESS != result)
            {
                mDevice->handleDisconnect("Failed to queue audio: {:#08x}", result);
                break;
            }

            data[0] = data[0].subspan(updatebytes);
        }
    }
}


void OpenSLPlayback::open(std::string_view name)
{
    if(name.empty())
        name = GetDeviceName();
    else if(name != GetDeviceName())
        throw al::backend_exception{al::backend_error::NoDevice, "Device name \"{}\" not found",
            name};

    /* There's only one device, so if it's already open, there's nothing to do. */
    if(mEngineObj) return;

    // create engine
    auto result = slCreateEngine(&mEngineObj, 0, nullptr, 0, nullptr, nullptr);
    PrintErr(result, "slCreateEngine");
    if(SL_RESULT_SUCCESS == result)
    {
        result = VCALL(mEngineObj,Realize)(SL_BOOLEAN_FALSE);
        PrintErr(result, "engine->Realize");
    }
    if(SL_RESULT_SUCCESS == result)
    {
        result = VCALL(mEngineObj,GetInterface)(SL_IID_ENGINE, static_cast<void*>(&mEngine));
        PrintErr(result, "engine->GetInterface");
    }
    if(SL_RESULT_SUCCESS == result)
    {
        result = VCALL(mEngine,CreateOutputMix)(&mOutputMix, 0, nullptr, nullptr);
        PrintErr(result, "engine->CreateOutputMix");
    }
    if(SL_RESULT_SUCCESS == result)
    {
        result = VCALL(mOutputMix,Realize)(SL_BOOLEAN_FALSE);
        PrintErr(result, "outputMix->Realize");
    }

    if(SL_RESULT_SUCCESS != result)
    {
        if(mOutputMix)
            VCALL0(mOutputMix,Destroy)();
        mOutputMix = nullptr;

        if(mEngineObj)
            VCALL0(mEngineObj,Destroy)();
        mEngineObj = nullptr;
        mEngine = nullptr;

        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to initialize OpenSL device: {:#08x}", result};
    }

    mDeviceName = name;
}

bool OpenSLPlayback::reset()
{
    auto result = SLresult{};

    if(mBufferQueueObj)
        VCALL0(mBufferQueueObj,Destroy)();
    mBufferQueueObj = nullptr;

    mRing = nullptr;

    mDevice->FmtChans = DevFmtStereo;
    mDevice->FmtType = DevFmtShort;

    setDefaultWFXChannelOrder();
    mFrameSize = mDevice->frameSizeFromFmt();


    const auto ids = std::array<SLInterfaceID,2>{SL_IID_ANDROIDSIMPLEBUFFERQUEUE, SL_IID_ANDROIDCONFIGURATION};
    const auto reqs = std::array<SLboolean,2>{SL_BOOLEAN_TRUE, SL_BOOLEAN_FALSE};

    auto loc_outmix = SLDataLocator_OutputMix{};
    loc_outmix.locatorType = SL_DATALOCATOR_OUTPUTMIX;
    loc_outmix.outputMix = mOutputMix;

    auto audioSnk = SLDataSink{};
    audioSnk.pLocator = &loc_outmix;
    audioSnk.pFormat = nullptr;

    auto loc_bufq = SLDataLocator_AndroidSimpleBufferQueue{};
    loc_bufq.locatorType = SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE;
    loc_bufq.numBuffers = mDevice->mBufferSize / mDevice->mUpdateSize;

    auto audioSrc = SLDataSource{};
#ifdef SL_ANDROID_DATAFORMAT_PCM_EX
    auto format_pcm_ex = SLAndroidDataFormat_PCM_EX{};
    format_pcm_ex.formatType = SL_ANDROID_DATAFORMAT_PCM_EX;
    format_pcm_ex.numChannels = mDevice->channelsFromFmt();
    format_pcm_ex.sampleRate = mDevice->mSampleRate * 1000;
    format_pcm_ex.bitsPerSample = mDevice->bytesFromFmt() * 8;
    format_pcm_ex.containerSize = format_pcm_ex.bitsPerSample;
    format_pcm_ex.channelMask = GetChannelMask(mDevice->FmtChans);
    format_pcm_ex.endianness = GetByteOrderEndianness();
    format_pcm_ex.representation = GetTypeRepresentation(mDevice->FmtType);

    audioSrc.pLocator = &loc_bufq;
    audioSrc.pFormat = &format_pcm_ex;

    result = VCALL(mEngine,CreateAudioPlayer)(&mBufferQueueObj, &audioSrc, &audioSnk, ids.size(),
        ids.data(), reqs.data());
    if(SL_RESULT_SUCCESS != result)
#endif
    {
        /* Alter sample type according to what SLDataFormat_PCM can support. */
        switch(mDevice->FmtType)
        {
        case DevFmtByte: mDevice->FmtType = DevFmtUByte; break;
        case DevFmtUInt: mDevice->FmtType = DevFmtInt; break;
        case DevFmtFloat:
        case DevFmtUShort: mDevice->FmtType = DevFmtShort; break;
        case DevFmtUByte:
        case DevFmtShort:
        case DevFmtInt:
            break;
        }

        auto format_pcm = SLDataFormat_PCM{};
        format_pcm.formatType = SL_DATAFORMAT_PCM;
        format_pcm.numChannels = mDevice->channelsFromFmt();
        format_pcm.samplesPerSec = mDevice->mSampleRate * 1000;
        format_pcm.bitsPerSample = mDevice->bytesFromFmt() * 8;
        format_pcm.containerSize = format_pcm.bitsPerSample;
        format_pcm.channelMask = GetChannelMask(mDevice->FmtChans);
        format_pcm.endianness = GetByteOrderEndianness();

        audioSrc.pLocator = &loc_bufq;
        audioSrc.pFormat = &format_pcm;

        result = VCALL(mEngine,CreateAudioPlayer)(&mBufferQueueObj, &audioSrc, &audioSnk, ids.size(),
            ids.data(), reqs.data());
        PrintErr(result, "engine->CreateAudioPlayer");
    }
    if(SL_RESULT_SUCCESS == result)
    {
        /* Set the stream type to "media" (games, music, etc), if possible. */
        auto config = SLAndroidConfigurationItf{};
        result = VCALL(mBufferQueueObj,GetInterface)(SL_IID_ANDROIDCONFIGURATION,
            static_cast<void*>(&config));
        PrintErr(result, "bufferQueue->GetInterface SL_IID_ANDROIDCONFIGURATION");
        if(SL_RESULT_SUCCESS == result)
        {
            auto streamType = SLint32{SL_ANDROID_STREAM_MEDIA};
            result = VCALL(config,SetConfiguration)(SL_ANDROID_KEY_STREAM_TYPE, &streamType,
                sizeof(streamType));
            PrintErr(result, "config->SetConfiguration");
        }

        /* Clear any error since this was optional. */
        result = SL_RESULT_SUCCESS;
    }
    if(SL_RESULT_SUCCESS == result)
    {
        result = VCALL(mBufferQueueObj,Realize)(SL_BOOLEAN_FALSE);
        PrintErr(result, "bufferQueue->Realize");
    }
    if(SL_RESULT_SUCCESS == result)
    {
        const auto num_updates = mDevice->mBufferSize / mDevice->mUpdateSize;
        mRing = RingBuffer<std::byte>::Create(num_updates, mFrameSize*mDevice->mUpdateSize, true);
    }

    if(SL_RESULT_SUCCESS != result)
    {
        if(mBufferQueueObj)
            VCALL0(mBufferQueueObj,Destroy)();
        mBufferQueueObj = nullptr;

        return false;
    }

    return true;
}

void OpenSLPlayback::start()
{
    mRing->reset();

    auto bufferQueue = SLAndroidSimpleBufferQueueItf{};
    auto result = VCALL(mBufferQueueObj,GetInterface)(SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
        static_cast<void*>(&bufferQueue));
    PrintErr(result, "bufferQueue->GetInterface");
    if(SL_RESULT_SUCCESS == result)
    {
        result = VCALL(bufferQueue,RegisterCallback)(
            [](SLAndroidSimpleBufferQueueItf bq, void *context) noexcept
            { static_cast<OpenSLPlayback*>(context)->process(bq); }, this);
        PrintErr(result, "bufferQueue->RegisterCallback");
    }
    if(SL_RESULT_SUCCESS != result)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to register callback: {:#08x}", result};

    try {
        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread(&OpenSLPlayback::mixerProc, this);
    }
    catch(std::exception& e) {
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start mixing thread: {}", e.what()};
    }
}

void OpenSLPlayback::stop()
{
    if(mKillNow.exchange(true, std::memory_order_acq_rel) || !mThread.joinable())
        return;

    mSignal.store(true, std::memory_order_release);
    mSignal.notify_all();
    mThread.join();

    auto player = SLPlayItf{};
    auto result = VCALL(mBufferQueueObj,GetInterface)(SL_IID_PLAY, static_cast<void*>(&player));
    PrintErr(result, "bufferQueue->GetInterface");
    if(SL_RESULT_SUCCESS == result)
    {
        result = VCALL(player,SetPlayState)(SL_PLAYSTATE_STOPPED);
        PrintErr(result, "player->SetPlayState");
    }

    auto bufferQueue = SLAndroidSimpleBufferQueueItf{};
    result = VCALL(mBufferQueueObj,GetInterface)(SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
        static_cast<void*>(&bufferQueue));
    PrintErr(result, "bufferQueue->GetInterface");
    if(SL_RESULT_SUCCESS == result)
    {
        result = VCALL0(bufferQueue,Clear)();
        PrintErr(result, "bufferQueue->Clear");
    }
    if(SL_RESULT_SUCCESS == result)
    {
        result = VCALL(bufferQueue,RegisterCallback)(nullptr, nullptr);
        PrintErr(result, "bufferQueue->RegisterCallback");
    }
    if(SL_RESULT_SUCCESS == result)
    {
        auto state = SLAndroidSimpleBufferQueueState{};
        do {
            std::this_thread::yield();
            result = VCALL(bufferQueue,GetState)(&state);
        } while(SL_RESULT_SUCCESS == result && state.count > 0);
        PrintErr(result, "bufferQueue->GetState");

        mRing->reset();
    }
}

ClockLatency OpenSLPlayback::getClockLatency()
{
    auto ret = ClockLatency{};

    auto dlock = std::lock_guard{mMutex};
    ret.ClockTime = mDevice->getClockTime();
    ret.Latency  = std::chrono::seconds{mRing->readSpace() * mDevice->mUpdateSize};
    ret.Latency /= mDevice->mSampleRate;

    return ret;
}


struct OpenSLCapture final : public BackendBase {
    explicit OpenSLCapture(gsl::not_null<DeviceBase*> device) noexcept : BackendBase{device} { }
    ~OpenSLCapture() override;

    void process(SLAndroidSimpleBufferQueueItf bq) const noexcept;

    void open(std::string_view name) override;
    void start() override;
    void stop() override;
    void captureSamples(std::span<std::byte> outbuffer) override;
    auto availableSamples() -> usize override;

    /* engine interfaces */
    SLObjectItf mEngineObj{nullptr};
    SLEngineItf mEngine{nullptr};

    /* recording interfaces */
    SLObjectItf mRecordObj{nullptr};

    RingBufferPtr<std::byte> mRing;
    u32 mByteOffset{0u};

    u32 mFrameSize{0u};
};

OpenSLCapture::~OpenSLCapture()
{
    if(mRecordObj)
        VCALL0(mRecordObj,Destroy)();
    mRecordObj = nullptr;

    if(mEngineObj)
        VCALL0(mEngineObj,Destroy)();
    mEngineObj = nullptr;
    mEngine = nullptr;
}


void OpenSLCapture::process(SLAndroidSimpleBufferQueueItf) const noexcept
{
    /* A new chunk has been written into the ring buffer, advance it. */
    mRing->writeAdvance(1);
}


void OpenSLCapture::open(std::string_view name)
{
    if(name.empty())
        name = GetDeviceName();
    else if(name != GetDeviceName())
        throw al::backend_exception{al::backend_error::NoDevice, "Device name \"{}\" not found",
            name};

    auto result = slCreateEngine(&mEngineObj, 0, nullptr, 0, nullptr, nullptr);
    PrintErr(result, "slCreateEngine");
    if(SL_RESULT_SUCCESS == result)
    {
        result = VCALL(mEngineObj,Realize)(SL_BOOLEAN_FALSE);
        PrintErr(result, "engine->Realize");
    }
    if(SL_RESULT_SUCCESS == result)
    {
        result = VCALL(mEngineObj,GetInterface)(SL_IID_ENGINE, static_cast<void*>(&mEngine));
        PrintErr(result, "engine->GetInterface");
    }
    if(SL_RESULT_SUCCESS == result)
    {
        mFrameSize = mDevice->frameSizeFromFmt();
        /* Ensure the total length is at least 100ms */
        auto length = std::max(mDevice->mBufferSize, mDevice->mSampleRate/10u);
        /* Ensure the per-chunk length is at least 10ms, and no more than 50ms. */
        auto update_len = std::clamp(mDevice->mBufferSize/3u, mDevice->mSampleRate/100u,
            mDevice->mSampleRate/100u*5u);
        auto num_updates = (length+update_len-1) / update_len;

        mRing = RingBuffer<std::byte>::Create(num_updates, update_len*mFrameSize, false);

        mDevice->mUpdateSize = update_len;
        mDevice->mBufferSize = gsl::narrow_cast<u32>(mRing->writeSpace() * update_len);
    }
    if(SL_RESULT_SUCCESS == result)
    {
        const auto ids = std::array<SLInterfaceID,2>{SL_IID_ANDROIDSIMPLEBUFFERQUEUE, SL_IID_ANDROIDCONFIGURATION};
        const auto reqs = std::array<SLboolean,2>{SL_BOOLEAN_TRUE, SL_BOOLEAN_FALSE};

        auto loc_dev = SLDataLocator_IODevice{};
        loc_dev.locatorType = SL_DATALOCATOR_IODEVICE;
        loc_dev.deviceType = SL_IODEVICE_AUDIOINPUT;
        loc_dev.deviceID = SL_DEFAULTDEVICEID_AUDIOINPUT;
        loc_dev.device = nullptr;

        auto audioSrc = SLDataSource{};
        audioSrc.pLocator = &loc_dev;
        audioSrc.pFormat = nullptr;

        auto loc_bq = SLDataLocator_AndroidSimpleBufferQueue{};
        loc_bq.locatorType = SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE;
        loc_bq.numBuffers = mDevice->mBufferSize / mDevice->mUpdateSize;

        auto audioSnk = SLDataSink{};
#ifdef SL_ANDROID_DATAFORMAT_PCM_EX
        auto format_pcm_ex = SLAndroidDataFormat_PCM_EX{};
        format_pcm_ex.formatType = SL_ANDROID_DATAFORMAT_PCM_EX;
        format_pcm_ex.numChannels = mDevice->channelsFromFmt();
        format_pcm_ex.sampleRate = mDevice->mSampleRate * 1000;
        format_pcm_ex.bitsPerSample = mDevice->bytesFromFmt() * 8;
        format_pcm_ex.containerSize = format_pcm_ex.bitsPerSample;
        format_pcm_ex.channelMask = GetChannelMask(mDevice->FmtChans);
        format_pcm_ex.endianness = GetByteOrderEndianness();
        format_pcm_ex.representation = GetTypeRepresentation(mDevice->FmtType);

        audioSnk.pLocator = &loc_bq;
        audioSnk.pFormat = &format_pcm_ex;
        result = VCALL(mEngine,CreateAudioRecorder)(&mRecordObj, &audioSrc, &audioSnk,
            ids.size(), ids.data(), reqs.data());
        if(SL_RESULT_SUCCESS != result)
#endif
        {
            /* Fallback to SLDataFormat_PCM only if it supports the desired
             * sample type.
             */
            if(mDevice->FmtType == DevFmtUByte || mDevice->FmtType == DevFmtShort
                || mDevice->FmtType == DevFmtInt)
            {
                auto format_pcm = SLDataFormat_PCM{};
                format_pcm.formatType = SL_DATAFORMAT_PCM;
                format_pcm.numChannels = mDevice->channelsFromFmt();
                format_pcm.samplesPerSec = mDevice->mSampleRate * 1000;
                format_pcm.bitsPerSample = mDevice->bytesFromFmt() * 8;
                format_pcm.containerSize = format_pcm.bitsPerSample;
                format_pcm.channelMask = GetChannelMask(mDevice->FmtChans);
                format_pcm.endianness = GetByteOrderEndianness();

                audioSnk.pLocator = &loc_bq;
                audioSnk.pFormat = &format_pcm;
                result = VCALL(mEngine,CreateAudioRecorder)(&mRecordObj, &audioSrc, &audioSnk,
                    ids.size(), ids.data(), reqs.data());
            }
            PrintErr(result, "engine->CreateAudioRecorder");
        }
    }
    if(SL_RESULT_SUCCESS == result)
    {
        /* Set the record preset to "generic", if possible. */
        auto config = SLAndroidConfigurationItf{};
        result = VCALL(mRecordObj,GetInterface)(SL_IID_ANDROIDCONFIGURATION,
            static_cast<void*>(&config));
        PrintErr(result, "recordObj->GetInterface SL_IID_ANDROIDCONFIGURATION");
        if(SL_RESULT_SUCCESS == result)
        {
            auto preset = SLuint32{SL_ANDROID_RECORDING_PRESET_GENERIC};
            result = VCALL(config,SetConfiguration)(SL_ANDROID_KEY_RECORDING_PRESET, &preset,
                sizeof(preset));
            PrintErr(result, "config->SetConfiguration");
        }

        /* Clear any error since this was optional. */
        result = SL_RESULT_SUCCESS;
    }
    if(SL_RESULT_SUCCESS == result)
    {
        result = VCALL(mRecordObj,Realize)(SL_BOOLEAN_FALSE);
        PrintErr(result, "recordObj->Realize");
    }

    auto bufferQueue = SLAndroidSimpleBufferQueueItf{};
    if(SL_RESULT_SUCCESS == result)
    {
        result = VCALL(mRecordObj,GetInterface)(SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
            static_cast<void*>(&bufferQueue));
        PrintErr(result, "recordObj->GetInterface");
    }
    if(SL_RESULT_SUCCESS == result)
    {
        result = VCALL(bufferQueue,RegisterCallback)(
            [](SLAndroidSimpleBufferQueueItf bq, void *context) noexcept
            { static_cast<OpenSLCapture*>(context)->process(bq); }, this);
        PrintErr(result, "bufferQueue->RegisterCallback");
    }
    if(SL_RESULT_SUCCESS == result)
    {
        const auto chunk_size = mDevice->mUpdateSize * mFrameSize;
        const auto silence = (mDevice->FmtType == DevFmtUByte) ? std::byte{0x80} : std::byte{0};

        auto data = mRing->getWriteVector();
        std::ranges::fill(data[0], silence);
        std::ranges::fill(data[1], silence);
        for(usize i{0u};i < data[0].size() && SL_RESULT_SUCCESS == result;i+=chunk_size)
        {
            result = VCALL(bufferQueue,Enqueue)(data[0].data() + i, chunk_size);
            PrintErr(result, "bufferQueue->Enqueue");
        }
        for(usize i{0u};i < data[1].size() && SL_RESULT_SUCCESS == result;i+=chunk_size)
        {
            result = VCALL(bufferQueue,Enqueue)(data[1].data() + i, chunk_size);
            PrintErr(result, "bufferQueue->Enqueue");
        }
    }

    if(SL_RESULT_SUCCESS != result)
    {
        if(mRecordObj)
            VCALL0(mRecordObj,Destroy)();
        mRecordObj = nullptr;

        if(mEngineObj)
            VCALL0(mEngineObj,Destroy)();
        mEngineObj = nullptr;
        mEngine = nullptr;

        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to initialize OpenSL device: {:#08x}", result};
    }

    mDeviceName = name;
}

void OpenSLCapture::start()
{
    auto record = SLRecordItf{};
    auto result = VCALL(mRecordObj,GetInterface)(SL_IID_RECORD, static_cast<void*>(&record));
    PrintErr(result, "recordObj->GetInterface");

    if(SL_RESULT_SUCCESS == result)
    {
        result = VCALL(record,SetRecordState)(SL_RECORDSTATE_RECORDING);
        PrintErr(result, "record->SetRecordState");
    }
    if(SL_RESULT_SUCCESS != result)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start capture: {:#08x}", result};
}

void OpenSLCapture::stop()
{
    auto record = SLRecordItf{};
    auto result = VCALL(mRecordObj,GetInterface)(SL_IID_RECORD, static_cast<void*>(&record));
    PrintErr(result, "recordObj->GetInterface");

    if(SL_RESULT_SUCCESS == result)
    {
        result = VCALL(record,SetRecordState)(SL_RECORDSTATE_PAUSED);
        PrintErr(result, "record->SetRecordState");
    }
}

void OpenSLCapture::captureSamples(std::span<std::byte> outbuffer)
{
    const auto update_size = usize{mDevice->mUpdateSize};
    const auto chunk_size = update_size * mFrameSize;

    auto bufferQueue = SLAndroidSimpleBufferQueueItf{};
    if(mDevice->Connected.load(std::memory_order_acquire)) [[likely]]
    {
        auto const result = VCALL(mRecordObj,GetInterface)(SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
            static_cast<void*>(&bufferQueue));
        PrintErr(result, "recordObj->GetInterface");
        if(SL_RESULT_SUCCESS != result) [[unlikely]]
        {
            mDevice->handleDisconnect("Failed to get capture buffer queue: {:#08x}", result);
            bufferQueue = nullptr;
        }
    }

    /* Read the desired samples from the ring buffer then advance its read
     * pointer.
     */
    auto rdata = mRing->getReadVector();
    while(!outbuffer.empty())
    {
        auto const rem = std::min(outbuffer.size(), usize{chunk_size}-mByteOffset);
        auto const oiter = std::ranges::copy(rdata[0].subspan(mByteOffset, rem),
            outbuffer.begin()).out;
        outbuffer = {oiter, outbuffer.end()};

        mByteOffset += rem;
        if(mByteOffset == chunk_size)
        {
            /* Finished a chunk, reset the offset and advance the read pointer. */
            mByteOffset = 0;

            mRing->readAdvance(1);
            if(bufferQueue)
            {
                auto const result = VCALL(bufferQueue,Enqueue)(rdata[0].data(), chunk_size);
                PrintErr(result, "bufferQueue->Enqueue");
                if(SL_RESULT_SUCCESS != result) [[unlikely]]
                {
                    bufferQueue = nullptr;
                    mDevice->handleDisconnect("Failed to queue capture buffer: {:#08x}", result);
                }
            }

            rdata[0] = rdata[0].subspan(chunk_size);
            if(rdata[0].empty())
                rdata[0] = rdata[1];
        }
    }
}

auto OpenSLCapture::availableSamples() -> usize
{
    return mRing->readSpace()*mDevice->mUpdateSize - mByteOffset/mFrameSize;
}

#define SLES_LIB "libOpenSLES.so"

#if HAVE_DYNLOAD
OAL_ELF_NOTE_DLOPEN(
    "backend-opensl",
    "Support for the OpenSL backend",
    OAL_ELF_NOTE_DLOPEN_PRIORITY_SUGGESTED,
    SLES_LIB
);
#endif

} // namespace

auto OSLBackendFactory::init() -> bool
{
#if HAVE_DYNLOAD
    if(!sles_handle)
    {
        auto *const sles_lib = gsl::czstring{SLES_LIB};
        if(auto const libresult = LoadLib(sles_lib))
            sles_handle = libresult.value();
        else
        {
            WARN("Failed to load {}: {}", sles_lib, libresult.error());
            return false;
        }

        static constexpr auto load_func = [](auto *&func, gsl::czstring const name) -> bool
        {
            using func_t = std::remove_reference_t<decltype(func)>;
            auto const funcresult = GetSymbol(sles_handle, name);
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
        SLES_SYMBOLS(LOAD_FUNC)
#undef LOAD_FUNC
        if(!ok)
        {
            CloseLib(sles_handle);
            sles_handle = nullptr;
            return false;
        }
    }
#endif

    return true;
}

auto OSLBackendFactory::querySupport(BackendType const type) -> bool
{ return (type == BackendType::Playback || type == BackendType::Capture); }

auto OSLBackendFactory::enumerate(BackendType const type) -> std::vector<std::string>
{
    switch(type)
    {
    case BackendType::Playback:
    case BackendType::Capture:
        return std::vector{std::string{GetDeviceName()}};
    }
    return {};
}

auto OSLBackendFactory::createBackend(gsl::not_null<DeviceBase*> const device,
    BackendType const type) -> BackendPtr
{
    if(type == BackendType::Playback)
        return BackendPtr{new OpenSLPlayback{device}};
    if(type == BackendType::Capture)
        return BackendPtr{new OpenSLCapture{device}};
    return nullptr;
}

auto OSLBackendFactory::getFactory() -> BackendFactory&
{
    static OSLBackendFactory factory{};
    return factory;
}
