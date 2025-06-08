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

#include "dsound.h"

#include <windows.h>

#include <cguid.h>
#include <mmreg.h>
#ifndef _WAVEFORMATEXTENSIBLE_
#include <ks.h>
#include <ksmedia.h>
#endif

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory.h>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "alnumeric.h"
#include "althrd_setname.h"
#include "comptr.h"
#include "core/device.h"
#include "core/helpers.h"
#include "core/logging.h"
#include "dynload.h"
#include "fmt/core.h"
#include "ringbuffer.h"
#include "strutils.h"

/* MinGW-w64 needs this for some unknown reason now. */
using LPCWAVEFORMATEX = const WAVEFORMATEX*;
#include <dsound.h> /* NOLINT(readability-duplicate-include) Not the same */


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

#if HAVE_DYNLOAD
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


#define MONO SPEAKER_FRONT_CENTER
#define STEREO (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT)
#define QUAD (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT)
#define X5DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)
#define X5DOT1REAR (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT)
#define X6DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_CENTER|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)
#define X7DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)
#define X7DOT1DOT4 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT|SPEAKER_TOP_FRONT_LEFT|SPEAKER_TOP_FRONT_RIGHT|SPEAKER_TOP_BACK_LEFT|SPEAKER_TOP_BACK_RIGHT)

#define MAX_UPDATES 128

struct DevMap {
    std::string name;
    GUID guid;
};

auto PlaybackDevices = std::vector<DevMap>{};
auto CaptureDevices = std::vector<DevMap>{};

auto checkName(const std::span<DevMap> list, const std::string_view name) -> bool
{ return std::ranges::find(list, name, &DevMap::name) != list.end(); }

auto CALLBACK DSoundEnumDevices(GUID *guid, const WCHAR *desc, const WCHAR*, void *data) noexcept
    -> BOOL
{
    if(!guid)
        return TRUE;

    auto& devices = *static_cast<std::vector<DevMap>*>(data);
    const auto basename = wstr_to_utf8(desc);

    auto count = 1;
    auto newname = basename;
    while(checkName(devices, newname))
        newname = fmt::format("{} #{}", basename, ++count);
    const DevMap &newentry = devices.emplace_back(std::move(newname), *guid);

    auto *guidstr = LPOLESTR{};
    if(const auto hr = StringFromCLSID(*guid, &guidstr); SUCCEEDED(hr))
    {
        TRACE("Got device \"{}\", GUID \"{}\"", newentry.name, wstr_to_utf8(guidstr));
        CoTaskMemFree(guidstr);
    }

    return TRUE;
}


struct DSoundPlayback final : public BackendBase {
    explicit DSoundPlayback(DeviceBase *device) noexcept : BackendBase{device} { }
    ~DSoundPlayback() override;

    int mixerProc();

    void open(std::string_view name) override;
    bool reset() override;
    void start() override;
    void stop() override;

    ComPtr<IDirectSound>       mDS;
    ComPtr<IDirectSoundBuffer> mPrimaryBuffer;
    ComPtr<IDirectSoundBuffer> mBuffer;
    ComPtr<IDirectSoundNotify> mNotifies;
    HANDLE mNotifyEvent{nullptr};

    std::atomic<bool> mKillNow{true};
    std::thread mThread;
};

DSoundPlayback::~DSoundPlayback()
{
    mNotifies = nullptr;
    mBuffer = nullptr;
    mPrimaryBuffer = nullptr;
    mDS = nullptr;

    if(mNotifyEvent)
        CloseHandle(mNotifyEvent);
    mNotifyEvent = nullptr;
}


FORCE_ALIGN int DSoundPlayback::mixerProc()
{
    SetRTPriority();
    althrd_setname(GetMixerThreadName());

    auto DSBCaps = DSBCAPS{};
    DSBCaps.dwSize = sizeof(DSBCaps);
    auto err = mBuffer->GetCaps(&DSBCaps);
    if(FAILED(err))
    {
        ERR("Failed to get buffer caps: {:#x}", as_unsigned(err));
        mDevice->handleDisconnect("Failure retrieving playback buffer info: {:#x}",
            as_unsigned(err));
        return 1;
    }

    const auto FrameStep = size_t{mDevice->channelsFromFmt()};
    auto FrameSize = DWORD{mDevice->frameSizeFromFmt()};
    auto FragSize = DWORD{mDevice->mUpdateSize} * FrameSize;

    auto Playing = false;
    auto LastCursor = DWORD{0};
    mBuffer->GetCurrentPosition(&LastCursor, nullptr);
    while(!mKillNow.load(std::memory_order_acquire)
        && mDevice->Connected.load(std::memory_order_acquire))
    {
        // Get current play cursor
        auto PlayCursor = DWORD{};
        mBuffer->GetCurrentPosition(&PlayCursor, nullptr);
        auto avail = (PlayCursor-LastCursor+DSBCaps.dwBufferBytes) % DSBCaps.dwBufferBytes;

        if(avail < FragSize)
        {
            if(!Playing)
            {
                err = mBuffer->Play(0, 0, DSBPLAY_LOOPING);
                if(FAILED(err))
                {
                    ERR("Failed to play buffer: {:#x}", as_unsigned(err));
                    mDevice->handleDisconnect("Failure starting playback: {:#x}",
                        as_unsigned(err));
                    return 1;
                }
                Playing = true;
            }

            avail = WaitForSingleObjectEx(mNotifyEvent, 2000, FALSE);
            if(avail != WAIT_OBJECT_0)
                ERR("WaitForSingleObjectEx error: {:#x}", avail);
            continue;
        }
        avail -= avail%FragSize;

        // Lock output buffer
        auto *WritePtr1 = LPVOID{};
        auto *WritePtr2 = LPVOID{};
        auto WriteCnt1 = DWORD{};
        auto WriteCnt2 = DWORD{};
        err = mBuffer->Lock(LastCursor, avail, &WritePtr1, &WriteCnt1, &WritePtr2, &WriteCnt2, 0);

        // If the buffer is lost, restore it and lock
        if(err == DSERR_BUFFERLOST)
        {
            WARN("Buffer lost, restoring...");
            err = mBuffer->Restore();
            if(SUCCEEDED(err))
            {
                Playing = false;
                LastCursor = 0;
                err = mBuffer->Lock(0, DSBCaps.dwBufferBytes, &WritePtr1, &WriteCnt1,
                                    &WritePtr2, &WriteCnt2, 0);
            }
        }
        if(FAILED(err))
        {
            ERR("Buffer lock error: {:#x}", as_unsigned(err));
            mDevice->handleDisconnect("Failed to lock output buffer: {:#x}", as_unsigned(err));
            return 1;
        }

        mDevice->renderSamples(WritePtr1, WriteCnt1/FrameSize, FrameStep);
        if(WriteCnt2 > 0)
            mDevice->renderSamples(WritePtr2, WriteCnt2/FrameSize, FrameStep);

        mBuffer->Unlock(WritePtr1, WriteCnt1, WritePtr2, WriteCnt2);

        // Update old write cursor location
        LastCursor += WriteCnt1+WriteCnt2;
        LastCursor %= DSBCaps.dwBufferBytes;
    }

    return 0;
}

void DSoundPlayback::open(std::string_view name)
{
    auto hr = HRESULT{};
    if(PlaybackDevices.empty())
    {
        /* Initialize COM to prevent name truncation */
        const auto com = ComWrapper{};
        hr = DirectSoundEnumerateW(DSoundEnumDevices, &PlaybackDevices);
        if(FAILED(hr))
            ERR("Error enumerating DirectSound devices: {:#x}", as_unsigned(hr));
    }

    auto *guid = LPCGUID{nullptr};
    if(name.empty() && !PlaybackDevices.empty())
    {
        name = PlaybackDevices[0].name;
        guid = &PlaybackDevices[0].guid;
    }
    else
    {
        auto iter = std::ranges::find(PlaybackDevices, name, &DevMap::name);
        if(iter == PlaybackDevices.end())
        {
            auto id = GUID{};
            hr = CLSIDFromString(utf8_to_wstr(name).c_str(), &id);
            if(SUCCEEDED(hr))
                iter = std::ranges::find(PlaybackDevices, id, &DevMap::guid);
            if(iter == PlaybackDevices.end())
                throw al::backend_exception{al::backend_error::NoDevice,
                    "Device name \"{}\" not found", name};
        }
        guid = &iter->guid;
    }

    hr = DS_OK;
    if(!mNotifyEvent)
    {
        mNotifyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if(!mNotifyEvent) hr = E_FAIL;
    }

    //DirectSound Init code
    auto ds = ComPtr<IDirectSound>{};
    if(SUCCEEDED(hr))
        hr = DirectSoundCreate(guid, al::out_ptr(ds), nullptr);
    if(SUCCEEDED(hr))
        hr = ds->SetCooperativeLevel(GetForegroundWindow(), DSSCL_PRIORITY);
    if(FAILED(hr))
        throw al::backend_exception{al::backend_error::DeviceError, "Device init failed: {:#x}",
            as_unsigned(hr)};

    mNotifies = nullptr;
    mBuffer = nullptr;
    mPrimaryBuffer = nullptr;
    mDS = std::move(ds);

    mDeviceName = name;
}

bool DSoundPlayback::reset()
{
    mNotifies = nullptr;
    mBuffer = nullptr;
    mPrimaryBuffer = nullptr;

    switch(mDevice->FmtType)
    {
    case DevFmtByte:
        mDevice->FmtType = DevFmtUByte;
        break;
    case DevFmtFloat:
        if(mDevice->Flags.test(SampleTypeRequest))
            break;
        [[fallthrough]];
    case DevFmtUShort:
        mDevice->FmtType = DevFmtShort;
        break;
    case DevFmtUInt:
        mDevice->FmtType = DevFmtInt;
        break;
    case DevFmtUByte:
    case DevFmtShort:
    case DevFmtInt:
        break;
    }

    WAVEFORMATEXTENSIBLE OutputType{};
    DWORD speakers{};
    HRESULT hr{mDS->GetSpeakerConfig(&speakers)};
    if(FAILED(hr))
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to get speaker config: {:#x}", as_unsigned(hr)};

    speakers = DSSPEAKER_CONFIG(speakers);
    if(!mDevice->Flags.test(ChannelsRequest))
    {
        if(speakers == DSSPEAKER_MONO)
            mDevice->FmtChans = DevFmtMono;
        else if(speakers == DSSPEAKER_STEREO || speakers == DSSPEAKER_HEADPHONE)
            mDevice->FmtChans = DevFmtStereo;
        else if(speakers == DSSPEAKER_QUAD)
            mDevice->FmtChans = DevFmtQuad;
        else if(speakers == DSSPEAKER_5POINT1_SURROUND || speakers == DSSPEAKER_5POINT1_BACK)
            mDevice->FmtChans = DevFmtX51;
        else if(speakers == DSSPEAKER_7POINT1 || speakers == DSSPEAKER_7POINT1_SURROUND)
            mDevice->FmtChans = DevFmtX71;
        else
            ERR("Unknown system speaker config: {:#x}", speakers);
    }
    mDevice->Flags.set(DirectEar, (speakers == DSSPEAKER_HEADPHONE));
    const bool isRear51{speakers == DSSPEAKER_5POINT1_BACK};

    switch(mDevice->FmtChans)
    {
    case DevFmtMono: OutputType.dwChannelMask = MONO; break;
    case DevFmtAmbi3D: mDevice->FmtChans = DevFmtStereo;
        [[fallthrough]];
    case DevFmtStereo: OutputType.dwChannelMask = STEREO; break;
    case DevFmtQuad: OutputType.dwChannelMask = QUAD; break;
    case DevFmtX51: OutputType.dwChannelMask = isRear51 ? X5DOT1REAR : X5DOT1; break;
    case DevFmtX61: OutputType.dwChannelMask = X6DOT1; break;
    case DevFmtX71: OutputType.dwChannelMask = X7DOT1; break;
    case DevFmtX7144: mDevice->FmtChans = DevFmtX714;
        [[fallthrough]];
    case DevFmtX714: OutputType.dwChannelMask = X7DOT1DOT4; break;
    case DevFmtX3D71: OutputType.dwChannelMask = X7DOT1; break;
    }

    do {
        hr = S_OK;
        OutputType.Format.wFormatTag = WAVE_FORMAT_PCM;
        OutputType.Format.nChannels = static_cast<WORD>(mDevice->channelsFromFmt());
        OutputType.Format.wBitsPerSample = static_cast<WORD>(mDevice->bytesFromFmt() * 8);
        OutputType.Format.nBlockAlign = static_cast<WORD>(OutputType.Format.nChannels *
            OutputType.Format.wBitsPerSample / 8);
        OutputType.Format.nSamplesPerSec = mDevice->mSampleRate;
        OutputType.Format.nAvgBytesPerSec = OutputType.Format.nSamplesPerSec *
            OutputType.Format.nBlockAlign;
        OutputType.Format.cbSize = 0;

        if(OutputType.Format.nChannels > 2 || mDevice->FmtType == DevFmtFloat)
        {
            OutputType.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) */
            OutputType.Samples.wValidBitsPerSample = OutputType.Format.wBitsPerSample;
            OutputType.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
            if(mDevice->FmtType == DevFmtFloat)
                OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            else
                OutputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

            mPrimaryBuffer = nullptr;
        }
        else
        {
            if(SUCCEEDED(hr) && !mPrimaryBuffer)
            {
                auto DSBDescription = DSBUFFERDESC{};
                DSBDescription.dwSize = sizeof(DSBDescription);
                DSBDescription.dwFlags = DSBCAPS_PRIMARYBUFFER;
                hr = mDS->CreateSoundBuffer(&DSBDescription, al::out_ptr(mPrimaryBuffer), nullptr);
            }
            if(SUCCEEDED(hr))
                hr = mPrimaryBuffer->SetFormat(&OutputType.Format);
        }

        if(FAILED(hr))
            break;

        auto num_updates = mDevice->mBufferSize / mDevice->mUpdateSize;
        if(num_updates > MAX_UPDATES)
            num_updates = MAX_UPDATES;
        mDevice->mBufferSize = mDevice->mUpdateSize * num_updates;

        auto DSBDescription = DSBUFFERDESC{};
        DSBDescription.dwSize = sizeof(DSBDescription);
        DSBDescription.dwFlags = DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GETCURRENTPOSITION2
            | DSBCAPS_GLOBALFOCUS;
        DSBDescription.dwBufferBytes = mDevice->mBufferSize * OutputType.Format.nBlockAlign;
        DSBDescription.lpwfxFormat = &OutputType.Format;

        hr = mDS->CreateSoundBuffer(&DSBDescription, al::out_ptr(mBuffer), nullptr);
        if(SUCCEEDED(hr) || mDevice->FmtType != DevFmtFloat)
            break;
        mDevice->FmtType = DevFmtShort;
    } while(FAILED(hr));

    if(SUCCEEDED(hr))
    {
        hr = mBuffer->QueryInterface(IID_IDirectSoundNotify, al::out_ptr(mNotifies));
        if(SUCCEEDED(hr))
        {
            auto num_updates = mDevice->mBufferSize / mDevice->mUpdateSize;
            assert(num_updates <= MAX_UPDATES);

            auto nots = std::array<DSBPOSITIONNOTIFY,MAX_UPDATES>{};
            for(auto i = 0u;i < num_updates;++i)
            {
                nots[i].dwOffset = i * mDevice->mUpdateSize * OutputType.Format.nBlockAlign;
                nots[i].hEventNotify = mNotifyEvent;
            }
            if(mNotifies->SetNotificationPositions(num_updates, nots.data()) != DS_OK)
                hr = E_FAIL;
        }
    }

    if(FAILED(hr))
    {
        mNotifies = nullptr;
        mBuffer = nullptr;
        mPrimaryBuffer = nullptr;
        return false;
    }

    ResetEvent(mNotifyEvent);
    setDefaultWFXChannelOrder();

    return true;
}

void DSoundPlayback::start()
{
    try {
        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{&DSoundPlayback::mixerProc, this};
    }
    catch(std::exception& e) {
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start mixing thread: {}", e.what()};
    }
}

void DSoundPlayback::stop()
{
    if(mKillNow.exchange(true, std::memory_order_acq_rel) || !mThread.joinable())
        return;
    mThread.join();

    mBuffer->Stop();
}


struct DSoundCapture final : public BackendBase {
    explicit DSoundCapture(DeviceBase *device) noexcept : BackendBase{device} { }
    ~DSoundCapture() override;

    void open(std::string_view name) override;
    void start() override;
    void stop() override;
    void captureSamples(std::span<std::byte> outbuffer) override;
    auto availableSamples() -> uint override;

    ComPtr<IDirectSoundCapture> mDSC;
    ComPtr<IDirectSoundCaptureBuffer> mDSCbuffer;
    DWORD mBufferBytes{0u};
    DWORD mCursor{0u};

    RingBufferPtr<std::byte> mRing;
};

DSoundCapture::~DSoundCapture()
{
    if(mDSCbuffer)
    {
        mDSCbuffer->Stop();
        mDSCbuffer = nullptr;
    }
    mDSC = nullptr;
}


void DSoundCapture::open(std::string_view name)
{
    auto hr = HRESULT{};
    if(CaptureDevices.empty())
    {
        /* Initialize COM to prevent name truncation */
        auto com = ComWrapper{};
        hr = DirectSoundCaptureEnumerateW(DSoundEnumDevices, &CaptureDevices);
        if(FAILED(hr))
            ERR("Error enumerating DirectSound devices: {:#x}", as_unsigned(hr));
    }

    const GUID *guid{nullptr};
    if(name.empty() && !CaptureDevices.empty())
    {
        name = CaptureDevices[0].name;
        guid = &CaptureDevices[0].guid;
    }
    else
    {
        auto iter = std::ranges::find(CaptureDevices, name, &DevMap::name);
        if(iter == CaptureDevices.end())
        {
            auto id = GUID{};
            hr = CLSIDFromString(utf8_to_wstr(name).c_str(), &id);
            if(SUCCEEDED(hr))
                iter = std::ranges::find(CaptureDevices, id, &DevMap::guid);
            if(iter == CaptureDevices.end())
                throw al::backend_exception{al::backend_error::NoDevice,
                    "Device name \"{}\" not found", name};
        }
        guid = &iter->guid;
    }

    switch(mDevice->FmtType)
    {
    case DevFmtByte:
    case DevFmtUShort:
    case DevFmtUInt:
        WARN("{} capture samples not supported", DevFmtTypeString(mDevice->FmtType));
        throw al::backend_exception{al::backend_error::DeviceError,
            "{} capture samples not supported", DevFmtTypeString(mDevice->FmtType)};

    case DevFmtUByte:
    case DevFmtShort:
    case DevFmtInt:
    case DevFmtFloat:
        break;
    }

    auto InputType = WAVEFORMATEXTENSIBLE{};
    switch(mDevice->FmtChans)
    {
    case DevFmtMono: InputType.dwChannelMask = MONO; break;
    case DevFmtStereo: InputType.dwChannelMask = STEREO; break;
    case DevFmtQuad: InputType.dwChannelMask = QUAD; break;
    case DevFmtX51: InputType.dwChannelMask = X5DOT1; break;
    case DevFmtX61: InputType.dwChannelMask = X6DOT1; break;
    case DevFmtX71: InputType.dwChannelMask = X7DOT1; break;
    case DevFmtX714: InputType.dwChannelMask = X7DOT1DOT4; break;
    case DevFmtX7144:
    case DevFmtX3D71:
    case DevFmtAmbi3D:
        WARN("{} capture not supported", DevFmtChannelsString(mDevice->FmtChans));
        throw al::backend_exception{al::backend_error::DeviceError, "{} capture not supported",
            DevFmtChannelsString(mDevice->FmtChans)};
    }

    InputType.Format.wFormatTag = WAVE_FORMAT_PCM;
    InputType.Format.nChannels = static_cast<WORD>(mDevice->channelsFromFmt());
    InputType.Format.wBitsPerSample = static_cast<WORD>(mDevice->bytesFromFmt() * 8);
    InputType.Format.nBlockAlign = static_cast<WORD>(InputType.Format.nChannels *
        InputType.Format.wBitsPerSample / 8);
    InputType.Format.nSamplesPerSec = mDevice->mSampleRate;
    InputType.Format.nAvgBytesPerSec = InputType.Format.nSamplesPerSec *
        InputType.Format.nBlockAlign;
    InputType.Format.cbSize = 0;
    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) */
    InputType.Samples.wValidBitsPerSample = InputType.Format.wBitsPerSample;
    if(mDevice->FmtType == DevFmtFloat)
        InputType.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    else
        InputType.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

    if(InputType.Format.nChannels > 2 || mDevice->FmtType == DevFmtFloat)
    {
        InputType.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        InputType.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    }

    const auto samples = std::max(mDevice->mBufferSize, mDevice->mSampleRate/10u);

    auto DSCBDescription = DSCBUFFERDESC{};
    DSCBDescription.dwSize = sizeof(DSCBDescription);
    DSCBDescription.dwFlags = 0;
    DSCBDescription.dwBufferBytes = samples * InputType.Format.nBlockAlign;
    DSCBDescription.lpwfxFormat = &InputType.Format;

    //DirectSoundCapture Init code
    hr = DirectSoundCaptureCreate(guid, al::out_ptr(mDSC), nullptr);
    if(SUCCEEDED(hr))
        mDSC->CreateCaptureBuffer(&DSCBDescription, al::out_ptr(mDSCbuffer), nullptr);
    if(SUCCEEDED(hr))
         mRing = RingBuffer<std::byte>::Create(mDevice->mBufferSize, InputType.Format.nBlockAlign,
            false);

    if(FAILED(hr))
    {
        mRing = nullptr;
        mDSCbuffer = nullptr;
        mDSC = nullptr;

        throw al::backend_exception{al::backend_error::DeviceError, "Device init failed: {:#x}",
            as_unsigned(hr)};
    }

    mBufferBytes = DSCBDescription.dwBufferBytes;
    setDefaultWFXChannelOrder();

    mDeviceName = name;
}

void DSoundCapture::start()
{
    if(const auto hr = mDSCbuffer->Start(DSCBSTART_LOOPING); FAILED(hr))
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failure starting capture: {:#x}", as_unsigned(hr)};
}

void DSoundCapture::stop()
{
    if(const auto hr = mDSCbuffer->Stop(); FAILED(hr))
    {
        ERR("stop failed: {:#x}", as_unsigned(hr));
        mDevice->handleDisconnect("Failure stopping capture: {:#x}", as_unsigned(hr));
    }
}

void DSoundCapture::captureSamples(std::span<std::byte> outbuffer)
{ std::ignore = mRing->read(outbuffer); }

uint DSoundCapture::availableSamples()
{
    if(mDevice->Connected.load(std::memory_order_acquire))
    {
        const auto BufferBytes = mBufferBytes;
        const auto LastCursor = mCursor;

        auto ReadCursor = DWORD{};
        auto *ReadPtr1 = LPVOID{};
        auto *ReadPtr2 = LPVOID{};
        auto ReadCnt1 = DWORD{};
        auto ReadCnt2 = DWORD{};
        auto hr = mDSCbuffer->GetCurrentPosition(nullptr, &ReadCursor);
        if(SUCCEEDED(hr))
        {
            const auto NumBytes = (BufferBytes+ReadCursor-LastCursor) % BufferBytes;
            if(!NumBytes) return static_cast<uint>(mRing->readSpace());
            hr = mDSCbuffer->Lock(LastCursor, NumBytes, &ReadPtr1, &ReadCnt1, &ReadPtr2, &ReadCnt2,
                0);
        }
        if(SUCCEEDED(hr))
        {
            std::ignore = mRing->write(std::span{static_cast<std::byte*>(ReadPtr1), ReadCnt1});
            if(ReadPtr2 != nullptr && ReadCnt2 > 0)
                std::ignore = mRing->write(std::span{static_cast<std::byte*>(ReadPtr2), ReadCnt2});
            hr = mDSCbuffer->Unlock(ReadPtr1, ReadCnt1, ReadPtr2, ReadCnt2);
            mCursor = ReadCursor;
        }

        if(FAILED(hr))
        {
            ERR("update failed: {:#x}", as_unsigned(hr));
            mDevice->handleDisconnect("Failure retrieving capture data: {:#x}", as_unsigned(hr));
        }
    }

    return static_cast<uint>(mRing->readSpace());
}

} // namespace


BackendFactory &DSoundBackendFactory::getFactory()
{
    static DSoundBackendFactory factory{};
    return factory;
}

auto DSoundBackendFactory::init() -> bool
{
#if HAVE_DYNLOAD
    if(!ds_handle)
    {
        if(auto libresult = LoadLib("dsound.dll"))
            ds_handle = libresult.value();
        else
        {
            WARN("Failed to load dsound.dll: {}", libresult.error());
            return false;
        }

        static constexpr auto load_func = [](auto *&func, const char *name) -> bool
        {
            using func_t = std::remove_reference_t<decltype(func)>;
            auto funcresult = GetSymbol(ds_handle, name);
            if(!funcresult)
            {
                WARN("Failed to load function {}: {}", name, funcresult.error());
                return false;
            }
            /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
            func = reinterpret_cast<func_t>(funcresult.value());
            return true;
        };
        auto ok = true;
#define LOAD_FUNC(f) ok &= load_func(p##f, #f)
        LOAD_FUNC(DirectSoundCreate);
        LOAD_FUNC(DirectSoundEnumerateW);
        LOAD_FUNC(DirectSoundCaptureCreate);
        LOAD_FUNC(DirectSoundCaptureEnumerateW);
#undef LOAD_FUNC
        if(!ok)
        {
            CloseLib(ds_handle);
            ds_handle = nullptr;
            return false;
        }
    }
#endif
    return true;
}

bool DSoundBackendFactory::querySupport(BackendType type)
{ return (type == BackendType::Playback || type == BackendType::Capture); }

auto DSoundBackendFactory::enumerate(BackendType type) -> std::vector<std::string>
{
    std::vector<std::string> outnames;
    auto add_device = [&outnames](const DevMap &entry) -> void
    { outnames.emplace_back(entry.name); };

    /* Initialize COM to prevent name truncation */
    const auto com = ComWrapper{};
    switch(type)
    {
    case BackendType::Playback:
        PlaybackDevices.clear();
        if(const auto hr = DirectSoundEnumerateW(DSoundEnumDevices, &PlaybackDevices); FAILED(hr))
            ERR("Error enumerating DirectSound playback devices: {:#x}", as_unsigned(hr));
        outnames.reserve(PlaybackDevices.size());
        std::ranges::for_each(PlaybackDevices, add_device);
        break;

    case BackendType::Capture:
        CaptureDevices.clear();
        if(const auto hr = DirectSoundCaptureEnumerateW(DSoundEnumDevices, &CaptureDevices);
            FAILED(hr))
            ERR("Error enumerating DirectSound capture devices: {:#x}", as_unsigned(hr));
        outnames.reserve(CaptureDevices.size());
        std::ranges::for_each(CaptureDevices, add_device);
        break;
    }

    return outnames;
}

BackendPtr DSoundBackendFactory::createBackend(DeviceBase *device, BackendType type)
{
    if(type == BackendType::Playback)
        return BackendPtr{new DSoundPlayback{device}};
    if(type == BackendType::Capture)
        return BackendPtr{new DSoundCapture{device}};
    return nullptr;
}
