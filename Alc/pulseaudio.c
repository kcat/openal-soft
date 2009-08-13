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
MAKE_FUNC(pa_stream_set_read_callback);
MAKE_FUNC(pa_stream_set_state_callback);
MAKE_FUNC(pa_stream_new);
MAKE_FUNC(pa_stream_disconnect);
MAKE_FUNC(pa_threaded_mainloop_lock);
#undef MAKE_FUNC

typedef struct {
    ALCdevice *device;

    ALCenum format;
    ALCuint samples;
    ALCuint frequency;
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

static char *pulse_device;
static char *pulse_capture_device;

// PulseAudio Event Callbacks {{{
static void stream_state_callback(pa_stream *stream, void *pdata) //{{{
{
    pulse_data *data = pdata;

    switch(ppa_stream_get_state(stream))
	{
		case PA_STREAM_READY:
            AL_PRINT("%s: %s ready!\n", data->context_name, data->stream_name);
			break;

		case PA_STREAM_FAILED:
            AL_PRINT("%s: %s: Connection failed: %s\n", data->context_name,
                     data->stream_name, ppa_strerror(ppa_context_errno(data->context)));
			break;

		case PA_STREAM_TERMINATED:
            AL_PRINT("%s: %s terminated!\n", data->context_name, data->stream_name);
			break;

		default:
			break;
	}

    ppa_threaded_mainloop_signal(data->loop, 1);
} //}}}

static void context_state_callback(pa_context *context, void *pdata) //{{{
{
    pulse_data *data = pdata;

    switch(ppa_context_get_state(context))
	{
		case PA_CONTEXT_READY:
            AL_PRINT("%s ready!\n", data->context_name);
			break;

		case PA_CONTEXT_FAILED:
            AL_PRINT("%s: Connection failed: %s\n", data->context_name,
                     ppa_strerror(ppa_context_errno(context)));
            break;

		case PA_CONTEXT_TERMINATED:
            AL_PRINT("%s terminated!\n", data->context_name);
			break;

		default:
			break;
	}

    ppa_threaded_mainloop_signal(data->loop, 1);
} //}}}
//}}}

// PulseAudio I/O Callbacks //{{{
static void stream_write_callback(pa_stream *stream, size_t len, void *pdata) //{{{
{
    ALCdevice *Device = pdata;
    void *buf = ppa_xmalloc0(len);

    SuspendContext(NULL);
    aluMixData(Device->Context, buf, len, Device->Format);
    ProcessContext(NULL);

    ppa_stream_write(stream, buf, len, ppa_xfree, 0, PA_SEEK_RELATIVE);
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

static ALCboolean pulse_open(ALCdevice *device, ALCchar *device_name, ALCenum format, ALCuint samples, ALCuint frequency) //{{{
{
    pulse_data *data = ppa_xmalloc0(sizeof(pulse_data));

    data->device = device;
    data->format = format;
    data->samples = samples;
    data->frequency = frequency;
    data->frame_size = aluBytesFromFormat(format) * aluChannelsFromFormat(format);

    if(ppa_get_binary_name(data->path_name, sizeof(data->path_name)))
        data->context_name = ppa_path_get_filename(data->path_name);
    else
        data->context_name = "OpenAL Soft";

    if(!(data->ring = CreateRingBuffer(data->frame_size, data->samples)))
    {
        ppa_xfree(data);
        return ALC_FALSE;
    }

    device->ExtraData = data;
    device->szDeviceName = device_name;

    data->attr.minreq = -1;
    data->attr.prebuf = -1;
    data->attr.maxlength = -1;

    if(device->IsCaptureDevice)
    {
        data->attr.tlength = -1;
        data->attr.fragsize = data->frame_size * data->samples / 2;
        data->stream_name = "Capture Stream";
    }
    else
    {
        data->attr.tlength = data->frame_size * (device->BufferSize&~3);
        data->attr.fragsize = -1;
        data->stream_name = "Playback Stream";
    }

    data->spec.rate = data->frequency;
    data->spec.channels = aluChannelsFromFormat(data->format);

    switch(aluBytesFromFormat(data->format))
    {
        case 1:
            data->spec.format = PA_SAMPLE_U8;
            break;
        case 2:
            data->spec.format = PA_SAMPLE_S16NE;
            break;
        default:
            AL_PRINT("Unknown format: %x\n", data->format);
			goto out2;
    }

    if(ppa_sample_spec_valid(&data->spec) == 0)
    {
        AL_PRINT("Invalid sample format\n");
		goto out2;
    }

    if(!(data->loop = ppa_threaded_mainloop_new()))
    {
        AL_PRINT("pa_threaded_mainloop_new() failed!\n");
		goto out2;
    }

    if(ppa_threaded_mainloop_start(data->loop) < 0)
    {
        AL_PRINT("pa_threaded_mainloop_start() failed\n");
		goto out3;
    }

    ppa_threaded_mainloop_lock(data->loop);

    data->context = ppa_context_new(ppa_threaded_mainloop_get_api(data->loop), data->context_name);
    if(!data->context)
    {
        AL_PRINT("pa_context_new() failed: %s\n",
                 ppa_strerror(ppa_context_errno(data->context)));

        ppa_threaded_mainloop_unlock(data->loop);
		goto out3;
    }

    ppa_context_set_state_callback(data->context, context_state_callback, data);

    if(ppa_context_connect(data->context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0)
    {
        AL_PRINT("Context did not connect: %s\n",
                 ppa_strerror(ppa_context_errno(data->context)));

        ppa_context_unref(data->context);
        ppa_threaded_mainloop_unlock(data->loop);

        data->context = NULL;
		goto out3;
    }

    while(ppa_context_get_state(data->context) != PA_CONTEXT_READY)
    {
        if(!PA_CONTEXT_IS_GOOD(ppa_context_get_state(data->context)))
        {
            ppa_context_unref(data->context);
            ppa_threaded_mainloop_unlock(data->loop);

            data->context = NULL;
			goto out3;
        }

        ppa_threaded_mainloop_wait(data->loop);
        ppa_threaded_mainloop_accept(data->loop);
    }

    data->stream = ppa_stream_new(data->context, data->stream_name, &data->spec, NULL);
    if(!data->stream)
    {
        AL_PRINT("pa_stream_new() failed: %s\n",
                 ppa_strerror(ppa_context_errno(data->context)));

        ppa_threaded_mainloop_unlock(data->loop);
		goto out4;
    }

    ppa_stream_set_state_callback(data->stream, stream_state_callback, data);

    if(device->IsCaptureDevice)
    {
        if(ppa_stream_connect_record(data->stream, NULL, &data->attr, PA_STREAM_ADJUST_LATENCY) < 0)
        {
            AL_PRINT("Stream did not connect: %s\n",
                     ppa_strerror(ppa_context_errno(data->context)));

            ppa_stream_unref(data->stream);
            ppa_threaded_mainloop_unlock(data->loop);

            data->stream = NULL;
			goto out4;
        }
    }
    else
    {
        ppa_stream_set_write_callback(data->stream, stream_write_callback, device);

        if(ppa_stream_connect_playback(data->stream, NULL, &data->attr, PA_STREAM_ADJUST_LATENCY, NULL, NULL) < 0)
        {
            AL_PRINT("Stream did not connect: %s\n",
                     ppa_strerror(ppa_context_errno(data->context)));

            ppa_stream_unref(data->stream);
            ppa_threaded_mainloop_unlock(data->loop);

            data->stream = NULL;
			goto out4;
        }
    }

    while(ppa_stream_get_state(data->stream) != PA_STREAM_READY)
    {
        if(!PA_STREAM_IS_GOOD(ppa_stream_get_state(data->stream)))
        {
            ppa_stream_unref(data->stream);
            ppa_threaded_mainloop_unlock(data->loop);

            data->stream = NULL;
			goto out4;
        }

        ppa_threaded_mainloop_wait(data->loop);
        ppa_threaded_mainloop_accept(data->loop);
    }

    device->UpdateSize = device->BufferSize/4;
    ppa_threaded_mainloop_unlock(data->loop);

    return ALC_TRUE;

out4:
    ppa_threaded_mainloop_lock(data->loop);

    ppa_context_disconnect(data->context);
    ppa_context_unref(data->context);

    ppa_threaded_mainloop_unlock(data->loop);
out3:
    ppa_threaded_mainloop_stop(data->loop);
    ppa_threaded_mainloop_free(data->loop);
out2:
    device->ExtraData = NULL;
    device->szDeviceName = NULL;
    DestroyRingBuffer(data->ring);

    ppa_xfree(data);
    return ALC_FALSE;
} //}}}

static void pulse_close(ALCdevice *device) //{{{
{
    pulse_data *data = device->ExtraData;

    ppa_threaded_mainloop_lock(data->loop);

    ppa_stream_disconnect(data->stream);
    ppa_stream_unref(data->stream);

    ppa_context_disconnect(data->context);
    ppa_context_unref(data->context);

    ppa_threaded_mainloop_unlock(data->loop);

    ppa_threaded_mainloop_stop(data->loop);
    ppa_threaded_mainloop_free(data->loop);

    device->ExtraData = NULL;
    device->szDeviceName = NULL;
    DestroyRingBuffer(data->ring);

    ppa_xfree(data);
} //}}}
//}}}

// OpenAL {{{
static ALCboolean pulse_open_playback(ALCdevice *device, const ALCchar *device_name) //{{{
{
    if(!pa_handle)
        return ALC_FALSE;

    if(device_name)
    {
        if(strcmp(device_name, pulse_device) != 0)
            return ALC_FALSE;
    }

    return pulse_open(device, pulse_device, device->Format, 0, device->Frequency);
} //}}}

static void pulse_close_playback(ALCdevice *device) //{{{
{
    pulse_close(device);
} //}}}

static ALCboolean pulse_start_context(ALCdevice *device, ALCcontext *context) //{{{
{
    return ALC_TRUE;
    (void)device;
    (void)context;
} //}}}

static void pulse_stop_context(ALCdevice *device, ALCcontext *context) //{{{
{
    (void)device;
    (void)context;
} //}}}


static ALCboolean pulse_open_capture(ALCdevice *device, const ALCchar *device_name, ALCuint frequency, ALCenum format, ALCsizei samples) //{{{
{
    if(!pa_handle)
        return ALC_FALSE;

    if(device_name)
    {
        if(strcmp(device_name, pulse_capture_device) != 0)
            return ALC_FALSE;
    }

    return pulse_open(device, pulse_capture_device, format, samples, frequency);
} //}}}

static void pulse_close_capture(ALCdevice *device) //{{{
{
    pulse_close(device);
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
        SetALCError(ALC_INVALID_VALUE);
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
    pulse_start_context,
    pulse_stop_context,
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

#if defined(__APPLE__) && defined(__MACH__)
    pa_handle = dlopen("libpulse.0.dylib", RTLD_NOW);
#else
    pa_handle = dlopen("libpulse.so.0", RTLD_NOW);
#endif
#define LOAD_FUNC(x) do { \
    p##x = dlsym(pa_handle, #x); \
    if(!(p##x)) { \
        AL_PRINT("Could not load %s from libpulse\n", #x); \
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
LOAD_FUNC(pa_stream_set_read_callback);
LOAD_FUNC(pa_stream_set_state_callback);
LOAD_FUNC(pa_stream_new);
LOAD_FUNC(pa_stream_disconnect);
LOAD_FUNC(pa_threaded_mainloop_lock);

#undef LOAD_FUNC

    pulse_device = AppendDeviceList("PulseAudio Software");
    AppendAllDeviceList(pulse_device);

    pulse_capture_device = AppendCaptureDeviceList("PulseAudio Capture");
} //}}}
//}}}
