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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "SDL_sound.h"
#include "SDL_audio.h"
#include "SDL_stdinc.h"

#include "AL/al.h"
#include "AL/alc.h"

#include "common/alhelpers.h"


#ifndef SDL_AUDIO_MASK_BITSIZE
#define SDL_AUDIO_MASK_BITSIZE (0xFF)
#endif
#ifndef SDL_AUDIO_BITSIZE
#define SDL_AUDIO_BITSIZE(x) (x & SDL_AUDIO_MASK_BITSIZE)
#endif


#ifndef AL_SOFT_callback_buffer
#define AL_SOFT_callback_buffer
typedef unsigned int ALbitfieldSOFT;
#define AL_BUFFER_CALLBACK_FUNCTION_SOFT         0x19A0
#define AL_BUFFER_CALLBACK_USER_PARAM_SOFT       0x19A1
typedef ALsizei (AL_APIENTRY*LPALBUFFERCALLBACKTYPESOFT)(ALvoid *userptr, ALvoid *sampledata, ALsizei numsamples);
typedef void (AL_APIENTRY*LPALBUFFERCALLBACKSOFT)(ALuint buffer, ALenum format, ALsizei freq, LPALBUFFERCALLBACKTYPESOFT callback, ALvoid *userptr, ALbitfieldSOFT flags);
typedef void (AL_APIENTRY*LPALGETBUFFERPTRSOFT)(ALuint buffer, ALenum param, ALvoid **value);
typedef void (AL_APIENTRY*LPALGETBUFFER3PTRSOFT)(ALuint buffer, ALenum param, ALvoid **value1, ALvoid **value2, ALvoid **value3);
typedef void (AL_APIENTRY*LPALGETBUFFERPTRVSOFT)(ALuint buffer, ALenum param, ALvoid **values);
#endif

namespace {

using std::chrono::seconds;
using std::chrono::nanoseconds;

LPALBUFFERCALLBACKSOFT alBufferCallbackSOFT;

struct StreamPlayer {
    /* A lockless ring-buffer (supports single-provider, single-consumer
     * operation).
     */
    std::unique_ptr<ALbyte[]> mBufferData;
    size_t mBufferDataSize{0};
    std::atomic<size_t> mReadPos{0};
    std::atomic<size_t> mWritePos{0};

    /* The buffer to get the callback, and source to play with. */
    ALuint mBuffer{0}, mSource{0};
    size_t mStartOffset{0};

    /* Handle for the audio file to decode. */
    Sound_Sample *mSample{nullptr};
    Uint32 mAvailableData{0};
    size_t mDecoderOffset{0};

    /* The format of the callback samples. */
    ALenum mFormat;
    ALsizei mSampleRate;

    StreamPlayer()
    {
        alGenBuffers(1, &mBuffer);
        if(ALenum err{alGetError()})
            throw std::runtime_error{"alGenBuffers failed"};
        alGenSources(1, &mSource);
        if(ALenum err{alGetError()})
        {
            alDeleteBuffers(1, &mBuffer);
            throw std::runtime_error{"alGenSources failed"};
        }
    }
    ~StreamPlayer()
    {
        alDeleteSources(1, &mSource);
        alDeleteBuffers(1, &mBuffer);
        if(mSample)
            Sound_FreeSample(mSample);
    }

    void close()
    {
        if(mSample)
        {
            alSourceRewind(mSource);
            alSourcei(mSource, AL_BUFFER, 0);
            Sound_FreeSample(mSample);
            mSample = nullptr;
        }
    }

    bool open(const char *filename)
    {
        close();

        /* Open the file in its normal format. */
        mSample = Sound_NewSampleFromFile(filename, nullptr, 0);
        if(!mSample)
        {
            fprintf(stderr, "Could not open audio in %s\n", filename);
            return false;
        }

        /* Figure out the OpenAL format from the sample's format. */
        mFormat = AL_NONE;
        if(mSample->actual.channels == 1)
        {
            if(mSample->actual.format == AUDIO_U8)
                mFormat = AL_FORMAT_MONO8;
            else if(mSample->actual.format == AUDIO_S16SYS)
                mFormat = AL_FORMAT_MONO16;
        }
        else if(mSample->actual.channels == 2)
        {
            if(mSample->actual.format == AUDIO_U8)
                mFormat = AL_FORMAT_STEREO8;
            else if(mSample->actual.format == AUDIO_S16SYS)
                mFormat = AL_FORMAT_STEREO16;
        }
        if(!mFormat)
        {
            fprintf(stderr, "Unsupported sample format: 0x%04x, %d channels\n",
                mSample->actual.format, mSample->actual.channels);
            Sound_FreeSample(mSample);
            mSample = nullptr;

            return false;
        }
        mSampleRate = static_cast<ALsizei>(mSample->actual.rate);

        const auto frame_size = Uint32{mSample->actual.channels} *
            SDL_AUDIO_BITSIZE(mSample->actual.format) / 8;

        /* Set a 50ms decode buffer size. */
        Sound_SetBufferSize(mSample, static_cast<Uint32>(mSampleRate)*50/1000 * frame_size);
        mAvailableData = 0;

        /* Set a 1s ring buffer size. */
        mBufferDataSize = static_cast<Uint32>(mSampleRate) * size_t{frame_size};
        mBufferData.reset(new ALbyte[mBufferDataSize]);
        mReadPos.store(0, std::memory_order_relaxed);
        mWritePos.store(0, std::memory_order_relaxed);
        mDecoderOffset = 0;

        return true;
    }

    /* The actual C-style callback just forwards to the non-static method. Not
     * strictly needed and the compiler will optimize it to a normal function,
     * but it allows the callback implementation to have a nice 'this' pointer
     * with normal member access.
     */
    static ALsizei AL_APIENTRY bufferCallbackC(void *userptr, void *data, ALsizei size)
    { return static_cast<StreamPlayer*>(userptr)->bufferCallback(data, size); }
    ALsizei bufferCallback(void *data, ALsizei size)
    {
        /* NOTE: The callback *MUST* be real-time safe! That means no blocking,
         * no allocations or deallocations, no I/O, no page faults, or calls to
         * functions that could do these things (this includes calling to
         * libraries like SDL_sound, libsndfile, ffmpeg, etc). Nothing should
         * unexpectedly stall this call since the audio has to get to the
         * device on time.
         */
        ALsizei got{0};

        size_t roffset{mReadPos.load(std::memory_order_acquire)};
        while(got < size)
        {
            /* If the write offset == read offset, there's nothing left in the
             * ring-buffer. Break from the loop and give what has been written.
             */
            const size_t woffset{mWritePos.load(std::memory_order_relaxed)};
            if(woffset == roffset) break;

            /* If the write offset is behind the read offset, the readable
             * portion wrapped around. Just read up to the end of the buffer in
             * that case, otherwise read up to the write offset. Also limit the
             * amount to copy given how much is remaining to write.
             */
            size_t todo{((woffset < roffset) ? mBufferDataSize : woffset) - roffset};
            todo = std::min<size_t>(todo, static_cast<ALuint>(size-got));

            /* Copy from the ring buffer to the provided output buffer. Wrap
             * the resulting read offset if it reached the end of the ring-
             * buffer.
             */
            memcpy(data, &mBufferData[roffset], todo);
            data = static_cast<ALbyte*>(data) + todo;
            got += static_cast<ALsizei>(todo);

            roffset += todo;
            if(roffset == mBufferDataSize)
                roffset = 0;
        }
        /* Finally, store the updated read offset, and return how many bytes
         * have been written.
         */
        mReadPos.store(roffset, std::memory_order_release);

        return got;
    }

    bool prepare()
    {
        alBufferCallbackSOFT(mBuffer, mFormat, mSampleRate, bufferCallbackC, this, 0);
        alSourcei(mSource, AL_BUFFER, static_cast<ALint>(mBuffer));
        if(ALenum err{alGetError()})
        {
            fprintf(stderr, "Failed to set callback: %s (0x%04x)\n", alGetString(err), err);
            return false;
        }

        mAvailableData = Sound_Decode(mSample);
        if(!mAvailableData)
            fprintf(stderr, "Failed to decode any samples: %s\n", Sound_GetError());
        return mAvailableData != 0;
    }

    bool update()
    {
        constexpr int BadFlags{SOUND_SAMPLEFLAG_EOF | SOUND_SAMPLEFLAG_ERROR};

        ALenum state;
        ALint pos;
        alGetSourcei(mSource, AL_SAMPLE_OFFSET, &pos);
        alGetSourcei(mSource, AL_SOURCE_STATE, &state);

        size_t woffset{mWritePos.load(std::memory_order_acquire)};
        if(state != AL_INITIAL)
        {
            const auto frame_size = Uint32{mSample->actual.channels} *
                SDL_AUDIO_BITSIZE(mSample->actual.format) / 8;
            const size_t roffset{mReadPos.load(std::memory_order_relaxed)};
            const size_t readable{((woffset >= roffset) ? woffset : (mBufferDataSize+woffset)) -
                roffset};
            /* For a stopped (underrun) source, the current playback offset is
             * the current decoder offset excluding the readable buffered data.
             * For a playing/paused source, it's the source's offset including
             * the playback offset the source was started with.
             */
            const size_t curtime{((state==AL_STOPPED) ? (mDecoderOffset-readable) / frame_size
                : (static_cast<ALuint>(pos) + mStartOffset/frame_size)) / mSample->actual.rate};
            printf("\r%3zus (%3zu%% full)", curtime, readable * 100 / mBufferDataSize);
        }
        else
            fputs("Starting...", stdout);
        fflush(stdout);

        while(mAvailableData > 0)
        {
            const size_t roffset{mReadPos.load(std::memory_order_relaxed)};
            if(roffset > woffset)
            {
                /* Note that the ring buffer's writable space is one byte less
                 * than the available area because the write offset ending up
                 * at the read offset would be interpreted as being empty
                 * instead of full.
                 */
                const size_t writable{roffset-woffset-1};
                /* Don't copy the sample data if it can't all fit. */
                if(writable < mAvailableData) break;

                memcpy(&mBufferData[woffset], mSample->buffer, mAvailableData);
                woffset += mAvailableData;
            }
            else
            {
                /* If the read offset is at or behind the write offset, the
                 * writeable area (might) wrap around. Make sure the sample
                 * data can fit, and calculate how much goes in front and in
                 * back.
                 */
                const size_t writable{mBufferDataSize+roffset-woffset-1};
                if(writable < mAvailableData) break;

                const size_t todo1{std::min<size_t>(mAvailableData, mBufferDataSize-woffset)};
                const size_t todo2{mAvailableData - todo1};

                memcpy(&mBufferData[woffset], mSample->buffer, todo1);
                woffset += todo1;
                if(woffset == mBufferDataSize)
                {
                    woffset = 0;
                    if(todo2 > 0)
                    {
                        memcpy(&mBufferData[woffset], static_cast<ALbyte*>(mSample->buffer)+todo1,
                            todo2);
                        woffset += todo2;
                    }
                }
            }
            mWritePos.store(woffset, std::memory_order_release);
            mDecoderOffset += mAvailableData;

            if(!(mSample->flags&BadFlags))
                mAvailableData = Sound_Decode(mSample);
            else
                mAvailableData = 0;
        }

        if(state != AL_PLAYING && state != AL_PAUSED)
        {
            /* If the source is not playing or paused, it either underrun
             * (AL_STOPPED) or is just getting started (AL_INITIAL). If the
             * ring buffer is empty, it's done, otherwise play the source with
             * what's available.
             */
            const size_t roffset{mReadPos.load(std::memory_order_relaxed)};
            const size_t readable{((woffset >= roffset) ? woffset : (mBufferDataSize+woffset)) -
                roffset};
            if(readable == 0)
                return false;

            /* Store the playback offset that the source will start reading
             * from, so it can be tracked during playback.
             */
            mStartOffset = mDecoderOffset - readable;
            alSourcePlay(mSource);
            if(alGetError() != AL_NO_ERROR)
                return false;
        }
        return true;
    }
};

} // namespace

int main(int argc, char **argv)
{
    /* A simple RAII container for OpenAL and SDL_sound startup and shutdown. */
    struct AudioManager {
        AudioManager(char ***argv_, int *argc_)
        {
            if(InitAL(argv_, argc_) != 0)
                throw std::runtime_error{"Failed to initialize OpenAL"};
            Sound_Init();
        }
        ~AudioManager() { Sound_Quit(); CloseAL(); }
    };

    /* Print out usage if no arguments were specified */
    if(argc < 2)
    {
        fprintf(stderr, "Usage: %s [-device <name>] <filenames...>\n", argv[0]);
        return 1;
    }

    argv++; argc--;
    AudioManager almgr{&argv, &argc};

    if(!alIsExtensionPresent("AL_SOFTX_callback_buffer"))
    {
        fprintf(stderr, "AL_SOFT_callback_buffer extension not available\n");
        return 1;
    }

    alBufferCallbackSOFT = reinterpret_cast<LPALBUFFERCALLBACKSOFT>(
        alGetProcAddress("alBufferCallbackSOFT"));

    ALCint refresh{25};
    alcGetIntegerv(alcGetContextsDevice(alcGetCurrentContext()), ALC_REFRESH, 1, &refresh);

    std::unique_ptr<StreamPlayer> player{new StreamPlayer{}};

    /* Play each file listed on the command line */
    for(int i{0};i < argc;++i)
    {
        if(!player->open(argv[i]))
            continue;

        /* Get the name portion, without the path, for display. */
        const char *namepart{strrchr(argv[i], '/')};
        if(namepart || (namepart=strrchr(argv[i], '\\')))
            ++namepart;
        else
            namepart = argv[i];

        printf("Playing: %s (%s, %dhz)\n", namepart, FormatName(player->mFormat),
            player->mSampleRate);
        fflush(stdout);

        if(!player->prepare())
        {
            player->close();
            continue;
        }

        while(player->update())
            std::this_thread::sleep_for(nanoseconds{seconds{1}} / refresh);
        putc('\n', stdout);

        /* All done with this file. Close it and go to the next */
        player->close();
    }
    /* All done. */
    printf("Done.\n");

    return 0;
}
