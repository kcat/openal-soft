/**
 * OpenAL cross platform audio library
 * Copyright (C) 2010 by Chris Robinson
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

#include "pipewire.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cerrno>
#include <memory>
#include <mutex>
#include <stdint.h>
#include <utility>

#include "albyte.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "alspan.h"
#include "core/devformat.h"
#include "core/device.h"
#include "core/helpers.h"
#include "core/logging.h"
#include "dynload.h"
#include "opthelpers.h"

/* Ignore warnings caused by PipeWire headers (lots in standard C++ mode). */
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Weverything\"")
#include "pipewire/pipewire.h"
#include "spa/buffer/buffer.h"
#include "spa/param/audio/format-utils.h"
#include "spa/param/audio/raw.h"
#include "spa/param/param.h"
#include "spa/pod/builder.h"
_Pragma("GCC diagnostic pop")

namespace {

using uint = unsigned int;

constexpr char pwireDevice[] = "PipeWire Output";


#ifdef HAVE_DYNLOAD
#define PWIRE_FUNCS(MAGIC)                                                    \
    MAGIC(pw_context_destroy)                                                 \
    MAGIC(pw_context_new)                                                     \
    MAGIC(pw_init)                                                            \
    MAGIC(pw_properties_free)                                                 \
    MAGIC(pw_properties_new)                                                  \
    MAGIC(pw_properties_set)                                                  \
    MAGIC(pw_properties_setf)                                                 \
    MAGIC(pw_stream_connect)                                                  \
    MAGIC(pw_stream_dequeue_buffer)                                           \
    MAGIC(pw_stream_destroy)                                                  \
    MAGIC(pw_stream_get_state)                                                \
    MAGIC(pw_stream_new_simple)                                               \
    MAGIC(pw_stream_queue_buffer)                                             \
    MAGIC(pw_stream_set_active)                                               \
    MAGIC(pw_thread_loop_new)                                                 \
    MAGIC(pw_thread_loop_destroy)                                             \
    MAGIC(pw_thread_loop_get_loop)                                            \
    MAGIC(pw_thread_loop_start)                                               \
    MAGIC(pw_thread_loop_stop)                                                \
    MAGIC(pw_thread_loop_lock)                                                \
    MAGIC(pw_thread_loop_wait)                                                \
    MAGIC(pw_thread_loop_signal)                                              \
    MAGIC(pw_thread_loop_unlock)                                              \

void *pwire_handle;
#define MAKE_FUNC(f) decltype(f) * p##f;
PWIRE_FUNCS(MAKE_FUNC)
#undef MAKE_FUNC

#ifndef IN_IDE_PARSER
#define pw_context_destroy ppw_context_destroy
#define pw_context_new ppw_context_new
#define pw_init ppw_init
#define pw_properties_free ppw_properties_free
#define pw_properties_new ppw_properties_new
#define pw_properties_set ppw_properties_set
#define pw_properties_setf ppw_properties_setf
#define pw_stream_connect ppw_stream_connect
#define pw_stream_dequeue_buffer ppw_stream_dequeue_buffer
#define pw_stream_destroy ppw_stream_destroy
#define pw_stream_get_state ppw_stream_get_state
#define pw_stream_new_simple ppw_stream_new_simple
#define pw_stream_queue_buffer ppw_stream_queue_buffer
#define pw_stream_set_active ppw_stream_set_active
#define pw_thread_loop_destroy ppw_thread_loop_destroy
#define pw_thread_loop_get_loop ppw_thread_loop_get_loop
#define pw_thread_loop_lock ppw_thread_loop_lock
#define pw_thread_loop_new ppw_thread_loop_new
#define pw_thread_loop_signal ppw_thread_loop_signal
#define pw_thread_loop_start ppw_thread_loop_start
#define pw_thread_loop_stop ppw_thread_loop_stop
#define pw_thread_loop_unlock ppw_thread_loop_unlock
#define pw_thread_loop_wait ppw_thread_loop_wait
#endif
#endif


bool pwire_load()
{
    bool error{false};

#ifdef HAVE_DYNLOAD
    if(!pwire_handle)
    {
        static constexpr char pwire_library[] = "libpipewire-0.3.so.0";
        std::string missing_funcs;

        pwire_handle = LoadLib(pwire_library);
        if(!pwire_handle)
        {
            WARN("Failed to load %s\n", pwire_library);
            return false;
        }

        error = false;
#define LOAD_FUNC(f) do {                                                     \
    p##f = reinterpret_cast<decltype(p##f)>(GetSymbol(pwire_handle, #f));     \
    if(p##f == nullptr) {                                                     \
        error = true;                                                         \
        missing_funcs += "\n" #f;                                             \
    }                                                                         \
} while(0);
        PWIRE_FUNCS(LOAD_FUNC)
#undef LOAD_FUNC

        if(error)
        {
            WARN("Missing expected functions:%s\n", missing_funcs.c_str());
            CloseLib(pwire_handle);
            pwire_handle = nullptr;
        }
    }
#endif

    return !error;
}


class ThreadMainloop {
    pw_thread_loop *mLoop{};

public:
    ThreadMainloop() = default;
    ThreadMainloop(const ThreadMainloop&) = delete;
    ThreadMainloop(ThreadMainloop&& rhs) noexcept : mLoop{rhs.mLoop} { rhs.mLoop = nullptr; }
    explicit ThreadMainloop(pw_thread_loop *loop) noexcept : mLoop{loop} { }
    ~ThreadMainloop() { if(mLoop) pw_thread_loop_destroy(mLoop); }

    ThreadMainloop& operator=(const ThreadMainloop&) = delete;
    ThreadMainloop& operator=(ThreadMainloop&& rhs) noexcept
    { std::swap(mLoop, rhs.mLoop); return *this; }

    operator bool() const noexcept { return mLoop != nullptr; }

    auto start() const { return pw_thread_loop_start(mLoop); }
    auto stop() const { return pw_thread_loop_stop(mLoop); }

    auto signal(bool wait) const { return pw_thread_loop_signal(mLoop, wait); }
    auto wait() const { return pw_thread_loop_wait(mLoop); }

    auto getLoop() const { return pw_thread_loop_get_loop(mLoop); }

    auto lock() const { return pw_thread_loop_lock(mLoop); }
    auto unlock() const { return pw_thread_loop_unlock(mLoop); }
};
using MainloopUniqueLock = std::unique_lock<ThreadMainloop>;
using MainloopLockGuard = std::lock_guard<ThreadMainloop>;

struct PwStreamDeleter {
    void operator()(pw_stream *stream) const { pw_stream_destroy(stream); }
};
using PwStreamPtr = std::unique_ptr<pw_stream,PwStreamDeleter>;


/* Enums for bitflags... again... *sigh* */
constexpr pw_stream_flags operator|(pw_stream_flags lhs, pw_stream_flags rhs) noexcept
{ return static_cast<pw_stream_flags>(lhs | uint{rhs}); }

/* Using PW_ID_ANY causes a compiler warning, so use our own variable with the
 * same type/value.
 */
constexpr uint32_t IdAny{0xffffffff};

/* SPA_POD_BUILDER_INIT causes a compiler warning, so make this function for
 * the same functionality.
 */
inline spa_pod_builder make_pod_builder(void *data, uint32_t size) noexcept
{
    spa_pod_builder ret{};
    spa_pod_builder_init(&ret, data, size);
    return ret;
}


enum use_f32p_e : bool { UseDevType=false, ForceF32Planar=true };
spa_audio_info_raw make_spa_info(DeviceBase *device, use_f32p_e use_f32p)
{
    static const spa_audio_channel MonoMap[]{
        SPA_AUDIO_CHANNEL_MONO
    }, StereoMap[] {
        SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR
    }, QuadMap[]{
        SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR, SPA_AUDIO_CHANNEL_RL, SPA_AUDIO_CHANNEL_RR
    }, X51Map[]{
        SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR, SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
        SPA_AUDIO_CHANNEL_SL, SPA_AUDIO_CHANNEL_SR
    }, X61Map[]{
        SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR, SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
        SPA_AUDIO_CHANNEL_RC, SPA_AUDIO_CHANNEL_SL, SPA_AUDIO_CHANNEL_SR
    }, X71Map[]{
        SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR, SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
        SPA_AUDIO_CHANNEL_RL, SPA_AUDIO_CHANNEL_RR, SPA_AUDIO_CHANNEL_SL, SPA_AUDIO_CHANNEL_SR
    };

    spa_audio_info_raw info{};
    if(use_f32p)
    {
        device->FmtType = DevFmtFloat;
        info.format = SPA_AUDIO_FORMAT_F32P;
    }
    else switch(device->FmtType)
    {
    case DevFmtByte: info.format = SPA_AUDIO_FORMAT_S8;
    case DevFmtUByte: info.format = SPA_AUDIO_FORMAT_U8;
    case DevFmtShort: info.format = SPA_AUDIO_FORMAT_S16;
    case DevFmtUShort: info.format = SPA_AUDIO_FORMAT_U16;
    case DevFmtInt: info.format = SPA_AUDIO_FORMAT_S32;
    case DevFmtUInt: info.format = SPA_AUDIO_FORMAT_U32;
    case DevFmtFloat: info.format = SPA_AUDIO_FORMAT_F32;
    }

    info.rate = device->Frequency;

    al::span<const spa_audio_channel> map{};
    switch(device->FmtChans)
    {
    case DevFmtMono: map = MonoMap; break;
    case DevFmtStereo: map = StereoMap; break;
    case DevFmtQuad: map = QuadMap; break;
    case DevFmtX51: map = X51Map; break;
    case DevFmtX61: map = X61Map; break;
    case DevFmtX71: map = X71Map; break;
    case DevFmtAmbi3D:
        info.flags |= SPA_AUDIO_FLAG_UNPOSITIONED;
        info.channels = device->channelsFromFmt();
        break;
    }
    if(!map.empty())
    {
        info.channels = static_cast<uint32_t>(map.size());
        std::copy(map.begin(), map.end(), info.position);
    }

    return info;
}

struct PipeWirePlayback final : public BackendBase {
    PipeWirePlayback(DeviceBase *device) noexcept : BackendBase{device} { }
    ~PipeWirePlayback();

    void stateChangedCallback(pw_stream_state old, pw_stream_state state, const char *error);
    static void stateChangedCallbackC(void *data, pw_stream_state old, pw_stream_state state,
        const char *error)
    { static_cast<PipeWirePlayback*>(data)->stateChangedCallback(old, state, error); }

    void outputCallback();
    static void outputCallbackC(void *data)
    { static_cast<PipeWirePlayback*>(data)->outputCallback(); }

    void open(const char *name) override;
    bool reset() override;
    void start() override;
    void stop() override;

    ThreadMainloop mLoop;
    PwStreamPtr mStream;
    std::unique_ptr<float*[]> mChannelPtrs;
    uint mNumChannels{};

    static const pw_stream_events sEvents;
    static constexpr pw_stream_events InitEvent()
    {
        pw_stream_events ret{};
        ret.version = PW_VERSION_STREAM_EVENTS;
        ret.state_changed = &PipeWirePlayback::stateChangedCallbackC;
        ret.process = &PipeWirePlayback::outputCallbackC;
        return ret;
    }

    DEF_NEWDEL(PipeWirePlayback)
};
const pw_stream_events PipeWirePlayback::sEvents{PipeWirePlayback::InitEvent()};

PipeWirePlayback::~PipeWirePlayback()
{
    if(mLoop && mStream)
    {
        /* The main loop needs to be locked when accessing/destroying the
         * stream from user threads.
         */
        MainloopLockGuard _{mLoop};
        mStream = nullptr;
    }
}


void PipeWirePlayback::stateChangedCallback(pw_stream_state, pw_stream_state, const char*)
{ mLoop.signal(false); }

void PipeWirePlayback::outputCallback()
{
    /* TODO: Should all buffers be filled? There can be more than one buffer to
     * dequeue, but example code only ever does one.
     */
    pw_buffer *pw_buf{pw_stream_dequeue_buffer(mStream.get())};
    if UNLIKELY(!pw_buf) return;

    spa_buffer *spa_buf{pw_buf->buffer};
    uint length{mDevice->UpdateSize};
    /* For planar formats, each datas[] seems to contain one channel, so store
     * the pointers in an array. Limit the render length in case the available
     * buffer length in any one channel is smaller than we wanted (shouldn't
     * be, but just in case).
     */
    const size_t chancount{minu(mNumChannels, spa_buf->n_datas)};
    for(size_t i{0};i < chancount;++i)
    {
        length = minu(length, spa_buf->datas[i].maxsize/sizeof(float));
        mChannelPtrs[i] = static_cast<float*>(spa_buf->datas[i].data);
    }

    /* TODO: How many samples should actually be written? 'maxsize' can be 16k
     * samples, which is excessive (~341ms @ 48khz), but aside from what gets
     * specified with PW_KEY_NODE_LATENCY, there's nothing here saying how much
     * is needed to keep the stream healthy.
     */
    mDevice->renderSamples({mChannelPtrs.get(), chancount}, length);

    for(size_t i{0};i < chancount;++i)
    {
        spa_buf->datas[i].chunk->offset = 0;
        spa_buf->datas[i].chunk->stride = sizeof(float);
        spa_buf->datas[i].chunk->size   = length * sizeof(float);
    }
    pw_stream_queue_buffer(mStream.get(), pw_buf);
}


void PipeWirePlayback::open(const char *name)
{
    static std::atomic<uint> OpenCount{0};

    if(!name)
        name = pwireDevice;
    else if(strcmp(name, pwireDevice) != 0)
        throw al::backend_exception{al::backend_error::NoDevice, "Device name \"%s\" not found",
            name};

    if(!mLoop)
    {
        const uint count{OpenCount.fetch_add(1, std::memory_order_relaxed)};
        const std::string thread_name{"ALSoftP" + std::to_string(count)};
        mLoop = ThreadMainloop{pw_thread_loop_new(thread_name.c_str(), nullptr)};
        if(!mLoop)
            throw al::backend_exception{al::backend_error::DeviceError,
                "Failed to create PipeWire mainloop (errno: %d)", errno};
        if(int res{mLoop.start()})
            throw al::backend_exception{al::backend_error::DeviceError,
                "Failed to start PipeWire mainloop (res: %d)", res};
    }

    mDevice->DeviceName = name;
}

bool PipeWirePlayback::reset()
{
    if(mStream)
    {
        MainloopLockGuard _{mLoop};
        mStream = nullptr;
    }

    /* TODO: Detect format from output device to avoid unnecessary conversions.
     * Force planar 32-bit float output for playback. This is what PipeWire
     * handles internally, and it's easier for us too.
     */
    spa_audio_info_raw info{make_spa_info(mDevice, ForceF32Planar)};

    /* TODO: How to tell what an appropriate size is? Examples just use this
     * magic value.
     */
    constexpr uint32_t pod_buffer_size{1024};
    auto pod_buffer = std::make_unique<al::byte[]>(pod_buffer_size);
    spa_pod_builder b{make_pod_builder(pod_buffer.get(), pod_buffer_size)};

    const spa_pod *params{spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info)};
    if(!params)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to set PipeWire audio format parameters"};

    pw_properties *props{pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_MEDIA_ROLE, "Game",
        PW_KEY_NODE_ALWAYS_PROCESS, "true",
        nullptr)};
    if(!props)
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to create PipeWire stream properties (errno: %d)", errno};

    auto&& binary = GetProcBinary();
    const char *appname{binary.fname.length() ? binary.fname.c_str() : "OpenAL Soft"};
    /* TODO: Which properties are actually needed here? Any others that could
     * be useful?
     */
    pw_properties_set(props, PW_KEY_NODE_NAME, appname);
    pw_properties_set(props, PW_KEY_NODE_DESCRIPTION, appname);
    pw_properties_setf(props, PW_KEY_NODE_LATENCY, "%u/%u", mDevice->UpdateSize,
        mDevice->Frequency);

    MainloopUniqueLock plock{mLoop};
    mStream = PwStreamPtr{pw_stream_new_simple(mLoop.getLoop(), "Playback Stream", props,
        &sEvents, this)};
    if(!mStream)
    {
        plock.unlock();
        pw_properties_free(props);
        throw al::backend_exception{al::backend_error::NoDevice,
            "Failed to create PipeWire stream (errno: %d)", errno};
    }

    static constexpr pw_stream_flags Flags{PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_INACTIVE
        | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS};
    if(int res{pw_stream_connect(mStream.get(), PW_DIRECTION_OUTPUT, IdAny, Flags, &params, 1)})
        throw al::backend_exception{al::backend_error::DeviceError,
            "Error connecting PipeWire stream (res: %d)", res};

    /* Wait for the stream to become paused (ready to start streaming). */
    pw_stream_state state{};
    const char *error{};
    while((state=pw_stream_get_state(mStream.get(), &error)) != PW_STREAM_STATE_PAUSED)
    {
        if(state == PW_STREAM_STATE_ERROR)
            throw al::backend_exception{al::backend_error::DeviceError,
                "Error connecting PipeWire stream: \"%s\"", error};
        mLoop.wait();
    }
    plock.unlock();

    mNumChannels = mDevice->channelsFromFmt();
    mChannelPtrs = std::make_unique<float*[]>(mNumChannels);

    setDefaultWFXChannelOrder();

    return true;
}

void PipeWirePlayback::start()
{
    MainloopLockGuard _{mLoop};
    if(int res{pw_stream_set_active(mStream.get(), true)})
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start PipeWire stream (res: %d)", res};
}

void PipeWirePlayback::stop()
{
    MainloopLockGuard _{mLoop};
    if(int res{pw_stream_set_active(mStream.get(), false)})
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to stop PipeWire stream (res: %d)", res};
}

} // namespace


bool PipeWireBackendFactory::init()
{
    if(!pwire_load())
        return false;

    pw_init(0, nullptr);

    /* TODO: Check that audio devices are supported. */

    return true;
}

bool PipeWireBackendFactory::querySupport(BackendType type)
{ return (type == BackendType::Playback); }

std::string PipeWireBackendFactory::probe(BackendType type)
{
    std::string outnames;
    switch(type)
    {
    case BackendType::Playback:
        /* Includes null char. */
        outnames.append(pwireDevice, sizeof(pwireDevice));
        break;
    case BackendType::Capture:
        break;
    }
    return outnames;
}

BackendPtr PipeWireBackendFactory::createBackend(DeviceBase *device, BackendType type)
{
    if(type == BackendType::Playback)
        return BackendPtr{new PipeWirePlayback{device}};
    return nullptr;
}

BackendFactory &PipeWireBackendFactory::getFactory()
{
    static PipeWireBackendFactory factory{};
    return factory;
}
