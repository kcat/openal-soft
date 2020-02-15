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

#include <poll.h>
#include <cstring>

#include <array>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <algorithm>
#include <functional>
#include <condition_variable>

#include "alcmain.h"
#include "alu.h"
#include "alconfig.h"
#include "alexcpt.h"
#include "compat.h"
#include "dynload.h"
#include "strutils.h"

#include <pulse/pulseaudio.h>


namespace {

#ifdef HAVE_DYNLOAD
#define PULSE_FUNCS(MAGIC)                                                    \
    MAGIC(pa_mainloop_new);                                                   \
    MAGIC(pa_mainloop_free);                                                  \
    MAGIC(pa_mainloop_set_poll_func);                                         \
    MAGIC(pa_mainloop_run);                                                   \
    MAGIC(pa_mainloop_quit);                                                  \
    MAGIC(pa_mainloop_get_api);                                               \
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
    MAGIC(pa_channel_map_init_auto);                                          \
    MAGIC(pa_channel_map_parse);                                              \
    MAGIC(pa_channel_map_snprint);                                            \
    MAGIC(pa_channel_map_equal);                                              \
    MAGIC(pa_channel_map_superset);                                           \
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
#define pa_mainloop_new ppa_mainloop_new
#define pa_mainloop_free ppa_mainloop_free
#define pa_mainloop_set_poll_func ppa_mainloop_set_poll_func
#define pa_mainloop_run ppa_mainloop_run
#define pa_mainloop_quit ppa_mainloop_quit
#define pa_mainloop_get_api ppa_mainloop_get_api
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
#define pa_stream_begin_write ppa_stream_begin_write*/
#define pa_channel_map_init_auto ppa_channel_map_init_auto
#define pa_channel_map_parse ppa_channel_map_parse
#define pa_channel_map_snprint ppa_channel_map_snprint
#define pa_channel_map_equal ppa_channel_map_equal
#define pa_channel_map_superset ppa_channel_map_superset
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
};

size_t ChannelFromPulse(pa_channel_position_t chan)
{
    switch(chan)
    {
    case PA_CHANNEL_POSITION_INVALID: break;
    case PA_CHANNEL_POSITION_MONO: return FrontCenter;
    case PA_CHANNEL_POSITION_FRONT_LEFT: return FrontLeft;
    case PA_CHANNEL_POSITION_FRONT_RIGHT: return FrontRight;
    case PA_CHANNEL_POSITION_FRONT_CENTER: return FrontCenter;
    case PA_CHANNEL_POSITION_REAR_CENTER: return BackCenter;
    case PA_CHANNEL_POSITION_REAR_LEFT: return BackLeft;
    case PA_CHANNEL_POSITION_REAR_RIGHT: return BackRight;
    case PA_CHANNEL_POSITION_LFE: return LFE;
    case PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER: break;
    case PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER: break;
    case PA_CHANNEL_POSITION_SIDE_LEFT: return SideLeft;
    case PA_CHANNEL_POSITION_SIDE_RIGHT: return SideRight;
    case PA_CHANNEL_POSITION_AUX0: return Aux0;
    case PA_CHANNEL_POSITION_AUX1: return Aux1;
    case PA_CHANNEL_POSITION_AUX2: return Aux2;
    case PA_CHANNEL_POSITION_AUX3: return Aux3;
    case PA_CHANNEL_POSITION_AUX4: return Aux4;
    case PA_CHANNEL_POSITION_AUX5: return Aux5;
    case PA_CHANNEL_POSITION_AUX6: return Aux6;
    case PA_CHANNEL_POSITION_AUX7: return Aux7;
    case PA_CHANNEL_POSITION_AUX8: return Aux8;
    case PA_CHANNEL_POSITION_AUX9: return Aux9;
    case PA_CHANNEL_POSITION_AUX10: return Aux10;
    case PA_CHANNEL_POSITION_AUX11: return Aux11;
    case PA_CHANNEL_POSITION_AUX12: return Aux12;
    case PA_CHANNEL_POSITION_AUX13: return Aux13;
    case PA_CHANNEL_POSITION_AUX14: return Aux14;
    case PA_CHANNEL_POSITION_AUX15: return Aux15;
    case PA_CHANNEL_POSITION_AUX16: break;
    case PA_CHANNEL_POSITION_AUX17: break;
    case PA_CHANNEL_POSITION_AUX18: break;
    case PA_CHANNEL_POSITION_AUX19: break;
    case PA_CHANNEL_POSITION_AUX20: break;
    case PA_CHANNEL_POSITION_AUX21: break;
    case PA_CHANNEL_POSITION_AUX22: break;
    case PA_CHANNEL_POSITION_AUX23: break;
    case PA_CHANNEL_POSITION_AUX24: break;
    case PA_CHANNEL_POSITION_AUX25: break;
    case PA_CHANNEL_POSITION_AUX26: break;
    case PA_CHANNEL_POSITION_AUX27: break;
    case PA_CHANNEL_POSITION_AUX28: break;
    case PA_CHANNEL_POSITION_AUX29: break;
    case PA_CHANNEL_POSITION_AUX30: break;
    case PA_CHANNEL_POSITION_AUX31: break;
    case PA_CHANNEL_POSITION_TOP_CENTER: break;
    case PA_CHANNEL_POSITION_TOP_FRONT_LEFT: return UpperFrontLeft;
    case PA_CHANNEL_POSITION_TOP_FRONT_RIGHT: return UpperFrontRight;
    case PA_CHANNEL_POSITION_TOP_FRONT_CENTER: break;
    case PA_CHANNEL_POSITION_TOP_REAR_LEFT: return UpperBackLeft;
    case PA_CHANNEL_POSITION_TOP_REAR_RIGHT: return UpperBackRight;
    case PA_CHANNEL_POSITION_TOP_REAR_CENTER: break;
    case PA_CHANNEL_POSITION_MAX: break;
    }
    throw al::backend_exception{ALC_INVALID_VALUE, "Unexpected channel enum %d", chan};
}

void SetChannelOrderFromMap(ALCdevice *device, const pa_channel_map &chanmap)
{
    device->RealOut.ChannelIndex.fill(INVALID_CHANNEL_INDEX);
    for(ALuint i{0};i < chanmap.channels;++i)
        device->RealOut.ChannelIndex[ChannelFromPulse(chanmap.map[i])] = i;
}


/* *grumble* Don't use enums for bitflags. */
constexpr inline pa_stream_flags_t operator|(pa_stream_flags_t lhs, pa_stream_flags_t rhs)
{ return pa_stream_flags_t(int(lhs) | int(rhs)); }
inline pa_stream_flags_t& operator|=(pa_stream_flags_t &lhs, pa_stream_flags_t rhs)
{
    lhs = lhs | rhs;
    return lhs;
}
inline pa_stream_flags_t& operator&=(pa_stream_flags_t &lhs, int rhs)
{
    lhs = pa_stream_flags_t(int(lhs) & rhs);
    return lhs;
}

inline pa_context_flags_t& operator|=(pa_context_flags_t &lhs, pa_context_flags_t rhs)
{
    lhs = pa_context_flags_t(int(lhs) | int(rhs));
    return lhs;
}


/* Global flags and properties */
pa_context_flags_t pulse_ctx_flags;

int pulse_poll_func(struct pollfd *ufds, unsigned long nfds, int timeout, void *userdata) noexcept
{
    auto plock = static_cast<std::unique_lock<std::mutex>*>(userdata);
    plock->unlock();
    int r{poll(ufds, nfds, timeout)};
    plock->lock();
    return r;
}

class PulseMainloop {
    std::thread mThread;
    std::mutex mMutex;
    std::condition_variable mCondVar;
    pa_mainloop *mMainloop{nullptr};

public:
    ~PulseMainloop()
    {
        if(mThread.joinable())
        {
            pa_mainloop_quit(mMainloop, 0);
            mThread.join();
        }
    }

    int mainloop_thread()
    {
        SetRTPriority();

        std::unique_lock<std::mutex> plock{mMutex};
        mMainloop = pa_mainloop_new();

        pa_mainloop_set_poll_func(mMainloop, pulse_poll_func, &plock);
        mCondVar.notify_all();

        int ret{};
        pa_mainloop_run(mMainloop, &ret);

        pa_mainloop_free(mMainloop);
        mMainloop = nullptr;

        return ret;
    }

    void doLock() { mMutex.lock(); }
    void doUnlock() { mMutex.unlock(); }
    std::unique_lock<std::mutex> getLock() { return std::unique_lock<std::mutex>{mMutex}; }
    std::condition_variable &getCondVar() noexcept { return mCondVar; }

    void contextStateCallback(pa_context *context) noexcept
    {
        pa_context_state_t state{pa_context_get_state(context)};
        if(state == PA_CONTEXT_READY || !PA_CONTEXT_IS_GOOD(state))
            mCondVar.notify_all();
    }
    static void contextStateCallbackC(pa_context *context, void *pdata) noexcept
    { static_cast<PulseMainloop*>(pdata)->contextStateCallback(context); }

    void streamStateCallback(pa_stream *stream) noexcept
    {
        pa_stream_state_t state{pa_stream_get_state(stream)};
        if(state == PA_STREAM_READY || !PA_STREAM_IS_GOOD(state))
            mCondVar.notify_all();
    }
    static void streamStateCallbackC(pa_stream *stream, void *pdata) noexcept
    { static_cast<PulseMainloop*>(pdata)->streamStateCallback(stream); }

    void streamSuccessCallback(pa_stream*, int) noexcept
    { mCondVar.notify_all(); }
    static void streamSuccessCallbackC(pa_stream *stream, int success, void *pdata) noexcept
    { static_cast<PulseMainloop*>(pdata)->streamSuccessCallback(stream, success); }

    void waitForOperation(pa_operation *op, std::unique_lock<std::mutex> &plock)
    {
        if(op)
        {
            while(pa_operation_get_state(op) == PA_OPERATION_RUNNING)
                mCondVar.wait(plock);
            pa_operation_unref(op);
        }
    }

    pa_context *connectContext(std::unique_lock<std::mutex> &plock);

    pa_stream *connectStream(const char *device_name, std::unique_lock<std::mutex> &plock,
        pa_context *context, pa_stream_flags_t flags, pa_buffer_attr *attr, pa_sample_spec *spec,
        pa_channel_map *chanmap, BackendType type);

    void close(pa_context *context, pa_stream *stream);
};


pa_context *PulseMainloop::connectContext(std::unique_lock<std::mutex> &plock)
{
    const char *name{"OpenAL Soft"};

    const PathNamePair &binname = GetProcBinary();
    if(!binname.fname.empty())
        name = binname.fname.c_str();

    if(!mMainloop)
    {
        mThread = std::thread{std::mem_fn(&PulseMainloop::mainloop_thread), this};
        while(!mMainloop) mCondVar.wait(plock);
    }

    pa_context *context{pa_context_new(pa_mainloop_get_api(mMainloop), name)};
    if(!context) throw al::backend_exception{ALC_OUT_OF_MEMORY, "pa_context_new() failed"};

    pa_context_set_state_callback(context, &contextStateCallbackC, this);

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

            mCondVar.wait(plock);
        }
    }
    pa_context_set_state_callback(context, nullptr, nullptr);

    if(err < 0)
    {
        pa_context_unref(context);
        throw al::backend_exception{ALC_INVALID_VALUE, "Context did not connect (%s)",
            pa_strerror(err)};
    }

    return context;
}

pa_stream *PulseMainloop::connectStream(const char *device_name,
    std::unique_lock<std::mutex> &plock, pa_context *context, pa_stream_flags_t flags,
    pa_buffer_attr *attr, pa_sample_spec *spec, pa_channel_map *chanmap, BackendType type)
{
    const char *stream_id{(type==BackendType::Playback) ? "Playback Stream" : "Capture Stream"};
    pa_stream *stream{pa_stream_new(context, stream_id, spec, chanmap)};
    if(!stream)
        throw al::backend_exception{ALC_OUT_OF_MEMORY, "pa_stream_new() failed (%s)",
            pa_strerror(pa_context_errno(context))};

    pa_stream_set_state_callback(stream, &streamStateCallbackC, this);

    int err{(type==BackendType::Playback) ?
        pa_stream_connect_playback(stream, device_name, attr, flags, nullptr, nullptr) :
        pa_stream_connect_record(stream, device_name, attr, flags)};
    if(err < 0)
    {
        pa_stream_unref(stream);
        throw al::backend_exception{ALC_INVALID_VALUE, "%s did not connect (%s)", stream_id,
            pa_strerror(err)};
    }

    pa_stream_state_t state;
    while((state=pa_stream_get_state(stream)) != PA_STREAM_READY)
    {
        if(!PA_STREAM_IS_GOOD(state))
        {
            err = pa_context_errno(context);
            pa_stream_unref(stream);
            throw al::backend_exception{ALC_INVALID_VALUE, "%s did not get ready (%s)", stream_id,
                pa_strerror(err)};
        }

        mCondVar.wait(plock);
    }
    pa_stream_set_state_callback(stream, nullptr, nullptr);

    return stream;
}

void PulseMainloop::close(pa_context *context, pa_stream *stream)
{
    std::lock_guard<std::mutex> _{mMutex};
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


/* Used for initial connection test and enumeration. */
PulseMainloop gGlobalMainloop;


struct DevMap {
    std::string name;
    std::string device_name;
};

bool checkName(const al::vector<DevMap> &list, const std::string &name)
{
    auto match_name = [&name](const DevMap &entry) -> bool { return entry.name == name; };
    return std::find_if(list.cbegin(), list.cend(), match_name) != list.cend();
}

al::vector<DevMap> PlaybackDevices;
al::vector<DevMap> CaptureDevices;


void device_sink_callback(pa_context*, const pa_sink_info *info, int eol, void *pdata) noexcept
{
    if(eol)
    {
        static_cast<PulseMainloop*>(pdata)->getCondVar().notify_all();
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
    PlaybackDevices.emplace_back(DevMap{std::move(newname), info->name});
    DevMap &newentry = PlaybackDevices.back();

    TRACE("Got device \"%s\", \"%s\"\n", newentry.name.c_str(), newentry.device_name.c_str());
}

void probePlaybackDevices(PulseMainloop &mainloop)
{
    pa_context *context{};
    pa_stream *stream{};

    PlaybackDevices.clear();
    try {
        auto plock = mainloop.getLock();

        context = mainloop.connectContext(plock);

        constexpr pa_stream_flags_t flags{PA_STREAM_FIX_FORMAT | PA_STREAM_FIX_RATE |
            PA_STREAM_FIX_CHANNELS | PA_STREAM_DONT_MOVE | PA_STREAM_START_CORKED};

        pa_sample_spec spec{};
        spec.format = PA_SAMPLE_S16NE;
        spec.rate = 44100;
        spec.channels = 2;

        stream = mainloop.connectStream(nullptr, plock, context, flags, nullptr, &spec, nullptr,
            BackendType::Playback);
        pa_operation *op{pa_context_get_sink_info_by_name(context,
            pa_stream_get_device_name(stream), device_sink_callback, &mainloop)};
        mainloop.waitForOperation(op, plock);

        pa_stream_disconnect(stream);
        pa_stream_unref(stream);
        stream = nullptr;

        op = pa_context_get_sink_info_list(context, device_sink_callback, &mainloop);
        mainloop.waitForOperation(op, plock);

        pa_context_disconnect(context);
        pa_context_unref(context);
        context = nullptr;
    }
    catch(std::exception &e) {
        ERR("Error enumerating devices: %s\n", e.what());
        if(context) mainloop.close(context, stream);
    }
}


void device_source_callback(pa_context*, const pa_source_info *info, int eol, void *pdata) noexcept
{
    if(eol)
    {
        static_cast<PulseMainloop*>(pdata)->getCondVar().notify_all();
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
    CaptureDevices.emplace_back(DevMap{std::move(newname), info->name});
    DevMap &newentry = CaptureDevices.back();

    TRACE("Got device \"%s\", \"%s\"\n", newentry.name.c_str(), newentry.device_name.c_str());
}

void probeCaptureDevices(PulseMainloop &mainloop)
{
    pa_context *context{};
    pa_stream *stream{};

    CaptureDevices.clear();
    try {
        auto plock = mainloop.getLock();

        context = mainloop.connectContext(plock);

        constexpr pa_stream_flags_t flags{PA_STREAM_FIX_FORMAT | PA_STREAM_FIX_RATE |
            PA_STREAM_FIX_CHANNELS | PA_STREAM_DONT_MOVE | PA_STREAM_START_CORKED};

        pa_sample_spec spec{};
        spec.format = PA_SAMPLE_S16NE;
        spec.rate = 44100;
        spec.channels = 1;

        stream = mainloop.connectStream(nullptr, plock, context, flags, nullptr, &spec, nullptr,
            BackendType::Capture);
        pa_operation *op{pa_context_get_source_info_by_name(context,
            pa_stream_get_device_name(stream), device_source_callback, &mainloop)};
        mainloop.waitForOperation(op, plock);

        pa_stream_disconnect(stream);
        pa_stream_unref(stream);
        stream = nullptr;

        op = pa_context_get_source_info_list(context, device_source_callback, &mainloop);
        mainloop.waitForOperation(op, plock);

        pa_context_disconnect(context);
        pa_context_unref(context);
        context = nullptr;
    }
    catch(std::exception &e) {
        ERR("Error enumerating devices: %s\n", e.what());
        if(context) mainloop.close(context, stream);
    }
}


struct PulsePlayback final : public BackendBase {
    PulsePlayback(ALCdevice *device) noexcept : BackendBase{device} { }
    ~PulsePlayback() override;

    void bufferAttrCallback(pa_stream *stream) noexcept;
    static void bufferAttrCallbackC(pa_stream *stream, void *pdata) noexcept
    { static_cast<PulsePlayback*>(pdata)->bufferAttrCallback(stream); }

    void streamStateCallback(pa_stream *stream) noexcept;
    static void streamStateCallbackC(pa_stream *stream, void *pdata) noexcept
    { static_cast<PulsePlayback*>(pdata)->streamStateCallback(stream); }

    void streamWriteCallback(pa_stream *stream, size_t nbytes) noexcept;
    static void streamWriteCallbackC(pa_stream *stream, size_t nbytes, void *pdata) noexcept
    { static_cast<PulsePlayback*>(pdata)->streamWriteCallback(stream, nbytes); }

    void sinkInfoCallback(pa_context *context, const pa_sink_info *info, int eol) noexcept;
    static void sinkInfoCallbackC(pa_context *context, const pa_sink_info *info, int eol, void *pdata) noexcept
    { static_cast<PulsePlayback*>(pdata)->sinkInfoCallback(context, info, eol); }

    void sinkNameCallback(pa_context *context, const pa_sink_info *info, int eol) noexcept;
    static void sinkNameCallbackC(pa_context *context, const pa_sink_info *info, int eol, void *pdata) noexcept
    { static_cast<PulsePlayback*>(pdata)->sinkNameCallback(context, info, eol); }

    void streamMovedCallback(pa_stream *stream) noexcept;
    static void streamMovedCallbackC(pa_stream *stream, void *pdata) noexcept
    { static_cast<PulsePlayback*>(pdata)->streamMovedCallback(stream); }

    void open(const ALCchar *name) override;
    bool reset() override;
    bool start() override;
    void stop() override;
    ClockLatency getClockLatency() override;
    void lock() override { mMainloop.doLock(); }
    void unlock() override { mMainloop.doUnlock(); }

    PulseMainloop mMainloop;

    std::string mDeviceName;

    pa_buffer_attr mAttr;
    pa_sample_spec mSpec;

    pa_stream *mStream{nullptr};
    pa_context *mContext{nullptr};

    ALuint mFrameSize{0u};

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
        aluHandleDisconnect(mDevice, "Playback stream failure");
    }
    mMainloop.getCondVar().notify_all();
}

void PulsePlayback::streamWriteCallback(pa_stream *stream, size_t nbytes) noexcept
{
    void *buf{pa_xmalloc(nbytes)};
    aluMixData(mDevice, buf, static_cast<ALuint>(nbytes/mFrameSize), mDevice->channelsFromFmt());

    int ret{pa_stream_write(stream, buf, nbytes, pa_xfree, 0, PA_SEEK_RELATIVE)};
    if UNLIKELY(ret != PA_OK)
        ERR("Failed to write to stream: %d, %s\n", ret, pa_strerror(ret));
}

void PulsePlayback::sinkInfoCallback(pa_context*, const pa_sink_info *info, int eol) noexcept
{
    struct ChannelMap {
        DevFmtChannels fmt;
        pa_channel_map map;
    };
    static constexpr std::array<ChannelMap,7> chanmaps{{
        { DevFmtX71, X71ChanMap },
        { DevFmtX61, X61ChanMap },
        { DevFmtX51, X51ChanMap },
        { DevFmtX51Rear, X51RearChanMap },
        { DevFmtQuad, QuadChanMap },
        { DevFmtStereo, StereoChanMap },
        { DevFmtMono, MonoChanMap }
    }};

    if(eol)
    {
        mMainloop.getCondVar().notify_all();
        return;
    }

    auto chaniter = std::find_if(chanmaps.cbegin(), chanmaps.cend(),
        [info](const ChannelMap &chanmap) -> bool
        { return pa_channel_map_superset(&info->channel_map, &chanmap.map); }
    );
    if(chaniter != chanmaps.cend())
    {
        if(!mDevice->Flags.get<ChannelsRequest>())
            mDevice->FmtChans = chaniter->fmt;
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

void PulsePlayback::sinkNameCallback(pa_context*, const pa_sink_info *info, int eol) noexcept
{
    if(eol)
    {
        mMainloop.getCondVar().notify_all();
        return;
    }
    mDevice->DeviceName = info->description;
}

void PulsePlayback::streamMovedCallback(pa_stream *stream) noexcept
{
    mDeviceName = pa_stream_get_device_name(stream);
    TRACE("Stream moved to %s\n", mDeviceName.c_str());
}


void PulsePlayback::open(const ALCchar *name)
{
    const char *pulse_name{nullptr};
    const char *dev_name{nullptr};

    if(name)
    {
        if(PlaybackDevices.empty())
            probePlaybackDevices(mMainloop);

        auto iter = std::find_if(PlaybackDevices.cbegin(), PlaybackDevices.cend(),
            [name](const DevMap &entry) -> bool
            { return entry.name == name; }
        );
        if(iter == PlaybackDevices.cend())
            throw al::backend_exception{ALC_INVALID_VALUE, "Device name \"%s\" not found", name};
        pulse_name = iter->device_name.c_str();
        dev_name = iter->name.c_str();
    }

    auto plock = mMainloop.getLock();

    mContext = mMainloop.connectContext(plock);

    pa_stream_flags_t flags{PA_STREAM_START_CORKED | PA_STREAM_FIX_FORMAT | PA_STREAM_FIX_RATE |
        PA_STREAM_FIX_CHANNELS};
    if(!GetConfigValueBool(nullptr, "pulse", "allow-moves", 1))
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
    mStream = mMainloop.connectStream(pulse_name, plock, mContext, flags, nullptr, &spec, nullptr,
        BackendType::Playback);

    pa_stream_set_moved_callback(mStream, &PulsePlayback::streamMovedCallbackC, this);
    mFrameSize = static_cast<ALuint>(pa_frame_size(pa_stream_get_sample_spec(mStream)));

    mDeviceName = pa_stream_get_device_name(mStream);
    if(!dev_name)
    {
        pa_operation *op{pa_context_get_sink_info_by_name(mContext, mDeviceName.c_str(),
            &PulsePlayback::sinkNameCallbackC, this)};
        mMainloop.waitForOperation(op, plock);
    }
    else
        mDevice->DeviceName = dev_name;
}

bool PulsePlayback::reset()
{
    auto plock = mMainloop.getLock();

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
    mMainloop.waitForOperation(op, plock);

    pa_stream_flags_t flags{PA_STREAM_START_CORKED | PA_STREAM_INTERPOLATE_TIMING |
        PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_EARLY_REQUESTS};
    if(!GetConfigValueBool(nullptr, "pulse", "allow-moves", 1))
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
       !mDevice->Flags.get<FrequencyRequest>())
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
        chanmap = X51ChanMap;
        break;
    case DevFmtX51Rear:
        chanmap = X51RearChanMap;
        break;
    case DevFmtX61:
        chanmap = X61ChanMap;
        break;
    case DevFmtX71:
        chanmap = X71ChanMap;
        break;
    }
    SetChannelOrderFromMap(mDevice, chanmap);

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
        throw al::backend_exception{ALC_INVALID_VALUE, "Invalid sample spec"};

    const ALuint frame_size{static_cast<ALuint>(pa_frame_size(&mSpec))};
    mAttr.maxlength = ~0u;
    mAttr.tlength = mDevice->BufferSize * frame_size;
    mAttr.prebuf = 0u;
    mAttr.minreq = mDevice->UpdateSize * frame_size;
    mAttr.fragsize = ~0u;

    mStream = mMainloop.connectStream(mDeviceName.c_str(), plock, mContext, flags, &mAttr, &mSpec,
        &chanmap, BackendType::Playback);

    pa_stream_set_state_callback(mStream, &PulsePlayback::streamStateCallbackC, this);
    pa_stream_set_moved_callback(mStream, &PulsePlayback::streamMovedCallbackC, this);

    mSpec = *(pa_stream_get_sample_spec(mStream));
    mFrameSize = static_cast<ALuint>(pa_frame_size(&mSpec));

    if(mDevice->Frequency != mSpec.rate)
    {
        /* Server updated our playback rate, so modify the buffer attribs
         * accordingly.
         */
        const auto scale = static_cast<double>(mSpec.rate) / mDevice->Frequency;
        const ALuint perlen{static_cast<ALuint>(clampd(scale*mDevice->UpdateSize + 0.5, 64.0,
            8192.0))};
        const ALuint buflen{static_cast<ALuint>(clampd(scale*mDevice->BufferSize + 0.5, perlen*2,
            std::numeric_limits<int>::max()/mFrameSize))};

        mAttr.maxlength = ~0u;
        mAttr.tlength = buflen * mFrameSize;
        mAttr.prebuf = 0u;
        mAttr.minreq = perlen * mFrameSize;

        op = pa_stream_set_buffer_attr(mStream, &mAttr, &PulseMainloop::streamSuccessCallbackC,
            &mMainloop);
        mMainloop.waitForOperation(op, plock);

        mDevice->Frequency = mSpec.rate;
    }

    pa_stream_set_buffer_attr_callback(mStream, &PulsePlayback::bufferAttrCallbackC, this);
    bufferAttrCallback(mStream);

    mDevice->BufferSize = mAttr.tlength / mFrameSize;
    mDevice->UpdateSize = mAttr.minreq / mFrameSize;

    return true;
}

bool PulsePlayback::start()
{
    auto plock = mMainloop.getLock();

    pa_stream_set_write_callback(mStream, &PulsePlayback::streamWriteCallbackC, this);
    pa_operation *op{pa_stream_cork(mStream, 0, &PulseMainloop::streamSuccessCallbackC,
        &mMainloop)};
    mMainloop.waitForOperation(op, plock);

    return true;
}

void PulsePlayback::stop()
{
    auto plock = mMainloop.getLock();

    pa_operation *op{pa_stream_cork(mStream, 1, &PulseMainloop::streamSuccessCallbackC,
        &mMainloop)};
    mMainloop.waitForOperation(op, plock);
    pa_stream_set_write_callback(mStream, nullptr, nullptr);
}


ClockLatency PulsePlayback::getClockLatency()
{
    ClockLatency ret;
    pa_usec_t latency;
    int neg, err;

    {
        auto _ = mMainloop.getLock();
        ret.ClockTime = GetDeviceClockTime(mDevice);
        err = pa_stream_get_latency(mStream, &latency, &neg);
    }

    if UNLIKELY(err != 0)
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
    else if UNLIKELY(neg)
        latency = 0;
    ret.Latency = std::chrono::microseconds{latency};

    return ret;
}


struct PulseCapture final : public BackendBase {
    PulseCapture(ALCdevice *device) noexcept : BackendBase{device} { }
    ~PulseCapture() override;

    void streamStateCallback(pa_stream *stream) noexcept;
    static void streamStateCallbackC(pa_stream *stream, void *pdata) noexcept
    { static_cast<PulseCapture*>(pdata)->streamStateCallback(stream); }

    void sourceNameCallback(pa_context *context, const pa_source_info *info, int eol) noexcept;
    static void sourceNameCallbackC(pa_context *context, const pa_source_info *info, int eol, void *pdata) noexcept
    { static_cast<PulseCapture*>(pdata)->sourceNameCallback(context, info, eol); }

    void streamMovedCallback(pa_stream *stream) noexcept;
    static void streamMovedCallbackC(pa_stream *stream, void *pdata) noexcept
    { static_cast<PulseCapture*>(pdata)->streamMovedCallback(stream); }

    void open(const ALCchar *name) override;
    bool start() override;
    void stop() override;
    ALCenum captureSamples(al::byte *buffer, ALCuint samples) override;
    ALCuint availableSamples() override;
    ClockLatency getClockLatency() override;
    void lock() override { mMainloop.doLock(); }
    void unlock() override { mMainloop.doUnlock(); }

    PulseMainloop mMainloop;

    std::string mDeviceName;

    ALCuint mLastReadable{0u};
    al::byte mSilentVal{};

    al::span<const al::byte> mCapBuffer;
    ssize_t mCapLen{0};

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
        aluHandleDisconnect(mDevice, "Capture stream failure");
    }
    mMainloop.getCondVar().notify_all();
}

void PulseCapture::sourceNameCallback(pa_context*, const pa_source_info *info, int eol) noexcept
{
    if(eol)
    {
        mMainloop.getCondVar().notify_all();
        return;
    }
    mDevice->DeviceName = info->description;
}

void PulseCapture::streamMovedCallback(pa_stream *stream) noexcept
{
    mDeviceName = pa_stream_get_device_name(stream);
    TRACE("Stream moved to %s\n", mDeviceName.c_str());
}


void PulseCapture::open(const ALCchar *name)
{
    const char *pulse_name{nullptr};
    if(name)
    {
        if(CaptureDevices.empty())
            probeCaptureDevices(mMainloop);

        auto iter = std::find_if(CaptureDevices.cbegin(), CaptureDevices.cend(),
            [name](const DevMap &entry) -> bool
            { return entry.name == name; }
        );
        if(iter == CaptureDevices.cend())
            throw al::backend_exception{ALC_INVALID_VALUE, "Device name \"%s\" not found", name};
        pulse_name = iter->device_name.c_str();
        mDevice->DeviceName = iter->name;
    }

    auto plock = mMainloop.getLock();

    mContext = mMainloop.connectContext(plock);

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
    case DevFmtX51Rear:
        chanmap = X51RearChanMap;
        break;
    case DevFmtX61:
        chanmap = X61ChanMap;
        break;
    case DevFmtX71:
        chanmap = X71ChanMap;
        break;
    case DevFmtAmbi3D:
        throw al::backend_exception{ALC_INVALID_VALUE, "%s capture not supported",
            DevFmtChannelsString(mDevice->FmtChans)};
    }
    SetChannelOrderFromMap(mDevice, chanmap);

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
        throw al::backend_exception{ALC_INVALID_VALUE, "%s capture samples not supported",
            DevFmtTypeString(mDevice->FmtType)};
    }
    mSpec.rate = mDevice->Frequency;
    mSpec.channels = static_cast<uint8_t>(mDevice->channelsFromFmt());
    if(pa_sample_spec_valid(&mSpec) == 0)
        throw al::backend_exception{ALC_INVALID_VALUE, "Invalid sample format"};

    const ALuint frame_size{static_cast<ALuint>(pa_frame_size(&mSpec))};
    const ALuint samples{maxu(mDevice->BufferSize, 100 * mDevice->Frequency / 1000)};
    mAttr.minreq = ~0u;
    mAttr.prebuf = ~0u;
    mAttr.maxlength = samples * frame_size;
    mAttr.tlength = ~0u;
    mAttr.fragsize = minu(samples, 50*mDevice->Frequency/1000) * frame_size;

    pa_stream_flags_t flags{PA_STREAM_START_CORKED | PA_STREAM_ADJUST_LATENCY};
    if(!GetConfigValueBool(nullptr, "pulse", "allow-moves", 1))
        flags |= PA_STREAM_DONT_MOVE;

    TRACE("Connecting to \"%s\"\n", pulse_name ? pulse_name : "(default)");
    mStream = mMainloop.connectStream(pulse_name, plock, mContext, flags, &mAttr, &mSpec, &chanmap,
        BackendType::Capture);

    pa_stream_set_moved_callback(mStream, &PulseCapture::streamMovedCallbackC, this);
    pa_stream_set_state_callback(mStream, &PulseCapture::streamStateCallbackC, this);

    mDeviceName = pa_stream_get_device_name(mStream);
    if(mDevice->DeviceName.empty())
    {
        pa_operation *op{pa_context_get_source_info_by_name(mContext, mDeviceName.c_str(),
            &PulseCapture::sourceNameCallbackC, this)};
        mMainloop.waitForOperation(op, plock);
    }
}

bool PulseCapture::start()
{
    auto plock = mMainloop.getLock();
    pa_operation *op{pa_stream_cork(mStream, 0, &PulseMainloop::streamSuccessCallbackC,
        &mMainloop)};
    mMainloop.waitForOperation(op, plock);
    return true;
}

void PulseCapture::stop()
{
    auto plock = mMainloop.getLock();
    pa_operation *op{pa_stream_cork(mStream, 1, &PulseMainloop::streamSuccessCallbackC,
        &mMainloop)};
    mMainloop.waitForOperation(op, plock);
}

ALCenum PulseCapture::captureSamples(al::byte *buffer, ALCuint samples)
{
    al::span<al::byte> dstbuf{buffer, samples * pa_frame_size(&mSpec)};

    /* Capture is done in fragment-sized chunks, so we loop until we get all
     * that's available */
    mLastReadable -= static_cast<ALCuint>(dstbuf.size());
    while(!dstbuf.empty())
    {
        if(!mCapBuffer.empty())
        {
            const size_t rem{minz(dstbuf.size(), mCapBuffer.size())};
            if UNLIKELY(mCapLen < 0)
                std::fill_n(dstbuf.begin(), rem, mSilentVal);
            else
                std::copy_n(mCapBuffer.begin(), rem, dstbuf.begin());
            dstbuf = dstbuf.subspan(rem);
            mCapBuffer = mCapBuffer.subspan(rem);

            continue;
        }

        if UNLIKELY(!mDevice->Connected.load(std::memory_order_acquire))
            break;

        auto plock = mMainloop.getLock();
        if(mCapLen != 0)
        {
            pa_stream_drop(mStream);
            mCapBuffer = {};
            mCapLen = 0;
        }
        const pa_stream_state_t state{pa_stream_get_state(mStream)};
        if UNLIKELY(!PA_STREAM_IS_GOOD(state))
        {
            aluHandleDisconnect(mDevice, "Bad capture state: %u", state);
            break;
        }
        const void *capbuf;
        size_t caplen;
        if UNLIKELY(pa_stream_peek(mStream, &capbuf, &caplen) < 0)
        {
            aluHandleDisconnect(mDevice, "Failed retrieving capture samples: %s",
                pa_strerror(pa_context_errno(mContext)));
            break;
        }
        plock.unlock();

        if(caplen == 0) break;
        if UNLIKELY(!capbuf)
            mCapLen = -static_cast<ssize_t>(caplen);
        else
            mCapLen = static_cast<ssize_t>(caplen);
        mCapBuffer = {static_cast<const al::byte*>(capbuf), caplen};
    }
    if(!dstbuf.empty())
        std::fill(dstbuf.begin(), dstbuf.end(), mSilentVal);

    return ALC_NO_ERROR;
}

ALCuint PulseCapture::availableSamples()
{
    size_t readable{mCapBuffer.size()};

    if(mDevice->Connected.load(std::memory_order_acquire))
    {
        auto _ = mMainloop.getLock();
        size_t got{pa_stream_readable_size(mStream)};
        if UNLIKELY(static_cast<ssize_t>(got) < 0)
        {
            const char *err{pa_strerror(static_cast<int>(got))};
            ERR("pa_stream_readable_size() failed: %s\n", err);
            aluHandleDisconnect(mDevice, "Failed getting readable size: %s", err);
        }
        else
        {
            const auto caplen = static_cast<size_t>(std::abs(mCapLen));
            if(got > caplen) readable += got - caplen;
        }
    }

    readable = std::min<size_t>(readable, std::numeric_limits<ALCuint>::max());
    mLastReadable = std::max(mLastReadable, static_cast<ALCuint>(readable));
    return mLastReadable / static_cast<ALCuint>(pa_frame_size(&mSpec));
}


ClockLatency PulseCapture::getClockLatency()
{
    ClockLatency ret;
    pa_usec_t latency;
    int neg, err;

    {
        auto _ = mMainloop.getLock();
        ret.ClockTime = GetDeviceClockTime(mDevice);
        err = pa_stream_get_latency(mStream, &latency, &neg);
    }

    if UNLIKELY(err != 0)
    {
        ERR("Failed to get stream latency: 0x%x\n", err);
        latency = 0;
        neg = 0;
    }
    else if UNLIKELY(neg)
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
    if(!GetConfigValueBool(nullptr, "pulse", "spawn-server", 1))
        pulse_ctx_flags |= PA_CONTEXT_NOAUTOSPAWN;

    try {
        auto plock = gGlobalMainloop.getLock();
        pa_context *context{gGlobalMainloop.connectContext(plock)};
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
        probePlaybackDevices(gGlobalMainloop);
        std::for_each(PlaybackDevices.cbegin(), PlaybackDevices.cend(), add_device);
        break;

    case DevProbe::Capture:
        probeCaptureDevices(gGlobalMainloop);
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
