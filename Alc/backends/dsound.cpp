/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
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

#include "backends/dsound.h"

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#include <cguid.h>
#include <mmreg.h>
#ifndef _WAVEFORMATEXTENSIBLE_
#include <ks.h>
#include <ksmedia.h>
#endif

#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include <algorithm>

#include "alMain.h"
#include "alu.h"
#include "ringbuffer.h"
#include "compat.h"

/* MinGW-w64 needs this for some unknown reason now. */
using LPCWAVEFORMATEX = const WAVEFORMATEX*;
#include <dsound.h>


#ifndef DSSPEAKER_5POINT1
#   define DSSPEAKER_5POINT1          0x00000006
#endif
#ifndef DSSPEAKER_5POINT1_BACK
#   define DSSPEAKER_5POINT1_BACK     0x00000006
#endif
#ifndef DSSPEAKER_7POINT1
#   define DSSPEAKER_7POINT1          0x00000007
#endif
#ifndef DSSPEAKER_7POINT1_SURROUND
#   define DSSPEAKER_7POINT1_SURROUND 0x00000008
#endif
#ifndef DSSPEAKER_5POINT1_SURROUND
#   define DSSPEAKER_5POINT1_SURROUND 0x00000009
#endif


/* Some headers seem to define these as macros for __uuidof, which is annoying
 * since some headers don't declare them at all. Hopefully the ifdef is enough
 * to tell if they need to be declared.
 */
#ifndef KSDATAFORMAT_SUBTYPE_PCM
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_PCM, 0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
#endif
#ifndef KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
#endif

namespace {

#define DEVNAME_HEAD "OpenAL Soft on "


#ifdef HAVE_DYNLOAD
void *ds_handle;
HRESULT (WINAPI *pDirectSoundCreate)(const GUID *pcGuidDevice, IDirectSound **ppDS, IUnknown *pUnkOuter);
HRESULT (WINAPI *pDirectSoundEnumerateW)(LPDSENUMCALLBACKW pDSEnumCallback, void *pContext);
HRESULT (WINAPI *pDirectSoundCaptureCreate)(const GUID *pcGuidDevice, IDirectSoundCapture **ppDSC, IUnknown *pUnkOuter);
HRESULT (WINAPI *pDirectSoundCaptureEnumerateW)(LPDSENUMCALLBACKW pDSEnumCallback, void *pContext);

#ifndef IN_IDE_PARSER
#define DirectSoundCreate            pDirectSoundCreate
#define DirectSoundEnumerateW        pDirectSoundEnumerateW
#define DirectSoundCaptureCreate     pDirectSoundCaptureCreate
#define DirectSoundCaptureEnumerateW pDirectSoundCaptureEnumerateW
#endif
#endif


bool DSoundLoad(void)
{
#ifdef HAVE_DYNLOAD
    if(!ds_handle)
    {
        ds_handle = LoadLib("dsound.dll");
        if(!ds_handle)
        {
            ERR("Failed to load dsound.dll\n");
            return false;
        }

#define LOAD_FUNC(f) do {                                                     \
    p##f = reinterpret_cast<decltype(p##f)>(GetSymbol(ds_handle, #f));        \
    if(!p##f)                                                                 \
    {                                                                         \
        CloseLib(ds_handle);                                                  \
        ds_handle = nullptr;                                                  \
        return false;                                                         \
    }                                                                         \
} while(0)
        LOAD_FUNC(DirectSoundCreate);
        LOAD_FUNC(DirectSoundEnumerateW);
        LOAD_FUNC(DirectSoundCaptureCreate);
        LOAD_FUNC(DirectSoundCaptureEnumerateW);
#undef LOAD_FUNC
    }
#endif
    return true;
}


#define MAX_UPDATES 128

struct DevMap {
    std::string name;
    GUID guid;

    template<typename T0, typename T1>
    DevMap(T0&& name_, T1&& guid_)
      : name{std::forward<T0>(name_)}, guid{std::forward<T1>(guid_)}
    { }
};

al::vector<DevMap> PlaybackDevices;
al::vector<DevMap> CaptureDevices;

bool checkName(const al::vector<DevMap> &list, const std::string &name)
{
    return std::find_if(list.cbegin(), list.cend(),
        [&name](const DevMap &entry) -> bool
        { return entry.name == name; }
    ) != list.cend();
}

BOOL CALLBACK DSoundEnumDevices(GUID *guid, const WCHAR *desc, const WCHAR* UNUSED(drvname), void *data)
{
    if(!guid)
        return TRUE;

    auto& devices = *reinterpret_cast<al::vector<DevMap>*>(data);
    const std::string basename{DEVNAME_HEAD + wstr_to_utf8(desc)};

    int count{1};
    std::string newname{basename};
    while(checkName(devices, newname))
    {
        newname = basename;
        newname += " #";
        newname += std::to_string(++count);
    }
    devices.emplace_back(std::move(newname), *guid);
    const DevMap &newentry = devices.back();

    OLECHAR *guidstr{nullptr};
    HRESULT hr{StringFromCLSID(*guid, &guidstr)};
    if(SUCCEEDED(hr))
    {
        TRACE("Got device \"%s\", GUID \"%ls\"\n", newentry.name.c_str(), guidstr);
        CoTaskMemFree(guidstr);
    }

    return TRUE;
}


struct ALCdsoundPlayback final : public ALCbackend {
    IDirectSound       *mDS{nullptr};
    IDirectSoundBuffer *mPrimaryBuffer{nullptr};
    IDirectSoundBuffer *mBuffer{nullptr};
    IDirectSoundNotify *mNotifies{nullptr};
    HANDLE             mNotifyEvent{nullptr};

    std::atomic<ALenum> mKillNow{AL_TRUE};
    std::thread mThread;
};

int ALCdsoundPlayback_mixerProc(ALCdsoundPlayback *self);

void ALCdsoundPlayback_Construct(ALCdsoundPlayback *self, ALCdevice *device);
void ALCdsoundPlayback_Destruct(ALCdsoundPlayback *self);
ALCenum ALCdsoundPlayback_open(ALCdsoundPlayback *self, const ALCchar *name);
ALCboolean ALCdsoundPlayback_reset(ALCdsoundPlayback *self);
ALCboolean ALCdsoundPlayback_start(ALCdsoundPlayback *self);
void ALCdsoundPlayback_stop(ALCdsoundPlayback *self);
DECLARE_FORWARD2(ALCdsoundPlayback, ALCbackend, ALCenum, captureSamples, void*, ALCuint)
DECLARE_FORWARD(ALCdsoundPlayback, ALCbackend, ALCuint, availableSamples)
DECLARE_FORWARD(ALCdsoundPlayback, ALCbackend, ClockLatency, getClockLatency)
DECLARE_FORWARD(ALCdsoundPlayback, ALCbackend, void, lock)
DECLARE_FORWARD(ALCdsoundPlayback, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(ALCdsoundPlayback)

DEFINE_ALCBACKEND_VTABLE(ALCdsoundPlayback);


void ALCdsoundPlayback_Construct(ALCdsoundPlayback *self, ALCdevice *device)
{
    new (self) ALCdsoundPlayback{};
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(ALCdsoundPlayback, ALCbackend, self);
}

void ALCdsoundPlayback_Destruct(ALCdsoundPlayback *self)
{
    if(self->mNotifies)
        self->mNotifies->Release();
    self->mNotifies = nullptr;
    if(self->mBuffer)
        self->mBuffer->Release();
    self->mBuffer = nullptr;
    if(self->mPrimaryBuffer)
        self->mPrimaryBuffer->Release();
    self->mPrimaryBuffer = nullptr;

    if(self->mDS)
        self->mDS->Release();
    self->mDS = nullptr;
    if(self->mNotifyEvent)
        CloseHandle(self->mNotifyEvent);
    self->mNotifyEvent = nullptr;

    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
    self->~ALCdsoundPlayback();
}


FORCE_ALIGN int ALCdsoundPlayback_mixerProc(ALCdsoundPlayback *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;

    SetRTPriority();
    althrd_setname(MIXER_THREAD_NAME);

    IDirectSoundBuffer *const Buffer{self->mBuffer};

    DSBCAPS DSBCaps{};
    DSBCaps.dwSize = sizeof(DSBCaps);
    HRESULT err{Buffer->GetCaps(&DSBCaps)};
    if(FAILED(err))
    {
        ERR("Failed to get buffer caps: 0x%lx\n", err);
        ALCdsoundPlayback_lock(self);
        aluHandleDisconnect(device, "Failure retrieving playback buffer info: 0x%lx", err);
        ALCdsoundPlayback_unlock(self);
        return 1;
    }

    ALsizei FrameSize{device->frameSizeFromFmt()};
    DWORD FragSize{device->UpdateSize * FrameSize};

    bool Playing{false};
    DWORD LastCursor{0u};
    Buffer->GetCurrentPosition(&LastCursor, nullptr);
    while(!self->mKillNow.load(std::memory_order_acquire) &&
          device->Connected.load(std::memory_order_acquire))
    {
        // Get current play cursor
        DWORD PlayCursor;
        Buffer->GetCurrentPosition(&PlayCursor, nullptr);
        DWORD avail = (PlayCursor-LastCursor+DSBCaps.dwBufferBytes) % DSBCaps.dwBufferBytes;

        if(avail < FragSize)
        {
            if(!Playing)
            {
                err = Buffer->Play(0, 0, DSBPLAY_LOOPING);
                if(FAILED(err))
                {
                    ERR("Failed to play buffer: 0x%lx\n", err);
                    ALCdsoundPlayback_lock(self);
                    aluHandleDisconnect(device, "Failure starting playback: 0x%lx", err);
                    ALCdsoundPlayback_unlock(self);
                    return 1;
                }
                Playing = true;
            }

            avail = WaitForSingleObjectEx(self->mNotifyEvent, 2000, FALSE);
            if(avail != WAIT_OBJECT_0)
                ERR("WaitForSingleObjectEx error: 0x%lx\n", avail);
            continue;
        }
        avail -= avail%FragSize;

        // Lock output buffer
        void *WritePtr1, *WritePtr2;
        DWORD WriteCnt1{0u},  WriteCnt2{0u};
        err = Buffer->Lock(LastCursor, avail, &WritePtr1, &WriteCnt1, &WritePtr2, &WriteCnt2, 0);

        // If the buffer is lost, restore it and lock
        if(err == DSERR_BUFFERLOST)
        {
            WARN("Buffer lost, restoring...\n");
            err = Buffer->Restore();
            if(SUCCEEDED(err))
            {
                Playing = false;
                LastCursor = 0;
                err = Buffer->Lock(0, DSBCaps.dwBufferBytes, &WritePtr1, &WriteCnt1,
                                   &WritePtr2, &WriteCnt2, 0);
            }
        }

        // Successfully locked the output buffer
        if(SUCCEEDED(err))
        {
            // If we have an active context, mix data directly into output buffer otherwise fill with silence
            ALCdsoundPlayback_lock(self);
            aluMixData(device, WritePtr1, WriteCnt1/FrameSize);
            aluMixData(device, WritePtr2, WriteCnt2/FrameSize);
            ALCdsoundPlayback_unlock(self);

            // Unlock output buffer only when successfully locked
            Buffer->Unlock(WritePtr1, WriteCnt1, WritePtr2, WriteCnt2);
        }
        else
        {
            ERR("Buffer lock error: %#lx\n", err);
            ALCdsoundPlayback_lock(self);
            aluHandleDisconnect(device, "Failed to lock output buffer: 0x%lx", err);
            ALCdsoundPlayback_unlock(self);
            return 1;
        }

        // Update old write cursor location
        LastCursor += WriteCnt1+WriteCnt2;
        LastCursor %= DSBCaps.dwBufferBytes;
    }

    return 0;
}

ALCenum ALCdsoundPlayback_open(ALCdsoundPlayback *self, const ALCchar *deviceName)
{
    ALCdevice *device{STATIC_CAST(ALCbackend, self)->mDevice};

    HRESULT hr;
    if(PlaybackDevices.empty())
    {
        /* Initialize COM to prevent name truncation */
        HRESULT hrcom{CoInitialize(nullptr)};
        hr = DirectSoundEnumerateW(DSoundEnumDevices, &PlaybackDevices);
        if(FAILED(hr))
            ERR("Error enumerating DirectSound devices (0x%lx)!\n", hr);
        if(SUCCEEDED(hrcom))
            CoUninitialize();
    }

    const GUID *guid{nullptr};
    if(!deviceName && !PlaybackDevices.empty())
    {
        deviceName = PlaybackDevices[0].name.c_str();
        guid = &PlaybackDevices[0].guid;
    }
    else
    {
        auto iter = std::find_if(PlaybackDevices.cbegin(), PlaybackDevices.cend(),
            [deviceName](const DevMap &entry) -> bool
            { return entry.name == deviceName; }
        );
        if(iter == PlaybackDevices.cend())
            return ALC_INVALID_VALUE;
        guid = &iter->guid;
    }

    hr = DS_OK;
    self->mNotifyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if(!self->mNotifyEvent) hr = E_FAIL;

    //DirectSound Init code
    if(SUCCEEDED(hr))
        hr = DirectSoundCreate(guid, &self->mDS, nullptr);
    if(SUCCEEDED(hr))
        hr = self->mDS->SetCooperativeLevel(GetForegroundWindow(), DSSCL_PRIORITY);
    if(FAILED(hr))
    {
        ERR("Device init failed: 0x%08lx\n", hr);
        return ALC_INVALID_VALUE;
    }

    device->DeviceName = deviceName;
    return ALC_NO_ERROR;
}

ALCboolean ALCdsoundPlayback_reset(ALCdsoundPlayback *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;

    if(self->mNotifies)
        self->mNotifies->Release();
    self->mNotifies = nullptr;
    if(self->mBuffer)
        self->mBuffer->Release();
    self->mBuffer = nullptr;
    if(self->mPrimaryBuffer)
        self->mPrimaryBuffer->Release();
    self->mPrimaryBuffer = nullptr;

    switch(device->FmtType)
    {
        case DevFmtByte:
            device->FmtType = DevFmtUByte;
            break;
        case DevFmtFloat:
            if((device->Flags&DEVICE_SAMPLE_TYPE_REQUEST))
                break;
            /* fall-through */
        case DevFmtUShort:
            device->FmtType = DevFmtShort;
            break;
        case DevFmtUInt:
            device->FmtType = DevFmtInt;
            break;
        case DevFmtUByte:
        case DevFmtShort:
        case DevFmtInt:
            break;
    }

    WAVEFORMATEXTENSIBLE OutputType{};
    DWORD speakers;
    HRESULT hr{self->mDS->GetSpeakerConfig(&speakers)};
    if(SUCCEEDED(hr))
    {
        speakers = DSSPEAKER_CONFIG(speakers);
        if(!(device->Flags&DEVICE_CHANNELS_REQUEST))
        {
            if(speakers == DSSPEAKER_MONO)
                device->FmtChans = DevFmtMono;
            else if(speakers == DSSPEAKER_STEREO || speakers == DSSPEAKER_HEADPHONE)
                device->FmtChans = DevFmtStereo;
            else if(speakers == DSSPEAKER_QUAD)
                device->FmtChans = DevFmtQuad;
            else if(speakers == DSSPEAKER_5POINT1_SURROUND)
                device->FmtChans = DevFmtX51;
            else if(speakers == DSSPEAKER_5POINT1_BACK)
                device->FmtChans = DevFmtX51Rear;
            else if(speakers == DSSPEAKER_7POINT1 || speakers == DSSPEAKER_7POINT1_SURROUND)
                device->FmtChans = DevFmtX71;
            else
                ERR("Unknown system speaker config: 0x%lx\n", speakers);
        }
        device->IsHeadphones = (device->FmtChans == DevFmtStereo &&
                                speakers == DSSPEAKER_HEADPHONE);

        switch(device->FmtChans)
        {
            case DevFmtMono:
                OutputType.dwChannelMask = SPEAKER_FRONT_CENTER;
                break;
            case DevFmtAmbi3D:
                device->FmtChans = DevFmtStereo;
                /*fall-through*/
            case DevFmtStereo:
                OutputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                           SPEAKER_FRONT_RIGHT;
                break;
            case DevFmtQuad:
                OutputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                           SPEAKER_FRONT_RIGHT |
                                           SPEAKER_BACK_LEFT |
                                           SPEAKER_BACK_RIGHT;
                break;
            case DevFmtX51:
                OutputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                           SPEAKER_FRONT_RIGHT |
                                           SPEAKER_FRONT_CENTER |
                                           SPEAKER_LOW_FREQUENCY |
                                           SPEAKER_SIDE_LEFT |
                                           SPEAKER_SIDE_RIGHT;
                break;
            case DevFmtX51Rear:
                OutputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                           SPEAKER_FRONT_RIGHT |
                                           SPEAKER_FRONT_CENTER |
                                           SPEAKER_LOW_FREQUENCY |
                                           SPEAKER_BACK_LEFT |
                                           SPEAKER_BACK_RIGHT;
                break;
            case DevFmtX61:
                OutputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                           SPEAKER_FRONT_RIGHT |
                                           SPEAKER_FRONT_CENTER |
                                           SPEAKER_LOW_FREQUENCY |
                                           SPEAKER_BACK_CENTER |
                                           SPEAKER_SIDE_LEFT |
                                           SPEAKER_SIDE_RIGHT;
                break;
            case DevFmtX71:
                OutputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                           SPEAKER_FRONT_RIGHT |
                                           SPEAKER_FRONT_CENTER |
                                           SPEAKER_LOW_FREQUENCY |
                                           SPEAKER_BACK_LEFT |
                                           SPEAKER_BACK_RIGHT |
                                           SPEAKER_SIDE_LEFT |
                                           SPEAKER_SIDE_RIGHT;
                break;
        }

retry_open:
        hr = S_OK;
        OutputType.Format.wFormatTag = WAVE_FORMAT_PCM;
        OutputType.Format.nChannels = device->channelsFromFmt();
        OutputType.Format.wBitsPerSample = device->bytesFromFmt() * 8;
        OutputType.Format.nBlockAlign = OutputType.Format.nChannels*OutputType.Format.wBitsPerSample/8;
        OutputType.Format.nSamplesPerSec = device->Frequency;
        OutputType.Format.nAvgBytesPerSec = OutputType.Format.nSamplesPerSec*OutputType.Format.nBlockAlign;
        OutputType.Format.cbSize = 0;
    }

    if(OutputType.Format.nChannels > 2 || device->FmtType == DevFmtFloat)
    {
        OutputType.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        OutputType.Samples.wValidBitsPerSample = OutputType.Format.wBitsPerSample;
        OutputType.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        if(device->FmtType == DevFmtFloat)
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        else
            OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

        if(self->mPrimaryBuffer)
            self->mPrimaryBuffer->Release();
        self->mPrimaryBuffer = nullptr;
    }
    else
    {
        if(SUCCEEDED(hr) && !self->mPrimaryBuffer)
        {
            DSBUFFERDESC DSBDescription{};
            DSBDescription.dwSize = sizeof(DSBDescription);
            DSBDescription.dwFlags = DSBCAPS_PRIMARYBUFFER;
            hr = self->mDS->CreateSoundBuffer(&DSBDescription, &self->mPrimaryBuffer, nullptr);
        }
        if(SUCCEEDED(hr))
            hr = self->mPrimaryBuffer->SetFormat(&OutputType.Format);
    }

    if(SUCCEEDED(hr))
    {
        if(device->NumUpdates > MAX_UPDATES)
        {
            device->UpdateSize = (device->UpdateSize*device->NumUpdates +
                                  MAX_UPDATES-1) / MAX_UPDATES;
            device->NumUpdates = MAX_UPDATES;
        }

        DSBUFFERDESC DSBDescription{};
        DSBDescription.dwSize = sizeof(DSBDescription);
        DSBDescription.dwFlags = DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GETCURRENTPOSITION2 |
                                 DSBCAPS_GLOBALFOCUS;
        DSBDescription.dwBufferBytes = device->UpdateSize * device->NumUpdates *
                                       OutputType.Format.nBlockAlign;
        DSBDescription.lpwfxFormat = &OutputType.Format;

        hr = self->mDS->CreateSoundBuffer(&DSBDescription, &self->mBuffer, nullptr);
        if(FAILED(hr) && device->FmtType == DevFmtFloat)
        {
            device->FmtType = DevFmtShort;
            goto retry_open;
        }
    }

    if(SUCCEEDED(hr))
    {
        void *ptr;
        hr = self->mBuffer->QueryInterface(IID_IDirectSoundNotify, &ptr);
        if(SUCCEEDED(hr))
        {
            auto Notifies = static_cast<IDirectSoundNotify*>(ptr);
            self->mNotifies = Notifies;

            device->NumUpdates = minu(device->NumUpdates, MAX_UPDATES);

            std::array<DSBPOSITIONNOTIFY,MAX_UPDATES> nots;
            for(ALuint i{0};i < device->NumUpdates;++i)
            {
                nots[i].dwOffset = i * device->UpdateSize * OutputType.Format.nBlockAlign;
                nots[i].hEventNotify = self->mNotifyEvent;
            }
            if(Notifies->SetNotificationPositions(device->NumUpdates, nots.data()) != DS_OK)
                hr = E_FAIL;
        }
    }

    if(FAILED(hr))
    {
        if(self->mNotifies)
            self->mNotifies->Release();
        self->mNotifies = nullptr;
        if(self->mBuffer)
            self->mBuffer->Release();
        self->mBuffer = nullptr;
        if(self->mPrimaryBuffer)
            self->mPrimaryBuffer->Release();
        self->mPrimaryBuffer = nullptr;
        return ALC_FALSE;
    }

    ResetEvent(self->mNotifyEvent);
    SetDefaultWFXChannelOrder(device);

    return ALC_TRUE;
}

ALCboolean ALCdsoundPlayback_start(ALCdsoundPlayback *self)
{
    try {
        self->mKillNow.store(AL_FALSE, std::memory_order_release);
        self->mThread = std::thread(ALCdsoundPlayback_mixerProc, self);
        return ALC_TRUE;
    }
    catch(std::exception& e) {
        ERR("Failed to start mixing thread: %s\n", e.what());
    }
    catch(...) {
    }
    return ALC_FALSE;
}

void ALCdsoundPlayback_stop(ALCdsoundPlayback *self)
{
    if(self->mKillNow.exchange(AL_TRUE, std::memory_order_acq_rel) || !self->mThread.joinable())
        return;

    self->mThread.join();

    self->mBuffer->Stop();
}


struct ALCdsoundCapture final : public ALCbackend {
    IDirectSoundCapture *mDSC{nullptr};
    IDirectSoundCaptureBuffer *mDSCbuffer{nullptr};
    DWORD mBufferBytes{0u};
    DWORD mCursor{0u};

    RingBufferPtr mRing;
};

void ALCdsoundCapture_Construct(ALCdsoundCapture *self, ALCdevice *device);
void ALCdsoundCapture_Destruct(ALCdsoundCapture *self);
ALCenum ALCdsoundCapture_open(ALCdsoundCapture *self, const ALCchar *name);
DECLARE_FORWARD(ALCdsoundCapture, ALCbackend, ALCboolean, reset)
ALCboolean ALCdsoundCapture_start(ALCdsoundCapture *self);
void ALCdsoundCapture_stop(ALCdsoundCapture *self);
ALCenum ALCdsoundCapture_captureSamples(ALCdsoundCapture *self, ALCvoid *buffer, ALCuint samples);
ALCuint ALCdsoundCapture_availableSamples(ALCdsoundCapture *self);
DECLARE_FORWARD(ALCdsoundCapture, ALCbackend, ClockLatency, getClockLatency)
DECLARE_FORWARD(ALCdsoundCapture, ALCbackend, void, lock)
DECLARE_FORWARD(ALCdsoundCapture, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(ALCdsoundCapture)
DEFINE_ALCBACKEND_VTABLE(ALCdsoundCapture);

void ALCdsoundCapture_Construct(ALCdsoundCapture *self, ALCdevice *device)
{
    new (self) ALCdsoundCapture{};
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(ALCdsoundCapture, ALCbackend, self);
}

void ALCdsoundCapture_Destruct(ALCdsoundCapture *self)
{
    if(self->mDSCbuffer)
    {
        self->mDSCbuffer->Stop();
        self->mDSCbuffer->Release();
        self->mDSCbuffer = nullptr;
    }

    if(self->mDSC)
        self->mDSC->Release();
    self->mDSC = nullptr;

    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
    self->~ALCdsoundCapture();
}


ALCenum ALCdsoundCapture_open(ALCdsoundCapture *self, const ALCchar *deviceName)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;

    HRESULT hr;
    if(CaptureDevices.empty())
    {
        /* Initialize COM to prevent name truncation */
        HRESULT hrcom{CoInitialize(nullptr)};
        hr = DirectSoundCaptureEnumerateW(DSoundEnumDevices, &CaptureDevices);
        if(FAILED(hr))
            ERR("Error enumerating DirectSound devices (0x%lx)!\n", hr);
        if(SUCCEEDED(hrcom))
            CoUninitialize();
    }

    const GUID *guid{nullptr};
    if(!deviceName && !CaptureDevices.empty())
    {
        deviceName = CaptureDevices[0].name.c_str();
        guid = &CaptureDevices[0].guid;
    }
    else
    {
        auto iter = std::find_if(CaptureDevices.cbegin(), CaptureDevices.cend(),
            [deviceName](const DevMap &entry) -> bool
            { return entry.name == deviceName; }
        );
        if(iter == CaptureDevices.cend())
            return ALC_INVALID_VALUE;
        guid = &iter->guid;
    }

    switch(device->FmtType)
    {
        case DevFmtByte:
        case DevFmtUShort:
        case DevFmtUInt:
            WARN("%s capture samples not supported\n", DevFmtTypeString(device->FmtType));
            return ALC_INVALID_ENUM;

        case DevFmtUByte:
        case DevFmtShort:
        case DevFmtInt:
        case DevFmtFloat:
            break;
    }

    WAVEFORMATEXTENSIBLE InputType{};
    switch(device->FmtChans)
    {
        case DevFmtMono:
            InputType.dwChannelMask = SPEAKER_FRONT_CENTER;
            break;
        case DevFmtStereo:
            InputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                      SPEAKER_FRONT_RIGHT;
            break;
        case DevFmtQuad:
            InputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                      SPEAKER_FRONT_RIGHT |
                                      SPEAKER_BACK_LEFT |
                                      SPEAKER_BACK_RIGHT;
            break;
        case DevFmtX51:
            InputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                      SPEAKER_FRONT_RIGHT |
                                      SPEAKER_FRONT_CENTER |
                                      SPEAKER_LOW_FREQUENCY |
                                      SPEAKER_SIDE_LEFT |
                                      SPEAKER_SIDE_RIGHT;
            break;
        case DevFmtX51Rear:
            InputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                      SPEAKER_FRONT_RIGHT |
                                      SPEAKER_FRONT_CENTER |
                                      SPEAKER_LOW_FREQUENCY |
                                      SPEAKER_BACK_LEFT |
                                      SPEAKER_BACK_RIGHT;
            break;
        case DevFmtX61:
            InputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                      SPEAKER_FRONT_RIGHT |
                                      SPEAKER_FRONT_CENTER |
                                      SPEAKER_LOW_FREQUENCY |
                                      SPEAKER_BACK_CENTER |
                                      SPEAKER_SIDE_LEFT |
                                      SPEAKER_SIDE_RIGHT;
            break;
        case DevFmtX71:
            InputType.dwChannelMask = SPEAKER_FRONT_LEFT |
                                      SPEAKER_FRONT_RIGHT |
                                      SPEAKER_FRONT_CENTER |
                                      SPEAKER_LOW_FREQUENCY |
                                      SPEAKER_BACK_LEFT |
                                      SPEAKER_BACK_RIGHT |
                                      SPEAKER_SIDE_LEFT |
                                      SPEAKER_SIDE_RIGHT;
            break;
        case DevFmtAmbi3D:
            WARN("%s capture not supported\n", DevFmtChannelsString(device->FmtChans));
            return ALC_INVALID_ENUM;
    }

    InputType.Format.wFormatTag = WAVE_FORMAT_PCM;
    InputType.Format.nChannels = device->channelsFromFmt();
    InputType.Format.wBitsPerSample = device->bytesFromFmt() * 8;
    InputType.Format.nBlockAlign = InputType.Format.nChannels*InputType.Format.wBitsPerSample/8;
    InputType.Format.nSamplesPerSec = device->Frequency;
    InputType.Format.nAvgBytesPerSec = InputType.Format.nSamplesPerSec*InputType.Format.nBlockAlign;
    InputType.Format.cbSize = 0;
    InputType.Samples.wValidBitsPerSample = InputType.Format.wBitsPerSample;
    if(device->FmtType == DevFmtFloat)
        InputType.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    else
        InputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

    if(InputType.Format.nChannels > 2 || device->FmtType == DevFmtFloat)
    {
        InputType.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        InputType.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    }

    ALuint samples{device->UpdateSize * device->NumUpdates};
    samples = maxu(samples, 100 * device->Frequency / 1000);

    DSCBUFFERDESC DSCBDescription{};
    DSCBDescription.dwSize = sizeof(DSCBDescription);
    DSCBDescription.dwFlags = 0;
    DSCBDescription.dwBufferBytes = samples * InputType.Format.nBlockAlign;
    DSCBDescription.lpwfxFormat = &InputType.Format;

    //DirectSoundCapture Init code
    hr = DirectSoundCaptureCreate(guid, &self->mDSC, nullptr);
    if(SUCCEEDED(hr))
        self->mDSC->CreateCaptureBuffer(&DSCBDescription, &self->mDSCbuffer, nullptr);
    if(SUCCEEDED(hr))
    {
         self->mRing = CreateRingBuffer(device->UpdateSize*device->NumUpdates,
            InputType.Format.nBlockAlign, false);
         if(!self->mRing) hr = DSERR_OUTOFMEMORY;
    }

    if(FAILED(hr))
    {
        ERR("Device init failed: 0x%08lx\n", hr);

        self->mRing = nullptr;
        if(self->mDSCbuffer)
            self->mDSCbuffer->Release();
        self->mDSCbuffer = nullptr;
        if(self->mDSC)
            self->mDSC->Release();
        self->mDSC = nullptr;

        return ALC_INVALID_VALUE;
    }

    self->mBufferBytes = DSCBDescription.dwBufferBytes;
    SetDefaultWFXChannelOrder(device);

    device->DeviceName = deviceName;
    return ALC_NO_ERROR;
}

ALCboolean ALCdsoundCapture_start(ALCdsoundCapture *self)
{
    HRESULT hr{self->mDSCbuffer->Start(DSCBSTART_LOOPING)};
    if(FAILED(hr))
    {
        ERR("start failed: 0x%08lx\n", hr);
        aluHandleDisconnect(STATIC_CAST(ALCbackend, self)->mDevice,
                            "Failure starting capture: 0x%lx", hr);
        return ALC_FALSE;
    }

    return ALC_TRUE;
}

void ALCdsoundCapture_stop(ALCdsoundCapture *self)
{
    HRESULT hr{self->mDSCbuffer->Stop()};
    if(FAILED(hr))
    {
        ERR("stop failed: 0x%08lx\n", hr);
        aluHandleDisconnect(STATIC_CAST(ALCbackend, self)->mDevice,
                            "Failure stopping capture: 0x%lx", hr);
    }
}

ALCenum ALCdsoundCapture_captureSamples(ALCdsoundCapture *self, ALCvoid *buffer, ALCuint samples)
{
    RingBuffer *ring{self->mRing.get()};
    ring->read(buffer, samples);
    return ALC_NO_ERROR;
}

ALCuint ALCdsoundCapture_availableSamples(ALCdsoundCapture *self)
{
    ALCdevice *device{self->mDevice};
    RingBuffer *ring{self->mRing.get()};

    if(!device->Connected.load(std::memory_order_acquire))
        return static_cast<ALCuint>(ring->readSpace());

    ALsizei FrameSize{device->frameSizeFromFmt()};
    DWORD BufferBytes{self->mBufferBytes};
    DWORD LastCursor{self->mCursor};

    DWORD ReadCursor;
    void *ReadPtr1, *ReadPtr2;
    DWORD ReadCnt1,  ReadCnt2;
    HRESULT hr{self->mDSCbuffer->GetCurrentPosition(nullptr, &ReadCursor)};
    if(SUCCEEDED(hr))
    {
        DWORD NumBytes{(ReadCursor-LastCursor + BufferBytes) % BufferBytes};
        if(!NumBytes) return static_cast<ALCubyte>(ring->readSpace());
        hr = self->mDSCbuffer->Lock(LastCursor, NumBytes, &ReadPtr1, &ReadCnt1,
            &ReadPtr2, &ReadCnt2, 0);
    }
    if(SUCCEEDED(hr))
    {
        ring->write(ReadPtr1, ReadCnt1/FrameSize);
        if(ReadPtr2 != nullptr && ReadCnt2 > 0)
            ring->write(ReadPtr2, ReadCnt2/FrameSize);
        hr = self->mDSCbuffer->Unlock(ReadPtr1, ReadCnt1, ReadPtr2, ReadCnt2);
        self->mCursor = (LastCursor+ReadCnt1+ReadCnt2) % BufferBytes;
    }

    if(FAILED(hr))
    {
        ERR("update failed: 0x%08lx\n", hr);
        aluHandleDisconnect(device, "Failure retrieving capture data: 0x%lx", hr);
    }

    return static_cast<ALCuint>(ring->readSpace());
}

} // namespace


BackendFactory &DSoundBackendFactory::getFactory()
{
    static DSoundBackendFactory factory{};
    return factory;
}

bool DSoundBackendFactory::init()
{ return DSoundLoad(); }

void DSoundBackendFactory::deinit()
{
    PlaybackDevices.clear();
    CaptureDevices.clear();

#ifdef HAVE_DYNLOAD
    if(ds_handle)
        CloseLib(ds_handle);
    ds_handle = nullptr;
#endif
}

bool DSoundBackendFactory::querySupport(ALCbackend_Type type)
{ return (type == ALCbackend_Playback || type == ALCbackend_Capture); }

void DSoundBackendFactory::probe(DevProbe type, std::string *outnames)
{
    auto add_device = [outnames](const DevMap &entry) -> void
    {
        /* +1 to also append the null char (to ensure a null-separated list and
         * double-null terminated list).
         */
        outnames->append(entry.name.c_str(), entry.name.length()+1);
    };

    /* Initialize COM to prevent name truncation */
    HRESULT hr;
    HRESULT hrcom{CoInitialize(nullptr)};
    switch(type)
    {
        case ALL_DEVICE_PROBE:
            PlaybackDevices.clear();
            hr = DirectSoundEnumerateW(DSoundEnumDevices, &PlaybackDevices);
            if(FAILED(hr))
                ERR("Error enumerating DirectSound playback devices (0x%lx)!\n", hr);
            std::for_each(PlaybackDevices.cbegin(), PlaybackDevices.cend(), add_device);
            break;

        case CAPTURE_DEVICE_PROBE:
            CaptureDevices.clear();
            hr = DirectSoundCaptureEnumerateW(DSoundEnumDevices, &CaptureDevices);
            if(FAILED(hr))
                ERR("Error enumerating DirectSound capture devices (0x%lx)!\n", hr);
            std::for_each(CaptureDevices.cbegin(), CaptureDevices.cend(), add_device);
            break;
    }
    if(SUCCEEDED(hrcom))
        CoUninitialize();
}

ALCbackend *DSoundBackendFactory::createBackend(ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        ALCdsoundPlayback *backend;
        NEW_OBJ(backend, ALCdsoundPlayback)(device);
        if(!backend) return nullptr;
        return STATIC_CAST(ALCbackend, backend);
    }

    if(type == ALCbackend_Capture)
    {
        ALCdsoundCapture *backend;
        NEW_OBJ(backend, ALCdsoundCapture)(device);
        if(!backend) return nullptr;
        return STATIC_CAST(ALCbackend, backend);
    }

    return nullptr;
}
