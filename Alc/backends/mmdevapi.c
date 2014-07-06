/**
 * OpenAL cross platform audio library
 * Copyright (C) 2011 by authors.
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

#define COBJMACROS
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <cguid.h>
#include <devpropdef.h>
#include <mmreg.h>
#include <propsys.h>
#include <propkey.h>
#include <devpkey.h>
#ifndef _WAVEFORMATEXTENSIBLE_
#include <ks.h>
#include <ksmedia.h>
#endif

#include "alMain.h"
#include "alu.h"
#include "threads.h"
#include "compat.h"
#include "alstring.h"

#include "backends/base.h"


DEFINE_GUID(KSDATAFORMAT_SUBTYPE_PCM, 0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);

DEFINE_DEVPROPKEY(DEVPKEY_Device_FriendlyName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80,0x20, 0x67,0xd1,0x46,0xa8,0x50,0xe0, 14);

#define MONO SPEAKER_FRONT_CENTER
#define STEREO (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT)
#define QUAD (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT)
#define X5DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT)
#define X5DOT1SIDE (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)
#define X6DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_CENTER|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)
#define X7DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)


typedef struct {
    al_string name;
    WCHAR *devid;
} DevMap;
TYPEDEF_VECTOR(DevMap, vector_DevMap)

static void clear_devlist(vector_DevMap *list)
{
    DevMap *iter, *end;

    iter = VECTOR_ITER_BEGIN(*list);
    end = VECTOR_ITER_END(*list);
    for(;iter != end;iter++)
    {
        AL_STRING_DEINIT(iter->name);
        free(iter->devid);
    }
    VECTOR_RESIZE(*list, 0);
}

static vector_DevMap PlaybackDevices;
static vector_DevMap CaptureDevices;


static HANDLE ThreadHdl;
static DWORD ThreadID;

typedef struct {
    HANDLE FinishedEvt;
    HRESULT result;
} ThreadRequest;

#define WM_USER_First       (WM_USER+0)
#define WM_USER_OpenDevice  (WM_USER+0)
#define WM_USER_ResetDevice (WM_USER+1)
#define WM_USER_StartDevice (WM_USER+2)
#define WM_USER_StopDevice  (WM_USER+3)
#define WM_USER_CloseDevice (WM_USER+4)
#define WM_USER_Enumerate   (WM_USER+5)
#define WM_USER_Last        (WM_USER+5)

static inline void ReturnMsgResponse(ThreadRequest *req, HRESULT res)
{
    req->result = res;
    SetEvent(req->FinishedEvt);
}

static HRESULT WaitForResponse(ThreadRequest *req)
{
    if(WaitForSingleObject(req->FinishedEvt, INFINITE) == WAIT_OBJECT_0)
        return req->result;
    ERR("Message response error: %lu\n", GetLastError());
    return E_FAIL;
}


static void get_device_name(IMMDevice *device, al_string *name)
{
    IPropertyStore *ps;
    PROPVARIANT pvname;
    HRESULT hr;

    hr = IMMDevice_OpenPropertyStore(device, STGM_READ, &ps);
    if(FAILED(hr))
    {
        WARN("OpenPropertyStore failed: 0x%08lx\n", hr);
        return;
    }

    PropVariantInit(&pvname);

    hr = IPropertyStore_GetValue(ps, (const PROPERTYKEY*)&DEVPKEY_Device_FriendlyName, &pvname);
    if(FAILED(hr))
        WARN("GetValue failed: 0x%08lx\n", hr);
    else
        al_string_copy_wcstr(name, pvname.pwszVal);

    PropVariantClear(&pvname);
    IPropertyStore_Release(ps);
}

static void add_device(IMMDevice *device, vector_DevMap *list)
{
    LPWSTR devid;
    HRESULT hr;

    hr = IMMDevice_GetId(device, &devid);
    if(SUCCEEDED(hr))
    {
        DevMap entry;
        AL_STRING_INIT(entry.name);

        entry.devid = strdupW(devid);
        get_device_name(device, &entry.name);

        CoTaskMemFree(devid);

        TRACE("Got device \"%s\", \"%ls\"\n", al_string_get_cstr(entry.name), entry.devid);
        VECTOR_PUSH_BACK(*list, entry);
    }
}

static HRESULT probe_devices(IMMDeviceEnumerator *devenum, EDataFlow flowdir, vector_DevMap *list)
{
    IMMDeviceCollection *coll;
    IMMDevice *defdev = NULL;
    HRESULT hr;
    UINT count;
    UINT i;

    hr = IMMDeviceEnumerator_EnumAudioEndpoints(devenum, flowdir, DEVICE_STATE_ACTIVE, &coll);
    if(FAILED(hr))
    {
        ERR("Failed to enumerate audio endpoints: 0x%08lx\n", hr);
        return hr;
    }

    count = 0;
    hr = IMMDeviceCollection_GetCount(coll, &count);
    if(SUCCEEDED(hr) && count > 0)
    {
        clear_devlist(list);
        if(!VECTOR_RESERVE(*list, count+1))
        {
            IMMDeviceCollection_Release(coll);
            return E_OUTOFMEMORY;
        }

        hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(devenum, flowdir,
                                                         eMultimedia, &defdev);
    }
    if(SUCCEEDED(hr) && defdev != NULL)
        add_device(defdev, list);

    for(i = 0;i < count;++i)
    {
        IMMDevice *device;

        if(FAILED(IMMDeviceCollection_Item(coll, i, &device)))
            continue;

        if(device != defdev)
            add_device(device, list);

        IMMDevice_Release(device);
    }

    if(defdev) IMMDevice_Release(defdev);
    IMMDeviceCollection_Release(coll);

    return S_OK;
}


/* Proxy interface used by the message handler. */
struct ALCmmdevProxyVtable;

typedef struct ALCmmdevProxy {
    const struct ALCmmdevProxyVtable *vtbl;
} ALCmmdevProxy;

struct ALCmmdevProxyVtable {
    HRESULT (*const openProxy)(ALCmmdevProxy*);
    void (*const closeProxy)(ALCmmdevProxy*);

    HRESULT (*const resetProxy)(ALCmmdevProxy*);
    HRESULT (*const startProxy)(ALCmmdevProxy*);
    void  (*const stopProxy)(ALCmmdevProxy*);
};

#define DEFINE_ALCMMDEVPROXY_VTABLE(T)                                        \
DECLARE_THUNK(T, ALCmmdevProxy, HRESULT, openProxy)                           \
DECLARE_THUNK(T, ALCmmdevProxy, void, closeProxy)                             \
DECLARE_THUNK(T, ALCmmdevProxy, HRESULT, resetProxy)                          \
DECLARE_THUNK(T, ALCmmdevProxy, HRESULT, startProxy)                          \
DECLARE_THUNK(T, ALCmmdevProxy, void, stopProxy)                              \
                                                                              \
static const struct ALCmmdevProxyVtable T##_ALCmmdevProxy_vtable = {          \
    T##_ALCmmdevProxy_openProxy,                                              \
    T##_ALCmmdevProxy_closeProxy,                                             \
    T##_ALCmmdevProxy_resetProxy,                                             \
    T##_ALCmmdevProxy_startProxy,                                             \
    T##_ALCmmdevProxy_stopProxy,                                              \
}

static void ALCmmdevProxy_Construct(ALCmmdevProxy* UNUSED(self)) { }
static void ALCmmdevProxy_Destruct(ALCmmdevProxy* UNUSED(self)) { }

static DWORD CALLBACK ALCmmdevProxy_messageHandler(void *ptr)
{
    ThreadRequest *req = ptr;
    IMMDeviceEnumerator *Enumerator;
    ALuint deviceCount = 0;
    ALCmmdevProxy *proxy;
    HRESULT hr, cohr;
    MSG msg;

    TRACE("Starting message thread\n");

    cohr = CoInitialize(NULL);
    if(FAILED(cohr))
    {
        WARN("Failed to initialize COM: 0x%08lx\n", cohr);
        ReturnMsgResponse(req, cohr);
        return 0;
    }

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator, &ptr);
    if(FAILED(hr))
    {
        WARN("Failed to create IMMDeviceEnumerator instance: 0x%08lx\n", hr);
        CoUninitialize();
        ReturnMsgResponse(req, hr);
        return 0;
    }
    Enumerator = ptr;
    IMMDeviceEnumerator_Release(Enumerator);
    Enumerator = NULL;

    CoUninitialize();

    /* HACK: Force Windows to create a message queue for this thread before
     * returning success, otherwise PostThreadMessage may fail if it gets
     * called before GetMessage.
     */
    PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);

    TRACE("Message thread initialization complete\n");
    ReturnMsgResponse(req, S_OK);

    TRACE("Starting message loop\n");
    while(GetMessage(&msg, NULL, WM_USER_First, WM_USER_Last))
    {
        TRACE("Got message %u\n", msg.message);
        switch(msg.message)
        {
        case WM_USER_OpenDevice:
            req = (ThreadRequest*)msg.wParam;
            proxy = (ALCmmdevProxy*)msg.lParam;

            hr = cohr = S_OK;
            if(++deviceCount == 1)
                hr = cohr = CoInitialize(NULL);
            if(SUCCEEDED(hr))
                hr = V0(proxy,openProxy)();
            if(FAILED(hr))
            {
                if(--deviceCount == 0 && SUCCEEDED(cohr))
                    CoUninitialize();
            }

            ReturnMsgResponse(req, hr);
            continue;

        case WM_USER_ResetDevice:
            req = (ThreadRequest*)msg.wParam;
            proxy = (ALCmmdevProxy*)msg.lParam;

            hr = V0(proxy,resetProxy)();
            ReturnMsgResponse(req, hr);
            continue;

        case WM_USER_StartDevice:
            req = (ThreadRequest*)msg.wParam;
            proxy = (ALCmmdevProxy*)msg.lParam;

            hr = V0(proxy,startProxy)();
            ReturnMsgResponse(req, hr);
            continue;

        case WM_USER_StopDevice:
            req = (ThreadRequest*)msg.wParam;
            proxy = (ALCmmdevProxy*)msg.lParam;

            V0(proxy,stopProxy)();
            ReturnMsgResponse(req, S_OK);
            continue;

        case WM_USER_CloseDevice:
            req = (ThreadRequest*)msg.wParam;
            proxy = (ALCmmdevProxy*)msg.lParam;

            V0(proxy,closeProxy)();
            if(--deviceCount == 0)
                CoUninitialize();

            ReturnMsgResponse(req, S_OK);
            continue;

        case WM_USER_Enumerate:
            req = (ThreadRequest*)msg.wParam;

            hr = cohr = S_OK;
            if(++deviceCount == 1)
                hr = cohr = CoInitialize(NULL);
            if(SUCCEEDED(hr))
                hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator, &ptr);
            if(SUCCEEDED(hr))
            {
                Enumerator = ptr;

                if(msg.lParam == ALL_DEVICE_PROBE)
                    hr = probe_devices(Enumerator, eRender, &PlaybackDevices);
                else if(msg.lParam == CAPTURE_DEVICE_PROBE)
                    hr = probe_devices(Enumerator, eCapture, &CaptureDevices);

                IMMDeviceEnumerator_Release(Enumerator);
                Enumerator = NULL;
            }

            if(--deviceCount == 0 && SUCCEEDED(cohr))
                CoUninitialize();

            ReturnMsgResponse(req, hr);
            continue;

        default:
            ERR("Unexpected message: %u\n", msg.message);
            continue;
        }
    }
    TRACE("Message loop finished\n");

    return 0;
}


typedef struct ALCmmdevPlayback {
    DERIVE_FROM_TYPE(ALCbackend);
    DERIVE_FROM_TYPE(ALCmmdevProxy);

    WCHAR *devid;

    IMMDevice *mmdev;
    IAudioClient *client;
    IAudioRenderClient *render;
    HANDLE NotifyEvent;

    HANDLE MsgEvent;

    volatile UINT32 Padding;

    volatile int killNow;
    althrd_t thread;
} ALCmmdevPlayback;

static int ALCmmdevPlayback_mixerProc(void *arg);

static void ALCmmdevPlayback_Construct(ALCmmdevPlayback *self, ALCdevice *device);
static void ALCmmdevPlayback_Destruct(ALCmmdevPlayback *self);
static ALCenum ALCmmdevPlayback_open(ALCmmdevPlayback *self, const ALCchar *name);
static HRESULT ALCmmdevPlayback_openProxy(ALCmmdevPlayback *self);
static void ALCmmdevPlayback_close(ALCmmdevPlayback *self);
static void ALCmmdevPlayback_closeProxy(ALCmmdevPlayback *self);
static ALCboolean ALCmmdevPlayback_reset(ALCmmdevPlayback *self);
static HRESULT ALCmmdevPlayback_resetProxy(ALCmmdevPlayback *self);
static ALCboolean ALCmmdevPlayback_start(ALCmmdevPlayback *self);
static HRESULT ALCmmdevPlayback_startProxy(ALCmmdevPlayback *self);
static void ALCmmdevPlayback_stop(ALCmmdevPlayback *self);
static void ALCmmdevPlayback_stopProxy(ALCmmdevPlayback *self);
static DECLARE_FORWARD2(ALCmmdevPlayback, ALCbackend, ALCenum, captureSamples, ALCvoid*, ALCuint)
static DECLARE_FORWARD(ALCmmdevPlayback, ALCbackend, ALCuint, availableSamples)
static ALint64 ALCmmdevPlayback_getLatency(ALCmmdevPlayback *self);
static DECLARE_FORWARD(ALCmmdevPlayback, ALCbackend, void, lock)
static DECLARE_FORWARD(ALCmmdevPlayback, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(ALCmmdevPlayback)

DEFINE_ALCMMDEVPROXY_VTABLE(ALCmmdevPlayback);
DEFINE_ALCBACKEND_VTABLE(ALCmmdevPlayback);


static void ALCmmdevPlayback_Construct(ALCmmdevPlayback *self, ALCdevice *device)
{
    SET_VTABLE2(ALCmmdevPlayback, ALCbackend, self);
    SET_VTABLE2(ALCmmdevPlayback, ALCmmdevProxy, self);
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    ALCmmdevProxy_Construct(STATIC_CAST(ALCmmdevProxy, self));

    self->devid = NULL;

    self->mmdev = NULL;
    self->client = NULL;
    self->render = NULL;
    self->NotifyEvent = NULL;

    self->MsgEvent = NULL;

    self->Padding = 0;

    self->killNow = 0;
}

static void ALCmmdevPlayback_Destruct(ALCmmdevPlayback *self)
{
    if(self->NotifyEvent != NULL)
        CloseHandle(self->NotifyEvent);
    self->NotifyEvent = NULL;
    if(self->MsgEvent != NULL)
        CloseHandle(self->MsgEvent);
    self->MsgEvent = NULL;

    free(self->devid);
    self->devid = NULL;

    ALCmmdevProxy_Destruct(STATIC_CAST(ALCmmdevProxy, self));
    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
}


FORCE_ALIGN static int ALCmmdevPlayback_mixerProc(void *arg)
{
    ALCmmdevPlayback *self = arg;
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    UINT32 buffer_len, written;
    ALuint update_size, len;
    BYTE *buffer;
    HRESULT hr;

    hr = CoInitialize(NULL);
    if(FAILED(hr))
    {
        ERR("CoInitialize(NULL) failed: 0x%08lx\n", hr);
        ALCdevice_Lock(device);
        aluHandleDisconnect(device);
        ALCdevice_Unlock(device);
        return 1;
    }

    SetRTPriority();
    althrd_setname(althrd_current(), MIXER_THREAD_NAME);

    update_size = device->UpdateSize;
    buffer_len = update_size * device->NumUpdates;
    while(!self->killNow)
    {
        hr = IAudioClient_GetCurrentPadding(self->client, &written);
        if(FAILED(hr))
        {
            ERR("Failed to get padding: 0x%08lx\n", hr);
            ALCdevice_Lock(device);
            aluHandleDisconnect(device);
            ALCdevice_Unlock(device);
            break;
        }
        self->Padding = written;

        len = buffer_len - written;
        if(len < update_size)
        {
            DWORD res;
            res = WaitForSingleObjectEx(self->NotifyEvent, 2000, FALSE);
            if(res != WAIT_OBJECT_0)
                ERR("WaitForSingleObjectEx error: 0x%lx\n", res);
            continue;
        }
        len -= len%update_size;

        hr = IAudioRenderClient_GetBuffer(self->render, len, &buffer);
        if(SUCCEEDED(hr))
        {
            ALCdevice_Lock(device);
            aluMixData(device, buffer, len);
            self->Padding = written + len;
            ALCdevice_Unlock(device);
            hr = IAudioRenderClient_ReleaseBuffer(self->render, len, 0);
        }
        if(FAILED(hr))
        {
            ERR("Failed to buffer data: 0x%08lx\n", hr);
            ALCdevice_Lock(device);
            aluHandleDisconnect(device);
            ALCdevice_Unlock(device);
            break;
        }
    }
    self->Padding = 0;

    CoUninitialize();
    return 0;
}


static ALCboolean MakeExtensible(WAVEFORMATEXTENSIBLE *out, const WAVEFORMATEX *in)
{
    memset(out, 0, sizeof(*out));
    if(in->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        *out = *(const WAVEFORMATEXTENSIBLE*)in;
    else if(in->wFormatTag == WAVE_FORMAT_PCM)
    {
        out->Format = *in;
        out->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        out->Format.cbSize = sizeof(*out) - sizeof(*in);
        if(out->Format.nChannels == 1)
            out->dwChannelMask = MONO;
        else if(out->Format.nChannels == 2)
            out->dwChannelMask = STEREO;
        else
            ERR("Unhandled PCM channel count: %d\n", out->Format.nChannels);
        out->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    }
    else if(in->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        out->Format = *in;
        out->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        out->Format.cbSize = sizeof(*out) - sizeof(*in);
        if(out->Format.nChannels == 1)
            out->dwChannelMask = MONO;
        else if(out->Format.nChannels == 2)
            out->dwChannelMask = STEREO;
        else
            ERR("Unhandled IEEE float channel count: %d\n", out->Format.nChannels);
        out->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }
    else
    {
        ERR("Unhandled format tag: 0x%04x\n", in->wFormatTag);
        return ALC_FALSE;
    }
    return ALC_TRUE;
}


static ALCenum ALCmmdevPlayback_open(ALCmmdevPlayback *self, const ALCchar *deviceName)
{
    HRESULT hr = S_OK;

    self->NotifyEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    self->MsgEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if(self->NotifyEvent == NULL || self->MsgEvent == NULL)
    {
        ERR("Failed to create message events: %lu\n", GetLastError());
        hr = E_FAIL;
    }

    if(SUCCEEDED(hr))
    {
        if(deviceName)
        {
            const DevMap *iter, *end;

            if(VECTOR_SIZE(PlaybackDevices) == 0)
            {
                ThreadRequest req = { self->MsgEvent, 0 };
                if(PostThreadMessage(ThreadID, WM_USER_Enumerate, (WPARAM)&req, ALL_DEVICE_PROBE))
                    (void)WaitForResponse(&req);
            }

            hr = E_FAIL;
            iter = VECTOR_ITER_BEGIN(PlaybackDevices);
            end = VECTOR_ITER_END(PlaybackDevices);
            for(;iter != end;iter++)
            {
                if(al_string_cmp_cstr(iter->name, deviceName) == 0)
                {
                    self->devid = strdupW(iter->devid);
                    hr = S_OK;
                    break;
                }
            }
            if(FAILED(hr))
                WARN("Failed to find device name matching \"%s\"\n", deviceName);
        }
    }

    if(SUCCEEDED(hr))
    {
        ThreadRequest req = { self->MsgEvent, 0 };

        hr = E_FAIL;
        if(PostThreadMessage(ThreadID, WM_USER_OpenDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCmmdevProxy, self)))
            hr = WaitForResponse(&req);
        else
            ERR("Failed to post thread message: %lu\n", GetLastError());
    }

    if(FAILED(hr))
    {
        if(self->NotifyEvent != NULL)
            CloseHandle(self->NotifyEvent);
        self->NotifyEvent = NULL;
        if(self->MsgEvent != NULL)
            CloseHandle(self->MsgEvent);
        self->MsgEvent = NULL;

        free(self->devid);
        self->devid = NULL;

        ERR("Device init failed: 0x%08lx\n", hr);
        return ALC_INVALID_VALUE;
    }

    return ALC_NO_ERROR;
}

static HRESULT ALCmmdevPlayback_openProxy(ALCmmdevPlayback *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    void *ptr;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator, &ptr);
    if(SUCCEEDED(hr))
    {
        IMMDeviceEnumerator *Enumerator = ptr;
        if(!self->devid)
            hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(Enumerator, eRender, eMultimedia, &self->mmdev);
        else
            hr = IMMDeviceEnumerator_GetDevice(Enumerator, self->devid, &self->mmdev);
        IMMDeviceEnumerator_Release(Enumerator);
        Enumerator = NULL;
    }
    if(SUCCEEDED(hr))
        hr = IMMDevice_Activate(self->mmdev, &IID_IAudioClient, CLSCTX_INPROC_SERVER, NULL, &ptr);
    if(SUCCEEDED(hr))
    {
        self->client = ptr;
        get_device_name(self->mmdev, &device->DeviceName);
    }

    if(FAILED(hr))
    {
        if(self->mmdev)
            IMMDevice_Release(self->mmdev);
        self->mmdev = NULL;
    }

    return hr;
}


static void ALCmmdevPlayback_close(ALCmmdevPlayback *self)
{
    ThreadRequest req = { self->MsgEvent, 0 };

    if(PostThreadMessage(ThreadID, WM_USER_CloseDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCmmdevProxy, self)))
        (void)WaitForResponse(&req);

    CloseHandle(self->MsgEvent);
    self->MsgEvent = NULL;

    CloseHandle(self->NotifyEvent);
    self->NotifyEvent = NULL;

    free(self->devid);
    self->devid = NULL;
}

static void ALCmmdevPlayback_closeProxy(ALCmmdevPlayback *self)
{
    if(self->client)
        IAudioClient_Release(self->client);
    self->client = NULL;

    if(self->mmdev)
        IMMDevice_Release(self->mmdev);
    self->mmdev = NULL;
}


static ALCboolean ALCmmdevPlayback_reset(ALCmmdevPlayback *self)
{
    ThreadRequest req = { self->MsgEvent, 0 };
    HRESULT hr = E_FAIL;

    if(PostThreadMessage(ThreadID, WM_USER_ResetDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCmmdevProxy, self)))
        hr = WaitForResponse(&req);

    return SUCCEEDED(hr) ? ALC_TRUE : ALC_FALSE;
}

static HRESULT ALCmmdevPlayback_resetProxy(ALCmmdevPlayback *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    WAVEFORMATEXTENSIBLE OutputType;
    WAVEFORMATEX *wfx = NULL;
    REFERENCE_TIME min_per, buf_time;
    UINT32 buffer_len, min_len;
    void *ptr = NULL;
    HRESULT hr;

    if(self->client)
        IAudioClient_Release(self->client);
    self->client = NULL;

    hr = IMMDevice_Activate(self->mmdev, &IID_IAudioClient, CLSCTX_INPROC_SERVER, NULL, &ptr);
    if(FAILED(hr))
    {
        ERR("Failed to reactivate audio client: 0x%08lx\n", hr);
        return hr;
    }
    self->client = ptr;

    hr = IAudioClient_GetMixFormat(self->client, &wfx);
    if(FAILED(hr))
    {
        ERR("Failed to get mix format: 0x%08lx\n", hr);
        return hr;
    }

    if(!MakeExtensible(&OutputType, wfx))
    {
        CoTaskMemFree(wfx);
        return E_FAIL;
    }
    CoTaskMemFree(wfx);
    wfx = NULL;

    buf_time = ((REFERENCE_TIME)device->UpdateSize*device->NumUpdates*10000000 +
                                device->Frequency-1) / device->Frequency;

    if(!(device->Flags&DEVICE_FREQUENCY_REQUEST))
        device->Frequency = OutputType.Format.nSamplesPerSec;
    if(!(device->Flags&DEVICE_CHANNELS_REQUEST))
    {
        if(OutputType.Format.nChannels == 1 && OutputType.dwChannelMask == MONO)
            device->FmtChans = DevFmtMono;
        else if(OutputType.Format.nChannels == 2 && OutputType.dwChannelMask == STEREO)
            device->FmtChans = DevFmtStereo;
        else if(OutputType.Format.nChannels == 4 && OutputType.dwChannelMask == QUAD)
            device->FmtChans = DevFmtQuad;
        else if(OutputType.Format.nChannels == 6 && OutputType.dwChannelMask == X5DOT1)
            device->FmtChans = DevFmtX51;
        else if(OutputType.Format.nChannels == 6 && OutputType.dwChannelMask == X5DOT1SIDE)
            device->FmtChans = DevFmtX51Side;
        else if(OutputType.Format.nChannels == 7 && OutputType.dwChannelMask == X6DOT1)
            device->FmtChans = DevFmtX61;
        else if(OutputType.Format.nChannels == 8 && OutputType.dwChannelMask == X7DOT1)
            device->FmtChans = DevFmtX71;
        else
            ERR("Unhandled channel config: %d -- 0x%08lx\n", OutputType.Format.nChannels, OutputType.dwChannelMask);
    }

    switch(device->FmtChans)
    {
        case DevFmtMono:
            OutputType.Format.nChannels = 1;
            OutputType.dwChannelMask = MONO;
            break;
        case DevFmtStereo:
            OutputType.Format.nChannels = 2;
            OutputType.dwChannelMask = STEREO;
            break;
        case DevFmtQuad:
            OutputType.Format.nChannels = 4;
            OutputType.dwChannelMask = QUAD;
            break;
        case DevFmtX51:
            OutputType.Format.nChannels = 6;
            OutputType.dwChannelMask = X5DOT1;
            break;
        case DevFmtX51Side:
            OutputType.Format.nChannels = 6;
            OutputType.dwChannelMask = X5DOT1SIDE;
            break;
        case DevFmtX61:
            OutputType.Format.nChannels = 7;
            OutputType.dwChannelMask = X6DOT1;
            break;
        case DevFmtX71:
            OutputType.Format.nChannels = 8;
            OutputType.dwChannelMask = X7DOT1;
            break;
    }
    switch(device->FmtType)
    {
        case DevFmtByte:
            device->FmtType = DevFmtUByte;
            /* fall-through */
        case DevFmtUByte:
            OutputType.Format.wBitsPerSample = 8;
            OutputType.Samples.wValidBitsPerSample = 8;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            break;
        case DevFmtUShort:
            device->FmtType = DevFmtShort;
            /* fall-through */
        case DevFmtShort:
            OutputType.Format.wBitsPerSample = 16;
            OutputType.Samples.wValidBitsPerSample = 16;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            break;
        case DevFmtUInt:
            device->FmtType = DevFmtInt;
            /* fall-through */
        case DevFmtInt:
            OutputType.Format.wBitsPerSample = 32;
            OutputType.Samples.wValidBitsPerSample = 32;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            break;
        case DevFmtFloat:
            OutputType.Format.wBitsPerSample = 32;
            OutputType.Samples.wValidBitsPerSample = 32;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            break;
    }
    OutputType.Format.nSamplesPerSec = device->Frequency;

    OutputType.Format.nBlockAlign = OutputType.Format.nChannels *
                                    OutputType.Format.wBitsPerSample / 8;
    OutputType.Format.nAvgBytesPerSec = OutputType.Format.nSamplesPerSec *
                                        OutputType.Format.nBlockAlign;

    hr = IAudioClient_IsFormatSupported(self->client, AUDCLNT_SHAREMODE_SHARED, &OutputType.Format, &wfx);
    if(FAILED(hr))
    {
        ERR("Failed to check format support: 0x%08lx\n", hr);
        hr = IAudioClient_GetMixFormat(self->client, &wfx);
    }
    if(FAILED(hr))
    {
        ERR("Failed to find a supported format: 0x%08lx\n", hr);
        return hr;
    }

    if(wfx != NULL)
    {
        if(!MakeExtensible(&OutputType, wfx))
        {
            CoTaskMemFree(wfx);
            return E_FAIL;
        }
        CoTaskMemFree(wfx);
        wfx = NULL;

        device->Frequency = OutputType.Format.nSamplesPerSec;
        if(OutputType.Format.nChannels == 1 && OutputType.dwChannelMask == MONO)
            device->FmtChans = DevFmtMono;
        else if(OutputType.Format.nChannels == 2 && OutputType.dwChannelMask == STEREO)
            device->FmtChans = DevFmtStereo;
        else if(OutputType.Format.nChannels == 4 && OutputType.dwChannelMask == QUAD)
            device->FmtChans = DevFmtQuad;
        else if(OutputType.Format.nChannels == 6 && OutputType.dwChannelMask == X5DOT1)
            device->FmtChans = DevFmtX51;
        else if(OutputType.Format.nChannels == 6 && OutputType.dwChannelMask == X5DOT1SIDE)
            device->FmtChans = DevFmtX51Side;
        else if(OutputType.Format.nChannels == 7 && OutputType.dwChannelMask == X6DOT1)
            device->FmtChans = DevFmtX61;
        else if(OutputType.Format.nChannels == 8 && OutputType.dwChannelMask == X7DOT1)
            device->FmtChans = DevFmtX71;
        else
        {
            ERR("Unhandled extensible channels: %d -- 0x%08lx\n", OutputType.Format.nChannels, OutputType.dwChannelMask);
            device->FmtChans = DevFmtStereo;
            OutputType.Format.nChannels = 2;
            OutputType.dwChannelMask = STEREO;
        }

        if(IsEqualGUID(&OutputType.SubFormat, &KSDATAFORMAT_SUBTYPE_PCM))
        {
            if(OutputType.Format.wBitsPerSample == 8)
                device->FmtType = DevFmtUByte;
            else if(OutputType.Format.wBitsPerSample == 16)
                device->FmtType = DevFmtShort;
            else if(OutputType.Format.wBitsPerSample == 32)
                device->FmtType = DevFmtInt;
            else
            {
                device->FmtType = DevFmtShort;
                OutputType.Format.wBitsPerSample = 16;
            }
        }
        else if(IsEqualGUID(&OutputType.SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
        {
            device->FmtType = DevFmtFloat;
            OutputType.Format.wBitsPerSample = 32;
        }
        else
        {
            ERR("Unhandled format sub-type\n");
            device->FmtType = DevFmtShort;
            OutputType.Format.wBitsPerSample = 16;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
        }
        OutputType.Samples.wValidBitsPerSample = OutputType.Format.wBitsPerSample;
    }

    SetDefaultWFXChannelOrder(device);

    hr = IAudioClient_Initialize(self->client, AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                 buf_time, 0, &OutputType.Format, NULL);
    if(FAILED(hr))
    {
        ERR("Failed to initialize audio client: 0x%08lx\n", hr);
        return hr;
    }

    hr = IAudioClient_GetDevicePeriod(self->client, &min_per, NULL);
    if(SUCCEEDED(hr))
    {
        min_len = (UINT32)((min_per*device->Frequency + 10000000-1) / 10000000);
        /* Find the nearest multiple of the period size to the update size */
        if(min_len < device->UpdateSize)
            min_len *= (device->UpdateSize + min_len/2)/min_len;
        hr = IAudioClient_GetBufferSize(self->client, &buffer_len);
    }
    if(FAILED(hr))
    {
        ERR("Failed to get audio buffer info: 0x%08lx\n", hr);
        return hr;
    }

    device->UpdateSize = min_len;
    device->NumUpdates = buffer_len / device->UpdateSize;
    if(device->NumUpdates <= 1)
    {
        ERR("Audio client returned buffer_len < period*2; expect break up\n");
        device->NumUpdates = 2;
        device->UpdateSize = buffer_len / device->NumUpdates;
    }

    hr = IAudioClient_SetEventHandle(self->client, self->NotifyEvent);
    if(FAILED(hr))
    {
        ERR("Failed to set event handle: 0x%08lx\n", hr);
        return hr;
    }

    return hr;
}


static ALCboolean ALCmmdevPlayback_start(ALCmmdevPlayback *self)
{
    ThreadRequest req = { self->MsgEvent, 0 };
    HRESULT hr = E_FAIL;

    if(PostThreadMessage(ThreadID, WM_USER_StartDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCmmdevProxy, self)))
        hr = WaitForResponse(&req);

    return SUCCEEDED(hr) ? ALC_TRUE : ALC_FALSE;
}

static HRESULT ALCmmdevPlayback_startProxy(ALCmmdevPlayback *self)
{
    HRESULT hr;
    void *ptr;

    ResetEvent(self->NotifyEvent);
    hr = IAudioClient_Start(self->client);
    if(FAILED(hr))
        ERR("Failed to start audio client: 0x%08lx\n", hr);

    if(SUCCEEDED(hr))
        hr = IAudioClient_GetService(self->client, &IID_IAudioRenderClient, &ptr);
    if(SUCCEEDED(hr))
    {
        self->render = ptr;
        self->killNow = 0;
        if(althrd_create(&self->thread, ALCmmdevPlayback_mixerProc, self) != althrd_success)
        {
            if(self->render)
                IAudioRenderClient_Release(self->render);
            self->render = NULL;
            IAudioClient_Stop(self->client);
            ERR("Failed to start thread\n");
            hr = E_FAIL;
        }
    }

    return hr;
}


static void ALCmmdevPlayback_stop(ALCmmdevPlayback *self)
{
    ThreadRequest req = { self->MsgEvent, 0 };
    if(PostThreadMessage(ThreadID, WM_USER_StopDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCmmdevProxy, self)))
        (void)WaitForResponse(&req);
}

static void ALCmmdevPlayback_stopProxy(ALCmmdevPlayback *self)
{
    int res;

    if(!self->render)
        return;

    self->killNow = 1;
    althrd_join(self->thread, &res);

    IAudioRenderClient_Release(self->render);
    self->render = NULL;
    IAudioClient_Stop(self->client);
}


static ALint64 ALCmmdevPlayback_getLatency(ALCmmdevPlayback *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    return (ALint64)self->Padding * 1000000000 / device->Frequency;
}


static inline void AppendAllDevicesList2(const DevMap *entry)
{ AppendAllDevicesList(al_string_get_cstr(entry->name)); }
static inline void AppendCaptureDeviceList2(const DevMap *entry)
{ AppendCaptureDeviceList(al_string_get_cstr(entry->name)); }

typedef struct ALCmmdevBackendFactory {
    DERIVE_FROM_TYPE(ALCbackendFactory);
} ALCmmdevBackendFactory;
#define ALCMMDEVBACKENDFACTORY_INITIALIZER { { GET_VTABLE2(ALCmmdevBackendFactory, ALCbackendFactory) } }

static ALCboolean ALCmmdevBackendFactory_init(ALCmmdevBackendFactory *self);
static void ALCmmdevBackendFactory_deinit(ALCmmdevBackendFactory *self);
static ALCboolean ALCmmdevBackendFactory_querySupport(ALCmmdevBackendFactory *self, ALCbackend_Type type);
static void ALCmmdevBackendFactory_probe(ALCmmdevBackendFactory *self, enum DevProbe type);
static ALCbackend* ALCmmdevBackendFactory_createBackend(ALCmmdevBackendFactory *self, ALCdevice *device, ALCbackend_Type type);

DEFINE_ALCBACKENDFACTORY_VTABLE(ALCmmdevBackendFactory);


static BOOL MMDevApiLoad(void)
{
    static HRESULT InitResult;
    if(!ThreadHdl)
    {
        ThreadRequest req;
        InitResult = E_FAIL;

        req.FinishedEvt = CreateEvent(NULL, FALSE, FALSE, NULL);
        if(req.FinishedEvt == NULL)
            ERR("Failed to create event: %lu\n", GetLastError());
        else
        {
            ThreadHdl = CreateThread(NULL, 0, ALCmmdevProxy_messageHandler, &req, 0, &ThreadID);
            if(ThreadHdl != NULL)
                InitResult = WaitForResponse(&req);
            CloseHandle(req.FinishedEvt);
        }
    }
    return SUCCEEDED(InitResult);
}

static ALCboolean ALCmmdevBackendFactory_init(ALCmmdevBackendFactory* UNUSED(self))
{
    VECTOR_INIT(PlaybackDevices);
    VECTOR_INIT(CaptureDevices);

    if(!MMDevApiLoad())
        return ALC_FALSE;
    return ALC_TRUE;
}

static void ALCmmdevBackendFactory_deinit(ALCmmdevBackendFactory* UNUSED(self))
{
    clear_devlist(&PlaybackDevices);
    VECTOR_DEINIT(PlaybackDevices);

    clear_devlist(&CaptureDevices);
    VECTOR_DEINIT(CaptureDevices);

    if(ThreadHdl)
    {
        TRACE("Sending WM_QUIT to Thread %04lx\n", ThreadID);
        PostThreadMessage(ThreadID, WM_QUIT, 0, 0);
        CloseHandle(ThreadHdl);
        ThreadHdl = NULL;
    }
}

static ALCboolean ALCmmdevBackendFactory_querySupport(ALCmmdevBackendFactory* UNUSED(self), ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
        return ALC_TRUE;
    return ALC_FALSE;
}

static void ALCmmdevBackendFactory_probe(ALCmmdevBackendFactory* UNUSED(self), enum DevProbe type)
{
    ThreadRequest req = { NULL, 0 };

    req.FinishedEvt = CreateEvent(NULL, FALSE, FALSE, NULL);
    if(req.FinishedEvt == NULL)
        ERR("Failed to create event: %lu\n", GetLastError());
    else
    {
        HRESULT hr = E_FAIL;
        if(PostThreadMessage(ThreadID, WM_USER_Enumerate, (WPARAM)&req, type))
            hr = WaitForResponse(&req);
        if(SUCCEEDED(hr)) switch(type)
        {
        case ALL_DEVICE_PROBE:
            VECTOR_FOR_EACH(const DevMap, PlaybackDevices, AppendAllDevicesList2);
            break;

        case CAPTURE_DEVICE_PROBE:
            VECTOR_FOR_EACH(const DevMap, CaptureDevices, AppendCaptureDeviceList2);
            break;
        }
        CloseHandle(req.FinishedEvt);
        req.FinishedEvt = NULL;
    }
}

static ALCbackend* ALCmmdevBackendFactory_createBackend(ALCmmdevBackendFactory* UNUSED(self), ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        ALCmmdevPlayback *backend;

        backend = ALCmmdevPlayback_New(sizeof(*backend));
        if(!backend) return NULL;
        memset(backend, 0, sizeof(*backend));

        ALCmmdevPlayback_Construct(backend, device);

        return STATIC_CAST(ALCbackend, backend);
    }

    return NULL;
}


ALCbackendFactory *ALCmmdevBackendFactory_getFactory(void)
{
    static ALCmmdevBackendFactory factory = ALCMMDEVBACKENDFACTORY_INITIALIZER;
    return STATIC_CAST(ALCbackendFactory, &factory);
}
