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

#include "winmm.h"

#include <cstdlib>
#include <cstdio>
#include <memory.h>

#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <ranges>
#include <string>
#include <thread>
#include <vector>

#include "alformat.hpp"
#include "alnumeric.h"
#include "althrd_setname.h"
#include "core/device.h"
#include "core/helpers.h"
#include "core/logging.h"
#include "gsl/gsl"
#include "ringbuffer.h"
#include "strutils.hpp"
#include "vector.h"

#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT  0x0003
#endif

namespace {

std::vector<std::string> PlaybackDevices;
std::vector<std::string> CaptureDevices;

[[nodiscard]]
auto checkName(const std::vector<std::string> &list, const std::string &name) -> bool
{ return std::ranges::find(list, name) != list.end(); }

void ProbePlaybackDevices()
{
    PlaybackDevices.clear();

    const auto numdevs = waveOutGetNumDevs();
    PlaybackDevices.reserve(numdevs);
    for(const auto i : std::views::iota(0u, numdevs))
    {
        auto dname = std::string{};

        auto WaveCaps = WAVEOUTCAPSW{};
        if(waveOutGetDevCapsW(i, &WaveCaps, sizeof(WaveCaps)) == MMSYSERR_NOERROR)
        {
            const auto basename = wstr_to_utf8(std::data(WaveCaps.szPname));

            auto count = 1;
            auto newname = basename;
            while(checkName(PlaybackDevices, newname))
                newname = al::format("{} #{}", basename, ++count);
            dname = std::move(newname);

            TRACE("Got device \"{}\", ID {}", dname, i);
        }
        PlaybackDevices.emplace_back(std::move(dname));
    }
}

void ProbeCaptureDevices()
{
    CaptureDevices.clear();

    const auto numdevs = waveInGetNumDevs();
    CaptureDevices.reserve(numdevs);
    for(const auto i : std::views::iota(0u, numdevs))
    {
        auto dname = std::string{};

        auto WaveCaps = WAVEINCAPSW{};
        if(waveInGetDevCapsW(i, &WaveCaps, sizeof(WaveCaps)) == MMSYSERR_NOERROR)
        {
            const auto basename = wstr_to_utf8(std::data(WaveCaps.szPname));

            auto count = 1;
            auto newname = basename;
            while(checkName(CaptureDevices, newname))
                newname = al::format("{} #{}", basename, ++count);
            dname = std::move(newname);

            TRACE("Got device \"{}\", ID {}", dname, i);
        }
        CaptureDevices.emplace_back(std::move(dname));
    }
}


struct WinMMPlayback final : public BackendBase {
    explicit WinMMPlayback(gsl::not_null<DeviceBase*> const device) noexcept : BackendBase{device}
    { }
    ~WinMMPlayback() override;

    void CALLBACK waveOutProc(HWAVEOUT device, UINT msg, DWORD_PTR param1, DWORD_PTR param2) noexcept;
    static void CALLBACK waveOutProcC(HWAVEOUT const device, UINT const msg,
        DWORD_PTR const instance, DWORD_PTR const param1, DWORD_PTR const param2) noexcept
    { std::bit_cast<WinMMPlayback*>(instance)->waveOutProc(device, msg, param1, param2); }

    void mixerProc();

    void open(std::string_view name) override;
    auto reset() -> bool override;
    void start() override;
    void stop() override;

    std::atomic<u32> mWritable{0_u32};
    u32 mIdx{0_u32};
    std::array<WAVEHDR, 4> mWaveBuffer{};
    al::vector<char,16> mBuffer;

    HWAVEOUT mOutHdl{nullptr};

    WAVEFORMATEX mFormat{};

    std::atomic<bool> mKillNow{true};
    std::thread mThread;
};

WinMMPlayback::~WinMMPlayback()
{
    if(mOutHdl)
        waveOutClose(mOutHdl);
    mOutHdl = nullptr;
}

/* WinMMPlayback::waveOutProc
 *
 * Posts a message to 'WinMMPlayback::mixerProc' every time a WaveOut Buffer is
 * completed and returns to the application (for more data)
 */
void CALLBACK WinMMPlayback::waveOutProc(HWAVEOUT, UINT const msg, DWORD_PTR, DWORD_PTR) noexcept
{
    if(msg != WOM_DONE) return;
    mWritable.fetch_add(1, std::memory_order_acq_rel);
    mWritable.notify_all();
}

FORCE_ALIGN void WinMMPlayback::mixerProc()
{
    SetRTPriority();
    althrd_setname(GetMixerThreadName());

    while(!mKillNow.load(std::memory_order_acquire)
        && mDevice->Connected.load(std::memory_order_acquire))
    {
        mWritable.wait(0, std::memory_order_acquire);
        auto todo = mWritable.load(std::memory_order_acquire);

        auto widx = usize{mIdx};
        while(todo > 0)
        {
            auto &waveHdr = mWaveBuffer[widx];
            if(++widx == mWaveBuffer.size()) widx = 0;

            mDevice->renderSamples(waveHdr.lpData, mDevice->mUpdateSize, mFormat.nChannels);
            mWritable.fetch_sub(1, std::memory_order_acq_rel);
            waveOutWrite(mOutHdl, &waveHdr, sizeof(WAVEHDR));
            --todo;
        }
        mIdx = gsl::narrow_cast<u32>(widx);
    }
}


void WinMMPlayback::open(std::string_view name)
{
    if(PlaybackDevices.empty())
        ProbePlaybackDevices();

    // Find the Device ID matching the deviceName if valid
    auto const iter = !name.empty() ? std::ranges::find(PlaybackDevices, name)
        : PlaybackDevices.begin();
    if(iter == PlaybackDevices.end())
        throw al::backend_exception{al::backend_error::NoDevice, "Device name \"{}\" not found",
            name};
    auto const DeviceID = gsl::narrow_cast<UINT>(std::distance(PlaybackDevices.begin(), iter));

    auto fmttype = mDevice->FmtType;
    auto format = WAVEFORMATEX{};
    do {
        format = WAVEFORMATEX{};
        if(fmttype == DevFmtFloat)
        {
            format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
            format.wBitsPerSample = 32;
        }
        else
        {
            format.wFormatTag = WAVE_FORMAT_PCM;
            if(fmttype == DevFmtUByte || fmttype == DevFmtByte)
                format.wBitsPerSample = 8;
            else
                format.wBitsPerSample = 16;
        }
        format.nChannels = ((mDevice->FmtChans == DevFmtMono) ? 1 : 2);
        format.nBlockAlign = gsl::narrow_cast<WORD>(format.wBitsPerSample * format.nChannels / 8);
        format.nSamplesPerSec = mDevice->mSampleRate;
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        format.cbSize = 0;

        auto const res = waveOutOpen(&mOutHdl, DeviceID, &format,
            std::bit_cast<DWORD_PTR>(&WinMMPlayback::waveOutProcC),
            std::bit_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
        if(res == MMSYSERR_NOERROR) break;

        if(fmttype != DevFmtFloat)
            throw al::backend_exception{al::backend_error::DeviceError, "waveOutOpen failed: {}",
                res};

        fmttype = DevFmtShort;
    } while(true);

    mFormat = format;

    mDeviceName = PlaybackDevices[DeviceID];
}

auto WinMMPlayback::reset() -> bool
{
    mDevice->mBufferSize = gsl::narrow_cast<u32>(u64{mDevice->mBufferSize} *
        mFormat.nSamplesPerSec / mDevice->mSampleRate);
    mDevice->mBufferSize = (mDevice->mBufferSize+3) & ~0x3_u32;
    mDevice->mUpdateSize = mDevice->mBufferSize / 4;
    mDevice->mSampleRate = mFormat.nSamplesPerSec;

    auto clearval = char{0};
    if(mFormat.wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        if(mFormat.wBitsPerSample == 32)
            mDevice->FmtType = DevFmtFloat;
        else
        {
            ERR("Unhandled IEEE float sample depth: {}", mFormat.wBitsPerSample);
            return false;
        }
    }
    else if(mFormat.wFormatTag == WAVE_FORMAT_PCM)
    {
        if(mFormat.wBitsPerSample == 16)
            mDevice->FmtType = DevFmtShort;
        else if(mFormat.wBitsPerSample == 8)
        {
            mDevice->FmtType = DevFmtUByte;
            clearval = char{-0x80};
        }
        else
        {
            ERR("Unhandled PCM sample depth: {}", mFormat.wBitsPerSample);
            return false;
        }
    }
    else
    {
        ERR("Unhandled format tag: {:#04x}", as_unsigned(mFormat.wFormatTag));
        return false;
    }

    if(mFormat.nChannels >= 2)
        mDevice->FmtChans = DevFmtStereo;
    else if(mFormat.nChannels == 1)
        mDevice->FmtChans = DevFmtMono;
    else
    {
        ERR("Unhandled channel count: {}", mFormat.nChannels);
        return false;
    }
    setDefaultWFXChannelOrder();

    auto const BufferSize = mDevice->mUpdateSize * mFormat.nChannels * mDevice->bytesFromFmt();

    decltype(mBuffer)(BufferSize*mWaveBuffer.size(), clearval).swap(mBuffer);
    auto bufferiter = mBuffer.begin();

    mWaveBuffer[0] = WAVEHDR{};
    mWaveBuffer[0].lpData = std::to_address(bufferiter);
    mWaveBuffer[0].dwBufferLength = BufferSize;
    for(auto i=1_uz;i < mWaveBuffer.size();i++)
    {
        bufferiter += mWaveBuffer[i-1].dwBufferLength;

        mWaveBuffer[i] = WAVEHDR{};
        mWaveBuffer[i].lpData = std::to_address(bufferiter);
        mWaveBuffer[i].dwBufferLength = BufferSize;
    }
    mIdx = 0;

    return true;
}

void WinMMPlayback::start()
{
    try {
        for(auto &waveHdr : mWaveBuffer)
            waveOutPrepareHeader(mOutHdl, &waveHdr, sizeof(WAVEHDR));
        mWritable.store(gsl::narrow_cast<u32>(mWaveBuffer.size()), std::memory_order_release);

        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{&WinMMPlayback::mixerProc, this};
    }
    catch(std::exception& e) {
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start mixing thread: {}", e.what()};
    }
}

void WinMMPlayback::stop()
{
    if(mKillNow.exchange(true, std::memory_order_acq_rel) || !mThread.joinable())
        return;
    mThread.join();

    auto writable = mWritable.load(std::memory_order_acquire);
    while(writable < mWaveBuffer.size())
    {
        mWritable.wait(writable, std::memory_order_acquire);
        writable = mWritable.load(std::memory_order_acquire);
    }
    for(auto &waveHdr : mWaveBuffer)
        waveOutUnprepareHeader(mOutHdl, &waveHdr, sizeof(WAVEHDR));
    mWritable.store(0, std::memory_order_release);
}


struct WinMMCapture final : public BackendBase {
    explicit WinMMCapture(gsl::not_null<DeviceBase*> device) noexcept : BackendBase{device} { }
    ~WinMMCapture() override;

    void CALLBACK waveInProc(HWAVEIN device, UINT msg, DWORD_PTR param1, DWORD_PTR param2) noexcept;
    static void CALLBACK waveInProcC(HWAVEIN const device, UINT const msg,
        DWORD_PTR const instance, DWORD_PTR const param1, DWORD_PTR const param2) noexcept
    { std::bit_cast<WinMMCapture*>(instance)->waveInProc(device, msg, param1, param2); }

    void captureProc();

    void open(std::string_view name) override;
    void start() override;
    void stop() override;
    void captureSamples(std::span<std::byte> outbuffer) override;
    auto availableSamples() -> usize override;

    std::atomic<u32> mReadable{0_u32};
    u32 mIdx{0_u32};
    std::array<WAVEHDR, 4> mWaveBuffer{};
    al::vector<char, 16> mBuffer;

    HWAVEIN mInHdl{nullptr};

    RingBufferPtr<std::byte> mRing;

    WAVEFORMATEX mFormat{};

    std::atomic<bool> mKillNow{true};
    std::thread mThread;
};

WinMMCapture::~WinMMCapture()
{
    // Close the Wave device
    if(mInHdl)
        waveInClose(mInHdl);
    mInHdl = nullptr;
}

/* WinMMCapture::waveInProc
 *
 * Posts a message to 'WinMMCapture::captureProc' every time a WaveIn Buffer is
 * completed and returns to the application (with more data).
 */
void CALLBACK WinMMCapture::waveInProc(HWAVEIN, UINT const msg, DWORD_PTR, DWORD_PTR) noexcept
{
    if(msg != WIM_DATA) return;
    mReadable.fetch_add(1, std::memory_order_acq_rel);
    mReadable.notify_all();
}

void WinMMCapture::captureProc()
{
    althrd_setname(GetRecordThreadName());

    while(!mKillNow.load(std::memory_order_acquire)
        && mDevice->Connected.load(std::memory_order_acquire))
    {
        mReadable.wait(0, std::memory_order_acquire);
        auto todo = mReadable.load(std::memory_order_acquire);

        auto widx = usize{mIdx};
        while(todo > 0)
        {
            auto &waveHdr = mWaveBuffer[widx];
            widx = (widx+1) % mWaveBuffer.size();

            std::ignore = mRing->write(std::as_bytes(std::span{waveHdr.lpData,
                waveHdr.dwBytesRecorded}));
            mReadable.fetch_sub(1, std::memory_order_acq_rel);
            waveInAddBuffer(mInHdl, &waveHdr, sizeof(WAVEHDR));
            --todo;
        }
        mIdx = gsl::narrow_cast<u32>(widx);
    }
}


void WinMMCapture::open(std::string_view name)
{
    if(CaptureDevices.empty())
        ProbeCaptureDevices();

    // Find the Device ID matching the deviceName if valid
    auto const iter = !name.empty() ? std::ranges::find(CaptureDevices, name)
        : CaptureDevices.begin();
    if(iter == CaptureDevices.end())
        throw al::backend_exception{al::backend_error::NoDevice, "Device name \"{}\" not found",
            name};
    auto const DeviceID = gsl::narrow_cast<UINT>(std::distance(CaptureDevices.begin(), iter));

    switch(mDevice->FmtChans)
    {
    case DevFmtMono:
    case DevFmtStereo:
        break;

    case DevFmtQuad:
    case DevFmtX51:
    case DevFmtX61:
    case DevFmtX71:
    case DevFmtX714:
    case DevFmtX7144:
    case DevFmtX3D71:
    case DevFmtAmbi3D:
        throw al::backend_exception{al::backend_error::DeviceError, "{} capture not supported",
            DevFmtChannelsString(mDevice->FmtChans)};
    }

    auto clearval = char{0};
    switch(mDevice->FmtType)
    {
    case DevFmtUByte:
        clearval = char{-0x80};
        [[fallthrough]];
    case DevFmtShort:
    case DevFmtInt:
    case DevFmtFloat:
        break;

    case DevFmtByte:
    case DevFmtUShort:
    case DevFmtUInt:
        throw al::backend_exception{al::backend_error::DeviceError, "{} samples not supported",
            DevFmtTypeString(mDevice->FmtType)};
    }

    mFormat = WAVEFORMATEX{};
    mFormat.wFormatTag = (mDevice->FmtType == DevFmtFloat) ?
        WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
    mFormat.nChannels = gsl::narrow_cast<WORD>(mDevice->channelsFromFmt());
    mFormat.wBitsPerSample = gsl::narrow_cast<WORD>(mDevice->bytesFromFmt() * 8);
    mFormat.nBlockAlign = gsl::narrow_cast<WORD>(mFormat.wBitsPerSample * mFormat.nChannels / 8);
    mFormat.nSamplesPerSec = mDevice->mSampleRate;
    mFormat.nAvgBytesPerSec = mFormat.nSamplesPerSec * mFormat.nBlockAlign;
    mFormat.cbSize = 0;

    auto res = waveInOpen(&mInHdl, DeviceID, &mFormat,
        std::bit_cast<DWORD_PTR>(&WinMMCapture::waveInProcC),
        std::bit_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
    if(res != MMSYSERR_NOERROR)
        throw al::backend_exception{al::backend_error::DeviceError, "waveInOpen failed: {}", res};

    // Ensure each buffer is 50ms each
    auto BufferSize = DWORD{mFormat.nAvgBytesPerSec / 20u};
    BufferSize -= (BufferSize % mFormat.nBlockAlign);

    // Allocate circular memory buffer for the captured audio
    // Make sure circular buffer is at least 100ms in size
    auto const CapturedDataSize = std::max<usize>(mDevice->mBufferSize,
        BufferSize*mWaveBuffer.size());

    mRing = RingBuffer<std::byte>::Create(CapturedDataSize, mFormat.nBlockAlign, false);

    decltype(mBuffer)(BufferSize*mWaveBuffer.size(), clearval).swap(mBuffer);
    auto bufferiter = mBuffer.begin();

    mWaveBuffer[0] = WAVEHDR{};
    mWaveBuffer[0].lpData = std::to_address(bufferiter);
    mWaveBuffer[0].dwBufferLength = BufferSize;
    for(auto i=1_uz;i < mWaveBuffer.size();++i)
    {
        bufferiter += mWaveBuffer[i-1].dwBufferLength;

        mWaveBuffer[i] = WAVEHDR{};
        mWaveBuffer[i].lpData = std::to_address(bufferiter);
        mWaveBuffer[i].dwBufferLength = mWaveBuffer[i-1].dwBufferLength;
    }

    mDeviceName = CaptureDevices[DeviceID];
}

void WinMMCapture::start()
{
    try {
        for(auto &buffer : mWaveBuffer)
        {
            waveInPrepareHeader(mInHdl, &buffer, sizeof(WAVEHDR));
            waveInAddBuffer(mInHdl, &buffer, sizeof(WAVEHDR));
        }

        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{&WinMMCapture::captureProc, this};

        waveInStart(mInHdl);
    }
    catch(std::exception& e) {
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start recording thread: {}", e.what()};
    }
}

void WinMMCapture::stop()
{
    mKillNow.store(true, std::memory_order_release);
    if(mThread.joinable())
        mThread.join();

    waveInStop(mInHdl);
    waveInReset(mInHdl);
    for(auto &buffer : mWaveBuffer)
        waveInUnprepareHeader(mInHdl, &buffer, sizeof(WAVEHDR));

    mReadable.store(0, std::memory_order_release);
    mIdx = 0;
}

void WinMMCapture::captureSamples(std::span<std::byte> outbuffer)
{ std::ignore = mRing->read(outbuffer); }

auto WinMMCapture::availableSamples() -> usize
{ return mRing->readSpace(); }

} // namespace


auto WinMMBackendFactory::init() -> bool
{ return true; }

auto WinMMBackendFactory::querySupport(BackendType const type) -> bool
{ return type == BackendType::Playback || type == BackendType::Capture; }

auto WinMMBackendFactory::enumerate(BackendType const type) -> std::vector<std::string>
{
    auto outnames = std::vector<std::string>{};
    auto add_device = [&outnames](std::string const &dname) -> void
    { if(!dname.empty()) outnames.emplace_back(dname); };

    switch(type)
    {
    case BackendType::Playback:
        ProbePlaybackDevices();
        outnames.reserve(PlaybackDevices.size());
        std::ranges::for_each(PlaybackDevices, add_device);
        break;

    case BackendType::Capture:
        ProbeCaptureDevices();
        outnames.reserve(CaptureDevices.size());
        std::ranges::for_each(CaptureDevices, add_device);
        break;
    }
    return outnames;
}

auto WinMMBackendFactory::createBackend(gsl::not_null<DeviceBase*> const device,
    BackendType const type) -> BackendPtr
{
    if(type == BackendType::Playback)
        return BackendPtr{new WinMMPlayback{device}};
    if(type == BackendType::Capture)
        return BackendPtr{new WinMMCapture{device}};
    return nullptr;
}

auto WinMMBackendFactory::getFactory() -> BackendFactory&
{
    static WinMMBackendFactory factory{};
    return factory;
}
