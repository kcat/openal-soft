/*
 * 2-channel UHJ Decoder
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
#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <numbers>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "alnumeric.h"
#include "alstring.h"
#include "filesystem.h"
#include "gsl/gsl"
#include "opthelpers.h"
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
#include "fmt/std.h"
#include "gsl/gsl"
#endif


namespace {

using namespace std::string_view_literals;

using SndFilePtr = std::unique_ptr<SNDFILE, decltype([](SNDFILE *sndfile) { sf_close(sndfile); })>;

using ubyte = unsigned char;
using ushort = unsigned short;
using uint = unsigned int;


constexpr auto SUBTYPE_BFORMAT_FLOAT = std::bit_cast<std::array<char,16>>(std::to_array<ubyte>({
    0x03, 0x00, 0x00, 0x00, 0x21, 0x07, 0xd3, 0x11, 0x86, 0x44, 0xc8, 0xc1,
    0xca, 0x00, 0x00, 0x00
}));

void fwrite16le(const ushort value, std::ostream &f)
{
    auto data = std::bit_cast<std::array<char,2>>(value);
    if constexpr(std::endian::native == std::endian::big)
        std::ranges::reverse(data);
    f.write(data.data(), std::ssize(data));
}

void fwrite32le(const uint value, std::ostream &f)
{
    auto data = std::bit_cast<std::array<char,4>>(value);
    if constexpr(std::endian::native == std::endian::big)
        std::ranges::reverse(data);
    f.write(data.data(), std::ssize(data));
}

auto f32AsLEBytes(const float value) -> std::array<char,4>
{
    auto ret = std::bit_cast<std::array<char,4>>(value);
    if constexpr(std::endian::native == std::endian::big)
        std::ranges::reverse(ret);
    return ret;
}


constexpr auto BufferLineSize = 1024u;

using FloatBufferLine = std::array<float,BufferLineSize>;


struct UhjDecoder {
    constexpr static auto sFilterDelay = 1024_uz;

    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mS{};
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mD{};
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mT{};
    alignas(16) std::array<float,BufferLineSize+sFilterDelay> mQ{};

    /* History for the FIR filter. */
    alignas(16) std::array<float,sFilterDelay-1> mDTHistory{};
    alignas(16) std::array<float,sFilterDelay-1> mSHistory{};

    alignas(16) std::array<float,BufferLineSize + sFilterDelay*2> mTemp{};

    void decode(const std::span<const float> InSamples, const std::size_t InChannels,
        const std::span<FloatBufferLine> OutSamples, const std::size_t SamplesToDo);
    void decode2(const std::span<const float> InSamples,
        const std::span<FloatBufferLine> OutSamples, const std::size_t SamplesToDo);
};

auto const PShift = PhaseShifterT<UhjDecoder::sFilterDelay*2>{};


/* Decoding UHJ is done as:
 *
 * S = Left + Right
 * D = Left - Right
 *
 * W = 0.981532*S + 0.197484*j(0.828331*D + 0.767820*T)
 * X = 0.418496*S - j(0.828331*D + 0.767820*T)
 * Y = 0.795968*D - 0.676392*T + j(0.186633*S)
 * Z = 1.023332*Q
 *
 * where j is a +90 degree phase shift. 3-channel UHJ excludes Q, while 2-
 * channel excludes Q and T. The B-Format signal reconstructed from 2-channel
 * UHJ should not be run through a normal B-Format decoder, as it needs
 * different shelf filters.
 *
 * NOTE: Some sources specify
 *
 * S = (Left + Right)/2
 * D = (Left - Right)/2
 *
 * However, this is incorrect. It's halving Left and Right even though they
 * were already halved during encoding, causing S and D to be half what they
 * initially were at the encoding stage. This division is not present in
 * Gerzon's original paper for deriving Sigma (S) or Delta (D) from the L and R
 * signals. As proof, taking Y for example:
 *
 * Y = 0.795968*D - 0.676392*T + j(0.186633*S)
 *
 * * Plug in the encoding parameters, using ? as a placeholder for whether S
 *   and D should receive an extra 0.5 factor
 * Y = 0.795968*(j(-0.3420201*W + 0.5098604*X) + 0.6554516*Y)*? -
 *     0.676392*(j(-0.1432*W + 0.6512*X) - 0.7071068*Y) +
 *     0.186633*j(0.9396926*W + 0.1855740*X)*?
 *
 * * Move common factors in
 * Y = (j(-0.3420201*0.795968*?*W + 0.5098604*0.795968*?*X) + 0.6554516*0.795968*?*Y) -
 *     (j(-0.1432*0.676392*W + 0.6512*0.676392*X) - 0.7071068*0.676392*Y) +
 *     j(0.9396926*0.186633*?*W + 0.1855740*0.186633*?*X)
 *
 * * Clean up extraneous groupings
 * Y = j(-0.3420201*0.795968*?*W + 0.5098604*0.795968*?*X) + 0.6554516*0.795968*?*Y -
 *     j(-0.1432*0.676392*W + 0.6512*0.676392*X) + 0.7071068*0.676392*Y +
 *     j*(0.9396926*0.186633*?*W + 0.1855740*0.186633*?*X)
 *
 * * Move phase shifts together and combine them
 * Y = j(-0.3420201*0.795968*?*W + 0.5098604*0.795968*?*X - -0.1432*0.676392*W -
 *        0.6512*0.676392*X + 0.9396926*0.186633*?*W + 0.1855740*0.186633*?*X) +
 *     0.6554516*0.795968*?*Y + 0.7071068*0.676392*Y
 *
 * * Reorder terms
 * Y = j(-0.3420201*0.795968*?*W +  0.1432*0.676392*W + 0.9396926*0.186633*?*W +
 *        0.5098604*0.795968*?*X + -0.6512*0.676392*X + 0.1855740*0.186633*?*X) +
 *     0.7071068*0.676392*Y + 0.6554516*0.795968*?*Y
 *
 * * Move common factors out
 * Y = j((-0.3420201*0.795968*? +  0.1432*0.676392 + 0.9396926*0.186633*?)*W +
 *       ( 0.5098604*0.795968*? + -0.6512*0.676392 + 0.1855740*0.186633*?)*X) +
 *     (0.7071068*0.676392 + 0.6554516*0.795968*?)*Y
 *
 * * Result w/ 0.5 factor:
 * -0.3420201*0.795968*0.5 +  0.1432*0.676392 + 0.9396926*0.186633*0.5 =  0.04843*W
 *  0.5098604*0.795968*0.5 + -0.6512*0.676392 + 0.1855740*0.186633*0.5 = -0.22023*X
 *  0.7071068*0.676392                        + 0.6554516*0.795968*0.5 =  0.73914*Y
 * -> Y = j(0.04843*W + -0.22023*X) + 0.73914*Y
 *
 * * Result w/o 0.5 factor:
 * -0.3420201*0.795968 +  0.1432*0.676392 + 0.9396926*0.186633 = 0.00000*W
 *  0.5098604*0.795968 + -0.6512*0.676392 + 0.1855740*0.186633 = 0.00000*X
 *  0.7071068*0.676392                    + 0.6554516*0.795968 = 1.00000*Y
 * -> Y = j(0.00000*W + 0.00000*X) + 1.00000*Y
 *
 * Not halving produces a result matching the original input.
 */
void UhjDecoder::decode(const std::span<const float> InSamples, const std::size_t InChannels,
    const std::span<FloatBufferLine> OutSamples, const std::size_t SamplesToDo)
{
    ASSUME(SamplesToDo > 0);

    auto woutput = std::span{OutSamples[0]};
    auto xoutput = std::span{OutSamples[1]};
    auto youtput = std::span{OutSamples[2]};

    /* Add a delay to the input channels, to align it with the all-passed
     * signal.
     */

    /* S = Left + Right */
    for(auto i = 0_uz;i < SamplesToDo;++i)
        mS[sFilterDelay+i] = InSamples[i*InChannels + 0] + InSamples[i*InChannels + 1];

    /* D = Left - Right */
    for(auto i = 0_uz;i < SamplesToDo;++i)
        mD[sFilterDelay+i] = InSamples[i*InChannels + 0] - InSamples[i*InChannels + 1];

    if(InChannels > 2)
    {
        /* T */
        for(auto i = 0_uz;i < SamplesToDo;++i)
            mT[sFilterDelay+i] = InSamples[i*InChannels + 2];
    }
    if(InChannels > 3)
    {
        /* Q */
        for(auto i = 0_uz;i < SamplesToDo;++i)
            mQ[sFilterDelay+i] = InSamples[i*InChannels + 3];
    }

    /* Precompute j(0.828331*D + 0.767820*T) and store in xoutput. */
    auto tmpiter = std::ranges::copy(mDTHistory, mTemp.begin()).out;
    std::ranges::transform(mD | std::views::take(SamplesToDo+sFilterDelay), mT, tmpiter,
        [](const float d, const float t) noexcept { return 0.828331f*d + 0.767820f*t; });
    std::ranges::copy(mTemp | std::views::drop(SamplesToDo) | std::views::take(mDTHistory.size()),
        mDTHistory.begin());
    PShift.process(xoutput.first(SamplesToDo), mTemp);

    /* W = 0.981532*S + 0.197484*j(0.828331*D + 0.767820*T) */
    std::ranges::transform(mS | std::views::take(SamplesToDo), xoutput, woutput.begin(),
        [](const float s, const float jdt) -> float { return 0.981532f*s + 0.197484f*jdt; });

    /* X = 0.418496*S - j(0.828331*D + 0.767820*T) */
    std::ranges::transform(mS | std::views::take(SamplesToDo), xoutput, xoutput.begin(),
        [](const float s, const float jdt) -> float { return 0.418496f*s - jdt; });

    /* Precompute j*S and store in youtput. */
    tmpiter = std::ranges::copy(mSHistory, mTemp.begin()).out;
    std::ranges::copy(mS | std::views::take(SamplesToDo+sFilterDelay), tmpiter);
    std::ranges::copy(mTemp | std::views::drop(SamplesToDo) | std::views::take(mSHistory.size()),
        mSHistory.begin());
    PShift.process(youtput.first(SamplesToDo), mTemp);

    for(auto i = 0_uz;i < SamplesToDo;++i)
    {
        /* Y = 0.795968*D - 0.676392*T + j(0.186633*S) */
        youtput[i] = 0.795968f*mD[i] - 0.676392f*mT[i] + 0.186633f*youtput[i];
    }

    if(OutSamples.size() > 3)
    {
        const auto zoutput = std::span{OutSamples[3]};
        /* Z = 1.023332*Q */
        std::ranges::transform(mQ | std::views::take(SamplesToDo), zoutput.begin(),
            [](const float q) noexcept -> float { return 1.023332f*q; });
    }

    const auto get_end = std::views::drop(SamplesToDo) | std::views::take(sFilterDelay);
    std::ranges::copy(mS | get_end, mS.begin());
    std::ranges::copy(mD | get_end, mD.begin());
    std::ranges::copy(mT | get_end, mT.begin());
    std::ranges::copy(mQ | get_end, mQ.begin());
}

/* This is an alternative equation for decoding 2-channel UHJ. Not sure what
 * the intended benefit is over the above equation as this slightly reduces the
 * amount of the original left response and has more of the phase-shifted
 * forward response on the left response.
 *
 * This decoding is done as:
 *
 * S = Left + Right
 * D = Left - Right
 *
 * W = 0.981530*S + j*0.163585*D
 * X = 0.418504*S - j*0.828347*D
 * Y = 0.762956*D + j*0.384230*S
 *
 * where j is a +90 degree phase shift.
 *
 * NOTE: As above, S and D should not be halved. The only consequence of
 * halving here is merely a -6dB reduction in output, but it's still incorrect.
 */
void UhjDecoder::decode2(const std::span<const float> InSamples,
    const std::span<FloatBufferLine> OutSamples, const std::size_t SamplesToDo)
{
    ASSUME(SamplesToDo > 0);

    auto woutput = std::span{OutSamples[0]};
    auto xoutput = std::span{OutSamples[1]};
    auto youtput = std::span{OutSamples[2]};

    /* S = Left + Right */
    for(auto i = 0_uz;i < SamplesToDo;++i)
        mS[sFilterDelay+i] = InSamples[i*2 + 0] + InSamples[i*2 + 1];

    /* D = Left - Right */
    for(auto i = 0_uz;i < SamplesToDo;++i)
        mD[sFilterDelay+i] = InSamples[i*2 + 0] - InSamples[i*2 + 1];

    /* Precompute j*D and store in xoutput. */
    auto tmpiter = std::ranges::copy(mDTHistory, mTemp.begin()).out;
    std::ranges::copy(mD | std::views::take(SamplesToDo+sFilterDelay), tmpiter);
    std::ranges::copy(mTemp | std::views::drop(SamplesToDo) | std::views::take(mDTHistory.size()),
        mDTHistory.begin());
    PShift.process(xoutput.first(SamplesToDo), mTemp);

    /* W = 0.981530*S + j*0.163585*D */
    std::ranges::transform(mS | std::views::take(SamplesToDo), xoutput, woutput.begin(),
        [](const float s, const float jd) -> float { return 0.981530f*s + 0.163585f*jd; });

    /* X = 0.418504*S - j*0.828347*D */
    std::ranges::transform(mS | std::views::take(SamplesToDo), xoutput, xoutput.begin(),
        [](const float s, const float jd) -> float { return 0.418504f*s - 0.828347f*jd; });

    /* Precompute j*S and store in youtput. */
    tmpiter = std::ranges::copy(mSHistory, mTemp.begin()).out;
    std::ranges::copy(mS | std::views::take(SamplesToDo+sFilterDelay), tmpiter);
    std::ranges::copy(mTemp | std::views::drop(SamplesToDo) | std::views::take(mSHistory.size()),
        mSHistory.begin());
    PShift.process(youtput.first(SamplesToDo), mTemp);

    /* Y = 0.762956*D + j*0.384230*S */
    std::ranges::transform(mD | std::views::take(SamplesToDo), youtput, youtput.begin(),
        [](const float d, const float js) -> float { return 0.762956f*d + 0.384230f*js; });

    const auto get_end = std::views::drop(SamplesToDo) | std::views::take(sFilterDelay);
    std::ranges::copy(mS | get_end, mS.begin());
    std::ranges::copy(mD | get_end, mD.begin());
}


auto main(std::span<std::string_view> args) -> int
{
    if(args.size() < 2 || args[1] == "-h" || args[1] == "--help")
    {
        fmt::println("Usage: {} <[options] filename.wav...>\n\n"
            "  Options:\n"
            "    --general      Use the general equations for 2-channel UHJ (default).\n"
            "    --alternative  Use the alternative equations for 2-channel UHJ.\n"
            "\n"
            "Note: When decoding 2-channel UHJ to an .amb file, the result should not use\n"
            "the normal B-Format shelf filters! Only 3- and 4-channel UHJ can accurately\n"
            "reconstruct the original B-Format signal.",
            args[0]);
        return 1;
    }
    args = args.subspan(1);

    auto num_files = 0_uz;
    auto num_decoded = 0_uz;
    auto use_general = true;
    std::ranges::for_each(args, [&num_files,&num_decoded,&use_general](const std::string_view arg)
    {
        if(arg == "--general"sv)
        {
            use_general = true;
            return;
        }
        if(arg == "--alternative"sv)
        {
            use_general = false;
            return;
        }
        ++num_files;

        auto ininfo = SF_INFO{};
        auto infile = SndFilePtr{sf_open(std::string{arg}.c_str(), SFM_READ, &ininfo)};
        if(!infile)
        {
            fmt::println(std::cerr, "Failed to open {}", arg);
            return;
        }
        if(sf_command(infile.get(), SFC_WAVEX_GET_AMBISONIC, nullptr, 0) == SF_AMBISONIC_B_FORMAT)
        {
            fmt::println(std::cerr, "{} is already B-Format", arg);
            return;
        }

        const auto inchannels = gsl::narrow<uint>(ininfo.channels);
        auto outchans = uint{};
        if(inchannels == 2)
            outchans = 3;
        else if(inchannels == 3 || inchannels == 4)
            outchans = inchannels;
        else
        {
            fmt::println(std::cerr, "{} is not a 2-, 3-, or 4-channel file", arg);
            return;
        }
        fmt::println("Converting {} from {}-channel UHJ{}...", arg, inchannels,
            (inchannels == 2) ? use_general ? " (general)" : " (alternative)" : "");

        auto outname = fs::path(al::char_as_u8(arg)).stem().replace_extension(u8".amb");
        auto outfile = std::ofstream{outname, std::ios_base::binary};
        if(!outfile.is_open())
        {
            fmt::println(std::cerr, "Failed to create {}", outname);
            return;
        }

        outfile.write("RIFF", 4);
        fwrite32le(0xFFFFFFFF, outfile); // 'RIFF' header len; filled in at close
        outfile.write("WAVE", 4);

        outfile.write("fmt ", 4);
        fwrite32le(40, outfile); // 'fmt ' header len; 40 bytes for EXTENSIBLE

        // 16-bit val, format type id (extensible: 0xFFFE)
        fwrite16le(0xFFFE, outfile);
        // 16-bit val, channel count
        fwrite16le(gsl::narrow<ushort>(outchans), outfile);
        // 32-bit val, frequency
        fwrite32le(gsl::narrow<uint>(ininfo.samplerate), outfile);
        // 32-bit val, bytes per second
        fwrite32le(gsl::narrow<uint>(ininfo.samplerate)*outchans*uint{sizeof(float)}, outfile);
        // 16-bit val, frame size
        fwrite16le(gsl::narrow<ushort>(sizeof(float)*outchans), outfile);
        // 16-bit val, bits per sample
        fwrite16le(gsl::narrow<ushort>(sizeof(float)*8), outfile);
        // 16-bit val, extra byte count
        fwrite16le(22, outfile);
        // 16-bit val, valid bits per sample
        fwrite16le(gsl::narrow<ushort>(sizeof(float)*8), outfile);
        // 32-bit val, channel mask
        fwrite32le(0, outfile);
        // 16 byte GUID, sub-type format
        outfile.write(SUBTYPE_BFORMAT_FLOAT.data(), std::ssize(SUBTYPE_BFORMAT_FLOAT));

        outfile.write("data", 4);
        fwrite32le(0xFFFFFFFF, outfile); // 'data' header len; filled in at close
        if(!outfile)
        {
            fmt::println(std::cerr, "Error writing wave file header: {} ({})",
                std::generic_category().message(errno), errno);
            return;
        }

        const auto DataStart = std::streamoff{outfile.tellp()};

        auto decoder = std::make_unique<UhjDecoder>();
        auto inmem = std::vector<float>(size_t{BufferLineSize} * inchannels);
        auto decmem = al::vector<std::array<float,BufferLineSize>, 16>(outchans);
        auto outmem = std::vector<char>(size_t{BufferLineSize}*outchans*sizeof(float));

        /* A number of initial samples need to be skipped to cut the lead-in
         * from the all-pass filter delay. The same number of samples need to
         * be fed through the decoder after reaching the end of the input file
         * to ensure none of the original input is lost.
         */
        auto LeadIn = size_t{UhjDecoder::sFilterDelay};
        auto LeadOut = size_t{UhjDecoder::sFilterDelay};
        while(LeadOut > 0)
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

            if(inchannels > 2 || use_general)
                decoder->decode(inmem, inchannels, decmem, got);
            else
                decoder->decode2(inmem, decmem, got);
            if(LeadIn >= got)
            {
                LeadIn -= got;
                continue;
            }

            got -= LeadIn;
            auto oiter = outmem.begin();
            for(auto i = 0_uz;i < got;++i)
            {
                /* Attenuate by -3dB for FuMa output levels. */
                static constexpr auto inv_sqrt2 = gsl::narrow_cast<float>(1.0/std::numbers::sqrt2);
                for(auto j = 0_uz;j < outchans;++j)
                    oiter = std::ranges::copy(f32AsLEBytes(decmem[j][LeadIn+i]*inv_sqrt2), oiter)
                        .out;
            }
            LeadIn = 0;

            if(!outfile.write(outmem.data(), std::distance(outmem.begin(), oiter)))
            {
                fmt::println(std::cerr, "Error writing wave data: {} ({})",
                    std::generic_category().message(errno), errno);
                break;
            }
        }

        if(const auto DataEnd = std::streamoff{outfile.tellp()}; DataEnd > DataStart)
        {
            const auto dataLen = DataEnd - DataStart;
            if(outfile.seekp(4))
                fwrite32le(gsl::narrow<uint>(DataEnd-8), outfile); // 'WAVE' header len
            if(outfile.seekp(DataStart-4))
                fwrite32le(gsl::narrow<uint>(dataLen), outfile); // 'data' header len
        }
        outfile.flush();
        ++num_decoded;
    });
    if(num_decoded == 0)
        fmt::println(std::cerr, "Failed to decode any input files");
    else if(num_decoded < num_files)
        fmt::println(std::cerr, "Decoded {} of {} files", num_decoded, num_files);
    else
        fmt::println("Decoded {} file{}", num_decoded, (num_decoded==1)?"":"s");
    return 0;
}

} /* namespace */

auto main(int argc, char **argv) -> int
{
    auto args = std::vector<std::string_view>(gsl::narrow<unsigned>(argc));
    std::ranges::copy(std::views::counted(argv, argc), args.begin());
    return main(std::span{args});
}
