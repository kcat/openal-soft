/*
 * OpenAL LAF Playback Example
 *
 * Copyright (c) 2024 by Chris Robinson <chris.kcat@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* This file contains an example for playback of Limitless Audio Format files.
 *
 * Some current shortcomings:
 *
 * - 256 track limit. Could be made higher, but making it too flexible would
 *   necessitate more micro-allocations.
 *
 * - "Objects" mode only supports sample rates that are a multiple of 48. Since
 *   positions are specified as samples in extra channels/tracks, and 3*16
 *   samples are needed per track to specify the full set of positions, and
 *   each chunk is exactly one second long, other sample rates would result in
 *   the positions being split across chunks, causing the source playback
 *   offset to go out of sync with the offset used to look up the current
 *   spatial positions. Fixing this will require slightly more work to update
 *   and synchronize the spatial position arrays against the playback offset.
 *
 * - Updates are specified as fast as the app can detect and react to the
 *   reported source offset (that in turn depends on how often OpenAL renders).
 *   This can cause some positions to be a touch late and lose some granular
 *   temporal movement. In practice, this should probably be good enough for
 *   most use-cases. Fixing this would need either a new extension to queue
 *   position changes to apply when needed, or use a separate loopback device
 *   to render with and control the number of samples rendered between updates
 *   (with a second device to do the actual playback).
 *
 * - The LAF documentation doesn't prohibit object position tracks from being
 *   separated with audio tracks in between, or from being the first tracks
 *   followed by the audio tracks. It's not known if this is intended to be
 *   allowed, but it's not supported. Object position tracks must be last.
 */

#include "config.h"

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <numbers>
#include <numeric>
#include <ranges>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "alnumeric.h"
#include "alstring.h"
#include "common/alhelpers.hpp"
#include "filesystem.h"
#include "fmt/base.h"
#include "fmt/ostream.h"
#include "fmt/std.h"

#include "win_main_utf8.h"

#if HAVE_CXXMODULES
import gsl;
import openal;

#else

#include "AL/alc.h"
#include "AL/al.h"
#include "AL/alext.h"

#include "gsl/gsl"
#endif

namespace {

using ALCdevicePtr = std::unique_ptr<ALCdevice, decltype([](ALCdevice *device)
    { alcCloseDevice(device); })>;
using ALCcontextPtr = std::unique_ptr<ALCcontext, decltype([](ALCcontext *context)
    { alcDestroyContext(context); })>;

/* Filter object functions */
auto alGenFilters = LPALGENFILTERS{};
auto alDeleteFilters = LPALDELETEFILTERS{};
auto alIsFilter = LPALISFILTER{};
auto alFilteri = LPALFILTERI{};
auto alFilteriv = LPALFILTERIV{};
auto alFilterf = LPALFILTERF{};
auto alFilterfv = LPALFILTERFV{};
auto alGetFilteri = LPALGETFILTERI{};
auto alGetFilteriv = LPALGETFILTERIV{};
auto alGetFilterf = LPALGETFILTERF{};
auto alGetFilterfv = LPALGETFILTERFV{};

/* Effect object functions */
auto alGenEffects = LPALGENEFFECTS{};
auto alDeleteEffects = LPALDELETEEFFECTS{};
auto alIsEffect = LPALISEFFECT{};
auto alEffecti = LPALEFFECTI{};
auto alEffectiv = LPALEFFECTIV{};
auto alEffectf = LPALEFFECTF{};
auto alEffectfv = LPALEFFECTFV{};
auto alGetEffecti = LPALGETEFFECTI{};
auto alGetEffectiv = LPALGETEFFECTIV{};
auto alGetEffectf = LPALGETEFFECTF{};
auto alGetEffectfv = LPALGETEFFECTFV{};

/* Auxiliary Effect Slot object functions */
auto alGenAuxiliaryEffectSlots = LPALGENAUXILIARYEFFECTSLOTS{};
auto alDeleteAuxiliaryEffectSlots = LPALDELETEAUXILIARYEFFECTSLOTS{};
auto alIsAuxiliaryEffectSlot = LPALISAUXILIARYEFFECTSLOT{};
auto alAuxiliaryEffectSloti = LPALAUXILIARYEFFECTSLOTI{};
auto alAuxiliaryEffectSlotiv = LPALAUXILIARYEFFECTSLOTIV{};
auto alAuxiliaryEffectSlotf = LPALAUXILIARYEFFECTSLOTF{};
auto alAuxiliaryEffectSlotfv = LPALAUXILIARYEFFECTSLOTFV{};
auto alGetAuxiliaryEffectSloti = LPALGETAUXILIARYEFFECTSLOTI{};
auto alGetAuxiliaryEffectSlotiv = LPALGETAUXILIARYEFFECTSLOTIV{};
auto alGetAuxiliaryEffectSlotf = LPALGETAUXILIARYEFFECTSLOTF{};
auto alGetAuxiliaryEffectSlotfv = LPALGETAUXILIARYEFFECTSLOTFV{};

auto alcRenderSamplesSOFT = LPALCRENDERSAMPLESSOFT{};


auto MuteFilterID = ALuint{};
auto LowFrequencyEffectID = ALuint{};
auto LfeSlotID = ALuint{};

auto RenderChannels = ALCenum{};
auto RenderOutMode = ALCenum{};
auto RenderSamples = ALCenum{};
auto RenderSampleRate = ALCsizei{};
auto RenderAmbiOrder = ALCint{};


void fwrite16be(u16 const value, std::ostream &f)
{
    auto data = std::bit_cast<std::array<char,2>>(value);
    if constexpr(std::endian::native != std::endian::big)
        std::ranges::reverse(data);
    f.write(data.data(), std::ssize(data));
}

void fwrite32be(u32 const value, std::ostream &f)
{
    auto data = std::bit_cast<std::array<char,4>>(value);
    if constexpr(std::endian::native != std::endian::big)
        std::ranges::reverse(data);
    f.write(data.data(), std::ssize(data));
}

void fwrite64be(u64 const value, std::ostream &f)
{
    auto data = std::bit_cast<std::array<char,8>>(value);
    if constexpr(std::endian::native != std::endian::big)
        std::ranges::reverse(data);
    f.write(data.data(), std::ssize(data));
}


using namespace std::string_view_literals;

[[noreturn]]
void do_assert(const char *message, const std::source_location loc=std::source_location::current())
{
    auto errstr = fmt::format("{}:{}: {}", loc.file_name(), loc.line(), message);
    throw std::runtime_error{errstr};
}

#define MyAssert(cond) do {                                                   \
    if(!(cond)) [[unlikely]]                                                  \
        do_assert("Assertion '" #cond "' failed");                            \
} while(0)


template<typename... Ts>
struct overloaded : Ts... { using Ts::operator()...; };


enum class Quality : u8 {
    s8, s16, f32, s24
};
enum class Mode : bool {
    Channels, Objects
};

auto GetQualityName(Quality const quality) noexcept -> std::string_view
{
    switch(quality)
    {
    case Quality::s8: return "8-bit int"sv;
    case Quality::s16: return "16-bit int"sv;
    case Quality::f32: return "32-bit float"sv;
    case Quality::s24: return "24-bit int"sv;
    }
    return "<unknown>"sv;
}

auto GetModeName(Mode const mode) noexcept -> std::string_view
{
    switch(mode)
    {
    case Mode::Channels: return "channels"sv;
    case Mode::Objects: return "objects"sv;
    }
    return "<unknown>"sv;
}

auto BytesFromQuality(Quality const quality) noexcept -> usize
{
    switch(quality)
    {
    case Quality::s8: return 1;
    case Quality::s16: return 2;
    case Quality::f32: return 4;
    case Quality::s24: return 3;
    }
    return 4;
}


/* Helper class for reading little-endian samples on big-endian targets, and
 * convert 24-bit samples.
 */
template<typename T>
struct SampleInfo;

template<>
struct SampleInfo<f32> {
    static constexpr auto SrcSize = 4_uz;

    [[nodiscard]]
    static auto read(std::input_iterator auto input) -> f32
    {
        auto src = std::array<char,4>{};
        if constexpr(std::endian::native == std::endian::little)
            std::ranges::copy(std::views::counted(input, 4), src.begin());
        else
            std::ranges::copy(std::views::counted(input, 4), src.rbegin());
        return std::bit_cast<f32>(src);
    }
};

template<>
struct SampleInfo<i32> {
    static constexpr auto SrcSize = 3_uz;

    [[nodiscard]]
    static auto read(std::input_iterator auto input) -> i32
    {
        auto src = std::array<char,4>{};
        if constexpr(std::endian::native == std::endian::little)
            std::ranges::copy(std::views::counted(input, 3), src.begin()+1);
        else
            std::ranges::copy(std::views::counted(input, 3), src.rbegin()+1);
        return std::bit_cast<i32>(src);
    }
};

template<>
struct SampleInfo<i16> {
    static constexpr auto SrcSize = 2_uz;

    [[nodiscard]]
    static auto read(std::input_iterator auto input) -> i16
    {
        auto src = std::array<char,2>{};
        if constexpr(std::endian::native == std::endian::little)
            std::ranges::copy(std::views::counted(input, 2), src.begin());
        else
            std::ranges::copy(std::views::counted(input, 2), src.rbegin());
        return std::bit_cast<i16>(src);
    }
};

template<>
struct SampleInfo<i8> {
    static constexpr auto SrcSize = 1_uz;

    [[nodiscard]]
    static auto read(std::input_iterator auto input) -> i8
    { return std::bit_cast<i8>(*input); }
};


/* Each track with position data consists of a set of 3 samples per 16 audio
 * channels, resulting in a full set of positions being specified over 48
 * sample frames.
 */
constexpr auto FramesPerPos = 48_uz;

struct Channel {
    ALuint mSource{};
    std::array<ALuint,2> mBuffers{};
    f32 mAzimuth{};
    f32 mElevation{};
    bool mIsLfe{};

    Channel() = default;
    Channel(const Channel&) = delete;
    Channel(Channel&& rhs) noexcept
        : mSource{rhs.mSource}, mBuffers{rhs.mBuffers}, mAzimuth{rhs.mAzimuth}
        , mElevation{rhs.mElevation}, mIsLfe{rhs.mIsLfe}
    {
        rhs.mSource = 0;
        rhs.mBuffers.fill(0);
    }
    ~Channel()
    {
        if(mSource) alDeleteSources(1, &mSource);
        if(mBuffers[0]) alDeleteBuffers(gsl::narrow<ALsizei>(mBuffers.size()), mBuffers.data());
    }

    auto operator=(const Channel&) -> Channel& = delete;
    auto operator=(Channel&& rhs) noexcept -> Channel&
    {
        std::swap(mSource, rhs.mSource);
        std::swap(mBuffers, rhs.mBuffers);
        std::swap(mAzimuth, rhs.mAzimuth);
        std::swap(mElevation, rhs.mElevation);
        std::swap(mIsLfe, rhs.mIsLfe);
        return *this;
    }
};

struct LafStream {
    std::ifstream mInFile;

    Quality mQuality{};
    Mode mMode{};
    u32 mNumTracks{};
    u32 mSampleRate{};
    ALenum mALFormat{};
    u64 mSampleCount{};

    u64 mCurrentSample{};

    std::array<u8, 32> mEnabledTracks{};
    u32 mNumEnabled{};
    std::vector<char> mSampleChunk;
    template<typename T> using vector = std::vector<T>;
    std::variant<vector<i8>,vector<i16>,vector<f32>,vector<i32>> mSampleLine;

    std::vector<Channel> mChannels;
    std::vector<std::vector<f32>> mPosTracks;

    LafStream() = default;
    LafStream(const LafStream&) = delete;
    ~LafStream() = default;
    auto operator=(const LafStream&) -> LafStream& = delete;

    [[nodiscard]]
    auto isAtEnd() const noexcept -> bool { return mCurrentSample >= mSampleCount; }

    [[nodiscard]]
    auto readChunk() -> u32;

    [[nodiscard]]
    auto prepareTrack(usize trackidx, usize count) -> std::span<std::byte>;

    void convertSamples(std::span<std::byte> samples) const;

    void convertPositions(std::span<f32> dst) const;
};

auto LafStream::readChunk() -> u32
{
    auto enableTrackBits = std::array<char, std::tuple_size_v<decltype(mEnabledTracks)>>{};
    auto &infile = mInFile.is_open() ? mInFile : std::cin;
    if(!infile.read(enableTrackBits.data(), gsl::narrow<std::streamsize>((mNumTracks+7u)>>3u)))
         [[unlikely]]
    {
        /* Only print an error when expecting more samples. A sample count of
         * ~0_u64 indicates unbounded input, which will end when it has nothing
         * more to give.
         */
        if(mSampleCount < ~0_u64 || infile.gcount() != 0)
            fmt::println(std::cerr, "Premature end of file ({} of {} samples)", mCurrentSample,
                mSampleCount);
        mSampleCount = mCurrentSample;
        return 0_u32;
    }

    mEnabledTracks = std::bit_cast<decltype(mEnabledTracks)>(enableTrackBits);
    mNumEnabled = gsl::narrow<u32>(std::accumulate(mEnabledTracks.cbegin(),
        mEnabledTracks.cend(), 0, [](int const val, u8 const in) -> int
    { return val + std::popcount(in); }));

    /* Make sure enable bits aren't set for non-existent tracks. */
    if(mNumEnabled > 0 && mEnabledTracks[((mNumTracks+7_uz)>>3) - 1] >= 1u<<(mNumTracks&7))
        throw std::runtime_error{"Invalid channel enable bits"};

    /* Each chunk is exactly one second long, with samples interleaved for each
     * enabled track. The last chunk may be shorter if there isn't enough time
     * remaining for a full second.
     */
    auto const numsamples = gsl::narrow<usize>(std::min(u64{mSampleRate},
        mSampleCount-mCurrentSample));

    /* Choose the smaller of std::streamsize or isize, to ensure neither the
     * read size or range drop size get truncated.
     */
    using readsize_t = std::conditional_t<(sizeof(std::streamsize) > sizeof(isize)), isize,
        std::streamsize>;
    const auto toread = gsl::narrow<readsize_t>(numsamples * BytesFromQuality(mQuality)
        * mNumEnabled);
    if(!infile.read(mSampleChunk.data(), toread)) [[unlikely]]
    {
        const auto framesize = BytesFromQuality(mQuality) * mNumEnabled;
        const auto samplesread = al::saturate_cast<u64>(infile.gcount()) / framesize;
        mCurrentSample += samplesread;
        if(mSampleCount < ~0_u64)
            fmt::println(std::cerr, "Premature end of file ({} of {} samples)",
                mCurrentSample, mSampleCount);
        mSampleCount = mCurrentSample;
        std::ranges::fill(mSampleChunk | std::views::drop(numsamples*framesize), char{});
        return gsl::narrow<u32>(samplesread);
    }
    std::ranges::fill(mSampleChunk | std::views::drop(toread), char{});

    mCurrentSample += numsamples;
    return gsl::narrow<u32>(numsamples);
}

auto LafStream::prepareTrack(usize const trackidx, usize const count) -> std::span<std::byte>
{
    auto const todo = std::min(usize{mSampleRate}, count);
    if((mEnabledTracks[trackidx>>3] & (1_uz<<(trackidx&7))))
    {
        /* If the track is enabled, get the real index (skipping disabled
         * tracks), and deinterlace it into the mono line.
         */
        auto const idx = std::invoke([this,trackidx]() -> u32
        {
            auto const bits = std::span{mEnabledTracks}.first(trackidx>>3);
            auto const res = std::accumulate(bits.begin(), bits.end(), 0_i32,
                [](int const val, u8 const in) -> int { return val + std::popcount(in); })
                + std::popcount(mEnabledTracks[trackidx>>3] & ((1u<<(trackidx&7))-1));
            return gsl::narrow_cast<u32>(res);
        });

        auto const step = usize{mNumEnabled};
        Expects(idx < step);
        return std::visit([count,idx,step,src=std::span{mSampleChunk}]<typename T>(T &dst)
        {
            using sample_t = T::value_type;
            auto inptr = src.begin();
            std::advance(inptr, idx*SampleInfo<sample_t>::SrcSize);
            auto output = std::span{dst}.first(count);
            output.front() = SampleInfo<sample_t>::read(inptr);
            std::ranges::generate(output | std::views::drop(1), [&inptr,step]
            {
                std::advance(inptr, step*SampleInfo<sample_t>::SrcSize);
                return SampleInfo<sample_t>::read(inptr);
            });
            return std::as_writable_bytes(output);
        }, mSampleLine);
    }

    /* If the track is disabled, provide silence. */
    return std::visit([todo]<typename T>(T &dst)
    {
        using sample_t = T::value_type;
        std::ranges::fill(dst, sample_t{});
        return std::as_writable_bytes(std::span{dst}.first(todo));
    }, mSampleLine);
}

void LafStream::convertSamples(std::span<std::byte> const samples) const
{
    /* OpenAL uses unsigned 8-bit samples (0...255), so signed 8-bit samples
     * (-128...+127) need conversion. The other formats are fine.
     */
    if(mQuality == Quality::s8)
    {
        std::ranges::transform(samples, samples.begin(),
            [](std::byte const sample) noexcept -> std::byte
        {
            return sample^std::byte{0x80};
        });
    }
}

void LafStream::convertPositions(std::span<f32> const dst) const
{
    std::visit(overloaded {
        [dst](vector<i8> const &src)
        {
            std::ranges::transform(src, dst.begin(), [](i8 const in) noexcept -> f32
            { return gsl::narrow_cast<f32>(in.c_val) / 127.0f; });
        },
        [dst](vector<i16> const &src)
        {
            std::ranges::transform(src, dst.begin(), [](i16 const in) noexcept -> f32
            { return gsl::narrow_cast<f32>(in) / 32767.0f; });
        },
        [dst](vector<f32> const &src) { std::ranges::copy(src, dst.begin()); },
        [dst](vector<i32> const &src)
        {
            /* 24-bit samples are converted to 32-bit in copySamples. */
            std::ranges::transform(src, dst.begin(), [](i32 const in) noexcept -> f32
            { return gsl::narrow_cast<f32>(in>>8) / 8388607.0f; });
        },
    }, mSampleLine);
}

auto LoadLAF(const fs::path &fname) -> std::unique_ptr<LafStream>
{
    auto laf = std::make_unique<LafStream>();
    auto &infile = std::invoke([&fname,&laf]() -> std::istream&
    {
        if(fname == "-")
        {
#ifdef _WIN32
            /* Set stdin to binary mode, so cin's rdbuf will read the file
             * correctly.
             */
            if(_setmode(_fileno(stdin), _O_BINARY) == -1)
                throw std::runtime_error{"Failed to set stdin to binary mode"};
#endif
            return std::cin;
        }

        laf->mInFile.open(fname, std::ios_base::binary);
        if(!laf->mInFile.is_open())
            throw std::runtime_error{"Could not open file"};
        return laf->mInFile;
    });
    /* Throw exceptions if we fail reading the header, so it will skip the file
     * and go to the next.
     */
    infile.exceptions(std::ios_base::eofbit | std::ios_base::failbit | std::ios_base::badbit);

    auto marker = std::array<char,9>{};
    infile.read(marker.data(), marker.size());
    if(std::string_view{marker.data(), marker.size()} != "LIMITLESS"sv)
        throw std::runtime_error{"Not an LAF file"};

    auto header = std::array<char,10>{};
    infile.read(header.data(), header.size());
    while(std::string_view{header.data(), 4} != "HEAD"sv)
    {
        auto headview = std::string_view{header.data(), header.size()};
        auto hiter = header.begin();
        if(const auto hpos = headview.find("HEAD"sv); hpos < headview.size())
        {
            /* Found the HEAD marker. Copy what was read of the header to the
             * front, fill in the rest of the header, and continue loading.
             */
            hiter = std::ranges::copy(header | std::views::drop(hpos), hiter).out;
        }
        else if(headview.ends_with("HEA"sv))
        {
            /* Found what might be the HEAD marker at the end. Copy it to the
             * front, refill the header, and check again.
             */
            hiter = std::ranges::copy_n(header.end()-3, 3, hiter).out;
        }
        else if(headview.ends_with("HE"sv))
            hiter = std::ranges::copy_n(header.end()-2, 2, hiter).out;
        else if(headview.back() == 'H')
            hiter = std::ranges::copy_n(header.end()-1, 1, hiter).out;

        infile.read(std::to_address(hiter), std::distance(hiter, header.end()));
    }

    laf->mQuality = std::invoke([stype=int{header[4]}]
    {
        if(stype == 0) return Quality::s8;
        if(stype == 1) return Quality::s16;
        if(stype == 2) return Quality::f32;
        if(stype == 3) return Quality::s24;
        throw std::runtime_error{fmt::format("Invalid quality type: {}", stype)};
    });

    laf->mMode = std::invoke([mode=int{header[5]}]
    {
        if(mode == 0) return Mode::Channels;
        if(mode == 1) return Mode::Objects;
        throw std::runtime_error{fmt::format("Invalid mode: {}", mode)};
    });

    laf->mNumTracks = std::invoke([input=std::span{header}.subspan<6,4>()]
    {
        return u32{as_unsigned(input[0])} | (u32{as_unsigned(input[1])}<<8)
            | (u32{as_unsigned(input[2])}<<16) | (u32{as_unsigned(input[3])}<<24);
    });

    fmt::println("Filename: {}", al::u8_as_char(fname.u8string()));
    fmt::println(" quality: {}", GetQualityName(laf->mQuality));
    fmt::println(" mode: {}", GetModeName(laf->mMode));
    fmt::println(" track count: {}", laf->mNumTracks);

    if(laf->mNumTracks == 0)
        throw std::runtime_error{"No tracks"};
    if(laf->mNumTracks > 256)
        throw std::runtime_error{fmt::format("Too many tracks: {}", laf->mNumTracks)};

    auto chandata = std::vector<char>(laf->mNumTracks*9_uz);
    infile.read(chandata.data(), std::ssize(chandata));

    if(laf->mMode == Mode::Channels)
        laf->mChannels.resize(laf->mNumTracks);
    else
    {
        if(laf->mNumTracks < 2)
            throw std::runtime_error{"Not enough tracks"};

        auto numchans = usize{laf->mNumTracks - 1};
        auto numpostracks = 1_uz;
        while(numpostracks*16 < numchans)
        {
            --numchans;
            ++numpostracks;
        }
        laf->mChannels.resize(numchans);
        laf->mPosTracks.resize(numpostracks);
    }

    static constexpr auto read_float = [](std::span<char,4> const input)
    {
        const auto value = u32{as_unsigned(input[0])} | (u32{as_unsigned(input[1])}<<8)
            | (u32{as_unsigned(input[2])}<<16) | (u32{as_unsigned(input[3])}<<24);
        return std::bit_cast<f32>(value);
    };

    /* C++23 can use chandata | std::views::chunk(9) | std::views::enumerate to
     * get a range of ~std::pair<size_t index, std::span<char> chunk>.
     */
    auto chanspan = std::span{chandata}.first(laf->mChannels.size()*9_uz);
    std::ranges::generate(laf->mChannels, [&chandata,&chanspan]
    {
        const auto idx = (chanspan.data()-chandata.data()) / 9_z;
        auto x_axis = read_float(chanspan.first<4>());
        auto y_axis = read_float(chanspan.subspan<4,4>());
        auto lfe_flag = int{chanspan[8]};
        chanspan = chanspan.subspan(9);

        fmt::println("Track {}: E={:f}, A={:f} (LFE: {})", idx, x_axis, y_axis, lfe_flag);
        MyAssert(std::isfinite(x_axis) && std::isfinite(y_axis));

        auto channel = Channel{};
        channel.mAzimuth = y_axis;
        channel.mElevation = x_axis;
        channel.mIsLfe = lfe_flag != 0;
        return channel;
    });
    chanspan = std::span{chandata}.last(laf->mPosTracks.size()*9_uz);
    std::ranges::for_each(laf->mPosTracks, [&chandata,&chanspan](auto&&)
    {
        const auto idx = (chanspan.data()-chandata.data()) / 9_z;
        auto x_axis = read_float(chanspan.first<4>());
        auto y_axis = read_float(chanspan.subspan<4,4>());
        auto lfe_flag = int{chanspan[8]};
        chanspan = chanspan.subspan(9);

        fmt::println("Track {}: E={:f}, A={:f} (LFE: {})", idx, x_axis, y_axis, lfe_flag);
        MyAssert(std::isnan(x_axis) && y_axis == 0.0f);
        MyAssert(idx != 0);
    });
    fmt::println("Channels: {}", laf->mChannels.size());

    /* For "objects" mode, ensure there's enough tracks with position data to
     * handle the audio channels.
     */
    if(laf->mMode == Mode::Objects)
        MyAssert(((laf->mChannels.size()-1)>>4) == laf->mPosTracks.size()-1);

    auto footer = std::array<char,12>{};
    infile.read(footer.data(), footer.size());

    laf->mSampleRate = std::invoke([input=std::span{footer}.first<4>()]
    {
        return u32{as_unsigned(input[0])} | (u32{as_unsigned(input[1])}<<8)
            | (u32{as_unsigned(input[2])}<<16) | (u32{as_unsigned(input[3])}<<24);
    });
    laf->mSampleCount = std::invoke([input=std::span{footer}.last<8>()]
    {
        return u64{as_unsigned(input[0])} | (u64{as_unsigned(input[1])}<<8)
            | (u64{as_unsigned(input[2])}<<16) | (u64{as_unsigned(input[3])}<<24)
            | (u64{as_unsigned(input[4])}<<32) | (u64{as_unsigned(input[5])}<<40)
            | (u64{as_unsigned(input[6])}<<48) | (u64{as_unsigned(input[7])}<<56);
    });
    fmt::println("Sample rate: {}", laf->mSampleRate);
    if(laf->mSampleCount < ~0_u64)
        fmt::println("Length: {} samples ({:.2f} sec)", laf->mSampleCount,
            static_cast<double>(laf->mSampleCount)/static_cast<double>(laf->mSampleRate));
    else
        fmt::println("Length: unbounded");

    /* Position vectors get split across the PCM chunks if the sample rate
     * isn't a multiple of 48. Each PCM chunk is exactly one second (the sample
     * rate in sample frames). Each track with position data consists of a set
     * of 3 samples for 16 audio channels, resulting in 48 sample frames for a
     * full set of positions. Extra logic will be needed to manage the position
     * frame offset separate from each chunk.
     */
    MyAssert(laf->mMode == Mode::Channels || (laf->mSampleRate%FramesPerPos) == 0);

    std::ranges::generate(laf->mPosTracks, [length=laf->mSampleRate*2_uz]
    { return std::vector(length, 0.0f); });

    laf->mSampleChunk.resize(laf->mSampleRate*BytesFromQuality(laf->mQuality)*laf->mNumTracks);
    switch(laf->mQuality)
    {
    case Quality::s8: laf->mSampleLine.emplace<std::vector<i8>>(laf->mSampleRate); break;
    case Quality::s16: laf->mSampleLine.emplace<std::vector<i16>>(laf->mSampleRate); break;
    case Quality::f32: laf->mSampleLine.emplace<std::vector<f32>>(laf->mSampleRate); break;
    case Quality::s24: laf->mSampleLine.emplace<std::vector<i32>>(laf->mSampleRate); break;
    }

    /* Re-disable exceptions since we'll manually check each read. */
    infile.exceptions(std::ios_base::goodbit);
    return laf;
}

void PlayLAF(std::string_view const fname)
try {
    const auto laf = LoadLAF(fs::path(al::char_as_u8(fname)));

    switch(laf->mQuality)
    {
    case Quality::s8:
        laf->mALFormat = AL_FORMAT_MONO8;
        break;
    case Quality::s16:
        laf->mALFormat = AL_FORMAT_MONO16;
        break;
    case Quality::f32:
        if(alIsExtensionPresent("AL_EXT_FLOAT32"))
            laf->mALFormat = AL_FORMAT_MONO_FLOAT32;
        break;
    case Quality::s24:
        laf->mALFormat = alGetEnumValue("AL_FORMAT_MONO32");
        if(!laf->mALFormat || laf->mALFormat == -1)
            laf->mALFormat = alGetEnumValue("AL_FORMAT_MONO_I32");
        break;
    }
    if(!laf->mALFormat || laf->mALFormat == -1)
        throw std::runtime_error{fmt::format("No supported format for {} samples",
            GetQualityName(laf->mQuality))};

    std::ranges::for_each(laf->mChannels, [](Channel &channel)
    {
        alGenSources(1, &channel.mSource);
        alGenBuffers(gsl::narrow<ALsizei>(channel.mBuffers.size()), channel.mBuffers.data());

        /* Disable distance attenuation, and make sure the source stays locked
         * relative to the listener.
         */
        alSourcef(channel.mSource, AL_ROLLOFF_FACTOR, 0.0f);
        alSourcei(channel.mSource, AL_SOURCE_RELATIVE, AL_TRUE);

        /* Convert degrees to radians, wrapping between -pi...+pi. */
        auto azi = channel.mAzimuth / 180.0f;
        /* At this magnitude, the result is always 0. */
        if(!(std::abs(azi) < 16777216.0f))
            azi = 0.0f;
        else
        {
            auto const tmp = gsl::narrow_cast<i32>(azi);
            azi -= gsl::narrow_cast<f32>(tmp + (tmp%2));
            azi *= std::numbers::pi_v<f32>;
        }

        auto elev = channel.mElevation / 180.0f;
        if(!(std::abs(elev) < 16777216.0f))
            elev = 0.0f;
        else
        {
            auto const tmp = gsl::narrow_cast<i32>(elev);
            elev -= gsl::narrow_cast<f32>(tmp + (tmp%2));
            elev *= std::numbers::pi_v<f32>;
        }

        auto const x = std::sin(azi) * std::cos(elev);
        auto const y = std::sin(elev);
        auto const z = -std::cos(azi) * std::cos(elev);
        alSource3f(channel.mSource, AL_POSITION, x, y, z);

        if(channel.mIsLfe)
        {
            if(LfeSlotID)
            {
                /* For LFE, silence the direct/dry path and connect the LFE aux
                 * slot on send 0.
                 */
                alSourcei(channel.mSource, AL_DIRECT_FILTER, as_signed(MuteFilterID));
                alSource3i(channel.mSource, AL_AUXILIARY_SEND_FILTER, as_signed(LfeSlotID), 0,
                    AL_FILTER_NULL);
            }
            else
            {
                /* If AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT isn't available,
                 * silence LFE channels since they may not be appropriate to
                 * play normally.
                 */
                alSourcef(channel.mSource, AL_GAIN, 0.0f);
            }
        }

        if(const auto err = alGetError())
            throw std::runtime_error{fmt::format("OpenAL error: {}", alGetString(err))};
    });

    auto renderFile = std::ofstream{};
    auto renderStart = std::streamoff{};
    auto leadIn = 0_z;
    auto leadOut = 0_z;
    auto renderbuf = std::vector<char>{};
    if(alcRenderSamplesSOFT)
    {
        auto *device = alcGetContextsDevice(alcGetCurrentContext());

        auto const chancount = std::invoke([]() -> u32
        {
            switch(RenderChannels)
            {
            case ALC_MONO_SOFT: return 1_u32;
            case ALC_STEREO_SOFT: return 2_u32;
            case ALC_QUAD_SOFT: return 4_u32;
            case ALC_SURROUND_5_1_SOFT: return 6_u32;
            case ALC_SURROUND_6_1_SOFT: return 7_u32;
            case ALC_SURROUND_7_1_SOFT: return 8_u32;
            case ALC_BFORMAT3D_SOFT:
                return gsl::narrow<u32>((RenderAmbiOrder+1)*(RenderAmbiOrder+1));
            default:
                throw std::runtime_error{fmt::format("Unexpected channel enum: {:#x}",
                    RenderChannels)};
            }
        });

        auto const samplesize = std::invoke([]() -> u32
        {
            switch(RenderSamples)
            {
            case ALC_UNSIGNED_BYTE_SOFT: return 1_u32;
            case ALC_BYTE_SOFT: return 1_u32;
            case ALC_UNSIGNED_SHORT_SOFT: return 2_u32;
            case ALC_SHORT_SOFT: return 2_u32;
            case ALC_UNSIGNED_INT_SOFT: return 4_u32;
            case ALC_INT_SOFT: return 4_u32;
            case ALC_FLOAT_SOFT: return 4_u32;
            default:
                throw std::runtime_error{fmt::format("Unexpected sample type enum: {:#x}",
                    RenderSamples)};
            }
        });
        auto const framesize = usize{chancount} * samplesize;
        renderbuf.resize(framesize * FramesPerPos);

        if(std::cmp_not_equal(RenderSampleRate, laf->mSampleRate))
        {
            /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
            auto const alcResetDeviceSOFT = reinterpret_cast<LPALCRESETDEVICESOFT>(
                alcGetProcAddress(nullptr, "alcResetDeviceSOFT"));

            auto const attribs = std::to_array<ALCint>({
                ALC_FREQUENCY, gsl::narrow<i32>(laf->mSampleRate),
                ALC_FORMAT_CHANNELS_SOFT, RenderChannels,
                ALC_FORMAT_TYPE_SOFT, RenderSamples,
                ALC_OUTPUT_MODE_SOFT, RenderOutMode,
                ALC_AMBISONIC_LAYOUT_SOFT, ALC_ACN_SOFT,
                ALC_AMBISONIC_SCALING_SOFT, ALC_SN3D_SOFT,
                ALC_AMBISONIC_ORDER_SOFT, RenderAmbiOrder,
                0});
            if(!alcResetDeviceSOFT(device, attribs.data()))
                throw std::runtime_error{fmt::format(
                    "Failed to reset loopback device for {}hz rendering", RenderSampleRate)};
            RenderSampleRate = gsl::narrow_cast<i32>(laf->mSampleRate);
        }

        if(alcIsExtensionPresent(device, "ALC_SOFT_device_clock"))
        {
            /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
            const auto alcGetInteger64vSOFT = reinterpret_cast<LPALCGETINTEGER64VSOFT>(
                alcGetProcAddress(device, "alcGetInteger64vSOFT"));

            auto latency = ALCint64SOFT{};
            alcGetInteger64vSOFT(device, ALC_DEVICE_LATENCY_SOFT, 1, &latency);
            std::ignore = alcGetError(device);

            leadIn = gsl::narrow<isize>(latency * RenderSampleRate / 1'000'000'000)
                * gsl::narrow_cast<i32>(framesize);
            leadOut = gsl::narrow<isize>((latency*RenderSampleRate + 999'999'999) / 1'000'000'000)
                * gsl::narrow_cast<i32>(framesize);
        }

        auto outname = fs::path(al::char_as_u8(fname)).stem();
        outname += u8".caf";
        if(fs::exists(outname) && !fs::is_fifo(outname))
            throw std::runtime_error{fmt::format("Output file {} exists",
                al::u8_as_char(outname.u8string()))};

        renderFile.open(outname, std::ios_base::binary | std::ios_base::out);
        if(!renderFile.is_open())
            throw std::runtime_error{fmt::format("Failed to create {}",
                al::u8_as_char(outname.u8string()))};

        renderFile.write("caff", 4);
        fwrite16be(1, renderFile);
        fwrite16be(0, renderFile);

        renderFile.write("desc", 4);
        fwrite64be(32, renderFile);
        fwrite64be(std::bit_cast<u64>(gsl::narrow_cast<f64>(RenderSampleRate)),renderFile);
        renderFile.write("lpcm", 4);

        auto const flags = std::invoke([]
        {
            switch(RenderSamples)
            {
            case ALC_UNSIGNED_BYTE_SOFT:
            case ALC_BYTE_SOFT:
                break;
            case ALC_UNSIGNED_SHORT_SOFT:
            case ALC_SHORT_SOFT:
            case ALC_UNSIGNED_INT_SOFT:
            case ALC_INT_SOFT:
                if constexpr(std::endian::native == std::endian::little)
                    return 2_u32; /* kCAFLinearPCMFormatFlagIsLittleEndian */
                else
                    break;
            case ALC_FLOAT_SOFT:
                if constexpr(std::endian::native == std::endian::little)
                    return 3_u32; /* kCAFLinearPCMFormatFlagIsFloat | kCAFLinearPCMFormatFlagIsLittleEndian */
                else
                    return 1_u32; /* kCAFLinearPCMFormatFlagIsFloat */
            }
            return 0_u32;
        });
        fwrite32be(flags, renderFile);
        fwrite32be(samplesize*chancount, renderFile);
        fwrite32be(1, renderFile);
        fwrite32be(chancount, renderFile);
        fwrite32be(samplesize*8, renderFile);

        auto const chanmask = std::invoke([]() -> u32
        {
            switch(RenderChannels)
            {
            case ALC_MONO_SOFT: return 0x4u;
            case ALC_STEREO_SOFT: return 0x1u | 0x2u;
            case ALC_QUAD_SOFT: return 0x1u | 0x2u | 0x10u | 0x20u;
            case ALC_SURROUND_5_1_SOFT: return 0x1u | 0x2u | 0x4u | 0x8u | 0x200u | 0x400u;
            case ALC_SURROUND_6_1_SOFT: return 0x1u | 0x2u | 0x4u | 0x8u | 0x100u | 0x200u | 0x400u;
            case ALC_SURROUND_7_1_SOFT: return 0x1u | 0x2u | 0x4u | 0x8u | 0x10u | 0x20u | 0x200u | 0x400u;
            case ALC_BFORMAT3D_SOFT: return 0u;
            default:
                throw std::runtime_error{fmt::format("Unexpected channel enum: {:#x}",
                    RenderChannels)};
            }
        });
        if(chanmask)
        {
            renderFile.write("chan", 4);
            fwrite64be(12, renderFile);
            fwrite32be(0x10000, renderFile); /* kCAFChannelLayoutTag_UseChannelBitmap */
            fwrite32be(chanmask, renderFile);
            fwrite32be(0, renderFile);
        }

        renderFile.write("data", 4);
        fwrite64be(~0_u64, renderFile); /* filled in at stop */

        renderStart = renderFile.tellp();
        fwrite32be(0, renderFile);

        fmt::println("Rendering to {}...", al::u8_as_char(outname.u8string()));
    }

    while(!laf->isAtEnd())
    {
        auto state = ALenum{};
        auto offset = ALint{};
        auto processed = ALint{};
        /* All sources are played in sync, so they'll all be at the same offset
         * with the same state and number of processed buffers. Query the back
         * source just in case the previous update ran really late and missed
         * updating only some sources on time (in which case, the latter ones
         * will underrun, which this will detect and restart them all as
         * needed).
         */
        alGetSourcei(laf->mChannels.back().mSource, AL_BUFFERS_PROCESSED, &processed);
        alGetSourcei(laf->mChannels.back().mSource, AL_SAMPLE_OFFSET, &offset);
        alGetSourcei(laf->mChannels.back().mSource, AL_SOURCE_STATE, &state);

        if(state == AL_PLAYING || state == AL_PAUSED)
        {
            /* Playing normally. Update the source positions for the current
             * playback offset, for dynamic objects.
             */
            if(!laf->mPosTracks.empty())
            {
                alcSuspendContext(alcGetCurrentContext());
                for(auto const i : std::views::iota(0_uz, laf->mChannels.size()))
                {
                    auto const trackidx = i>>4;

                    auto const posoffset = gsl::narrow<u32>(offset)/FramesPerPos*16_uz + (i&15);
                    auto const x = laf->mPosTracks[trackidx][posoffset*3 + 0];
                    auto const y = laf->mPosTracks[trackidx][posoffset*3 + 1];
                    auto const z = laf->mPosTracks[trackidx][posoffset*3 + 2];

                    /* Convert left-handed coords to right-handed. */
                    alSource3f(laf->mChannels[i].mSource, AL_POSITION, x, y, -z);
                }
                alcProcessContext(alcGetCurrentContext());
            }

            /* Unqueue processed buffers and refill with the next chunk, or
             * sleep for ~10ms before updating again.
             */
            if(processed > 0)
            {
                auto const numsamples = laf->readChunk();
                for(auto const i : std::views::iota(0_uz, laf->mChannels.size()))
                {
                    auto const samples = laf->prepareTrack(i, numsamples);
                    laf->convertSamples(samples);

                    auto bufid = ALuint{};
                    alSourceUnqueueBuffers(laf->mChannels[i].mSource, 1, &bufid);
                    alBufferData(bufid, laf->mALFormat, samples.data(),
                        gsl::narrow<ALsizei>(samples.size()),
                        gsl::narrow<ALsizei>(laf->mSampleRate));
                    alSourceQueueBuffers(laf->mChannels[i].mSource, 1, &bufid);
                }
                for(auto const i : std::views::iota(0_uz, laf->mPosTracks.size()))
                {
                    std::ranges::copy(laf->mPosTracks[i] | std::views::drop(laf->mSampleRate),
                        laf->mPosTracks[i].begin());

                    std::ignore = laf->prepareTrack(laf->mChannels.size()+i, numsamples);
                    laf->convertPositions(std::span{laf->mPosTracks[i]}.last(laf->mSampleRate));
                }
            }
            else if(alcRenderSamplesSOFT)
            {
                alcRenderSamplesSOFT(alcGetContextsDevice(alcGetCurrentContext()),
                    renderbuf.data(), FramesPerPos);
                if(leadIn >= std::ssize(renderbuf))
                    leadIn -= std::ssize(renderbuf);
                else if(leadIn > 0)
                {
                    auto const out = renderbuf | std::views::drop(leadIn);
                    renderFile.write(out.data(), std::ssize(out));
                    leadIn = 0;
                }
                else
                    renderFile.write(renderbuf.data(), std::ssize(renderbuf));
            }
            else
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        else if(state == AL_STOPPED)
        {
            /* Underrun. Restart all sources in sync from the beginning of the
             * currently buffered chunks. This will cause some old audio to
             * replay, but all the channels will agree on where they are in the
             * stream and ensure nothing is skipped.
             */
            auto sources = std::array<ALuint, 256>{};
            std::ranges::transform(laf->mChannels, sources.begin(), &Channel::mSource);
            alSourcePlayv(gsl::narrow<ALsizei>(laf->mChannels.size()), sources.data());
        }
        else if(state == AL_INITIAL)
        {
            /* Starting playback. Read and prepare the two second-long chunks
             * per track (buffering audio samples to OpenAL, and storing the
             * position vectors).
             */
            auto numsamples = laf->readChunk();
            for(auto const i : std::views::iota(0_uz, laf->mChannels.size()))
            {
                auto const samples = laf->prepareTrack(i, numsamples);
                laf->convertSamples(samples);
                alBufferData(laf->mChannels[i].mBuffers[0], laf->mALFormat, samples.data(),
                    gsl::narrow<ALsizei>(samples.size()),
                    gsl::narrow<ALsizei>(laf->mSampleRate));
            }
            for(auto const i : std::views::iota(0_uz, laf->mPosTracks.size()))
            {
                std::ignore = laf->prepareTrack(laf->mChannels.size()+i, numsamples);
                laf->convertPositions(std::span{laf->mPosTracks[i]}.first(laf->mSampleRate));
            }

            numsamples = laf->readChunk();
            for(auto const i : std::views::iota(0_uz, laf->mChannels.size()))
            {
                auto const samples = laf->prepareTrack(i, numsamples);
                laf->convertSamples(samples);
                alBufferData(laf->mChannels[i].mBuffers[1], laf->mALFormat, samples.data(),
                    gsl::narrow<ALsizei>(samples.size()),
                    gsl::narrow<ALsizei>(laf->mSampleRate));
                alSourceQueueBuffers(laf->mChannels[i].mSource,
                    gsl::narrow<ALsizei>(laf->mChannels[i].mBuffers.size()),
                    laf->mChannels[i].mBuffers.data());
            }
            for(auto const i : std::views::iota(0_uz, laf->mPosTracks.size()))
            {
                std::ignore = laf->prepareTrack(laf->mChannels.size()+i, numsamples);
                laf->convertPositions(std::span{laf->mPosTracks[i]}.last(laf->mSampleRate));
            }

            /* Set the initial source positions for dynamic objects, then start
             * all sources in sync.
             */
            if(!laf->mPosTracks.empty())
            {
                for(auto const i : std::views::iota(0_uz, laf->mChannels.size()))
                {
                    auto const trackidx = i>>4;

                    auto const x = laf->mPosTracks[trackidx][(i&15)*3 + 0];
                    auto const y = laf->mPosTracks[trackidx][(i&15)*3 + 1];
                    auto const z = laf->mPosTracks[trackidx][(i&15)*3 + 2];

                    alSource3f(laf->mChannels[i].mSource, AL_POSITION, x, y, -z);
                }
            }

            auto sources = std::array<ALuint, 256>{};
            std::ranges::transform(laf->mChannels, sources.begin(), &Channel::mSource);
            alSourcePlayv(gsl::narrow<ALsizei>(laf->mChannels.size()), sources.data());
        }
        else
            break;
    }

    auto state = ALenum{};
    auto offset = ALint{};
    alGetSourcei(laf->mChannels.back().mSource, AL_SAMPLE_OFFSET, &offset);
    alGetSourcei(laf->mChannels.back().mSource, AL_SOURCE_STATE, &state);
    while(alGetError() == AL_NO_ERROR && state == AL_PLAYING)
    {
        if(!laf->mPosTracks.empty())
        {
            alcSuspendContext(alcGetCurrentContext());
            for(auto const i : std::views::iota(0_uz, laf->mChannels.size()))
            {
                auto const trackidx = i>>4;

                auto const posoffset = gsl::narrow<u32>(offset)/FramesPerPos*16_uz + (i&15);
                auto const x = laf->mPosTracks[trackidx][posoffset*3 + 0];
                auto const y = laf->mPosTracks[trackidx][posoffset*3 + 1];
                auto const z = laf->mPosTracks[trackidx][posoffset*3 + 2];

                alSource3f(laf->mChannels[i].mSource, AL_POSITION, x, y, -z);
            }
            alcProcessContext(alcGetCurrentContext());
        }
        if(alcRenderSamplesSOFT)
        {
            alcRenderSamplesSOFT(alcGetContextsDevice(alcGetCurrentContext()),
                renderbuf.data(), FramesPerPos);
            if(leadIn > std::ssize(renderbuf))
                leadIn -= std::ssize(renderbuf);
            else if(leadIn > 0)
            {
                auto const out = renderbuf | std::views::drop(leadIn);
                renderFile.write(out.data(), std::ssize(out));
                leadIn = 0;
            }
            else
                renderFile.write(renderbuf.data(), std::ssize(renderbuf));
        }
        else
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        alGetSourcei(laf->mChannels.back().mSource, AL_SAMPLE_OFFSET, &offset);
        alGetSourcei(laf->mChannels.back().mSource, AL_SOURCE_STATE, &state);
    }

    while(leadOut > 0)
    {
        alcRenderSamplesSOFT(alcGetContextsDevice(alcGetCurrentContext()),
            renderbuf.data(), FramesPerPos);
        auto const todo = std::min(std::ssize(renderbuf), leadOut);
        renderFile.write(renderbuf.data(), todo);
        leadOut -= todo;
    }

    if(renderStart > 0)
    {
        auto const renderEnd = std::streamoff{renderFile.tellp()};
        if(renderEnd > renderStart)
        {
            auto const dataLen = renderEnd - renderStart;
            if(renderFile.seekp(renderStart-8))
            {
                fwrite64be(gsl::narrow<u64>(dataLen), renderFile);
                renderFile.seekp(0, std::ios_base::end);
            }
        }
    }
}
catch(std::exception& e) {
    fmt::println(std::cerr, "Error playing {}:\n  {}", fname, e.what());
}

auto main(std::span<std::string_view> args) -> int
{
    /* Print out usage if no arguments were specified */
    if(args.size() < 2)
    {
        fmt::println(std::cerr,
            "Usage: {} [-device <name>] [-render <channels,samples>] <filenames...>\n"
            "\n"
            "  -render   Renders samples to an output file instead of real-time playback.\n"
            "            Outputs a CAF file with the same name as the input, but with the\n"
            "            \"caf\" extension.\n"
            "            Available channels: mono, stereo, hrtf, uhj, quad, surround51,\n"
            "                                surround61, surround71, ambi1, ambi2, ambi3,\n"
            "                                ambi4\n"
            "            Available samples: s16, f32",
            args[0]);
        return 1;
    }
    args = args.subspan(1);

    auto almgr = InitAL(args);
    almgr.printName();

    if(!args.empty() && args[0] == "-render")
    {
        if(args.size() < 2)
        {
            fmt::println(std::cerr, "Missing -render format");
            return 1;
        }
        auto params = std::vector<std::string>{};
        std::ranges::transform(args[1] | std::views::split(','), std::back_inserter(params),
            [](auto prange) { return std::string(prange.begin(), prange.end()); });
        if(params.size() != 2)
        {
            fmt::println(std::cerr, "Invalid -render argument: {}", args[1]);
            return 1;
        }
        args = args.subspan(2);

        RenderOutMode = ALC_ANY_SOFT;
        RenderAmbiOrder = 0;
        if(al::case_compare(params[0], "mono") == 0)
            RenderChannels = ALC_MONO_SOFT;
        else if(al::case_compare(params[0], "stereo") == 0)
        {
            RenderChannels = ALC_STEREO_SOFT;
            RenderOutMode = ALC_STEREO_BASIC_SOFT;
        }
        else if(al::case_compare(params[0], "hrtf") == 0)
        {
            RenderChannels = ALC_STEREO_SOFT;
            RenderOutMode = ALC_STEREO_HRTF_SOFT;
        }
        else if(al::case_compare(params[0], "uhj") == 0)
        {
            RenderChannels = ALC_STEREO_SOFT;
            RenderOutMode = ALC_STEREO_UHJ_SOFT;
        }
        else if(al::case_compare(params[0], "quad") == 0)
            RenderChannels = ALC_QUAD_SOFT;
        else if(al::case_compare(params[0], "surround51") == 0)
            RenderChannels = ALC_SURROUND_5_1_SOFT;
        else if(al::case_compare(params[0], "surround61") == 0)
            RenderChannels = ALC_SURROUND_6_1_SOFT;
        else if(al::case_compare(params[0], "surround71") == 0)
            RenderChannels = ALC_SURROUND_7_1_SOFT;
        else if(al::case_compare(params[0], "ambi1") == 0)
        {
            RenderChannels = ALC_BFORMAT3D_SOFT;
            RenderAmbiOrder = 1;
        }
        else if(al::case_compare(params[0], "ambi2") == 0)
        {
            RenderChannels = ALC_BFORMAT3D_SOFT;
            RenderAmbiOrder = 2;
        }
        else if(al::case_compare(params[0], "ambi3") == 0)
        {
            RenderChannels = ALC_BFORMAT3D_SOFT;
            RenderAmbiOrder = 3;
        }
        else if(al::case_compare(params[0], "ambi4") == 0)
        {
            RenderChannels = ALC_BFORMAT3D_SOFT;
            RenderAmbiOrder = 4;
        }
        else
        {
            fmt::println(std::cerr, "Unsupported channel configuration: {}", params[0]);
            return 1;
        }

        if(al::case_compare(params[1], "f32") == 0)
            RenderSamples = ALC_FLOAT_SOFT;
        else if(al::case_compare(params[1], "s16") == 0)
            RenderSamples = ALC_SHORT_SOFT;
        else
        {
            fmt::println(std::cerr, "Unsupported sample type: {}", params[1]);
            return 1;
        }

        RenderSampleRate = 48'000;

        if(!alcIsExtensionPresent(nullptr, "ALC_SOFT_loopback"))
        {
            fmt::println(std::cerr, "Loopback rendering not supported");
            return 1;
        }

        /* NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast) */
        auto const alcLoopbackOpenDevice = reinterpret_cast<LPALCLOOPBACKOPENDEVICESOFT>(
            alcGetProcAddress(nullptr, "alcLoopbackOpenDeviceSOFT"));
        auto const alcIsRenderFormatSupported = reinterpret_cast<LPALCISRENDERFORMATSUPPORTEDSOFT>(
            alcGetProcAddress(nullptr, "alcIsRenderFormatSupportedSOFT"));
        alcRenderSamplesSOFT = reinterpret_cast<LPALCRENDERSAMPLESSOFT>(
            alcGetProcAddress(nullptr, "alcRenderSamplesSOFT"));
        /* NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast) */

        auto loopbackDev = ALCdevicePtr{alcLoopbackOpenDevice(nullptr)};
        if(!loopbackDev)
        {
            fmt::println(std::cerr, "Failed to open loopback device: {:x}", alcGetError(nullptr));
            return 1;
        }

        if(!alcIsRenderFormatSupported(loopbackDev.get(), RenderSampleRate, RenderChannels,
            RenderSamples))
        {
            fmt::println(std::cerr, "Format {},{} @ {}hz not supported", params[0], params[1],
                RenderSampleRate);
            return 1;
        }
        if(RenderAmbiOrder > 0)
        {
            auto maxorder = ALCint{};
            if(alcIsExtensionPresent(loopbackDev.get(), "ALC_SOFT_loopback_bformat"))
                alcGetIntegerv(loopbackDev.get(), ALC_MAX_AMBISONIC_ORDER_SOFT, 1, &maxorder);
            if(RenderAmbiOrder > maxorder)
            {
                fmt::println(std::cerr, "Unsupported ambisonic order: {} (max: {})",
                    RenderAmbiOrder, maxorder);
                return 1;
            }
        }

        auto const attribs = std::to_array<ALCint>({
            ALC_FREQUENCY, RenderSampleRate,
            ALC_FORMAT_CHANNELS_SOFT, RenderChannels,
            ALC_FORMAT_TYPE_SOFT, RenderSamples,
            ALC_OUTPUT_MODE_SOFT, RenderOutMode,
            ALC_AMBISONIC_LAYOUT_SOFT, ALC_ACN_SOFT,
            ALC_AMBISONIC_SCALING_SOFT, ALC_SN3D_SOFT,
            ALC_AMBISONIC_ORDER_SOFT, RenderAmbiOrder,
            0});
        auto loopbackCtx = ALCcontextPtr{alcCreateContext(loopbackDev.get(), attribs.data())};
        if(!loopbackCtx || alcMakeContextCurrent(loopbackCtx.get()) == ALC_FALSE)
        {
            fmt::println(std::cerr, "Failed to create loopback device context: {:x}",
                alcGetError(loopbackDev.get()));
            return 1;
        }

        almgr.close();
        almgr.mDevice = loopbackDev.release();
        almgr.mContext = loopbackCtx.release();
    }

    /* Automate effect cleanup at end of scope (before almgr destructs). */
    const auto _ = gsl::finally([]
    {
        if(LfeSlotID)
        {
            alDeleteAuxiliaryEffectSlots(1, &LfeSlotID);
            alDeleteEffects(1, &LowFrequencyEffectID);
            alDeleteFilters(1, &MuteFilterID);
        }
    });

    if(alcIsExtensionPresent(almgr.mDevice, "ALC_EXT_EFX")
        && alcIsExtensionPresent(almgr.mDevice, "ALC_EXT_DEDICATED"))
    {
        static constexpr auto load_proc = []<typename T>(T &func, gsl::czstring const funcname)
        {
            /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
            func = reinterpret_cast<T>(alGetProcAddress(funcname));
            if(!func) fmt::println(std::cerr, "Failed to find function '{}'", funcname);
        };
#define LOAD_PROC(x) load_proc(x, #x)
        LOAD_PROC(alGenFilters);
        LOAD_PROC(alDeleteFilters);
        LOAD_PROC(alIsFilter);
        LOAD_PROC(alFilterf);
        LOAD_PROC(alFilterfv);
        LOAD_PROC(alFilteri);
        LOAD_PROC(alFilteriv);
        LOAD_PROC(alGetFilterf);
        LOAD_PROC(alGetFilterfv);
        LOAD_PROC(alGetFilteri);
        LOAD_PROC(alGetFilteriv);
        LOAD_PROC(alGenEffects);
        LOAD_PROC(alDeleteEffects);
        LOAD_PROC(alIsEffect);
        LOAD_PROC(alEffectf);
        LOAD_PROC(alEffectfv);
        LOAD_PROC(alEffecti);
        LOAD_PROC(alEffectiv);
        LOAD_PROC(alGetEffectf);
        LOAD_PROC(alGetEffectfv);
        LOAD_PROC(alGetEffecti);
        LOAD_PROC(alGetEffectiv);
        LOAD_PROC(alGenAuxiliaryEffectSlots);
        LOAD_PROC(alDeleteAuxiliaryEffectSlots);
        LOAD_PROC(alIsAuxiliaryEffectSlot);
        LOAD_PROC(alAuxiliaryEffectSlotf);
        LOAD_PROC(alAuxiliaryEffectSlotfv);
        LOAD_PROC(alAuxiliaryEffectSloti);
        LOAD_PROC(alAuxiliaryEffectSlotiv);
        LOAD_PROC(alGetAuxiliaryEffectSlotf);
        LOAD_PROC(alGetAuxiliaryEffectSlotfv);
        LOAD_PROC(alGetAuxiliaryEffectSloti);
        LOAD_PROC(alGetAuxiliaryEffectSlotiv);
#undef LOAD_PROC

        alGenFilters(1, &MuteFilterID);
        alFilteri(MuteFilterID, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
        alFilterf(MuteFilterID, AL_LOWPASS_GAIN, 0.0f);
        MyAssert(alGetError() == AL_NO_ERROR);

        alGenEffects(1, &LowFrequencyEffectID);
        alEffecti(LowFrequencyEffectID, AL_EFFECT_TYPE, AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT);
        MyAssert(alGetError() == AL_NO_ERROR);

        alGenAuxiliaryEffectSlots(1, &LfeSlotID);
        alAuxiliaryEffectSloti(LfeSlotID, AL_EFFECTSLOT_EFFECT, as_signed(LowFrequencyEffectID));
        MyAssert(alGetError() == AL_NO_ERROR);
    }

    std::ranges::for_each(args, PlayLAF);

    return 0;
}

} // namespace

auto main(int const argc, char **const argv) -> int
{
    auto args = std::vector<std::string_view>(gsl::narrow<unsigned>(argc));
    std::ranges::copy(std::views::counted(argv, argc), args.begin());
    return main(std::span{args});
}
