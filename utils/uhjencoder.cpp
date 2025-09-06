/*
 * 2-channel UHJ Encoder
 *
 * Copyright (c) Chris Robinson <chris.kcat@gmail.com>
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

#include "config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <numbers>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "alnumeric.h"
#include "alstring.h"
#include "phase_shifter.h"
#include "vector.h"

#include "sndfile.h"

#include "win_main_utf8.h"

#if HAVE_CXXMODULES
import alsoft.fmt;
import alsoft.gsl;

#else

#include "fmt/base.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h"
#include "fmt/std.h"
#include "gsl/gsl"
#endif

namespace {

using namespace std::string_view_literals;

using SndFilePtr = std::unique_ptr<SNDFILE, decltype([](SNDFILE *sndfile) { sf_close(sndfile); })>;


using uint = unsigned int;

constexpr auto BufferLineSize = 1024u;

using FloatBufferLine = std::array<float,BufferLineSize>;
using FloatBufferSpan = std::span<float,BufferLineSize>;


struct UhjEncoder {
    constexpr static auto sFilterDelay = 1024_uz;

    /* Delays and processing storage for the unfiltered signal. */
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mW{};
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mX{};
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mY{};
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mZ{};

    alignas(16) std::array<float,BufferLineSize> mS{};
    alignas(16) std::array<float,BufferLineSize> mD{};
    alignas(16) std::array<float,BufferLineSize> mT{};

    /* History for the FIR filter. */
    alignas(16) std::array<float,sFilterDelay*2 - 1> mWXHistory1{};
    alignas(16) std::array<float,sFilterDelay*2 - 1> mWXHistory2{};

    alignas(16) std::array<float,BufferLineSize + sFilterDelay*2> mTemp{};

    void encode(const std::span<FloatBufferLine> OutSamples,
        const std::span<const FloatBufferLine,4> InSamples, const size_t SamplesToDo);
};

const PhaseShifterT<UhjEncoder::sFilterDelay*2> PShift{};


/* Encoding UHJ from B-Format is done as:
 *
 * S = 0.9396926*W + 0.1855740*X
 * D = j(-0.3420201*W + 0.5098604*X) + 0.6554516*Y
 *
 * Left = (S + D)/2.0
 * Right = (S - D)/2.0
 * T = j(-0.1432*W + 0.6512*X) - 0.7071068*Y
 * Q = 0.9772*Z
 *
 * where j is a wide-band +90 degree phase shift. T is excluded from 2-channel
 * output, and Q is excluded from 2- and 3-channel output.
 */
void UhjEncoder::encode(const std::span<FloatBufferLine> OutSamples,
    const std::span<const FloatBufferLine,4> InSamples, const size_t SamplesToDo)
{
    const auto take_todo = std::views::take(SamplesToDo);
    const auto skip_todo = std::views::drop(SamplesToDo);
    constexpr auto skip_filter = std::views::drop(sFilterDelay);

    const auto winput = InSamples[0] | take_todo;
    const auto xinput = InSamples[1] | take_todo;
    const auto yinput = InSamples[2] | take_todo;
    const auto zinput = InSamples[3] | take_todo;

    /* Combine the previously delayed input signal with the new input. */
    std::ranges::copy(winput, (mW | skip_filter).begin());
    std::ranges::copy(xinput, (mX | skip_filter).begin());
    std::ranges::copy(yinput, (mY | skip_filter).begin());
    std::ranges::copy(zinput, (mZ | skip_filter).begin());

    /* S = 0.9396926*W + 0.1855740*X */
    std::ranges::transform(mW | take_todo, mX | take_todo, mS.begin(),
        [](const float w, const float x) { return 0.9396926f*w + 0.1855740f*x; });

    /* Precompute j(-0.3420201*W + 0.5098604*X) and store in mD. */
    auto tmpiter = std::ranges::copy(mWXHistory1, mTemp.begin()).out;
    std::ranges::transform(winput, xinput, tmpiter, [](const float w, const float x) -> float
    { return -0.3420201f*w + 0.5098604f*x; });
    std::ranges::copy(mTemp|skip_todo|std::views::take(mWXHistory1.size()), mWXHistory1.begin());
    PShift.process(std::span{mD}.first(SamplesToDo), mTemp);

    /* D = j(-0.3420201*W + 0.5098604*X) + 0.6554516*Y */
    std::ranges::transform(mD | take_todo, mY | take_todo, mD.begin(),
        [](const float jwx, const float y) -> float { return jwx + 0.6554516f*y; });

    /* Left = (S + D)/2.0 */
    auto left = std::span{OutSamples[0]};
    std::ranges::transform(mS | take_todo, mD | take_todo, left.begin(),
        [](const float s, const float d) -> float { return (s + d) * 0.5f; });
    /* Right = (S - D)/2.0 */
    auto right = std::span{OutSamples[1]};
    std::ranges::transform(mS | take_todo, mD | take_todo, right.begin(),
        [](const float s, const float d) -> float { return (s - d) * 0.5f; });

    if(OutSamples.size() > 2)
    {
        /* Precompute j(-0.1432*W + 0.6512*X) and store in mT. */
        tmpiter = std::ranges::copy(mWXHistory2, mTemp.begin()).out;
        std::ranges::transform(winput, xinput, tmpiter, [](const float w, const float x) -> float
        { return -0.1432f*w + 0.6512f*x; });
        std::ranges::copy(mTemp | skip_todo | std::views::take(mWXHistory2.size()),
            mWXHistory2.begin());
        PShift.process(std::span{mT}.first(SamplesToDo), mTemp);

        /* T = j(-0.1432*W + 0.6512*X) - 0.7071068*Y */
        auto t = std::span{OutSamples[2]};
        std::ranges::transform(mT | take_todo, mY | take_todo, t.begin(),
            [](const float jwx, const float y) -> float { return jwx - 0.7071068f*y; });
    }
    if(OutSamples.size() > 3)
    {
        /* Q = 0.9772*Z */
        auto q = std::span{OutSamples[3]};
        std::ranges::transform(mZ | take_todo, q.begin(), [](const float z) noexcept -> float
        { return 0.9772f*z; });
    }

    /* Copy the future samples to the front for next time. */
    const auto get_end = skip_todo | std::views::take(sFilterDelay);
    std::ranges::copy(mW | get_end, mW.begin());
    std::ranges::copy(mX | get_end, mX.begin());
    std::ranges::copy(mY | get_end, mY.begin());
    std::ranges::copy(mZ | get_end, mZ.begin());
}


struct SpeakerPos {
    int mChannelID;
    float mAzimuth;
    float mElevation;
};

/* Azimuth is counter-clockwise. */
constexpr auto MonoMap = std::array{
    SpeakerPos{SF_CHANNEL_MAP_CENTER, 0.0f, 0.0f},
};
constexpr auto StereoMap = std::array{
    SpeakerPos{SF_CHANNEL_MAP_LEFT,   30.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_RIGHT, -30.0f, 0.0f},
};
constexpr auto QuadMap = std::array{
    SpeakerPos{SF_CHANNEL_MAP_LEFT,         45.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_RIGHT,       -45.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_REAR_LEFT,   135.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_REAR_RIGHT, -135.0f, 0.0f},
};
constexpr auto X51Map = std::array{
    SpeakerPos{SF_CHANNEL_MAP_LEFT,         30.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_RIGHT,       -30.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_CENTER,        0.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_LFE, 0.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_SIDE_LEFT,   110.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_SIDE_RIGHT, -110.0f, 0.0f},
};
constexpr auto X51RearMap = std::array{
    SpeakerPos{SF_CHANNEL_MAP_LEFT,         30.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_RIGHT,       -30.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_CENTER,        0.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_LFE, 0.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_REAR_LEFT,   110.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_REAR_RIGHT, -110.0f, 0.0f},
};
constexpr auto X71Map = std::array{
    SpeakerPos{SF_CHANNEL_MAP_LEFT,         30.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_RIGHT,       -30.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_CENTER,        0.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_LFE, 0.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_REAR_LEFT,   150.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_REAR_RIGHT, -150.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_SIDE_LEFT,    90.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_SIDE_RIGHT,  -90.0f, 0.0f},
};
constexpr auto X714Map = std::array{
    SpeakerPos{SF_CHANNEL_MAP_LEFT,         30.0f,  0.0f},
    SpeakerPos{SF_CHANNEL_MAP_RIGHT,       -30.0f,  0.0f},
    SpeakerPos{SF_CHANNEL_MAP_CENTER,        0.0f,  0.0f},
    SpeakerPos{SF_CHANNEL_MAP_LFE, 0.0f, 0.0f},
    SpeakerPos{SF_CHANNEL_MAP_REAR_LEFT,   150.0f,  0.0f},
    SpeakerPos{SF_CHANNEL_MAP_REAR_RIGHT, -150.0f,  0.0f},
    SpeakerPos{SF_CHANNEL_MAP_SIDE_LEFT,    90.0f,  0.0f},
    SpeakerPos{SF_CHANNEL_MAP_SIDE_RIGHT,  -90.0f,  0.0f},
    SpeakerPos{SF_CHANNEL_MAP_TOP_FRONT_LEFT,    45.0f, 35.0f},
    SpeakerPos{SF_CHANNEL_MAP_TOP_FRONT_RIGHT,  -45.0f, 35.0f},
    SpeakerPos{SF_CHANNEL_MAP_TOP_REAR_LEFT,    135.0f, 35.0f},
    SpeakerPos{SF_CHANNEL_MAP_TOP_REAR_RIGHT,  -135.0f, 35.0f},
};

constexpr auto GenCoeffs(double x /*+front*/, double y /*+left*/, double z /*+up*/) noexcept
{
    /* Coefficients are +3dB of FuMa. */
    return std::array{1.0f,
        gsl::narrow_cast<float>(std::numbers::sqrt2 * x),
        gsl::narrow_cast<float>(std::numbers::sqrt2 * y),
        gsl::narrow_cast<float>(std::numbers::sqrt2 * z)};
}


auto main(std::span<std::string_view> args) -> int
{
    if(args.size() < 2 || args[1] == "-h" || args[1] == "--help")
    {
        fmt::println("Usage: {} <[options] infile...>\n\n"
            "  Options:\n"
            "    -bhj  Encode 2-channel UHJ, aka \"BJH\" (default).\n"
            "    -thj  Encode 3-channel UHJ, aka \"TJH\".\n"
            "    -phj  Encode 4-channel UHJ, aka \"PJH\".\n"
            "\n"
            "3-channel UHJ supplements 2-channel UHJ with an extra channel that allows full\n"
            "reconstruction of first-order 2D ambisonics. 4-channel UHJ supplements 3-channel\n"
            "UHJ with an extra channel carrying height information, providing for full\n"
            "reconstruction of first-order 3D ambisonics.\n"
            "\n"
            "Note: The third and fourth channels should be ignored if they're not being\n"
            "decoded. Unlike the first two channels, they are not designed for undecoded\n"
            "playback, so the resulting files will not play correctly if this isn't handled.",
            args[0]);
        return 1;
    }
    args = args.subspan(1);

    auto uhjchans = 2u;
    auto num_files = 0_uz;
    auto num_encoded = 0_uz;
    std::ranges::for_each(args, [&uhjchans,&num_files,&num_encoded](std::string_view arg) -> void
    {
        if(arg == "-bhj"sv)
        {
            uhjchans = 2;
            return;
        }
        if(arg == "-thj"sv)
        {
            uhjchans = 3;
            return;
        }
        if(arg == "-phj"sv)
        {
            uhjchans = 4;
            return;
        }
        ++num_files;

        const auto outname = std::filesystem::path(al::char_as_u8(arg)).stem()
            .replace_extension(u8".uhj.flac");

        auto ininfo = SF_INFO{};
        auto infile = SndFilePtr{sf_open(std::string{arg}.c_str(), SFM_READ, &ininfo)};
        if(!infile)
        {
            fmt::println(std::cerr, "Failed to open {}", arg);
            return;
        }
        fmt::println("Converting {} to {}...", arg, outname);
        const auto inchannels = gsl::narrow<uint>(ininfo.channels);

        /* Work out the channel map, preferably using the actual channel map
         * from the file/format, but falling back to assuming WFX order.
         */
        auto spkrs = std::span<const SpeakerPos>{};
        auto chanmap = std::vector<int>(inchannels, SF_CHANNEL_MAP_INVALID);
        if(sf_command(infile.get(), SFC_GET_CHANNEL_MAP_INFO, chanmap.data(),
            gsl::narrow<int>(std::span{chanmap}.size_bytes())) == SF_TRUE)
        {
            static constexpr auto monomap = std::array{SF_CHANNEL_MAP_CENTER};
            static constexpr auto stereomap = std::array{SF_CHANNEL_MAP_LEFT,SF_CHANNEL_MAP_RIGHT};
            static constexpr auto quadmap = std::array{SF_CHANNEL_MAP_LEFT, SF_CHANNEL_MAP_RIGHT,
                SF_CHANNEL_MAP_REAR_LEFT, SF_CHANNEL_MAP_REAR_RIGHT};
            static constexpr auto x51map = std::array{SF_CHANNEL_MAP_LEFT, SF_CHANNEL_MAP_RIGHT,
                SF_CHANNEL_MAP_CENTER, SF_CHANNEL_MAP_LFE,
                SF_CHANNEL_MAP_SIDE_LEFT, SF_CHANNEL_MAP_SIDE_RIGHT};
            static constexpr auto x51rearmap = std::array{SF_CHANNEL_MAP_LEFT,SF_CHANNEL_MAP_RIGHT,
                SF_CHANNEL_MAP_CENTER, SF_CHANNEL_MAP_LFE,
                SF_CHANNEL_MAP_REAR_LEFT, SF_CHANNEL_MAP_REAR_RIGHT};
            static constexpr auto x71map = std::array{SF_CHANNEL_MAP_LEFT, SF_CHANNEL_MAP_RIGHT,
                SF_CHANNEL_MAP_CENTER, SF_CHANNEL_MAP_LFE,
                SF_CHANNEL_MAP_REAR_LEFT, SF_CHANNEL_MAP_REAR_RIGHT,
                SF_CHANNEL_MAP_SIDE_LEFT, SF_CHANNEL_MAP_SIDE_RIGHT};
            static constexpr auto x714map = std::array{SF_CHANNEL_MAP_LEFT, SF_CHANNEL_MAP_RIGHT,
                SF_CHANNEL_MAP_CENTER, SF_CHANNEL_MAP_LFE,
                SF_CHANNEL_MAP_REAR_LEFT, SF_CHANNEL_MAP_REAR_RIGHT,
                SF_CHANNEL_MAP_SIDE_LEFT, SF_CHANNEL_MAP_SIDE_RIGHT,
                SF_CHANNEL_MAP_TOP_FRONT_LEFT, SF_CHANNEL_MAP_TOP_FRONT_RIGHT,
                SF_CHANNEL_MAP_TOP_REAR_LEFT, SF_CHANNEL_MAP_TOP_REAR_RIGHT};
            static constexpr auto ambi2dmap = std::array{SF_CHANNEL_MAP_AMBISONIC_B_W,
                SF_CHANNEL_MAP_AMBISONIC_B_X, SF_CHANNEL_MAP_AMBISONIC_B_Y};
            static constexpr auto ambi3dmap = std::array{SF_CHANNEL_MAP_AMBISONIC_B_W,
                SF_CHANNEL_MAP_AMBISONIC_B_X, SF_CHANNEL_MAP_AMBISONIC_B_Y,
                SF_CHANNEL_MAP_AMBISONIC_B_Z};

            static constexpr auto match_chanmap = [](const std::span<const int> a,
                const std::span<const decltype(SF_CHANNEL_MAP_INVALID)> b) -> bool
            {
                if(a.size() != b.size())
                    return false;
                return std::ranges::all_of(a, [b](const int id) -> bool
                { return std::ranges::find(b, id) != b.end(); });
            };
            if(match_chanmap(chanmap, monomap))
                spkrs = MonoMap;
            else if(match_chanmap(chanmap, stereomap))
                spkrs = StereoMap;
            else if(match_chanmap(chanmap, quadmap))
                spkrs = QuadMap;
            else if(match_chanmap(chanmap, x51map))
                spkrs = X51Map;
            else if(match_chanmap(chanmap, x51rearmap))
                spkrs = X51RearMap;
            else if(match_chanmap(chanmap, x71map))
                spkrs = X71Map;
            else if(match_chanmap(chanmap, x714map))
                spkrs = X714Map;
            else if(match_chanmap(chanmap, ambi2dmap) || match_chanmap(chanmap, ambi3dmap))
            {
                /* Do nothing. */
            }
            else
            {
                fmt::println(std::cerr, " ... {} channels not supported (map: {})", chanmap.size(),
                    fmt::join(chanmap, ", "));
                return;
            }
        }
        else if(sf_command(infile.get(), SFC_WAVEX_GET_AMBISONIC, nullptr,
            0) == SF_AMBISONIC_B_FORMAT)
        {
            switch(inchannels)
            {
            case 4:
                fmt::println(std::cerr, " ... detected FuMa 3D B-Format");
                chanmap[0] = SF_CHANNEL_MAP_AMBISONIC_B_W;
                chanmap[1] = SF_CHANNEL_MAP_AMBISONIC_B_X;
                chanmap[2] = SF_CHANNEL_MAP_AMBISONIC_B_Y;
                chanmap[3] = SF_CHANNEL_MAP_AMBISONIC_B_Z;
                break;

            case 3:
                fmt::println(std::cerr, " ... detected FuMa 2D B-Format");
                chanmap[0] = SF_CHANNEL_MAP_AMBISONIC_B_W;
                chanmap[1] = SF_CHANNEL_MAP_AMBISONIC_B_X;
                chanmap[2] = SF_CHANNEL_MAP_AMBISONIC_B_Y;
                break;

            default:
                fmt::println(std::cerr, " ... unhandled {}-channel B-Format", inchannels);
                return;
            }
        }
        else switch(inchannels)
        {
        case 1:
            fmt::println(std::cerr, " ... assuming front-center");
            spkrs = MonoMap;
            chanmap[0] = SF_CHANNEL_MAP_CENTER;
            break;

        case 2:
            fmt::println(std::cerr, " ... assuming WFX order stereo");
            spkrs = StereoMap;
            chanmap[0] = SF_CHANNEL_MAP_LEFT;
            chanmap[1] = SF_CHANNEL_MAP_RIGHT;
            break;

        case 6:
            fmt::println(std::cerr, " ... assuming WFX order 5.1");
            spkrs = X51Map;
            chanmap[0] = SF_CHANNEL_MAP_LEFT;
            chanmap[1] = SF_CHANNEL_MAP_RIGHT;
            chanmap[2] = SF_CHANNEL_MAP_CENTER;
            chanmap[3] = SF_CHANNEL_MAP_LFE;
            chanmap[4] = SF_CHANNEL_MAP_SIDE_LEFT;
            chanmap[5] = SF_CHANNEL_MAP_SIDE_RIGHT;
            break;

        case 8:
            fmt::println(std::cerr, " ... assuming WFX order 7.1");
            spkrs = X71Map;
            chanmap[0] = SF_CHANNEL_MAP_LEFT;
            chanmap[1] = SF_CHANNEL_MAP_RIGHT;
            chanmap[2] = SF_CHANNEL_MAP_CENTER;
            chanmap[3] = SF_CHANNEL_MAP_LFE;
            chanmap[4] = SF_CHANNEL_MAP_REAR_LEFT;
            chanmap[5] = SF_CHANNEL_MAP_REAR_RIGHT;
            chanmap[6] = SF_CHANNEL_MAP_SIDE_LEFT;
            chanmap[7] = SF_CHANNEL_MAP_SIDE_RIGHT;
            break;

        default:
            fmt::println(std::cerr, " ... unmapped {}-channel audio not supported", inchannels);
            return;
        }

        auto outinfo = SF_INFO{};
        outinfo.frames = ininfo.frames;
        outinfo.samplerate = ininfo.samplerate;
        outinfo.channels = gsl::narrow<int>(uhjchans);
        outinfo.format = SF_FORMAT_PCM_24 | SF_FORMAT_FLAC;
        auto outfile = SndFilePtr{sf_open(
            /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
            reinterpret_cast<const char*>(outname.u8string().c_str()), SFM_WRITE, &outinfo)};
        if(!outfile)
        {
            fmt::println(std::cerr, " ... failed to create {}", outname);
            return;
        }

        auto encoder = std::make_unique<UhjEncoder>();
        auto splbuf = al::vector<FloatBufferLine, 16>(9);
        auto ambmem = std::span{splbuf}.subspan<0,4>();
        auto encmem = std::span{splbuf}.subspan<4,4>();
        auto srcmem = std::span{splbuf[8]};
        auto membuf = al::vector<float,16>((size_t{inchannels}+uhjchans) * BufferLineSize);
        auto outmem = std::span{membuf}.first(size_t{BufferLineSize}*uhjchans);
        auto inmem = std::span{membuf}.last(size_t{BufferLineSize} * inchannels);

        /* A number of initial samples need to be skipped to cut the lead-in
         * from the all-pass filter delay. The same number of samples need to
         * be fed through the encoder after reaching the end of the input file
         * to ensure none of the original input is lost.
         */
        auto total_wrote = 0_uz;
        auto clipped_samples = 0_uz;
        auto LeadIn = size_t{UhjEncoder::sFilterDelay};
        auto LeadOut = size_t{UhjEncoder::sFilterDelay};
        while(LeadIn > 0 || LeadOut > 0)
        {
            auto got = al::saturate_cast<size_t>(sf_readf_float(infile.get(), inmem.data(),
                BufferLineSize));
            if(got < BufferLineSize)
            {
                const auto remaining = std::min(BufferLineSize - got, LeadOut);
                std::ranges::fill(inmem | std::views::drop(got*inchannels), 0.0f);
                got += remaining;
                LeadOut -= remaining;
            }

            std::ranges::fill(ambmem | std::views::join, 0.0f);

            if(spkrs.empty())
            {
                /* B-Format is already in the correct order. It just needs a
                 * +3dB boost.
                 */
                static constexpr auto scale = std::numbers::sqrt2_v<float>;
                const auto chans = size_t{std::min(inchannels, 4u)};
                for(const auto c : std::views::iota(0_uz, chans))
                {
                    for(const auto i : std::views::iota(0_uz, got))
                        ambmem[c][i] = inmem[i*inchannels + c]*scale;
                }
            }
            else for(const auto idx : std::views::iota(0_uz, chanmap.size()))
            {
                const auto chanid = chanmap[idx];
                /* Skip LFE. Or mix directly into W? Or W+X? */
                if(chanid == SF_CHANNEL_MAP_LFE)
                    continue;

                const auto spkr = std::ranges::find(spkrs, chanid, &SpeakerPos::mChannelID);
                if(spkr == spkrs.end())
                {
                    fmt::println(std::cerr, " ... failed to find channel ID {}", chanid);
                    continue;
                }

                for(const auto i : std::views::iota(0_uz, got))
                    srcmem[i] = inmem[i*inchannels + idx];

                static constexpr auto Deg2Rad = std::numbers::pi / 180.0;
                const auto coeffs = GenCoeffs(
                    std::cos(spkr->mAzimuth*Deg2Rad) * std::cos(spkr->mElevation*Deg2Rad),
                    std::sin(spkr->mAzimuth*Deg2Rad) * std::cos(spkr->mElevation*Deg2Rad),
                    std::sin(spkr->mElevation*Deg2Rad));
                std::ignore = std::ranges::mismatch(ambmem, coeffs,
                    [srcmem,got](const FloatBufferSpan output, const float gain)
                {
                    std::ranges::transform(srcmem.first(got), output, output.begin(),
                        [gain](const float s, const float o) noexcept { return s*gain + o; });
                    return true;
                });
            }

            encoder->encode(encmem.first(uhjchans), ambmem, got);
            if(LeadIn >= got)
            {
                LeadIn -= got;
                continue;
            }

            got -= LeadIn;
            for(const auto c : std::views::iota(0_uz, uhjchans))
            {
                static constexpr auto max_val = 8388607.0f / 8388608.0f;
                for(const auto i : std::views::iota(0_uz, got))
                {
                    const auto sample = std::clamp(encmem[c][LeadIn+i], -1.0f, max_val);
                    clipped_samples += sample != encmem[c][LeadIn+i];
                    outmem[i*uhjchans + c] = sample;
                }
            }
            LeadIn = 0;

            const auto wrote = sf_writef_float(outfile.get(), outmem.data(),
                gsl::narrow<sf_count_t>(got));
            if(wrote < 0)
                fmt::println(std::cerr, " ... failed to write samples: {}",
                    sf_error(outfile.get()));
            else
                total_wrote += gsl::narrow<size_t>(wrote);
        }
        fmt::println(" ... wrote {} samples ({} total, {} clipped).", total_wrote, ininfo.frames,
            clipped_samples);
        ++num_encoded;
    });

    if(num_encoded == 0)
        fmt::println(std::cerr, "Failed to encode any input files");
    else if(num_encoded < num_files)
        fmt::println(std::cerr, "Encoded {} of {} files", num_encoded, num_files);
    else
        fmt::println("Encoded {}{} file{}", (num_encoded > 1) ? "all " : "", num_encoded,
            (num_encoded == 1) ? "" : "s");
    return 0;
}

} /* namespace */

auto main(int argc, char **argv) -> int
{
    auto args = std::vector<std::string_view>(gsl::narrow<unsigned>(argc));
    std::ranges::copy(std::views::counted(argv, argc), args.begin());
    return main(std::span{args});
}
