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
 *  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA  02111-1307, USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <string.h>

#include "alMain.h"
#include "alu.h"
#include "threads.h"
#include "compat.h"

#include "backends/base.h"

#include <pulse/pulseaudio.h>

#if PA_API_VERSION == 12

#ifndef PA_CHECK_VERSION
#define PA_CHECK_VERSION(major,minor,micro)                             \
    ((PA_MAJOR > (major)) ||                                            \
     (PA_MAJOR == (major) && PA_MINOR > (minor)) ||                     \
     (PA_MAJOR == (major) && PA_MINOR == (minor) && PA_MICRO >= (micro)))
#endif

#ifdef HAVE_DYNLOAD
static void *pa_handle;
#define MAKE_FUNC(x) static __typeof(x) * p##x
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
#if PA_CHECK_VERSION(0,9,15)
MAKE_FUNC(pa_channel_map_superset);
MAKE_FUNC(pa_stream_set_buffer_attr_callback);
#endif
#if PA_CHECK_VERSION(0,9,16)
MAKE_FUNC(pa_stream_begin_write);
#endif
#undef MAKE_FUNC

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
#if PA_CHECK_VERSION(0,9,15)
#define pa_channel_map_superset ppa_channel_map_superset
#define pa_stream_set_buffer_attr_callback ppa_stream_set_buffer_attr_callback
#endif
#if PA_CHECK_VERSION(0,9,16)
#define pa_stream_begin_write ppa_stream_begin_write
#endif

#endif

static ALCboolean pulse_load(void)
{
    ALCboolean ret = ALC_TRUE;
#ifdef HAVE_DYNLOAD
    if(!pa_handle)
    {
#ifdef _WIN32
#define PALIB "libpulse-0.dll"
#elif defined(__APPLE__) && defined(__MACH__)
#define PALIB "libpulse.0.dylib"
#else
#define PALIB "libpulse.so.0"
#endif
        pa_handle = LoadLib(PALIB);
        if(!pa_handle)
            return ALC_FALSE;

#define LOAD_FUNC(x) do {                                                     \
    p##x = GetSymbol(pa_handle, #x);                                          \
    if(!(p##x)) {                                                             \
        ret = ALC_FALSE;                                                      \
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
#undef LOAD_FUNC
#define LOAD_OPTIONAL_FUNC(x) do {                                            \
    p##x = GetSymbol(pa_handle, #x);                                          \
} while(0)
#if PA_CHECK_VERSION(0,9,15)
        LOAD_OPTIONAL_FUNC(pa_channel_map_superset);
        LOAD_OPTIONAL_FUNC(pa_stream_set_buffer_attr_callback);
#endif
#if PA_CHECK_VERSION(0,9,16)
        LOAD_OPTIONAL_FUNC(pa_stream_begin_write);
#endif
#undef LOAD_OPTIONAL_FUNC

        if(ret == ALC_FALSE)
        {
            CloseLib(pa_handle);
            pa_handle = NULL;
        }
    }
#endif /* HAVE_DYNLOAD */
    return ret;
}


/* Global flags and properties */
static pa_context_flags_t pulse_ctx_flags;
static pa_proplist *prop_filter;


/* PulseAudio Event Callbacks */
static void context_state_callback(pa_context *context, void *pdata)
{
    pa_threaded_mainloop *loop = pdata;
    pa_context_state_t state;

    state = pa_context_get_state(context);
    if(state == PA_CONTEXT_READY || !PA_CONTEXT_IS_GOOD(state))
        pa_threaded_mainloop_signal(loop, 0);
}

static void stream_state_callback(pa_stream *stream, void *pdata)
{
    pa_threaded_mainloop *loop = pdata;
    pa_stream_state_t state;

    state = pa_stream_get_state(stream);
    if(state == PA_STREAM_READY || !PA_STREAM_IS_GOOD(state))
        pa_threaded_mainloop_signal(loop, 0);
}

static void stream_success_callback(pa_stream *UNUSED(stream), int UNUSED(success), void *pdata)
{
    pa_threaded_mainloop *loop = pdata;
    pa_threaded_mainloop_signal(loop, 0);
}

static void wait_for_operation(pa_operation *op, pa_threaded_mainloop *loop)
{
    if(op)
    {
        while(pa_operation_get_state(op) == PA_OPERATION_RUNNING)
            pa_threaded_mainloop_wait(loop);
        pa_operation_unref(op);
    }
}


static pa_context *connect_context(pa_threaded_mainloop *loop, ALboolean silent)
{
    const char *name = "OpenAL Soft";
    char path_name[PATH_MAX];
    pa_context_state_t state;
    pa_context *context;
    int err;

    if(pa_get_binary_name(path_name, sizeof(path_name)))
        name = pa_path_get_filename(path_name);

    context = pa_context_new(pa_threaded_mainloop_get_api(loop), name);
    if(!context)
    {
        ERR("pa_context_new() failed\n");
        return NULL;
    }

    pa_context_set_state_callback(context, context_state_callback, loop);

    if((err=pa_context_connect(context, NULL, pulse_ctx_flags, NULL)) >= 0)
    {
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
    pa_context_set_state_callback(context, NULL, NULL);

    if(err < 0)
    {
        if(!silent)
            ERR("Context did not connect: %s\n", pa_strerror(err));
        pa_context_unref(context);
        return NULL;
    }

    return context;
}


static ALCboolean pulse_open(pa_threaded_mainloop **loop, pa_context **context,
                             void(*state_cb)(pa_context*,void*), void *ptr)
{
    if(!(*loop = pa_threaded_mainloop_new()))
    {
        ERR("pa_threaded_mainloop_new() failed!\n");
        return ALC_FALSE;
    }
    if(pa_threaded_mainloop_start(*loop) < 0)
    {
        ERR("pa_threaded_mainloop_start() failed\n");
        goto error;
    }

    pa_threaded_mainloop_lock(*loop);

    *context = connect_context(*loop, AL_FALSE);
    if(!*context)
    {
        pa_threaded_mainloop_unlock(*loop);
        pa_threaded_mainloop_stop(*loop);
        goto error;
    }
    pa_context_set_state_callback(*context, state_cb, ptr);

    pa_threaded_mainloop_unlock(*loop);
    return ALC_TRUE;

error:
    pa_threaded_mainloop_free(*loop);
    *loop = NULL;

    return ALC_FALSE;
}

static void pulse_close(pa_threaded_mainloop *loop, pa_context *context,
                        pa_stream *stream)
{
    pa_threaded_mainloop_lock(loop);

    if(stream)
    {
        pa_stream_set_state_callback(stream, NULL, NULL);
        pa_stream_set_moved_callback(stream, NULL, NULL);
        pa_stream_set_write_callback(stream, NULL, NULL);
#if PA_CHECK_VERSION(0,9,15)
        if(pa_stream_set_buffer_attr_callback)
            pa_stream_set_buffer_attr_callback(stream, NULL, NULL);
#endif
        pa_stream_disconnect(stream);
        pa_stream_unref(stream);
    }

    pa_context_disconnect(context);
    pa_context_unref(context);

    pa_threaded_mainloop_unlock(loop);

    pa_threaded_mainloop_stop(loop);
    pa_threaded_mainloop_free(loop);
}


typedef struct {
    al_string name;
    al_string device_name;
} DevMap;
TYPEDEF_VECTOR(DevMap, vector_DevMap)

static vector_DevMap PlaybackDevices;
static vector_DevMap CaptureDevices;

static void clear_devlist(vector_DevMap *list)
{
#define DEINIT_STRS(i)  (AL_STRING_DEINIT((i)->name),AL_STRING_DEINIT((i)->device_name))
    VECTOR_FOR_EACH(DevMap, *list, DEINIT_STRS);
#undef DEINIT_STRS
    VECTOR_RESIZE(*list, 0);
}


typedef struct ALCpulsePlayback {
    DERIVE_FROM_TYPE(ALCbackend);

    al_string device_name;

    pa_buffer_attr attr;
    pa_sample_spec spec;

    pa_threaded_mainloop *loop;

    pa_stream *stream;
    pa_context *context;

    volatile ALboolean killNow;
    althrd_t thread;
} ALCpulsePlayback;

static void ALCpulsePlayback_deviceCallback(pa_context *context, const pa_sink_info *info, int eol, void *pdata);
static void ALCpulsePlayback_probeDevices(void);

static void ALCpulsePlayback_bufferAttrCallback(pa_stream *stream, void *pdata);
static void ALCpulsePlayback_contextStateCallback(pa_context *context, void *pdata);
static void ALCpulsePlayback_streamStateCallback(pa_stream *stream, void *pdata);
static void ALCpulsePlayback_streamWriteCallback(pa_stream *p, size_t nbytes, void *userdata);
static void ALCpulsePlayback_sinkInfoCallback(pa_context *context, const pa_sink_info *info, int eol, void *pdata);
static void ALCpulsePlayback_sinkNameCallback(pa_context *context, const pa_sink_info *info, int eol, void *pdata);
static void ALCpulsePlayback_streamMovedCallback(pa_stream *stream, void *pdata);
static pa_stream *ALCpulsePlayback_connectStream(const char *device_name, pa_threaded_mainloop *loop,
                                                 pa_context *context, pa_stream_flags_t flags,
                                                 pa_buffer_attr *attr, pa_sample_spec *spec,
                                                 pa_channel_map *chanmap);
static int ALCpulsePlayback_mixerProc(void *ptr);

static void ALCpulsePlayback_Construct(ALCpulsePlayback *self, ALCdevice *device);
static void ALCpulsePlayback_Destruct(ALCpulsePlayback *self);
static ALCenum ALCpulsePlayback_open(ALCpulsePlayback *self, const ALCchar *name);
static void ALCpulsePlayback_close(ALCpulsePlayback *self);
static ALCboolean ALCpulsePlayback_reset(ALCpulsePlayback *self);
static ALCboolean ALCpulsePlayback_start(ALCpulsePlayback *self);
static void ALCpulsePlayback_stop(ALCpulsePlayback *self);
static DECLARE_FORWARD2(ALCpulsePlayback, ALCbackend, ALCenum, captureSamples, ALCvoid*, ALCuint)
static DECLARE_FORWARD(ALCpulsePlayback, ALCbackend, ALCuint, availableSamples)
static ALint64 ALCpulsePlayback_getLatency(ALCpulsePlayback *self);
static void ALCpulsePlayback_lock(ALCpulsePlayback *self);
static void ALCpulsePlayback_unlock(ALCpulsePlayback *self);
DECLARE_DEFAULT_ALLOCATORS(ALCpulsePlayback)

DEFINE_ALCBACKEND_VTABLE(ALCpulsePlayback);


static void ALCpulsePlayback_Construct(ALCpulsePlayback *self, ALCdevice *device)
{
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(ALCpulsePlayback, ALCbackend, self);

    AL_STRING_INIT(self->device_name);
}

static void ALCpulsePlayback_Destruct(ALCpulsePlayback *self)
{
    AL_STRING_DEINIT(self->device_name);
    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
}


static void ALCpulsePlayback_deviceCallback(pa_context *UNUSED(context), const pa_sink_info *info, int eol, void *pdata)
{
    pa_threaded_mainloop *loop = pdata;
    const DevMap *iter;
    DevMap entry;

    if(eol)
    {
        pa_threaded_mainloop_signal(loop, 0);
        return;
    }

#define MATCH_INFO_NAME(iter) (al_string_cmp_cstr((iter)->device_name, info->name) == 0)
    VECTOR_FIND_IF(iter, const DevMap, PlaybackDevices, MATCH_INFO_NAME);
#undef MATCH_INFO_NAME
    if(iter != VECTOR_ITER_END(PlaybackDevices))
        return;

    TRACE("Got device \"%s\", \"%s\"\n", info->description, info->name);

    AL_STRING_INIT(entry.name);
    AL_STRING_INIT(entry.device_name);

    al_string_copy_cstr(&entry.name, info->description);
    al_string_copy_cstr(&entry.device_name, info->name);

    VECTOR_PUSH_BACK(PlaybackDevices, entry);
}

static void ALCpulsePlayback_probeDevices(void)
{
    pa_threaded_mainloop *loop;

    clear_devlist(&PlaybackDevices);

    if((loop=pa_threaded_mainloop_new()) &&
       pa_threaded_mainloop_start(loop) >= 0)
    {
        pa_context *context;

        pa_threaded_mainloop_lock(loop);
        context = connect_context(loop, AL_FALSE);
        if(context)
        {
            pa_operation *o;
            pa_stream_flags_t flags;
            pa_sample_spec spec;
            pa_stream *stream;

            flags = PA_STREAM_FIX_FORMAT | PA_STREAM_FIX_RATE |
                    PA_STREAM_FIX_CHANNELS | PA_STREAM_DONT_MOVE;

            spec.format = PA_SAMPLE_S16NE;
            spec.rate = 44100;
            spec.channels = 2;

            stream = ALCpulsePlayback_connectStream(NULL, loop, context, flags,
                                                    NULL, &spec, NULL);
            if(stream)
            {
                o = pa_context_get_sink_info_by_name(context, pa_stream_get_device_name(stream),
                                                     ALCpulsePlayback_deviceCallback, loop);
                wait_for_operation(o, loop);

                pa_stream_disconnect(stream);
                pa_stream_unref(stream);
                stream = NULL;
            }

            o = pa_context_get_sink_info_list(context, ALCpulsePlayback_deviceCallback, loop);
            wait_for_operation(o, loop);

            pa_context_disconnect(context);
            pa_context_unref(context);
        }
        pa_threaded_mainloop_unlock(loop);
        pa_threaded_mainloop_stop(loop);
    }
    if(loop)
        pa_threaded_mainloop_free(loop);
}


static void ALCpulsePlayback_bufferAttrCallback(pa_stream *stream, void *pdata)
{
    ALCpulsePlayback *self = pdata;

    self->attr = *pa_stream_get_buffer_attr(stream);
    TRACE("minreq=%d, tlength=%d, prebuf=%d\n", self->attr.minreq, self->attr.tlength, self->attr.prebuf);
}

static void ALCpulsePlayback_contextStateCallback(pa_context *context, void *pdata)
{
    ALCpulsePlayback *self = pdata;
    if(pa_context_get_state(context) == PA_CONTEXT_FAILED)
    {
        ERR("Received context failure!\n");
        aluHandleDisconnect(STATIC_CAST(ALCbackend,self)->mDevice);
    }
    pa_threaded_mainloop_signal(self->loop, 0);
}

static void ALCpulsePlayback_streamStateCallback(pa_stream *stream, void *pdata)
{
    ALCpulsePlayback *self = pdata;
    if(pa_stream_get_state(stream) == PA_STREAM_FAILED)
    {
        ERR("Received stream failure!\n");
        aluHandleDisconnect(STATIC_CAST(ALCbackend,self)->mDevice);
    }
    pa_threaded_mainloop_signal(self->loop, 0);
}

static void ALCpulsePlayback_streamWriteCallback(pa_stream* UNUSED(p), size_t UNUSED(nbytes), void *pdata)
{
    ALCpulsePlayback *self = pdata;
    pa_threaded_mainloop_signal(self->loop, 0);
}

static void ALCpulsePlayback_sinkInfoCallback(pa_context *UNUSED(context), const pa_sink_info *info, int eol, void *pdata)
{
    ALCpulsePlayback *self = pdata;
    ALCdevice *device = STATIC_CAST(ALCbackend,self)->mDevice;
    char chanmap_str[256] = "";
    const struct {
        const char *str;
        enum DevFmtChannels chans;
    } chanmaps[] = {
        { "front-left,front-right,front-center,lfe,rear-left,rear-right,side-left,side-right",
          DevFmtX71 },
        { "front-left,front-right,front-center,lfe,rear-center,side-left,side-right",
          DevFmtX61 },
        { "front-left,front-right,front-center,lfe,rear-left,rear-right",
          DevFmtX51 },
        { "front-left,front-right,front-center,lfe,side-left,side-right",
          DevFmtX51Side },
        { "front-left,front-right,rear-left,rear-right", DevFmtQuad },
        { "front-left,front-right", DevFmtStereo },
        { "mono", DevFmtMono },
        { NULL, 0 }
    };
    int i;

    if(eol)
    {
        pa_threaded_mainloop_signal(self->loop, 0);
        return;
    }

    for(i = 0;chanmaps[i].str;i++)
    {
        pa_channel_map map;
        if(!pa_channel_map_parse(&map, chanmaps[i].str))
            continue;

        if(pa_channel_map_equal(&info->channel_map, &map)
#if PA_CHECK_VERSION(0,9,15)
           || (pa_channel_map_superset &&
               pa_channel_map_superset(&info->channel_map, &map))
#endif
            )
        {
            device->FmtChans = chanmaps[i].chans;
            return;
        }
    }

    pa_channel_map_snprint(chanmap_str, sizeof(chanmap_str), &info->channel_map);
    ERR("Failed to find format for channel map:\n    %s\n", chanmap_str);
}

static void ALCpulsePlayback_sinkNameCallback(pa_context *UNUSED(context), const pa_sink_info *info, int eol, void *pdata)
{
    ALCpulsePlayback *self = pdata;
    ALCdevice *device = STATIC_CAST(ALCbackend,self)->mDevice;

    if(eol)
    {
        pa_threaded_mainloop_signal(self->loop, 0);
        return;
    }

    al_string_copy_cstr(&device->DeviceName, info->description);
}


static void ALCpulsePlayback_streamMovedCallback(pa_stream *stream, void *pdata)
{
    ALCpulsePlayback *self = pdata;

    al_string_copy_cstr(&self->device_name, pa_stream_get_device_name(stream));

    TRACE("Stream moved to %s\n", al_string_get_cstr(self->device_name));
}


static pa_stream *ALCpulsePlayback_connectStream(const char *device_name,
    pa_threaded_mainloop *loop, pa_context *context,
    pa_stream_flags_t flags, pa_buffer_attr *attr, pa_sample_spec *spec,
    pa_channel_map *chanmap)
{
    pa_stream_state_t state;
    pa_stream *stream;

    stream = pa_stream_new_with_proplist(context, "Playback Stream", spec, chanmap, prop_filter);
    if(!stream)
    {
        ERR("pa_stream_new_with_proplist() failed: %s\n", pa_strerror(pa_context_errno(context)));
        return NULL;
    }

    pa_stream_set_state_callback(stream, stream_state_callback, loop);

    if(pa_stream_connect_playback(stream, device_name, attr, flags, NULL, NULL) < 0)
    {
        ERR("Stream did not connect: %s\n", pa_strerror(pa_context_errno(context)));
        pa_stream_unref(stream);
        return NULL;
    }

    while((state=pa_stream_get_state(stream)) != PA_STREAM_READY)
    {
        if(!PA_STREAM_IS_GOOD(state))
        {
            ERR("Stream did not get ready: %s\n", pa_strerror(pa_context_errno(context)));
            pa_stream_unref(stream);
            return NULL;
        }

        pa_threaded_mainloop_wait(loop);
    }
    pa_stream_set_state_callback(stream, NULL, NULL);

    return stream;
}


static int ALCpulsePlayback_mixerProc(void *ptr)
{
    ALCpulsePlayback *self = ptr;
    ALCdevice *device = STATIC_CAST(ALCbackend,self)->mDevice;
    ALuint buffer_size;
    ALint update_size;
    size_t frame_size;
    ssize_t len;

    SetRTPriority();
    althrd_setname(althrd_current(), MIXER_THREAD_NAME);

    pa_threaded_mainloop_lock(self->loop);
    frame_size = pa_frame_size(&self->spec);
    update_size = device->UpdateSize * frame_size;

    /* Sanitize buffer metrics, in case we actually have less than what we
     * asked for. */
    buffer_size = minu(update_size*device->NumUpdates, self->attr.tlength);
    update_size = minu(update_size, buffer_size/2);
    do {
        len = pa_stream_writable_size(self->stream) - self->attr.tlength +
              buffer_size;
        if(len < update_size)
        {
            if(pa_stream_is_corked(self->stream) == 1)
            {
                pa_operation *o;
                o = pa_stream_cork(self->stream, 0, NULL, NULL);
                if(o) pa_operation_unref(o);
            }
            pa_threaded_mainloop_wait(self->loop);
            continue;
        }
        len -= len%update_size;

        while(len > 0)
        {
            size_t newlen = len;
            void *buf;
            pa_free_cb_t free_func = NULL;

#if PA_CHECK_VERSION(0,9,16)
            if(!pa_stream_begin_write ||
               pa_stream_begin_write(self->stream, &buf, &newlen) < 0)
#endif
            {
                buf = pa_xmalloc(newlen);
                free_func = pa_xfree;
            }

            aluMixData(device, buf, newlen/frame_size);

            pa_stream_write(self->stream, buf, newlen, free_func, 0, PA_SEEK_RELATIVE);
            len -= newlen;
        }
    } while(!self->killNow && device->Connected);
    pa_threaded_mainloop_unlock(self->loop);

    return 0;
}


static ALCenum ALCpulsePlayback_open(ALCpulsePlayback *self, const ALCchar *name)
{
    const char *pulse_name = NULL;
    pa_stream_flags_t flags;
    pa_sample_spec spec;
    pa_operation *o;

    if(name)
    {
        const DevMap *iter;

        if(VECTOR_SIZE(PlaybackDevices) == 0)
            ALCpulsePlayback_probeDevices();

#define MATCH_NAME(iter) (al_string_cmp_cstr((iter)->name, name) == 0)
        VECTOR_FIND_IF(iter, const DevMap, PlaybackDevices, MATCH_NAME);
#undef MATCH_NAME
        if(iter == VECTOR_ITER_END(PlaybackDevices))
            return ALC_INVALID_VALUE;
        pulse_name = al_string_get_cstr(iter->device_name);
    }

    if(!pulse_open(&self->loop, &self->context, ALCpulsePlayback_contextStateCallback, self))
        return ALC_INVALID_VALUE;

    pa_threaded_mainloop_lock(self->loop);

    flags = PA_STREAM_FIX_FORMAT | PA_STREAM_FIX_RATE |
            PA_STREAM_FIX_CHANNELS;
    if(!GetConfigValueBool("pulse", "allow-moves", 0))
        flags |= PA_STREAM_DONT_MOVE;

    spec.format = PA_SAMPLE_S16NE;
    spec.rate = 44100;
    spec.channels = 2;

    TRACE("Connecting to \"%s\"\n", pulse_name ? pulse_name : "(default)");
    self->stream = ALCpulsePlayback_connectStream(pulse_name, self->loop, self->context,
                                                  flags, NULL, &spec, NULL);
    if(!self->stream)
    {
        pa_threaded_mainloop_unlock(self->loop);
        pulse_close(self->loop, self->context, self->stream);
        self->loop = NULL;
        self->context = NULL;
        return ALC_INVALID_VALUE;
    }
    pa_stream_set_moved_callback(self->stream, ALCpulsePlayback_streamMovedCallback, self);

    al_string_copy_cstr(&self->device_name, pa_stream_get_device_name(self->stream));
    o = pa_context_get_sink_info_by_name(self->context,
                                         al_string_get_cstr(self->device_name),
                                         ALCpulsePlayback_sinkNameCallback, self);
    wait_for_operation(o, self->loop);

    pa_threaded_mainloop_unlock(self->loop);

    return ALC_NO_ERROR;
}

static void ALCpulsePlayback_close(ALCpulsePlayback *self)
{
    pulse_close(self->loop, self->context, self->stream);
    self->loop = NULL;
    self->context = NULL;
    self->stream = NULL;

    al_string_clear(&self->device_name);
}

static ALCboolean ALCpulsePlayback_reset(ALCpulsePlayback *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend,self)->mDevice;
    pa_stream_flags_t flags = 0;
    const char *mapname = NULL;
    pa_channel_map chanmap;
    ALuint len;

    pa_threaded_mainloop_lock(self->loop);

    if(self->stream)
    {
        pa_stream_set_moved_callback(self->stream, NULL, NULL);
#if PA_CHECK_VERSION(0,9,15)
        if(pa_stream_set_buffer_attr_callback)
            pa_stream_set_buffer_attr_callback(self->stream, NULL, NULL);
#endif
        pa_stream_disconnect(self->stream);
        pa_stream_unref(self->stream);
        self->stream = NULL;
    }

    if(!(device->Flags&DEVICE_CHANNELS_REQUEST))
    {
        pa_operation *o;
        o = pa_context_get_sink_info_by_name(self->context,
                                             al_string_get_cstr(self->device_name),
                                             ALCpulsePlayback_sinkInfoCallback, self);
        wait_for_operation(o, self->loop);
    }
    if(!(device->Flags&DEVICE_FREQUENCY_REQUEST))
        flags |= PA_STREAM_FIX_RATE;

    flags |= PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE;
    flags |= PA_STREAM_ADJUST_LATENCY;
    flags |= PA_STREAM_START_CORKED;
    if(!GetConfigValueBool("pulse", "allow-moves", 0))
        flags |= PA_STREAM_DONT_MOVE;

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
    self->spec.channels = ChannelsFromDevFmt(device->FmtChans);

    if(pa_sample_spec_valid(&self->spec) == 0)
    {
        ERR("Invalid sample format\n");
        pa_threaded_mainloop_unlock(self->loop);
        return ALC_FALSE;
    }

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
            mapname = "front-left,front-right,front-center,lfe,rear-left,rear-right";
            break;
        case DevFmtX51Side:
            mapname = "front-left,front-right,front-center,lfe,side-left,side-right";
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
        pa_threaded_mainloop_unlock(self->loop);
        return ALC_FALSE;
    }
    SetDefaultWFXChannelOrder(device);

    self->attr.fragsize = -1;
    self->attr.prebuf = 0;
    self->attr.minreq = device->UpdateSize * pa_frame_size(&self->spec);
    self->attr.tlength = self->attr.minreq * maxu(device->NumUpdates, 2);
    self->attr.maxlength = -1;

    self->stream = ALCpulsePlayback_connectStream(al_string_get_cstr(self->device_name),
                                                  self->loop, self->context, flags,
                                                  &self->attr, &self->spec, &chanmap);
    if(!self->stream)
    {
        pa_threaded_mainloop_unlock(self->loop);
        return ALC_FALSE;
    }
    pa_stream_set_state_callback(self->stream, ALCpulsePlayback_streamStateCallback, self);
    pa_stream_set_moved_callback(self->stream, ALCpulsePlayback_streamMovedCallback, self);
    pa_stream_set_write_callback(self->stream, ALCpulsePlayback_streamWriteCallback, self);

    self->spec = *(pa_stream_get_sample_spec(self->stream));
    if(device->Frequency != self->spec.rate)
    {
        pa_operation *o;

        /* Server updated our playback rate, so modify the buffer attribs
         * accordingly. */
        device->NumUpdates = (ALuint)((ALdouble)device->NumUpdates / device->Frequency *
                                      self->spec.rate + 0.5);
        self->attr.minreq  = device->UpdateSize * pa_frame_size(&self->spec);
        self->attr.tlength = self->attr.minreq * clampu(device->NumUpdates, 2, 16);
        self->attr.maxlength = -1;
        self->attr.prebuf  = 0;

        o = pa_stream_set_buffer_attr(self->stream, &self->attr,
                                      stream_success_callback, self->loop);
        wait_for_operation(o, self->loop);

        device->Frequency = self->spec.rate;
    }

#if PA_CHECK_VERSION(0,9,15)
    if(pa_stream_set_buffer_attr_callback)
        pa_stream_set_buffer_attr_callback(self->stream, ALCpulsePlayback_bufferAttrCallback, self);
#endif
    ALCpulsePlayback_bufferAttrCallback(self->stream, self);

    len = self->attr.minreq / pa_frame_size(&self->spec);
    if((CPUCapFlags&CPU_CAP_SSE))
        len = (len+3)&~3;
    device->NumUpdates = (ALuint)((ALdouble)device->NumUpdates/len*device->UpdateSize + 0.5);
    device->NumUpdates = clampu(device->NumUpdates, 2, 16);
    device->UpdateSize = len;

    pa_threaded_mainloop_unlock(self->loop);
    return ALC_TRUE;
}

static ALCboolean ALCpulsePlayback_start(ALCpulsePlayback *self)
{
    self->killNow = AL_FALSE;
    if(althrd_create(&self->thread, ALCpulsePlayback_mixerProc, self) != althrd_success)
        return ALC_FALSE;
    return ALC_TRUE;
}

static void ALCpulsePlayback_stop(ALCpulsePlayback *self)
{
    pa_operation *o;
    int res;

    if(!self->stream)
        return;

    self->killNow = AL_TRUE;
    althrd_join(self->thread, &res);

    pa_threaded_mainloop_lock(self->loop);

    o = pa_stream_cork(self->stream, 1, stream_success_callback, self->loop);
    wait_for_operation(o, self->loop);

    pa_threaded_mainloop_unlock(self->loop);
}


static ALint64 ALCpulsePlayback_getLatency(ALCpulsePlayback *self)
{
    pa_usec_t latency = 0;
    int neg, err;

    if((err=pa_stream_get_latency(self->stream, &latency, &neg)) != 0)
    {
        /* FIXME: if err = -PA_ERR_NODATA, it means we were called too soon
         * after starting the stream and no timing info has been received from
         * the server yet. Should we wait, possibly stalling the app, or give a
         * dummy value? Either way, it shouldn't be 0. */
        if(err != -PA_ERR_NODATA)
            ERR("Failed to get stream latency: 0x%x\n", err);
        return 0;
    }

    if(neg) latency = 0;
    return (ALint64)minu64(latency, U64(0x7fffffffffffffff)/1000) * 1000;
}


static void ALCpulsePlayback_lock(ALCpulsePlayback *self)
{
    pa_threaded_mainloop_lock(self->loop);
}

static void ALCpulsePlayback_unlock(ALCpulsePlayback *self)
{
    pa_threaded_mainloop_unlock(self->loop);
}


typedef struct ALCpulseCapture {
    DERIVE_FROM_TYPE(ALCbackend);

    al_string device_name;

    const void *cap_store;
    size_t cap_len;
    size_t cap_remain;

    ALCuint last_readable;

    pa_buffer_attr attr;
    pa_sample_spec spec;

    pa_threaded_mainloop *loop;

    pa_stream *stream;
    pa_context *context;
} ALCpulseCapture;

static void ALCpulseCapture_deviceCallback(pa_context *context, const pa_source_info *info, int eol, void *pdata);
static void ALCpulseCapture_probeDevices(void);

static void ALCpulseCapture_contextStateCallback(pa_context *context, void *pdata);
static void ALCpulseCapture_streamStateCallback(pa_stream *stream, void *pdata);
static void ALCpulseCapture_sourceNameCallback(pa_context *context, const pa_source_info *info, int eol, void *pdata);
static void ALCpulseCapture_streamMovedCallback(pa_stream *stream, void *pdata);
static pa_stream *ALCpulseCapture_connectStream(const char *device_name,
                                                pa_threaded_mainloop *loop, pa_context *context,
                                                pa_stream_flags_t flags, pa_buffer_attr *attr,
                                                pa_sample_spec *spec, pa_channel_map *chanmap);

static void ALCpulseCapture_Construct(ALCpulseCapture *self, ALCdevice *device);
static void ALCpulseCapture_Destruct(ALCpulseCapture *self);
static ALCenum ALCpulseCapture_open(ALCpulseCapture *self, const ALCchar *name);
static void ALCpulseCapture_close(ALCpulseCapture *self);
static DECLARE_FORWARD(ALCpulseCapture, ALCbackend, ALCboolean, reset)
static ALCboolean ALCpulseCapture_start(ALCpulseCapture *self);
static void ALCpulseCapture_stop(ALCpulseCapture *self);
static ALCenum ALCpulseCapture_captureSamples(ALCpulseCapture *self, ALCvoid *buffer, ALCuint samples);
static ALCuint ALCpulseCapture_availableSamples(ALCpulseCapture *self);
static ALint64 ALCpulseCapture_getLatency(ALCpulseCapture *self);
static void ALCpulseCapture_lock(ALCpulseCapture *self);
static void ALCpulseCapture_unlock(ALCpulseCapture *self);
DECLARE_DEFAULT_ALLOCATORS(ALCpulseCapture)

DEFINE_ALCBACKEND_VTABLE(ALCpulseCapture);


static void ALCpulseCapture_Construct(ALCpulseCapture *self, ALCdevice *device)
{
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(ALCpulseCapture, ALCbackend, self);

    AL_STRING_INIT(self->device_name);
}

static void ALCpulseCapture_Destruct(ALCpulseCapture *self)
{
    AL_STRING_DEINIT(self->device_name);
    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
}


static void ALCpulseCapture_deviceCallback(pa_context *UNUSED(context), const pa_source_info *info, int eol, void *pdata)
{
    pa_threaded_mainloop *loop = pdata;
    const DevMap *iter;
    DevMap entry;

    if(eol)
    {
        pa_threaded_mainloop_signal(loop, 0);
        return;
    }

#define MATCH_INFO_NAME(iter) (al_string_cmp_cstr((iter)->device_name, info->name) == 0)
    VECTOR_FIND_IF(iter, const DevMap, CaptureDevices, MATCH_INFO_NAME);
#undef MATCH_INFO_NAME
    if(iter != VECTOR_ITER_END(CaptureDevices))
        return;

    TRACE("Got device \"%s\", \"%s\"\n", info->description, info->name);

    AL_STRING_INIT(entry.name);
    AL_STRING_INIT(entry.device_name);

    al_string_copy_cstr(&entry.name, info->description);
    al_string_copy_cstr(&entry.device_name, info->name);

    VECTOR_PUSH_BACK(CaptureDevices, entry);
}

static void ALCpulseCapture_probeDevices(void)
{
    pa_threaded_mainloop *loop;

    clear_devlist(&CaptureDevices);

    if((loop=pa_threaded_mainloop_new()) &&
       pa_threaded_mainloop_start(loop) >= 0)
    {
        pa_context *context;

        pa_threaded_mainloop_lock(loop);
        context = connect_context(loop, AL_FALSE);
        if(context)
        {
            pa_operation *o;
            pa_stream_flags_t flags;
            pa_sample_spec spec;
            pa_stream *stream;

            flags = PA_STREAM_FIX_FORMAT | PA_STREAM_FIX_RATE |
                    PA_STREAM_FIX_CHANNELS | PA_STREAM_DONT_MOVE;

            spec.format = PA_SAMPLE_S16NE;
            spec.rate = 44100;
            spec.channels = 1;

            stream = ALCpulseCapture_connectStream(NULL, loop, context, flags,
                                                   NULL, &spec, NULL);
            if(stream)
            {
                o = pa_context_get_source_info_by_name(context, pa_stream_get_device_name(stream),
                                                       ALCpulseCapture_deviceCallback, loop);
                wait_for_operation(o, loop);

                pa_stream_disconnect(stream);
                pa_stream_unref(stream);
                stream = NULL;
            }

            o = pa_context_get_source_info_list(context, ALCpulseCapture_deviceCallback, loop);
            wait_for_operation(o, loop);

            pa_context_disconnect(context);
            pa_context_unref(context);
        }
        pa_threaded_mainloop_unlock(loop);
        pa_threaded_mainloop_stop(loop);
    }
    if(loop)
        pa_threaded_mainloop_free(loop);
}


static void ALCpulseCapture_contextStateCallback(pa_context *context, void *pdata)
{
    ALCpulseCapture *self = pdata;
    if(pa_context_get_state(context) == PA_CONTEXT_FAILED)
    {
        ERR("Received context failure!\n");
        aluHandleDisconnect(STATIC_CAST(ALCbackend,self)->mDevice);
    }
    pa_threaded_mainloop_signal(self->loop, 0);
}

static void ALCpulseCapture_streamStateCallback(pa_stream *stream, void *pdata)
{
    ALCpulseCapture *self = pdata;
    if(pa_stream_get_state(stream) == PA_STREAM_FAILED)
    {
        ERR("Received stream failure!\n");
        aluHandleDisconnect(STATIC_CAST(ALCbackend,self)->mDevice);
    }
    pa_threaded_mainloop_signal(self->loop, 0);
}


static void ALCpulseCapture_sourceNameCallback(pa_context *UNUSED(context), const pa_source_info *info, int eol, void *pdata)
{
    ALCpulseCapture *self = pdata;
    ALCdevice *device = STATIC_CAST(ALCbackend,self)->mDevice;

    if(eol)
    {
        pa_threaded_mainloop_signal(self->loop, 0);
        return;
    }

    al_string_copy_cstr(&device->DeviceName, info->description);
}


static void ALCpulseCapture_streamMovedCallback(pa_stream *stream, void *pdata)
{
    ALCpulseCapture *self = pdata;

    al_string_copy_cstr(&self->device_name, pa_stream_get_device_name(stream));

    TRACE("Stream moved to %s\n", al_string_get_cstr(self->device_name));
}


static pa_stream *ALCpulseCapture_connectStream(const char *device_name,
    pa_threaded_mainloop *loop, pa_context *context,
    pa_stream_flags_t flags, pa_buffer_attr *attr, pa_sample_spec *spec,
    pa_channel_map *chanmap)
{
    pa_stream_state_t state;
    pa_stream *stream;

    stream = pa_stream_new_with_proplist(context, "Capture Stream", spec, chanmap, prop_filter);
    if(!stream)
    {
        ERR("pa_stream_new_with_proplist() failed: %s\n", pa_strerror(pa_context_errno(context)));
        return NULL;
    }

    pa_stream_set_state_callback(stream, stream_state_callback, loop);

    if(pa_stream_connect_record(stream, device_name, attr, flags) < 0)
    {
        ERR("Stream did not connect: %s\n", pa_strerror(pa_context_errno(context)));
        pa_stream_unref(stream);
        return NULL;
    }

    while((state=pa_stream_get_state(stream)) != PA_STREAM_READY)
    {
        if(!PA_STREAM_IS_GOOD(state))
        {
            ERR("Stream did not get ready: %s\n", pa_strerror(pa_context_errno(context)));
            pa_stream_unref(stream);
            return NULL;
        }

        pa_threaded_mainloop_wait(loop);
    }
    pa_stream_set_state_callback(stream, NULL, NULL);

    return stream;
}


static ALCenum ALCpulseCapture_open(ALCpulseCapture *self, const ALCchar *name)
{
    ALCdevice *device = STATIC_CAST(ALCbackend,self)->mDevice;
    const char *pulse_name = NULL;
    pa_stream_flags_t flags = 0;
    pa_channel_map chanmap;
    pa_operation *o;
    ALuint samples;

    if(name)
    {
        const DevMap *iter;

        if(VECTOR_SIZE(CaptureDevices) == 0)
            ALCpulseCapture_probeDevices();

#define MATCH_NAME(iter) (al_string_cmp_cstr((iter)->name, name) == 0)
        VECTOR_FIND_IF(iter, const DevMap, CaptureDevices, MATCH_NAME);
#undef MATCH_NAME
        if(iter == VECTOR_ITER_END(CaptureDevices))
            return ALC_INVALID_VALUE;
        pulse_name = al_string_get_cstr(iter->device_name);
    }

    if(!pulse_open(&self->loop, &self->context, ALCpulseCapture_contextStateCallback, self))
        return ALC_INVALID_VALUE;

    pa_threaded_mainloop_lock(self->loop);

    self->spec.rate = device->Frequency;
    self->spec.channels = ChannelsFromDevFmt(device->FmtChans);

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
            pa_threaded_mainloop_unlock(self->loop);
            goto fail;
    }

    if(pa_sample_spec_valid(&self->spec) == 0)
    {
        ERR("Invalid sample format\n");
        pa_threaded_mainloop_unlock(self->loop);
        goto fail;
    }

    if(!pa_channel_map_init_auto(&chanmap, self->spec.channels, PA_CHANNEL_MAP_WAVEEX))
    {
        ERR("Couldn't build map for channel count (%d)!\n", self->spec.channels);
        pa_threaded_mainloop_unlock(self->loop);
        goto fail;
    }

    samples = device->UpdateSize * device->NumUpdates;
    samples = maxu(samples, 100 * device->Frequency / 1000);

    self->attr.minreq = -1;
    self->attr.prebuf = -1;
    self->attr.maxlength = samples * pa_frame_size(&self->spec);
    self->attr.tlength = -1;
    self->attr.fragsize = minu(samples, 50*device->Frequency/1000) *
                          pa_frame_size(&self->spec);

    flags |= PA_STREAM_START_CORKED|PA_STREAM_ADJUST_LATENCY;
    if(!GetConfigValueBool("pulse", "allow-moves", 0))
        flags |= PA_STREAM_DONT_MOVE;

    TRACE("Connecting to \"%s\"\n", pulse_name ? pulse_name : "(default)");
    self->stream = ALCpulseCapture_connectStream(pulse_name, self->loop, self->context,
                                                 flags, &self->attr, &self->spec,
                                                 &chanmap);
    if(!self->stream)
    {
        pa_threaded_mainloop_unlock(self->loop);
        goto fail;
    }
    pa_stream_set_moved_callback(self->stream, ALCpulseCapture_streamMovedCallback, self);
    pa_stream_set_state_callback(self->stream, ALCpulseCapture_streamStateCallback, self);

    al_string_copy_cstr(&self->device_name, pa_stream_get_device_name(self->stream));
    o = pa_context_get_source_info_by_name(self->context,
                                           al_string_get_cstr(self->device_name),
                                           ALCpulseCapture_sourceNameCallback, self);
    wait_for_operation(o, self->loop);

    pa_threaded_mainloop_unlock(self->loop);
    return ALC_NO_ERROR;

fail:
    pulse_close(self->loop, self->context, self->stream);
    self->loop = NULL;
    self->context = NULL;
    self->stream = NULL;

    return ALC_INVALID_VALUE;
}

static void ALCpulseCapture_close(ALCpulseCapture *self)
{
    pulse_close(self->loop, self->context, self->stream);
    self->loop = NULL;
    self->context = NULL;
    self->stream = NULL;

    al_string_clear(&self->device_name);
}

static ALCboolean ALCpulseCapture_start(ALCpulseCapture *self)
{
    pa_operation *o;
    o = pa_stream_cork(self->stream, 0, stream_success_callback, self->loop);
    wait_for_operation(o, self->loop);

    return ALC_TRUE;
}

static void ALCpulseCapture_stop(ALCpulseCapture *self)
{
    pa_operation *o;
    o = pa_stream_cork(self->stream, 1, stream_success_callback, self->loop);
    wait_for_operation(o, self->loop);
}

static ALCenum ALCpulseCapture_captureSamples(ALCpulseCapture *self, ALCvoid *buffer, ALCuint samples)
{
    ALCdevice *device = STATIC_CAST(ALCbackend,self)->mDevice;
    ALCuint todo = samples * pa_frame_size(&self->spec);

    /* Capture is done in fragment-sized chunks, so we loop until we get all
     * that's available */
    self->last_readable -= todo;
    while(todo > 0)
    {
        size_t rem = todo;

        if(self->cap_len == 0)
        {
            pa_stream_state_t state;

            state = pa_stream_get_state(self->stream);
            if(!PA_STREAM_IS_GOOD(state))
            {
                aluHandleDisconnect(device);
                break;
            }
            if(pa_stream_peek(self->stream, &self->cap_store, &self->cap_len) < 0)
            {
                ERR("pa_stream_peek() failed: %s\n",
                    pa_strerror(pa_context_errno(self->context)));
                aluHandleDisconnect(device);
                break;
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
    if(todo > 0)
        memset(buffer, ((device->FmtType==DevFmtUByte) ? 0x80 : 0), todo);

    return ALC_NO_ERROR;
}

static ALCuint ALCpulseCapture_availableSamples(ALCpulseCapture *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend,self)->mDevice;
    size_t readable = self->cap_remain;

    if(device->Connected)
    {
        ssize_t got = pa_stream_readable_size(self->stream);
        if(got < 0)
        {
            ERR("pa_stream_readable_size() failed: %s\n", pa_strerror(got));
            aluHandleDisconnect(device);
        }
        else if((size_t)got > self->cap_len)
            readable += got - self->cap_len;
    }

    if(self->last_readable < readable)
        self->last_readable = readable;
    return self->last_readable / pa_frame_size(&self->spec);
}


static ALint64 ALCpulseCapture_getLatency(ALCpulseCapture *self)
{
    pa_usec_t latency = 0;
    int neg;

    if(pa_stream_get_latency(self->stream, &latency, &neg) != 0)
    {
        ERR("Failed to get stream latency!\n");
        return 0;
    }

    if(neg) latency = 0;
    return (ALint64)minu64(latency, U64(0x7fffffffffffffff)/1000) * 1000;
}


static void ALCpulseCapture_lock(ALCpulseCapture *self)
{
    pa_threaded_mainloop_lock(self->loop);
}

static void ALCpulseCapture_unlock(ALCpulseCapture *self)
{
    pa_threaded_mainloop_unlock(self->loop);
}


static inline void AppendAllDevicesList2(const DevMap *entry)
{ AppendAllDevicesList(al_string_get_cstr(entry->name)); }
static inline void AppendCaptureDeviceList2(const DevMap *entry)
{ AppendCaptureDeviceList(al_string_get_cstr(entry->name)); }

typedef struct ALCpulseBackendFactory {
    DERIVE_FROM_TYPE(ALCbackendFactory);
} ALCpulseBackendFactory;
#define ALCPULSEBACKENDFACTORY_INITIALIZER { { GET_VTABLE2(ALCpulseBackendFactory, ALCbackendFactory) } }

static ALCboolean ALCpulseBackendFactory_init(ALCpulseBackendFactory *self);
static void ALCpulseBackendFactory_deinit(ALCpulseBackendFactory *self);
static ALCboolean ALCpulseBackendFactory_querySupport(ALCpulseBackendFactory *self, ALCbackend_Type type);
static void ALCpulseBackendFactory_probe(ALCpulseBackendFactory *self, enum DevProbe type);
static ALCbackend* ALCpulseBackendFactory_createBackend(ALCpulseBackendFactory *self, ALCdevice *device, ALCbackend_Type type);

DEFINE_ALCBACKENDFACTORY_VTABLE(ALCpulseBackendFactory);


static ALCboolean ALCpulseBackendFactory_init(ALCpulseBackendFactory* UNUSED(self))
{
    ALCboolean ret = ALC_FALSE;

    VECTOR_INIT(PlaybackDevices);
    VECTOR_INIT(CaptureDevices);

    if(pulse_load())
    {
        pa_threaded_mainloop *loop;

        pulse_ctx_flags = 0;
        if(!GetConfigValueBool("pulse", "spawn-server", 1))
            pulse_ctx_flags |= PA_CONTEXT_NOAUTOSPAWN;

        if((loop=pa_threaded_mainloop_new()) &&
           pa_threaded_mainloop_start(loop) >= 0)
        {
            pa_context *context;

            pa_threaded_mainloop_lock(loop);
            context = connect_context(loop, AL_TRUE);
            if(context)
            {
                ret = ALC_TRUE;

                /* Some libraries (Phonon, Qt) set some pulseaudio properties
                 * through environment variables, which causes all streams in
                 * the process to inherit them. This attempts to filter those
                 * properties out by setting them to 0-length data. */
                prop_filter = pa_proplist_new();
                pa_proplist_set(prop_filter, PA_PROP_MEDIA_ROLE, NULL, 0);
                pa_proplist_set(prop_filter, "phonon.streamid", NULL, 0);

                pa_context_disconnect(context);
                pa_context_unref(context);
            }
            pa_threaded_mainloop_unlock(loop);
            pa_threaded_mainloop_stop(loop);
        }
        if(loop)
            pa_threaded_mainloop_free(loop);
    }

    return ret;
}

static void ALCpulseBackendFactory_deinit(ALCpulseBackendFactory* UNUSED(self))
{
    clear_devlist(&PlaybackDevices);
    VECTOR_DEINIT(PlaybackDevices);

    clear_devlist(&CaptureDevices);
    VECTOR_DEINIT(CaptureDevices);

    if(prop_filter)
        pa_proplist_free(prop_filter);
    prop_filter = NULL;

    /* PulseAudio doesn't like being CloseLib'd sometimes */
}

static ALCboolean ALCpulseBackendFactory_querySupport(ALCpulseBackendFactory* UNUSED(self), ALCbackend_Type type)
{
    if(type == ALCbackend_Playback || type == ALCbackend_Capture)
        return ALC_TRUE;
    return ALC_FALSE;
}

static void ALCpulseBackendFactory_probe(ALCpulseBackendFactory* UNUSED(self), enum DevProbe type)
{
    switch(type)
    {
        case ALL_DEVICE_PROBE:
            ALCpulsePlayback_probeDevices();
            VECTOR_FOR_EACH(const DevMap, PlaybackDevices, AppendAllDevicesList2);
            break;

        case CAPTURE_DEVICE_PROBE:
            ALCpulseCapture_probeDevices();
            VECTOR_FOR_EACH(const DevMap, CaptureDevices, AppendCaptureDeviceList2);
            break;
    }
}

static ALCbackend* ALCpulseBackendFactory_createBackend(ALCpulseBackendFactory* UNUSED(self), ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        ALCpulsePlayback *backend;

        backend = ALCpulsePlayback_New(sizeof(*backend));
        if(!backend) return NULL;
        memset(backend, 0, sizeof(*backend));

        ALCpulsePlayback_Construct(backend, device);

        return STATIC_CAST(ALCbackend, backend);
    }
    if(type == ALCbackend_Capture)
    {
        ALCpulseCapture *backend;

        backend = ALCpulseCapture_New(sizeof(*backend));
        if(!backend) return NULL;
        memset(backend, 0, sizeof(*backend));

        ALCpulseCapture_Construct(backend, device);

        return STATIC_CAST(ALCbackend, backend);
    }

    return NULL;
}


#else /* PA_API_VERSION == 12 */

#warning "Unsupported API version, backend will be unavailable!"

typedef struct ALCpulseBackendFactory {
    DERIVE_FROM_TYPE(ALCbackendFactory);
} ALCpulseBackendFactory;
#define ALCPULSEBACKENDFACTORY_INITIALIZER { { GET_VTABLE2(ALCpulseBackendFactory, ALCbackendFactory) } }

static ALCboolean ALCpulseBackendFactory_init(ALCpulseBackendFactory* UNUSED(self))
{
    return ALC_FALSE;
}

static void ALCpulseBackendFactory_deinit(ALCpulseBackendFactory* UNUSED(self))
{
}

static ALCboolean ALCpulseBackendFactory_querySupport(ALCpulseBackendFactory* UNUSED(self), ALCbackend_Type UNUSED(type))
{
    return ALC_FALSE;
}

static void ALCpulseBackendFactory_probe(ALCpulseBackendFactory* UNUSED(self), enum DevProbe UNUSED(type))
{
}

static ALCbackend* ALCpulseBackendFactory_createBackend(ALCpulseBackendFactory* UNUSED(self), ALCdevice* UNUSED(device), ALCbackend_Type UNUSED(type))
{
    return NULL;
}

DEFINE_ALCBACKENDFACTORY_VTABLE(ALCpulseBackendFactory);

#endif /* PA_API_VERSION == 12 */

ALCbackendFactory *ALCpulseBackendFactory_getFactory(void)
{
    static ALCpulseBackendFactory factory = ALCPULSEBACKENDFACTORY_INITIALIZER;
    return STATIC_CAST(ALCbackendFactory, &factory);
}
