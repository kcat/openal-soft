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
#include <functional>

#include "alcmain.h"
#include "alexcpt.h"
#include "alu.h"
#include "ringbuffer.h"
#include "strutils.h"
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


struct WinMMPlayback final : public BackendBase {
    WinMMPlayback(ALCdevice *device) noexcept : BackendBase{device} { }
    ~WinMMPlayback() override;

    void CALLBACK waveOutProc(HWAVEOUT device, UINT msg, DWORD_PTR param1, DWORD_PTR param2) noexcept;
    static void CALLBACK waveOutProcC(HWAVEOUT device, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2) noexcept
    { reinterpret_cast<WinMMPlayback*>(instance)->waveOutProc(device, msg, param1, param2); }

    int mixerProc();

    void open(const ALCchar *name) override;
    bool reset() override;
    bool start() override;
    void stop() override;

    std::atomic<ALuint> mWritable{0u};
    al::semaphore mSem;
    ALuint mIdx{0u};
    std::array<WAVEHDR,4> mWaveBuffer{};

    HWAVEOUT mOutHdl{nullptr};

    WAVEFORMATEX mFormat{};

    std::atomic<bool> mKillNow{true};
    std::thread mThread;

    DEF_NEWDEL(WinMMPlayback)
};

WinMMPlayback::~WinMMPlayback()
{
    if(mOutHdl)
        waveOutClose(mOutHdl);
    mOutHdl = nullptr;

    al_free(mWaveBuffer[0].lpData);
    std::fill(mWaveBuffer.begin(), mWaveBuffer.end(), WAVEHDR{});
}

/* WinMMPlayback::waveOutProc
 *
 * Posts a message to 'WinMMPlayback::mixerProc' everytime a WaveOut Buffer is
 * completed and returns to the application (for more data)
 */
void CALLBACK WinMMPlayback::waveOutProc(HWAVEOUT, UINT msg, DWORD_PTR, DWORD_PTR) noexcept
{
    if(msg != WOM_DONE) return;
    mWritable.fetch_add(1, std::memory_order_acq_rel);
    mSem.post();
}

FORCE_ALIGN int WinMMPlayback::mixerProc()
{
    SetRTPriority();
    althrd_setname(MIXER_THREAD_NAME);

    const size_t frame_step{mDevice->channelsFromFmt()};

    std::unique_lock<WinMMPlayback> dlock{*this};
    while(!mKillNow.load(std::memory_order_acquire) &&
          mDevice->Connected.load(std::memory_order_acquire))
    {
        ALsizei todo = mWritable.load(std::memory_order_acquire);
        if(todo < 1)
        {
            dlock.unlock();
            mSem.wait();
            dlock.lock();
            continue;
        }

        size_t widx{mIdx};
        do {
            WAVEHDR &waveHdr = mWaveBuffer[widx];
            widx = (widx+1) % mWaveBuffer.size();

            aluMixData(mDevice, waveHdr.lpData, mDevice->UpdateSize, frame_step);
            mWritable.fetch_sub(1, std::memory_order_acq_rel);
            waveOutWrite(mOutHdl, &waveHdr, sizeof(WAVEHDR));
        } while(--todo);
        mIdx = static_cast<ALuint>(widx);
    }

    return 0;
}


void WinMMPlayback::open(const ALCchar *name)
{
    if(PlaybackDevices.empty())
        ProbePlaybackDevices();

    // Find the Device ID matching the deviceName if valid
    auto iter = name ?
        std::find(PlaybackDevices.cbegin(), PlaybackDevices.cend(), name) :
        PlaybackDevices.cbegin();
    if(iter == PlaybackDevices.cend())
        throw al::backend_exception{ALC_INVALID_VALUE, "Device name \"%s\" not found", name};
    auto DeviceID = static_cast<UINT>(std::distance(PlaybackDevices.cbegin(), iter));

retry_open:
    mFormat = WAVEFORMATEX{};
    if(mDevice->FmtType == DevFmtFloat)
    {
        mFormat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        mFormat.wBitsPerSample = 32;
    }
    else
    {
        mFormat.wFormatTag = WAVE_FORMAT_PCM;
        if(mDevice->FmtType == DevFmtUByte || mDevice->FmtType == DevFmtByte)
            mFormat.wBitsPerSample = 8;
        else
            mFormat.wBitsPerSample = 16;
    }
    mFormat.nChannels = ((mDevice->FmtChans == DevFmtMono) ? 1 : 2);
    mFormat.nBlockAlign = static_cast<WORD>(mFormat.wBitsPerSample * mFormat.nChannels / 8);
    mFormat.nSamplesPerSec = mDevice->Frequency;
    mFormat.nAvgBytesPerSec = mFormat.nSamplesPerSec * mFormat.nBlockAlign;
    mFormat.cbSize = 0;

    MMRESULT res{waveOutOpen(&mOutHdl, DeviceID, &mFormat,
        reinterpret_cast<DWORD_PTR>(&WinMMPlayback::waveOutProcC),
        reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION)};
    if(res != MMSYSERR_NOERROR)
    {
        if(mDevice->FmtType == DevFmtFloat)
        {
            mDevice->FmtType = DevFmtShort;
            goto retry_open;
        }
        throw al::backend_exception{ALC_INVALID_VALUE, "waveOutOpen failed: %u", res};
    }

    mDevice->DeviceName = PlaybackDevices[DeviceID];
}

bool WinMMPlayback::reset()
{
    mDevice->BufferSize = static_cast<ALuint>(uint64_t{mDevice->BufferSize} *
        mFormat.nSamplesPerSec / mDevice->Frequency);
    mDevice->BufferSize = (mDevice->BufferSize+3) & ~0x3;
    mDevice->UpdateSize = mDevice->BufferSize / 4;
    mDevice->Frequency = mFormat.nSamplesPerSec;

    if(mFormat.wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        if(mFormat.wBitsPerSample == 32)
            mDevice->FmtType = DevFmtFloat;
        else
        {
            ERR("Unhandled IEEE float sample depth: %d\n", mFormat.wBitsPerSample);
            return false;
        }
    }
    else if(mFormat.wFormatTag == WAVE_FORMAT_PCM)
    {
        if(mFormat.wBitsPerSample == 16)
            mDevice->FmtType = DevFmtShort;
        else if(mFormat.wBitsPerSample == 8)
            mDevice->FmtType = DevFmtUByte;
        else
        {
            ERR("Unhandled PCM sample depth: %d\n", mFormat.wBitsPerSample);
            return false;
        }
    }
    else
    {
        ERR("Unhandled format tag: 0x%04x\n", mFormat.wFormatTag);
        return false;
    }

    if(mFormat.nChannels == 2)
        mDevice->FmtChans = DevFmtStereo;
    else if(mFormat.nChannels == 1)
        mDevice->FmtChans = DevFmtMono;
    else
    {
        ERR("Unhandled channel count: %d\n", mFormat.nChannels);
        return false;
    }
    SetDefaultWFXChannelOrder(mDevice);

    ALuint BufferSize{mDevice->UpdateSize * mDevice->frameSizeFromFmt()};

    al_free(mWaveBuffer[0].lpData);
    mWaveBuffer[0] = WAVEHDR{};
    mWaveBuffer[0].lpData = static_cast<char*>(al_calloc(16, BufferSize * mWaveBuffer.size()));
    mWaveBuffer[0].dwBufferLength = BufferSize;
    for(size_t i{1};i < mWaveBuffer.size();i++)
    {
        mWaveBuffer[i] = WAVEHDR{};
        mWaveBuffer[i].lpData = mWaveBuffer[i-1].lpData + mWaveBuffer[i-1].dwBufferLength;
        mWaveBuffer[i].dwBufferLength = BufferSize;
    }
    mIdx = 0;

    return true;
}

bool WinMMPlayback::start()
{
    try {
        std::for_each(mWaveBuffer.begin(), mWaveBuffer.end(),
            [this](WAVEHDR &waveHdr) -> void
            { waveOutPrepareHeader(mOutHdl, &waveHdr, static_cast<UINT>(sizeof(WAVEHDR))); }
        );
        mWritable.store(static_cast<ALuint>(mWaveBuffer.size()), std::memory_order_release);

        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{std::mem_fn(&WinMMPlayback::mixerProc), this};
        return true;
    }
    catch(std::exception& e) {
        ERR("Failed to start mixing thread: %s\n", e.what());
    }
    catch(...) {
    }
    return false;
}

void WinMMPlayback::stop()
{
    if(mKillNow.exchange(true, std::memory_order_acq_rel) || !mThread.joinable())
        return;
    mThread.join();

    while(mWritable.load(std::memory_order_acquire) < mWaveBuffer.size())
        mSem.wait();
    std::for_each(mWaveBuffer.begin(), mWaveBuffer.end(),
        [this](WAVEHDR &waveHdr) -> void
        { waveOutUnprepareHeader(mOutHdl, &waveHdr, sizeof(WAVEHDR)); }
    );
    mWritable.store(0, std::memory_order_release);
}


struct WinMMCapture final : public BackendBase {
    WinMMCapture(ALCdevice *device) noexcept : BackendBase{device} { }
    ~WinMMCapture() override;

    void CALLBACK waveInProc(HWAVEIN device, UINT msg, DWORD_PTR param1, DWORD_PTR param2) noexcept;
    static void CALLBACK waveInProcC(HWAVEIN device, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2) noexcept
    { reinterpret_cast<WinMMCapture*>(instance)->waveInProc(device, msg, param1, param2); }

    int captureProc();

    void open(const ALCchar *name) override;
    bool start() override;
    void stop() override;
    ALCenum captureSamples(al::byte *buffer, ALCuint samples) override;
    ALCuint availableSamples() override;

    std::atomic<ALuint> mReadable{0u};
    al::semaphore mSem;
    ALuint mIdx{0};
    std::array<WAVEHDR,4> mWaveBuffer{};

    HWAVEIN mInHdl{nullptr};

    RingBufferPtr mRing{nullptr};

    WAVEFORMATEX mFormat{};

    std::atomic<bool> mKillNow{true};
    std::thread mThread;

    DEF_NEWDEL(WinMMCapture)
};

WinMMCapture::~WinMMCapture()
{
    // Close the Wave device
    if(mInHdl)
        waveInClose(mInHdl);
    mInHdl = nullptr;

    al_free(mWaveBuffer[0].lpData);
    std::fill(mWaveBuffer.begin(), mWaveBuffer.end(), WAVEHDR{});
}

/* WinMMCapture::waveInProc
 *
 * Posts a message to 'WinMMCapture::captureProc' everytime a WaveIn Buffer is
 * completed and returns to the application (with more data).
 */
void CALLBACK WinMMCapture::waveInProc(HWAVEIN, UINT msg, DWORD_PTR, DWORD_PTR) noexcept
{
    if(msg != WIM_DATA) return;
    mReadable.fetch_add(1, std::memory_order_acq_rel);
    mSem.post();
}

int WinMMCapture::captureProc()
{
    althrd_setname(RECORD_THREAD_NAME);

    std::unique_lock<WinMMCapture> dlock{*this};
    while(!mKillNow.load(std::memory_order_acquire) &&
          mDevice->Connected.load(std::memory_order_acquire))
    {
        ALuint todo{mReadable.load(std::memory_order_acquire)};
        if(todo < 1)
        {
            dlock.unlock();
            mSem.wait();
            dlock.lock();
            continue;
        }

        size_t widx{mIdx};
        do {
            WAVEHDR &waveHdr = mWaveBuffer[widx];
            widx = (widx+1) % mWaveBuffer.size();

            mRing->write(waveHdr.lpData, waveHdr.dwBytesRecorded / mFormat.nBlockAlign);
            mReadable.fetch_sub(1, std::memory_order_acq_rel);
            waveInAddBuffer(mInHdl, &waveHdr, sizeof(WAVEHDR));
        } while(--todo);
        mIdx = static_cast<ALuint>(widx);
    }

    return 0;
}


void WinMMCapture::open(const ALCchar *name)
{
    if(CaptureDevices.empty())
        ProbeCaptureDevices();

    // Find the Device ID matching the deviceName if valid
    auto iter = name ?
        std::find(CaptureDevices.cbegin(), CaptureDevices.cend(), name) :
        CaptureDevices.cbegin();
    if(iter == CaptureDevices.cend())
        throw al::backend_exception{ALC_INVALID_VALUE, "Device name \"%s\" not found", name};
    auto DeviceID = static_cast<UINT>(std::distance(CaptureDevices.cbegin(), iter));

    switch(mDevice->FmtChans)
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
        throw al::backend_exception{ALC_INVALID_VALUE, "%s capture not supported",
            DevFmtChannelsString(mDevice->FmtChans)};
    }

    switch(mDevice->FmtType)
    {
    case DevFmtUByte:
    case DevFmtShort:
    case DevFmtInt:
    case DevFmtFloat:
        break;

    case DevFmtByte:
    case DevFmtUShort:
    case DevFmtUInt:
        throw al::backend_exception{ALC_INVALID_VALUE, "%s samples not supported",
            DevFmtTypeString(mDevice->FmtType)};
    }

    mFormat = WAVEFORMATEX{};
    mFormat.wFormatTag = (mDevice->FmtType == DevFmtFloat) ?
                         WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
    mFormat.nChannels = static_cast<WORD>(mDevice->channelsFromFmt());
    mFormat.wBitsPerSample = static_cast<WORD>(mDevice->bytesFromFmt() * 8);
    mFormat.nBlockAlign = static_cast<WORD>(mFormat.wBitsPerSample * mFormat.nChannels / 8);
    mFormat.nSamplesPerSec = mDevice->Frequency;
    mFormat.nAvgBytesPerSec = mFormat.nSamplesPerSec * mFormat.nBlockAlign;
    mFormat.cbSize = 0;

    MMRESULT res{waveInOpen(&mInHdl, DeviceID, &mFormat,
        reinterpret_cast<DWORD_PTR>(&WinMMCapture::waveInProcC),
        reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION)};
    if(res != MMSYSERR_NOERROR)
        throw al::backend_exception{ALC_INVALID_VALUE, "waveInOpen failed: %u", res};

    // Ensure each buffer is 50ms each
    DWORD BufferSize{mFormat.nAvgBytesPerSec / 20u};
    BufferSize -= (BufferSize % mFormat.nBlockAlign);

    // Allocate circular memory buffer for the captured audio
    // Make sure circular buffer is at least 100ms in size
    ALuint CapturedDataSize{mDevice->BufferSize};
    CapturedDataSize = static_cast<ALuint>(maxz(CapturedDataSize, BufferSize*mWaveBuffer.size()));

    mRing = RingBuffer::Create(CapturedDataSize, mFormat.nBlockAlign, false);

    al_free(mWaveBuffer[0].lpData);
    mWaveBuffer[0] = WAVEHDR{};
    mWaveBuffer[0].lpData = static_cast<char*>(al_calloc(16, BufferSize * mWaveBuffer.size()));
    mWaveBuffer[0].dwBufferLength = BufferSize;
    for(size_t i{1};i < mWaveBuffer.size();++i)
    {
        mWaveBuffer[i] = WAVEHDR{};
        mWaveBuffer[i].lpData = mWaveBuffer[i-1].lpData + mWaveBuffer[i-1].dwBufferLength;
        mWaveBuffer[i].dwBufferLength = mWaveBuffer[i-1].dwBufferLength;
    }

    mDevice->DeviceName = CaptureDevices[DeviceID];
}

bool WinMMCapture::start()
{
    try {
        for(size_t i{0};i < mWaveBuffer.size();++i)
        {
            waveInPrepareHeader(mInHdl, &mWaveBuffer[i], sizeof(WAVEHDR));
            waveInAddBuffer(mInHdl, &mWaveBuffer[i], sizeof(WAVEHDR));
        }

        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{std::mem_fn(&WinMMCapture::captureProc), this};

        waveInStart(mInHdl);
        return true;
    }
    catch(std::exception& e) {
        ERR("Failed to start mixing thread: %s\n", e.what());
    }
    catch(...) {
    }
    return false;
}

void WinMMCapture::stop()
{
    waveInStop(mInHdl);

    mKillNow.store(true, std::memory_order_release);
    if(mThread.joinable())
    {
        mSem.post();
        mThread.join();
    }

    waveInReset(mInHdl);
    for(size_t i{0};i < mWaveBuffer.size();++i)
        waveInUnprepareHeader(mInHdl, &mWaveBuffer[i], sizeof(WAVEHDR));

    mReadable.store(0, std::memory_order_release);
    mIdx = 0;
}

ALCenum WinMMCapture::captureSamples(al::byte *buffer, ALCuint samples)
{
    mRing->read(buffer, samples);
    return ALC_NO_ERROR;
}

ALCuint WinMMCapture::availableSamples()
{ return static_cast<ALCuint>(mRing->readSpace()); }

} // namespace


bool WinMMBackendFactory::init()
{ return true; }

bool WinMMBackendFactory::querySupport(BackendType type)
{ return type == BackendType::Playback || type == BackendType::Capture; }

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
        case DevProbe::Playback:
            ProbePlaybackDevices();
            std::for_each(PlaybackDevices.cbegin(), PlaybackDevices.cend(), add_device);
            break;

        case DevProbe::Capture:
            ProbeCaptureDevices();
            std::for_each(CaptureDevices.cbegin(), CaptureDevices.cend(), add_device);
            break;
    }
}

BackendPtr WinMMBackendFactory::createBackend(ALCdevice *device, BackendType type)
{
    if(type == BackendType::Playback)
        return BackendPtr{new WinMMPlayback{device}};
    if(type == BackendType::Capture)
        return BackendPtr{new WinMMCapture{device}};
    return nullptr;
}

BackendFactory &WinMMBackendFactory::getFactory()
{
    static WinMMBackendFactory factory{};
    return factory;
}
