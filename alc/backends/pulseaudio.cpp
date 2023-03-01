/**
 * OpenAL cross platform audio library
 * Copyright (C) 2009 by Konstantinos Natsakis <konstantinos.natsakis@gmail.com>
 * Copyright (C) 2010 by Chris Robinson <chris.kcat@gmail.com>
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

#include "pulseaudio.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <sys/types.h>
#include <utility>

#include "albyte.h"
#include "alc/alconfig.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "aloptional.h"
#include "alspan.h"
#include "core/devformat.h"
#include "core/device.h"
#include "core/logging.h"
#include "dynload.h"
#include "opthelpers.h"
#include "strutils.h"
#include "vector.h"

#include <pulse/pulseaudio.h>


namespace {

using uint = unsigned int;

#ifdef HAVE_DYNLOAD
#define PULSE_FUNCS(MAGIC)                                                    \
    MAGIC(pa_context_new);                                                    \
    MAGIC(pa_context_unref);                                                  \
    MAGIC(pa_context_get_state);                                              \
    MAGIC(pa_context_disconnect);                                             \
    MAGIC(pa_context_set_state_callback);                                     \
    MAGIC(pa_context_errno);                                                  \
    MAGIC(pa_context_connect);                                                \
    MAGIC(pa_context_get_server_info);                                        \
    MAGIC(pa_context_get_sink_info_by_name);                                  \
    MAGIC(pa_context_get_sink_info_list);                                     \
    MAGIC(pa_context_get_source_info_by_name);                                \
    MAGIC(pa_context_get_source_info_list);                                   \
    MAGIC(pa_stream_new);                                                     \
    MAGIC(pa_stream_unref);                                                   \
    MAGIC(pa_stream_drop);                                                    \
    MAGIC(pa_stream_get_state);                                               \
    MAGIC(pa_stream_peek);                                                    \
    MAGIC(pa_stream_write);                                                   \
    MAGIC(pa_stream_connect_record);                                          \
    MAGIC(pa_stream_connect_playback);                                        \
    MAGIC(pa_stream_readable_size);                                           \
    MAGIC(pa_stream_writable_size);                                           \
    MAGIC(pa_stream_is_corked);                                               \
    MAGIC(pa_stream_cork);                                                    \
    MAGIC(pa_stream_is_suspended);                                            \
    MAGIC(pa_stream_get_device_name);                                         \
    MAGIC(pa_stream_get_latency);                                             \
    MAGIC(pa_stream_set_write_callback);                                      \
    MAGIC(pa_stream_set_buffer_attr);                                         \
    MAGIC(pa_stream_get_buffer_attr);                                         \
    MAGIC(pa_stream_get_sample_spec);                                         \
    MAGIC(pa_stream_get_time);                                                \
    MAGIC(pa_stream_set_read_callback);                                       \
    MAGIC(pa_stream_set_state_callback);                                      \
    MAGIC(pa_stream_set_moved_callback);                                      \
    MAGIC(pa_stream_set_underflow_callback);                                  \
    MAGIC(pa_stream_new_with_proplist);                                       \
    MAGIC(pa_stream_disconnect);                                              \
    MAGIC(pa_stream_set_buffer_attr_callback);                                \
    MAGIC(pa_stream_begin_write);                                             \
    MAGIC(pa_threaded_mainloop_free);                                         \
    MAGIC(pa_threaded_mainloop_get_api);                                      \
    MAGIC(pa_threaded_mainloop_lock);                                         \
    MAGIC(pa_threaded_mainloop_new);                                          \
    MAGIC(pa_threaded_mainloop_signal);                                       \
    MAGIC(pa_threaded_mainloop_start);                                        \
    MAGIC(pa_threaded_mainloop_stop);                                         \
    MAGIC(pa_threaded_mainloop_unlock);                                       \
    MAGIC(pa_threaded_mainloop_wait);                                         \
    MAGIC(pa_channel_map_init_auto);                                          \
    MAGIC(pa_channel_map_parse);                                              \
    MAGIC(pa_channel_map_snprint);                                            \
    MAGIC(pa_channel_map_equal);                                              \
    MAGIC(pa_channel_map_superset);                                           \
    MAGIC(pa_channel_position_to_string);                                     \
    MAGIC(pa_operation_get_state);                                            \
    MAGIC(pa_operation_unref);                                                \
    MAGIC(pa_sample_spec_valid);                                              \
    MAGIC(pa_frame_size);                                                     \
    MAGIC(pa_strerror);                                                       \
    MAGIC(pa_path_get_filename);                                              \
    MAGIC(pa_get_binary_name);                                                \
    MAGIC(pa_xmalloc);                                                        \
    MAGIC(pa_xfree);

void *pulse_handle;
#define MAKE_FUNC(x) decltype(x) * p##x
PULSE_FUNCS(MAKE_FUNC)
#undef MAKE_FUNC

#ifndef IN_IDE_PARSER
#define pa_context_new ppa_context_new
#define pa_context_unref ppa_context_unref
#define pa_context_get_state ppa_context_get_state
#define pa_context_disconnect ppa_context_disconnect
#define pa_context_set_state_callback ppa_context_set_state_callback
#define pa_context_errno ppa_context_errno
#define pa_context_connect ppa_context_connect
#define pa_context_get_server_info ppa_context_get_server_info
#define pa_context_get_sink_info_by_name ppa_context_get_sink_info_by_name
#define pa_context_get_sink_info_list ppa_context_get_sink_info_list
#define pa_context_get_source_info_by_name ppa_context_get_source_info_by_name
#define pa_context_get_source_info_list ppa_context_get_source_info_list
#define pa_stream_new ppa_stream_new
#define pa_stream_unref ppa_stream_unref
#define pa_stream_disconnect ppa_stream_disconnect
#define pa_stream_drop ppa_stream_drop
#define pa_stream_set_write_callback ppa_stream_set_write_callback
#define pa_stream_set_buffer_attr ppa_stream_set_buffer_attr
#define pa_stream_get_buffer_attr ppa_stream_get_buffer_attr
#define pa_stream_get_sample_spec ppa_stream_get_sample_spec
#define pa_stream_get_time ppa_stream_get_time
#define pa_stream_set_read_callback ppa_stream_set_read_callback
#define pa_stream_set_state_callback ppa_stream_set_state_callback
#define pa_stream_set_moved_callback ppa_stream_set_moved_callback
#define pa_stream_set_underflow_callback ppa_stream_set_underflow_callback
#define pa_stream_connect_record ppa_stream_connect_record
#define pa_stream_connect_playback ppa_stream_connect_playback
#define pa_stream_readable_size ppa_stream_readable_size
#define pa_stream_writable_size ppa_stream_writable_size
#define pa_stream_is_corked ppa_stream_is_corked
#define pa_stream_cork ppa_stream_cork
#define pa_stream_is_suspended ppa_stream_is_suspended
#define pa_stream_get_device_name ppa_stream_get_device_name
#define pa_stream_get_latency ppa_stream_get_latency
#define pa_stream_set_buffer_attr_callback ppa_stream_set_buffer_attr_callback
#define pa_stream_begin_write ppa_stream_begin_write
#define pa_threaded_mainloop_free ppa_threaded_mainloop_free
#define pa_threaded_mainloop_get_api ppa_threaded_mainloop_get_api
#define pa_threaded_mainloop_lock ppa_threaded_mainloop_lock
#define pa_threaded_mainloop_new ppa_threaded_mainloop_new
#define pa_threaded_mainloop_signal ppa_threaded_mainloop_signal
#define pa_threaded_mainloop_start ppa_threaded_mainloop_start
#define pa_threaded_mainloop_stop ppa_threaded_mainloop_stop
#define pa_threaded_mainloop_unlock ppa_threaded_mainloop_unlock
#define pa_threaded_mainloop_wait ppa_threaded_mainloop_wait
#define pa_channel_map_init_auto ppa_channel_map_init_auto
#define pa_channel_map_parse ppa_channel_map_parse
#define pa_channel_map_snprint ppa_channel_map_snprint
#define pa_channel_map_equal ppa_channel_map_equal
#define pa_channel_map_superset ppa_channel_map_superset
#define pa_channel_position_to_string ppa_channel_position_to_string
#define pa_operation_get_state ppa_operation_get_state
#define pa_operation_unref ppa_operation_unref
#define pa_sample_spec_valid ppa_sample_spec_valid
#define pa_frame_size ppa_frame_size
#define pa_strerror ppa_strerror
#define pa_stream_get_state ppa_stream_get_state
#define pa_stream_peek ppa_stream_peek
#define pa_stream_write ppa_stream_write
#define pa_xfree ppa_xfree
#define pa_path_get_filename ppa_path_get_filename
#define pa_get_binary_name ppa_get_binary_name
#define pa_xmalloc ppa_xmalloc
#endif /* IN_IDE_PARSER */

#endif


constexpr pa_channel_map MonoChanMap{
    1, {PA_CHANNEL_POSITION_MONO}
}, StereoChanMap{
    2, {PA_CHANNEL_POSITION_FRONT_LEFT, PA_CHANNEL_POSITION_FRONT_RIGHT}
}, QuadChanMap{
    4, {
        PA_CHANNEL_POSITION_FRONT_LEFT, PA_CHANNEL_POSITION_FRONT_RIGHT,
        PA_CHANNEL_POSITION_REAR_LEFT, PA_CHANNEL_POSITION_REAR_RIGHT
    }
}, X51ChanMap{
    6, {
        PA_CHANNEL_POSITION_FRONT_LEFT, PA_CHANNEL_POSITION_FRONT_RIGHT,
        PA_CHANNEL_POSITION_FRONT_CENTER, PA_CHANNEL_POSITION_LFE,
        PA_CHANNEL_POSITION_SIDE_LEFT, PA_CHANNEL_POSITION_SIDE_RIGHT
    }
}, X51RearChanMap{
    6, {
        PA_CHANNEL_POSITION_FRONT_LEFT, PA_CHANNEL_POSITION_FRONT_RIGHT,
        PA_CHANNEL_POSITION_FRONT_CENTER, PA_CHANNEL_POSITION_LFE,
        PA_CHANNEL_POSITION_REAR_LEFT, PA_CHANNEL_POSITION_REAR_RIGHT
    }
}, X61ChanMap{
    7, {
        PA_CHANNEL_POSITION_FRONT_LEFT, PA_CHANNEL_POSITION_FRONT_RIGHT,
        PA_CHANNEL_POSITION_FRONT_CENTER, PA_CHANNEL_POSITION_LFE,
        PA_CHANNEL_POSITION_REAR_CENTER,
        PA_CHANNEL_POSITION_SIDE_LEFT, PA_CHANNEL_POSITION_SIDE_RIGHT
    }
}, X71ChanMap{
    8, {
        PA_CHANNEL_POSITION_FRONT_LEFT, PA_CHANNEL_POSITION_FRONT_RIGHT,
        PA_CHANNEL_POSITION_FRONT_CENTER, PA_CHANNEL_POSITION_LFE,
        PA_CHANNEL_POSITION_REAR_LEFT, PA_CHANNEL_POSITION_REAR_RIGHT,
        PA_CHANNEL_POSITION_SIDE_LEFT, PA_CHANNEL_POSITION_SIDE_RIGHT
    }
}, X714ChanMap{
    12, {
        PA_CHANNEL_POSITION_FRONT_LEFT, PA_CHANNEL_POSITION_FRONT_RIGHT,
        PA_CHANNEL_POSITION_FRONT_CENTER, PA_CHANNEL_POSITION_LFE,
        PA_CHANNEL_POSITION_REAR_LEFT, PA_CHANNEL_POSITION_REAR_RIGHT,
        PA_CHANNEL_POSITION_SIDE_LEFT, PA_CHANNEL_POSITION_SIDE_RIGHT,
        PA_CHANNEL_POSITION_TOP_FRONT_LEFT, PA_CHANNEL_POSITION_TOP_FRONT_RIGHT,
        PA_CHANNEL_POSITION_TOP_REAR_LEFT, PA_CHANNEL_POSITION_TOP_REAR_RIGHT
    }
};


/* *grumble* Don't use enums for bitflags. */
constexpr pa_stream_flags_t operator|(pa_stream_flags_t lhs, pa_stream_flags_t rhs)
{ return pa_stream_flags_t(lhs | al::to_underlying(rhs)); }
constexpr pa_stream_flags_t& operator|=(pa_stream_flags_t &lhs, pa_stream_flags_t rhs)
{
    lhs = lhs | rhs;
    return lhs;
}
constexpr pa_stream_flags_t operator~(pa_stream_flags_t flag)
{ return pa_stream_flags_t(~al::to_underlying(flag)); }
constexpr pa_stream_flags_t& operator&=(pa_stream_flags_t &lhs, pa_stream_flags_t rhs)
{
    lhs = pa_stream_flags_t(al::to_underlying(lhs) & rhs);
    return lhs;
}

constexpr pa_context_flags_t operator|(pa_context_flags_t lhs, pa_context_flags_t rhs)
{ return pa_context_flags_t(lhs | al::to_underlying(rhs)); }
constexpr pa_context_flags_t& operator|=(pa_context_flags_t &lhs, pa_context_flags_t rhs)
{
    lhs = lhs | rhs;
    return lhs;
}


struct DevMap {
    std::string name;
    std::string device_name;
};

bool checkName(const al::span<const DevMap> list, const std::string &name)
{
    auto match_name = [&name](const DevMap &entry) -> bool { return entry.name == name; };
    return std::find_if(list.cbegin(), list.cend(), match_name) != list.cend();
}

al::vector<DevMap> PlaybackDevices;
al::vector<DevMap> CaptureDevices;


/* Global flags and properties */
pa_context_flags_t pulse_ctx_flags;

class PulseMainloop {
    pa_threaded_mainloop *mLoop{};

public:
    PulseMainloop() = default;
    PulseMainloop(const PulseMainloop&) = delete;
    PulseMainloop(PulseMainloop&& rhs) noexcept : mLoop{rhs.mLoop} { rhs.mLoop = nullptr; }
    explicit PulseMainloop(pa_threaded_mainloop *loop) noexcept : mLoop{loop} { }
    ~PulseMainloop() { if(mLoop) pa_threaded_mainloop_free(mLoop); }

    PulseMainloop& operator=(const PulseMainloop&) = delete;
    PulseMainloop& operator=(PulseMainloop&& rhs) noexcept
    { std::swap(mLoop, rhs.mLoop); return *this; }
    PulseMainloop& operator=(std::nullptr_t) noexcept
    {
        if(mLoop)
            pa_threaded_mainloop_free(mLoop);
        mLoop = nullptr;
        return *this;
    }

    explicit operator bool() const noexcept { return mLoop != nullptr; }

    auto start() const { return pa_threaded_mainloop_start(mLoop); }
    auto stop() const { return pa_threaded_mainloop_stop(mLoop); }

    auto getApi() const { return pa_threaded_mainloop_get_api(mLoop); }

    auto lock() const { return pa_threaded_mainloop_lock(mLoop); }
    auto unlock() const { return pa_threaded_mainloop_unlock(mLoop); }

    auto signal(bool wait=false) const { return pa_threaded_mainloop_signal(mLoop, wait); }

    static auto Create() { return PulseMainloop{pa_threaded_mainloop_new()}; }


    void streamSuccessCallback(pa_stream*, int) noexcept { signal(); }
    static void streamSuccessCallbackC(pa_stream *stream, int success, void *pdata) noexcept
    { static_cast<PulseMainloop*>(pdata)->streamSuccessCallback(stream, success); }

    void close(pa_context *context, pa_stream *stream=nullptr);


    void deviceSinkCallback(pa_context*, const pa_sink_info *info, int eol) noexcept
    {
        if(eol)
        {
            signal();
            return;
        }

        /* Skip this device is if it's already in the list. */
        auto match_devname = [info](const DevMap &entry) -> bool
        { return entry.device_name == info->name; };
        if(std::find_if(PlaybackDevices.cbegin(), PlaybackDevices.cend(), match_devname) != PlaybackDevices.cend())
            return;

        /* Make sure the display name (description) is unique. Append a number
         * counter as needed.
         */
        int count{1};
        std::string newname{info->description};
        while(checkName(PlaybackDevices, newname))
        {
            newname = info->description;
            newname += " #";
            newname += std::to_string(++count);
        }
        PlaybackDevices.emplace_back(DevMap{std::move(newname), info->name});
        DevMap &newentry = PlaybackDevices.back();

        TRACE("Got device \"%s\", \"%s\"\n", newentry.name.c_str(), newentry.device_name.c_str());
    }

    void deviceSourceCallback(pa_context*, const pa_source_info *info, int eol) noexcept
    {
        if(eol)
        {
            signal();
            return;
        }

        /* Skip this device is if it's already in the list. */
        auto match_devname = [info](const DevMap &entry) -> bool
        { return entry.device_name == info->name; };
        if(std::find_if(CaptureDevices.cbegin(), CaptureDevices.cend(), match_devname) != CaptureDevices.cend())
            return;

        /* Make sure the display name (description) is unique. Append a number
         * counter as needed.
         */
        int count{1};
        std::string newname{info->description};
        while(checkName(CaptureDevices, newname))
        {
            newname = info->description;
            newname += " #";
            newname += std::to_string(++count);
        }
        CaptureDevices.emplace_back(DevMap{std::move(newname), info->name});
        DevMap &newentry = CaptureDevices.back();

        TRACE("Got device \"%s\", \"%s\"\n", newentry.name.c_str(), newentry.device_name.c_str());
    }

    void probePlaybackDevices();
    void probeCaptureDevices();

    friend struct MainloopUniqueLock;
};
struct MainloopUniqueLock : public std::unique_lock<PulseMainloop> {
    using std::unique_lock<PulseMainloop>::unique_lock;
    MainloopUniqueLock& operator=(MainloopUniqueLock&&) = default;

    auto wait() const -> void
    { pa_threaded_mainloop_wait(mutex()->mLoop); }

    template<typename Predicate>
    auto wait(Predicate done_waiting) const -> void
    { while(!done_waiting()) wait(); }

    void waitForOperation(pa_operation *op)
    {
        if(op)
        {
            wait([op]{ return pa_operation_get_state(op) != PA_OPERATION_RUNNING; });
            pa_operation_unref(op);
        }
    }


    void contextStateCallback(pa_context *context) noexcept
    {
        pa_context_state_t state{pa_context_get_state(context)};
        if(state == PA_CONTEXT_READY || !PA_CONTEXT_IS_GOOD(state))
            mutex()->signal();
    }

    void streamStateCallback(pa_stream *stream) noexcept
    {
        pa_stream_state_t state{pa_stream_get_state(stream)};
        if(state == PA_STREAM_READY || !PA_STREAM_IS_GOOD(state))
            mutex()->signal();
    }

    pa_context *connectContext();
    pa_stream *connectStream(const char *device_name, pa_context *context, pa_stream_flags_t flags,
        pa_buffer_attr *attr, pa_sample_spec *spec, pa_channel_map *chanmap, BackendType type);
};
using MainloopLockGuard = std::lock_guard<PulseMainloop>;


pa_context *MainloopUniqueLock::connectContext()
{
    pa_context *context{pa_context_new(mutex()->getApi(), nullptr)};
    if(!context) throw al::backend_exception{al::backend_error::OutOfMemory,
        "pa_context_new() failed"};

    pa_context_set_state_callback(context, [](pa_context *ctx, void *pdata) noexcept
    { return static_cast<MainloopUniqueLock*>(pdata)->contextStateCallback(ctx); }, this);

    int err;
    if((err=pa_context_connect(context, nullptr, pulse_ctx_flags, nullptr)) >= 0)
    {
        pa_context_state_t state;
        while((state=pa_context_get_state(context)) != PA_CONTEXT_READY)
        {
            if(!PA_CONTEXT_IS_GOOD(state))
            {
                err = pa_context_errno(context);
                if(err > 0)  err = -err;
                break;
            }

            wait();
        }
    }
    pa_context_set_state_callback(context, nullptr, nullptr);

    if(err < 0)
    {
        pa_context_unref(context);
        throw al::backend_exception{al::backend_error::DeviceError, "Context did not connect (%s)",
            pa_strerror(err)};
    }

    return context;
}

pa_stream *MainloopUniqueLock::connectStream(const char *device_name, pa_context *context,
    pa_stream_flags_t flags, pa_buffer_attr *attr, pa_sample_spec *spec, pa_channel_map *chanmap,
    BackendType type)
{
    const char *stream_id{(type==BackendType::Playback) ? "Playback Stream" : "Capture Stream"};
    pa_stream *stream{pa_stream_new(context, stream_id, spec, chanmap)};
    if(!stream)
        throw al::backend_exception{al::backend_error::OutOfMemory, "pa_stream_new() failed (%s)",
            pa_strerror(pa_context_errno(context))};

    pa_stream_set_state_callback(stream, [](pa_stream *strm, void *pdata) noexcept
    { return static_cast<MainloopUniqueLock*>(pdata)->streamStateCallback(strm); }, this);

    int err{(type==BackendType::Playback) ?
        pa_stream_connect_playback(stream, device_name, attr, flags, nullptr, nullptr) :
        pa_stream_connect_record(stream, device_name, attr, flags)};
    if(err < 0)
    {
        pa_stream_unref(stream);
        throw al::backend_exception{al::backend_error::DeviceError, "%s did not connect (%s)",
            stream_id, pa_strerror(err)};
    }

    pa_stream_state_t state;
    while((state=pa_stream_get_state(stream)) != PA_STREAM_READY)
    {
        if(!PA_STREAM_IS_GOOD(state))
        {
            err = pa_context_errno(context);
            pa_stream_unref(stream);
            throw al::backend_exception{al::backend_error::DeviceError,
                "%s did not get ready (%s)", stream_id, pa_strerror(err)};
        }

        wait();
    }
    pa_stream_set_state_callback(stream, nullptr, nullptr);

    return stream;
}

void PulseMainloop::close(pa_context *context, pa_stream *stream)
{
    MainloopUniqueLock _{*this};
    if(stream)
    {
        pa_stream_set_state_callback(stream, nullptr, nullptr);
        pa_stream_set_moved_callback(stream, nullptr, nullptr);
        pa_stream_set_write_callback(stream, nullptr, nullptr);
        pa_stream_set_buffer_attr_callback(stream, nullptr, nullptr);
        pa_stream_disconnect(stream);
        pa_stream_unref(stream);
    }

    pa_context_disconnect(context);
    pa_context_unref(context);
}


void PulseMainloop::probePlaybackDevices()
{
    pa_context *context{};

    PlaybackDevices.clear();
    try {
        MainloopUniqueLock plock{*this};
        auto sink_callback = [](pa_context *ctx, const pa_sink_info *info, int eol, void *pdata) noexcept
        { return static_cast<PulseMainloop*>(pdata)->deviceSinkCallback(ctx, info, eol); };

        context = plock.connectContext();
        pa_operation *op{pa_context_get_sink_info_by_name(context, nullptr, sink_callback, this)};
        plock.waitForOperation(op);

        op = pa_context_get_sink_info_list(context, sink_callback, this);
        plock.waitForOperation(op);

        pa_context_disconnect(context);
        pa_context_unref(context);
        context = nullptr;
    }
    catch(std::exception &e) {
        ERR("Error enumerating devices: %s\n", e.what());
        if(context) close(context);
    }
}

void PulseMainloop::probeCaptureDevices()
{
    pa_context *context{};

    CaptureDevices.clear();
    try {
        MainloopUniqueLock plock{*this};
        auto src_callback = [](pa_context *ctx, const pa_source_info *info, int eol, void *pdata) noexcept
        { return static_cast<PulseMainloop*>(pdata)->deviceSourceCallback(ctx, info, eol); };

        context = plock.connectContext();
        pa_operation *op{pa_context_get_source_info_by_name(context, nullptr, src_callback, this)};
        plock.waitForOperation(op);

        op = pa_context_get_source_info_list(context, src_callback, this);
        plock.waitForOperation(op);

        pa_context_disconnect(context);
        pa_context_unref(context);
        context = nullptr;
    }
    catch(std::exception &e) {
        ERR("Error enumerating devices: %s\n", e.what());
        if(context) close(context);
    }
}


/* Used for initial connection test and enumeration. */
PulseMainloop gGlobalMainloop;


struct PulsePlayback final : public BackendBase {
    PulsePlayback(DeviceBase *device) noexcept : BackendBase{device} { }
    ~PulsePlayback() override;

    void bufferAttrCallback(pa_stream *stream) noexcept;
    void streamStateCallback(pa_stream *stream) noexcept;
    void streamWriteCallback(pa_stream *stream, size_t nbytes) noexcept;
    void sinkInfoCallback(pa_context *context, const pa_sink_info *info, int eol) noexcept;
    void sinkNameCallback(pa_context *context, const pa_sink_info *info, int eol) noexcept;
    void streamMovedCallback(pa_stream *stream) noexcept;

    void open(const char *name) override;
    bool reset() override;
    void start() override;
    void stop() override;
    ClockLatency getClockLatency() override;

    PulseMainloop mMainloop;

    al::optional<std::string> mDeviceName{al::nullopt};

    bool mIs51Rear{false};
    pa_buffer_attr mAttr;
    pa_sample_spec mSpec;

    pa_stream *mStream{nullptr};
    pa_context *mContext{nullptr};

    uint mFrameSize{0u};

    DEF_NEWDEL(PulsePlayback)
};

PulsePlayback::~PulsePlayback()
{
    if(!mContext)
        return;

    mMainloop.close(mContext, mStream);
    mContext = nullptr;
    mStream = nullptr;
}


void PulsePlayback::bufferAttrCallback(pa_stream *stream) noexcept
{
    /* FIXME: Update the device's UpdateSize (and/or BufferSize) using the new
     * buffer attributes? Changing UpdateSize will change the ALC_REFRESH
     * property, which probably shouldn't change between device resets. But
     * leaving it alone means ALC_REFRESH will be off.
     */
    mAttr = *(pa_stream_get_buffer_attr(stream));
    TRACE("minreq=%d, tlength=%d, prebuf=%d\n", mAttr.minreq, mAttr.tlength, mAttr.prebuf);
}

void PulsePlayback::streamStateCallback(pa_stream *stream) noexcept
{
    if(pa_stream_get_state(stream) == PA_STREAM_FAILED)
    {
        ERR("Received stream failure!\n");
        mDevice->handleDisconnect("Playback stream failure");
    }
    mMainloop.signal();
}

void PulsePlayback::streamWriteCallback(pa_stream *stream, size_t nbytes) noexcept
{
    do {
        pa_free_cb_t free_func{nullptr};
        auto buflen = static_cast<size_t>(-1);
        void *buf{};
        if(pa_stream_begin_write(stream, &buf, &buflen) || !buf) UNLIKELY
        {
            buflen = nbytes;
            buf = pa_xmalloc(buflen);
            free_func = pa_xfree;
        }
        else
            buflen = minz(buflen, nbytes);
        nbytes -= buflen;

        mDevice->renderSamples(buf, static_cast<uint>(buflen/mFrameSize), mSpec.channels);

        int ret{pa_stream_write(stream, buf, buflen, free_func, 0, PA_SEEK_RELATIVE)};
        if(ret != PA_OK) UNLIKELY
            ERR("Failed to write to stream: %d, %s\n", ret, pa_strerror(ret));
    } while(nbytes > 0);
}

void PulsePlayback::sinkInfoCallback(pa_context*, const pa_sink_info *info, int eol) noexcept
{
    struct ChannelMap {
        DevFmtChannels fmt;
        pa_channel_map map;
        bool is_51rear;
    };
    static constexpr std::array<ChannelMap,8> chanmaps{{
        { DevFmtX714, X714ChanMap, false },
        { DevFmtX71, X71ChanMap, false },
        { DevFmtX61, X61ChanMap, false },
        { DevFmtX51, X51ChanMap, false },
        { DevFmtX51, X51RearChanMap, true },
        { DevFmtQuad, QuadChanMap, false },
        { DevFmtStereo, StereoChanMap, false },
        { DevFmtMono, MonoChanMap, false }
    }};

    if(eol)
    {
        mMainloop.signal();
        return;
    }

    auto chaniter = std::find_if(chanmaps.cbegin(), chanmaps.cend(),
        [info](const ChannelMap &chanmap) -> bool
        { return pa_channel_map_superset(&info->channel_map, &chanmap.map); }
    );
    if(chaniter != chanmaps.cend())
    {
        if(!mDevice->Flags.test(ChannelsRequest))
            mDevice->FmtChans = chaniter->fmt;
        mIs51Rear = chaniter->is_51rear;
    }
    else
    {
        mIs51Rear = false;
        char chanmap_str[PA_CHANNEL_MAP_SNPRINT_MAX]{};
        pa_channel_map_snprint(chanmap_str, sizeof(chanmap_str), &info->channel_map);
        WARN("Failed to find format for channel map:\n    %s\n", chanmap_str);
    }

    if(info->active_port)
        TRACE("Active port: %s (%s)\n", info->active_port->name, info->active_port->description);
    mDevice->Flags.set(DirectEar, (info->active_port
        && strcmp(info->active_port->name, "analog-output-headphones") == 0));
}

void PulsePlayback::sinkNameCallback(pa_context*, const pa_sink_info *info, int eol) noexcept
{
    if(eol)
    {
        mMainloop.signal();
        return;
    }
    mDevice->DeviceName = info->description;
}

void PulsePlayback::streamMovedCallback(pa_stream *stream) noexcept
{
    mDeviceName = pa_stream_get_device_name(stream);
    TRACE("Stream moved to %s\n", mDeviceName->c_str());
}


void PulsePlayback::open(const char *name)
{
    mMainloop = PulseMainloop::Create();
    mMainloop.start();

    const char *pulse_name{nullptr};
    const char *dev_name{nullptr};
    if(name)
    {
        if(PlaybackDevices.empty())
            mMainloop.probePlaybackDevices();

        auto iter = std::find_if(PlaybackDevices.cbegin(), PlaybackDevices.cend(),
            [name](const DevMap &entry) -> bool { return entry.name == name; });
        if(iter == PlaybackDevices.cend())
            throw al::backend_exception{al::backend_error::NoDevice,
                "Device name \"%s\" not found", name};
        pulse_name = iter->device_name.c_str();
        dev_name = iter->name.c_str();
    }

    MainloopUniqueLock plock{mMainloop};
    mContext = plock.connectContext();

    pa_stream_flags_t flags{PA_STREAM_START_CORKED | PA_STREAM_FIX_FORMAT | PA_STREAM_FIX_RATE |
        PA_STREAM_FIX_CHANNELS};
    if(!GetConfigValueBool(nullptr, "pulse", "allow-moves", true))
        flags |= PA_STREAM_DONT_MOVE;

    pa_sample_spec spec{};
    spec.format = PA_SAMPLE_S16NE;
    spec.rate = 44100;
    spec.channels = 2;

    if(!pulse_name)
    {
        static const auto defname = al::getenv("ALSOFT_PULSE_DEFAULT");
        if(defname) pulse_name = defname->c_str();
    }
    TRACE("Connecting to \"%s\"\n", pulse_name ? pulse_name : "(default)");
    mStream = plock.connectStream(pulse_name, mContext, flags, nullptr, &spec, nullptr,
        BackendType::Playback);

    pa_stream_set_moved_callback(mStream, [](pa_stream *stream, void *pdata) noexcept
    { return static_cast<PulsePlayback*>(pdata)->streamMovedCallback(stream); }, this);
    mFrameSize = static_cast<uint>(pa_frame_size(pa_stream_get_sample_spec(mStream)));

    if(pulse_name) mDeviceName.emplace(pulse_name);
    else mDeviceName.reset();
    if(!dev_name)
    {
        auto name_callback = [](pa_context *context, const pa_sink_info *info, int eol, void *pdata) noexcept
        { return static_cast<PulsePlayback*>(pdata)->sinkNameCallback(context, info, eol); };
        pa_operation *op{pa_context_get_sink_info_by_name(mContext,
            pa_stream_get_device_name(mStream), name_callback, this)};
        plock.waitForOperation(op);
    }
    else
        mDevice->DeviceName = dev_name;
}

bool PulsePlayback::reset()
{
    MainloopUniqueLock plock{mMainloop};
    const auto deviceName = mDeviceName ? mDeviceName->c_str() : nullptr;

    if(mStream)
    {
        pa_stream_set_state_callback(mStream, nullptr, nullptr);
        pa_stream_set_moved_callback(mStream, nullptr, nullptr);
        pa_stream_set_write_callback(mStream, nullptr, nullptr);
        pa_stream_set_buffer_attr_callback(mStream, nullptr, nullptr);
        pa_stream_disconnect(mStream);
        pa_stream_unref(mStream);
        mStream = nullptr;
    }

    auto info_callback = [](pa_context *context, const pa_sink_info *info, int eol, void *pdata) noexcept
    { return static_cast<PulsePlayback*>(pdata)->sinkInfoCallback(context, info, eol); };
    pa_operation *op{pa_context_get_sink_info_by_name(mContext, deviceName, info_callback, this)};
    plock.waitForOperation(op);

    pa_stream_flags_t flags{PA_STREAM_START_CORKED | PA_STREAM_INTERPOLATE_TIMING |
        PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_EARLY_REQUESTS};
    if(!GetConfigValueBool(nullptr, "pulse", "allow-moves", true))
        flags |= PA_STREAM_DONT_MOVE;
    if(GetConfigValueBool(mDevice->DeviceName.c_str(), "pulse", "adjust-latency", false))
    {
        /* ADJUST_LATENCY can't be specified with EARLY_REQUESTS, for some
         * reason. So if the user wants to adjust the overall device latency,
         * we can't ask to get write signals as soon as minreq is reached.
         */
        flags &= ~PA_STREAM_EARLY_REQUESTS;
        flags |= PA_STREAM_ADJUST_LATENCY;
    }
    if(GetConfigValueBool(mDevice->DeviceName.c_str(), "pulse", "fix-rate", false)
        || !mDevice->Flags.test(FrequencyRequest))
        flags |= PA_STREAM_FIX_RATE;

    pa_channel_map chanmap{};
    switch(mDevice->FmtChans)
    {
    case DevFmtMono:
        chanmap = MonoChanMap;
        break;
    case DevFmtAmbi3D:
        mDevice->FmtChans = DevFmtStereo;
        /*fall-through*/
    case DevFmtStereo:
        chanmap = StereoChanMap;
        break;
    case DevFmtQuad:
        chanmap = QuadChanMap;
        break;
    case DevFmtX51:
        chanmap = (mIs51Rear ? X51RearChanMap : X51ChanMap);
        break;
    case DevFmtX61:
        chanmap = X61ChanMap;
        break;
    case DevFmtX71:
    case DevFmtX3D71:
        chanmap = X71ChanMap;
        break;
    case DevFmtX714:
        chanmap = X714ChanMap;
        break;
    }
    setDefaultWFXChannelOrder();

    switch(mDevice->FmtType)
    {
    case DevFmtByte:
        mDevice->FmtType = DevFmtUByte;
        /* fall-through */
    case DevFmtUByte:
        mSpec.format = PA_SAMPLE_U8;
        break;
    case DevFmtUShort:
        mDevice->FmtType = DevFmtShort;
        /* fall-through */
    case DevFmtShort:
        mSpec.format = PA_SAMPLE_S16NE;
        break;
    case DevFmtUInt:
        mDevice->FmtType = DevFmtInt;
        /* fall-through */
    case DevFmtInt:
        mSpec.format = PA_SAMPLE_S32NE;
        break;
    case DevFmtFloat:
        mSpec.format = PA_SAMPLE_FLOAT32NE;
        break;
    }
    mSpec.rate = mDevice->Frequency;
    mSpec.channels = static_cast<uint8_t>(mDevice->channelsFromFmt());
    if(pa_sample_spec_valid(&mSpec) == 0)
        throw al::backend_exception{al::backend_error::DeviceError, "Invalid sample spec"};

    const auto frame_size = static_cast<uint>(pa_frame_size(&mSpec));
    mAttr.maxlength = ~0u;
    mAttr.tlength = mDevice->BufferSize * frame_size;
    mAttr.prebuf = 0u;
    mAttr.minreq = mDevice->UpdateSize * frame_size;
    mAttr.fragsize = ~0u;

    mStream = plock.connectStream(deviceName, mContext, flags, &mAttr, &mSpec, &chanmap,
        BackendType::Playback);

    pa_stream_set_state_callback(mStream, [](pa_stream *stream, void *pdata) noexcept
    { return static_cast<PulsePlayback*>(pdata)->streamStateCallback(stream); }, this);
    pa_stream_set_moved_callback(mStream, [](pa_stream *stream, void *pdata) noexcept
    { return static_cast<PulsePlayback*>(pdata)->streamMovedCallback(stream); }, this);

    mSpec = *(pa_stream_get_sample_spec(mStream));
    mFrameSize = static_cast<uint>(pa_frame_size(&mSpec));

    if(mDevice->Frequency != mSpec.rate)
    {
        /* Server updated our playback rate, so modify the buffer attribs
         * accordingly.
         */
        const auto scale = static_cast<double>(mSpec.rate) / mDevice->Frequency;
        const auto perlen = static_cast<uint>(clampd(scale*mDevice->UpdateSize + 0.5, 64.0,
            8192.0));
        const auto buflen = static_cast<uint>(clampd(scale*mDevice->BufferSize + 0.5, perlen*2,
            std::numeric_limits<int>::max()/mFrameSize));

        mAttr.maxlength = ~0u;
        mAttr.tlength = buflen * mFrameSize;
        mAttr.prebuf = 0u;
        mAttr.minreq = perlen * mFrameSize;

        op = pa_stream_set_buffer_attr(mStream, &mAttr, &PulseMainloop::streamSuccessCallbackC,
            &mMainloop);
        plock.waitForOperation(op);

        mDevice->Frequency = mSpec.rate;
    }

    auto attr_callback = [](pa_stream *stream, void *pdata) noexcept
    { return static_cast<PulsePlayback*>(pdata)->bufferAttrCallback(stream); };
    pa_stream_set_buffer_attr_callback(mStream, attr_callback, this);
    bufferAttrCallback(mStream);

    mDevice->BufferSize = mAttr.tlength / mFrameSize;
    mDevice->UpdateSize = mAttr.minreq / mFrameSize;

    return true;
}

void PulsePlayback::start()
{
    MainloopUniqueLock plock{mMainloop};

    /* Write some samples to fill the buffer before we start feeding it newly
     * mixed samples.
     */
    if(size_t todo{pa_stream_writable_size(mStream)})
    {
        void *buf{pa_xmalloc(todo)};
        mDevice->renderSamples(buf, static_cast<uint>(todo/mFrameSize), mSpec.channels);
        pa_stream_write(mStream, buf, todo, pa_xfree, 0, PA_SEEK_RELATIVE);
    }

    pa_stream_set_write_callback(mStream, [](pa_stream *stream, size_t nbytes, void *pdata)noexcept
    { return static_cast<PulsePlayback*>(pdata)->streamWriteCallback(stream, nbytes); }, this);
    pa_operation *op{pa_stream_cork(mStream, 0, &PulseMainloop::streamSuccessCallbackC,
        &mMainloop)};

    plock.waitForOperation(op);
}

void PulsePlayback::stop()
{
    MainloopUniqueLock plock{mMainloop};

    pa_operation *op{pa_stream_cork(mStream, 1, &PulseMainloop::streamSuccessCallbackC,
        &mMainloop)};
    plock.waitForOperation(op);
    pa_stream_set_write_callback(mStream, nullptr, nullptr);
}


ClockLatency PulsePlayback::getClockLatency()
{
    ClockLatency ret;
    pa_usec_t latency;
    int neg, err;

    {
        MainloopUniqueLock plock{mMainloop};
        ret.ClockTime = GetDeviceClockTime(mDevice);
        err = pa_stream_get_latency(mStream, &latency, &neg);
    }

    if(err != 0) UNLIKELY
    {
        /* If err = -PA_ERR_NODATA, it means we were called too soon after
         * starting the stream and no timing info has been received from the
         * server yet. Give a generic value since nothing better is available.
         */
        if(err != -PA_ERR_NODATA)
            ERR("Failed to get stream latency: 0x%x\n", err);
        latency = mDevice->BufferSize - mDevice->UpdateSize;
        neg = 0;
    }
    else if(neg) UNLIKELY
        latency = 0;
    ret.Latency = std::chrono::microseconds{latency};

    return ret;
}


struct PulseCapture final : public BackendBase {
    PulseCapture(DeviceBase *device) noexcept : BackendBase{device} { }
    ~PulseCapture() override;

    void streamStateCallback(pa_stream *stream) noexcept;
    void sourceNameCallback(pa_context *context, const pa_source_info *info, int eol) noexcept;
    void streamMovedCallback(pa_stream *stream) noexcept;

    void open(const char *name) override;
    void start() override;
    void stop() override;
    void captureSamples(al::byte *buffer, uint samples) override;
    uint availableSamples() override;
    ClockLatency getClockLatency() override;

    PulseMainloop mMainloop;

    al::optional<std::string> mDeviceName{al::nullopt};

    al::span<const al::byte> mCapBuffer;
    size_t mHoleLength{0};
    size_t mPacketLength{0};

    uint mLastReadable{0u};
    al::byte mSilentVal{};

    pa_buffer_attr mAttr{};
    pa_sample_spec mSpec{};

    pa_stream *mStream{nullptr};
    pa_context *mContext{nullptr};

    DEF_NEWDEL(PulseCapture)
};

PulseCapture::~PulseCapture()
{
    if(!mContext)
        return;

    mMainloop.close(mContext, mStream);
    mContext = nullptr;
    mStream = nullptr;
}


void PulseCapture::streamStateCallback(pa_stream *stream) noexcept
{
    if(pa_stream_get_state(stream) == PA_STREAM_FAILED)
    {
        ERR("Received stream failure!\n");
        mDevice->handleDisconnect("Capture stream failure");
    }
    mMainloop.signal();
}

void PulseCapture::sourceNameCallback(pa_context*, const pa_source_info *info, int eol) noexcept
{
    if(eol)
    {
        mMainloop.signal();
        return;
    }
    mDevice->DeviceName = info->description;
}

void PulseCapture::streamMovedCallback(pa_stream *stream) noexcept
{
    mDeviceName = pa_stream_get_device_name(stream);
    TRACE("Stream moved to %s\n", mDeviceName->c_str());
}


void PulseCapture::open(const char *name)
{
    if(!mMainloop)
    {
        mMainloop = PulseMainloop::Create();
        mMainloop.start();
    }

    const char *pulse_name{nullptr};
    if(name)
    {
        if(CaptureDevices.empty())
            mMainloop.probeCaptureDevices();

        auto iter = std::find_if(CaptureDevices.cbegin(), CaptureDevices.cend(),
            [name](const DevMap &entry) -> bool { return entry.name == name; });
        if(iter == CaptureDevices.cend())
            throw al::backend_exception{al::backend_error::NoDevice,
                "Device name \"%s\" not found", name};
        pulse_name = iter->device_name.c_str();
        mDevice->DeviceName = iter->name;
    }

    MainloopUniqueLock plock{mMainloop};
    mContext = plock.connectContext();

    pa_channel_map chanmap{};
    switch(mDevice->FmtChans)
    {
    case DevFmtMono:
        chanmap = MonoChanMap;
        break;
    case DevFmtStereo:
        chanmap = StereoChanMap;
        break;
    case DevFmtQuad:
        chanmap = QuadChanMap;
        break;
    case DevFmtX51:
        chanmap = X51ChanMap;
        break;
    case DevFmtX61:
        chanmap = X61ChanMap;
        break;
    case DevFmtX71:
        chanmap = X71ChanMap;
        break;
    case DevFmtX714:
        chanmap = X714ChanMap;
        break;
    case DevFmtX3D71:
    case DevFmtAmbi3D:
        throw al::backend_exception{al::backend_error::DeviceError, "%s capture not supported",
            DevFmtChannelsString(mDevice->FmtChans)};
    }
    setDefaultWFXChannelOrder();

    switch(mDevice->FmtType)
    {
    case DevFmtUByte:
        mSilentVal = al::byte(0x80);
        mSpec.format = PA_SAMPLE_U8;
        break;
    case DevFmtShort:
        mSpec.format = PA_SAMPLE_S16NE;
        break;
    case DevFmtInt:
        mSpec.format = PA_SAMPLE_S32NE;
        break;
    case DevFmtFloat:
        mSpec.format = PA_SAMPLE_FLOAT32NE;
        break;
    case DevFmtByte:
    case DevFmtUShort:
    case DevFmtUInt:
        throw al::backend_exception{al::backend_error::DeviceError,
            "%s capture samples not supported", DevFmtTypeString(mDevice->FmtType)};
    }
    mSpec.rate = mDevice->Frequency;
    mSpec.channels = static_cast<uint8_t>(mDevice->channelsFromFmt());
    if(pa_sample_spec_valid(&mSpec) == 0)
        throw al::backend_exception{al::backend_error::DeviceError, "Invalid sample format"};

    const auto frame_size = static_cast<uint>(pa_frame_size(&mSpec));
    const uint samples{maxu(mDevice->BufferSize, 100 * mDevice->Frequency / 1000)};
    mAttr.minreq = ~0u;
    mAttr.prebuf = ~0u;
    mAttr.maxlength = samples * frame_size;
    mAttr.tlength = ~0u;
    mAttr.fragsize = minu(samples, 50*mDevice->Frequency/1000) * frame_size;

    pa_stream_flags_t flags{PA_STREAM_START_CORKED | PA_STREAM_ADJUST_LATENCY};
    if(!GetConfigValueBool(nullptr, "pulse", "allow-moves", true))
        flags |= PA_STREAM_DONT_MOVE;

    TRACE("Connecting to \"%s\"\n", pulse_name ? pulse_name : "(default)");
    mStream = plock.connectStream(pulse_name, mContext, flags, &mAttr, &mSpec, &chanmap,
        BackendType::Capture);

    pa_stream_set_moved_callback(mStream, [](pa_stream *stream, void *pdata) noexcept
    { return static_cast<PulseCapture*>(pdata)->streamMovedCallback(stream); }, this);
    pa_stream_set_state_callback(mStream, [](pa_stream *stream, void *pdata) noexcept
    { return static_cast<PulseCapture*>(pdata)->streamStateCallback(stream); }, this);

    if(pulse_name) mDeviceName.emplace(pulse_name);
    else mDeviceName.reset();
    if(mDevice->DeviceName.empty())
    {
        auto name_callback = [](pa_context *context, const pa_source_info *info, int eol, void *pdata) noexcept
        { return static_cast<PulseCapture*>(pdata)->sourceNameCallback(context, info, eol); };
        pa_operation *op{pa_context_get_source_info_by_name(mContext,
            pa_stream_get_device_name(mStream), name_callback, this)};
        plock.waitForOperation(op);
    }
}

void PulseCapture::start()
{
    MainloopUniqueLock plock{mMainloop};
    pa_operation *op{pa_stream_cork(mStream, 0, &PulseMainloop::streamSuccessCallbackC,
        &mMainloop)};
    plock.waitForOperation(op);
}

void PulseCapture::stop()
{
    MainloopUniqueLock plock{mMainloop};
    pa_operation *op{pa_stream_cork(mStream, 1, &PulseMainloop::streamSuccessCallbackC,
        &mMainloop)};
    plock.waitForOperation(op);
}

void PulseCapture::captureSamples(al::byte *buffer, uint samples)
{
    al::span<al::byte> dstbuf{buffer, samples * pa_frame_size(&mSpec)};

    /* Capture is done in fragment-sized chunks, so we loop until we get all
     * that's available.
     */
    mLastReadable -= static_cast<uint>(dstbuf.size());
    while(!dstbuf.empty())
    {
        if(mHoleLength > 0) UNLIKELY
        {
            const size_t rem{minz(dstbuf.size(), mHoleLength)};
            std::fill_n(dstbuf.begin(), rem, mSilentVal);
            dstbuf = dstbuf.subspan(rem);
            mHoleLength -= rem;

            continue;
        }
        if(!mCapBuffer.empty())
        {
            const size_t rem{minz(dstbuf.size(), mCapBuffer.size())};
            std::copy_n(mCapBuffer.begin(), rem, dstbuf.begin());
            dstbuf = dstbuf.subspan(rem);
            mCapBuffer = mCapBuffer.subspan(rem);

            continue;
        }

        if(!mDevice->Connected.load(std::memory_order_acquire)) UNLIKELY
            break;

        MainloopUniqueLock plock{mMainloop};
        if(mPacketLength > 0)
        {
            pa_stream_drop(mStream);
            mPacketLength = 0;
        }

        const pa_stream_state_t state{pa_stream_get_state(mStream)};
        if(!PA_STREAM_IS_GOOD(state)) UNLIKELY
        {
            mDevice->handleDisconnect("Bad capture state: %u", state);
            break;
        }

        const void *capbuf;
        size_t caplen;
        if(pa_stream_peek(mStream, &capbuf, &caplen) < 0) UNLIKELY
        {
            mDevice->handleDisconnect("Failed retrieving capture samples: %s",
                pa_strerror(pa_context_errno(mContext)));
            break;
        }
        plock.unlock();

        if(caplen == 0) break;
        if(!capbuf) UNLIKELY
            mHoleLength = caplen;
        else
            mCapBuffer = {static_cast<const al::byte*>(capbuf), caplen};
        mPacketLength = caplen;
    }
    if(!dstbuf.empty())
        std::fill(dstbuf.begin(), dstbuf.end(), mSilentVal);
}

uint PulseCapture::availableSamples()
{
    size_t readable{maxz(mCapBuffer.size(), mHoleLength)};

    if(mDevice->Connected.load(std::memory_order_acquire))
    {
        MainloopUniqueLock plock{mMainloop};
        size_t got{pa_stream_readable_size(mStream)};
        if(static_cast<ssize_t>(got) < 0) UNLIKELY
        {
            const char *err{pa_strerror(static_cast<int>(got))};
            ERR("pa_stream_readable_size() failed: %s\n", err);
            mDevice->handleDisconnect("Failed getting readable size: %s", err);
        }
        else
        {
            /* "readable" is the number of bytes from the last packet that have
             * not yet been read by the caller. So add the stream's readable
             * size excluding the last packet (the stream size includes the
             * last packet until it's dropped).
             */
            if(got > mPacketLength)
                readable += got - mPacketLength;
        }
    }

    /* Avoid uint overflow, and avoid decreasing the readable count. */
    readable = std::min<size_t>(readable, std::numeric_limits<uint>::max());
    mLastReadable = std::max(mLastReadable, static_cast<uint>(readable));
    return mLastReadable / static_cast<uint>(pa_frame_size(&mSpec));
}


ClockLatency PulseCapture::getClockLatency()
{
    ClockLatency ret;
    pa_usec_t latency;
    int neg, err;

    {
        MainloopUniqueLock plock{mMainloop};
        ret.ClockTime = GetDeviceClockTime(mDevice);
        err = pa_stream_get_latency(mStream, &latency, &neg);
    }

    if(err != 0) UNLIKELY
    {
        ERR("Failed to get stream latency: 0x%x\n", err);
        latency = 0;
        neg = 0;
    }
    else if(neg) UNLIKELY
        latency = 0;
    ret.Latency = std::chrono::microseconds{latency};

    return ret;
}

} // namespace


bool PulseBackendFactory::init()
{
#ifdef HAVE_DYNLOAD
    if(!pulse_handle)
    {
        bool ret{true};
        std::string missing_funcs;

#ifdef _WIN32
#define PALIB "libpulse-0.dll"
#elif defined(__APPLE__) && defined(__MACH__)
#define PALIB "libpulse.0.dylib"
#else
#define PALIB "libpulse.so.0"
#endif
        pulse_handle = LoadLib(PALIB);
        if(!pulse_handle)
        {
            WARN("Failed to load %s\n", PALIB);
            return false;
        }

#define LOAD_FUNC(x) do {                                                     \
    p##x = reinterpret_cast<decltype(p##x)>(GetSymbol(pulse_handle, #x));     \
    if(!(p##x)) {                                                             \
        ret = false;                                                          \
        missing_funcs += "\n" #x;                                             \
    }                                                                         \
} while(0)
        PULSE_FUNCS(LOAD_FUNC)
#undef LOAD_FUNC

        if(!ret)
        {
            WARN("Missing expected functions:%s\n", missing_funcs.c_str());
            CloseLib(pulse_handle);
            pulse_handle = nullptr;
            return false;
        }
    }
#endif /* HAVE_DYNLOAD */

    pulse_ctx_flags = PA_CONTEXT_NOFLAGS;
    if(!GetConfigValueBool(nullptr, "pulse", "spawn-server", false))
        pulse_ctx_flags |= PA_CONTEXT_NOAUTOSPAWN;

    try {
        if(!gGlobalMainloop)
        {
            gGlobalMainloop = PulseMainloop::Create();
            gGlobalMainloop.start();
        }

        MainloopUniqueLock plock{gGlobalMainloop};
        pa_context *context{plock.connectContext()};
        pa_context_disconnect(context);
        pa_context_unref(context);
        return true;
    }
    catch(...) {
        return false;
    }
}

bool PulseBackendFactory::querySupport(BackendType type)
{ return type == BackendType::Playback || type == BackendType::Capture; }

std::string PulseBackendFactory::probe(BackendType type)
{
    std::string outnames;

    auto add_device = [&outnames](const DevMap &entry) -> void
    {
        /* +1 to also append the null char (to ensure a null-separated list and
         * double-null terminated list).
         */
        outnames.append(entry.name.c_str(), entry.name.length()+1);
    };

    switch(type)
    {
    case BackendType::Playback:
        gGlobalMainloop.probePlaybackDevices();
        std::for_each(PlaybackDevices.cbegin(), PlaybackDevices.cend(), add_device);
        break;

    case BackendType::Capture:
        gGlobalMainloop.probeCaptureDevices();
        std::for_each(CaptureDevices.cbegin(), CaptureDevices.cend(), add_device);
        break;
    }

    return outnames;
}

BackendPtr PulseBackendFactory::createBackend(DeviceBase *device, BackendType type)
{
    if(type == BackendType::Playback)
        return BackendPtr{new PulsePlayback{device}};
    if(type == BackendType::Capture)
        return BackendPtr{new PulseCapture{device}};
    return nullptr;
}

BackendFactory &PulseBackendFactory::getFactory()
{
    static PulseBackendFactory factory{};
    return factory;
}
