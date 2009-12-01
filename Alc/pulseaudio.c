/**
 * OpenAL cross platform audio library
 * Copyright (C) 2009 by Konstantinos Natsakis <konstantinos.natsakis@gmail.com>
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

#include "alMain.h"
#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

#include <pulse/pulseaudio.h>

#if PA_API_VERSION == 11
#define PA_STREAM_ADJUST_LATENCY 0x2000U
static inline int PA_STREAM_IS_GOOD(pa_stream_state_t x)
{
    return (x == PA_STREAM_CREATING || x == PA_STREAM_READY);
}
static inline int PA_CONTEXT_IS_GOOD(pa_context_state_t x)
{
    return (x == PA_CONTEXT_CONNECTING || x == PA_CONTEXT_AUTHORIZING ||
            x == PA_CONTEXT_SETTING_NAME || x == PA_CONTEXT_READY);
}
#define PA_STREAM_IS_GOOD PA_STREAM_IS_GOOD
#define PA_CONTEXT_IS_GOOD PA_CONTEXT_IS_GOOD
#elif PA_API_VERSION != 12
#error Invalid PulseAudio API version
#endif

static void *pa_handle;
#define MAKE_FUNC(x) static typeof(x) * p##x
MAKE_FUNC(pa_context_unref);
MAKE_FUNC(pa_sample_spec_valid);
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
MAKE_FUNC(pa_path_get_filename);
MAKE_FUNC(pa_get_binary_name);
MAKE_FUNC(pa_threaded_mainloop_free);
MAKE_FUNC(pa_context_errno);
MAKE_FUNC(pa_xmalloc0);
MAKE_FUNC(pa_stream_unref);
MAKE_FUNC(pa_threaded_mainloop_accept);
MAKE_FUNC(pa_stream_set_write_callback);
MAKE_FUNC(pa_threaded_mainloop_new);
MAKE_FUNC(pa_context_connect);
MAKE_FUNC(pa_stream_get_buffer_attr);
MAKE_FUNC(pa_stream_set_buffer_attr_callback);
MAKE_FUNC(pa_stream_set_read_callback);
MAKE_FUNC(pa_stream_set_state_callback);
MAKE_FUNC(pa_stream_new);
MAKE_FUNC(pa_stream_disconnect);
MAKE_FUNC(pa_threaded_mainloop_lock);
MAKE_FUNC(pa_channel_map_init_auto);
MAKE_FUNC(pa_channel_map_parse);
#undef MAKE_FUNC

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    ALCuint samples;
    ALCuint frame_size;

    RingBuffer *ring;

    pa_buffer_attr attr;
    pa_sample_spec spec;

    char path_name[PATH_MAX];
    const char *context_name;
    const char *stream_name;

    pa_threaded_mainloop *loop;

    pa_stream *stream;
    pa_context *context;
} pulse_data;

static const ALCchar pulse_device[] = "PulseAudio Software";
static const ALCchar pulse_capture_device[] = "PulseAudio Capture";
static volatile ALuint load_count;


void pulse_load(void) //{{{
{
    if(load_count == 0)
    {
#ifdef _WIN32
        pa_handle = LoadLibrary("libpulse-0.dll");
#define LOAD_FUNC(x) do { \
    p##x = GetProcAddress(pa_handle, #x); \
    if(!(p##x)) { \
        AL_PRINT("Could not load %s from libpulse-0.dll\n", #x); \
        FreeLibrary(pa_handle); \
        pa_handle = NULL; \
        return; \
    } \
} while(0)

#elif defined (HAVE_DLFCN_H)

        const char *err;
#if defined(__APPLE__) && defined(__MACH__)
        pa_handle = dlopen("libpulse.0.dylib", RTLD_NOW);
#else
        pa_handle = dlopen("libpulse.so.0", RTLD_NOW);
#endif
        dlerror();

#define LOAD_FUNC(x) do { \
    p##x = dlsym(pa_handle, #x); \
    if((err=dlerror()) != NULL) { \
        AL_PRINT("Could not load %s from libpulse: %s\n", #x, err); \
        dlclose(pa_handle); \
        pa_handle = NULL; \
        return; \
    } \
} while(0)

#else

        pa_handle = (void*)0xDEADBEEF;
#define LOAD_FUNC(x) p##x = (x)

#endif
        if(!pa_handle)
            return;

LOAD_FUNC(pa_context_unref);
LOAD_FUNC(pa_sample_spec_valid);
LOAD_FUNC(pa_stream_drop);
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
LOAD_FUNC(pa_path_get_filename);
LOAD_FUNC(pa_get_binary_name);
LOAD_FUNC(pa_threaded_mainloop_free);
LOAD_FUNC(pa_context_errno);
LOAD_FUNC(pa_xmalloc0);
LOAD_FUNC(pa_stream_unref);
LOAD_FUNC(pa_threaded_mainloop_accept);
LOAD_FUNC(pa_stream_set_write_callback);
LOAD_FUNC(pa_threaded_mainloop_new);
LOAD_FUNC(pa_context_connect);
LOAD_FUNC(pa_stream_get_buffer_attr);
LOAD_FUNC(pa_stream_set_buffer_attr_callback);
LOAD_FUNC(pa_stream_set_read_callback);
LOAD_FUNC(pa_stream_set_state_callback);
LOAD_FUNC(pa_stream_new);
LOAD_FUNC(pa_stream_disconnect);
LOAD_FUNC(pa_threaded_mainloop_lock);
LOAD_FUNC(pa_channel_map_init_auto);
LOAD_FUNC(pa_channel_map_parse);

#undef LOAD_FUNC
    }
    ++load_count;
} //}}}

void pulse_unload(void) //{{{
{
    if(load_count == 0 || --load_count > 0)
        return;

#ifdef _WIN32
    FreeLibrary(pa_handle);
#elif defined (HAVE_DLFCN_H)
    dlclose(pa_handle);
#endif
    pa_handle = NULL;
} //}}}


// PulseAudio Event Callbacks //{{{
static void context_state_callback(pa_context *context, void *pdata) //{{{
{
    ALCdevice *Device = pdata;
    pulse_data *data = Device->ExtraData;
    pa_context_state_t state;

    state = ppa_context_get_state(context);
    if(state == PA_CONTEXT_READY)
    {
        if(ppa_threaded_mainloop_in_thread(data->loop))
            ppa_threaded_mainloop_signal(data->loop, 1);
        else
            ppa_threaded_mainloop_signal(data->loop, 0);
    }
    else if(!PA_CONTEXT_IS_GOOD(state))
        ppa_threaded_mainloop_signal(data->loop, 0);
}//}}}

static void stream_state_callback(pa_stream *stream, void *pdata) //{{{
{
    ALCdevice *Device = pdata;
    pulse_data *data = Device->ExtraData;
    pa_stream_state_t state;

    state = ppa_stream_get_state(stream);
    if(state == PA_STREAM_READY)
    {
        if(ppa_threaded_mainloop_in_thread(data->loop))
            ppa_threaded_mainloop_signal(data->loop, 1);
        else
            ppa_threaded_mainloop_signal(data->loop, 0);
    }
    else if(!PA_STREAM_IS_GOOD(state))
        ppa_threaded_mainloop_signal(data->loop, 0);
}//}}}

static void stream_buffer_attr_callback(pa_stream *stream, void *pdata) //{{{
{
    ALCdevice *Device = pdata;
    pulse_data *data = Device->ExtraData;

    SuspendContext(NULL);

    data->attr = *(ppa_stream_get_buffer_attr(stream));
    if(data->attr.tlength < data->attr.minreq*2)
        AL_PRINT("new tlength (%d) is smaller than two periods (%d x 2)!\n",
                 data->attr.tlength, data->attr.minreq);
    Device->UpdateSize = data->attr.minreq / data->frame_size;
    Device->NumUpdates = data->attr.tlength/data->attr.minreq;

    ProcessContext(NULL);
}//}}}

static void context_state_callback2(pa_context *context, void *pdata) //{{{
{
    ALCdevice *Device = pdata;

    if(ppa_context_get_state(context) == PA_CONTEXT_FAILED)
    {
        AL_PRINT("Received context failure!\n");
        aluHandleDisconnect(Device);
    }
}//}}}

static void stream_state_callback2(pa_stream *stream, void *pdata) //{{{
{
    ALCdevice *Device = pdata;

    if(ppa_stream_get_state(stream) == PA_STREAM_FAILED)
    {
        AL_PRINT("Received stream failure!\n");
        aluHandleDisconnect(Device);
    }
}//}}}

//}}}

// PulseAudio I/O Callbacks //{{{
static void stream_write_callback(pa_stream *stream, size_t len, void *pdata) //{{{
{
    ALCdevice *Device = pdata;
    pulse_data *data = Device->ExtraData;

    len -= len%data->attr.minreq;
    if(len > 0)
    {
        void *buf = ppa_xmalloc0(len);
        aluMixData(Device, buf, len/data->frame_size);
        ppa_stream_write(stream, buf, len, ppa_xfree, 0, PA_SEEK_RELATIVE);
    }
} //}}}

static void stream_read_callback(pa_stream *stream, size_t length, void *pdata) //{{{
{
    ALCdevice *Device = pdata;
    pulse_data *data = Device->ExtraData;
    const void *buf;

    if(ppa_stream_peek(stream, &buf, &length) < 0)
    {
        AL_PRINT("pa_stream_peek() failed: %s\n",
                 ppa_strerror(ppa_context_errno(data->context)));
        return;
    }

    assert(buf);
    assert(length);

    length /= data->frame_size;

    if(data->samples < length)
        AL_PRINT("stream_read_callback: buffer overflow!\n");

    WriteRingBuffer(data->ring, buf, (length<data->samples) ? length : data->samples);

    ppa_stream_drop(stream);
} //}}}
//}}}

static ALCboolean pulse_open(ALCdevice *device, const ALCchar *device_name) //{{{
{
    pulse_data *data = ppa_xmalloc0(sizeof(pulse_data));
    pa_context_state_t state;

    if(ppa_get_binary_name(data->path_name, sizeof(data->path_name)))
        data->context_name = ppa_path_get_filename(data->path_name);
    else
        data->context_name = "OpenAL Soft";

    if(!(data->loop = ppa_threaded_mainloop_new()))
    {
        AL_PRINT("pa_threaded_mainloop_new() failed!\n");
        goto out;
    }

    if(ppa_threaded_mainloop_start(data->loop) < 0)
    {
        AL_PRINT("pa_threaded_mainloop_start() failed\n");
        goto out;
    }

    ppa_threaded_mainloop_lock(data->loop);
    device->ExtraData = data;

    data->context = ppa_context_new(ppa_threaded_mainloop_get_api(data->loop), data->context_name);
    if(!data->context)
    {
        AL_PRINT("pa_context_new() failed: %s\n",
                 ppa_strerror(ppa_context_errno(data->context)));

        ppa_threaded_mainloop_unlock(data->loop);
        goto out;
    }

    ppa_context_set_state_callback(data->context, context_state_callback, device);

    if(ppa_context_connect(data->context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0)
    {
        AL_PRINT("Context did not connect: %s\n",
                 ppa_strerror(ppa_context_errno(data->context)));

        ppa_context_unref(data->context);
        data->context = NULL;

        ppa_threaded_mainloop_unlock(data->loop);
        goto out;
    }

    while((state=ppa_context_get_state(data->context)) != PA_CONTEXT_READY)
    {
        if(!PA_CONTEXT_IS_GOOD(state))
        {
            AL_PRINT("Context did not get ready: %s\n",
                     ppa_strerror(ppa_context_errno(data->context)));

            ppa_context_unref(data->context);
            data->context = NULL;

            ppa_threaded_mainloop_unlock(data->loop);
            goto out;
        }

        ppa_threaded_mainloop_wait(data->loop);
    }
    ppa_threaded_mainloop_accept(data->loop);
    ppa_context_set_state_callback(data->context, context_state_callback2, device);

    device->szDeviceName = strdup(device_name);

    ppa_threaded_mainloop_unlock(data->loop);
    return ALC_TRUE;

out:
    if(data->loop)
    {
        ppa_threaded_mainloop_stop(data->loop);
        ppa_threaded_mainloop_free(data->loop);
    }

    device->ExtraData = NULL;
    ppa_xfree(data);
    return ALC_FALSE;
} //}}}

static void pulse_close(ALCdevice *device) //{{{
{
    pulse_data *data = device->ExtraData;

    ppa_threaded_mainloop_lock(data->loop);

    if(data->stream)
    {
        ppa_stream_disconnect(data->stream);
        ppa_stream_unref(data->stream);
    }

    ppa_context_disconnect(data->context);
    ppa_context_unref(data->context);

    ppa_threaded_mainloop_unlock(data->loop);

    ppa_threaded_mainloop_stop(data->loop);
    ppa_threaded_mainloop_free(data->loop);

    DestroyRingBuffer(data->ring);

    device->ExtraData = NULL;
    ppa_xfree(data);
} //}}}
//}}}

// OpenAL {{{
static ALCboolean pulse_open_playback(ALCdevice *device, const ALCchar *device_name) //{{{
{
    if(!device_name)
        device_name = pulse_device;
    else if(strcmp(device_name, pulse_device) != 0)
        return ALC_FALSE;

    pulse_load();
    if(!pa_handle)
        return ALC_FALSE;

    if(pulse_open(device, device_name) != ALC_FALSE)
        return ALC_TRUE;

    pulse_unload();
    return ALC_FALSE;
} //}}}

static void pulse_close_playback(ALCdevice *device) //{{{
{
    pulse_close(device);
    pulse_unload();
} //}}}

static ALCboolean pulse_reset_playback(ALCdevice *device) //{{{
{
    pulse_data *data = device->ExtraData;
    pa_stream_state_t state;
    pa_channel_map chanmap;

    ppa_threaded_mainloop_lock(data->loop);

    data->frame_size = aluBytesFromFormat(device->Format) *
                       aluChannelsFromFormat(device->Format);
    data->attr.minreq = data->frame_size * device->UpdateSize;
    data->attr.prebuf = -1;
    data->attr.maxlength = -1;
    data->attr.fragsize = -1;
    data->attr.tlength = data->attr.minreq * device->NumUpdates;
    data->stream_name = "Playback Stream";

    switch(aluBytesFromFormat(device->Format))
    {
        case 1:
            data->spec.format = PA_SAMPLE_U8;
            break;
        case 2:
            data->spec.format = PA_SAMPLE_S16NE;
            break;
        case 4:
            data->spec.format = PA_SAMPLE_FLOAT32NE;
            break;
        default:
            AL_PRINT("Unknown format: 0x%x\n", device->Format);
            ppa_threaded_mainloop_unlock(data->loop);
            return ALC_FALSE;
    }
    data->spec.rate = device->Frequency;
    data->spec.channels = aluChannelsFromFormat(device->Format);

    if(ppa_sample_spec_valid(&data->spec) == 0)
    {
        AL_PRINT("Invalid sample format\n");
        ppa_threaded_mainloop_unlock(data->loop);
        return ALC_FALSE;
    }

#ifdef _WIN32
    if(!ppa_channel_map_init_auto(&chanmap, data->spec.channels, PA_CHANNEL_MAP_WAVEEX))
    {
        AL_PRINT("Couldn't build map for channel count (%d)!", data->spec.channels);
        ppa_threaded_mainloop_unlock(data->loop);
        return ALC_FALSE;
    }
#else
    switch(data->spec.channels)
    {
    case 1:
        ppa_channel_map_parse(&chanmap, "mono");
        break;
    case 2:
        ppa_channel_map_parse(&chanmap, "front-left,front-right");
        break;
    case 4:
        ppa_channel_map_parse(&chanmap, "front-left,front-right,rear-left,rear-right");
        break;
    case 6:
        ppa_channel_map_parse(&chanmap, "front-left,front-right,rear-left,rear-right,front-center,lfe");
        break;
    case 7:
        ppa_channel_map_parse(&chanmap, "front-left,front-right,front-center,lfe,rear-center,side-left,side-right");
        break;
    case 8:
        ppa_channel_map_parse(&chanmap, "front-left,front-right,rear-left,rear-right,front-center,lfe,side-left,side-right");
        break;
    default:
        AL_PRINT("Got unhandled channel count (%d)!", data->spec.channels);
        ppa_threaded_mainloop_unlock(data->loop);
        return ALC_FALSE;
    }
#endif

    data->stream = ppa_stream_new(data->context, data->stream_name, &data->spec, &chanmap);
    if(!data->stream)
    {
        AL_PRINT("pa_stream_new() failed: %s\n",
                 ppa_strerror(ppa_context_errno(data->context)));

        ppa_threaded_mainloop_unlock(data->loop);
        return ALC_FALSE;
    }

    ppa_stream_set_state_callback(data->stream, stream_state_callback, device);
    ppa_stream_set_write_callback(data->stream, stream_write_callback, device);

    if(ppa_stream_connect_playback(data->stream, NULL, &data->attr, PA_STREAM_ADJUST_LATENCY, NULL, NULL) < 0)
    {
        AL_PRINT("Stream did not connect: %s\n",
                 ppa_strerror(ppa_context_errno(data->context)));

        ppa_stream_unref(data->stream);
        data->stream = NULL;

        ppa_threaded_mainloop_unlock(data->loop);
        return ALC_FALSE;
    }

    while((state=ppa_stream_get_state(data->stream)) != PA_STREAM_READY)
    {
        if(!PA_STREAM_IS_GOOD(state))
        {
            AL_PRINT("Stream did not get ready: %s\n",
                     ppa_strerror(ppa_context_errno(data->context)));

            ppa_stream_unref(data->stream);
            data->stream = NULL;

            ppa_threaded_mainloop_unlock(data->loop);
            return ALC_FALSE;
        }

        ppa_threaded_mainloop_wait(data->loop);
    }
    ppa_threaded_mainloop_accept(data->loop);
    ppa_stream_set_state_callback(data->stream, stream_state_callback2, device);

    stream_buffer_attr_callback(data->stream, device);
    ppa_stream_set_buffer_attr_callback(data->stream, stream_buffer_attr_callback, device);

    ppa_threaded_mainloop_unlock(data->loop);
    return ALC_TRUE;
} //}}}

static void pulse_stop_playback(ALCdevice *device) //{{{
{
    pulse_data *data = device->ExtraData;

    if(!data->stream)
        return;

    ppa_threaded_mainloop_lock(data->loop);

    ppa_stream_disconnect(data->stream);
    ppa_stream_unref(data->stream);
    data->stream = NULL;

    ppa_threaded_mainloop_unlock(data->loop);
} //}}}


static ALCboolean pulse_open_capture(ALCdevice *device, const ALCchar *device_name) //{{{
{
    pulse_data *data;
    pa_stream_state_t state;

    if(!device_name)
        device_name = pulse_capture_device;
    else if(strcmp(device_name, pulse_capture_device) != 0)
        return ALC_FALSE;

    pulse_load();
    if(!pa_handle)
        return ALC_FALSE;

    if(pulse_open(device, device_name) == ALC_FALSE)
    {
        pulse_unload();
        return ALC_FALSE;
    }

    data = device->ExtraData;
    ppa_threaded_mainloop_lock(data->loop);

    data->samples = device->UpdateSize * device->NumUpdates;
    data->frame_size = aluBytesFromFormat(device->Format) *
                       aluChannelsFromFormat(device->Format);

    if(!(data->ring = CreateRingBuffer(data->frame_size, data->samples)))
    {
        ppa_threaded_mainloop_unlock(data->loop);
        pulse_close(device);
        pulse_unload();
        return ALC_FALSE;
    }

    data->attr.minreq = -1;
    data->attr.prebuf = -1;
    data->attr.maxlength = -1;
    data->attr.tlength = -1;
    data->attr.fragsize = data->frame_size * data->samples / 2;
    data->stream_name = "Capture Stream";

    data->spec.rate = device->Frequency;
    data->spec.channels = aluChannelsFromFormat(device->Format);

    switch(aluBytesFromFormat(device->Format))
    {
        case 1:
            data->spec.format = PA_SAMPLE_U8;
            break;
        case 2:
            data->spec.format = PA_SAMPLE_S16NE;
            break;
        case 4:
            data->spec.format = PA_SAMPLE_FLOAT32NE;
            break;
        default:
            AL_PRINT("Unknown format: 0x%x\n", device->Format);
            ppa_threaded_mainloop_unlock(data->loop);
            pulse_close(device);
            pulse_unload();
            return ALC_FALSE;
    }

    if(ppa_sample_spec_valid(&data->spec) == 0)
    {
        AL_PRINT("Invalid sample format\n");
        ppa_threaded_mainloop_unlock(data->loop);
        pulse_close(device);
        pulse_unload();
        return ALC_FALSE;
    }

    data->stream = ppa_stream_new(data->context, data->stream_name, &data->spec, NULL);
    if(!data->stream)
    {
        AL_PRINT("pa_stream_new() failed: %s\n",
                 ppa_strerror(ppa_context_errno(data->context)));

        ppa_threaded_mainloop_unlock(data->loop);
        pulse_close(device);
        pulse_unload();
        return ALC_FALSE;
    }

    ppa_stream_set_state_callback(data->stream, stream_state_callback, device);

    if(ppa_stream_connect_record(data->stream, NULL, &data->attr, PA_STREAM_ADJUST_LATENCY) < 0)
    {
        AL_PRINT("Stream did not connect: %s\n",
                 ppa_strerror(ppa_context_errno(data->context)));

        ppa_stream_unref(data->stream);
        ppa_threaded_mainloop_unlock(data->loop);

        data->stream = NULL;
        pulse_close(device);
        pulse_unload();
        return ALC_FALSE;
    }

    while((state=ppa_stream_get_state(data->stream)) != PA_STREAM_READY)
    {
        if(!PA_STREAM_IS_GOOD(state))
        {
            AL_PRINT("Stream did not get ready: %s\n",
                     ppa_strerror(ppa_context_errno(data->context)));

            ppa_stream_unref(data->stream);
            ppa_threaded_mainloop_unlock(data->loop);

            data->stream = NULL;
            pulse_close(device);
            pulse_unload();
            return ALC_FALSE;
        }

        ppa_threaded_mainloop_wait(data->loop);
    }
    ppa_threaded_mainloop_accept(data->loop);
    ppa_stream_set_state_callback(data->stream, stream_state_callback2, device);

    ppa_threaded_mainloop_unlock(data->loop);
    return ALC_TRUE;
} //}}}

static void pulse_close_capture(ALCdevice *device) //{{{
{
    pulse_close(device);
    pulse_unload();
} //}}}

static void pulse_start_capture(ALCdevice *device) //{{{
{
    pulse_data *data = device->ExtraData;

    ppa_threaded_mainloop_lock(data->loop);
    ppa_stream_set_read_callback(data->stream, stream_read_callback, device);
    ppa_threaded_mainloop_unlock(data->loop);
} //}}}

static void pulse_stop_capture(ALCdevice *device) //{{{
{
    pulse_data *data = device->ExtraData;

    ppa_threaded_mainloop_lock(data->loop);
    ppa_stream_set_read_callback(data->stream, NULL, NULL);
    ppa_threaded_mainloop_unlock(data->loop);
} //}}}

static void pulse_capture_samples(ALCdevice *device, ALCvoid *buffer, ALCuint samples) //{{{
{
    pulse_data *data = device->ExtraData;
    ALCuint available = RingBufferSize(data->ring);

    if(available < samples)
        alcSetError(ALC_INVALID_VALUE);
    else
        ReadRingBuffer(data->ring, buffer, samples);
} //}}}

static ALCuint pulse_available_samples(ALCdevice *device) //{{{
{
    pulse_data *data = device->ExtraData;
    return RingBufferSize(data->ring);
} //}}}

BackendFuncs pulse_funcs = { //{{{
    pulse_open_playback,
    pulse_close_playback,
    pulse_reset_playback,
    pulse_stop_playback,
    pulse_open_capture,
    pulse_close_capture,
    pulse_start_capture,
    pulse_stop_capture,
    pulse_capture_samples,
    pulse_available_samples
}; //}}}

void alc_pulse_init(BackendFuncs *func_list) //{{{
{
    *func_list = pulse_funcs;
} //}}}

void alc_pulse_deinit(void) //{{{
{
} //}}}

void alc_pulse_probe(int type) //{{{
{
    pulse_load();
    if(!pa_handle) return;

    if(type == DEVICE_PROBE)
        AppendDeviceList(pulse_device);
    else if(type == ALL_DEVICE_PROBE)
        AppendAllDeviceList(pulse_device);
    else if(type == CAPTURE_DEVICE_PROBE)
        AppendCaptureDeviceList(pulse_capture_device);

    pulse_unload();
} //}}}
//}}}
