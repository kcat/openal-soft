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

#include "wave.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <system_error>
#include <thread>
#include <vector>

#include "alc/alconfig.h"
#include "alnumeric.h"
#include "alstring.h"
#include "althrd_setname.h"
#include "core/device.h"
#include "core/logging.h"
#include "gsl/gsl"
#include "strutils.hpp"


namespace {

using namespace std::string_view_literals;
using std::chrono::seconds;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;

using ubyte = unsigned char;
using ushort = unsigned short;

using FilePtr = std::unique_ptr<FILE, decltype([](gsl::owner<FILE*> f) { fclose(f); })>;

[[nodiscard]] constexpr auto GetDeviceName() noexcept { return "Wave File Writer"sv; }

constexpr std::array<ubyte,16> SUBTYPE_PCM{{
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xaa,
    0x00, 0x38, 0x9b, 0x71
}};
constexpr std::array<ubyte,16> SUBTYPE_FLOAT{{
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xaa,
    0x00, 0x38, 0x9b, 0x71
}};

constexpr std::array<ubyte,16> SUBTYPE_BFORMAT_PCM{{
    0x01, 0x00, 0x00, 0x00, 0x21, 0x07, 0xd3, 0x11, 0x86, 0x44, 0xc8, 0xc1,
    0xca, 0x00, 0x00, 0x00
}};

constexpr std::array<ubyte,16> SUBTYPE_BFORMAT_FLOAT{{
    0x03, 0x00, 0x00, 0x00, 0x21, 0x07, 0xd3, 0x11, 0x86, 0x44, 0xc8, 0xc1,
    0xca, 0x00, 0x00, 0x00
}};

constexpr auto MonoChannels = 0x04u;
constexpr auto StereoChannels = 0x01u | 0x02u;
constexpr auto QuadChannels = 0x01u | 0x02u | 0x10u | 0x20u;
constexpr auto X51Channels = 0x01u | 0x02u | 0x04u | 0x08u | 0x200u | 0x400u;
constexpr auto X61Channels = 0x01u | 0x02u | 0x04u | 0x08u | 0x100u | 0x200u | 0x400u;
constexpr auto X71Channels = 0x01u | 0x02u | 0x04u | 0x08u | 0x010u | 0x020u | 0x200u | 0x400u;
constexpr auto X714Channels = 0x01u | 0x02u | 0x04u | 0x08u | 0x010u | 0x020u | 0x200u | 0x400u | 0x1000u | 0x4000u | 0x8000u | 0x20000u;


void fwrite16le(ushort val, FILE *f)
{
    const auto data = std::array{gsl::narrow_cast<ubyte>(val&0xff),
        gsl::narrow_cast<ubyte>((val>>8)&0xff)};
    fwrite(data.data(), 1, data.size(), f);
}

void fwrite32le(uint val, FILE *f)
{
    const auto data = std::array{gsl::narrow_cast<ubyte>(val&0xff),
        gsl::narrow_cast<ubyte>((val>>8)&0xff), gsl::narrow_cast<ubyte>((val>>16)&0xff),
        gsl::narrow_cast<ubyte>((val>>24)&0xff)};
    fwrite(data.data(), 1, data.size(), f);
}

void fwrite16be(ushort val, FILE *f)
{
    const auto data = std::array{gsl::narrow_cast<ubyte>((val>>8)&0xff),
        gsl::narrow_cast<ubyte>(val&0xff)};
    fwrite(data.data(), 1, data.size(), f);
}

void fwrite32be(uint val, FILE *f)
{
    const auto data = std::array{gsl::narrow_cast<ubyte>((val>>24)&0xff),
        gsl::narrow_cast<ubyte>((val>>16)&0xff), gsl::narrow_cast<ubyte>((val>>8)&0xff),
        gsl::narrow_cast<ubyte>(val&0xff)};
    fwrite(data.data(), 1, data.size(), f);
}

void fwrite64be(uint64_t val, FILE *f)
{
    const auto data = std::array{gsl::narrow_cast<ubyte>((val>>56)&0xff),
        gsl::narrow_cast<ubyte>((val>>48)&0xff), gsl::narrow_cast<ubyte>((val>>40)&0xff),
        gsl::narrow_cast<ubyte>((val>>32)&0xff), gsl::narrow_cast<ubyte>((val>>24)&0xff),
        gsl::narrow_cast<ubyte>((val>>16)&0xff), gsl::narrow_cast<ubyte>((val>>8)&0xff),
        gsl::narrow_cast<ubyte>(val&0xff)};
    fwrite(data.data(), 1, data.size(), f);
}


struct WaveBackend final : public BackendBase {
    explicit WaveBackend(gsl::not_null<DeviceBase*> device) noexcept : BackendBase{device} { }
    ~WaveBackend() override;

    void mixerProc();

    void open(std::string_view name) override;
    bool reset() override;
    void start() override;
    void stop() override;

    FilePtr mFile{nullptr};
    long mDataStart{-1};

    std::vector<std::byte> mBuffer;
    bool mCAFOutput{};

    std::atomic<bool> mKillNow{true};
    std::thread mThread;
};

WaveBackend::~WaveBackend() = default;

void WaveBackend::mixerProc()
{
    const milliseconds restTime{mDevice->mUpdateSize*1000/mDevice->mSampleRate / 2};

    althrd_setname(GetMixerThreadName());

    const size_t frameStep{mDevice->channelsFromFmt()};
    const size_t frameSize{mDevice->frameSizeFromFmt()};

    auto done = int64_t{0};
    auto start = std::chrono::steady_clock::now();
    while(!mKillNow.load(std::memory_order_acquire)
        && mDevice->Connected.load(std::memory_order_acquire))
    {
        auto now = std::chrono::steady_clock::now();

        /* This converts from nanoseconds to nanosamples, then to samples. */
        const auto avail = int64_t{std::chrono::duration_cast<seconds>((now-start) *
            mDevice->mSampleRate).count()};
        if(avail-done < mDevice->mUpdateSize)
        {
            std::this_thread::sleep_for(restTime);
            continue;
        }
        while(avail-done >= mDevice->mUpdateSize)
        {
            mDevice->renderSamples(mBuffer.data(), mDevice->mUpdateSize, frameStep);
            done += mDevice->mUpdateSize;

            if constexpr(std::endian::native != std::endian::little)
            {
                if(!mCAFOutput)
                {
                    const auto bytesize = mDevice->bytesFromFmt();

                    if(bytesize == 2)
                    {
                        const auto len = mBuffer.size() & ~1_uz;
                        for(size_t i{0};i < len;i+=2)
                            std::swap(mBuffer[i], mBuffer[i+1]);
                    }
                    else if(bytesize == 4)
                    {
                        const auto len = mBuffer.size() & ~3_uz;
                        for(size_t i{0};i < len;i+=4)
                        {
                            std::swap(mBuffer[i  ], mBuffer[i+3]);
                            std::swap(mBuffer[i+1], mBuffer[i+2]);
                        }
                    }
                }
            }

            const size_t fs{fwrite(mBuffer.data(), frameSize, mDevice->mUpdateSize, mFile.get())};
            if(fs < mDevice->mUpdateSize || ferror(mFile.get()))
            {
                ERR("Error writing to file");
                mDevice->handleDisconnect("Failed to write playback samples");
                break;
            }
        }

        /* For every completed second, increment the start time and reduce the
         * samples done. This prevents the difference between the start time
         * and current time from growing too large, while maintaining the
         * correct number of samples to render.
         */
        if(done >= mDevice->mSampleRate)
        {
            const auto s = seconds{done/mDevice->mSampleRate};
            done %= mDevice->mSampleRate;
            start += s;
        }
    }
}

void WaveBackend::open(std::string_view name)
{
    auto fname = ConfigValueStr({}, "wave", "file");
    if(!fname) throw al::backend_exception{al::backend_error::NoDevice,
        "No wave output filename"};

    if(name.empty())
        name = GetDeviceName();
    else if(name != GetDeviceName())
        throw al::backend_exception{al::backend_error::NoDevice, "Device name \"{}\" not found",
            name};

    /* There's only one "device", so if it's already open, we're done. */
    if(mFile) return;

#ifdef _WIN32
    mFile = FilePtr{_wfopen(utf8_to_wstr(fname.value()).c_str(), L"wb")};
#else
    mFile = FilePtr{fopen(fname->c_str(), "wb")};
#endif
    if(!mFile)
        throw al::backend_exception{al::backend_error::DeviceError, "Could not open file '{}': {}",
            *fname, std::generic_category().message(errno)};

    mDeviceName = name;
}

bool WaveBackend::reset()
{
    mCAFOutput = false;
    if(const auto formatopt = ConfigValueStr({}, "wave"sv, "format"sv))
    {
        if(al::case_compare(*formatopt, "caf"sv) == 0)
            mCAFOutput = true;
        else if(al::case_compare(*formatopt, "wav"sv) != 0)
            WARN("Unsupported wave file format: \"{}\"", *formatopt);
    }

    if(GetConfigValueBool({}, "wave", "bformat", false))
    {
        mDevice->FmtChans = DevFmtAmbi3D;
        mDevice->mAmbiOrder = std::max(mDevice->mAmbiOrder, 1u);
    }

    switch(mDevice->FmtType)
    {
    case DevFmtByte:
    case DevFmtUByte:
        if(!mCAFOutput)
            mDevice->FmtType = DevFmtUByte;
        else
            mDevice->FmtType = DevFmtByte;
        break;
    case DevFmtUShort:
        mDevice->FmtType = DevFmtShort;
        break;
    case DevFmtUInt:
        mDevice->FmtType = DevFmtInt;
        break;
    case DevFmtShort:
    case DevFmtInt:
    case DevFmtFloat:
        break;
    }
    auto chanmask = 0u;
    auto isbformat = false;
    switch(mDevice->FmtChans)
    {
    case DevFmtMono:   chanmask = MonoChannels; break;
    case DevFmtStereo: chanmask = StereoChannels; break;
    case DevFmtQuad:   chanmask = QuadChannels; break;
    case DevFmtX51: chanmask = X51Channels; break;
    case DevFmtX61: chanmask = X61Channels; break;
    case DevFmtX71: chanmask = X71Channels; break;
    case DevFmtX7144:
        mDevice->FmtChans = DevFmtX714;
        [[fallthrough]];
    case DevFmtX714:
        chanmask = X714Channels;
        break;
    /* NOTE: Same as 7.1. */
    case DevFmtX3D71:
        chanmask = X71Channels;
        break;
    case DevFmtAmbi3D:
        if(!mCAFOutput)
        {
            /* .amb output requires FuMa */
            mDevice->mAmbiOrder = std::min(mDevice->mAmbiOrder, 3u);
            mDevice->mAmbiLayout = DevAmbiLayout::FuMa;
            mDevice->mAmbiScale = DevAmbiScaling::FuMa;
        }
        else
        {
            /* .ambix output requires ACN+SN3D */
            mDevice->mAmbiOrder = std::min(mDevice->mAmbiOrder, uint{MaxAmbiOrder});
            mDevice->mAmbiLayout = DevAmbiLayout::ACN;
            mDevice->mAmbiScale = DevAmbiScaling::SN3D;
        }
        isbformat = true;
        break;
    }
    const auto bytes = mDevice->bytesFromFmt();
    const auto channels = mDevice->channelsFromFmt();

    if(fseek(mFile.get(), 0, SEEK_CUR) != 0)
    {
        /* ESPIPE means the underlying file isn't seekable, which is fine for
         * piped output.
         */
        if(auto errcode = errno; errcode != ESPIPE)
        {
            ERR("Failed to reset file offset: {} ({})", std::generic_category().message(errcode),
                errcode);
        }
    }
    clearerr(mFile.get());

    if(!mCAFOutput)
    {
        fputs("RIFF", mFile.get());
        fwrite32le(0xFFFFFFFF, mFile.get()); // 'RIFF' header len; filled in at stop

        fputs("WAVE", mFile.get());

        fputs("fmt ", mFile.get());
        fwrite32le(40, mFile.get()); // 'fmt ' header len; 40 bytes for EXTENSIBLE

        // 16-bit val, format type id (extensible: 0xFFFE)
        fwrite16le(0xFFFE, mFile.get());
        // 16-bit val, channel count
        fwrite16le(gsl::narrow_cast<ushort>(channels), mFile.get());
        // 32-bit val, frequency
        fwrite32le(mDevice->mSampleRate, mFile.get());
        // 32-bit val, bytes per second
        fwrite32le(mDevice->mSampleRate * channels * bytes, mFile.get());
        // 16-bit val, frame size
        fwrite16le(gsl::narrow_cast<ushort>(channels * bytes), mFile.get());
        // 16-bit val, bits per sample
        fwrite16le(gsl::narrow_cast<ushort>(bytes * 8), mFile.get());
        // 16-bit val, extra byte count
        fwrite16le(22, mFile.get());
        // 16-bit val, valid bits per sample
        fwrite16le(gsl::narrow_cast<ushort>(bytes * 8), mFile.get());
        // 32-bit val, channel mask
        fwrite32le(chanmask, mFile.get());
        // 16 byte GUID, sub-type format
        std::ignore = fwrite((mDevice->FmtType == DevFmtFloat) ?
            (isbformat ? SUBTYPE_BFORMAT_FLOAT.data() : SUBTYPE_FLOAT.data()) :
            (isbformat ? SUBTYPE_BFORMAT_PCM.data() : SUBTYPE_PCM.data()), 1, 16, mFile.get());

        fputs("data", mFile.get());
        fwrite32le(0xFFFFFFFF, mFile.get()); // 'data' header len; filled in at stop
    }
    else
    {
        /* 32-bit uint, mFileType */
        fputs("caff", mFile.get());
        /* 16-bit uint, mFileVersion */
        fwrite16be(1, mFile.get());
        /* 16-bit uint, mFileFlags */
        fwrite16be(0, mFile.get());

        /* Audio Description chunk */
        fputs("desc", mFile.get());
        fwrite64be(32, mFile.get());
        /* 64-bit double, mSampleRate */
        fwrite64be(std::bit_cast<uint64_t>(gsl::narrow_cast<double>(mDevice->mSampleRate)),
            mFile.get());
        /* 32-bit uint, mFormatID */
        fputs("lpcm", mFile.get());

        const auto flags = std::invoke([this]
        {
            switch(mDevice->FmtType)
            {
            case DevFmtByte:
            case DevFmtUByte:
                break;
            case DevFmtShort:
            case DevFmtUShort:
            case DevFmtInt:
            case DevFmtUInt:
                if constexpr(std::endian::native == std::endian::little)
                    return 2u; /* kCAFLinearPCMFormatFlagIsLittleEndian */
                else
                    break;
            case DevFmtFloat:
                if constexpr(std::endian::native == std::endian::little)
                    return 3u; /* kCAFLinearPCMFormatFlagIsFloat | kCAFLinearPCMFormatFlagIsLittleEndian */
                else
                    return 1u; /* kCAFLinearPCMFormatFlagIsFloat */
            }
            return 0u;
        });

        /* 32-bit uint, mFormatFlags */
        fwrite32be(flags, mFile.get());
        /* 32-bit uint, mBytesPerPacket */
        fwrite32be(bytes*channels, mFile.get());
        /* 32-bit uint, mFramesPerPacket */
        fwrite32be(1, mFile.get());
        /* 32-bit uint, mChannelsPerFrame */
        fwrite32be(channels, mFile.get());
        /* 32-bit uint, mBitsPerChannel */
        fwrite32be(bytes*8, mFile.get());

        if(chanmask != 0)
        {
            /* Channel Layout chunk */
            fputs("chan", mFile.get());
            fwrite64be(12, mFile.get());

            /* 32-bit uint, mChannelLayoutTag */
            fwrite32be(0x10000, mFile.get()); /* kCAFChannelLayoutTag_UseChannelBitmap */
            /* 32-bit uint, mChannelBitmap */
            fwrite32be(chanmask, mFile.get()); /* Same as WFX, thankfully. */
            /* 32-bit uint, mNumberChannelDescriptions */
            fwrite32be(0, mFile.get());
        }

        /* Audio Data chunk */
        fputs("data", mFile.get());
        fwrite64be(~0_u64, mFile.get()); /* filled in at stop */
    }

    if(ferror(mFile.get()))
    {
        ERR("Error writing header: {}", std::generic_category().message(errno));
        return false;
    }
    mDataStart = ftell(mFile.get());

    setDefaultWFXChannelOrder();

    mBuffer.resize(size_t{mDevice->frameSizeFromFmt()} * mDevice->mUpdateSize);

    return true;
}

void WaveBackend::start()
{
    try {
        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{&WaveBackend::mixerProc, this};
    }
    catch(std::exception& e) {
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start mixing thread: {}", e.what()};
    }
}

void WaveBackend::stop()
{
    if(mKillNow.exchange(true, std::memory_order_acq_rel) || !mThread.joinable())
        return;
    mThread.join();

    if(mDataStart > 0)
    {
        const auto size = ftell(mFile.get());
        if(size > 0)
        {
            const auto dataLen = size - mDataStart;
            if(!mCAFOutput)
            {
                if(fseek(mFile.get(), 4, SEEK_SET) == 0) // 'WAVE' header len
                    fwrite32le(gsl::narrow_cast<uint>(size-8), mFile.get());
                if(fseek(mFile.get(), mDataStart-4, SEEK_SET) == 0) // 'data' header len
                    fwrite32le(gsl::narrow_cast<uint>(dataLen), mFile.get());
            }
            else
            {
                if(fseek(mFile.get(), mDataStart-8, SEEK_SET) == 0) // 'data' header len
                    fwrite64be(gsl::narrow_cast<uint64_t>(dataLen), mFile.get());
            }
            fseek(mFile.get(), 0, SEEK_END);
        }
    }
}

} // namespace


bool WaveBackendFactory::init()
{ return true; }

bool WaveBackendFactory::querySupport(BackendType type)
{ return type == BackendType::Playback; }

auto WaveBackendFactory::enumerate(BackendType type) -> std::vector<std::string>
{
    switch(type)
    {
    case BackendType::Playback:
        return std::vector{std::string{GetDeviceName()}};
    case BackendType::Capture:
        break;
    }
    return {};
}

auto WaveBackendFactory::createBackend(gsl::not_null<DeviceBase*> device, BackendType type)
    -> BackendPtr
{
    if(type == BackendType::Playback)
        return BackendPtr{new WaveBackend{device}};
    return nullptr;
}

BackendFactory &WaveBackendFactory::getFactory()
{
    static WaveBackendFactory factory{};
    return factory;
}
