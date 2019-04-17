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

#include "backends/pulseaudio.h"

#include <cstring>

#include <array>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <algorithm>

#include "alMain.h"
#include "alu.h"
#include "alconfig.h"
#include "compat.h"

#include <pulse/pulseaudio.h>


namespace {

#ifdef HAVE_DYNLOAD
void *pa_handle;
#define MAKE_FUNC(x) decltype(x) * p##x
MAKE_FUNC(pa_context_unref);
MAKE_FUNC(pa_sample_spec_valid);
MAKE_FUNC(pa_frame_size);
MAKE_FUNC(pa_stream_drop);
MAKE_FUNC(pa_strerror);
MAKE_FUNC(pa_context_get_state);
MAKE_FUNC(pa_stream_get_state);
MAKE_FUNC(pa_threaded_mainloop_signal);
MAKE_FUNC(pa_stream_peek);
MAKE_FUNC(pa_threaded_mainloop_wait);
MAKE_FUNC(pa_threaded_mainloop_unlock);
MAKE_FUNC(pa_threaded_mainloop_in_thread);
MAKE_FUNC(pa_context_new);
MAKE_FUNC(pa_threaded_mainloop_stop);
MAKE_FUNC(pa_context_disconnect);
MAKE_FUNC(pa_threaded_mainloop_start);
MAKE_FUNC(pa_threaded_mainloop_get_api);
MAKE_FUNC(pa_context_set_state_callback);
MAKE_FUNC(pa_stream_write);
MAKE_FUNC(pa_xfree);
MAKE_FUNC(pa_stream_connect_record);
MAKE_FUNC(pa_stream_connect_playback);
MAKE_FUNC(pa_stream_readable_size);
MAKE_FUNC(pa_stream_writable_size);
MAKE_FUNC(pa_stream_is_corked);
MAKE_FUNC(pa_stream_cork);
MAKE_FUNC(pa_stream_is_suspended);
MAKE_FUNC(pa_stream_get_device_name);
MAKE_FUNC(pa_stream_get_latency);
MAKE_FUNC(pa_path_get_filename);
MAKE_FUNC(pa_get_binary_name);
MAKE_FUNC(pa_threaded_mainloop_free);
MAKE_FUNC(pa_context_errno);
MAKE_FUNC(pa_xmalloc);
MAKE_FUNC(pa_stream_unref);
MAKE_FUNC(pa_threaded_mainloop_accept);
MAKE_FUNC(pa_stream_set_write_callback);
MAKE_FUNC(pa_threaded_mainloop_new);
MAKE_FUNC(pa_context_connect);
MAKE_FUNC(pa_stream_set_buffer_attr);
MAKE_FUNC(pa_stream_get_buffer_attr);
MAKE_FUNC(pa_stream_get_sample_spec);
MAKE_FUNC(pa_stream_get_time);
MAKE_FUNC(pa_stream_set_read_callback);
MAKE_FUNC(pa_stream_set_state_callback);
MAKE_FUNC(pa_stream_set_moved_callback);
MAKE_FUNC(pa_stream_set_underflow_callback);
MAKE_FUNC(pa_stream_new_with_proplist);
MAKE_FUNC(pa_stream_disconnect);
MAKE_FUNC(pa_threaded_mainloop_lock);
MAKE_FUNC(pa_channel_map_init_auto);
MAKE_FUNC(pa_channel_map_parse);
MAKE_FUNC(pa_channel_map_snprint);
MAKE_FUNC(pa_channel_map_equal);
MAKE_FUNC(pa_context_get_server_info);
MAKE_FUNC(pa_context_get_sink_info_by_name);
MAKE_FUNC(pa_context_get_sink_info_list);
MAKE_FUNC(pa_context_get_source_info_by_name);
MAKE_FUNC(pa_context_get_source_info_list);
MAKE_FUNC(pa_operation_get_state);
MAKE_FUNC(pa_operation_unref);
MAKE_FUNC(pa_proplist_new);
MAKE_FUNC(pa_proplist_free);
MAKE_FUNC(pa_proplist_set);
MAKE_FUNC(pa_channel_map_superset);
MAKE_FUNC(pa_stream_set_buffer_attr_callback);
MAKE_FUNC(pa_stream_begin_write);
#undef MAKE_FUNC

#ifndef IN_IDE_PARSER
#define pa_context_unref ppa_context_unref
#define pa_sample_spec_valid ppa_sample_spec_valid
#define pa_frame_size ppa_frame_size
#define pa_stream_drop ppa_stream_drop
#define pa_strerror ppa_strerror
#define pa_context_get_state ppa_context_get_state
#define pa_stream_get_state ppa_stream_get_state
#define pa_threaded_mainloop_signal ppa_threaded_mainloop_signal
#define pa_stream_peek ppa_stream_peek
#define pa_threaded_mainloop_wait ppa_threaded_mainloop_wait
#define pa_threaded_mainloop_unlock ppa_threaded_mainloop_unlock
#define pa_threaded_mainloop_in_thread ppa_threaded_mainloop_in_thread
#define pa_context_new ppa_context_new
#define pa_threaded_mainloop_stop ppa_threaded_mainloop_stop
#define pa_context_disconnect ppa_context_disconnect
#define pa_threaded_mainloop_start ppa_threaded_mainloop_start
#define pa_threaded_mainloop_get_api ppa_threaded_mainloop_get_api
#define pa_context_set_state_callback ppa_context_set_state_callback
#define pa_stream_write ppa_stream_write
#define pa_xfree ppa_xfree
#define pa_stream_connect_record ppa_stream_connect_record
#define pa_stream_connect_playback ppa_stream_connect_playback
#define pa_stream_readable_size ppa_stream_readable_size
#define pa_stream_writable_size ppa_stream_writable_size
#define pa_stream_is_corked ppa_stream_is_corked
#define pa_stream_cork ppa_stream_cork
#define pa_stream_is_suspended ppa_stream_is_suspended
#define pa_stream_get_device_name ppa_stream_get_device_name
#define pa_stream_get_latency ppa_stream_get_latency
#define pa_path_get_filename ppa_path_get_filename
#define pa_get_binary_name ppa_get_binary_name
#define pa_threaded_mainloop_free ppa_threaded_mainloop_free
#define pa_context_errno ppa_context_errno
#define pa_xmalloc ppa_xmalloc
#define pa_stream_unref ppa_stream_unref
#define pa_threaded_mainloop_accept ppa_threaded_mainloop_accept
#define pa_stream_set_write_callback ppa_stream_set_write_callback
#define pa_threaded_mainloop_new ppa_threaded_mainloop_new
#define pa_context_connect ppa_context_connect
#define pa_stream_set_buffer_attr ppa_stream_set_buffer_attr
#define pa_stream_get_buffer_attr ppa_stream_get_buffer_attr
#define pa_stream_get_sample_spec ppa_stream_get_sample_spec
#define pa_stream_get_time ppa_stream_get_time
#define pa_stream_set_read_callback ppa_stream_set_read_callback
#define pa_stream_set_state_callback ppa_stream_set_state_callback
#define pa_stream_set_moved_callback ppa_stream_set_moved_callback
#define pa_stream_set_underflow_callback ppa_stream_set_underflow_callback
#define pa_stream_new_with_proplist ppa_stream_new_with_proplist
#define pa_stream_disconnect ppa_stream_disconnect
#define pa_threaded_mainloop_lock ppa_threaded_mainloop_lock
#define pa_channel_map_init_auto ppa_channel_map_init_auto
#define pa_channel_map_parse ppa_channel_map_parse
#define pa_channel_map_snprint ppa_channel_map_snprint
#define pa_channel_map_equal ppa_channel_map_equal
#define pa_context_get_server_info ppa_context_get_server_info
#define pa_context_get_sink_info_by_name ppa_context_get_sink_info_by_name
#define pa_context_get_sink_info_list ppa_context_get_sink_info_list
#define pa_context_get_source_info_by_name ppa_context_get_source_info_by_name
#define pa_context_get_source_info_list ppa_context_get_source_info_list
#define pa_operation_get_state ppa_operation_get_state
#define pa_operation_unref ppa_operation_unref
#define pa_proplist_new ppa_proplist_new
#define pa_proplist_free ppa_proplist_free
#define pa_proplist_set ppa_proplist_set
#define pa_channel_map_superset ppa_channel_map_superset
#define pa_stream_set_buffer_attr_callback ppa_stream_set_buffer_attr_callback
#define pa_stream_begin_write ppa_stream_begin_write
#endif /* IN_IDE_PARSER */

#endif


/* *grumble* Don't use enums for bitflags. */
inline pa_stream_flags_t operator|(pa_stream_flags_t lhs, pa_stream_flags_t rhs)
{ return pa_stream_flags_t(int(lhs) | int(rhs)); }
inline pa_stream_flags_t& operator|=(pa_stream_flags_t &lhs, pa_stream_flags_t rhs)
{
    lhs = pa_stream_flags_t(int(lhs) | int(rhs));
    return lhs;
}
inline pa_context_flags_t& operator|=(pa_context_flags_t &lhs, pa_context_flags_t rhs)
{
    lhs = pa_context_flags_t(int(lhs) | int(rhs));
    return lhs;
}

inline pa_stream_flags_t& operator&=(pa_stream_flags_t &lhs, int rhs)
{
    lhs = pa_stream_flags_t(int(lhs) & rhs);
    return lhs;
}


class palock_guard {
    pa_threaded_mainloop *mLoop;

public:
    explicit palock_guard(pa_threaded_mainloop *loop) : mLoop(loop)
    { pa_threaded_mainloop_lock(mLoop); }
    ~palock_guard() { pa_threaded_mainloop_unlock(mLoop); }

    palock_guard(const palock_guard&) = delete;
    palock_guard& operator=(const palock_guard&) = delete;
};

class unique_palock {
    pa_threaded_mainloop *mLoop{nullptr};
    bool mLocked{false};

public:
    unique_palock() noexcept = default;
    explicit unique_palock(pa_threaded_mainloop *loop) : mLoop(loop)
    {
        pa_threaded_mainloop_lock(mLoop);
        mLocked = true;
    }
    unique_palock(unique_palock&& rhs) : mLoop(rhs.mLoop), mLocked(rhs.mLocked)
    { rhs.mLoop = nullptr; rhs.mLocked = false; }
    ~unique_palock() { if(mLocked) pa_threaded_mainloop_unlock(mLoop); }

    unique_palock& operator=(const unique_palock&) = delete;
    unique_palock& operator=(unique_palock&& rhs)
    {
        if(mLocked)
            pa_threaded_mainloop_unlock(mLoop);
        mLoop = rhs.mLoop; rhs.mLoop = nullptr;
        mLocked = rhs.mLocked; rhs.mLocked = false;
        return *this;
    }

    void lock()
    {
        pa_threaded_mainloop_lock(mLoop);
        mLocked = true;
    }
    void unlock()
    {
        mLocked = false;
        pa_threaded_mainloop_unlock(mLoop);
    }
};


/* Global flags and properties */
pa_context_flags_t pulse_ctx_flags;
pa_proplist *prop_filter;


/* PulseAudio Event Callbacks */
void context_state_callback(pa_context *context, void *pdata)
{
    auto loop = static_cast<pa_threaded_mainloop*>(pdata);
    pa_context_state_t state{pa_context_get_state(context)};
    if(state == PA_CONTEXT_READY || !PA_CONTEXT_IS_GOOD(state))
        pa_threaded_mainloop_signal(loop, 0);
}

void stream_state_callback(pa_stream *stream, void *pdata)
{
    auto loop = static_cast<pa_threaded_mainloop*>(pdata);
    pa_stream_state_t state{pa_stream_get_state(stream)};
    if(state == PA_STREAM_READY || !PA_STREAM_IS_GOOD(state))
        pa_threaded_mainloop_signal(loop, 0);
}

void stream_success_callback(pa_stream *UNUSED(stream), int UNUSED(success), void *pdata)
{
    auto loop = static_cast<pa_threaded_mainloop*>(pdata);
    pa_threaded_mainloop_signal(loop, 0);
}

void wait_for_operation(pa_operation *op, pa_threaded_mainloop *loop)
{
    if(op)
    {
        while(pa_operation_get_state(op) == PA_OPERATION_RUNNING)
            pa_threaded_mainloop_wait(loop);
        pa_operation_unref(op);
    }
}


pa_context *connect_context(pa_threaded_mainloop *loop, ALboolean silent)
{
    const char *name{"OpenAL Soft"};

    const PathNamePair &binname = GetProcBinary();
    if(!binname.fname.empty())
        name = binname.fname.c_str();

    pa_context *context{pa_context_new(pa_threaded_mainloop_get_api(loop), name)};
    if(!context)
    {
        ERR("pa_context_new() failed\n");
        return nullptr;
    }

    pa_context_set_state_callback(context, context_state_callback, loop);

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

            pa_threaded_mainloop_wait(loop);
        }
    }
    pa_context_set_state_callback(context, nullptr, nullptr);

    if(err < 0)
    {
        if(!silent)
            ERR("Context did not connect: %s\n", pa_strerror(err));
        pa_context_unref(context);
        context = nullptr;
    }

    return context;
}


using MainloopContextPair = std::pair<pa_threaded_mainloop*,pa_context*>;
MainloopContextPair pulse_open(void(*state_cb)(pa_context*,void*), void *ptr)
{
    pa_threaded_mainloop *loop{pa_threaded_mainloop_new()};
    if(UNLIKELY(!loop))
    {
        ERR("pa_threaded_mainloop_new() failed!\n");
        return {nullptr, nullptr};
    }
    if(UNLIKELY(pa_threaded_mainloop_start(loop) < 0))
    {
        ERR("pa_threaded_mainloop_start() failed\n");
        pa_threaded_mainloop_free(loop);
        return {nullptr, nullptr};
    }

    unique_palock palock{loop};
    pa_context *context{connect_context(loop, AL_FALSE)};
    if(UNLIKELY(!context))
    {
        palock = unique_palock{};
        pa_threaded_mainloop_stop(loop);
        pa_threaded_mainloop_free(loop);
        return {nullptr, nullptr};
    }
    pa_context_set_state_callback(context, state_cb, ptr);
    return {loop, context};
}

void pulse_close(pa_threaded_mainloop *loop, pa_context *context, pa_stream *stream)
{
    { palock_guard _{loop};
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

    pa_threaded_mainloop_stop(loop);
    pa_threaded_mainloop_free(loop);
}


struct DevMap {
    std::string name;
    std::string device_name;

    template<typename StrT0, typename StrT1>
    DevMap(StrT0&& name_, StrT1&& devname_)
      : name{std::forward<StrT0>(name_)}, device_name{std::forward<StrT1>(devname_)}
    { }
};

bool checkName(const al::vector<DevMap> &list, const std::string &name)
{
    return std::find_if(list.cbegin(), list.cend(),
        [&name](const DevMap &entry) -> bool
        { return entry.name == name; }
    ) != list.cend();
}

al::vector<DevMap> PlaybackDevices;
al::vector<DevMap> CaptureDevices;


pa_stream *pulse_connect_stream(const char *device_name, pa_threaded_mainloop *loop,
    pa_context *context, pa_stream_flags_t flags, pa_buffer_attr *attr, pa_sample_spec *spec,
    pa_channel_map *chanmap, BackendType type)
{
    pa_stream *stream{pa_stream_new_with_proplist(context,
        (type==BackendType::Playback) ? "Playback Stream" : "Capture Stream", spec, chanmap,
        prop_filter)};
    if(!stream)
    {
        ERR("pa_stream_new_with_proplist() failed: %s\n", pa_strerror(pa_context_errno(context)));
        return nullptr;
    }

    pa_stream_set_state_callback(stream, stream_state_callback, loop);

    int err{(type==BackendType::Playback) ?
        pa_stream_connect_playback(stream, device_name, attr, flags, nullptr, nullptr) :
        pa_stream_connect_record(stream, device_name, attr, flags)};
    if(err < 0)
    {
        ERR("Stream did not connect: %s\n", pa_strerror(err));
        pa_stream_unref(stream);
        return nullptr;
    }

    pa_stream_state_t state;
    while((state=pa_stream_get_state(stream)) != PA_STREAM_READY)
    {
        if(!PA_STREAM_IS_GOOD(state))
        {
            ERR("Stream did not get ready: %s\n", pa_strerror(pa_context_errno(context)));
            pa_stream_unref(stream);
            return nullptr;
        }

        pa_threaded_mainloop_wait(loop);
    }
    pa_stream_set_state_callback(stream, nullptr, nullptr);

    return stream;
}


void device_sink_callback(pa_context *UNUSED(context), const pa_sink_info *info, int eol, void *pdata)
{
    auto loop = static_cast<pa_threaded_mainloop*>(pdata);

    if(eol)
    {
        pa_threaded_mainloop_signal(loop, 0);
        return;
    }

    /* Skip this device is if it's already in the list. */
    if(std::find_if(PlaybackDevices.cbegin(), PlaybackDevices.cend(),
        [info](const DevMap &entry) -> bool
        { return entry.device_name == info->name; }
    ) != PlaybackDevices.cend())
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
    PlaybackDevices.emplace_back(std::move(newname), info->name);
    DevMap &newentry = PlaybackDevices.back();

    TRACE("Got device \"%s\", \"%s\"\n", newentry.name.c_str(), newentry.device_name.c_str());
}

void probePlaybackDevices()
{
    PlaybackDevices.clear();

    pa_threaded_mainloop *loop{pa_threaded_mainloop_new()};
    if(loop && pa_threaded_mainloop_start(loop) >= 0)
    {
        unique_palock palock{loop};

        pa_context *context{connect_context(loop, AL_FALSE)};
        if(context)
        {
            pa_stream_flags_t flags{PA_STREAM_FIX_FORMAT | PA_STREAM_FIX_RATE |
                                    PA_STREAM_FIX_CHANNELS | PA_STREAM_DONT_MOVE};

            pa_sample_spec spec;
            spec.format = PA_SAMPLE_S16NE;
            spec.rate = 44100;
            spec.channels = 2;

            pa_stream *stream{pulse_connect_stream(nullptr, loop, context, flags, nullptr, &spec,
                nullptr, BackendType::Playback)};
            if(stream)
            {
                pa_operation *op{pa_context_get_sink_info_by_name(context,
                    pa_stream_get_device_name(stream), device_sink_callback, loop)};
                wait_for_operation(op, loop);

                pa_stream_disconnect(stream);
                pa_stream_unref(stream);
                stream = nullptr;
            }

            pa_operation *op{pa_context_get_sink_info_list(context,
                device_sink_callback, loop)};
            wait_for_operation(op, loop);

            pa_context_disconnect(context);
            pa_context_unref(context);
        }
        palock = unique_palock{};
        pa_threaded_mainloop_stop(loop);
    }
    if(loop)
        pa_threaded_mainloop_free(loop);
}


void device_source_callback(pa_context *UNUSED(context), const pa_source_info *info, int eol, void *pdata)
{
    auto loop = static_cast<pa_threaded_mainloop*>(pdata);

    if(eol)
    {
        pa_threaded_mainloop_signal(loop, 0);
        return;
    }

    /* Skip this device is if it's already in the list. */
    if(std::find_if(CaptureDevices.cbegin(), CaptureDevices.cend(),
        [info](const DevMap &entry) -> bool
        { return entry.device_name == info->name; }
    ) != CaptureDevices.cend())
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
    CaptureDevices.emplace_back(std::move(newname), info->name);
    DevMap &newentry = CaptureDevices.back();

    TRACE("Got device \"%s\", \"%s\"\n", newentry.name.c_str(), newentry.device_name.c_str());
}

void probeCaptureDevices()
{
    CaptureDevices.clear();

    pa_threaded_mainloop *loop{pa_threaded_mainloop_new()};
    if(loop && pa_threaded_mainloop_start(loop) >= 0)
    {
        unique_palock palock{loop};

        pa_context *context{connect_context(loop, AL_FALSE)};
        if(context)
        {
            pa_stream_flags_t flags{PA_STREAM_FIX_FORMAT | PA_STREAM_FIX_RATE |
                                    PA_STREAM_FIX_CHANNELS | PA_STREAM_DONT_MOVE};

            pa_sample_spec spec;
            spec.format = PA_SAMPLE_S16NE;
            spec.rate = 44100;
            spec.channels = 1;

            pa_stream *stream{pulse_connect_stream(nullptr, loop, context, flags, nullptr, &spec,
                nullptr, BackendType::Capture)};
            if(stream)
            {
                pa_operation *op{pa_context_get_source_info_by_name(context,
                    pa_stream_get_device_name(stream), device_source_callback, loop)};
                wait_for_operation(op, loop);

                pa_stream_disconnect(stream);
                pa_stream_unref(stream);
                stream = nullptr;
            }

            pa_operation *op{pa_context_get_source_info_list(context,
                device_source_callback, loop)};
            wait_for_operation(op, loop);

            pa_context_disconnect(context);
            pa_context_unref(context);
        }
        palock.unlock();
        pa_threaded_mainloop_stop(loop);
    }
    if(loop)
        pa_threaded_mainloop_free(loop);
}


struct PulsePlayback final : public BackendBase {
    PulsePlayback(ALCdevice *device) noexcept : BackendBase{device} { }
    ~PulsePlayback() override;

    static void bufferAttrCallbackC(pa_stream *stream, void *pdata);
    void bufferAttrCallback(pa_stream *stream);

    static void contextStateCallbackC(pa_context *context, void *pdata);
    void contextStateCallback(pa_context *context);

    static void streamStateCallbackC(pa_stream *stream, void *pdata);
    void streamStateCallback(pa_stream *stream);

    static void streamWriteCallbackC(pa_stream *stream, size_t nbytes, void *pdata);
    void streamWriteCallback(pa_stream *stream, size_t nbytes);

    static void sinkInfoCallbackC(pa_context *context, const pa_sink_info *info, int eol, void *pdata);
    void sinkInfoCallback(pa_context *context, const pa_sink_info *info, int eol);

    static void sinkNameCallbackC(pa_context *context, const pa_sink_info *info, int eol, void *pdata);
    void sinkNameCallback(pa_context *context, const pa_sink_info *info, int eol);

    static void streamMovedCallbackC(pa_stream *stream, void *pdata);
    void streamMovedCallback(pa_stream *stream);

    ALCenum open(const ALCchar *name) override;
    ALCboolean reset() override;
    ALCboolean start() override;
    void stop() override;
    ClockLatency getClockLatency() override;
    void lock() override;
    void unlock() override;

    std::string mDeviceName;

    pa_buffer_attr mAttr;
    pa_sample_spec mSpec;

    pa_threaded_mainloop *mLoop{nullptr};

    pa_stream *mStream{nullptr};
    pa_context *mContext{nullptr};

    ALuint mBufferSize{0u};
    ALuint mFrameSize{0u};

    static constexpr inline const char *CurrentPrefix() noexcept { return "PulsePlayback::"; }
    DEF_NEWDEL(PulsePlayback)
};

PulsePlayback::~PulsePlayback()
{
    if(!mLoop)
        return;

    pulse_close(mLoop, mContext, mStream);
    mLoop = nullptr;
    mContext = nullptr;
    mStream = nullptr;
}


void PulsePlayback::bufferAttrCallbackC(pa_stream *stream, void *pdata)
{ static_cast<PulsePlayback*>(pdata)->bufferAttrCallback(stream); }

void PulsePlayback::bufferAttrCallback(pa_stream *stream)
{
    /* FIXME: Update the device's UpdateSize (and/or NumUpdates) using the new
     * buffer attributes? Changing UpdateSize will change the ALC_REFRESH
     * property, which probably shouldn't change between device resets. But
     * leaving it alone means ALC_REFRESH will be off.
     */
    mAttr = *(pa_stream_get_buffer_attr(stream));
    TRACE("minreq=%d, tlength=%d, prebuf=%d\n", mAttr.minreq, mAttr.tlength, mAttr.prebuf);

    const ALuint num_periods{(mAttr.tlength + mAttr.minreq/2u) / mAttr.minreq};
    mBufferSize = maxu(num_periods, 2u) * mAttr.minreq;
}

void PulsePlayback::contextStateCallbackC(pa_context *context, void *pdata)
{ static_cast<PulsePlayback*>(pdata)->contextStateCallback(context); }

void PulsePlayback::contextStateCallback(pa_context *context)
{
    if(pa_context_get_state(context) == PA_CONTEXT_FAILED)
    {
        ERR("Received context failure!\n");
        aluHandleDisconnect(mDevice, "Playback state failure");
    }
    pa_threaded_mainloop_signal(mLoop, 0);
}

void PulsePlayback::streamStateCallbackC(pa_stream *stream, void *pdata)
{ static_cast<PulsePlayback*>(pdata)->streamStateCallback(stream); }

void PulsePlayback::streamStateCallback(pa_stream *stream)
{
    if(pa_stream_get_state(stream) == PA_STREAM_FAILED)
    {
        ERR("Received stream failure!\n");
        aluHandleDisconnect(mDevice, "Playback stream failure");
    }
    pa_threaded_mainloop_signal(mLoop, 0);
}

void PulsePlayback::streamWriteCallbackC(pa_stream *stream, size_t nbytes, void *pdata)
{ static_cast<PulsePlayback*>(pdata)->streamWriteCallback(stream, nbytes); }

void PulsePlayback::streamWriteCallback(pa_stream *stream, size_t nbytes)
{
    /* Round down to the nearest period/minreq multiple if doing more than 1. */
    if(nbytes > mAttr.minreq)
        nbytes -= nbytes%mAttr.minreq;

    void *buf{pa_xmalloc(nbytes)};
    aluMixData(mDevice, buf, nbytes/mFrameSize);

    int ret{pa_stream_write(stream, buf, nbytes, pa_xfree, 0, PA_SEEK_RELATIVE)};
    if(UNLIKELY(ret != PA_OK))
        ERR("Failed to write to stream: %d, %s\n", ret, pa_strerror(ret));
}

void PulsePlayback::sinkInfoCallbackC(pa_context *context, const pa_sink_info *info, int eol, void *pdata)
{ static_cast<PulsePlayback*>(pdata)->sinkInfoCallback(context, info, eol); }

void PulsePlayback::sinkInfoCallback(pa_context* UNUSED(context), const pa_sink_info *info, int eol)
{
    struct ChannelMap {
        DevFmtChannels chans;
        pa_channel_map map;
    };
    static constexpr std::array<ChannelMap,7> chanmaps{{
        { DevFmtX71, { 8, {
            PA_CHANNEL_POSITION_FRONT_LEFT, PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_FRONT_CENTER, PA_CHANNEL_POSITION_LFE,
            PA_CHANNEL_POSITION_REAR_LEFT, PA_CHANNEL_POSITION_REAR_RIGHT,
            PA_CHANNEL_POSITION_SIDE_LEFT, PA_CHANNEL_POSITION_SIDE_RIGHT
        } } },
        { DevFmtX61, { 7, {
            PA_CHANNEL_POSITION_FRONT_LEFT, PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_FRONT_CENTER, PA_CHANNEL_POSITION_LFE,
            PA_CHANNEL_POSITION_REAR_CENTER,
            PA_CHANNEL_POSITION_SIDE_LEFT, PA_CHANNEL_POSITION_SIDE_RIGHT
        } } },
        { DevFmtX51, { 6, {
            PA_CHANNEL_POSITION_FRONT_LEFT, PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_FRONT_CENTER, PA_CHANNEL_POSITION_LFE,
            PA_CHANNEL_POSITION_SIDE_LEFT, PA_CHANNEL_POSITION_SIDE_RIGHT
        } } },
        { DevFmtX51Rear, { 6, {
            PA_CHANNEL_POSITION_FRONT_LEFT, PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_FRONT_CENTER, PA_CHANNEL_POSITION_LFE,
            PA_CHANNEL_POSITION_REAR_LEFT, PA_CHANNEL_POSITION_REAR_RIGHT
        } } },
        { DevFmtQuad, { 4, {
            PA_CHANNEL_POSITION_FRONT_LEFT, PA_CHANNEL_POSITION_FRONT_RIGHT,
            PA_CHANNEL_POSITION_REAR_LEFT, PA_CHANNEL_POSITION_REAR_RIGHT
        } } },
        { DevFmtStereo, { 2, {
            PA_CHANNEL_POSITION_FRONT_LEFT, PA_CHANNEL_POSITION_FRONT_RIGHT
        } } },
        { DevFmtMono, { 1, {PA_CHANNEL_POSITION_MONO} } }
    }};

    if(eol)
    {
        pa_threaded_mainloop_signal(mLoop, 0);
        return;
    }

    auto chanmap = std::find_if(chanmaps.cbegin(), chanmaps.cend(),
        [info](const ChannelMap &chanmap) -> bool
        { return pa_channel_map_superset(&info->channel_map, &chanmap.map); }
    );
    if(chanmap != chanmaps.cend())
    {
        if(!(mDevice->Flags&DEVICE_CHANNELS_REQUEST))
            mDevice->FmtChans = chanmap->chans;
    }
    else
    {
        char chanmap_str[PA_CHANNEL_MAP_SNPRINT_MAX]{};
        pa_channel_map_snprint(chanmap_str, sizeof(chanmap_str), &info->channel_map);
        WARN("Failed to find format for channel map:\n    %s\n", chanmap_str);
    }

    if(info->active_port)
        TRACE("Active port: %s (%s)\n", info->active_port->name, info->active_port->description);
    mDevice->IsHeadphones = (mDevice->FmtChans == DevFmtStereo &&
        info->active_port && strcmp(info->active_port->name, "analog-output-headphones") == 0);
}

void PulsePlayback::sinkNameCallbackC(pa_context *context, const pa_sink_info *info, int eol, void *pdata)
{ static_cast<PulsePlayback*>(pdata)->sinkNameCallback(context, info, eol); }

void PulsePlayback::sinkNameCallback(pa_context* UNUSED(context), const pa_sink_info *info, int eol)
{
    if(eol)
    {
        pa_threaded_mainloop_signal(mLoop, 0);
        return;
    }
    mDevice->DeviceName = info->description;
}

void PulsePlayback::streamMovedCallbackC(pa_stream *stream, void *pdata)
{ static_cast<PulsePlayback*>(pdata)->streamMovedCallback(stream); }

void PulsePlayback::streamMovedCallback(pa_stream *stream)
{
    mDeviceName = pa_stream_get_device_name(stream);
    TRACE("Stream moved to %s\n", mDeviceName.c_str());
}


ALCenum PulsePlayback::open(const ALCchar *name)
{
    const char *pulse_name{nullptr};
    const char *dev_name{nullptr};

    if(name)
    {
        if(PlaybackDevices.empty())
            probePlaybackDevices();

        auto iter = std::find_if(PlaybackDevices.cbegin(), PlaybackDevices.cend(),
            [name](const DevMap &entry) -> bool
            { return entry.name == name; }
        );
        if(iter == PlaybackDevices.cend())
            return ALC_INVALID_VALUE;
        pulse_name = iter->device_name.c_str();
        dev_name = iter->name.c_str();
    }

    std::tie(mLoop, mContext) = pulse_open(&PulsePlayback::contextStateCallbackC, this);
    if(!mLoop) return ALC_INVALID_VALUE;

    unique_palock palock{mLoop};

    pa_stream_flags_t flags{PA_STREAM_FIX_FORMAT | PA_STREAM_FIX_RATE |
                            PA_STREAM_FIX_CHANNELS};
    if(!GetConfigValueBool(nullptr, "pulse", "allow-moves", 0))
        flags |= PA_STREAM_DONT_MOVE;

    pa_sample_spec spec{};
    spec.format = PA_SAMPLE_S16NE;
    spec.rate = 44100;
    spec.channels = 2;

    TRACE("Connecting to \"%s\"\n", pulse_name ? pulse_name : "(default)");
    if(!pulse_name)
    {
        pulse_name = getenv("ALSOFT_PULSE_DEFAULT");
        if(pulse_name && !pulse_name[0]) pulse_name = nullptr;
    }
    mStream = pulse_connect_stream(pulse_name, mLoop, mContext, flags, nullptr, &spec, nullptr,
        BackendType::Playback);
    if(!mStream)
    {
        palock = unique_palock{};
        pulse_close(mLoop, mContext, mStream);
        mLoop = nullptr;
        mContext = nullptr;
        return ALC_INVALID_VALUE;
    }
    pa_stream_set_moved_callback(mStream, &PulsePlayback::streamMovedCallbackC, this);
    mFrameSize = pa_frame_size(pa_stream_get_sample_spec(mStream));

    mDeviceName = pa_stream_get_device_name(mStream);
    if(!dev_name)
    {
        pa_operation *op{pa_context_get_sink_info_by_name(mContext, mDeviceName.c_str(),
            &PulsePlayback::sinkNameCallbackC, this)};
        wait_for_operation(op, mLoop);
    }
    else
        mDevice->DeviceName = dev_name;

    return ALC_NO_ERROR;
}

ALCboolean PulsePlayback::reset()
{
    unique_palock palock{mLoop};

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

    pa_operation *op{pa_context_get_sink_info_by_name(mContext, mDeviceName.c_str(),
        &PulsePlayback::sinkInfoCallbackC, this)};
    wait_for_operation(op, mLoop);

    pa_stream_flags_t flags{PA_STREAM_START_CORKED | PA_STREAM_INTERPOLATE_TIMING |
        PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_EARLY_REQUESTS};
    if(!GetConfigValueBool(nullptr, "pulse", "allow-moves", 0))
        flags |= PA_STREAM_DONT_MOVE;
    if(GetConfigValueBool(mDevice->DeviceName.c_str(), "pulse", "adjust-latency", 0))
    {
        /* ADJUST_LATENCY can't be specified with EARLY_REQUESTS, for some
         * reason. So if the user wants to adjust the overall device latency,
         * we can't ask to get write signals as soon as minreq is reached.
         */
        flags &= ~PA_STREAM_EARLY_REQUESTS;
        flags |= PA_STREAM_ADJUST_LATENCY;
    }
    if(GetConfigValueBool(mDevice->DeviceName.c_str(), "pulse", "fix-rate", 0) ||
       !(mDevice->Flags&DEVICE_FREQUENCY_REQUEST))
        flags |= PA_STREAM_FIX_RATE;

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
    mSpec.channels = mDevice->channelsFromFmt();

    if(pa_sample_spec_valid(&mSpec) == 0)
    {
        ERR("Invalid sample format\n");
        return ALC_FALSE;
    }

    const char *mapname{nullptr};
    pa_channel_map chanmap;
    switch(mDevice->FmtChans)
    {
        case DevFmtMono:
            mapname = "mono";
            break;
        case DevFmtAmbi3D:
            mDevice->FmtChans = DevFmtStereo;
            /*fall-through*/
        case DevFmtStereo:
            mapname = "front-left,front-right";
            break;
        case DevFmtQuad:
            mapname = "front-left,front-right,rear-left,rear-right";
            break;
        case DevFmtX51:
            mapname = "front-left,front-right,front-center,lfe,side-left,side-right";
            break;
        case DevFmtX51Rear:
            mapname = "front-left,front-right,front-center,lfe,rear-left,rear-right";
            break;
        case DevFmtX61:
            mapname = "front-left,front-right,front-center,lfe,rear-center,side-left,side-right";
            break;
        case DevFmtX71:
            mapname = "front-left,front-right,front-center,lfe,rear-left,rear-right,side-left,side-right";
            break;
    }
    if(!pa_channel_map_parse(&chanmap, mapname))
    {
        ERR("Failed to build channel map for %s\n", DevFmtChannelsString(mDevice->FmtChans));
        return ALC_FALSE;
    }
    SetDefaultWFXChannelOrder(mDevice);

    size_t period_size{mDevice->UpdateSize * pa_frame_size(&mSpec)};
    mAttr.maxlength = -1;
    mAttr.tlength = period_size * maxu(mDevice->NumUpdates, 2);
    mAttr.prebuf = 0;
    mAttr.minreq = period_size;
    mAttr.fragsize = -1;

    mStream = pulse_connect_stream(mDeviceName.c_str(), mLoop, mContext, flags, &mAttr, &mSpec,
        &chanmap, BackendType::Playback);
    if(!mStream) return ALC_FALSE;

    pa_stream_set_state_callback(mStream, &PulsePlayback::streamStateCallbackC, this);
    pa_stream_set_moved_callback(mStream, &PulsePlayback::streamMovedCallbackC, this);

    mSpec = *(pa_stream_get_sample_spec(mStream));
    mFrameSize = pa_frame_size(&mSpec);

    if(mDevice->Frequency != mSpec.rate)
    {
        /* Server updated our playback rate, so modify the buffer attribs
         * accordingly. */
        mDevice->NumUpdates = static_cast<ALuint>(clampd(
            static_cast<ALdouble>(mSpec.rate)/mDevice->Frequency*mDevice->NumUpdates + 0.5, 2.0, 16.0));

        period_size = mDevice->UpdateSize * mFrameSize;
        mAttr.maxlength = -1;
        mAttr.tlength = period_size * maxu(mDevice->NumUpdates, 2);
        mAttr.prebuf = 0;
        mAttr.minreq = period_size;

        op = pa_stream_set_buffer_attr(mStream, &mAttr, stream_success_callback, mLoop);
        wait_for_operation(op, mLoop);

        mDevice->Frequency = mSpec.rate;
    }

    pa_stream_set_buffer_attr_callback(mStream, &PulsePlayback::bufferAttrCallbackC, this);
    bufferAttrCallback(mStream);

    mDevice->NumUpdates = clampu((mAttr.tlength + mAttr.minreq/2u) / mAttr.minreq, 2u, 16u);
    mDevice->UpdateSize = mAttr.minreq / mFrameSize;

    /* HACK: prebuf should be 0 as that's what we set it to. However on some
     * systems it comes back as non-0, so we have to make sure the device will
     * write enough audio to start playback. The lack of manual start control
     * may have unintended consequences, but it's better than not starting at
     * all.
     */
    if(mAttr.prebuf != 0)
    {
        ALuint len{mAttr.prebuf / mFrameSize};
        if(len <= mDevice->UpdateSize*mDevice->NumUpdates)
            ERR("Non-0 prebuf, %u samples (%u bytes), device has %u samples\n",
                len, mAttr.prebuf, mDevice->UpdateSize*mDevice->NumUpdates);
        else
        {
            ERR("Large prebuf, %u samples (%u bytes), increasing device from %u samples",
                len, mAttr.prebuf, mDevice->UpdateSize*mDevice->NumUpdates);
            mDevice->NumUpdates = (len+mDevice->UpdateSize-1) / mDevice->UpdateSize;
        }
    }

    return ALC_TRUE;
}

ALCboolean PulsePlayback::start()
{
    unique_palock palock{mLoop};

    pa_stream_set_write_callback(mStream, &PulsePlayback::streamWriteCallbackC, this);
    pa_operation *op{pa_stream_cork(mStream, 0, stream_success_callback, mLoop)};
    wait_for_operation(op, mLoop);

    return ALC_TRUE;
}

void PulsePlayback::stop()
{
    unique_palock palock{mLoop};

    pa_stream_set_write_callback(mStream, nullptr, nullptr);
    pa_operation *op{pa_stream_cork(mStream, 1, stream_success_callback, mLoop)};
    wait_for_operation(op, mLoop);
}


ClockLatency PulsePlayback::getClockLatency()
{
    ClockLatency ret;
    pa_usec_t latency;
    int neg, err;

    { palock_guard _{mLoop};
        ret.ClockTime = GetDeviceClockTime(mDevice);
        err = pa_stream_get_latency(mStream, &latency, &neg);
    }

    if(UNLIKELY(err != 0))
    {
        /* FIXME: if err = -PA_ERR_NODATA, it means we were called too soon
         * after starting the stream and no timing info has been received from
         * the server yet. Should we wait, possibly stalling the app, or give a
         * dummy value? Either way, it shouldn't be 0. */
        if(err != -PA_ERR_NODATA)
            ERR("Failed to get stream latency: 0x%x\n", err);
        latency = 0;
        neg = 0;
    }
    else if(UNLIKELY(neg))
        latency = 0;
    ret.Latency = std::chrono::microseconds{latency};

    return ret;
}


void PulsePlayback::lock()
{ pa_threaded_mainloop_lock(mLoop); }

void PulsePlayback::unlock()
{ pa_threaded_mainloop_unlock(mLoop); }


struct PulseCapture final : public BackendBase {
    PulseCapture(ALCdevice *device) noexcept : BackendBase{device} { }
    ~PulseCapture() override;

    static void contextStateCallbackC(pa_context *context, void *pdata);
    void contextStateCallback(pa_context *context);

    static void streamStateCallbackC(pa_stream *stream, void *pdata);
    void streamStateCallback(pa_stream *stream);

    static void sourceNameCallbackC(pa_context *context, const pa_source_info *info, int eol, void *pdata);
    void sourceNameCallback(pa_context *context, const pa_source_info *info, int eol);

    static void streamMovedCallbackC(pa_stream *stream, void *pdata);
    void streamMovedCallback(pa_stream *stream);

    ALCenum open(const ALCchar *name) override;
    ALCboolean start() override;
    void stop() override;
    ALCenum captureSamples(ALCvoid *buffer, ALCuint samples) override;
    ALCuint availableSamples() override;
    ClockLatency getClockLatency() override;
    void lock() override;
    void unlock() override;

    std::string mDeviceName;

    const void *mCapStore{nullptr};
    size_t mCapLen{0u};
    size_t mCapRemain{0u};

    ALCuint mLastReadable{0u};

    pa_buffer_attr mAttr{};
    pa_sample_spec mSpec{};

    pa_threaded_mainloop *mLoop{nullptr};

    pa_stream *mStream{nullptr};
    pa_context *mContext{nullptr};

    static constexpr inline const char *CurrentPrefix() noexcept { return "PulseCapture::"; }
    DEF_NEWDEL(PulseCapture)
};

PulseCapture::~PulseCapture()
{
    if(!mLoop)
        return;
    pulse_close(mLoop, mContext, mStream);
    mLoop = nullptr;
    mContext = nullptr;
    mStream = nullptr;
}

void PulseCapture::contextStateCallbackC(pa_context *context, void *pdata)
{ static_cast<PulseCapture*>(pdata)->contextStateCallback(context); }

void PulseCapture::contextStateCallback(pa_context *context)
{
    if(pa_context_get_state(context) == PA_CONTEXT_FAILED)
    {
        ERR("Received context failure!\n");
        aluHandleDisconnect(mDevice, "Capture state failure");
    }
    pa_threaded_mainloop_signal(mLoop, 0);
}

void PulseCapture::streamStateCallbackC(pa_stream *stream, void *pdata)
{ static_cast<PulseCapture*>(pdata)->streamStateCallback(stream); }

void PulseCapture::streamStateCallback(pa_stream *stream)
{
    if(pa_stream_get_state(stream) == PA_STREAM_FAILED)
    {
        ERR("Received stream failure!\n");
        aluHandleDisconnect(mDevice, "Capture stream failure");
    }
    pa_threaded_mainloop_signal(mLoop, 0);
}

void PulseCapture::sourceNameCallbackC(pa_context *context, const pa_source_info *info, int eol, void *pdata)
{ static_cast<PulseCapture*>(pdata)->sourceNameCallback(context, info, eol); }

void PulseCapture::sourceNameCallback(pa_context* UNUSED(context), const pa_source_info *info, int eol)
{
    if(eol)
    {
        pa_threaded_mainloop_signal(mLoop, 0);
        return;
    }
    mDevice->DeviceName = info->description;
}

void PulseCapture::streamMovedCallbackC(pa_stream *stream, void *pdata)
{ static_cast<PulseCapture*>(pdata)->streamMovedCallback(stream); }

void PulseCapture::streamMovedCallback(pa_stream *stream)
{
    mDeviceName = pa_stream_get_device_name(stream);
    TRACE("Stream moved to %s\n", mDeviceName.c_str());
}


ALCenum PulseCapture::open(const ALCchar *name)
{
    const char *pulse_name{nullptr};
    if(name)
    {
        if(CaptureDevices.empty())
            probeCaptureDevices();

        auto iter = std::find_if(CaptureDevices.cbegin(), CaptureDevices.cend(),
            [name](const DevMap &entry) -> bool
            { return entry.name == name; }
        );
        if(iter == CaptureDevices.cend())
            return ALC_INVALID_VALUE;
        pulse_name = iter->device_name.c_str();
        mDevice->DeviceName = iter->name;
    }

    std::tie(mLoop, mContext) = pulse_open(&PulseCapture::contextStateCallbackC, this);
    if(!mLoop) return ALC_INVALID_VALUE;

    unique_palock palock{mLoop};

    switch(mDevice->FmtType)
    {
        case DevFmtUByte:
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
            ERR("%s capture samples not supported\n", DevFmtTypeString(mDevice->FmtType));
            return ALC_INVALID_VALUE;
    }

    const char *mapname{nullptr};
    pa_channel_map chanmap;
    switch(mDevice->FmtChans)
    {
        case DevFmtMono:
            mapname = "mono";
            break;
        case DevFmtStereo:
            mapname = "front-left,front-right";
            break;
        case DevFmtQuad:
            mapname = "front-left,front-right,rear-left,rear-right";
            break;
        case DevFmtX51:
            mapname = "front-left,front-right,front-center,lfe,side-left,side-right";
            break;
        case DevFmtX51Rear:
            mapname = "front-left,front-right,front-center,lfe,rear-left,rear-right";
            break;
        case DevFmtX61:
            mapname = "front-left,front-right,front-center,lfe,rear-center,side-left,side-right";
            break;
        case DevFmtX71:
            mapname = "front-left,front-right,front-center,lfe,rear-left,rear-right,side-left,side-right";
            break;
        case DevFmtAmbi3D:
            ERR("%s capture samples not supported\n", DevFmtChannelsString(mDevice->FmtChans));
            return ALC_INVALID_VALUE;
    }
    if(!pa_channel_map_parse(&chanmap, mapname))
    {
        ERR("Failed to build channel map for %s\n", DevFmtChannelsString(mDevice->FmtChans));
        return ALC_INVALID_VALUE;
    }

    mSpec.rate = mDevice->Frequency;
    mSpec.channels = mDevice->channelsFromFmt();

    if(pa_sample_spec_valid(&mSpec) == 0)
    {
        ERR("Invalid sample format\n");
        return ALC_INVALID_VALUE;
    }

    if(!pa_channel_map_init_auto(&chanmap, mSpec.channels, PA_CHANNEL_MAP_WAVEEX))
    {
        ERR("Couldn't build map for channel count (%d)!\n", mSpec.channels);
        return ALC_INVALID_VALUE;
    }

    ALuint samples{mDevice->UpdateSize * mDevice->NumUpdates};
    samples = maxu(samples, 100 * mDevice->Frequency / 1000);

    mAttr.minreq = -1;
    mAttr.prebuf = -1;
    mAttr.maxlength = samples * pa_frame_size(&mSpec);
    mAttr.tlength = -1;
    mAttr.fragsize = minu(samples, 50*mDevice->Frequency/1000) * pa_frame_size(&mSpec);

    pa_stream_flags_t flags{PA_STREAM_START_CORKED|PA_STREAM_ADJUST_LATENCY};
    if(!GetConfigValueBool(nullptr, "pulse", "allow-moves", 0))
        flags |= PA_STREAM_DONT_MOVE;

    TRACE("Connecting to \"%s\"\n", pulse_name ? pulse_name : "(default)");
    mStream = pulse_connect_stream(pulse_name, mLoop, mContext, flags, &mAttr, &mSpec, &chanmap,
        BackendType::Capture);
    if(!mStream) return ALC_INVALID_VALUE;

    pa_stream_set_moved_callback(mStream, &PulseCapture::streamMovedCallbackC, this);
    pa_stream_set_state_callback(mStream, &PulseCapture::streamStateCallbackC, this);

    mDeviceName = pa_stream_get_device_name(mStream);
    if(mDevice->DeviceName.empty())
    {
        pa_operation *op{pa_context_get_source_info_by_name(mContext, mDeviceName.c_str(),
            &PulseCapture::sourceNameCallbackC, this)};
        wait_for_operation(op, mLoop);
    }

    return ALC_NO_ERROR;
}

ALCboolean PulseCapture::start()
{
    palock_guard _{mLoop};
    pa_operation *op{pa_stream_cork(mStream, 0, stream_success_callback, mLoop)};
    wait_for_operation(op, mLoop);
    return ALC_TRUE;
}

void PulseCapture::stop()
{
    palock_guard _{mLoop};
    pa_operation *op{pa_stream_cork(mStream, 1, stream_success_callback, mLoop)};
    wait_for_operation(op, mLoop);
}

ALCenum PulseCapture::captureSamples(ALCvoid *buffer, ALCuint samples)
{
    ALCuint todo{samples * static_cast<ALCuint>(pa_frame_size(&mSpec))};

    /* Capture is done in fragment-sized chunks, so we loop until we get all
     * that's available */
    mLastReadable -= todo;
    unique_palock palock{mLoop};
    while(todo > 0)
    {
        size_t rem{todo};

        if(mCapLen == 0)
        {
            pa_stream_state_t state{pa_stream_get_state(mStream)};
            if(!PA_STREAM_IS_GOOD(state))
            {
                aluHandleDisconnect(mDevice, "Bad capture state: %u", state);
                return ALC_INVALID_DEVICE;
            }
            if(pa_stream_peek(mStream, &mCapStore, &mCapLen) < 0)
            {
                ERR("pa_stream_peek() failed: %s\n",
                    pa_strerror(pa_context_errno(mContext)));
                aluHandleDisconnect(mDevice, "Failed retrieving capture samples: %s",
                                    pa_strerror(pa_context_errno(mContext)));
                return ALC_INVALID_DEVICE;
            }
            mCapRemain = mCapLen;
        }
        rem = minz(rem, mCapRemain);

        memcpy(buffer, mCapStore, rem);

        buffer = static_cast<ALbyte*>(buffer) + rem;
        todo -= rem;

        mCapStore = reinterpret_cast<const ALbyte*>(mCapStore) + rem;
        mCapRemain -= rem;
        if(mCapRemain == 0)
        {
            pa_stream_drop(mStream);
            mCapLen = 0;
        }
    }
    palock.unlock();
    if(todo > 0)
        memset(buffer, ((mDevice->FmtType==DevFmtUByte) ? 0x80 : 0), todo);

    return ALC_NO_ERROR;
}

ALCuint PulseCapture::availableSamples()
{
    size_t readable{mCapRemain};

    if(mDevice->Connected.load(std::memory_order_acquire))
    {
        palock_guard _{mLoop};
        size_t got{pa_stream_readable_size(mStream)};
        if(static_cast<ssize_t>(got) < 0)
        {
            ERR("pa_stream_readable_size() failed: %s\n", pa_strerror(got));
            aluHandleDisconnect(mDevice, "Failed getting readable size: %s", pa_strerror(got));
        }
        else if(got > mCapLen)
            readable += got - mCapLen;
    }

    if(mLastReadable < readable)
        mLastReadable = readable;
    return mLastReadable / pa_frame_size(&mSpec);
}


ClockLatency PulseCapture::getClockLatency()
{
    ClockLatency ret;
    pa_usec_t latency;
    int neg, err;

    { palock_guard _{mLoop};
        ret.ClockTime = GetDeviceClockTime(mDevice);
        err = pa_stream_get_latency(mStream, &latency, &neg);
    }

    if(UNLIKELY(err != 0))
    {
        ERR("Failed to get stream latency: 0x%x\n", err);
        latency = 0;
        neg = 0;
    }
    else if(UNLIKELY(neg))
        latency = 0;
    ret.Latency = std::chrono::microseconds{latency};

    return ret;
}


void PulseCapture::lock()
{ pa_threaded_mainloop_lock(mLoop); }

void PulseCapture::unlock()
{ pa_threaded_mainloop_unlock(mLoop); }

} // namespace


bool PulseBackendFactory::init()
{
#ifdef HAVE_DYNLOAD
    if(!pa_handle)
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
        pa_handle = LoadLib(PALIB);
        if(!pa_handle)
        {
            WARN("Failed to load %s\n", PALIB);
            return false;
        }

#define LOAD_FUNC(x) do {                                                     \
    p##x = reinterpret_cast<decltype(p##x)>(GetSymbol(pa_handle, #x));        \
    if(!(p##x)) {                                                             \
        ret = false;                                                          \
        missing_funcs += "\n" #x;                                             \
    }                                                                         \
} while(0)
        LOAD_FUNC(pa_context_unref);
        LOAD_FUNC(pa_sample_spec_valid);
        LOAD_FUNC(pa_stream_drop);
        LOAD_FUNC(pa_frame_size);
        LOAD_FUNC(pa_strerror);
        LOAD_FUNC(pa_context_get_state);
        LOAD_FUNC(pa_stream_get_state);
        LOAD_FUNC(pa_threaded_mainloop_signal);
        LOAD_FUNC(pa_stream_peek);
        LOAD_FUNC(pa_threaded_mainloop_wait);
        LOAD_FUNC(pa_threaded_mainloop_unlock);
        LOAD_FUNC(pa_threaded_mainloop_in_thread);
        LOAD_FUNC(pa_context_new);
        LOAD_FUNC(pa_threaded_mainloop_stop);
        LOAD_FUNC(pa_context_disconnect);
        LOAD_FUNC(pa_threaded_mainloop_start);
        LOAD_FUNC(pa_threaded_mainloop_get_api);
        LOAD_FUNC(pa_context_set_state_callback);
        LOAD_FUNC(pa_stream_write);
        LOAD_FUNC(pa_xfree);
        LOAD_FUNC(pa_stream_connect_record);
        LOAD_FUNC(pa_stream_connect_playback);
        LOAD_FUNC(pa_stream_readable_size);
        LOAD_FUNC(pa_stream_writable_size);
        LOAD_FUNC(pa_stream_is_corked);
        LOAD_FUNC(pa_stream_cork);
        LOAD_FUNC(pa_stream_is_suspended);
        LOAD_FUNC(pa_stream_get_device_name);
        LOAD_FUNC(pa_stream_get_latency);
        LOAD_FUNC(pa_path_get_filename);
        LOAD_FUNC(pa_get_binary_name);
        LOAD_FUNC(pa_threaded_mainloop_free);
        LOAD_FUNC(pa_context_errno);
        LOAD_FUNC(pa_xmalloc);
        LOAD_FUNC(pa_stream_unref);
        LOAD_FUNC(pa_threaded_mainloop_accept);
        LOAD_FUNC(pa_stream_set_write_callback);
        LOAD_FUNC(pa_threaded_mainloop_new);
        LOAD_FUNC(pa_context_connect);
        LOAD_FUNC(pa_stream_set_buffer_attr);
        LOAD_FUNC(pa_stream_get_buffer_attr);
        LOAD_FUNC(pa_stream_get_sample_spec);
        LOAD_FUNC(pa_stream_get_time);
        LOAD_FUNC(pa_stream_set_read_callback);
        LOAD_FUNC(pa_stream_set_state_callback);
        LOAD_FUNC(pa_stream_set_moved_callback);
        LOAD_FUNC(pa_stream_set_underflow_callback);
        LOAD_FUNC(pa_stream_new_with_proplist);
        LOAD_FUNC(pa_stream_disconnect);
        LOAD_FUNC(pa_threaded_mainloop_lock);
        LOAD_FUNC(pa_channel_map_init_auto);
        LOAD_FUNC(pa_channel_map_parse);
        LOAD_FUNC(pa_channel_map_snprint);
        LOAD_FUNC(pa_channel_map_equal);
        LOAD_FUNC(pa_context_get_server_info);
        LOAD_FUNC(pa_context_get_sink_info_by_name);
        LOAD_FUNC(pa_context_get_sink_info_list);
        LOAD_FUNC(pa_context_get_source_info_by_name);
        LOAD_FUNC(pa_context_get_source_info_list);
        LOAD_FUNC(pa_operation_get_state);
        LOAD_FUNC(pa_operation_unref);
        LOAD_FUNC(pa_proplist_new);
        LOAD_FUNC(pa_proplist_free);
        LOAD_FUNC(pa_proplist_set);
        LOAD_FUNC(pa_channel_map_superset);
        LOAD_FUNC(pa_stream_set_buffer_attr_callback);
        LOAD_FUNC(pa_stream_begin_write);
#undef LOAD_FUNC

        if(!ret)
        {
            WARN("Missing expected functions:%s\n", missing_funcs.c_str());
            CloseLib(pa_handle);
            pa_handle = nullptr;
            return false;
        }
    }
#endif /* HAVE_DYNLOAD */

    pulse_ctx_flags = PA_CONTEXT_NOFLAGS;
    if(!GetConfigValueBool(nullptr, "pulse", "spawn-server", 1))
        pulse_ctx_flags |= PA_CONTEXT_NOAUTOSPAWN;

    bool ret{false};
    pa_threaded_mainloop *loop{pa_threaded_mainloop_new()};
    if(loop && pa_threaded_mainloop_start(loop) >= 0)
    {
        unique_palock palock{loop};
        pa_context *context{connect_context(loop, AL_TRUE)};
        if(context)
        {
            ret = true;

            /* Some libraries (Phonon, Qt) set some pulseaudio properties
             * through environment variables, which causes all streams in the
             * process to inherit them. This attempts to filter those
             * properties out by setting them to 0-length data.
             */
            prop_filter = pa_proplist_new();
            pa_proplist_set(prop_filter, PA_PROP_MEDIA_ROLE, nullptr, 0);
            pa_proplist_set(prop_filter, "phonon.streamid", nullptr, 0);

            pa_context_disconnect(context);
            pa_context_unref(context);
        }
        palock.unlock();
        pa_threaded_mainloop_stop(loop);
    }
    if(loop)
        pa_threaded_mainloop_free(loop);

    return ret;
}

bool PulseBackendFactory::querySupport(BackendType type)
{ return type == BackendType::Playback || type == BackendType::Capture; }

void PulseBackendFactory::probe(DevProbe type, std::string *outnames)
{
    auto add_device = [outnames](const DevMap &entry) -> void
    {
        /* +1 to also append the null char (to ensure a null-separated list and
         * double-null terminated list).
         */
        outnames->append(entry.name.c_str(), entry.name.length()+1);
    };
    switch(type)
    {
        case DevProbe::Playback:
            probePlaybackDevices();
            std::for_each(PlaybackDevices.cbegin(), PlaybackDevices.cend(), add_device);
            break;

        case DevProbe::Capture:
            probeCaptureDevices();
            std::for_each(CaptureDevices.cbegin(), CaptureDevices.cend(), add_device);
            break;
    }
}

BackendPtr PulseBackendFactory::createBackend(ALCdevice *device, BackendType type)
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
