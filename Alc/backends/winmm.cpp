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

#include "backends/winmm.h"

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#include <windows.h>
#include <mmsystem.h>

#include <array>
#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include <algorithm>

#include "alMain.h"
#include "alu.h"
#include "ringbuffer.h"
#include "threads.h"
#include "compat.h"

#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT  0x0003
#endif

namespace {

#define DEVNAME_HEAD "OpenAL Soft on "


al::vector<std::string> PlaybackDevices;
al::vector<std::string> CaptureDevices;

bool checkName(const al::vector<std::string> &list, const std::string &name)
{ return std::find(list.cbegin(), list.cend(), name) != list.cend(); }

void ProbePlaybackDevices(void)
{
    PlaybackDevices.clear();

    ALuint numdevs{waveOutGetNumDevs()};
    PlaybackDevices.reserve(numdevs);
    for(ALuint i{0};i < numdevs;i++)
    {
        std::string dname;

        WAVEOUTCAPSW WaveCaps{};
        if(waveOutGetDevCapsW(i, &WaveCaps, sizeof(WaveCaps)) == MMSYSERR_NOERROR)
        {
            const std::string basename{DEVNAME_HEAD + wstr_to_utf8(WaveCaps.szPname)};

            int count{1};
            std::string newname{basename};
            while(checkName(PlaybackDevices, newname))
            {
                newname = basename;
                newname += " #";
                newname += std::to_string(++count);
            }
            dname = std::move(newname);

            TRACE("Got device \"%s\", ID %u\n", dname.c_str(), i);
        }
        PlaybackDevices.emplace_back(std::move(dname));
    }
}

void ProbeCaptureDevices(void)
{
    CaptureDevices.clear();

    ALuint numdevs{waveInGetNumDevs()};
    CaptureDevices.reserve(numdevs);
    for(ALuint i{0};i < numdevs;i++)
    {
        std::string dname;

        WAVEINCAPSW WaveCaps{};
        if(waveInGetDevCapsW(i, &WaveCaps, sizeof(WaveCaps)) == MMSYSERR_NOERROR)
        {
            const std::string basename{DEVNAME_HEAD + wstr_to_utf8(WaveCaps.szPname)};

            int count{1};
            std::string newname{basename};
            while(checkName(CaptureDevices, newname))
            {
                newname = basename;
                newname += " #";
                newname += std::to_string(++count);
            }
            dname = std::move(newname);

            TRACE("Got device \"%s\", ID %u\n", dname.c_str(), i);
        }
        CaptureDevices.emplace_back(std::move(dname));
    }
}


struct ALCwinmmPlayback final : public ALCbackend {
    std::atomic<ALuint> Writable{0u};
    al::semaphore Sem;
    int Idx{0};
    std::array<WAVEHDR,4> WaveBuffer;

    HWAVEOUT OutHdl{nullptr};

    WAVEFORMATEX Format{};

    std::atomic<ALenum> mKillNow{AL_TRUE};
    std::thread mThread;
};

void ALCwinmmPlayback_Construct(ALCwinmmPlayback *self, ALCdevice *device);
void ALCwinmmPlayback_Destruct(ALCwinmmPlayback *self);

void CALLBACK ALCwinmmPlayback_waveOutProc(HWAVEOUT device, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2);
int ALCwinmmPlayback_mixerProc(ALCwinmmPlayback *self);

ALCenum ALCwinmmPlayback_open(ALCwinmmPlayback *self, const ALCchar *name);
ALCboolean ALCwinmmPlayback_reset(ALCwinmmPlayback *self);
ALCboolean ALCwinmmPlayback_start(ALCwinmmPlayback *self);
void ALCwinmmPlayback_stop(ALCwinmmPlayback *self);
DECLARE_FORWARD2(ALCwinmmPlayback, ALCbackend, ALCenum, captureSamples, ALCvoid*, ALCuint)
DECLARE_FORWARD(ALCwinmmPlayback, ALCbackend, ALCuint, availableSamples)
DECLARE_FORWARD(ALCwinmmPlayback, ALCbackend, ClockLatency, getClockLatency)
DECLARE_FORWARD(ALCwinmmPlayback, ALCbackend, void, lock)
DECLARE_FORWARD(ALCwinmmPlayback, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(ALCwinmmPlayback)

DEFINE_ALCBACKEND_VTABLE(ALCwinmmPlayback);


void ALCwinmmPlayback_Construct(ALCwinmmPlayback *self, ALCdevice *device)
{
    new (self) ALCwinmmPlayback{};
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(ALCwinmmPlayback, ALCbackend, self);

    std::fill(self->WaveBuffer.begin(), self->WaveBuffer.end(), WAVEHDR{});
}

void ALCwinmmPlayback_Destruct(ALCwinmmPlayback *self)
{
    if(self->OutHdl)
        waveOutClose(self->OutHdl);
    self->OutHdl = nullptr;

    al_free(self->WaveBuffer[0].lpData);
    std::fill(self->WaveBuffer.begin(), self->WaveBuffer.end(), WAVEHDR{});

    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
    self->~ALCwinmmPlayback();
}


/* ALCwinmmPlayback_waveOutProc
 *
 * Posts a message to 'ALCwinmmPlayback_mixerProc' everytime a WaveOut Buffer
 * is completed and returns to the application (for more data)
 */
void CALLBACK ALCwinmmPlayback_waveOutProc(HWAVEOUT UNUSED(device), UINT msg,
                                           DWORD_PTR instance, DWORD_PTR UNUSED(param1),
                                           DWORD_PTR UNUSED(param2))
{
    if(msg != WOM_DONE)
        return;

    auto self = reinterpret_cast<ALCwinmmPlayback*>(instance);
    self->Writable.fetch_add(1, std::memory_order_acq_rel);
    self->Sem.post();
}

FORCE_ALIGN int ALCwinmmPlayback_mixerProc(ALCwinmmPlayback *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;

    SetRTPriority();
    althrd_setname(MIXER_THREAD_NAME);

    ALCwinmmPlayback_lock(self);
    while(!self->mKillNow.load(std::memory_order_acquire) &&
          device->Connected.load(std::memory_order_acquire))
    {
        ALsizei todo = self->Writable.load(std::memory_order_acquire);
        if(todo < 1)
        {
            ALCwinmmPlayback_unlock(self);
            self->Sem.wait();
            ALCwinmmPlayback_lock(self);
            continue;
        }

        int widx{self->Idx};
        do {
            WAVEHDR &waveHdr = self->WaveBuffer[widx];
            widx = (widx+1) % self->WaveBuffer.size();

            aluMixData(device, waveHdr.lpData, device->UpdateSize);
            self->Writable.fetch_sub(1, std::memory_order_acq_rel);
            waveOutWrite(self->OutHdl, &waveHdr, sizeof(WAVEHDR));
        } while(--todo);
        self->Idx = widx;
    }
    ALCwinmmPlayback_unlock(self);

    return 0;
}


ALCenum ALCwinmmPlayback_open(ALCwinmmPlayback *self, const ALCchar *deviceName)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;

    if(PlaybackDevices.empty())
        ProbePlaybackDevices();

    // Find the Device ID matching the deviceName if valid
    auto iter = deviceName ?
        std::find(PlaybackDevices.cbegin(), PlaybackDevices.cend(), deviceName) :
        PlaybackDevices.cbegin();
    if(iter == PlaybackDevices.cend()) return ALC_INVALID_VALUE;
    UINT DeviceID{static_cast<UINT>(std::distance(PlaybackDevices.cbegin(), iter))};

retry_open:
    self->Format = WAVEFORMATEX{};
    if(device->FmtType == DevFmtFloat)
    {
        self->Format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        self->Format.wBitsPerSample = 32;
    }
    else
    {
        self->Format.wFormatTag = WAVE_FORMAT_PCM;
        if(device->FmtType == DevFmtUByte || device->FmtType == DevFmtByte)
            self->Format.wBitsPerSample = 8;
        else
            self->Format.wBitsPerSample = 16;
    }
    self->Format.nChannels = ((device->FmtChans == DevFmtMono) ? 1 : 2);
    self->Format.nBlockAlign = self->Format.wBitsPerSample *
                               self->Format.nChannels / 8;
    self->Format.nSamplesPerSec = device->Frequency;
    self->Format.nAvgBytesPerSec = self->Format.nSamplesPerSec *
                                   self->Format.nBlockAlign;
    self->Format.cbSize = 0;

    MMRESULT res{waveOutOpen(&self->OutHdl, DeviceID, &self->Format,
        (DWORD_PTR)&ALCwinmmPlayback_waveOutProc, (DWORD_PTR)self, CALLBACK_FUNCTION
    )};
    if(res != MMSYSERR_NOERROR)
    {
        if(device->FmtType == DevFmtFloat)
        {
            device->FmtType = DevFmtShort;
            goto retry_open;
        }
        ERR("waveOutOpen failed: %u\n", res);
        return ALC_INVALID_VALUE;
    }

    device->DeviceName = PlaybackDevices[DeviceID];
    return ALC_NO_ERROR;
}

ALCboolean ALCwinmmPlayback_reset(ALCwinmmPlayback *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;

    device->UpdateSize = (ALuint)((ALuint64)device->UpdateSize *
                                  self->Format.nSamplesPerSec /
                                  device->Frequency);
    device->UpdateSize = (device->UpdateSize*device->NumUpdates + 3) / 4;
    device->NumUpdates = 4;
    device->Frequency = self->Format.nSamplesPerSec;

    if(self->Format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        if(self->Format.wBitsPerSample == 32)
            device->FmtType = DevFmtFloat;
        else
        {
            ERR("Unhandled IEEE float sample depth: %d\n", self->Format.wBitsPerSample);
            return ALC_FALSE;
        }
    }
    else if(self->Format.wFormatTag == WAVE_FORMAT_PCM)
    {
        if(self->Format.wBitsPerSample == 16)
            device->FmtType = DevFmtShort;
        else if(self->Format.wBitsPerSample == 8)
            device->FmtType = DevFmtUByte;
        else
        {
            ERR("Unhandled PCM sample depth: %d\n", self->Format.wBitsPerSample);
            return ALC_FALSE;
        }
    }
    else
    {
        ERR("Unhandled format tag: 0x%04x\n", self->Format.wFormatTag);
        return ALC_FALSE;
    }

    if(self->Format.nChannels == 2)
        device->FmtChans = DevFmtStereo;
    else if(self->Format.nChannels == 1)
        device->FmtChans = DevFmtMono;
    else
    {
        ERR("Unhandled channel count: %d\n", self->Format.nChannels);
        return ALC_FALSE;
    }
    SetDefaultWFXChannelOrder(device);

    ALuint BufferSize{device->UpdateSize * device->frameSizeFromFmt()};

    al_free(self->WaveBuffer[0].lpData);
    self->WaveBuffer[0] = WAVEHDR{};
    self->WaveBuffer[0].lpData = static_cast<char*>(al_calloc(16,
        BufferSize * self->WaveBuffer.size()));
    self->WaveBuffer[0].dwBufferLength = BufferSize;
    for(size_t i{1};i < self->WaveBuffer.size();i++)
    {
        self->WaveBuffer[i] = WAVEHDR{};
        self->WaveBuffer[i].lpData = self->WaveBuffer[i-1].lpData +
                                     self->WaveBuffer[i-1].dwBufferLength;
        self->WaveBuffer[i].dwBufferLength = BufferSize;
    }
    self->Idx = 0;

    return ALC_TRUE;
}

ALCboolean ALCwinmmPlayback_start(ALCwinmmPlayback *self)
{
    try {
        std::for_each(self->WaveBuffer.begin(), self->WaveBuffer.end(),
            [self](WAVEHDR &waveHdr) -> void
            { waveOutPrepareHeader(self->OutHdl, &waveHdr, static_cast<UINT>(sizeof(WAVEHDR))); }
        );
        self->Writable.store(static_cast<ALuint>(self->WaveBuffer.size()),
            std::memory_order_release);

        self->mKillNow.store(AL_FALSE, std::memory_order_release);
        self->mThread = std::thread(ALCwinmmPlayback_mixerProc, self);
        return ALC_TRUE;
    }
    catch(std::exception& e) {
        ERR("Failed to start mixing thread: %s\n", e.what());
    }
    catch(...) {
    }
    return ALC_FALSE;
}

void ALCwinmmPlayback_stop(ALCwinmmPlayback *self)
{
    if(self->mKillNow.exchange(AL_TRUE, std::memory_order_acq_rel) || !self->mThread.joinable())
        return;
    self->mThread.join();

    while(self->Writable.load(std::memory_order_acquire) < self->WaveBuffer.size())
        self->Sem.wait();
    std::for_each(self->WaveBuffer.begin(), self->WaveBuffer.end(),
        [self](WAVEHDR &waveHdr) -> void
        { waveOutUnprepareHeader(self->OutHdl, &waveHdr, sizeof(WAVEHDR)); }
    );
    self->Writable.store(0, std::memory_order_release);
}


struct ALCwinmmCapture final : public ALCbackend {
    std::atomic<ALuint> Readable{0u};
    al::semaphore Sem;
    int Idx{0};
    std::array<WAVEHDR,4> WaveBuffer;

    HWAVEIN InHdl{nullptr};

    RingBufferPtr Ring{nullptr};

    WAVEFORMATEX Format{};

    std::atomic<ALenum> mKillNow{AL_TRUE};
    std::thread mThread;
};

void ALCwinmmCapture_Construct(ALCwinmmCapture *self, ALCdevice *device);
void ALCwinmmCapture_Destruct(ALCwinmmCapture *self);

void CALLBACK ALCwinmmCapture_waveInProc(HWAVEIN device, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2);
int ALCwinmmCapture_captureProc(ALCwinmmCapture *self);

ALCenum ALCwinmmCapture_open(ALCwinmmCapture *self, const ALCchar *deviceName);
DECLARE_FORWARD(ALCwinmmCapture, ALCbackend, ALCboolean, reset)
ALCboolean ALCwinmmCapture_start(ALCwinmmCapture *self);
void ALCwinmmCapture_stop(ALCwinmmCapture *self);
ALCenum ALCwinmmCapture_captureSamples(ALCwinmmCapture *self, ALCvoid *buffer, ALCuint samples);
ALCuint ALCwinmmCapture_availableSamples(ALCwinmmCapture *self);
DECLARE_FORWARD(ALCwinmmCapture, ALCbackend, ClockLatency, getClockLatency)
DECLARE_FORWARD(ALCwinmmCapture, ALCbackend, void, lock)
DECLARE_FORWARD(ALCwinmmCapture, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(ALCwinmmCapture)

DEFINE_ALCBACKEND_VTABLE(ALCwinmmCapture);


void ALCwinmmCapture_Construct(ALCwinmmCapture *self, ALCdevice *device)
{
    new (self) ALCwinmmCapture{};
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(ALCwinmmCapture, ALCbackend, self);

    std::fill(self->WaveBuffer.begin(), self->WaveBuffer.end(), WAVEHDR{});
}

void ALCwinmmCapture_Destruct(ALCwinmmCapture *self)
{
    // Close the Wave device
    if(self->InHdl)
        waveInClose(self->InHdl);
    self->InHdl = nullptr;

    al_free(self->WaveBuffer[0].lpData);
    std::fill(self->WaveBuffer.begin(), self->WaveBuffer.end(), WAVEHDR{});

    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
    self->~ALCwinmmCapture();
}


/* ALCwinmmCapture_waveInProc
 *
 * Posts a message to 'ALCwinmmCapture_captureProc' everytime a WaveIn Buffer
 * is completed and returns to the application (with more data).
 */
void CALLBACK ALCwinmmCapture_waveInProc(HWAVEIN UNUSED(device), UINT msg,
                                         DWORD_PTR instance, DWORD_PTR UNUSED(param1),
                                         DWORD_PTR UNUSED(param2))
{
    if(msg != WIM_DATA)
        return;

    auto self = reinterpret_cast<ALCwinmmCapture*>(instance);
    self->Readable.fetch_add(1, std::memory_order_acq_rel);
    self->Sem.post();
}

int ALCwinmmCapture_captureProc(ALCwinmmCapture *self)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;

    althrd_setname(RECORD_THREAD_NAME);

    ALCwinmmCapture_lock(self);
    while(!self->mKillNow.load(std::memory_order_acquire) &&
          device->Connected.load(std::memory_order_acquire))
    {
        ALsizei todo = self->Readable.load(std::memory_order_acquire);
        if(todo < 1)
        {
            ALCwinmmCapture_unlock(self);
            self->Sem.wait();
            ALCwinmmCapture_lock(self);
            continue;
        }

        int widx{self->Idx};
        do {
            WAVEHDR &waveHdr = self->WaveBuffer[widx];
            widx = (widx+1) % self->WaveBuffer.size();

            ll_ringbuffer_write(self->Ring.get(), waveHdr.lpData,
                waveHdr.dwBytesRecorded / self->Format.nBlockAlign
            );
            self->Readable.fetch_sub(1, std::memory_order_acq_rel);
            waveInAddBuffer(self->InHdl, &waveHdr, sizeof(WAVEHDR));
        } while(--todo);
        self->Idx = widx;
    }
    ALCwinmmCapture_unlock(self);

    return 0;
}


ALCenum ALCwinmmCapture_open(ALCwinmmCapture *self, const ALCchar *deviceName)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;

    if(CaptureDevices.empty())
        ProbeCaptureDevices();

    // Find the Device ID matching the deviceName if valid
    auto iter = deviceName ?
        std::find(CaptureDevices.cbegin(), CaptureDevices.cend(), deviceName) :
        CaptureDevices.cbegin();
    if(iter == CaptureDevices.cend()) return ALC_INVALID_VALUE;
    UINT DeviceID{static_cast<UINT>(std::distance(CaptureDevices.cbegin(), iter))};

    switch(device->FmtChans)
    {
        case DevFmtMono:
        case DevFmtStereo:
            break;

        case DevFmtQuad:
        case DevFmtX51:
        case DevFmtX51Rear:
        case DevFmtX61:
        case DevFmtX71:
        case DevFmtAmbi3D:
            return ALC_INVALID_ENUM;
    }

    switch(device->FmtType)
    {
        case DevFmtUByte:
        case DevFmtShort:
        case DevFmtInt:
        case DevFmtFloat:
            break;

        case DevFmtByte:
        case DevFmtUShort:
        case DevFmtUInt:
            return ALC_INVALID_ENUM;
    }

    self->Format = WAVEFORMATEX{};
    self->Format.wFormatTag = (device->FmtType == DevFmtFloat) ?
                              WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
    self->Format.nChannels = device->channelsFromFmt();
    self->Format.wBitsPerSample = device->bytesFromFmt() * 8;
    self->Format.nBlockAlign = self->Format.wBitsPerSample *
                               self->Format.nChannels / 8;
    self->Format.nSamplesPerSec = device->Frequency;
    self->Format.nAvgBytesPerSec = self->Format.nSamplesPerSec *
                                   self->Format.nBlockAlign;
    self->Format.cbSize = 0;

    MMRESULT res{waveInOpen(&self->InHdl, DeviceID, &self->Format,
        (DWORD_PTR)&ALCwinmmCapture_waveInProc, (DWORD_PTR)self, CALLBACK_FUNCTION
    )};
    if(res != MMSYSERR_NOERROR)
    {
        ERR("waveInOpen failed: %u\n", res);
        return ALC_INVALID_VALUE;
    }

    // Ensure each buffer is 50ms each
    DWORD BufferSize{self->Format.nAvgBytesPerSec / 20};
    BufferSize -= (BufferSize % self->Format.nBlockAlign);

    // Allocate circular memory buffer for the captured audio
    // Make sure circular buffer is at least 100ms in size
    auto CapturedDataSize = static_cast<DWORD>(
        std::max<size_t>(device->UpdateSize*device->NumUpdates, BufferSize*self->WaveBuffer.size())
    );

    self->Ring.reset(ll_ringbuffer_create(CapturedDataSize, self->Format.nBlockAlign, false));
    if(!self->Ring) return ALC_INVALID_VALUE;

    al_free(self->WaveBuffer[0].lpData);
    self->WaveBuffer[0] = WAVEHDR{};
    self->WaveBuffer[0].lpData = static_cast<char*>(al_calloc(16, BufferSize*4));
    self->WaveBuffer[0].dwBufferLength = BufferSize;
    for(size_t i{1};i < self->WaveBuffer.size();++i)
    {
        self->WaveBuffer[i] = WAVEHDR{};
        self->WaveBuffer[i].lpData = self->WaveBuffer[i-1].lpData +
                                     self->WaveBuffer[i-1].dwBufferLength;
        self->WaveBuffer[i].dwBufferLength = self->WaveBuffer[i-1].dwBufferLength;
    }

    device->DeviceName = CaptureDevices[DeviceID];
    return ALC_NO_ERROR;
}

ALCboolean ALCwinmmCapture_start(ALCwinmmCapture *self)
{
    try {
        for(size_t i{0};i < self->WaveBuffer.size();++i)
        {
            waveInPrepareHeader(self->InHdl, &self->WaveBuffer[i], sizeof(WAVEHDR));
            waveInAddBuffer(self->InHdl, &self->WaveBuffer[i], sizeof(WAVEHDR));
        }

        self->mKillNow.store(AL_FALSE, std::memory_order_release);
        self->mThread = std::thread(ALCwinmmCapture_captureProc, self);

        waveInStart(self->InHdl);
        return ALC_TRUE;
    }
    catch(std::exception& e) {
        ERR("Failed to start mixing thread: %s\n", e.what());
    }
    catch(...) {
    }
    return ALC_FALSE;
}

void ALCwinmmCapture_stop(ALCwinmmCapture *self)
{
    waveInStop(self->InHdl);

    self->mKillNow.store(AL_TRUE, std::memory_order_release);
    if(self->mThread.joinable())
    {
        self->Sem.post();
        self->mThread.join();
    }

    waveInReset(self->InHdl);
    for(size_t i{0};i < self->WaveBuffer.size();++i)
        waveInUnprepareHeader(self->InHdl, &self->WaveBuffer[i], sizeof(WAVEHDR));

    self->Readable.store(0, std::memory_order_release);
    self->Idx = 0;
}

ALCenum ALCwinmmCapture_captureSamples(ALCwinmmCapture *self, ALCvoid *buffer, ALCuint samples)
{
    ll_ringbuffer_read(self->Ring.get(), buffer, samples);
    return ALC_NO_ERROR;
}

ALCuint ALCwinmmCapture_availableSamples(ALCwinmmCapture *self)
{
    return (ALCuint)ll_ringbuffer_read_space(self->Ring.get());
}

} // namespace


bool WinMMBackendFactory::init()
{ return true; }

void WinMMBackendFactory::deinit()
{
    PlaybackDevices.clear();
    CaptureDevices.clear();
}

bool WinMMBackendFactory::querySupport(ALCbackend_Type type)
{ return (type == ALCbackend_Playback || type == ALCbackend_Capture); }

void WinMMBackendFactory::probe(DevProbe type, std::string *outnames)
{
    auto add_device = [outnames](const std::string &dname) -> void
    {
        /* +1 to also append the null char (to ensure a null-separated list and
         * double-null terminated list).
         */
        if(!dname.empty())
            outnames->append(dname.c_str(), dname.length()+1);
    };
    switch(type)
    {
        case ALL_DEVICE_PROBE:
            ProbePlaybackDevices();
            std::for_each(PlaybackDevices.cbegin(), PlaybackDevices.cend(), add_device);
            break;

        case CAPTURE_DEVICE_PROBE:
            ProbeCaptureDevices();
            std::for_each(CaptureDevices.cbegin(), CaptureDevices.cend(), add_device);
            break;
    }
}

ALCbackend *WinMMBackendFactory::createBackend(ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        ALCwinmmPlayback *backend;
        NEW_OBJ(backend, ALCwinmmPlayback)(device);
        if(!backend) return nullptr;
        return STATIC_CAST(ALCbackend, backend);
    }
    if(type == ALCbackend_Capture)
    {
        ALCwinmmCapture *backend;
        NEW_OBJ(backend, ALCwinmmCapture)(device);
        if(!backend) return nullptr;
        return STATIC_CAST(ALCbackend, backend);
    }

    return nullptr;
}

BackendFactory &WinMMBackendFactory::getFactory()
{
    static WinMMBackendFactory factory{};
    return factory;
}
