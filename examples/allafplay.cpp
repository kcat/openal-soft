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
 *
 * Some remaining issues:
 *
 * - There are bursts of static on some channels. This doesn't appear to be a
 *   parsing error since the bursts last less than the chunk size, and it never
 *   loses sync with the remaining chunks. Might be an encoding error with the
 *   files tested.
 *
 * - Positions are specified in left-handed coordinates, despite the LAF
 *   documentation saying it's right-handed. Might be an encoding error with
 *   the files tested, or might be a misunderstanding about which is which. How
 *   to proceed may depend on how wide-spread this issue ends up being, but for
 *   now, they're treated as left-handed here.
 *
 * - The LAF documentation doesn't specify the range or direction for the
 *   channels' X and Y axis rotation in Channels mode. Presumably X rotation
 *   (elevation) goes from -pi/2...+pi/2 and Y rotation (azimuth) goes from
 *   either -pi...+pi or 0...pi*2, but the direction of movement isn't
 *   specified. Currently positive azimuth moves from center rightward and
 *   positive elevation moves from head-level upward.
 */

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <numeric>
#include <ranges>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include "AL/alc.h"
#include "AL/al.h"
#include "AL/alext.h"

#include "alnumeric.h"
#include "alstring.h"
#include "common/alhelpers.hpp"
#include "filesystem.h"
#include "fmt/core.h"
#include "fmt/ranges.h"
#include "fmt/std.h"

#include "win_main_utf8.h"

namespace {

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


auto MuteFilterID = ALuint{};
auto LowFrequencyEffectID = ALuint{};
auto LfeSlotID = ALuint{};


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

template<typename... Ts>
overloaded(Ts...) -> overloaded<Ts...>;


enum class Quality : std::uint8_t {
    s8, s16, f32, s24
};
enum class Mode : bool {
    Channels, Objects
};

auto GetQualityName(Quality quality) noexcept -> std::string_view
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

auto GetModeName(Mode mode) noexcept -> std::string_view
{
    switch(mode)
    {
    case Mode::Channels: return "channels"sv;
    case Mode::Objects: return "objects"sv;
    }
    return "<unknown>"sv;
}

auto BytesFromQuality(Quality quality) noexcept -> size_t
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
struct SampleInfo<float> {
    static constexpr auto SrcSize = 4_uz;

    [[nodiscard]]
    static auto read(std::input_iterator auto input) -> float
    {
        auto src = std::array<char,4>{};
        if constexpr(std::endian::native == std::endian::little)
            std::ranges::copy(std::views::counted(input, 4), src.begin());
        else
            std::ranges::copy(std::views::counted(input, 4), src.rbegin());
        return std::bit_cast<float>(src);
    }
};

template<>
struct SampleInfo<int32_t> {
    static constexpr auto SrcSize = 3_uz;

    [[nodiscard]]
    static auto read(std::input_iterator auto input) -> int32_t
    {
        auto src = std::array<char,4>{};
        if constexpr(std::endian::native == std::endian::little)
            std::ranges::copy(std::views::counted(input, 3), src.begin()+1);
        else
            std::ranges::copy(std::views::counted(input, 3), src.rbegin()+1);
        return std::bit_cast<int32_t>(src);
    }
};

template<>
struct SampleInfo<int16_t> {
    static constexpr auto SrcSize = 2_uz;

    [[nodiscard]]
    static auto read(std::input_iterator auto input) -> int16_t
    {
        auto src = std::array<char,2>{};
        if constexpr(std::endian::native == std::endian::little)
            std::ranges::copy(std::views::counted(input, 2), src.begin());
        else
            std::ranges::copy(std::views::counted(input, 2), src.rbegin());
        return std::bit_cast<int16_t>(src);
    }
};

template<>
struct SampleInfo<int8_t> {
    static constexpr auto SrcSize = 1_uz;

    [[nodiscard]]
    static auto read(std::input_iterator auto input) -> int8_t
    { return std::bit_cast<int8_t>(*input); }
};


/* Each track with position data consists of a set of 3 samples per 16 audio
 * channels, resulting in a full set of positions being specified over 48
 * sample frames.
 */
constexpr auto FramesPerPos = 48_uz;

struct Channel {
    ALuint mSource{};
    std::array<ALuint,2> mBuffers{};
    float mAzimuth{};
    float mElevation{};
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
        if(mBuffers[0]) alDeleteBuffers(ALsizei(mBuffers.size()), mBuffers.data());
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
    std::filebuf mInFile;

    Quality mQuality{};
    Mode mMode{};
    uint32_t mNumTracks{};
    uint32_t mSampleRate{};
    ALenum mALFormat{};
    uint64_t mSampleCount{};

    uint64_t mCurrentSample{};

    std::array<uint8_t,32> mEnabledTracks{};
    uint32_t mNumEnabled{};
    std::vector<char> mSampleChunk;
    template<typename T> using vector = std::vector<T>;
    std::variant<vector<int8_t>,vector<int16_t>,vector<float>,vector<int32_t>> mSampleLine;

    std::vector<Channel> mChannels;
    std::vector<std::vector<float>> mPosTracks;

    LafStream() = default;
    LafStream(const LafStream&) = delete;
    ~LafStream() = default;
    auto operator=(const LafStream&) -> LafStream& = delete;

    [[nodiscard]]
    auto isAtEnd() const noexcept -> bool { return mCurrentSample >= mSampleCount; }

    [[nodiscard]]
    auto readChunk() -> uint32_t;

    [[nodiscard]]
    auto prepareTrack(size_t trackidx, size_t count) -> std::span<std::byte>;

    void convertSamples(const std::span<std::byte> samples) const;

    void convertPositions(const std::span<float> dst) const;
};

auto LafStream::readChunk() -> uint32_t
{
    auto enableTrackBits = std::array<char,std::tuple_size_v<decltype(mEnabledTracks)>>{};
    mInFile.sgetn(enableTrackBits.data(), std::streamsize(mNumTracks+7)>>3);

    mEnabledTracks = std::bit_cast<decltype(mEnabledTracks)>(enableTrackBits);
    mNumEnabled = std::accumulate(mEnabledTracks.cbegin(), mEnabledTracks.cend(), 0u,
        [](const unsigned int val, const uint8_t in) -> unsigned int
        { return val + unsigned(std::popcount(in)); });

    /* Make sure enable bits aren't set for non-existent tracks. */
    if(mEnabledTracks[((mNumTracks+7_uz)>>3) - 1] >= (1u<<(mNumTracks&7)))
        throw std::runtime_error{"Invalid channel enable bits"};

    /* Each chunk is exactly one second long, with samples interleaved for each
     * enabled track. The last chunk may be shorter if there isn't enough time
     * remaining for a full second.
     */
    const auto numsamples = std::min(uint64_t{mSampleRate}, mSampleCount-mCurrentSample);

    const auto toread = std::streamsize(numsamples * BytesFromQuality(mQuality) * mNumEnabled);
    if(mInFile.sgetn(mSampleChunk.data(), toread) != toread)
        throw std::runtime_error{"Failed to read sample chunk"};

    std::ranges::fill(mSampleChunk | std::views::drop(toread), char{});

    mCurrentSample += numsamples;
    return static_cast<uint32_t>(numsamples);
}

auto LafStream::prepareTrack(const size_t trackidx, const size_t count) -> std::span<std::byte>
{
    const auto todo = std::min(size_t{mSampleRate}, count);
    if((mEnabledTracks[trackidx>>3] & (1_uz<<(trackidx&7))))
    {
        /* If the track is enabled, get the real index (skipping disabled
         * tracks), and deinterlace it into the mono line.
         */
        const auto idx = std::invoke([this,trackidx]() -> unsigned int
        {
            const auto bits = std::span{mEnabledTracks}.first(trackidx>>3);
            const auto res = std::accumulate(bits.begin(), bits.end(), 0u,
                [](const unsigned int val, const uint8_t in) -> unsigned int
            { return val + unsigned(std::popcount(in)); });
            return unsigned(std::popcount(mEnabledTracks[trackidx>>3] & ((1u<<(trackidx&7))-1)))
                + res;
        });

        const auto step = size_t{mNumEnabled};
        assert(idx < step);
        return std::visit([=,src=std::span{mSampleChunk}](auto &dst)
        {
            using sample_t = typename std::remove_cvref_t<decltype(dst)>::value_type;
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
    return std::visit([todo](auto &dst)
    {
        using sample_t = typename std::remove_cvref_t<decltype(dst)>::value_type;
        std::ranges::fill(dst, sample_t{});
        return std::as_writable_bytes(std::span{dst}.first(todo));
    }, mSampleLine);
}

void LafStream::convertSamples(const std::span<std::byte> samples) const
{
    /* OpenAL uses unsigned 8-bit samples (0...255), so signed 8-bit samples
     * (-128...+127) need conversion. The other formats are fine.
     */
    if(mQuality == Quality::s8)
    {
        std::ranges::transform(samples, samples.begin(),
            [](const std::byte sample) noexcept -> std::byte
        {
            return sample^std::byte{0x80};
        });
    }
}

void LafStream::convertPositions(const std::span<float> dst) const
{
    std::visit(overloaded {
        [dst](const vector<int8_t> &src)
        {
            std::ranges::transform(src, dst.begin(), [](const int8_t in) noexcept -> float
            { return float(in) / 127.0f; });
        },
        [dst](const vector<int16_t> &src)
        {
            std::ranges::transform(src, dst.begin(), [](const int16_t in) noexcept -> float
            { return float(in) / 32767.0f; });
        },
        [dst](const vector<float> &src) { std::ranges::copy(src, dst.begin()); },
        [dst](const vector<int32_t> &src)
        {
            /* 24-bit samples are converted to 32-bit in copySamples. */
            std::ranges::transform(src, dst.begin(), [](const int32_t in) noexcept -> float
            { return float(in>>8) / 8388607.0f; });
        },
    }, mSampleLine);
}

auto LoadLAF(const fs::path &fname) -> std::unique_ptr<LafStream>
{
    auto laf = std::make_unique<LafStream>();
    if(!laf->mInFile.open(fname, std::ios_base::binary | std::ios_base::in))
        throw std::runtime_error{"Could not open file"};

    auto marker = std::array<char,9>{};
    if(laf->mInFile.sgetn(marker.data(), marker.size()) != marker.size())
        throw std::runtime_error{"Failed to read file marker"};
    if(std::string_view{marker.data(), marker.size()} != "LIMITLESS"sv)
        throw std::runtime_error{"Not an LAF file"};

    auto header = std::array<char,10>{};
    if(laf->mInFile.sgetn(header.data(), header.size()) != header.size())
        throw std::runtime_error{"Failed to read header"};
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

        const auto toread = std::distance(hiter, header.end());
        if(laf->mInFile.sgetn(std::to_address(hiter), toread) != toread)
            throw std::runtime_error{"Failed to read header"};
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
        return uint32_t{uint8_t(input[0])} | (uint32_t{uint8_t(input[1])}<<8u)
            | (uint32_t{uint8_t(input[2])}<<16u) | (uint32_t{uint8_t(input[3])}<<24u);
    });

    fmt::println("Filename: {}", fname.string());
    fmt::println(" quality: {}", GetQualityName(laf->mQuality));
    fmt::println(" mode: {}", GetModeName(laf->mMode));
    fmt::println(" track count: {}", laf->mNumTracks);

    if(laf->mNumTracks == 0)
        throw std::runtime_error{"No tracks"};
    if(laf->mNumTracks > 256)
        throw std::runtime_error{fmt::format("Too many tracks: {}", laf->mNumTracks)};

    auto chandata = std::vector<char>(laf->mNumTracks*9_uz);
    auto headersize = std::streamsize(chandata.size());
    if(laf->mInFile.sgetn(chandata.data(), headersize) != headersize)
        throw std::runtime_error{"Failed to read channel header data"};

    if(laf->mMode == Mode::Channels)
        laf->mChannels.resize(laf->mNumTracks);
    else
    {
        if(laf->mNumTracks < 2)
            throw std::runtime_error{"Not enough tracks"};

        auto numchans = uint32_t{laf->mNumTracks - 1};
        auto numpostracks = uint32_t{1};
        while(numpostracks*16 < numchans)
        {
            --numchans;
            ++numpostracks;
        }
        laf->mChannels.resize(numchans);
        laf->mPosTracks.resize(numpostracks);
    }

    static constexpr auto read_float = [](std::span<char,4> input)
    {
        const auto value = uint32_t{uint8_t(input[0])} | (uint32_t{uint8_t(input[1])}<<8u)
            | (uint32_t{uint8_t(input[2])}<<16u) | (uint32_t{uint8_t(input[3])}<<24u);
        return std::bit_cast<float>(value);
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
    if(laf->mInFile.sgetn(footer.data(), footer.size()) != footer.size())
        throw std::runtime_error{"Failed to read sample header data"};

    laf->mSampleRate = std::invoke([input=std::span{footer}.first<4>()]
    {
        return uint32_t{uint8_t(input[0])} | (uint32_t{uint8_t(input[1])}<<8u)
            | (uint32_t{uint8_t(input[2])}<<16u) | (uint32_t{uint8_t(input[3])}<<24u);
    });
    laf->mSampleCount = std::invoke([input=std::span{footer}.last<8>()]
    {
        return uint64_t{uint8_t(input[0])} | (uint64_t{uint8_t(input[1])}<<8)
            | (uint64_t{uint8_t(input[2])}<<16u) | (uint64_t{uint8_t(input[3])}<<24u)
            | (uint64_t{uint8_t(input[4])}<<32u) | (uint64_t{uint8_t(input[5])}<<40u)
            | (uint64_t{uint8_t(input[6])}<<48u) | (uint64_t{uint8_t(input[7])}<<56u);
    });
    fmt::println("Sample rate: {}", laf->mSampleRate);
    fmt::println("Length: {} samples ({:.2f} sec)", laf->mSampleCount,
        static_cast<double>(laf->mSampleCount)/static_cast<double>(laf->mSampleRate));

    /* Position vectors get split across the PCM chunks if the sample rate
     * isn't a multiple of 48. Each PCM chunk is exactly one second (the sample
     * rate in sample frames). Each track with position data consists of a set
     * of 3 samples for 16 audio channels, resuling in 48 sample frames for a
     * full set of positions. Extra logic will be needed to manage the position
     * frame offset separate from each chunk.
     */
    MyAssert(laf->mMode == Mode::Channels || (laf->mSampleRate%FramesPerPos) == 0);

    std::ranges::generate(laf->mPosTracks, [length=laf->mSampleRate*2_uz]
    { return std::vector<float>(length, 0.0f); });

    laf->mSampleChunk.resize(laf->mSampleRate*BytesFromQuality(laf->mQuality)*laf->mNumTracks);
    switch(laf->mQuality)
    {
    case Quality::s8: laf->mSampleLine.emplace<std::vector<int8_t>>(laf->mSampleRate); break;
    case Quality::s16: laf->mSampleLine.emplace<std::vector<int16_t>>(laf->mSampleRate); break;
    case Quality::f32: laf->mSampleLine.emplace<std::vector<float>>(laf->mSampleRate); break;
    case Quality::s24: laf->mSampleLine.emplace<std::vector<int32_t>>(laf->mSampleRate); break;
    }

    return laf;
}

void PlayLAF(std::string_view fname)
try {
    auto laf = LoadLAF(fs::path(al::char_as_u8(fname)));

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
        alGenBuffers(ALsizei(channel.mBuffers.size()), channel.mBuffers.data());

        /* Disable distance attenuation, and make sure the source stays locked
         * relative to the listener.
         */
        alSourcef(channel.mSource, AL_ROLLOFF_FACTOR, 0.0f);
        alSourcei(channel.mSource, AL_SOURCE_RELATIVE, AL_TRUE);

        /* FIXME: Is the Y rotation/azimuth clockwise or counter-clockwise?
         * Does +azimuth move a front sound right or left?
         */
        const auto x = std::sin(channel.mAzimuth) * std::cos(channel.mElevation);
        const auto y = std::sin(channel.mElevation);
        const auto z = -std::cos(channel.mAzimuth) * std::cos(channel.mElevation);
        alSource3f(channel.mSource, AL_POSITION, x, y, z);

        if(channel.mIsLfe)
        {
            if(LfeSlotID)
            {
                /* For LFE, silence the direct/dry path and connect the LFE aux
                 * slot on send 0.
                 */
                alSourcei(channel.mSource, AL_DIRECT_FILTER, ALint(MuteFilterID));
                alSource3i(channel.mSource, AL_AUXILIARY_SEND_FILTER, ALint(LfeSlotID), 0,
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
                for(auto i = 0_uz;i < laf->mChannels.size();++i)
                {
                    const auto trackidx = i>>4;

                    const auto posoffset = unsigned(offset)/FramesPerPos*16_uz + (i&15);
                    const auto x = laf->mPosTracks[trackidx][posoffset*3 + 0];
                    const auto y = laf->mPosTracks[trackidx][posoffset*3 + 1];
                    const auto z = laf->mPosTracks[trackidx][posoffset*3 + 2];

                    /* Contrary to the docs, the position is left-handed and
                     * needs to be converted to right-handed.
                     */
                    alSource3f(laf->mChannels[i].mSource, AL_POSITION, x, y, -z);
                }
                alcProcessContext(alcGetCurrentContext());
            }

            /* Unqueue processed buffers and refill with the next chunk, or
             * sleep for ~10ms before updating again.
             */
            if(processed > 0)
            {
                const auto numsamples = laf->readChunk();
                for(auto i = 0_uz;i < laf->mChannels.size();++i)
                {
                    const auto samples = laf->prepareTrack(i, numsamples);
                    laf->convertSamples(samples);

                    auto bufid = ALuint{};
                    alSourceUnqueueBuffers(laf->mChannels[i].mSource, 1, &bufid);
                    alBufferData(bufid, laf->mALFormat, samples.data(), ALsizei(samples.size()),
                        ALsizei(laf->mSampleRate));
                    alSourceQueueBuffers(laf->mChannels[i].mSource, 1, &bufid);
                }
                for(auto i = 0_uz;i < laf->mPosTracks.size();++i)
                {
                    std::ranges::copy(laf->mPosTracks[i] | std::views::drop(laf->mSampleRate),
                        laf->mPosTracks[i].begin());

                    std::ignore = laf->prepareTrack(laf->mChannels.size()+i, numsamples);
                    laf->convertPositions(std::span{laf->mPosTracks[i]}.last(laf->mSampleRate));
                }
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
            auto sources = std::array<ALuint,256_uz>{};
            std::ranges::transform(laf->mChannels, sources.begin(), &Channel::mSource);
            alSourcePlayv(ALsizei(laf->mChannels.size()), sources.data());
        }
        else if(state == AL_INITIAL)
        {
            /* Starting playback. Read and prepare the two second-long chunks
             * per track (buffering audio samples to OpenAL, and storing the
             * position vectors).
             */
            auto numsamples = laf->readChunk();
            for(auto i = 0_uz;i < laf->mChannels.size();++i)
            {
                const auto samples = laf->prepareTrack(i, numsamples);
                laf->convertSamples(samples);
                alBufferData(laf->mChannels[i].mBuffers[0], laf->mALFormat, samples.data(),
                    ALsizei(samples.size()), ALsizei(laf->mSampleRate));
            }
            for(auto i = 0_uz;i < laf->mPosTracks.size();++i)
            {
                std::ignore = laf->prepareTrack(laf->mChannels.size()+i, numsamples);
                laf->convertPositions(std::span{laf->mPosTracks[i]}.first(laf->mSampleRate));
            }

            numsamples = laf->readChunk();
            for(auto i = 0_uz;i < laf->mChannels.size();++i)
            {
                const auto samples = laf->prepareTrack(i, numsamples);
                laf->convertSamples(samples);
                alBufferData(laf->mChannels[i].mBuffers[1], laf->mALFormat, samples.data(),
                    ALsizei(samples.size()), ALsizei(laf->mSampleRate));
                alSourceQueueBuffers(laf->mChannels[i].mSource,
                    ALsizei(laf->mChannels[i].mBuffers.size()), laf->mChannels[i].mBuffers.data());
            }
            for(auto i = 0_uz;i < laf->mPosTracks.size();++i)
            {
                std::ignore = laf->prepareTrack(laf->mChannels.size()+i, numsamples);
                laf->convertPositions(std::span{laf->mPosTracks[i]}.last(laf->mSampleRate));
            }

            /* Set the initial source positions for dynamic objects, then start
             * all sources in sync.
             */
            if(!laf->mPosTracks.empty())
            {
                for(size_t i{0};i < laf->mChannels.size();++i)
                {
                    const auto trackidx = i>>4;

                    const auto x = laf->mPosTracks[trackidx][(i&15)*3 + 0];
                    const auto y = laf->mPosTracks[trackidx][(i&15)*3 + 1];
                    const auto z = laf->mPosTracks[trackidx][(i&15)*3 + 2];

                    alSource3f(laf->mChannels[i].mSource, AL_POSITION, x, y, -z);
                }
            }

            auto sources = std::array<ALuint,256_uz>{};
            std::ranges::transform(laf->mChannels, sources.begin(), &Channel::mSource);
            alSourcePlayv(ALsizei(laf->mChannels.size()), sources.data());
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
            for(size_t i{0};i < laf->mChannels.size();++i)
            {
                const auto trackidx = i>>4;

                const auto posoffset = unsigned(offset)/FramesPerPos*16_uz + (i&15);
                const auto x = laf->mPosTracks[trackidx][posoffset*3 + 0];
                const auto y = laf->mPosTracks[trackidx][posoffset*3 + 1];
                const auto z = laf->mPosTracks[trackidx][posoffset*3 + 2];

                alSource3f(laf->mChannels[i].mSource, AL_POSITION, x, y, -z);
            }
            alcProcessContext(alcGetCurrentContext());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        alGetSourcei(laf->mChannels.back().mSource, AL_SAMPLE_OFFSET, &offset);
        alGetSourcei(laf->mChannels.back().mSource, AL_SOURCE_STATE, &state);
    }
}
catch(std::exception& e) {
    fmt::println(stderr, "Error playing {}:\n  {}", fname, e.what());
}

auto main(std::span<std::string_view> args) -> int
{
    /* Print out usage if no arguments were specified */
    if(args.size() < 2)
    {
        fmt::println(stderr, "Usage: {} [-device <name>] <filenames...>\n", args[0]);
        return 1;
    }
    args = args.subspan(1);

    auto almgr = InitAL(args);

    /* A simple RAII container for automating OpenAL cleanup. */
    struct Cleaner {
        Cleaner() = default;
        Cleaner(const Cleaner&) = delete;
        auto operator=(const Cleaner&) -> Cleaner& = delete;
        ~Cleaner()
        {
            if(LfeSlotID)
            {
                alDeleteAuxiliaryEffectSlots(1, &LfeSlotID);
                alDeleteEffects(1, &LowFrequencyEffectID);
                alDeleteFilters(1, &MuteFilterID);
            }
        }
    };
    auto cleaner = Cleaner{};

    if(alcIsExtensionPresent(almgr.device, "ALC_EXT_EFX")
        && alcIsExtensionPresent(almgr.device, "ALC_EXT_DEDICATED"))
    {
#define LOAD_PROC(x) do {                                                     \
        x = reinterpret_cast<decltype(x)>(alGetProcAddress(#x));              \
        if(!x) fmt::println(stderr, "Failed to find function '{}'", #x##sv);  \
    } while(0)
        /* NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast) */
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
        /* NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast) */
#undef LOAD_PROC

        alGenFilters(1, &MuteFilterID);
        alFilteri(MuteFilterID, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
        alFilterf(MuteFilterID, AL_LOWPASS_GAIN, 0.0f);
        MyAssert(alGetError() == AL_NO_ERROR);

        alGenEffects(1, &LowFrequencyEffectID);
        alEffecti(LowFrequencyEffectID, AL_EFFECT_TYPE, AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT);
        MyAssert(alGetError() == AL_NO_ERROR);

        alGenAuxiliaryEffectSlots(1, &LfeSlotID);
        alAuxiliaryEffectSloti(LfeSlotID, AL_EFFECTSLOT_EFFECT, ALint(LowFrequencyEffectID));
        MyAssert(alGetError() == AL_NO_ERROR);
    }

    std::ranges::for_each(args, PlayLAF);

    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    MyAssert(argc >= 0);
    auto args = std::vector<std::string_view>(static_cast<unsigned int>(argc));
    std::ranges::copy(std::views::counted(argv, argc), args.begin());
    return main(std::span{args});
}
