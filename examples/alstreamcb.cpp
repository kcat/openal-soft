/*
 * OpenAL Callback-based Stream Example
 *
 * Copyright (c) 2020 by Chris Robinson <chris.kcat@gmail.com>
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

/* This file contains a streaming audio player using a callback buffer. */


#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include "sndfile.h"

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "alnumeric.h"
#include "common/alhelpers.h"
#include "common/alhelpers.hpp"
#include "fmt/core.h"

#include "win_main_utf8.h"


namespace {

using std::chrono::seconds;
using std::chrono::nanoseconds;

auto alBufferCallbackSOFT = LPALBUFFERCALLBACKSOFT{};

struct StreamPlayer {
    /* A buffer that can hold int16 or float samples, or raw (ADPCM) bytes. */
    std::variant<std::vector<short>, std::vector<float>, std::vector<std::byte>> mBufferVariant;

    /* A lockless ring-buffer (supports single-provider, single-consumer
     * operation), which aliases the above buffer.
     */
    std::span<std::byte> mBufferData;
    std::atomic<size_t> mReadPos{0_uz};
    std::atomic<size_t> mWritePos{0_uz};

    size_t mSamplesPerBlock{1_uz};
    size_t mBytesPerBlock{1_uz};

    /* The buffer to get the callback, and source to play with. */
    ALuint mBuffer{0u}, mSource{0u};
    size_t mStartOffset{0_uz};

    /* Handle for the audio file to decode. */
    SNDFILE *mSndfile{nullptr};
    SF_INFO mSfInfo{};
    size_t mDecoderOffset{0_uz};

    /* The format of the callback samples. */
    ALenum mFormat{};

    StreamPlayer()
    {
        alGenBuffers(1, &mBuffer);
        if(alGetError() != AL_NO_ERROR)
            throw std::runtime_error{"alGenBuffers failed"};
        alGenSources(1, &mSource);
        if(alGetError() != AL_NO_ERROR)
        {
            alDeleteBuffers(1, &mBuffer);
            throw std::runtime_error{"alGenSources failed"};
        }
    }
    ~StreamPlayer()
    {
        alDeleteSources(1, &mSource);
        alDeleteBuffers(1, &mBuffer);
        if(mSndfile)
            sf_close(mSndfile);
    }
    StreamPlayer(const StreamPlayer&) = delete;
    auto operator=(const StreamPlayer&) -> StreamPlayer& = delete;

    auto open(const std::string &filename) -> bool;
    void close();
    auto bufferCallback(const std::span<std::byte> output) noexcept -> ALsizei;
    auto prepare() -> bool;
    auto update() -> bool;
};

auto StreamPlayer::open(const std::string &filename) -> bool
{
    close();

    /* Open the file and figure out the OpenAL format. */
    mSndfile = sf_open(filename.c_str(), SFM_READ, &mSfInfo);
    if(!mSndfile)
    {
        fmt::println(stderr, "Could not open audio in {}: {}", filename,
            sf_strerror(mSndfile));
        return false;
    }

    enum class SampleType {
        Int16, Float, IMA4, MSADPCM
    };
    auto sampleFormat = SampleType{SampleType::Int16};
    switch((mSfInfo.format&SF_FORMAT_SUBMASK))
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
        if(alIsExtensionPresent("AL_EXT_FLOAT32"))
            sampleFormat = SampleType::Float;
        break;
    case SF_FORMAT_IMA_ADPCM:
        if(mSfInfo.channels <= 2 && (mSfInfo.format&SF_FORMAT_TYPEMASK) == SF_FORMAT_WAV
            && alIsExtensionPresent("AL_EXT_IMA4")
            && alIsExtensionPresent("AL_SOFT_block_alignment"))
            sampleFormat = SampleType::IMA4;
        break;
    case SF_FORMAT_MS_ADPCM:
        if(mSfInfo.channels <= 2 && (mSfInfo.format&SF_FORMAT_TYPEMASK) == SF_FORMAT_WAV
            && alIsExtensionPresent("AL_SOFT_MSADPCM")
            && alIsExtensionPresent("AL_SOFT_block_alignment"))
            sampleFormat = SampleType::MSADPCM;
        break;
    }

    auto splblocksize = int{};
    auto byteblocksize = int{};
    if(sampleFormat == SampleType::IMA4 || sampleFormat == SampleType::MSADPCM)
    {
        auto inf = SF_CHUNK_INFO{.id="fmt ", .id_size=4u, .datalen=0u, .data=nullptr};
        auto *iter = sf_get_chunk_iterator(mSndfile, &inf);
        if(!iter || sf_get_chunk_size(iter, &inf) != SF_ERR_NO_ERROR || inf.datalen < 14)
            sampleFormat = SampleType::Int16;
        else
        {
            auto fmtbuf = std::vector<ALubyte>(inf.datalen);
            inf.data = fmtbuf.data();
            if(sf_get_chunk_data(iter, &inf) != SF_ERR_NO_ERROR)
                sampleFormat = SampleType::Int16;
            else
            {
                byteblocksize = fmtbuf[12] | (fmtbuf[13]<<8u);
                if(sampleFormat == SampleType::IMA4)
                {
                    splblocksize = (byteblocksize/mSfInfo.channels - 4)/4*8 + 1;
                    if(splblocksize < 1
                        || ((splblocksize-1)/2 + 4)*mSfInfo.channels != byteblocksize)
                        sampleFormat = SampleType::Int16;
                }
                else
                {
                    splblocksize = (byteblocksize/mSfInfo.channels - 7)*2 + 2;
                    if(splblocksize < 2
                        || ((splblocksize-2)/2 + 7)*mSfInfo.channels != byteblocksize)
                        sampleFormat = SampleType::Int16;
                }
            }
        }
    }

    switch(sampleFormat)
    {
    case SampleType::Int16:
        mSamplesPerBlock = 1_uz;
        mBytesPerBlock = static_cast<size_t>(mSfInfo.channels) * sizeof(short);
        break;
    case SampleType::Float:
        mSamplesPerBlock = 1_uz;
        mBytesPerBlock = static_cast<size_t>(mSfInfo.channels) * sizeof(float);
        break;
    case SampleType::IMA4:
    case SampleType::MSADPCM:
        mSamplesPerBlock = static_cast<size_t>(splblocksize);
        mBytesPerBlock = static_cast<size_t>(byteblocksize);
        break;
    }

    mFormat = AL_NONE;
    if(mSfInfo.channels == 1)
    {
        switch(sampleFormat)
        {
        case SampleType::Int16: mFormat = AL_FORMAT_MONO16; break;
        case SampleType::Float: mFormat = AL_FORMAT_MONO_FLOAT32; break;
        case SampleType::IMA4: mFormat = AL_FORMAT_MONO_IMA4; break;
        case SampleType::MSADPCM: mFormat = AL_FORMAT_MONO_MSADPCM_SOFT; break;
        }
    }
    else if(mSfInfo.channels == 2)
    {
        switch(sampleFormat)
        {
        case SampleType::Int16: mFormat = AL_FORMAT_STEREO16; break;
        case SampleType::Float: mFormat = AL_FORMAT_STEREO_FLOAT32; break;
        case SampleType::IMA4: mFormat = AL_FORMAT_STEREO_IMA4; break;
        case SampleType::MSADPCM: mFormat = AL_FORMAT_STEREO_MSADPCM_SOFT; break;
        }
    }
    else if(mSfInfo.channels == 3)
    {
        if(sf_command(mSndfile, SFC_WAVEX_GET_AMBISONIC, nullptr, 0) == SF_AMBISONIC_B_FORMAT)
        {
            switch(sampleFormat)
            {
            case SampleType::Int16: mFormat = AL_FORMAT_BFORMAT2D_16; break;
            case SampleType::Float: mFormat = AL_FORMAT_BFORMAT2D_FLOAT32; break;
            case SampleType::IMA4:
            case SampleType::MSADPCM:
                break;
            }
        }
    }
    else if(mSfInfo.channels == 4)
    {
        if(sf_command(mSndfile, SFC_WAVEX_GET_AMBISONIC, nullptr, 0) == SF_AMBISONIC_B_FORMAT)
        {
            switch(sampleFormat)
            {
            case SampleType::Int16: mFormat = AL_FORMAT_BFORMAT3D_16; break;
            case SampleType::Float: mFormat = AL_FORMAT_BFORMAT3D_FLOAT32; break;
            case SampleType::IMA4:
            case SampleType::MSADPCM:
                break;
            }
        }
    }
    if(!mFormat)
    {
        fmt::println(stderr, "Unsupported channel count: {}", mSfInfo.channels);
        sf_close(mSndfile);
        mSndfile = nullptr;

        return false;
    }

    /* Set a 1s ring buffer size. */
    const auto numblocks = (static_cast<ALuint>(mSfInfo.samplerate) + mSamplesPerBlock-1_uz)
        / mSamplesPerBlock;
    switch(sampleFormat)
    {
    case SampleType::Int16:
        mBufferVariant.emplace<std::vector<short>>(numblocks * mBytesPerBlock / sizeof(short));
        break;
    case SampleType::Float:
        mBufferVariant.emplace<std::vector<float>>(numblocks * mBytesPerBlock / sizeof(float));
        break;
    case SampleType::IMA4:
    case SampleType::MSADPCM:
        mBufferVariant.emplace<std::vector<std::byte>>(numblocks * mBytesPerBlock);
        break;
    }
    std::visit([this](auto&& vec) { mBufferData = std::as_writable_bytes(std::span{vec}); },
        mBufferVariant);

    mReadPos.store(0_uz, std::memory_order_relaxed);
    mWritePos.store(0_uz, std::memory_order_relaxed);
    mDecoderOffset = 0_uz;

    return true;
}

void StreamPlayer::close()
{
    if(mSamplesPerBlock > 1_uz)
        alBufferi(mBuffer, AL_UNPACK_BLOCK_ALIGNMENT_SOFT, 0);

    if(mSndfile)
    {
        alSourceRewind(mSource);
        alSourcei(mSource, AL_BUFFER, 0);
        sf_close(mSndfile);
        mSndfile = nullptr;
    }
}

auto StreamPlayer::bufferCallback(const std::span<std::byte> output) noexcept -> ALsizei
{
    auto dst = output.begin();

    /* NOTE: The callback *MUST* be real-time safe! That means no blocking, no
     * allocations or deallocations, no I/O, no page faults, or calls to
     * functions that do these things (this includes calling to libraries like
     * SDL_sound, libsndfile, ffmpeg, etc). Nothing should unexpectedly stall
     * this call since the audio has to get to the device on time.
     */

    auto roffset = mReadPos.load(std::memory_order_acquire);
    while(const auto remaining = static_cast<size_t>(std::distance(dst, output.end())))
    {
        /* If the write offset == read offset, there's nothing left in the
         * ring-buffer. Break from the loop and give what has been written.
         * The source will stop after playing what it's been given.
         */
        const auto woffset = mWritePos.load(std::memory_order_relaxed);
        if(woffset == roffset) break;

        /* If the write offset is behind the read offset, the readable portion
         * wrapped around. Just read up to the end of the buffer in that case,
         * otherwise read up to the write offset. Also limit the amount to copy
         * given how much is remaining to write.
         */
        auto todo = ((woffset < roffset) ? mBufferData.size() : woffset) - roffset;
        todo = std::min(todo, remaining);

        /* Copy from the ring buffer to the provided output buffer. Wrap the
         * resulting read offset if it reached the end of the ring buffer.
         */
        const auto input = mBufferData | std::views::drop(roffset) | std::views::take(todo);
        dst = std::ranges::copy(input, dst).out;

        roffset += todo;
        if(roffset == mBufferData.size())
            roffset = 0;
    }
    /* Finally, store the updated read offset, and return how many bytes
     * have been written.
     */
    mReadPos.store(roffset, std::memory_order_release);

    return static_cast<ALsizei>(std::distance(output.begin(), dst));
}

auto StreamPlayer::prepare() -> bool
{
    if(mSamplesPerBlock > 1_uz)
        alBufferi(mBuffer, AL_UNPACK_BLOCK_ALIGNMENT_SOFT, static_cast<int>(mSamplesPerBlock));

    /* A lambda without captures can decay to a normal function pointer,
     * which here just forwards to a normal class method and gives the buffer
     * as a contiguous range/span. Not strictly needed and the compiler will
     * optimize it all to a normal function, but it allows the callback
     * implementation to have a nice 'this' pointer with normal member access.
     */
    static constexpr auto callback = [](void *userptr, void *data, ALsizei size) noexcept
        -> ALsizei
    {
        auto output = std::views::counted(static_cast<std::byte*>(data), size);
        return static_cast<StreamPlayer*>(userptr)->bufferCallback(output);
    };
    alBufferCallbackSOFT(mBuffer, mFormat, mSfInfo.samplerate, callback, this);

    alSourcei(mSource, AL_BUFFER, static_cast<ALint>(mBuffer));
    if(const auto err = alGetError())
    {
        fmt::println(stderr, "Failed to set callback: {} ({:#x})", alGetString(err),
            as_unsigned(err));
        return false;
    }
    return true;
}

auto StreamPlayer::update() -> bool
{
    auto state = ALenum{};
    auto pos = ALint{};
    alGetSourcei(mSource, AL_SAMPLE_OFFSET, &pos);
    alGetSourcei(mSource, AL_SOURCE_STATE, &state);

    auto woffset = mWritePos.load(std::memory_order_acquire);
    if(state != AL_INITIAL)
    {
        const auto roffset = mReadPos.load(std::memory_order_relaxed);
        const auto readable = ((woffset >= roffset) ? woffset : (mBufferData.size()+woffset)) -
            roffset;
        /* For a stopped (underrun) source, the current playback offset is the
         * current decoder offset excluding the readable buffered data. For a
         * playing/paused source, it's the source's offset including the
         * playback offset the source was started with.
         */
        const auto curtime = ((state == AL_STOPPED)
            ? (mDecoderOffset-readable) / mBytesPerBlock * mSamplesPerBlock
            : (size_t{static_cast<ALuint>(pos)} + mStartOffset))
            / static_cast<ALuint>(mSfInfo.samplerate);
        fmt::print("\r {}m{:02}s ({:3}% full)", curtime/60, curtime%60,
            readable * 100 / mBufferData.size());
    }
    else
        fmt::println("Starting...");
    fflush(stdout);

    while(!sf_error(mSndfile))
    {
        const auto roffset = mReadPos.load(std::memory_order_relaxed);
        const auto writable = std::invoke([this,roffset,woffset]() noexcept -> size_t
        {
            if(roffset > woffset)
            {
                /* Note that the ring buffer's writable space is one byte less
                 * than the available area because the write offset ending up
                 * at the read offset would be interpreted as being empty
                 * instead of full.
                 */
                return (roffset-woffset-1) / mBytesPerBlock;
            }

            /* If the read offset is at or behind the write offset, the
             * writeable area (might) wrap around. Make sure the sample data
             * can fit, and calculate how much can go in front before wrapping.
             */
            return (mBufferData.size() - (!roffset ? woffset+1 : woffset)) / mBytesPerBlock;
        });
        if(!writable)
            break;

        /* Read samples from the file according to the type of samples
         * being buffered, and get the number of bytes buffered.
         */
        const auto read_bytes = std::visit([this,woffset,writable](auto&& data) -> size_t
        {
            using sample_t = typename std::remove_cvref_t<decltype(data)>::value_type;
            if constexpr(std::is_same_v<sample_t, short>)
            {
                const auto num_frames = sf_readf_short(mSndfile, &data[woffset/sizeof(short)],
                    static_cast<sf_count_t>(writable*mSamplesPerBlock));
                if(num_frames < 1) return 0_uz;
                return static_cast<size_t>(num_frames) * mBytesPerBlock;
            }
            else if constexpr(std::is_same_v<sample_t, float>)
            {
                const auto num_frames = sf_readf_float(mSndfile, &data[woffset/sizeof(float)],
                    static_cast<sf_count_t>(writable*mSamplesPerBlock));
                if(num_frames < 1) return 0_uz;
                return static_cast<size_t>(num_frames) * mBytesPerBlock;
            }
            else if constexpr(std::is_same_v<sample_t, std::byte>)
            {
                const auto numbytes = sf_read_raw(mSndfile, &data[woffset],
                    static_cast<sf_count_t>(writable*mBytesPerBlock));
                if(numbytes < 1) return size_t{0};
                return static_cast<size_t>(numbytes);
            }
        }, mBufferVariant);
        if(read_bytes == 0)
            break;

        woffset += read_bytes;
        if(woffset == mBufferData.size())
            woffset = 0;

        mWritePos.store(woffset, std::memory_order_release);
        mDecoderOffset += read_bytes;
    }

    if(state != AL_PLAYING && state != AL_PAUSED)
    {
        /* If the source is not playing or paused, it either underrun
         * (AL_STOPPED) or is just getting started (AL_INITIAL). If the ring
         * buffer is empty, it's done, otherwise play the source with what's
         * available.
         */
        const auto roffset = mReadPos.load(std::memory_order_relaxed);
        const auto readable = ((woffset < roffset) ? mBufferData.size()+woffset : woffset) -
            roffset;
        if(readable == 0)
            return false;

        /* Store the playback offset that the source will start reading
         * from, so it can be tracked during playback.
         */
        mStartOffset = (mDecoderOffset-readable) / mBytesPerBlock * mSamplesPerBlock;
        alSourcePlay(mSource);
        if(alGetError() != AL_NO_ERROR)
            return false;
    }
    return true;
}


auto main(std::span<std::string_view> args) -> int
{
    /* Print out usage if no arguments were specified */
    if(args.size() < 2)
    {
        fmt::println(stderr, "Usage: {} [-device <name>] <filenames...>", args[0]);
        return 1;
    }

    args = args.subspan(1);
    auto almgr = InitAL(args);

    if(!alIsExtensionPresent("AL_SOFT_callback_buffer"))
    {
        fmt::println(stderr, "AL_SOFT_callback_buffer extension not available");
        return 1;
    }

    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
    alBufferCallbackSOFT = reinterpret_cast<LPALBUFFERCALLBACKSOFT>(
        alGetProcAddress("alBufferCallbackSOFT"));

    auto refresh = ALCint{25};
    alcGetIntegerv(alcGetContextsDevice(alcGetCurrentContext()), ALC_REFRESH, 1, &refresh);

    auto player = std::make_unique<StreamPlayer>();

    /* Play each file listed on the command line */
    std::ranges::for_each(args, [refresh,&player](const std::string_view fname)
    {
        if(!player->open(std::string{fname}))
            return;

        /* Get the name portion, without the path, for display. */
        const auto namepart = fname.substr(std::max(fname.rfind('/')+1, fname.rfind('\\')+1));

        fmt::println("Playing: {} ({}, {}hz)", namepart, FormatName(player->mFormat),
            player->mSfInfo.samplerate);
        fflush(stdout);

        if(!player->prepare())
        {
            player->close();
            return;
        }

        while(player->update())
            std::this_thread::sleep_for(nanoseconds{seconds{1}} / refresh);
        putc('\n', stdout);

        /* All done with this file. Close it and go to the next */
        player->close();
    });
    /* All done. */
    fmt::println("Done.");

    return 0;
}

} // namespace

auto main(int argc, char **argv) -> int
{
    assert(argc >= 0);
    auto args = std::vector<std::string_view>(static_cast<unsigned int>(argc));
    std::ranges::copy(std::views::counted(argv, argc), args.begin());
    return main(std::span{args});
}
