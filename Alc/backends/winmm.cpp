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
    std::atomic<ALuint> mWritable{0u};
    al::semaphore mSem;
    int mIdx{0};
    std::array<WAVEHDR,4> mWaveBuffer;

    HWAVEOUT mOutHdl{nullptr};

    WAVEFORMATEX mFormat{};

    std::atomic<ALenum> mKillNow{AL_TRUE};
    std::thread mThread;

    ALCwinmmPlayback(ALCdevice *device) noexcept : ALCbackend{device} { }
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
    new (self) ALCwinmmPlayback{device};
    SET_VTABLE2(ALCwinmmPlayback, ALCbackend, self);

    std::fill(self->mWaveBuffer.begin(), self->mWaveBuffer.end(), WAVEHDR{});
}

void ALCwinmmPlayback_Destruct(ALCwinmmPlayback *self)
{
    if(self->mOutHdl)
        waveOutClose(self->mOutHdl);
    self->mOutHdl = nullptr;

    al_free(self->mWaveBuffer[0].lpData);
    std::fill(self->mWaveBuffer.begin(), self->mWaveBuffer.end(), WAVEHDR{});

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
    self->mWritable.fetch_add(1, std::memory_order_acq_rel);
    self->mSem.post();
}

FORCE_ALIGN int ALCwinmmPlayback_mixerProc(ALCwinmmPlayback *self)
{
    ALCdevice *device{self->mDevice};

    SetRTPriority();
    althrd_setname(MIXER_THREAD_NAME);

    ALCwinmmPlayback_lock(self);
    while(!self->mKillNow.load(std::memory_order_acquire) &&
          device->Connected.load(std::memory_order_acquire))
    {
        ALsizei todo = self->mWritable.load(std::memory_order_acquire);
        if(todo < 1)
        {
            ALCwinmmPlayback_unlock(self);
            self->mSem.wait();
            ALCwinmmPlayback_lock(self);
            continue;
        }

        int widx{self->mIdx};
        do {
            WAVEHDR &waveHdr = self->mWaveBuffer[widx];
            widx = (widx+1) % self->mWaveBuffer.size();

            aluMixData(device, waveHdr.lpData, device->UpdateSize);
            self->mWritable.fetch_sub(1, std::memory_order_acq_rel);
            waveOutWrite(self->mOutHdl, &waveHdr, sizeof(WAVEHDR));
        } while(--todo);
        self->mIdx = widx;
    }
    ALCwinmmPlayback_unlock(self);

    return 0;
}


ALCenum ALCwinmmPlayback_open(ALCwinmmPlayback *self, const ALCchar *deviceName)
{
    ALCdevice *device{self->mDevice};

    if(PlaybackDevices.empty())
        ProbePlaybackDevices();

    // Find the Device ID matching the deviceName if valid
    auto iter = deviceName ?
        std::find(PlaybackDevices.cbegin(), PlaybackDevices.cend(), deviceName) :
        PlaybackDevices.cbegin();
    if(iter == PlaybackDevices.cend()) return ALC_INVALID_VALUE;
    auto DeviceID = static_cast<UINT>(std::distance(PlaybackDevices.cbegin(), iter));

retry_open:
    self->mFormat = WAVEFORMATEX{};
    if(device->FmtType == DevFmtFloat)
    {
        self->mFormat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        self->mFormat.wBitsPerSample = 32;
    }
    else
    {
        self->mFormat.wFormatTag = WAVE_FORMAT_PCM;
        if(device->FmtType == DevFmtUByte || device->FmtType == DevFmtByte)
            self->mFormat.wBitsPerSample = 8;
        else
            self->mFormat.wBitsPerSample = 16;
    }
    self->mFormat.nChannels = ((device->FmtChans == DevFmtMono) ? 1 : 2);
    self->mFormat.nBlockAlign = self->mFormat.wBitsPerSample *
                                self->mFormat.nChannels / 8;
    self->mFormat.nSamplesPerSec = device->Frequency;
    self->mFormat.nAvgBytesPerSec = self->mFormat.nSamplesPerSec *
                                    self->mFormat.nBlockAlign;
    self->mFormat.cbSize = 0;

    MMRESULT res{waveOutOpen(&self->mOutHdl, DeviceID, &self->mFormat,
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
    ALCdevice *device{self->mDevice};

    device->UpdateSize = static_cast<ALuint>(
        (ALuint64)device->UpdateSize * self->mFormat.nSamplesPerSec / device->Frequency);
    device->UpdateSize = (device->UpdateSize*device->NumUpdates + 3) / 4;
    device->NumUpdates = 4;
    device->Frequency = self->mFormat.nSamplesPerSec;

    if(self->mFormat.wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        if(self->mFormat.wBitsPerSample == 32)
            device->FmtType = DevFmtFloat;
        else
        {
            ERR("Unhandled IEEE float sample depth: %d\n", self->mFormat.wBitsPerSample);
            return ALC_FALSE;
        }
    }
    else if(self->mFormat.wFormatTag == WAVE_FORMAT_PCM)
    {
        if(self->mFormat.wBitsPerSample == 16)
            device->FmtType = DevFmtShort;
        else if(self->mFormat.wBitsPerSample == 8)
            device->FmtType = DevFmtUByte;
        else
        {
            ERR("Unhandled PCM sample depth: %d\n", self->mFormat.wBitsPerSample);
            return ALC_FALSE;
        }
    }
    else
    {
        ERR("Unhandled format tag: 0x%04x\n", self->mFormat.wFormatTag);
        return ALC_FALSE;
    }

    if(self->mFormat.nChannels == 2)
        device->FmtChans = DevFmtStereo;
    else if(self->mFormat.nChannels == 1)
        device->FmtChans = DevFmtMono;
    else
    {
        ERR("Unhandled channel count: %d\n", self->mFormat.nChannels);
        return ALC_FALSE;
    }
    SetDefaultWFXChannelOrder(device);

    ALuint BufferSize{device->UpdateSize * device->frameSizeFromFmt()};

    al_free(self->mWaveBuffer[0].lpData);
    self->mWaveBuffer[0] = WAVEHDR{};
    self->mWaveBuffer[0].lpData = static_cast<char*>(al_calloc(16,
        BufferSize * self->mWaveBuffer.size()));
    self->mWaveBuffer[0].dwBufferLength = BufferSize;
    for(size_t i{1};i < self->mWaveBuffer.size();i++)
    {
        self->mWaveBuffer[i] = WAVEHDR{};
        self->mWaveBuffer[i].lpData = self->mWaveBuffer[i-1].lpData +
                                     self->mWaveBuffer[i-1].dwBufferLength;
        self->mWaveBuffer[i].dwBufferLength = BufferSize;
    }
    self->mIdx = 0;

    return ALC_TRUE;
}

ALCboolean ALCwinmmPlayback_start(ALCwinmmPlayback *self)
{
    try {
        std::for_each(self->mWaveBuffer.begin(), self->mWaveBuffer.end(),
            [self](WAVEHDR &waveHdr) -> void
            { waveOutPrepareHeader(self->mOutHdl, &waveHdr, static_cast<UINT>(sizeof(WAVEHDR))); }
        );
        self->mWritable.store(static_cast<ALuint>(self->mWaveBuffer.size()),
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

    while(self->mWritable.load(std::memory_order_acquire) < self->mWaveBuffer.size())
        self->mSem.wait();
    std::for_each(self->mWaveBuffer.begin(), self->mWaveBuffer.end(),
        [self](WAVEHDR &waveHdr) -> void
        { waveOutUnprepareHeader(self->mOutHdl, &waveHdr, sizeof(WAVEHDR)); }
    );
    self->mWritable.store(0, std::memory_order_release);
}


struct ALCwinmmCapture final : public ALCbackend {
    std::atomic<ALuint> mReadable{0u};
    al::semaphore mSem;
    int mIdx{0};
    std::array<WAVEHDR,4> mWaveBuffer{};

    HWAVEIN mInHdl{nullptr};

    RingBufferPtr mRing{nullptr};

    WAVEFORMATEX mFormat{};

    std::atomic<ALenum> mKillNow{AL_TRUE};
    std::thread mThread;

    ALCwinmmCapture(ALCdevice *device) noexcept : ALCbackend{device} { }
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
    new (self) ALCwinmmCapture{device};
    SET_VTABLE2(ALCwinmmCapture, ALCbackend, self);
}

void ALCwinmmCapture_Destruct(ALCwinmmCapture *self)
{
    // Close the Wave device
    if(self->mInHdl)
        waveInClose(self->mInHdl);
    self->mInHdl = nullptr;

    al_free(self->mWaveBuffer[0].lpData);
    std::fill(self->mWaveBuffer.begin(), self->mWaveBuffer.end(), WAVEHDR{});

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
    self->mReadable.fetch_add(1, std::memory_order_acq_rel);
    self->mSem.post();
}

int ALCwinmmCapture_captureProc(ALCwinmmCapture *self)
{
    ALCdevice *device{self->mDevice};
    RingBuffer *ring{self->mRing.get()};

    althrd_setname(RECORD_THREAD_NAME);

    ALCwinmmCapture_lock(self);
    while(!self->mKillNow.load(std::memory_order_acquire) &&
          device->Connected.load(std::memory_order_acquire))
    {
        ALuint todo{self->mReadable.load(std::memory_order_acquire)};
        if(todo < 1)
        {
            ALCwinmmCapture_unlock(self);
            self->mSem.wait();
            ALCwinmmCapture_lock(self);
            continue;
        }

        int widx{self->mIdx};
        do {
            WAVEHDR &waveHdr = self->mWaveBuffer[widx];
            widx = (widx+1) % self->mWaveBuffer.size();

            ring->write(waveHdr.lpData, waveHdr.dwBytesRecorded / self->mFormat.nBlockAlign);
            self->mReadable.fetch_sub(1, std::memory_order_acq_rel);
            waveInAddBuffer(self->mInHdl, &waveHdr, sizeof(WAVEHDR));
        } while(--todo);
        self->mIdx = widx;
    }
    ALCwinmmCapture_unlock(self);

    return 0;
}


ALCenum ALCwinmmCapture_open(ALCwinmmCapture *self, const ALCchar *deviceName)
{
    ALCdevice *device{self->mDevice};

    if(CaptureDevices.empty())
        ProbeCaptureDevices();

    // Find the Device ID matching the deviceName if valid
    auto iter = deviceName ?
        std::find(CaptureDevices.cbegin(), CaptureDevices.cend(), deviceName) :
        CaptureDevices.cbegin();
    if(iter == CaptureDevices.cend()) return ALC_INVALID_VALUE;
    auto DeviceID = static_cast<UINT>(std::distance(CaptureDevices.cbegin(), iter));

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

    self->mFormat = WAVEFORMATEX{};
    self->mFormat.wFormatTag = (device->FmtType == DevFmtFloat) ?
                               WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
    self->mFormat.nChannels = device->channelsFromFmt();
    self->mFormat.wBitsPerSample = device->bytesFromFmt() * 8;
    self->mFormat.nBlockAlign = self->mFormat.wBitsPerSample *
                                self->mFormat.nChannels / 8;
    self->mFormat.nSamplesPerSec = device->Frequency;
    self->mFormat.nAvgBytesPerSec = self->mFormat.nSamplesPerSec *
                                    self->mFormat.nBlockAlign;
    self->mFormat.cbSize = 0;

    MMRESULT res{waveInOpen(&self->mInHdl, DeviceID, &self->mFormat,
        (DWORD_PTR)&ALCwinmmCapture_waveInProc, (DWORD_PTR)self, CALLBACK_FUNCTION
    )};
    if(res != MMSYSERR_NOERROR)
    {
        ERR("waveInOpen failed: %u\n", res);
        return ALC_INVALID_VALUE;
    }

    // Ensure each buffer is 50ms each
    DWORD BufferSize{self->mFormat.nAvgBytesPerSec / 20u};
    BufferSize -= (BufferSize % self->mFormat.nBlockAlign);

    // Allocate circular memory buffer for the captured audio
    // Make sure circular buffer is at least 100ms in size
    ALuint CapturedDataSize{device->UpdateSize*device->NumUpdates};
    CapturedDataSize = static_cast<ALuint>(
        std::max<size_t>(CapturedDataSize, BufferSize*self->mWaveBuffer.size()));

    self->mRing = CreateRingBuffer(CapturedDataSize, self->mFormat.nBlockAlign, false);
    if(!self->mRing) return ALC_INVALID_VALUE;

    al_free(self->mWaveBuffer[0].lpData);
    self->mWaveBuffer[0] = WAVEHDR{};
    self->mWaveBuffer[0].lpData = static_cast<char*>(al_calloc(16, BufferSize*4));
    self->mWaveBuffer[0].dwBufferLength = BufferSize;
    for(size_t i{1};i < self->mWaveBuffer.size();++i)
    {
        self->mWaveBuffer[i] = WAVEHDR{};
        self->mWaveBuffer[i].lpData = self->mWaveBuffer[i-1].lpData +
                                      self->mWaveBuffer[i-1].dwBufferLength;
        self->mWaveBuffer[i].dwBufferLength = self->mWaveBuffer[i-1].dwBufferLength;
    }

    device->DeviceName = CaptureDevices[DeviceID];
    return ALC_NO_ERROR;
}

ALCboolean ALCwinmmCapture_start(ALCwinmmCapture *self)
{
    try {
        for(size_t i{0};i < self->mWaveBuffer.size();++i)
        {
            waveInPrepareHeader(self->mInHdl, &self->mWaveBuffer[i], sizeof(WAVEHDR));
            waveInAddBuffer(self->mInHdl, &self->mWaveBuffer[i], sizeof(WAVEHDR));
        }

        self->mKillNow.store(AL_FALSE, std::memory_order_release);
        self->mThread = std::thread(ALCwinmmCapture_captureProc, self);

        waveInStart(self->mInHdl);
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
    waveInStop(self->mInHdl);

    self->mKillNow.store(AL_TRUE, std::memory_order_release);
    if(self->mThread.joinable())
    {
        self->mSem.post();
        self->mThread.join();
    }

    waveInReset(self->mInHdl);
    for(size_t i{0};i < self->mWaveBuffer.size();++i)
        waveInUnprepareHeader(self->mInHdl, &self->mWaveBuffer[i], sizeof(WAVEHDR));

    self->mReadable.store(0, std::memory_order_release);
    self->mIdx = 0;
}

ALCenum ALCwinmmCapture_captureSamples(ALCwinmmCapture *self, ALCvoid *buffer, ALCuint samples)
{
    RingBuffer *ring{self->mRing.get()};
    ring->read(buffer, samples);
    return ALC_NO_ERROR;
}

ALCuint ALCwinmmCapture_availableSamples(ALCwinmmCapture *self)
{
    RingBuffer *ring{self->mRing.get()};
    return (ALCuint)ring->readSpace();
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
        return backend;
    }
    if(type == ALCbackend_Capture)
    {
        ALCwinmmCapture *backend;
        NEW_OBJ(backend, ALCwinmmCapture)(device);
        return backend;
    }

    return nullptr;
}

BackendFactory &WinMMBackendFactory::getFactory()
{
    static WinMMBackendFactory factory{};
    return factory;
}
