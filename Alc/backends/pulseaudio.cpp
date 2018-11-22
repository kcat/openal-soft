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

#include <string.h>

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

#if PA_API_VERSION == 12

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

ALCboolean pulse_load(void)
{
    ALCboolean ret{ALC_TRUE};
#ifdef HAVE_DYNLOAD
    if(!pa_handle)
    {
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
            return ALC_FALSE;
        }

#define LOAD_FUNC(x) do {                                                     \
    p##x = reinterpret_cast<decltype(p##x)>(GetSymbol(pa_handle, #x));        \
    if(!(p##x)) {                                                             \
        ret = ALC_FALSE;                                                      \
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

        if(ret == ALC_FALSE)
        {
            WARN("Missing expected functions:%s\n", missing_funcs.c_str());
            CloseLib(pa_handle);
            pa_handle = nullptr;
        }
    }
#endif /* HAVE_DYNLOAD */
    return ret;
}


/* *grumble* Don't use enums for bitflags. */
inline pa_stream_flags_t operator|(pa_stream_flags_t lhs, pa_stream_flags_t rhs)
{ return pa_stream_flags_t(int(lhs) | int(rhs)); }

inline pa_stream_flags_t operator|=(pa_stream_flags_t &lhs, pa_stream_flags_t rhs)
{
    lhs = pa_stream_flags_t(int(lhs) | int(rhs));
    return lhs;
}

inline pa_context_flags_t operator|=(pa_context_flags_t &lhs, pa_context_flags_t rhs)
{
    lhs = pa_context_flags_t(int(lhs) | int(rhs));
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
    auto loop = reinterpret_cast<pa_threaded_mainloop*>(pdata);
    pa_context_state_t state{pa_context_get_state(context)};
    if(state == PA_CONTEXT_READY || !PA_CONTEXT_IS_GOOD(state))
        pa_threaded_mainloop_signal(loop, 0);
}

void stream_state_callback(pa_stream *stream, void *pdata)
{
    auto loop = reinterpret_cast<pa_threaded_mainloop*>(pdata);
    pa_stream_state_t state{pa_stream_get_state(stream)};
    if(state == PA_STREAM_READY || !PA_STREAM_IS_GOOD(state))
        pa_threaded_mainloop_signal(loop, 0);
}

void stream_success_callback(pa_stream *UNUSED(stream), int UNUSED(success), void *pdata)
{
    auto loop = reinterpret_cast<pa_threaded_mainloop*>(pdata);
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

    PathNamePair binname = GetProcBinary();
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

bool checkName(const std::vector<DevMap> &list, const std::string &name)
{
    return std::find_if(list.cbegin(), list.cend(),
        [&name](const DevMap &entry) -> bool
        { return entry.name == name; }
    ) != list.cend();
}

std::vector<DevMap> PlaybackDevices;
std::vector<DevMap> CaptureDevices;


struct PulsePlayback final : public ALCbackend {
    std::string device_name;

    pa_buffer_attr attr;
    pa_sample_spec spec;

    pa_threaded_mainloop *loop{nullptr};

    pa_stream *stream{nullptr};
    pa_context *context{nullptr};

    std::atomic<ALenum> killNow{ALC_TRUE};
    std::thread thread;
};

void PulsePlayback_deviceCallback(pa_context *context, const pa_sink_info *info, int eol, void *pdata);
void PulsePlayback_probeDevices(void);

void PulsePlayback_bufferAttrCallback(pa_stream *stream, void *pdata);
void PulsePlayback_contextStateCallback(pa_context *context, void *pdata);
void PulsePlayback_streamStateCallback(pa_stream *stream, void *pdata);
void PulsePlayback_streamWriteCallback(pa_stream *p, size_t nbytes, void *userdata);
void PulsePlayback_sinkInfoCallback(pa_context *context, const pa_sink_info *info, int eol, void *pdata);
void PulsePlayback_sinkNameCallback(pa_context *context, const pa_sink_info *info, int eol, void *pdata);
void PulsePlayback_streamMovedCallback(pa_stream *stream, void *pdata);
pa_stream *PulsePlayback_connectStream(const char *device_name, pa_threaded_mainloop *loop,
                                       pa_context *context, pa_stream_flags_t flags,
                                       pa_buffer_attr *attr, pa_sample_spec *spec,
                                       pa_channel_map *chanmap);
int PulsePlayback_mixerProc(PulsePlayback *self);

void PulsePlayback_Construct(PulsePlayback *self, ALCdevice *device);
void PulsePlayback_Destruct(PulsePlayback *self);
ALCenum PulsePlayback_open(PulsePlayback *self, const ALCchar *name);
ALCboolean PulsePlayback_reset(PulsePlayback *self);
ALCboolean PulsePlayback_start(PulsePlayback *self);
void PulsePlayback_stop(PulsePlayback *self);
DECLARE_FORWARD2(PulsePlayback, ALCbackend, ALCenum, captureSamples, ALCvoid*, ALCuint)
DECLARE_FORWARD(PulsePlayback, ALCbackend, ALCuint, availableSamples)
ClockLatency PulsePlayback_getClockLatency(PulsePlayback *self);
void PulsePlayback_lock(PulsePlayback *self);
void PulsePlayback_unlock(PulsePlayback *self);
DECLARE_DEFAULT_ALLOCATORS(PulsePlayback)

DEFINE_ALCBACKEND_VTABLE(PulsePlayback);


void PulsePlayback_Construct(PulsePlayback *self, ALCdevice *device)
{
    new (self) PulsePlayback();
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(PulsePlayback, ALCbackend, self);
}

void PulsePlayback_Destruct(PulsePlayback *self)
{
    if(self->loop)
    {
        pulse_close(self->loop, self->context, self->stream);
        self->loop = nullptr;
        self->context = nullptr;
        self->stream = nullptr;
    }
    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
    self->~PulsePlayback();
}


void PulsePlayback_deviceCallback(pa_context *UNUSED(context), const pa_sink_info *info, int eol, void *pdata)
{
    auto loop = reinterpret_cast<pa_threaded_mainloop*>(pdata);

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

void PulsePlayback_probeDevices(void)
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

            pa_stream *stream{PulsePlayback_connectStream(nullptr,
                loop, context, flags, nullptr, &spec, nullptr
            )};
            if(stream)
            {
                pa_operation *op{pa_context_get_sink_info_by_name(context,
                    pa_stream_get_device_name(stream), PulsePlayback_deviceCallback, loop
                )};
                wait_for_operation(op, loop);

                pa_stream_disconnect(stream);
                pa_stream_unref(stream);
                stream = nullptr;
            }

            pa_operation *op{pa_context_get_sink_info_list(context,
                PulsePlayback_deviceCallback, loop
            )};
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


void PulsePlayback_bufferAttrCallback(pa_stream *stream, void *pdata)
{
    auto self = reinterpret_cast<PulsePlayback*>(pdata);

    self->attr = *pa_stream_get_buffer_attr(stream);
    TRACE("minreq=%d, tlength=%d, prebuf=%d\n", self->attr.minreq, self->attr.tlength, self->attr.prebuf);
    /* FIXME: Update the device's UpdateSize (and/or NumUpdates) using the new
     * buffer attributes? Changing UpdateSize will change the ALC_REFRESH
     * property, which probably shouldn't change between device resets. But
     * leaving it alone means ALC_REFRESH will be off.
     */
}

void PulsePlayback_contextStateCallback(pa_context *context, void *pdata)
{
    auto self = reinterpret_cast<PulsePlayback*>(pdata);
    if(pa_context_get_state(context) == PA_CONTEXT_FAILED)
    {
        ERR("Received context failure!\n");
        aluHandleDisconnect(STATIC_CAST(ALCbackend,self)->mDevice, "Playback state failure");
    }
    pa_threaded_mainloop_signal(self->loop, 0);
}

void PulsePlayback_streamStateCallback(pa_stream *stream, void *pdata)
{
    auto self = reinterpret_cast<PulsePlayback*>(pdata);
    if(pa_stream_get_state(stream) == PA_STREAM_FAILED)
    {
        ERR("Received stream failure!\n");
        aluHandleDisconnect(STATIC_CAST(ALCbackend,self)->mDevice, "Playback stream failure");
    }
    pa_threaded_mainloop_signal(self->loop, 0);
}

void PulsePlayback_streamWriteCallback(pa_stream* UNUSED(p), size_t UNUSED(nbytes), void *pdata)
{
    auto self = reinterpret_cast<PulsePlayback*>(pdata);
    pa_threaded_mainloop_signal(self->loop, 0);
}

void PulsePlayback_sinkInfoCallback(pa_context *UNUSED(context), const pa_sink_info *info, int eol, void *pdata)
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
    auto self = reinterpret_cast<PulsePlayback*>(pdata);

    if(eol)
    {
        pa_threaded_mainloop_signal(self->loop, 0);
        return;
    }

    ALCdevice *device{STATIC_CAST(ALCbackend,self)->mDevice};
    auto chanmap = std::find_if(chanmaps.cbegin(), chanmaps.cend(),
        [info](const ChannelMap &chanmap) -> bool
        { return pa_channel_map_superset(&info->channel_map, &chanmap.map); }
    );
    if(chanmap != chanmaps.cend())
    {
        if(!(device->Flags&DEVICE_CHANNELS_REQUEST))
            device->FmtChans = chanmap->chans;
    }
    else
    {
        char chanmap_str[PA_CHANNEL_MAP_SNPRINT_MAX]{};
        pa_channel_map_snprint(chanmap_str, sizeof(chanmap_str), &info->channel_map);
        WARN("Failed to find format for channel map:\n    %s\n", chanmap_str);
    }

    if(info->active_port)
        TRACE("Active port: %s (%s)\n", info->active_port->name, info->active_port->description);
    device->IsHeadphones = (info->active_port &&
                            strcmp(info->active_port->name, "analog-output-headphones") == 0 &&
                            device->FmtChans == DevFmtStereo);
}

void PulsePlayback_sinkNameCallback(pa_context *UNUSED(context), const pa_sink_info *info, int eol, void *pdata)
{
    auto self = reinterpret_cast<PulsePlayback*>(pdata);

    if(eol)
    {
        pa_threaded_mainloop_signal(self->loop, 0);
        return;
    }

    ALCdevice *device{STATIC_CAST(ALCbackend,self)->mDevice};
    device->DeviceName = info->description;
}


void PulsePlayback_streamMovedCallback(pa_stream *stream, void *pdata)
{
    auto self = reinterpret_cast<PulsePlayback*>(pdata);

    self->device_name = pa_stream_get_device_name(stream);

    TRACE("Stream moved to %s\n", self->device_name.c_str());
}


pa_stream *PulsePlayback_connectStream(const char *device_name,
    pa_threaded_mainloop *loop, pa_context *context,
    pa_stream_flags_t flags, pa_buffer_attr *attr, pa_sample_spec *spec,
    pa_channel_map *chanmap)
{
    if(!device_name)
    {
        device_name = getenv("ALSOFT_PULSE_DEFAULT");
        if(device_name && !device_name[0])
            device_name = nullptr;
    }

    pa_stream *stream{pa_stream_new_with_proplist(context,
        "Playback Stream", spec, chanmap, prop_filter
    )};
    if(!stream)
    {
        ERR("pa_stream_new_with_proplist() failed: %s\n", pa_strerror(pa_context_errno(context)));
        return nullptr;
    }

    pa_stream_set_state_callback(stream, stream_state_callback, loop);

    if(pa_stream_connect_playback(stream, device_name, attr, flags, nullptr, nullptr) < 0)
    {
        ERR("Stream did not connect: %s\n", pa_strerror(pa_context_errno(context)));
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


int PulsePlayback_mixerProc(PulsePlayback *self)
{
    ALCdevice *device{STATIC_CAST(ALCbackend,self)->mDevice};

    SetRTPriority();
    althrd_setname(MIXER_THREAD_NAME);

    unique_palock palock{self->loop};
    size_t frame_size{pa_frame_size(&self->spec)};

    while(!self->killNow.load(std::memory_order_acquire) &&
          ATOMIC_LOAD(&device->Connected, almemory_order_acquire))
    {
        ssize_t len{static_cast<ssize_t>(pa_stream_writable_size(self->stream))};
        if(UNLIKELY(len < 0))
        {
            ERR("Failed to get writable size: %ld", (long)len);
            aluHandleDisconnect(device, "Failed to get writable size: %ld", (long)len);
            break;
        }

        /* Make sure we're going to write at least 2 'periods' (minreqs), in
         * case the server increased it since starting playback. Also round up
         * the number of writable periods if it's not an integer count.
         */
        ALint buffer_size{static_cast<int32_t>(self->attr.minreq) * maxi(
            (self->attr.tlength + self->attr.minreq/2) / self->attr.minreq, 2
        )};

        /* NOTE: This assumes pa_stream_writable_size returns between 0 and
         * tlength, else there will be more latency than intended.
         */
        len = buffer_size - maxi((ssize_t)self->attr.tlength - len, 0);
        if(len < self->attr.minreq)
        {
            if(pa_stream_is_corked(self->stream))
            {
                pa_operation *op{pa_stream_cork(self->stream, 0, nullptr, nullptr)};
                if(op) pa_operation_unref(op);
            }
            pa_threaded_mainloop_wait(self->loop);
            continue;
        }

        len -= len%self->attr.minreq;
        len -= len%frame_size;

        void *buf{pa_xmalloc(len)};
        aluMixData(device, buf, len/frame_size);

        int ret{pa_stream_write(self->stream, buf, len, pa_xfree, 0, PA_SEEK_RELATIVE)};
        if(UNLIKELY(ret != PA_OK))
            ERR("Failed to write to stream: %d, %s\n", ret, pa_strerror(ret));
    }

    return 0;
}


ALCenum PulsePlayback_open(PulsePlayback *self, const ALCchar *name)
{
    const char *pulse_name{nullptr};
    const char *dev_name{nullptr};

    if(name)
    {
        if(PlaybackDevices.empty())
            PulsePlayback_probeDevices();

        auto iter = std::find_if(PlaybackDevices.cbegin(), PlaybackDevices.cend(),
            [name](const DevMap &entry) -> bool
            { return entry.name == name; }
        );
        if(iter == PlaybackDevices.cend())
            return ALC_INVALID_VALUE;
        pulse_name = iter->device_name.c_str();
        dev_name = iter->name.c_str();
    }

    std::tie(self->loop, self->context) = pulse_open(PulsePlayback_contextStateCallback, self);
    if(!self->loop) return ALC_INVALID_VALUE;

    unique_palock palock{self->loop};

    pa_stream_flags_t flags{PA_STREAM_FIX_FORMAT | PA_STREAM_FIX_RATE |
                            PA_STREAM_FIX_CHANNELS};
    if(!GetConfigValueBool(nullptr, "pulse", "allow-moves", 0))
        flags |= PA_STREAM_DONT_MOVE;

    pa_sample_spec spec{};
    spec.format = PA_SAMPLE_S16NE;
    spec.rate = 44100;
    spec.channels = 2;

    TRACE("Connecting to \"%s\"\n", pulse_name ? pulse_name : "(default)");
    self->stream = PulsePlayback_connectStream(pulse_name, self->loop, self->context,
                                               flags, nullptr, &spec, nullptr);
    if(!self->stream)
    {
        palock = unique_palock{};
        pulse_close(self->loop, self->context, self->stream);
        self->loop = nullptr;
        self->context = nullptr;
        return ALC_INVALID_VALUE;
    }
    pa_stream_set_moved_callback(self->stream, PulsePlayback_streamMovedCallback, self);

    self->device_name = pa_stream_get_device_name(self->stream);
    if(!dev_name)
    {
        pa_operation *op{pa_context_get_sink_info_by_name(self->context,
            self->device_name.c_str(), PulsePlayback_sinkNameCallback, self
        )};
        wait_for_operation(op, self->loop);
    }
    else
    {
        ALCdevice *device{STATIC_CAST(ALCbackend,self)->mDevice};
        device->DeviceName = dev_name;
    }

    return ALC_NO_ERROR;
}

ALCboolean PulsePlayback_reset(PulsePlayback *self)
{
    unique_palock palock{self->loop};

    if(self->stream)
    {
        pa_stream_set_state_callback(self->stream, nullptr, nullptr);
        pa_stream_set_moved_callback(self->stream, nullptr, nullptr);
        pa_stream_set_write_callback(self->stream, nullptr, nullptr);
        pa_stream_set_buffer_attr_callback(self->stream, nullptr, nullptr);
        pa_stream_disconnect(self->stream);
        pa_stream_unref(self->stream);
        self->stream = nullptr;
    }

    pa_operation *op{pa_context_get_sink_info_by_name(self->context,
        self->device_name.c_str(), PulsePlayback_sinkInfoCallback, self
    )};
    wait_for_operation(op, self->loop);

    ALCdevice *device{STATIC_CAST(ALCbackend,self)->mDevice};
    pa_stream_flags_t flags{PA_STREAM_START_CORKED | PA_STREAM_ADJUST_LATENCY |
                            PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE};
    if(!GetConfigValueBool(nullptr, "pulse", "allow-moves", 0))
        flags |= PA_STREAM_DONT_MOVE;
    if(GetConfigValueBool(device->DeviceName.c_str(), "pulse", "fix-rate", 0) ||
       !(device->Flags&DEVICE_FREQUENCY_REQUEST))
        flags |= PA_STREAM_FIX_RATE;

    switch(device->FmtType)
    {
        case DevFmtByte:
            device->FmtType = DevFmtUByte;
            /* fall-through */
        case DevFmtUByte:
            self->spec.format = PA_SAMPLE_U8;
            break;
        case DevFmtUShort:
            device->FmtType = DevFmtShort;
            /* fall-through */
        case DevFmtShort:
            self->spec.format = PA_SAMPLE_S16NE;
            break;
        case DevFmtUInt:
            device->FmtType = DevFmtInt;
            /* fall-through */
        case DevFmtInt:
            self->spec.format = PA_SAMPLE_S32NE;
            break;
        case DevFmtFloat:
            self->spec.format = PA_SAMPLE_FLOAT32NE;
            break;
    }
    self->spec.rate = device->Frequency;
    self->spec.channels = ChannelsFromDevFmt(device->FmtChans, device->mAmbiOrder);

    if(pa_sample_spec_valid(&self->spec) == 0)
    {
        ERR("Invalid sample format\n");
        return ALC_FALSE;
    }

    const char *mapname{nullptr};
    pa_channel_map chanmap;
    switch(device->FmtChans)
    {
        case DevFmtMono:
            mapname = "mono";
            break;
        case DevFmtAmbi3D:
            device->FmtChans = DevFmtStereo;
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
        ERR("Failed to build channel map for %s\n", DevFmtChannelsString(device->FmtChans));
        return ALC_FALSE;
    }
    SetDefaultWFXChannelOrder(device);

    self->attr.fragsize = -1;
    self->attr.prebuf = 0;
    self->attr.minreq = device->UpdateSize * pa_frame_size(&self->spec);
    self->attr.tlength = self->attr.minreq * maxu(device->NumUpdates, 2);
    self->attr.maxlength = -1;

    self->stream = PulsePlayback_connectStream(self->device_name.c_str(),
        self->loop, self->context, flags, &self->attr, &self->spec, &chanmap
    );
    if(!self->stream)
        return ALC_FALSE;
    pa_stream_set_state_callback(self->stream, PulsePlayback_streamStateCallback, self);
    pa_stream_set_moved_callback(self->stream, PulsePlayback_streamMovedCallback, self);
    pa_stream_set_write_callback(self->stream, PulsePlayback_streamWriteCallback, self);

    self->spec = *(pa_stream_get_sample_spec(self->stream));
    if(device->Frequency != self->spec.rate)
    {
        /* Server updated our playback rate, so modify the buffer attribs
         * accordingly. */
        device->NumUpdates = (ALuint)clampd(
            (ALdouble)device->NumUpdates/device->Frequency*self->spec.rate + 0.5, 2.0, 16.0
        );

        self->attr.minreq  = device->UpdateSize * pa_frame_size(&self->spec);
        self->attr.tlength = self->attr.minreq * device->NumUpdates;
        self->attr.maxlength = -1;
        self->attr.prebuf  = 0;

        op = pa_stream_set_buffer_attr(self->stream, &self->attr,
                                       stream_success_callback, self->loop);
        wait_for_operation(op, self->loop);

        device->Frequency = self->spec.rate;
    }

    pa_stream_set_buffer_attr_callback(self->stream, PulsePlayback_bufferAttrCallback, self);
    PulsePlayback_bufferAttrCallback(self->stream, self);

    device->NumUpdates = (ALuint)clampu64(
        (self->attr.tlength + self->attr.minreq/2) / self->attr.minreq, 2, 16
    );
    device->UpdateSize = self->attr.minreq / pa_frame_size(&self->spec);

    /* HACK: prebuf should be 0 as that's what we set it to. However on some
     * systems it comes back as non-0, so we have to make sure the device will
     * write enough audio to start playback. The lack of manual start control
     * may have unintended consequences, but it's better than not starting at
     * all.
     */
    if(self->attr.prebuf != 0)
    {
        ALuint len{self->attr.prebuf / (ALuint)pa_frame_size(&self->spec)};
        if(len <= device->UpdateSize*device->NumUpdates)
            ERR("Non-0 prebuf, %u samples (%u bytes), device has %u samples\n",
                len, self->attr.prebuf, device->UpdateSize*device->NumUpdates);
        else
        {
            ERR("Large prebuf, %u samples (%u bytes), increasing device from %u samples",
                len, self->attr.prebuf, device->UpdateSize*device->NumUpdates);
            device->NumUpdates = (len+device->UpdateSize-1) / device->UpdateSize;
        }
    }

    return ALC_TRUE;
}

ALCboolean PulsePlayback_start(PulsePlayback *self)
{
    try {
        self->killNow.store(AL_FALSE, std::memory_order_release);
        self->thread = std::thread(PulsePlayback_mixerProc, self);
        return ALC_TRUE;
    }
    catch(std::exception& e) {
        ERR("Failed to start thread: %s\n", e.what());
    }
    catch(...) {
        ERR("Failed to start thread\n");
    }
    return ALC_FALSE;
}

void PulsePlayback_stop(PulsePlayback *self)
{
    self->killNow.store(AL_TRUE, std::memory_order_release);
    if(!self->stream || !self->thread.joinable())
        return;

    /* Signal the main loop in case PulseAudio isn't sending us audio requests
     * (e.g. if the device is suspended). We need to lock the mainloop in case
     * the mixer is between checking the killNow flag but before waiting for
     * the signal.
     */
    unique_palock palock{self->loop};
    palock.unlock();

    pa_threaded_mainloop_signal(self->loop, 0);
    self->thread.join();

    palock.lock();

    pa_operation *op{pa_stream_cork(self->stream, 1, stream_success_callback, self->loop)};
    wait_for_operation(op, self->loop);
}


ClockLatency PulsePlayback_getClockLatency(PulsePlayback *self)
{
    ClockLatency ret;
    pa_usec_t latency;
    int neg, err;

    { palock_guard _{self->loop};
        ret.ClockTime = GetDeviceClockTime(STATIC_CAST(ALCbackend,self)->mDevice);
        err = pa_stream_get_latency(self->stream, &latency, &neg);
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


void PulsePlayback_lock(PulsePlayback *self)
{
    pa_threaded_mainloop_lock(self->loop);
}

void PulsePlayback_unlock(PulsePlayback *self)
{
    pa_threaded_mainloop_unlock(self->loop);
}


struct PulseCapture final : public ALCbackend {
    std::string device_name;

    const void *cap_store{nullptr};
    size_t cap_len{0};
    size_t cap_remain{0};

    ALCuint last_readable{0};

    pa_buffer_attr attr;
    pa_sample_spec spec;

    pa_threaded_mainloop *loop{nullptr};

    pa_stream *stream{nullptr};
    pa_context *context{nullptr};
};

void PulseCapture_deviceCallback(pa_context *context, const pa_source_info *info, int eol, void *pdata);
void PulseCapture_probeDevices(void);

void PulseCapture_contextStateCallback(pa_context *context, void *pdata);
void PulseCapture_streamStateCallback(pa_stream *stream, void *pdata);
void PulseCapture_sourceNameCallback(pa_context *context, const pa_source_info *info, int eol, void *pdata);
void PulseCapture_streamMovedCallback(pa_stream *stream, void *pdata);
pa_stream *PulseCapture_connectStream(const char *device_name,
                                      pa_threaded_mainloop *loop, pa_context *context,
                                      pa_stream_flags_t flags, pa_buffer_attr *attr,
                                      pa_sample_spec *spec, pa_channel_map *chanmap);

void PulseCapture_Construct(PulseCapture *self, ALCdevice *device);
void PulseCapture_Destruct(PulseCapture *self);
ALCenum PulseCapture_open(PulseCapture *self, const ALCchar *name);
DECLARE_FORWARD(PulseCapture, ALCbackend, ALCboolean, reset)
ALCboolean PulseCapture_start(PulseCapture *self);
void PulseCapture_stop(PulseCapture *self);
ALCenum PulseCapture_captureSamples(PulseCapture *self, ALCvoid *buffer, ALCuint samples);
ALCuint PulseCapture_availableSamples(PulseCapture *self);
ClockLatency PulseCapture_getClockLatency(PulseCapture *self);
void PulseCapture_lock(PulseCapture *self);
void PulseCapture_unlock(PulseCapture *self);
DECLARE_DEFAULT_ALLOCATORS(PulseCapture)

DEFINE_ALCBACKEND_VTABLE(PulseCapture);


void PulseCapture_Construct(PulseCapture *self, ALCdevice *device)
{
    new (self) PulseCapture();
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(PulseCapture, ALCbackend, self);
}

void PulseCapture_Destruct(PulseCapture *self)
{
    if(self->loop)
    {
        pulse_close(self->loop, self->context, self->stream);
        self->loop = nullptr;
        self->context = nullptr;
        self->stream = nullptr;
    }
    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
    self->~PulseCapture();
}


void PulseCapture_deviceCallback(pa_context *UNUSED(context), const pa_source_info *info, int eol, void *pdata)
{
    auto loop = reinterpret_cast<pa_threaded_mainloop*>(pdata);

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

void PulseCapture_probeDevices(void)
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

            pa_stream *stream{PulseCapture_connectStream(nullptr,
                loop, context, flags, nullptr, &spec, nullptr
            )};
            if(stream)
            {
                pa_operation *op{pa_context_get_source_info_by_name(context,
                    pa_stream_get_device_name(stream), PulseCapture_deviceCallback, loop
                )};
                wait_for_operation(op, loop);

                pa_stream_disconnect(stream);
                pa_stream_unref(stream);
                stream = nullptr;
            }

            pa_operation *op{pa_context_get_source_info_list(context,
                PulseCapture_deviceCallback, loop
            )};
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


void PulseCapture_contextStateCallback(pa_context *context, void *pdata)
{
    auto self = reinterpret_cast<PulseCapture*>(pdata);
    if(pa_context_get_state(context) == PA_CONTEXT_FAILED)
    {
        ERR("Received context failure!\n");
        aluHandleDisconnect(STATIC_CAST(ALCbackend,self)->mDevice, "Capture state failure");
    }
    pa_threaded_mainloop_signal(self->loop, 0);
}

void PulseCapture_streamStateCallback(pa_stream *stream, void *pdata)
{
    auto self = reinterpret_cast<PulseCapture*>(pdata);
    if(pa_stream_get_state(stream) == PA_STREAM_FAILED)
    {
        ERR("Received stream failure!\n");
        aluHandleDisconnect(STATIC_CAST(ALCbackend,self)->mDevice, "Capture stream failure");
    }
    pa_threaded_mainloop_signal(self->loop, 0);
}


void PulseCapture_sourceNameCallback(pa_context *UNUSED(context), const pa_source_info *info, int eol, void *pdata)
{
    auto self = reinterpret_cast<PulseCapture*>(pdata);

    if(eol)
    {
        pa_threaded_mainloop_signal(self->loop, 0);
        return;
    }

    ALCdevice *device{STATIC_CAST(ALCbackend,self)->mDevice};
    device->DeviceName = info->description;
}


void PulseCapture_streamMovedCallback(pa_stream *stream, void *pdata)
{
    auto self = reinterpret_cast<PulseCapture*>(pdata);

    self->device_name = pa_stream_get_device_name(stream);

    TRACE("Stream moved to %s\n", self->device_name.c_str());
}


pa_stream *PulseCapture_connectStream(const char *device_name,
    pa_threaded_mainloop *loop, pa_context *context,
    pa_stream_flags_t flags, pa_buffer_attr *attr, pa_sample_spec *spec,
    pa_channel_map *chanmap)
{
    pa_stream *stream{pa_stream_new_with_proplist(context,
        "Capture Stream", spec, chanmap, prop_filter
    )};
    if(!stream)
    {
        ERR("pa_stream_new_with_proplist() failed: %s\n", pa_strerror(pa_context_errno(context)));
        return nullptr;
    }

    pa_stream_set_state_callback(stream, stream_state_callback, loop);

    if(pa_stream_connect_record(stream, device_name, attr, flags) < 0)
    {
        ERR("Stream did not connect: %s\n", pa_strerror(pa_context_errno(context)));
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


ALCenum PulseCapture_open(PulseCapture *self, const ALCchar *name)
{
    ALCdevice *device{STATIC_CAST(ALCbackend,self)->mDevice};
    const char *pulse_name{nullptr};

    if(name)
    {
        if(CaptureDevices.empty())
            PulseCapture_probeDevices();

        auto iter = std::find_if(CaptureDevices.cbegin(), CaptureDevices.cend(),
            [name](const DevMap &entry) -> bool
            { return entry.name == name; }
        );
        if(iter == CaptureDevices.cend())
            return ALC_INVALID_VALUE;
        pulse_name = iter->device_name.c_str();
        device->DeviceName = iter->name;
    }

    std::tie(self->loop, self->context) = pulse_open(PulseCapture_contextStateCallback, self);
    if(!self->loop) return ALC_INVALID_VALUE;

    unique_palock palock{self->loop};

    switch(device->FmtType)
    {
        case DevFmtUByte:
            self->spec.format = PA_SAMPLE_U8;
            break;
        case DevFmtShort:
            self->spec.format = PA_SAMPLE_S16NE;
            break;
        case DevFmtInt:
            self->spec.format = PA_SAMPLE_S32NE;
            break;
        case DevFmtFloat:
            self->spec.format = PA_SAMPLE_FLOAT32NE;
            break;
        case DevFmtByte:
        case DevFmtUShort:
        case DevFmtUInt:
            ERR("%s capture samples not supported\n", DevFmtTypeString(device->FmtType));
            return ALC_INVALID_VALUE;
    }

    const char *mapname{nullptr};
    pa_channel_map chanmap;
    switch(device->FmtChans)
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
            ERR("%s capture samples not supported\n", DevFmtChannelsString(device->FmtChans));
            return ALC_INVALID_VALUE;
    }
    if(!pa_channel_map_parse(&chanmap, mapname))
    {
        ERR("Failed to build channel map for %s\n", DevFmtChannelsString(device->FmtChans));
        return ALC_INVALID_VALUE;
    }

    self->spec.rate = device->Frequency;
    self->spec.channels = ChannelsFromDevFmt(device->FmtChans, device->mAmbiOrder);

    if(pa_sample_spec_valid(&self->spec) == 0)
    {
        ERR("Invalid sample format\n");
        return ALC_INVALID_VALUE;
    }

    if(!pa_channel_map_init_auto(&chanmap, self->spec.channels, PA_CHANNEL_MAP_WAVEEX))
    {
        ERR("Couldn't build map for channel count (%d)!\n", self->spec.channels);
        return ALC_INVALID_VALUE;
    }

    ALuint samples{device->UpdateSize * device->NumUpdates};
    samples = maxu(samples, 100 * device->Frequency / 1000);

    self->attr.minreq = -1;
    self->attr.prebuf = -1;
    self->attr.maxlength = samples * pa_frame_size(&self->spec);
    self->attr.tlength = -1;
    self->attr.fragsize = minu(samples, 50*device->Frequency/1000) *
                          pa_frame_size(&self->spec);

    pa_stream_flags_t flags{PA_STREAM_START_CORKED|PA_STREAM_ADJUST_LATENCY};
    if(!GetConfigValueBool(nullptr, "pulse", "allow-moves", 0))
        flags |= PA_STREAM_DONT_MOVE;

    TRACE("Connecting to \"%s\"\n", pulse_name ? pulse_name : "(default)");
    self->stream = PulseCapture_connectStream(pulse_name,
        self->loop, self->context, flags, &self->attr, &self->spec, &chanmap
    );
    if(!self->stream)
        return ALC_INVALID_VALUE;
    pa_stream_set_moved_callback(self->stream, PulseCapture_streamMovedCallback, self);
    pa_stream_set_state_callback(self->stream, PulseCapture_streamStateCallback, self);

    self->device_name = pa_stream_get_device_name(self->stream);
    if(device->DeviceName.empty())
    {
        pa_operation *op{pa_context_get_source_info_by_name(self->context,
            self->device_name.c_str(), PulseCapture_sourceNameCallback, self
        )};
        wait_for_operation(op, self->loop);
    }

    return ALC_NO_ERROR;
}

ALCboolean PulseCapture_start(PulseCapture *self)
{
    palock_guard _{self->loop};
    pa_operation *op{pa_stream_cork(self->stream, 0, stream_success_callback, self->loop)};
    wait_for_operation(op, self->loop);
    return ALC_TRUE;
}

void PulseCapture_stop(PulseCapture *self)
{
    palock_guard _{self->loop};
    pa_operation *op{pa_stream_cork(self->stream, 1, stream_success_callback, self->loop)};
    wait_for_operation(op, self->loop);
}

ALCenum PulseCapture_captureSamples(PulseCapture *self, ALCvoid *buffer, ALCuint samples)
{
    ALCdevice *device{STATIC_CAST(ALCbackend,self)->mDevice};
    ALCuint todo{samples * static_cast<ALCuint>(pa_frame_size(&self->spec))};

    /* Capture is done in fragment-sized chunks, so we loop until we get all
     * that's available */
    self->last_readable -= todo;
    unique_palock palock{self->loop};
    while(todo > 0)
    {
        size_t rem{todo};

        if(self->cap_len == 0)
        {
            pa_stream_state_t state{pa_stream_get_state(self->stream)};
            if(!PA_STREAM_IS_GOOD(state))
            {
                aluHandleDisconnect(device, "Bad capture state: %u", state);
                return ALC_INVALID_DEVICE;
            }
            if(pa_stream_peek(self->stream, &self->cap_store, &self->cap_len) < 0)
            {
                ERR("pa_stream_peek() failed: %s\n",
                    pa_strerror(pa_context_errno(self->context)));
                aluHandleDisconnect(device, "Failed retrieving capture samples: %s",
                                    pa_strerror(pa_context_errno(self->context)));
                return ALC_INVALID_DEVICE;
            }
            self->cap_remain = self->cap_len;
        }
        if(rem > self->cap_remain)
            rem = self->cap_remain;

        memcpy(buffer, self->cap_store, rem);

        buffer = (ALbyte*)buffer + rem;
        todo -= rem;

        self->cap_store = (ALbyte*)self->cap_store + rem;
        self->cap_remain -= rem;
        if(self->cap_remain == 0)
        {
            pa_stream_drop(self->stream);
            self->cap_len = 0;
        }
    }
    palock.unlock();
    if(todo > 0)
        memset(buffer, ((device->FmtType==DevFmtUByte) ? 0x80 : 0), todo);

    return ALC_NO_ERROR;
}

ALCuint PulseCapture_availableSamples(PulseCapture *self)
{
    ALCdevice *device{STATIC_CAST(ALCbackend,self)->mDevice};
    size_t readable{self->cap_remain};

    if(ATOMIC_LOAD(&device->Connected, almemory_order_acquire))
    {
        palock_guard _{self->loop};
        size_t got{pa_stream_readable_size(self->stream)};
        if(static_cast<ssize_t>(got) < 0)
        {
            ERR("pa_stream_readable_size() failed: %s\n", pa_strerror(got));
            aluHandleDisconnect(device, "Failed getting readable size: %s", pa_strerror(got));
        }
        else if(got > self->cap_len)
            readable += got - self->cap_len;
    }

    if(self->last_readable < readable)
        self->last_readable = readable;
    return self->last_readable / pa_frame_size(&self->spec);
}


ClockLatency PulseCapture_getClockLatency(PulseCapture *self)
{
    ClockLatency ret;
    pa_usec_t latency;
    int neg, err;

    { palock_guard _{self->loop};
        ret.ClockTime = GetDeviceClockTime(STATIC_CAST(ALCbackend,self)->mDevice);
        err = pa_stream_get_latency(self->stream, &latency, &neg);
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


void PulseCapture_lock(PulseCapture *self)
{
    pa_threaded_mainloop_lock(self->loop);
}

void PulseCapture_unlock(PulseCapture *self)
{
    pa_threaded_mainloop_unlock(self->loop);
}

} // namespace


bool PulseBackendFactory::init()
{
    bool ret{false};

    if(pulse_load())
    {
        pulse_ctx_flags = PA_CONTEXT_NOFLAGS;
        if(!GetConfigValueBool(nullptr, "pulse", "spawn-server", 1))
            pulse_ctx_flags |= PA_CONTEXT_NOAUTOSPAWN;

        pa_threaded_mainloop *loop{pa_threaded_mainloop_new()};
        if(loop && pa_threaded_mainloop_start(loop) >= 0)
        {
            unique_palock palock{loop};
            pa_context *context{connect_context(loop, AL_TRUE)};
            if(context)
            {
                ret = true;

                /* Some libraries (Phonon, Qt) set some pulseaudio properties
                 * through environment variables, which causes all streams in
                 * the process to inherit them. This attempts to filter those
                 * properties out by setting them to 0-length data. */
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
    }

    return ret;
}

void PulseBackendFactory::deinit()
{
    PlaybackDevices.clear();
    CaptureDevices.clear();

    if(prop_filter)
        pa_proplist_free(prop_filter);
    prop_filter = nullptr;

    /* PulseAudio doesn't like being CloseLib'd sometimes */
}

bool PulseBackendFactory::querySupport(ALCbackend_Type type)
{
    if(type == ALCbackend_Playback || type == ALCbackend_Capture)
        return true;
    return false;
}

void PulseBackendFactory::probe(enum DevProbe type, std::string *outnames)
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
        case ALL_DEVICE_PROBE:
            PulsePlayback_probeDevices();
            std::for_each(PlaybackDevices.cbegin(), PlaybackDevices.cend(), add_device);
            break;

        case CAPTURE_DEVICE_PROBE:
            PulseCapture_probeDevices();
            std::for_each(CaptureDevices.cbegin(), CaptureDevices.cend(), add_device);
            break;
    }
}

ALCbackend *PulseBackendFactory::createBackend(ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        PulsePlayback *backend;
        NEW_OBJ(backend, PulsePlayback)(device);
        if(!backend) return nullptr;
        return STATIC_CAST(ALCbackend, backend);
    }
    if(type == ALCbackend_Capture)
    {
        PulseCapture *backend;
        NEW_OBJ(backend, PulseCapture)(device);
        if(!backend) return nullptr;
        return STATIC_CAST(ALCbackend, backend);
    }

    return nullptr;
}


#else /* PA_API_VERSION == 12 */

#warning "Unsupported API version, backend will be unavailable!"

bool PulseBackendFactory::init() { return false; }

void PulseBackendFactory::deinit() { }

bool PulseBackendFactory::querySupport(ALCbackend_Type) { return false; }

void PulseBackendFactory::probe(enum DevProbe, std::string*) { }

ALCbackend *PulseBackendFactory::createBackend(ALCdevice*, ALCbackend_Type)
{ return nullptr; }

#endif /* PA_API_VERSION == 12 */

BackendFactory &PulseBackendFactory::getFactory()
{
    static PulseBackendFactory factory{};
    return factory;
}
