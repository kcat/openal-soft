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

#include "backends/wave.h"

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <errno.h>

#include <chrono>
#include <thread>
#include <vector>

#include "alMain.h"
#include "alu.h"
#include "alconfig.h"
#include "compat.h"


namespace {

using std::chrono::seconds;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;

constexpr ALCchar waveDevice[] = "Wave File Writer";

constexpr ALubyte SUBTYPE_PCM[]{
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xaa,
    0x00, 0x38, 0x9b, 0x71
};
constexpr ALubyte SUBTYPE_FLOAT[]{
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xaa,
    0x00, 0x38, 0x9b, 0x71
};

constexpr ALubyte SUBTYPE_BFORMAT_PCM[]{
    0x01, 0x00, 0x00, 0x00, 0x21, 0x07, 0xd3, 0x11, 0x86, 0x44, 0xc8, 0xc1,
    0xca, 0x00, 0x00, 0x00
};

constexpr ALubyte SUBTYPE_BFORMAT_FLOAT[]{
    0x03, 0x00, 0x00, 0x00, 0x21, 0x07, 0xd3, 0x11, 0x86, 0x44, 0xc8, 0xc1,
    0xca, 0x00, 0x00, 0x00
};

void fwrite16le(ALushort val, FILE *f)
{
    ALubyte data[2]{ static_cast<ALubyte>(val&0xff), static_cast<ALubyte>((val>>8)&0xff) };
    fwrite(data, 1, 2, f);
}

void fwrite32le(ALuint val, FILE *f)
{
    ALubyte data[4]{ static_cast<ALubyte>(val&0xff), static_cast<ALubyte>((val>>8)&0xff),
        static_cast<ALubyte>((val>>16)&0xff), static_cast<ALubyte>((val>>24)&0xff) };
    fwrite(data, 1, 4, f);
}


struct WaveBackend final : public ALCbackend {
    WaveBackend(ALCdevice *device) noexcept : ALCbackend{device} { }
    ~WaveBackend() override;

    int mixerProc();

    FILE *mFile{nullptr};
    long mDataStart{-1};

    al::vector<ALbyte> mBuffer;

    std::atomic<ALenum> mKillNow{AL_TRUE};
    std::thread mThread;

    static constexpr inline const char *CurrentPrefix() noexcept { return "WaveBackend::"; }
};

void WaveBackend_Construct(WaveBackend *self, ALCdevice *device);
void WaveBackend_Destruct(WaveBackend *self);
ALCenum WaveBackend_open(WaveBackend *self, const ALCchar *name);
ALCboolean WaveBackend_reset(WaveBackend *self);
ALCboolean WaveBackend_start(WaveBackend *self);
void WaveBackend_stop(WaveBackend *self);
DECLARE_FORWARD2(WaveBackend, ALCbackend, ALCenum, captureSamples, void*, ALCuint)
DECLARE_FORWARD(WaveBackend, ALCbackend, ALCuint, availableSamples)
DECLARE_FORWARD(WaveBackend, ALCbackend, ClockLatency, getClockLatency)
DECLARE_FORWARD(WaveBackend, ALCbackend, void, lock)
DECLARE_FORWARD(WaveBackend, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(WaveBackend)

DEFINE_ALCBACKEND_VTABLE(WaveBackend);

void WaveBackend_Construct(WaveBackend *self, ALCdevice *device)
{
    new (self) WaveBackend{device};
    SET_VTABLE2(WaveBackend, ALCbackend, self);
}

void WaveBackend_Destruct(WaveBackend *self)
{ self->~WaveBackend(); }

WaveBackend::~WaveBackend()
{
    if(mFile)
        fclose(mFile);
    mFile = nullptr;
}

int WaveBackend::mixerProc()
{
    const milliseconds restTime{mDevice->UpdateSize*1000/mDevice->Frequency / 2};

    althrd_setname(MIXER_THREAD_NAME);

    const ALsizei frameSize{mDevice->frameSizeFromFmt()};

    ALint64 done{0};
    auto start = std::chrono::steady_clock::now();
    while(!mKillNow.load(std::memory_order_acquire) &&
          mDevice->Connected.load(std::memory_order_acquire))
    {
        auto now = std::chrono::steady_clock::now();

        /* This converts from nanoseconds to nanosamples, then to samples. */
        ALint64 avail{std::chrono::duration_cast<seconds>((now-start) *
            mDevice->Frequency).count()};
        if(avail-done < mDevice->UpdateSize)
        {
            std::this_thread::sleep_for(restTime);
            continue;
        }
        while(avail-done >= mDevice->UpdateSize)
        {
            WaveBackend_lock(this);
            aluMixData(mDevice, mBuffer.data(), mDevice->UpdateSize);
            WaveBackend_unlock(this);
            done += mDevice->UpdateSize;

            if(!IS_LITTLE_ENDIAN)
            {
                const ALsizei bytesize{mDevice->bytesFromFmt()};
                ALsizei i;

                if(bytesize == 2)
                {
                    ALushort *samples = reinterpret_cast<ALushort*>(mBuffer.data());
                    const auto len = static_cast<ALsizei>(mBuffer.size() / 2);
                    for(i = 0;i < len;i++)
                    {
                        ALushort samp = samples[i];
                        samples[i] = (samp>>8) | (samp<<8);
                    }
                }
                else if(bytesize == 4)
                {
                    ALuint *samples = reinterpret_cast<ALuint*>(mBuffer.data());
                    const auto len = static_cast<ALsizei>(mBuffer.size() / 4);
                    for(i = 0;i < len;i++)
                    {
                        ALuint samp = samples[i];
                        samples[i] = (samp>>24) | ((samp>>8)&0x0000ff00) |
                                     ((samp<<8)&0x00ff0000) | (samp<<24);
                    }
                }
            }

            size_t fs{fwrite(mBuffer.data(), frameSize, mDevice->UpdateSize, mFile)};
            (void)fs;
            if(ferror(mFile))
            {
                ERR("Error writing to file\n");
                WaveBackend_lock(this);
                aluHandleDisconnect(mDevice, "Failed to write playback samples");
                WaveBackend_unlock(this);
                break;
            }
        }

        /* For every completed second, increment the start time and reduce the
         * samples done. This prevents the difference between the start time
         * and current time from growing too large, while maintaining the
         * correct number of samples to render.
         */
        if(done >= mDevice->Frequency)
        {
            seconds s{done/mDevice->Frequency};
            start += s;
            done -= mDevice->Frequency*s.count();
        }
    }

    return 0;
}


ALCenum WaveBackend_open(WaveBackend *self, const ALCchar *name)
{
    const char *fname{GetConfigValue(nullptr, "wave", "file", "")};
    if(!fname[0]) return ALC_INVALID_VALUE;

    if(!name)
        name = waveDevice;
    else if(strcmp(name, waveDevice) != 0)
        return ALC_INVALID_VALUE;

#ifdef _WIN32
    {
        std::wstring wname = utf8_to_wstr(fname);
        self->mFile = _wfopen(wname.c_str(), L"wb");
    }
#else
    self->mFile = fopen(fname, "wb");
#endif
    if(!self->mFile)
    {
        ERR("Could not open file '%s': %s\n", fname, strerror(errno));
        return ALC_INVALID_VALUE;
    }

    ALCdevice *device{self->mDevice};
    device->DeviceName = name;

    return ALC_NO_ERROR;
}

ALCboolean WaveBackend_reset(WaveBackend *self)
{
    ALCdevice *device{self->mDevice};
    ALuint channels=0, bits=0, chanmask=0;
    int isbformat = 0;
    size_t val;

    fseek(self->mFile, 0, SEEK_SET);
    clearerr(self->mFile);

    if(GetConfigValueBool(nullptr, "wave", "bformat", 0))
    {
        device->FmtChans = DevFmtAmbi3D;
        device->mAmbiOrder = 1;
    }

    switch(device->FmtType)
    {
        case DevFmtByte:
            device->FmtType = DevFmtUByte;
            break;
        case DevFmtUShort:
            device->FmtType = DevFmtShort;
            break;
        case DevFmtUInt:
            device->FmtType = DevFmtInt;
            break;
        case DevFmtUByte:
        case DevFmtShort:
        case DevFmtInt:
        case DevFmtFloat:
            break;
    }
    switch(device->FmtChans)
    {
        case DevFmtMono:   chanmask = 0x04; break;
        case DevFmtStereo: chanmask = 0x01 | 0x02; break;
        case DevFmtQuad:   chanmask = 0x01 | 0x02 | 0x10 | 0x20; break;
        case DevFmtX51: chanmask = 0x01 | 0x02 | 0x04 | 0x08 | 0x200 | 0x400; break;
        case DevFmtX51Rear: chanmask = 0x01 | 0x02 | 0x04 | 0x08 | 0x010 | 0x020; break;
        case DevFmtX61: chanmask = 0x01 | 0x02 | 0x04 | 0x08 | 0x100 | 0x200 | 0x400; break;
        case DevFmtX71: chanmask = 0x01 | 0x02 | 0x04 | 0x08 | 0x010 | 0x020 | 0x200 | 0x400; break;
        case DevFmtAmbi3D:
            /* .amb output requires FuMa */
            device->mAmbiOrder = mini(device->mAmbiOrder, 3);
            device->mAmbiLayout = AmbiLayout::FuMa;
            device->mAmbiScale = AmbiNorm::FuMa;
            isbformat = 1;
            chanmask = 0;
            break;
    }
    bits = device->bytesFromFmt() * 8;
    channels = device->channelsFromFmt();

    fputs("RIFF", self->mFile);
    fwrite32le(0xFFFFFFFF, self->mFile); // 'RIFF' header len; filled in at close

    fputs("WAVE", self->mFile);

    fputs("fmt ", self->mFile);
    fwrite32le(40, self->mFile); // 'fmt ' header len; 40 bytes for EXTENSIBLE

    // 16-bit val, format type id (extensible: 0xFFFE)
    fwrite16le(0xFFFE, self->mFile);
    // 16-bit val, channel count
    fwrite16le(channels, self->mFile);
    // 32-bit val, frequency
    fwrite32le(device->Frequency, self->mFile);
    // 32-bit val, bytes per second
    fwrite32le(device->Frequency * channels * bits / 8, self->mFile);
    // 16-bit val, frame size
    fwrite16le(channels * bits / 8, self->mFile);
    // 16-bit val, bits per sample
    fwrite16le(bits, self->mFile);
    // 16-bit val, extra byte count
    fwrite16le(22, self->mFile);
    // 16-bit val, valid bits per sample
    fwrite16le(bits, self->mFile);
    // 32-bit val, channel mask
    fwrite32le(chanmask, self->mFile);
    // 16 byte GUID, sub-type format
    val = fwrite((device->FmtType == DevFmtFloat) ?
                 (isbformat ? SUBTYPE_BFORMAT_FLOAT : SUBTYPE_FLOAT) :
                 (isbformat ? SUBTYPE_BFORMAT_PCM : SUBTYPE_PCM), 1, 16, self->mFile);
    (void)val;

    fputs("data", self->mFile);
    fwrite32le(0xFFFFFFFF, self->mFile); // 'data' header len; filled in at close

    if(ferror(self->mFile))
    {
        ERR("Error writing header: %s\n", strerror(errno));
        return ALC_FALSE;
    }
    self->mDataStart = ftell(self->mFile);

    SetDefaultWFXChannelOrder(device);

    const ALuint bufsize{device->frameSizeFromFmt() * device->UpdateSize};
    self->mBuffer.resize(bufsize);

    return ALC_TRUE;
}

ALCboolean WaveBackend_start(WaveBackend *self)
{
    try {
        self->mKillNow.store(AL_FALSE, std::memory_order_release);
        self->mThread = std::thread{std::mem_fn(&WaveBackend::mixerProc), self};
        return ALC_TRUE;
    }
    catch(std::exception& e) {
        ERR("Failed to start mixing thread: %s\n", e.what());
    }
    catch(...) {
    }
    return ALC_FALSE;
}

void WaveBackend_stop(WaveBackend *self)
{
    if(self->mKillNow.exchange(AL_TRUE, std::memory_order_acq_rel) || !self->mThread.joinable())
        return;
    self->mThread.join();

    long size{ftell(self->mFile)};
    if(size > 0)
    {
        long dataLen{size - self->mDataStart};
        if(fseek(self->mFile, self->mDataStart-4, SEEK_SET) == 0)
            fwrite32le(dataLen, self->mFile); // 'data' header len
        if(fseek(self->mFile, 4, SEEK_SET) == 0)
            fwrite32le(size-8, self->mFile); // 'WAVE' header len
    }
}

} // namespace


bool WaveBackendFactory::init()
{ return true; }

bool WaveBackendFactory::querySupport(ALCbackend_Type type)
{ return (type == ALCbackend_Playback); }

void WaveBackendFactory::probe(DevProbe type, std::string *outnames)
{
    switch(type)
    {
        case ALL_DEVICE_PROBE:
            /* Includes null char. */
            outnames->append(waveDevice, sizeof(waveDevice));
            break;
        case CAPTURE_DEVICE_PROBE:
            break;
    }
}

ALCbackend *WaveBackendFactory::createBackend(ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Playback)
    {
        WaveBackend *backend;
        NEW_OBJ(backend, WaveBackend)(device);
        return backend;
    }

    return nullptr;
}

BackendFactory &WaveBackendFactory::getFactory()
{
    static WaveBackendFactory factory{};
    return factory;
}
