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
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#include <wtypes.h>
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

#include <vector>

#include "alMain.h"
#include "alu.h"
#include "ringbuffer.h"
#include "threads.h"
#include "compat.h"
#include "alstring.h"
#include "converter.h"

#include "backends/base.h"


DEFINE_GUID(KSDATAFORMAT_SUBTYPE_PCM, 0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);

DEFINE_DEVPROPKEY(DEVPKEY_Device_FriendlyName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80,0x20, 0x67,0xd1,0x46,0xa8,0x50,0xe0, 14);
DEFINE_PROPERTYKEY(PKEY_AudioEndpoint_FormFactor, 0x1da5d803, 0xd492, 0x4edd, 0x8c,0x23, 0xe0,0xc0,0xff,0xee,0x7f,0x0e, 0);
DEFINE_PROPERTYKEY(PKEY_AudioEndpoint_GUID, 0x1da5d803, 0xd492, 0x4edd, 0x8c, 0x23,0xe0, 0xc0,0xff,0xee,0x7f,0x0e, 4 );

#define MONO SPEAKER_FRONT_CENTER
#define STEREO (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT)
#define QUAD (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT)
#define X5DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)
#define X5DOT1REAR (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT)
#define X6DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_CENTER|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)
#define X7DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)
#define X7DOT1_WIDE (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT|SPEAKER_FRONT_LEFT_OF_CENTER|SPEAKER_FRONT_RIGHT_OF_CENTER)

#define REFTIME_PER_SEC ((REFERENCE_TIME)10000000)

#define DEVNAME_HEAD "OpenAL Soft on "


/* Scales the given value using 64-bit integer math, ceiling the result. */
static inline ALuint64 ScaleCeil(ALuint64 val, ALuint64 new_scale, ALuint64 old_scale)
{
    return (val*new_scale + old_scale-1) / old_scale;
}


typedef struct {
    al_string name;
    al_string endpoint_guid; // obtained from PKEY_AudioEndpoint_GUID , set to "Unknown device GUID" if absent.
    WCHAR *devid;
} DevMap;
TYPEDEF_VECTOR(DevMap, vector_DevMap)

static void clear_devlist(vector_DevMap *list)
{
#define CLEAR_DEVMAP(i) do {     \
    AL_STRING_DEINIT((i)->name); \
    AL_STRING_DEINIT((i)->endpoint_guid); \
    free((i)->devid);            \
    (i)->devid = nullptr;           \
} while(0)
    VECTOR_FOR_EACH(DevMap, *list, CLEAR_DEVMAP);
    VECTOR_RESIZE(*list, 0, 0);
#undef CLEAR_DEVMAP
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

static const char MessageStr[WM_USER_Last+1-WM_USER][20] = {
    "Open Device",
    "Reset Device",
    "Start Device",
    "Stop Device",
    "Close Device",
    "Enumerate Devices",
};

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


static void get_device_name_and_guid(IMMDevice *device, al_string *name, al_string *guid)
{
    IPropertyStore *ps;
    PROPVARIANT pvname;
    PROPVARIANT pvguid;
    HRESULT hr;

    alstr_copy_cstr(name, DEVNAME_HEAD);

    hr = device->OpenPropertyStore(STGM_READ, &ps);
    if(FAILED(hr))
    {
        WARN("OpenPropertyStore failed: 0x%08lx\n", hr);
        alstr_append_cstr(name, "Unknown Device Name");
        if(guid) alstr_copy_cstr(guid, "Unknown Device GUID");
        return;
    }

    PropVariantInit(&pvname);

    hr = ps->GetValue(reinterpret_cast<const PROPERTYKEY&>(DEVPKEY_Device_FriendlyName), &pvname);
    if(FAILED(hr))
    {
        WARN("GetValue Device_FriendlyName failed: 0x%08lx\n", hr);
        alstr_append_cstr(name, "Unknown Device Name");
    }
    else if(pvname.vt == VT_LPWSTR)
        alstr_append_wcstr(name, pvname.pwszVal);
    else
    {
        WARN("Unexpected PROPVARIANT type: 0x%04x\n", pvname.vt);
        alstr_append_cstr(name, "Unknown Device Name");
    }
    PropVariantClear(&pvname);

    if(guid)
    {
        PropVariantInit(&pvguid);

        hr = ps->GetValue(reinterpret_cast<const PROPERTYKEY&>(PKEY_AudioEndpoint_GUID), &pvguid);
        if(FAILED(hr))
        {
            WARN("GetValue AudioEndpoint_GUID failed: 0x%08lx\n", hr);
            alstr_copy_cstr(guid, "Unknown Device GUID");
        }
        else if(pvguid.vt == VT_LPWSTR)
            alstr_copy_wcstr(guid, pvguid.pwszVal);
        else
        {
            WARN("Unexpected PROPVARIANT type: 0x%04x\n", pvguid.vt);
            alstr_copy_cstr(guid, "Unknown Device GUID");
        }

        PropVariantClear(&pvguid);
    }

    ps->Release();
}

static void get_device_formfactor(IMMDevice *device, EndpointFormFactor *formfactor)
{
    IPropertyStore *ps;
    PROPVARIANT pvform;
    HRESULT hr;

    hr = device->OpenPropertyStore(STGM_READ, &ps);
    if(FAILED(hr))
    {
        WARN("OpenPropertyStore failed: 0x%08lx\n", hr);
        return;
    }

    PropVariantInit(&pvform);

    hr = ps->GetValue(reinterpret_cast<const PROPERTYKEY&>(PKEY_AudioEndpoint_FormFactor), &pvform);
    if(FAILED(hr))
        WARN("GetValue AudioEndpoint_FormFactor failed: 0x%08lx\n", hr);
    else if(pvform.vt == VT_UI4)
        *formfactor = static_cast<EndpointFormFactor>(pvform.ulVal);
    else if(pvform.vt == VT_EMPTY)
        *formfactor = UnknownFormFactor;
    else
        WARN("Unexpected PROPVARIANT type: 0x%04x\n", pvform.vt);

    PropVariantClear(&pvform);
    ps->Release();
}


static void add_device(IMMDevice *device, const WCHAR *devid, vector_DevMap *list)
{
    int count = 0;
    al_string tmpname;
    DevMap entry;

    AL_STRING_INIT(tmpname);
    AL_STRING_INIT(entry.name);
    AL_STRING_INIT(entry.endpoint_guid);

    entry.devid = strdupW(devid);
    get_device_name_and_guid(device, &tmpname, &entry.endpoint_guid);

    while(1)
    {
        const DevMap *iter;

        alstr_copy(&entry.name, tmpname);
        if(count != 0)
        {
            char str[64];
            snprintf(str, sizeof(str), " #%d", count+1);
            alstr_append_cstr(&entry.name, str);
        }

#define MATCH_ENTRY(i) (alstr_cmp(entry.name, (i)->name) == 0)
        VECTOR_FIND_IF(iter, const DevMap, *list, MATCH_ENTRY);
        if(iter == VECTOR_END(*list)) break;
#undef MATCH_ENTRY
        count++;
    }

    TRACE("Got device \"%s\", \"%s\", \"%ls\"\n", alstr_get_cstr(entry.name), alstr_get_cstr(entry.endpoint_guid), entry.devid);
    VECTOR_PUSH_BACK(*list, entry);

    AL_STRING_DEINIT(tmpname);
}

static WCHAR *get_device_id(IMMDevice *device)
{
    WCHAR *devid;
    HRESULT hr;

    hr = device->GetId(&devid);
    if(FAILED(hr))
    {
        ERR("Failed to get device id: %lx\n", hr);
        return nullptr;
    }

    return devid;
}

static HRESULT probe_devices(IMMDeviceEnumerator *devenum, EDataFlow flowdir, vector_DevMap *list)
{
    IMMDeviceCollection *coll;
    IMMDevice *defdev = nullptr;
    WCHAR *defdevid = nullptr;
    HRESULT hr;
    UINT count;
    UINT i;

    hr = devenum->EnumAudioEndpoints(flowdir, DEVICE_STATE_ACTIVE, &coll);
    if(FAILED(hr))
    {
        ERR("Failed to enumerate audio endpoints: 0x%08lx\n", hr);
        return hr;
    }

    count = 0;
    hr = coll->GetCount(&count);
    if(SUCCEEDED(hr) && count > 0)
    {
        clear_devlist(list);
        VECTOR_RESIZE(*list, 0, count);

        hr = devenum->GetDefaultAudioEndpoint(flowdir, eMultimedia, &defdev);
    }
    if(SUCCEEDED(hr) && defdev != nullptr)
    {
        defdevid = get_device_id(defdev);
        if(defdevid)
            add_device(defdev, defdevid, list);
    }

    for(i = 0;i < count;++i)
    {
        IMMDevice *device;
        WCHAR *devid;

        hr = coll->Item(i, &device);
        if(FAILED(hr)) continue;

        devid = get_device_id(device);
        if(devid)
        {
            if(wcscmp(devid, defdevid) != 0)
                add_device(device, devid, list);
            CoTaskMemFree(devid);
        }
        device->Release();
    }

    if(defdev) defdev->Release();
    if(defdevid) CoTaskMemFree(defdevid);
    coll->Release();

    return S_OK;
}


/* Proxy interface used by the message handler. */
struct ALCwasapiProxyVtable;

typedef struct ALCwasapiProxy {
    const struct ALCwasapiProxyVtable *vtbl;
} ALCwasapiProxy;

struct ALCwasapiProxyVtable {
    HRESULT (*const openProxy)(ALCwasapiProxy*);
    void (*const closeProxy)(ALCwasapiProxy*);

    HRESULT (*const resetProxy)(ALCwasapiProxy*);
    HRESULT (*const startProxy)(ALCwasapiProxy*);
    void  (*const stopProxy)(ALCwasapiProxy*);
};

#define DEFINE_ALCWASAPIPROXY_VTABLE(T)                                       \
DECLARE_THUNK(T, ALCwasapiProxy, HRESULT, openProxy)                          \
DECLARE_THUNK(T, ALCwasapiProxy, void, closeProxy)                            \
DECLARE_THUNK(T, ALCwasapiProxy, HRESULT, resetProxy)                         \
DECLARE_THUNK(T, ALCwasapiProxy, HRESULT, startProxy)                         \
DECLARE_THUNK(T, ALCwasapiProxy, void, stopProxy)                             \
                                                                              \
static const struct ALCwasapiProxyVtable T##_ALCwasapiProxy_vtable = {        \
    T##_ALCwasapiProxy_openProxy,                                             \
    T##_ALCwasapiProxy_closeProxy,                                            \
    T##_ALCwasapiProxy_resetProxy,                                            \
    T##_ALCwasapiProxy_startProxy,                                            \
    T##_ALCwasapiProxy_stopProxy,                                             \
}

static void ALCwasapiProxy_Construct(ALCwasapiProxy* UNUSED(self)) { }
static void ALCwasapiProxy_Destruct(ALCwasapiProxy* UNUSED(self)) { }

static DWORD CALLBACK ALCwasapiProxy_messageHandler(void *ptr)
{
    auto req = reinterpret_cast<ThreadRequest*>(ptr);
    ALuint deviceCount = 0;
    ALCwasapiProxy *proxy;
    HRESULT hr, cohr;
    MSG msg;

    TRACE("Starting message thread\n");

    cohr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if(FAILED(cohr))
    {
        WARN("Failed to initialize COM: 0x%08lx\n", cohr);
        ReturnMsgResponse(req, cohr);
        return 0;
    }

    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_INPROC_SERVER, IID_IMMDeviceEnumerator, &ptr);
    if(FAILED(hr))
    {
        WARN("Failed to create IMMDeviceEnumerator instance: 0x%08lx\n", hr);
        CoUninitialize();
        ReturnMsgResponse(req, hr);
        return 0;
    }
    auto Enumerator = reinterpret_cast<IMMDeviceEnumerator*>(ptr);
    Enumerator->Release();
    Enumerator = nullptr;

    CoUninitialize();

    /* HACK: Force Windows to create a message queue for this thread before
     * returning success, otherwise PostThreadMessage may fail if it gets
     * called before GetMessage.
     */
    PeekMessage(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    TRACE("Message thread initialization complete\n");
    ReturnMsgResponse(req, S_OK);

    TRACE("Starting message loop\n");
    while(GetMessage(&msg, nullptr, WM_USER_First, WM_USER_Last))
    {
        TRACE("Got message \"%s\" (0x%04x, lparam=%p, wparam=%p)\n",
            (msg.message >= WM_USER && msg.message <= WM_USER_Last) ?
            MessageStr[msg.message-WM_USER] : "Unknown",
            msg.message, (void*)msg.lParam, (void*)msg.wParam
        );
        switch(msg.message)
        {
        case WM_USER_OpenDevice:
            req = reinterpret_cast<ThreadRequest*>(msg.wParam);
            proxy = reinterpret_cast<ALCwasapiProxy*>(msg.lParam);

            hr = cohr = S_OK;
            if(++deviceCount == 1)
                hr = cohr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
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
            req = reinterpret_cast<ThreadRequest*>(msg.wParam);
            proxy = reinterpret_cast<ALCwasapiProxy*>(msg.lParam);

            hr = V0(proxy,resetProxy)();
            ReturnMsgResponse(req, hr);
            continue;

        case WM_USER_StartDevice:
            req = reinterpret_cast<ThreadRequest*>(msg.wParam);
            proxy = reinterpret_cast<ALCwasapiProxy*>(msg.lParam);

            hr = V0(proxy,startProxy)();
            ReturnMsgResponse(req, hr);
            continue;

        case WM_USER_StopDevice:
            req = reinterpret_cast<ThreadRequest*>(msg.wParam);
            proxy = reinterpret_cast<ALCwasapiProxy*>(msg.lParam);

            V0(proxy,stopProxy)();
            ReturnMsgResponse(req, S_OK);
            continue;

        case WM_USER_CloseDevice:
            req = reinterpret_cast<ThreadRequest*>(msg.wParam);
            proxy = reinterpret_cast<ALCwasapiProxy*>(msg.lParam);

            V0(proxy,closeProxy)();
            if(--deviceCount == 0)
                CoUninitialize();

            ReturnMsgResponse(req, S_OK);
            continue;

        case WM_USER_Enumerate:
            req = reinterpret_cast<ThreadRequest*>(msg.wParam);

            hr = cohr = S_OK;
            if(++deviceCount == 1)
                hr = cohr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if(SUCCEEDED(hr))
                hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_INPROC_SERVER, IID_IMMDeviceEnumerator, &ptr);
            if(SUCCEEDED(hr))
            {
                Enumerator = reinterpret_cast<IMMDeviceEnumerator*>(ptr);

                if(msg.lParam == ALL_DEVICE_PROBE)
                    hr = probe_devices(Enumerator, eRender, &PlaybackDevices);
                else if(msg.lParam == CAPTURE_DEVICE_PROBE)
                    hr = probe_devices(Enumerator, eCapture, &CaptureDevices);

                Enumerator->Release();
                Enumerator = nullptr;
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


typedef struct ALCwasapiPlayback {
    DERIVE_FROM_TYPE(ALCbackend);
    DERIVE_FROM_TYPE(ALCwasapiProxy);

    WCHAR *devid;

    IMMDevice *mmdev;
    IAudioClient *client;
    IAudioRenderClient *render;
    HANDLE NotifyEvent;

    HANDLE MsgEvent;

    ATOMIC(UINT32) Padding;

    ATOMIC(int) killNow;
    althrd_t thread;
} ALCwasapiPlayback;

static int ALCwasapiPlayback_mixerProc(void *arg);

static void ALCwasapiPlayback_Construct(ALCwasapiPlayback *self, ALCdevice *device);
static void ALCwasapiPlayback_Destruct(ALCwasapiPlayback *self);
static ALCenum ALCwasapiPlayback_open(ALCwasapiPlayback *self, const ALCchar *name);
static HRESULT ALCwasapiPlayback_openProxy(ALCwasapiPlayback *self);
static void ALCwasapiPlayback_closeProxy(ALCwasapiPlayback *self);
static ALCboolean ALCwasapiPlayback_reset(ALCwasapiPlayback *self);
static HRESULT ALCwasapiPlayback_resetProxy(ALCwasapiPlayback *self);
static ALCboolean ALCwasapiPlayback_start(ALCwasapiPlayback *self);
static HRESULT ALCwasapiPlayback_startProxy(ALCwasapiPlayback *self);
static void ALCwasapiPlayback_stop(ALCwasapiPlayback *self);
static void ALCwasapiPlayback_stopProxy(ALCwasapiPlayback *self);
static DECLARE_FORWARD2(ALCwasapiPlayback, ALCbackend, ALCenum, captureSamples, ALCvoid*, ALCuint)
static DECLARE_FORWARD(ALCwasapiPlayback, ALCbackend, ALCuint, availableSamples)
static ClockLatency ALCwasapiPlayback_getClockLatency(ALCwasapiPlayback *self);
static DECLARE_FORWARD(ALCwasapiPlayback, ALCbackend, void, lock)
static DECLARE_FORWARD(ALCwasapiPlayback, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(ALCwasapiPlayback)

DEFINE_ALCWASAPIPROXY_VTABLE(ALCwasapiPlayback);
DEFINE_ALCBACKEND_VTABLE(ALCwasapiPlayback);


static void ALCwasapiPlayback_Construct(ALCwasapiPlayback *self, ALCdevice *device)
{
    SET_VTABLE2(ALCwasapiPlayback, ALCbackend, self);
    SET_VTABLE2(ALCwasapiPlayback, ALCwasapiProxy, self);
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    ALCwasapiProxy_Construct(STATIC_CAST(ALCwasapiProxy, self));

    self->devid = nullptr;

    self->mmdev = nullptr;
    self->client = nullptr;
    self->render = nullptr;
    self->NotifyEvent = nullptr;

    self->MsgEvent = nullptr;

    ATOMIC_INIT(&self->Padding, 0u);

    ATOMIC_INIT(&self->killNow, 0);
}

static void ALCwasapiPlayback_Destruct(ALCwasapiPlayback *self)
{
    if(self->MsgEvent)
    {
        ThreadRequest req = { self->MsgEvent, 0 };
        if(PostThreadMessage(ThreadID, WM_USER_CloseDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCwasapiProxy, self)))
            (void)WaitForResponse(&req);

        CloseHandle(self->MsgEvent);
        self->MsgEvent = nullptr;
    }

    if(self->NotifyEvent)
        CloseHandle(self->NotifyEvent);
    self->NotifyEvent = nullptr;

    free(self->devid);
    self->devid = nullptr;

    if(self->NotifyEvent != nullptr)
        CloseHandle(self->NotifyEvent);
    self->NotifyEvent = nullptr;
    if(self->MsgEvent != nullptr)
        CloseHandle(self->MsgEvent);
    self->MsgEvent = nullptr;

    free(self->devid);
    self->devid = nullptr;

    ALCwasapiProxy_Destruct(STATIC_CAST(ALCwasapiProxy, self));
    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
}


FORCE_ALIGN static int ALCwasapiPlayback_mixerProc(void *arg)
{
    auto self = reinterpret_cast<ALCwasapiPlayback*>(arg);
    ALCdevice *device{STATIC_CAST(ALCbackend, self)->mDevice};
    IAudioClient *client = self->client;
    IAudioRenderClient *render = self->render;
    UINT32 buffer_len, written;
    ALuint update_size, len;
    BYTE *buffer;
    HRESULT hr;

    hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if(FAILED(hr))
    {
        ERR("CoInitializeEx(nullptr, COINIT_MULTITHREADED) failed: 0x%08lx\n", hr);
        V0(device->Backend,lock)();
        aluHandleDisconnect(device, "COM init failed: 0x%08lx", hr);
        V0(device->Backend,unlock)();
        return 1;
    }

    SetRTPriority();
    althrd_setname(althrd_current(), MIXER_THREAD_NAME);

    update_size = device->UpdateSize;
    buffer_len = update_size * device->NumUpdates;
    while(!ATOMIC_LOAD(&self->killNow, almemory_order_relaxed))
    {
        hr = client->GetCurrentPadding(&written);
        if(FAILED(hr))
        {
            ERR("Failed to get padding: 0x%08lx\n", hr);
            V0(device->Backend,lock)();
            aluHandleDisconnect(device, "Failed to retrieve buffer padding: 0x%08lx", hr);
            V0(device->Backend,unlock)();
            break;
        }
        ATOMIC_STORE(&self->Padding, written, almemory_order_relaxed);

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

        hr = render->GetBuffer(len, &buffer);
        if(SUCCEEDED(hr))
        {
            ALCwasapiPlayback_lock(self);
            aluMixData(device, buffer, len);
            ATOMIC_STORE(&self->Padding, written + len, almemory_order_relaxed);
            ALCwasapiPlayback_unlock(self);
            hr = render->ReleaseBuffer(len, 0);
        }
        if(FAILED(hr))
        {
            ERR("Failed to buffer data: 0x%08lx\n", hr);
            V0(device->Backend,lock)();
            aluHandleDisconnect(device, "Failed to send playback samples: 0x%08lx", hr);
            V0(device->Backend,unlock)();
            break;
        }
    }
    ATOMIC_STORE(&self->Padding, 0u, almemory_order_release);

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

static ALCenum ALCwasapiPlayback_open(ALCwasapiPlayback *self, const ALCchar *deviceName)
{
    HRESULT hr = S_OK;

    self->NotifyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    self->MsgEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if(self->NotifyEvent == nullptr || self->MsgEvent == nullptr)
    {
        ERR("Failed to create message events: %lu\n", GetLastError());
        hr = E_FAIL;
    }

    if(SUCCEEDED(hr))
    {
        if(deviceName)
        {
            const DevMap *iter;

            if(VECTOR_SIZE(PlaybackDevices) == 0)
            {
                ThreadRequest req = { self->MsgEvent, 0 };
                if(PostThreadMessage(ThreadID, WM_USER_Enumerate, (WPARAM)&req, ALL_DEVICE_PROBE))
                    (void)WaitForResponse(&req);
            }

            hr = E_FAIL;
#define MATCH_NAME(i) (alstr_cmp_cstr((i)->name, deviceName) == 0 ||        \
                       alstr_cmp_cstr((i)->endpoint_guid, deviceName) == 0)
            VECTOR_FIND_IF(iter, const DevMap, PlaybackDevices, MATCH_NAME);
#undef MATCH_NAME
            if(iter == VECTOR_END(PlaybackDevices))
            {
                int len;
                if((len=MultiByteToWideChar(CP_UTF8, 0, deviceName, -1, nullptr, 0)) > 0)
                {
                    std::vector<WCHAR> wname(len);
                    MultiByteToWideChar(CP_UTF8, 0, deviceName, -1, wname.data(), len);
#define MATCH_NAME(i) (wcscmp((i)->devid, wname.data()) == 0)
                    VECTOR_FIND_IF(iter, const DevMap, PlaybackDevices, MATCH_NAME);
#undef MATCH_NAME
                }
            }
            if(iter == VECTOR_END(PlaybackDevices))
                WARN("Failed to find device name matching \"%s\"\n", deviceName);
            else
            {
                ALCdevice *device = STATIC_CAST(ALCbackend,self)->mDevice;
                self->devid = strdupW(iter->devid);
                alstr_copy(&device->DeviceName, iter->name);
                hr = S_OK;
            }
        }
    }

    if(SUCCEEDED(hr))
    {
        ThreadRequest req = { self->MsgEvent, 0 };

        hr = E_FAIL;
        if(PostThreadMessage(ThreadID, WM_USER_OpenDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCwasapiProxy, self)))
            hr = WaitForResponse(&req);
        else
            ERR("Failed to post thread message: %lu\n", GetLastError());
    }

    if(FAILED(hr))
    {
        if(self->NotifyEvent != nullptr)
            CloseHandle(self->NotifyEvent);
        self->NotifyEvent = nullptr;
        if(self->MsgEvent != nullptr)
            CloseHandle(self->MsgEvent);
        self->MsgEvent = nullptr;

        free(self->devid);
        self->devid = nullptr;

        ERR("Device init failed: 0x%08lx\n", hr);
        return ALC_INVALID_VALUE;
    }

    return ALC_NO_ERROR;
}

static HRESULT ALCwasapiPlayback_openProxy(ALCwasapiPlayback *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    void *ptr;
    HRESULT hr;

    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_INPROC_SERVER, IID_IMMDeviceEnumerator, &ptr);
    if(SUCCEEDED(hr))
    {
        auto Enumerator = reinterpret_cast<IMMDeviceEnumerator*>(ptr);
        if(!self->devid)
            hr = Enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &self->mmdev);
        else
            hr = Enumerator->GetDevice(self->devid, &self->mmdev);
        Enumerator->Release();
    }
    if(SUCCEEDED(hr))
        hr = self->mmdev->Activate(IID_IAudioClient, CLSCTX_INPROC_SERVER, nullptr, &ptr);
    if(SUCCEEDED(hr))
    {
        self->client = reinterpret_cast<IAudioClient*>(ptr);
        if(alstr_empty(device->DeviceName))
            get_device_name_and_guid(self->mmdev, &device->DeviceName, nullptr);
    }

    if(FAILED(hr))
    {
        if(self->mmdev)
            self->mmdev->Release();
        self->mmdev = nullptr;
    }

    return hr;
}


static void ALCwasapiPlayback_closeProxy(ALCwasapiPlayback *self)
{
    if(self->client)
        self->client->Release();
    self->client = nullptr;

    if(self->mmdev)
        self->mmdev->Release();
    self->mmdev = nullptr;
}


static ALCboolean ALCwasapiPlayback_reset(ALCwasapiPlayback *self)
{
    ThreadRequest req = { self->MsgEvent, 0 };
    HRESULT hr = E_FAIL;

    if(PostThreadMessage(ThreadID, WM_USER_ResetDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCwasapiProxy, self)))
        hr = WaitForResponse(&req);

    return SUCCEEDED(hr) ? ALC_TRUE : ALC_FALSE;
}

static HRESULT ALCwasapiPlayback_resetProxy(ALCwasapiPlayback *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    EndpointFormFactor formfactor = UnknownFormFactor;
    WAVEFORMATEXTENSIBLE OutputType;
    WAVEFORMATEX *wfx = nullptr;
    REFERENCE_TIME min_per, buf_time;
    UINT32 buffer_len, min_len;
    void *ptr = nullptr;
    HRESULT hr;

    if(self->client)
        self->client->Release();
    self->client = nullptr;

    hr = self->mmdev->Activate(IID_IAudioClient, CLSCTX_INPROC_SERVER, nullptr, &ptr);
    if(FAILED(hr))
    {
        ERR("Failed to reactivate audio client: 0x%08lx\n", hr);
        return hr;
    }
    self->client = reinterpret_cast<IAudioClient*>(ptr);

    hr = self->client->GetMixFormat(&wfx);
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
    wfx = nullptr;

    buf_time = ScaleCeil(device->UpdateSize*device->NumUpdates, REFTIME_PER_SEC,
                         device->Frequency);

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
        else if(OutputType.Format.nChannels == 6 && OutputType.dwChannelMask == X5DOT1REAR)
            device->FmtChans = DevFmtX51Rear;
        else if(OutputType.Format.nChannels == 7 && OutputType.dwChannelMask == X6DOT1)
            device->FmtChans = DevFmtX61;
        else if(OutputType.Format.nChannels == 8 && (OutputType.dwChannelMask == X7DOT1 || OutputType.dwChannelMask == X7DOT1_WIDE))
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
        case DevFmtAmbi3D:
            device->FmtChans = DevFmtStereo;
            /*fall-through*/
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
        case DevFmtX51Rear:
            OutputType.Format.nChannels = 6;
            OutputType.dwChannelMask = X5DOT1REAR;
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

    hr = self->client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &OutputType.Format, &wfx);
    if(FAILED(hr))
    {
        ERR("Failed to check format support: 0x%08lx\n", hr);
        hr = self->client->GetMixFormat(&wfx);
    }
    if(FAILED(hr))
    {
        ERR("Failed to find a supported format: 0x%08lx\n", hr);
        return hr;
    }

    if(wfx != nullptr)
    {
        if(!MakeExtensible(&OutputType, wfx))
        {
            CoTaskMemFree(wfx);
            return E_FAIL;
        }
        CoTaskMemFree(wfx);
        wfx = nullptr;

        device->Frequency = OutputType.Format.nSamplesPerSec;
        if(OutputType.Format.nChannels == 1 && OutputType.dwChannelMask == MONO)
            device->FmtChans = DevFmtMono;
        else if(OutputType.Format.nChannels == 2 && OutputType.dwChannelMask == STEREO)
            device->FmtChans = DevFmtStereo;
        else if(OutputType.Format.nChannels == 4 && OutputType.dwChannelMask == QUAD)
            device->FmtChans = DevFmtQuad;
        else if(OutputType.Format.nChannels == 6 && OutputType.dwChannelMask == X5DOT1)
            device->FmtChans = DevFmtX51;
        else if(OutputType.Format.nChannels == 6 && OutputType.dwChannelMask == X5DOT1REAR)
            device->FmtChans = DevFmtX51Rear;
        else if(OutputType.Format.nChannels == 7 && OutputType.dwChannelMask == X6DOT1)
            device->FmtChans = DevFmtX61;
        else if(OutputType.Format.nChannels == 8 && (OutputType.dwChannelMask == X7DOT1 || OutputType.dwChannelMask == X7DOT1_WIDE))
            device->FmtChans = DevFmtX71;
        else
        {
            ERR("Unhandled extensible channels: %d -- 0x%08lx\n", OutputType.Format.nChannels, OutputType.dwChannelMask);
            device->FmtChans = DevFmtStereo;
            OutputType.Format.nChannels = 2;
            OutputType.dwChannelMask = STEREO;
        }

        if(IsEqualGUID(OutputType.SubFormat, KSDATAFORMAT_SUBTYPE_PCM))
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
        else if(IsEqualGUID(OutputType.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
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
    get_device_formfactor(self->mmdev, &formfactor);
    device->IsHeadphones = (device->FmtChans == DevFmtStereo &&
                            (formfactor == Headphones || formfactor == Headset)
                           );

    SetDefaultWFXChannelOrder(device);

    hr = self->client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                  buf_time, 0, &OutputType.Format, nullptr);
    if(FAILED(hr))
    {
        ERR("Failed to initialize audio client: 0x%08lx\n", hr);
        return hr;
    }

    hr = self->client->GetDevicePeriod(&min_per, nullptr);
    if(SUCCEEDED(hr))
    {
        min_len = (UINT32)ScaleCeil(min_per, device->Frequency, REFTIME_PER_SEC);
        /* Find the nearest multiple of the period size to the update size */
        if(min_len < device->UpdateSize)
            min_len *= (device->UpdateSize + min_len/2)/min_len;
        hr = self->client->GetBufferSize(&buffer_len);
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

    hr = self->client->SetEventHandle(self->NotifyEvent);
    if(FAILED(hr))
    {
        ERR("Failed to set event handle: 0x%08lx\n", hr);
        return hr;
    }

    return hr;
}


static ALCboolean ALCwasapiPlayback_start(ALCwasapiPlayback *self)
{
    ThreadRequest req = { self->MsgEvent, 0 };
    HRESULT hr = E_FAIL;

    if(PostThreadMessage(ThreadID, WM_USER_StartDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCwasapiProxy, self)))
        hr = WaitForResponse(&req);

    return SUCCEEDED(hr) ? ALC_TRUE : ALC_FALSE;
}

static HRESULT ALCwasapiPlayback_startProxy(ALCwasapiPlayback *self)
{
    HRESULT hr;
    void *ptr;

    ResetEvent(self->NotifyEvent);
    hr = self->client->Start();
    if(FAILED(hr))
        ERR("Failed to start audio client: 0x%08lx\n", hr);

    if(SUCCEEDED(hr))
        hr = self->client->GetService(IID_IAudioRenderClient, &ptr);
    if(SUCCEEDED(hr))
    {
        self->render = reinterpret_cast<IAudioRenderClient*>(ptr);
        ATOMIC_STORE(&self->killNow, 0, almemory_order_release);
        if(althrd_create(&self->thread, ALCwasapiPlayback_mixerProc, self) != althrd_success)
        {
            if(self->render)
                self->render->Release();
            self->render = nullptr;
            self->client->Stop();
            ERR("Failed to start thread\n");
            hr = E_FAIL;
        }
    }

    return hr;
}


static void ALCwasapiPlayback_stop(ALCwasapiPlayback *self)
{
    ThreadRequest req = { self->MsgEvent, 0 };
    if(PostThreadMessage(ThreadID, WM_USER_StopDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCwasapiProxy, self)))
        (void)WaitForResponse(&req);
}

static void ALCwasapiPlayback_stopProxy(ALCwasapiPlayback *self)
{
    int res;

    if(!self->render)
        return;

    ATOMIC_STORE_SEQ(&self->killNow, 1);
    althrd_join(self->thread, &res);

    self->render->Release();
    self->render = nullptr;
    self->client->Stop();
}


static ClockLatency ALCwasapiPlayback_getClockLatency(ALCwasapiPlayback *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    ClockLatency ret;

    ALCwasapiPlayback_lock(self);
    ret.ClockTime = GetDeviceClockTime(device);
    ret.Latency = ATOMIC_LOAD(&self->Padding, almemory_order_relaxed) * DEVICE_CLOCK_RES /
                  device->Frequency;
    ALCwasapiPlayback_unlock(self);

    return ret;
}


typedef struct ALCwasapiCapture {
    DERIVE_FROM_TYPE(ALCbackend);
    DERIVE_FROM_TYPE(ALCwasapiProxy);

    WCHAR *devid;

    IMMDevice *mmdev;
    IAudioClient *client;
    IAudioCaptureClient *capture;
    HANDLE NotifyEvent;

    HANDLE MsgEvent;

    ChannelConverter *ChannelConv;
    SampleConverter *SampleConv;
    ll_ringbuffer_t *Ring;

    ATOMIC(int) killNow;
    althrd_t thread;
} ALCwasapiCapture;

static int ALCwasapiCapture_recordProc(void *arg);

static void ALCwasapiCapture_Construct(ALCwasapiCapture *self, ALCdevice *device);
static void ALCwasapiCapture_Destruct(ALCwasapiCapture *self);
static ALCenum ALCwasapiCapture_open(ALCwasapiCapture *self, const ALCchar *name);
static HRESULT ALCwasapiCapture_openProxy(ALCwasapiCapture *self);
static void ALCwasapiCapture_closeProxy(ALCwasapiCapture *self);
static DECLARE_FORWARD(ALCwasapiCapture, ALCbackend, ALCboolean, reset)
static HRESULT ALCwasapiCapture_resetProxy(ALCwasapiCapture *self);
static ALCboolean ALCwasapiCapture_start(ALCwasapiCapture *self);
static HRESULT ALCwasapiCapture_startProxy(ALCwasapiCapture *self);
static void ALCwasapiCapture_stop(ALCwasapiCapture *self);
static void ALCwasapiCapture_stopProxy(ALCwasapiCapture *self);
static ALCenum ALCwasapiCapture_captureSamples(ALCwasapiCapture *self, ALCvoid *buffer, ALCuint samples);
static ALuint ALCwasapiCapture_availableSamples(ALCwasapiCapture *self);
static DECLARE_FORWARD(ALCwasapiCapture, ALCbackend, ClockLatency, getClockLatency)
static DECLARE_FORWARD(ALCwasapiCapture, ALCbackend, void, lock)
static DECLARE_FORWARD(ALCwasapiCapture, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(ALCwasapiCapture)

DEFINE_ALCWASAPIPROXY_VTABLE(ALCwasapiCapture);
DEFINE_ALCBACKEND_VTABLE(ALCwasapiCapture);


static void ALCwasapiCapture_Construct(ALCwasapiCapture *self, ALCdevice *device)
{
    SET_VTABLE2(ALCwasapiCapture, ALCbackend, self);
    SET_VTABLE2(ALCwasapiCapture, ALCwasapiProxy, self);
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    ALCwasapiProxy_Construct(STATIC_CAST(ALCwasapiProxy, self));

    self->devid = nullptr;

    self->mmdev = nullptr;
    self->client = nullptr;
    self->capture = nullptr;
    self->NotifyEvent = nullptr;

    self->MsgEvent = nullptr;

    self->ChannelConv = nullptr;
    self->SampleConv = nullptr;
    self->Ring = nullptr;

    ATOMIC_INIT(&self->killNow, 0);
}

static void ALCwasapiCapture_Destruct(ALCwasapiCapture *self)
{
    if(self->MsgEvent)
    {
        ThreadRequest req = { self->MsgEvent, 0 };
        if(PostThreadMessage(ThreadID, WM_USER_CloseDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCwasapiProxy, self)))
            (void)WaitForResponse(&req);

        CloseHandle(self->MsgEvent);
        self->MsgEvent = nullptr;
    }

    if(self->NotifyEvent != nullptr)
        CloseHandle(self->NotifyEvent);
    self->NotifyEvent = nullptr;

    ll_ringbuffer_free(self->Ring);
    self->Ring = nullptr;

    DestroySampleConverter(&self->SampleConv);
    DestroyChannelConverter(&self->ChannelConv);

    free(self->devid);
    self->devid = nullptr;

    ALCwasapiProxy_Destruct(STATIC_CAST(ALCwasapiProxy, self));
    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
}


FORCE_ALIGN int ALCwasapiCapture_recordProc(void *arg)
{
    auto self = reinterpret_cast<ALCwasapiCapture*>(arg);
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    IAudioCaptureClient *capture = self->capture;
    ALfloat *samples = nullptr;
    size_t samplesmax = 0;
    HRESULT hr;

    hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if(FAILED(hr))
    {
        ERR("CoInitializeEx(nullptr, COINIT_MULTITHREADED) failed: 0x%08lx\n", hr);
        V0(device->Backend,lock)();
        aluHandleDisconnect(device, "COM init failed: 0x%08lx", hr);
        V0(device->Backend,unlock)();
        return 1;
    }

    althrd_setname(althrd_current(), RECORD_THREAD_NAME);

    while(!ATOMIC_LOAD(&self->killNow, almemory_order_relaxed))
    {
        UINT32 avail;
        DWORD res;

        hr = capture->GetNextPacketSize(&avail);
        if(FAILED(hr))
            ERR("Failed to get next packet size: 0x%08lx\n", hr);
        else if(avail > 0)
        {
            UINT32 numsamples;
            DWORD flags;
            BYTE *rdata;

            hr = capture->GetBuffer(&rdata, &numsamples, &flags, nullptr, nullptr);
            if(FAILED(hr))
                ERR("Failed to get capture buffer: 0x%08lx\n", hr);
            else
            {
                ll_ringbuffer_data_t data[2];
                size_t dstframes = 0;

                if(self->ChannelConv)
                {
                    if(samplesmax < numsamples)
                    {
                        size_t newmax = RoundUp(numsamples, 4096);
                        void *tmp = al_calloc(DEF_ALIGN, newmax*2*sizeof(ALfloat));
                        al_free(samples);
                        samples = reinterpret_cast<ALfloat*>(tmp);
                        samplesmax = newmax;
                    }
                    ChannelConverterInput(self->ChannelConv, rdata, samples, numsamples);
                    rdata = (BYTE*)samples;
                }

                ll_ringbuffer_get_write_vector(self->Ring, data);

                if(self->SampleConv)
                {
                    const ALvoid *srcdata = rdata;
                    ALsizei srcframes = numsamples;

                    dstframes = SampleConverterInput(self->SampleConv,
                        &srcdata, &srcframes, data[0].buf, (ALsizei)minz(data[0].len, INT_MAX)
                    );
                    if(srcframes > 0 && dstframes == data[0].len && data[1].len > 0)
                    {
                        /* If some source samples remain, all of the first dest
                         * block was filled, and there's space in the second
                         * dest block, do another run for the second block.
                         */
                        dstframes += SampleConverterInput(self->SampleConv,
                            &srcdata, &srcframes, data[1].buf, (ALsizei)minz(data[1].len, INT_MAX)
                        );
                    }
                }
                else
                {
                    ALuint framesize = FrameSizeFromDevFmt(device->FmtChans, device->FmtType,
                                                           device->AmbiOrder);
                    size_t len1 = minz(data[0].len, numsamples);
                    size_t len2 = minz(data[1].len, numsamples-len1);

                    memcpy(data[0].buf, rdata, len1*framesize);
                    if(len2 > 0)
                        memcpy(data[1].buf, rdata+len1*framesize, len2*framesize);
                    dstframes = len1 + len2;
                }

                ll_ringbuffer_write_advance(self->Ring, dstframes);

                hr = capture->ReleaseBuffer(numsamples);
                if(FAILED(hr)) ERR("Failed to release capture buffer: 0x%08lx\n", hr);
            }
        }

        if(FAILED(hr))
        {
            V0(device->Backend,lock)();
            aluHandleDisconnect(device, "Failed to capture samples: 0x%08lx", hr);
            V0(device->Backend,unlock)();
            break;
        }

        res = WaitForSingleObjectEx(self->NotifyEvent, 2000, FALSE);
        if(res != WAIT_OBJECT_0)
            ERR("WaitForSingleObjectEx error: 0x%lx\n", res);
    }

    al_free(samples);
    samples = nullptr;
    samplesmax = 0;

    CoUninitialize();
    return 0;
}


static ALCenum ALCwasapiCapture_open(ALCwasapiCapture *self, const ALCchar *deviceName)
{
    HRESULT hr = S_OK;

    self->NotifyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    self->MsgEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if(self->NotifyEvent == nullptr || self->MsgEvent == nullptr)
    {
        ERR("Failed to create message events: %lu\n", GetLastError());
        hr = E_FAIL;
    }

    if(SUCCEEDED(hr))
    {
        if(deviceName)
        {
            const DevMap *iter;

            if(VECTOR_SIZE(CaptureDevices) == 0)
            {
                ThreadRequest req = { self->MsgEvent, 0 };
                if(PostThreadMessage(ThreadID, WM_USER_Enumerate, (WPARAM)&req, CAPTURE_DEVICE_PROBE))
                    (void)WaitForResponse(&req);
            }

            hr = E_FAIL;
#define MATCH_NAME(i) (alstr_cmp_cstr((i)->name, deviceName) == 0 ||        \
                       alstr_cmp_cstr((i)->endpoint_guid, deviceName) == 0)
            VECTOR_FIND_IF(iter, const DevMap, CaptureDevices, MATCH_NAME);
#undef MATCH_NAME
            if(iter == VECTOR_END(CaptureDevices))
            {
                int len;
                if((len=MultiByteToWideChar(CP_UTF8, 0, deviceName, -1, nullptr, 0)) > 0)
                {
                    std::vector<WCHAR> wname(len);
                    MultiByteToWideChar(CP_UTF8, 0, deviceName, -1, wname.data(), len);
#define MATCH_NAME(i) (wcscmp((i)->devid, wname.data()) == 0)
                    VECTOR_FIND_IF(iter, const DevMap, CaptureDevices, MATCH_NAME);
#undef MATCH_NAME
                }
            }
            if(iter == VECTOR_END(CaptureDevices))
                WARN("Failed to find device name matching \"%s\"\n", deviceName);
            else
            {
                ALCdevice *device = STATIC_CAST(ALCbackend,self)->mDevice;
                self->devid = strdupW(iter->devid);
                alstr_copy(&device->DeviceName, iter->name);
                hr = S_OK;
            }
        }
    }

    if(SUCCEEDED(hr))
    {
        ThreadRequest req = { self->MsgEvent, 0 };

        hr = E_FAIL;
        if(PostThreadMessage(ThreadID, WM_USER_OpenDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCwasapiProxy, self)))
            hr = WaitForResponse(&req);
        else
            ERR("Failed to post thread message: %lu\n", GetLastError());
    }

    if(FAILED(hr))
    {
        if(self->NotifyEvent != nullptr)
            CloseHandle(self->NotifyEvent);
        self->NotifyEvent = nullptr;
        if(self->MsgEvent != nullptr)
            CloseHandle(self->MsgEvent);
        self->MsgEvent = nullptr;

        free(self->devid);
        self->devid = nullptr;

        ERR("Device init failed: 0x%08lx\n", hr);
        return ALC_INVALID_VALUE;
    }
    else
    {
        ThreadRequest req = { self->MsgEvent, 0 };

        hr = E_FAIL;
        if(PostThreadMessage(ThreadID, WM_USER_ResetDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCwasapiProxy, self)))
            hr = WaitForResponse(&req);
        else
            ERR("Failed to post thread message: %lu\n", GetLastError());

        if(FAILED(hr))
        {
            if(hr == E_OUTOFMEMORY)
               return ALC_OUT_OF_MEMORY;
            return ALC_INVALID_VALUE;
        }
    }

    return ALC_NO_ERROR;
}

static HRESULT ALCwasapiCapture_openProxy(ALCwasapiCapture *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    void *ptr;
    HRESULT hr;

    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_INPROC_SERVER, IID_IMMDeviceEnumerator, &ptr);
    if(SUCCEEDED(hr))
    {
        auto Enumerator = reinterpret_cast<IMMDeviceEnumerator*>(ptr);
        if(!self->devid)
            hr = Enumerator->GetDefaultAudioEndpoint(eCapture, eMultimedia, &self->mmdev);
        else
            hr = Enumerator->GetDevice(self->devid, &self->mmdev);
        Enumerator->Release();
    }
    if(SUCCEEDED(hr))
        hr = self->mmdev->Activate(IID_IAudioClient, CLSCTX_INPROC_SERVER, nullptr, &ptr);
    if(SUCCEEDED(hr))
    {
        self->client = reinterpret_cast<IAudioClient*>(ptr);
        if(alstr_empty(device->DeviceName))
            get_device_name_and_guid(self->mmdev, &device->DeviceName, nullptr);
    }

    if(FAILED(hr))
    {
        if(self->mmdev)
            self->mmdev->Release();
        self->mmdev = nullptr;
    }

    return hr;
}


static void ALCwasapiCapture_closeProxy(ALCwasapiCapture *self)
{
    if(self->client)
        self->client->Release();
    self->client = nullptr;

    if(self->mmdev)
        self->mmdev->Release();
    self->mmdev = nullptr;
}


static HRESULT ALCwasapiCapture_resetProxy(ALCwasapiCapture *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;
    WAVEFORMATEXTENSIBLE OutputType;
    WAVEFORMATEX *wfx = nullptr;
    enum DevFmtType srcType;
    REFERENCE_TIME buf_time;
    UINT32 buffer_len;
    void *ptr = nullptr;
    HRESULT hr;

    if(self->client)
        self->client->Release();
    self->client = nullptr;

    hr = self->mmdev->Activate(IID_IAudioClient, CLSCTX_INPROC_SERVER, nullptr, &ptr);
    if(FAILED(hr))
    {
        ERR("Failed to reactivate audio client: 0x%08lx\n", hr);
        return hr;
    }
    self->client = reinterpret_cast<IAudioClient*>(ptr);

    buf_time = ScaleCeil(device->UpdateSize*device->NumUpdates, REFTIME_PER_SEC,
                         device->Frequency);
    // Make sure buffer is at least 100ms in size
    buf_time = maxu64(buf_time, REFTIME_PER_SEC/10);
    device->UpdateSize = (ALuint)ScaleCeil(buf_time, device->Frequency, REFTIME_PER_SEC) /
                         device->NumUpdates;

    OutputType.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
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
        case DevFmtX51Rear:
            OutputType.Format.nChannels = 6;
            OutputType.dwChannelMask = X5DOT1REAR;
            break;
        case DevFmtX61:
            OutputType.Format.nChannels = 7;
            OutputType.dwChannelMask = X6DOT1;
            break;
        case DevFmtX71:
            OutputType.Format.nChannels = 8;
            OutputType.dwChannelMask = X7DOT1;
            break;

        case DevFmtAmbi3D:
            return E_FAIL;
    }
    switch(device->FmtType)
    {
        /* NOTE: Signedness doesn't matter, the converter will handle it. */
        case DevFmtByte:
        case DevFmtUByte:
            OutputType.Format.wBitsPerSample = 8;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            break;
        case DevFmtShort:
        case DevFmtUShort:
            OutputType.Format.wBitsPerSample = 16;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            break;
        case DevFmtInt:
        case DevFmtUInt:
            OutputType.Format.wBitsPerSample = 32;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            break;
        case DevFmtFloat:
            OutputType.Format.wBitsPerSample = 32;
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            break;
    }
    OutputType.Samples.wValidBitsPerSample = OutputType.Format.wBitsPerSample;
    OutputType.Format.nSamplesPerSec = device->Frequency;

    OutputType.Format.nBlockAlign = OutputType.Format.nChannels *
                                    OutputType.Format.wBitsPerSample / 8;
    OutputType.Format.nAvgBytesPerSec = OutputType.Format.nSamplesPerSec *
                                        OutputType.Format.nBlockAlign;
    OutputType.Format.cbSize = sizeof(OutputType) - sizeof(OutputType.Format);

    hr = self->client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &OutputType.Format, &wfx);
    if(FAILED(hr))
    {
        ERR("Failed to check format support: 0x%08lx\n", hr);
        return hr;
    }

    DestroySampleConverter(&self->SampleConv);
    DestroyChannelConverter(&self->ChannelConv);

    if(wfx != nullptr)
    {
        if(!(wfx->nChannels == OutputType.Format.nChannels ||
             (wfx->nChannels == 1 && OutputType.Format.nChannels == 2) ||
             (wfx->nChannels == 2 && OutputType.Format.nChannels == 1)))
        {
            ERR("Failed to get matching format, wanted: %s %s %uhz, got: %d channel%s %d-bit %luhz\n",
                DevFmtChannelsString(device->FmtChans), DevFmtTypeString(device->FmtType),
                device->Frequency, wfx->nChannels, (wfx->nChannels==1)?"":"s", wfx->wBitsPerSample,
                wfx->nSamplesPerSec);
            CoTaskMemFree(wfx);
            return E_FAIL;
        }

        if(!MakeExtensible(&OutputType, wfx))
        {
            CoTaskMemFree(wfx);
            return E_FAIL;
        }
        CoTaskMemFree(wfx);
        wfx = nullptr;
    }

    if(IsEqualGUID(OutputType.SubFormat, KSDATAFORMAT_SUBTYPE_PCM))
    {
        if(OutputType.Format.wBitsPerSample == 8)
            srcType = DevFmtUByte;
        else if(OutputType.Format.wBitsPerSample == 16)
            srcType = DevFmtShort;
        else if(OutputType.Format.wBitsPerSample == 32)
            srcType = DevFmtInt;
        else
        {
            ERR("Unhandled integer bit depth: %d\n", OutputType.Format.wBitsPerSample);
            return E_FAIL;
        }
    }
    else if(IsEqualGUID(OutputType.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
    {
        if(OutputType.Format.wBitsPerSample == 32)
            srcType = DevFmtFloat;
        else
        {
            ERR("Unhandled float bit depth: %d\n", OutputType.Format.wBitsPerSample);
            return E_FAIL;
        }
    }
    else
    {
        ERR("Unhandled format sub-type\n");
        return E_FAIL;
    }

    if(device->FmtChans == DevFmtMono && OutputType.Format.nChannels == 2)
    {
        self->ChannelConv = CreateChannelConverter(srcType, DevFmtStereo, device->FmtChans);
        if(!self->ChannelConv)
        {
            ERR("Failed to create %s stereo-to-mono converter\n", DevFmtTypeString(srcType));
            return E_FAIL;
        }
        TRACE("Created %s stereo-to-mono converter\n", DevFmtTypeString(srcType));
        /* The channel converter always outputs float, so change the input type
         * for the resampler/type-converter.
         */
        srcType = DevFmtFloat;
    }
    else if(device->FmtChans == DevFmtStereo && OutputType.Format.nChannels == 1)
    {
        self->ChannelConv = CreateChannelConverter(srcType, DevFmtMono, device->FmtChans);
        if(!self->ChannelConv)
        {
            ERR("Failed to create %s mono-to-stereo converter\n", DevFmtTypeString(srcType));
            return E_FAIL;
        }
        TRACE("Created %s mono-to-stereo converter\n", DevFmtTypeString(srcType));
        srcType = DevFmtFloat;
    }

    if(device->Frequency != OutputType.Format.nSamplesPerSec || device->FmtType != srcType)
    {
        self->SampleConv = CreateSampleConverter(
            srcType, device->FmtType, ChannelsFromDevFmt(device->FmtChans, device->AmbiOrder),
            OutputType.Format.nSamplesPerSec, device->Frequency
        );
        if(!self->SampleConv)
        {
            ERR("Failed to create converter for %s format, dst: %s %uhz, src: %s %luhz\n",
                DevFmtChannelsString(device->FmtChans), DevFmtTypeString(device->FmtType),
                device->Frequency, DevFmtTypeString(srcType), OutputType.Format.nSamplesPerSec);
            return E_FAIL;
        }
        TRACE("Created converter for %s format, dst: %s %uhz, src: %s %luhz\n",
              DevFmtChannelsString(device->FmtChans), DevFmtTypeString(device->FmtType),
              device->Frequency, DevFmtTypeString(srcType), OutputType.Format.nSamplesPerSec);
    }

    hr = self->client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                  buf_time, 0, &OutputType.Format, nullptr);
    if(FAILED(hr))
    {
        ERR("Failed to initialize audio client: 0x%08lx\n", hr);
        return hr;
    }

    hr = self->client->GetBufferSize(&buffer_len);
    if(FAILED(hr))
    {
        ERR("Failed to get buffer size: 0x%08lx\n", hr);
        return hr;
    }

    buffer_len = maxu(device->UpdateSize*device->NumUpdates, buffer_len);
    ll_ringbuffer_free(self->Ring);
    self->Ring = ll_ringbuffer_create(buffer_len,
        FrameSizeFromDevFmt(device->FmtChans, device->FmtType, device->AmbiOrder),
        false
    );
    if(!self->Ring)
    {
        ERR("Failed to allocate capture ring buffer\n");
        return E_OUTOFMEMORY;
    }

    hr = self->client->SetEventHandle(self->NotifyEvent);
    if(FAILED(hr))
    {
        ERR("Failed to set event handle: 0x%08lx\n", hr);
        return hr;
    }

    return hr;
}


static ALCboolean ALCwasapiCapture_start(ALCwasapiCapture *self)
{
    ThreadRequest req = { self->MsgEvent, 0 };
    HRESULT hr = E_FAIL;

    if(PostThreadMessage(ThreadID, WM_USER_StartDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCwasapiProxy, self)))
        hr = WaitForResponse(&req);

    return SUCCEEDED(hr) ? ALC_TRUE : ALC_FALSE;
}

static HRESULT ALCwasapiCapture_startProxy(ALCwasapiCapture *self)
{
    HRESULT hr;
    void *ptr;

    ResetEvent(self->NotifyEvent);
    hr = self->client->Start();
    if(FAILED(hr))
    {
        ERR("Failed to start audio client: 0x%08lx\n", hr);
        return hr;
    }

    hr = self->client->GetService(IID_IAudioCaptureClient, &ptr);
    if(SUCCEEDED(hr))
    {
        self->capture = reinterpret_cast<IAudioCaptureClient*>(ptr);
        ATOMIC_STORE(&self->killNow, 0, almemory_order_release);
        if(althrd_create(&self->thread, ALCwasapiCapture_recordProc, self) != althrd_success)
        {
            ERR("Failed to start thread\n");
            self->capture->Release();
            self->capture = nullptr;
            hr = E_FAIL;
        }
    }

    if(FAILED(hr))
    {
        self->client->Stop();
        self->client->Reset();
    }

    return hr;
}


static void ALCwasapiCapture_stop(ALCwasapiCapture *self)
{
    ThreadRequest req = { self->MsgEvent, 0 };
    if(PostThreadMessage(ThreadID, WM_USER_StopDevice, (WPARAM)&req, (LPARAM)STATIC_CAST(ALCwasapiProxy, self)))
        (void)WaitForResponse(&req);
}

static void ALCwasapiCapture_stopProxy(ALCwasapiCapture *self)
{
    int res;

    if(!self->capture)
        return;

    ATOMIC_STORE_SEQ(&self->killNow, 1);
    althrd_join(self->thread, &res);

    self->capture->Release();
    self->capture = nullptr;
    self->client->Stop();
    self->client->Reset();
}


ALuint ALCwasapiCapture_availableSamples(ALCwasapiCapture *self)
{
    return (ALuint)ll_ringbuffer_read_space(self->Ring);
}

ALCenum ALCwasapiCapture_captureSamples(ALCwasapiCapture *self, ALCvoid *buffer, ALCuint samples)
{
    if(ALCwasapiCapture_availableSamples(self) < samples)
        return ALC_INVALID_VALUE;
    ll_ringbuffer_read(self->Ring, reinterpret_cast<char*>(buffer), samples);
    return ALC_NO_ERROR;
}


typedef struct ALCwasapiBackendFactory {
    DERIVE_FROM_TYPE(ALCbackendFactory);
} ALCwasapiBackendFactory;
#define ALCWASAPIBACKENDFACTORY_INITIALIZER { { GET_VTABLE2(ALCwasapiBackendFactory, ALCbackendFactory) } }

static ALCboolean ALCwasapiBackendFactory_init(ALCwasapiBackendFactory *self);
static void ALCwasapiBackendFactory_deinit(ALCwasapiBackendFactory *self);
static ALCboolean ALCwasapiBackendFactory_querySupport(ALCwasapiBackendFactory *self, ALCbackend_Type type);
static void ALCwasapiBackendFactory_probe(ALCwasapiBackendFactory *self, enum DevProbe type, al_string *outnames);
static ALCbackend* ALCwasapiBackendFactory_createBackend(ALCwasapiBackendFactory *self, ALCdevice *device, ALCbackend_Type type);

DEFINE_ALCBACKENDFACTORY_VTABLE(ALCwasapiBackendFactory);


static ALCboolean ALCwasapiBackendFactory_init(ALCwasapiBackendFactory* UNUSED(self))
{
    static HRESULT InitResult;

    VECTOR_INIT(PlaybackDevices);
    VECTOR_INIT(CaptureDevices);

    if(!ThreadHdl)
    {
        ThreadRequest req;
        InitResult = E_FAIL;

        req.FinishedEvt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if(req.FinishedEvt == nullptr)
            ERR("Failed to create event: %lu\n", GetLastError());
        else
        {
            ThreadHdl = CreateThread(nullptr, 0, ALCwasapiProxy_messageHandler, &req, 0, &ThreadID);
            if(ThreadHdl != nullptr)
                InitResult = WaitForResponse(&req);
            CloseHandle(req.FinishedEvt);
        }
    }

    return SUCCEEDED(InitResult) ? ALC_TRUE : ALC_FALSE;
}

static void ALCwasapiBackendFactory_deinit(ALCwasapiBackendFactory* UNUSED(self))
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
        ThreadHdl = nullptr;
    }
}

static ALCboolean ALCwasapiBackendFactory_querySupport(ALCwasapiBackendFactory* UNUSED(self), ALCbackend_Type type)
{
    if(type == ALCbackend_Playback || type == ALCbackend_Capture)
        return ALC_TRUE;
    return ALC_FALSE;
}

static void ALCwasapiBackendFactory_probe(ALCwasapiBackendFactory* UNUSED(self), enum DevProbe type, al_string *outnames)
{
    ThreadRequest req = { nullptr, 0 };

    req.FinishedEvt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if(req.FinishedEvt == nullptr)
        ERR("Failed to create event: %lu\n", GetLastError());
    else
    {
        HRESULT hr = E_FAIL;
        if(PostThreadMessage(ThreadID, WM_USER_Enumerate, (WPARAM)&req, type))
            hr = WaitForResponse(&req);
        if(SUCCEEDED(hr)) switch(type)
        {
#define APPEND_OUTNAME(e) do {                                                \
    if(!alstr_empty((e)->name))                                               \
        alstr_append_range(outnames, VECTOR_BEGIN((e)->name),                 \
                           VECTOR_END((e)->name)+1);                          \
} while(0)
        case ALL_DEVICE_PROBE:
            VECTOR_FOR_EACH(const DevMap, PlaybackDevices, APPEND_OUTNAME);
            break;

        case CAPTURE_DEVICE_PROBE:
            VECTOR_FOR_EACH(const DevMap, CaptureDevices, APPEND_OUTNAME);
            break;
#undef APPEND_OUTNAME
        }
        CloseHandle(req.FinishedEvt);
        req.FinishedEvt = nullptr;
    }
}

static ALCbackend* ALCwasapiBackendFactory_createBackend(ALCwasapiBackendFactory* UNUSED(self), ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        ALCwasapiPlayback *backend;
        NEW_OBJ(backend, ALCwasapiPlayback)(device);
        if(!backend) return nullptr;
        return STATIC_CAST(ALCbackend, backend);
    }
    if(type == ALCbackend_Capture)
    {
        ALCwasapiCapture *backend;
        NEW_OBJ(backend, ALCwasapiCapture)(device);
        if(!backend) return nullptr;
        return STATIC_CAST(ALCbackend, backend);
    }

    return nullptr;
}


ALCbackendFactory *ALCwasapiBackendFactory_getFactory(void)
{
    static ALCwasapiBackendFactory factory = ALCWASAPIBACKENDFACTORY_INITIALIZER;
    return STATIC_CAST(ALCbackendFactory, &factory);
}
