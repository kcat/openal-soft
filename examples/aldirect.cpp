/*
 * OpenAL Direct Context Example
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

/* This file contains an example for playing a sound buffer with the Direct API
 * extension.
 */

#include "config.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <iostream>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "sndfile.h"

#include "common/alhelpers.h"
#include "fmt/base.h"
#include "fmt/ostream.h"

#include "win_main_utf8.h"

#if HAVE_CXXMODULES
import alsoft.gsl;
import openal;

#else

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "gsl/gsl"
#endif

namespace {

/* On Windows when using Creative's router, we need to override the ALC
 * functions and access the driver functions directly. This isn't needed when
 * not using the router, or on other OSs.
 */
LPALCOPENDEVICE p_alcOpenDevice{alcOpenDevice};
LPALCCLOSEDEVICE p_alcCloseDevice{alcCloseDevice};
LPALCISEXTENSIONPRESENT p_alcIsExtensionPresent{alcIsExtensionPresent};
LPALCCREATECONTEXT p_alcCreateContext{alcCreateContext};
LPALCDESTROYCONTEXT p_alcDestroyContext{alcDestroyContext};
LPALCGETPROCADDRESS p_alcGetProcAddress{alcGetProcAddress};


LPALGETSTRINGDIRECT alGetStringDirect{};
LPALGETERRORDIRECT alGetErrorDirect{};
LPALISEXTENSIONPRESENTDIRECT alIsExtensionPresentDirect{};

LPALGENBUFFERSDIRECT alGenBuffersDirect{};
LPALDELETEBUFFERSDIRECT alDeleteBuffersDirect{};
LPALISBUFFERDIRECT alIsBufferDirect{};
LPALBUFFERIDIRECT alBufferiDirect{};
LPALBUFFERDATADIRECT alBufferDataDirect{};

LPALGENSOURCESDIRECT alGenSourcesDirect{};
LPALDELETESOURCESDIRECT alDeleteSourcesDirect{};
LPALSOURCEIDIRECT alSourceiDirect{};
LPALGETSOURCEIDIRECT alGetSourceiDirect{};
LPALGETSOURCEFDIRECT alGetSourcefDirect{};
LPALSOURCEPLAYDIRECT alSourcePlayDirect{};


using SndFilePtr = std::unique_ptr<SNDFILE, decltype([](SNDFILE *sndfile) { sf_close(sndfile); })>;

/* LoadBuffer loads the named audio file into an OpenAL buffer object, and
 * returns the new buffer ID.
 */
auto LoadSound(ALCcontext *context, const std::string_view filename) -> ALuint
{
    /* Open the audio file and check that it's usable. */
    auto sfinfo = SF_INFO{};
    auto sndfile = SndFilePtr{sf_open(std::string{filename}.c_str(), SFM_READ, &sfinfo)};
    if(!sndfile)
    {
        fmt::println(std::cerr, "Could not open audio in {}: {}", filename,
            sf_strerror(sndfile.get()));
        return 0u;
    }
    if(sfinfo.frames < 1)
    {
        fmt::println(std::cerr, "Bad sample count in {} ({})", filename, sfinfo.frames);
        return 0u;
    }

    /* Detect a suitable format to load. Formats like Vorbis and Opus use float
     * natively, so load as float to avoid clipping when possible. Formats
     * larger than 16-bit can also use float to preserve a bit more precision.
     */
    enum class FormatType {
        Int16, Float, IMA4, MSADPCM
    };
    auto sample_format = FormatType::Int16;
    switch((sfinfo.format&SF_FORMAT_SUBMASK))
    {
    case SF_FORMAT_PCM_24:
    case SF_FORMAT_PCM_32:
    case SF_FORMAT_FLOAT:
    case SF_FORMAT_DOUBLE:
    case SF_FORMAT_VORBIS:
    case SF_FORMAT_OPUS:
    case SF_FORMAT_ALAC_20:
    case SF_FORMAT_ALAC_24:
    case SF_FORMAT_ALAC_32:
    case 0x0080/*SF_FORMAT_MPEG_LAYER_I*/:
    case 0x0081/*SF_FORMAT_MPEG_LAYER_II*/:
    case 0x0082/*SF_FORMAT_MPEG_LAYER_III*/:
        if(alIsExtensionPresentDirect(context, "AL_EXT_FLOAT32"))
            sample_format = FormatType::Float;
        break;
    case SF_FORMAT_IMA_ADPCM:
        /* ADPCM formats require setting a block alignment as specified in the
         * file, which needs to be read from the wave 'fmt ' chunk manually
         * since libsndfile doesn't provide it in a format-agnostic way.
         */
        if(sfinfo.channels <= 2 && (sfinfo.format&SF_FORMAT_TYPEMASK) == SF_FORMAT_WAV
            && alIsExtensionPresentDirect(context, "AL_EXT_IMA4")
            && alIsExtensionPresentDirect(context, "AL_SOFT_block_alignment"))
            sample_format = FormatType::IMA4;
        break;
    case SF_FORMAT_MS_ADPCM:
        if(sfinfo.channels <= 2 && (sfinfo.format&SF_FORMAT_TYPEMASK) == SF_FORMAT_WAV
            && alIsExtensionPresentDirect(context, "AL_SOFT_MSADPCM")
            && alIsExtensionPresentDirect(context, "AL_SOFT_block_alignment"))
            sample_format = FormatType::MSADPCM;
        break;
    }

    auto byteblockalign = 0;
    auto splblockalign = 0;
    if(sample_format == FormatType::IMA4 || sample_format == FormatType::MSADPCM)
    {
        /* For ADPCM, lookup the wave file's "fmt " chunk, which is a
         * WAVEFORMATEX-based structure for the audio format.
         */
        auto inf = SF_CHUNK_INFO{.id="fmt ", .id_size=4, .datalen=0, .data=nullptr};
        auto *iter = sf_get_chunk_iterator(sndfile.get(), &inf);

        /* If there's an issue getting the chunk or block alignment, load as
         * 16-bit and have libsndfile do the conversion.
         */
        if(!iter || sf_get_chunk_size(iter, &inf) != SF_ERR_NO_ERROR || inf.datalen < 14)
            sample_format = FormatType::Int16;
        else
        {
            auto fmtbuf = std::vector<ALubyte>(inf.datalen, ALubyte{0});
            inf.data = fmtbuf.data();
            if(sf_get_chunk_data(iter, &inf) != SF_ERR_NO_ERROR)
                sample_format = FormatType::Int16;
            else
            {
                /* Read the nBlockAlign field, and convert from bytes- to
                 * samples-per-block (verifying it's valid by converting back
                 * and comparing to the original value).
                 */
                byteblockalign = fmtbuf[12] | (fmtbuf[13]<<8);
                if(sample_format == FormatType::IMA4)
                {
                    splblockalign = (byteblockalign/sfinfo.channels - 4)/4*8 + 1;
                    if(splblockalign < 1
                        || ((splblockalign-1)/2 + 4)*sfinfo.channels != byteblockalign)
                        sample_format = FormatType::Int16;
                }
                else if(sample_format == FormatType::MSADPCM)
                {
                    splblockalign = (byteblockalign/sfinfo.channels - 7)*2 + 2;
                    if(splblockalign < 2
                        || ((splblockalign-2)/2 + 7)*sfinfo.channels != byteblockalign)
                        sample_format = FormatType::Int16;
                }
                else
                    sample_format = FormatType::Int16;
            }
        }
    }

    if(sample_format == FormatType::Int16)
    {
        splblockalign = 1;
        byteblockalign = sfinfo.channels * 2;
    }
    else if(sample_format == FormatType::Float)
    {
        splblockalign = 1;
        byteblockalign = sfinfo.channels * 4;
    }

    /* Figure out the OpenAL format from the file and desired sample type. */
    auto format = ALenum{AL_NONE};
    if(sfinfo.channels == 1)
    {
        if(sample_format == FormatType::Int16)
            format = AL_FORMAT_MONO16;
        else if(sample_format == FormatType::Float)
            format = AL_FORMAT_MONO_FLOAT32;
        else if(sample_format == FormatType::IMA4)
            format = AL_FORMAT_MONO_IMA4;
        else if(sample_format == FormatType::MSADPCM)
            format = AL_FORMAT_MONO_MSADPCM_SOFT;
    }
    else if(sfinfo.channels == 2)
    {
        if(sample_format == FormatType::Int16)
            format = AL_FORMAT_STEREO16;
        else if(sample_format == FormatType::Float)
            format = AL_FORMAT_STEREO_FLOAT32;
        else if(sample_format == FormatType::IMA4)
            format = AL_FORMAT_STEREO_IMA4;
        else if(sample_format == FormatType::MSADPCM)
            format = AL_FORMAT_STEREO_MSADPCM_SOFT;
    }
    else if(sfinfo.channels == 3)
    {
        if(sf_command(sndfile.get(), SFC_WAVEX_GET_AMBISONIC, nullptr, 0) == SF_AMBISONIC_B_FORMAT)
        {
            if(sample_format == FormatType::Int16)
                format = AL_FORMAT_BFORMAT2D_16;
            else if(sample_format == FormatType::Float)
                format = AL_FORMAT_BFORMAT2D_FLOAT32;
        }
    }
    else if(sfinfo.channels == 4)
    {
        if(sf_command(sndfile.get(), SFC_WAVEX_GET_AMBISONIC, nullptr, 0) == SF_AMBISONIC_B_FORMAT)
        {
            if(sample_format == FormatType::Int16)
                format = AL_FORMAT_BFORMAT3D_16;
            else if(sample_format == FormatType::Float)
                format = AL_FORMAT_BFORMAT3D_FLOAT32;
        }
    }
    if(!format)
    {
        fmt::println(std::cerr, "Unsupported channel count: {}", sfinfo.channels);
        return 0u;
    }

    if(sfinfo.frames/splblockalign > sf_count_t{std::numeric_limits<int>::max()}/byteblockalign)
    {
        fmt::println(std::cerr, "Too many sample frames in {} ({})", filename, sfinfo.frames);
        return 0u;
    }

    /* Decode the whole audio file to a buffer. */
    auto memstore = std::variant<std::vector<short>,std::vector<float>,std::vector<std::byte>>{};
    auto membuf = std::span<std::byte>{};

    if(sample_format == FormatType::Int16)
    {
        auto &vec = memstore.emplace<std::vector<short>>(gsl::narrow<size_t>(sfinfo.frames
            / splblockalign * sfinfo.channels));
        const auto num_frames = sf_readf_short(sndfile.get(), vec.data(), sfinfo.frames);
        if(num_frames > 0)
        {
            const auto num_samples = gsl::narrow<size_t>(num_frames * sfinfo.channels);
            membuf = std::as_writable_bytes(std::span{vec}.first(num_samples));
        }
    }
    else if(sample_format == FormatType::Float)
    {
        auto &vec = memstore.emplace<std::vector<float>>(gsl::narrow<size_t>(sfinfo.frames
            / splblockalign * sfinfo.channels));
        const auto num_frames = sf_readf_float(sndfile.get(), vec.data(), sfinfo.frames);
        if(num_frames > 0)
        {
            const auto num_samples = gsl::narrow<size_t>(num_frames * sfinfo.channels);
            membuf = std::as_writable_bytes(std::span{vec}.first(num_samples));
        }
    }
    else
    {
        const auto count = sfinfo.frames / splblockalign * byteblockalign;
        auto &vec = memstore.emplace<std::vector<std::byte>>(gsl::narrow<size_t>(count));
        const auto num_bytes = sf_read_raw(sndfile.get(), membuf.data(), count);
        if(num_bytes > 0)
            membuf = std::as_writable_bytes(std::span{vec}.first(gsl::narrow<size_t>(num_bytes)));
    }
    if(membuf.empty())
    {
        fmt::println(std::cerr, "Failed to read samples in {}", filename);
        return 0u;
    }

    fmt::println("Loading: {} ({}, {}hz)", filename, FormatName(format), sfinfo.samplerate);

    auto buffer = ALuint{};
    alGenBuffersDirect(context, 1, &buffer);
    if(splblockalign > 1)
        alBufferiDirect(context, buffer, AL_UNPACK_BLOCK_ALIGNMENT_SOFT, splblockalign);
    alBufferDataDirect(context, buffer, format, membuf.data(), gsl::narrow<ALsizei>(membuf.size()),
        sfinfo.samplerate);

    /* Check if an error occurred, and clean up if so. */
    if(const auto err = alGetErrorDirect(context); err != AL_NO_ERROR)
    {
        fmt::println(std::cerr, "OpenAL Error: {}", alGetStringDirect(context, err));
        if(buffer && alIsBufferDirect(context, buffer))
            alDeleteBuffersDirect(context, 1, &buffer);
        return 0u;
    }

    return buffer;
}


auto main(std::span<std::string_view> args) -> int
{
    /* Print out usage if no arguments were specified */
    if(args.size() < 2)
    {
        fmt::println(std::cerr, "Usage: {} [-device <name>] <filename>", args[0]);
        return 1;
    }

    /* Initialize OpenAL. */
    args = args.subspan(1);

    ALCdevice *device{};
    if(args.size() > 1 && args[0] == "-device")
    {
        device = p_alcOpenDevice(std::string{args[1]}.c_str());
        if(!device)
            fmt::println(std::cerr, "Failed to open \"{}\", trying default", args[1]);
        args = args.subspan(2);
    }
    if(!device)
        device = p_alcOpenDevice(nullptr);
    if(!device)
    {
        fmt::println(std::cerr, "Could not open a device!");
        return 1;
    }

    if(!p_alcIsExtensionPresent(device, "ALC_EXT_direct_context"))
    {
        fmt::println(std::cerr, "ALC_EXT_direct_context not supported on device");
        p_alcCloseDevice(device);
        return 1;
    }

    /* On Windows with Creative's router, the device needs to be bootstrapped
     * to use it through the driver directly. Otherwise the Direct functions
     * aren't able to recognize the router's ALCcontexts. To handle this, we
     * use the router's alcOpenDevice, alcGetProcAddress, and alcCloseDevice
     * functions to open the device with the router, get the device driver's
     * alcGetProcAddress2 function, and close the device with the router. Then
     * call alcGetProcAddress2 with the null device handle to get the driver's
     * functions. Afterward, we can open the device back up using the driver
     * functions directly and continue on.
     *
     * Note that this will allow using other devices from the same driver just
     * fine, but switching to a device on another driver will require using the
     * original functions from the router (and require re-bootstrapping to use
     * that driver's functions, if applicable). If controlling multiple devices
     * with Direct functions from separate drivers simultaneously is desired, a
     * good strategy may be to associate the driver's ALC and Direct functions
     * with the ALCdevice and ALCcontext handles created from them.
     *
     * This is all unnecessary when not using Creative's router, including on
     * non-Windows OSs or when using OpenAL Soft's router, where the original
     * ALC functions can be used as normal.
     */
    {
        const auto devname = std::string{alcGetString(device, ALC_ALL_DEVICES_SPECIFIER)};
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
        auto p_alcGetProcAddress2 = reinterpret_cast<LPALCGETPROCADDRESS2>(
            p_alcGetProcAddress(device, "alcGetProcAddress2"));
        p_alcCloseDevice(device);

        /* Load the driver-specific ALC functions we'll be using. */
#define LOAD_PROC(N) p_##N = reinterpret_cast<decltype(p_##N)>(p_alcGetProcAddress2(nullptr, #N))
        /* NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast) */
        LOAD_PROC(alcOpenDevice);
        LOAD_PROC(alcCloseDevice);
        LOAD_PROC(alcIsExtensionPresent);
        LOAD_PROC(alcGetProcAddress);
        LOAD_PROC(alcCreateContext);
        LOAD_PROC(alcDestroyContext);
        LOAD_PROC(alcGetProcAddress);
        /* NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast) */
#undef LOAD_PROC
        device = gsl::make_not_null(p_alcOpenDevice(devname.c_str()));
    }

    /* Load the Direct API functions we're using. */
#define LOAD_PROC(N) N = reinterpret_cast<decltype(N)>(p_alcGetProcAddress(device, #N))
    /* NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast) */
    LOAD_PROC(alGetStringDirect);
    LOAD_PROC(alGetErrorDirect);
    LOAD_PROC(alIsExtensionPresentDirect);

    LOAD_PROC(alGenBuffersDirect);
    LOAD_PROC(alDeleteBuffersDirect);
    LOAD_PROC(alIsBufferDirect);
    LOAD_PROC(alBufferiDirect);
    LOAD_PROC(alBufferDataDirect);

    LOAD_PROC(alGenSourcesDirect);
    LOAD_PROC(alDeleteSourcesDirect);
    LOAD_PROC(alSourceiDirect);
    LOAD_PROC(alGetSourceiDirect);
    LOAD_PROC(alGetSourcefDirect);
    LOAD_PROC(alSourcePlayDirect);
    /* NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast) */
#undef LOAD_PROC

    /* Create the context. It doesn't need to be set as current to use with the
     * Direct API functions.
     */
    auto *context = p_alcCreateContext(device, nullptr);
    if(!context)
    {
        p_alcCloseDevice(device);
        fmt::println(std::cerr, "Could not create a context!");
        return 1;
    }

    /* Load the sound into a buffer. */
    const auto buffer = LoadSound(context, args[0]);
    if(!buffer)
    {
        p_alcDestroyContext(context);
        p_alcCloseDevice(device);
        return 1;
    }

    /* Create the source to play the sound with. */
    auto source = ALuint{0u};
    alGenSourcesDirect(context, 1, &source);
    alSourceiDirect(context, source, AL_BUFFER, static_cast<ALint>(buffer));
    if(alGetErrorDirect(context) != AL_NO_ERROR)
        throw std::runtime_error{"Failed to setup sound source"};

    /* Play the sound until it finishes. */
    alSourcePlayDirect(context, source);
    auto state = ALenum{};
    do {
        al_nssleep(10000000);
        alGetSourceiDirect(context, source, AL_SOURCE_STATE, &state);

        /* Get the source offset. */
        auto offset = ALfloat{};
        alGetSourcefDirect(context, source, AL_SEC_OFFSET, &offset);
        fmt::print("  \rOffset: {:.02f}", offset);
        std::cout.flush();
    } while(alGetErrorDirect(context) == AL_NO_ERROR && state == AL_PLAYING);
    fmt::println("");

    /* All done. Delete resources, and close down OpenAL. */
    alDeleteSourcesDirect(context, 1, &source);
    alDeleteBuffersDirect(context, 1, &buffer);

    p_alcDestroyContext(context);
    p_alcCloseDevice(device);

    return 0;
}

} // namespace

auto main(int argc, char **argv) -> int
{
    auto args = std::vector<std::string_view>(gsl::narrow<unsigned int>(argc));
    std::ranges::copy(std::views::counted(argv, argc), args.begin());
    return main(std::span{args});
}
