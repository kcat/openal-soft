/*
 * OpenAL Convolution Reverb Example
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

/* This file contains a streaming audio player, using the convolution reverb
 * effect.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "sndfile.h"

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "common/alhelpers.h"


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

#ifndef AL_SOFT_convolution_reverb
#define AL_SOFT_convolution_reverb
#define AL_EFFECT_CONVOLUTION_REVERB_SOFT        0xA000
#endif


namespace {

/* Effect object functions */
LPALGENEFFECTS alGenEffects;
LPALDELETEEFFECTS alDeleteEffects;
LPALISEFFECT alIsEffect;
LPALEFFECTI alEffecti;
LPALEFFECTIV alEffectiv;
LPALEFFECTF alEffectf;
LPALEFFECTFV alEffectfv;
LPALGETEFFECTI alGetEffecti;
LPALGETEFFECTIV alGetEffectiv;
LPALGETEFFECTF alGetEffectf;
LPALGETEFFECTFV alGetEffectfv;

/* Auxiliary Effect Slot object functions */
LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots;
LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots;
LPALISAUXILIARYEFFECTSLOT alIsAuxiliaryEffectSlot;
LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti;
LPALAUXILIARYEFFECTSLOTIV alAuxiliaryEffectSlotiv;
LPALAUXILIARYEFFECTSLOTF alAuxiliaryEffectSlotf;
LPALAUXILIARYEFFECTSLOTFV alAuxiliaryEffectSlotfv;
LPALGETAUXILIARYEFFECTSLOTI alGetAuxiliaryEffectSloti;
LPALGETAUXILIARYEFFECTSLOTIV alGetAuxiliaryEffectSlotiv;
LPALGETAUXILIARYEFFECTSLOTF alGetAuxiliaryEffectSlotf;
LPALGETAUXILIARYEFFECTSLOTFV alGetAuxiliaryEffectSlotfv;


ALuint CreateEffect()
{
    /* Create the effect object and try to set convolution reverb. */
    ALuint effect{0};
    alGenEffects(1, &effect);

    printf("Using Convolution Reverb\n");

    alEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_CONVOLUTION_REVERB_SOFT);

    /* Check if an error occured, and clean up if so. */
    if(ALenum err{alGetError()})
    {
        fprintf(stderr, "OpenAL error: %s\n", alGetString(err));
        if(alIsEffect(effect))
            alDeleteEffects(1, &effect);
        return 0;
    }

    return effect;
}


ALuint LoadSound(const char *filename)
{
    /* Open the audio file and check that it's usable. */
    SF_INFO sfinfo{};
    SNDFILE *sndfile{sf_open(filename, SFM_READ, &sfinfo)};
    if(!sndfile)
    {
        fprintf(stderr, "Could not open audio in %s: %s\n", filename, sf_strerror(sndfile));
        return 0;
    }
    constexpr sf_count_t max_samples{std::numeric_limits<int>::max() / sizeof(float)};
    if(sfinfo.frames < 1 || sfinfo.frames > max_samples/sfinfo.channels)
    {
        fprintf(stderr, "Bad sample count in %s (%" PRId64 ")\n", filename, sfinfo.frames);
        sf_close(sndfile);
        return 0;
    }

    /* Get the sound format, and figure out the OpenAL format. Use a float
     * format since impulse responses are keen on having a low noise floor.
     */
    ALenum format{};
    if(sfinfo.channels == 1)
        format = AL_FORMAT_MONO_FLOAT32;
    else if(sfinfo.channels == 2)
        format = AL_FORMAT_STEREO_FLOAT32;
    else
    {
        fprintf(stderr, "Unsupported channel count: %d\n", sfinfo.channels);
        sf_close(sndfile);
        return 0;
    }

    auto membuf = std::make_unique<float[]>(static_cast<size_t>(sfinfo.frames * sfinfo.channels));

    sf_count_t num_frames{sf_readf_float(sndfile, membuf.get(), sfinfo.frames)};
    if(num_frames < 1)
    {
        membuf = nullptr;
        sf_close(sndfile);
        fprintf(stderr, "Failed to read samples in %s (%" PRId64 ")\n", filename, num_frames);
        return 0;
    }
    const auto num_bytes = static_cast<ALsizei>(num_frames * sfinfo.channels) *
        ALsizei{sizeof(float)};

    ALuint buffer{0};
    alGenBuffers(1, &buffer);
    alBufferData(buffer, format, membuf.get(), num_bytes, sfinfo.samplerate);

    membuf = nullptr;
    sf_close(sndfile);

    if(ALenum err{alGetError()})
    {
        fprintf(stderr, "OpenAL Error: %s\n", alGetString(err));
        if(buffer && alIsBuffer(buffer))
            alDeleteBuffers(1, &buffer);
        return 0;
    }

    return buffer;
}


/* This is largely the same as in alstreamcb.cpp. Comments removed for brevity,
 * see the aforementioned source for more details.
 */
using std::chrono::seconds;
using std::chrono::nanoseconds;

LPALBUFFERCALLBACKSOFT alBufferCallbackSOFT;

struct StreamPlayer {
    std::unique_ptr<ALbyte[]> mBufferData;
    size_t mBufferDataSize{0};
    std::atomic<size_t> mReadPos{0};
    std::atomic<size_t> mWritePos{0};

    ALuint mBuffer{0}, mSource{0};
    size_t mStartOffset{0};

    SNDFILE *mSndfile{nullptr};
    SF_INFO mSfInfo{};
    size_t mDecoderOffset{0};

    ALenum mFormat;

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
        if(mSndfile)
            sf_close(mSndfile);
    }

    void close()
    {
        if(mSndfile)
        {
            alSourceRewind(mSource);
            alSourcei(mSource, AL_BUFFER, 0);
            sf_close(mSndfile);
            mSndfile = nullptr;
        }
    }

    bool open(const char *filename)
    {
        close();

        mSndfile = sf_open(filename, SFM_READ, &mSfInfo);
        if(!mSndfile)
        {
            fprintf(stderr, "Could not open audio in %s: %s\n", filename, sf_strerror(mSndfile));
            return false;
        }

        mFormat = AL_NONE;
        if(mSfInfo.channels == 1)
            mFormat = AL_FORMAT_MONO_FLOAT32;
        else if(mSfInfo.channels == 2)
            mFormat = AL_FORMAT_STEREO_FLOAT32;
        else if(mSfInfo.channels == 6)
            mFormat = AL_FORMAT_51CHN32;
        else
        {
            fprintf(stderr, "Unsupported channel count: %d\n", mSfInfo.channels);
            sf_close(mSndfile);
            mSndfile = nullptr;

            return false;
        }

        mBufferDataSize = static_cast<ALuint>(mSfInfo.samplerate*mSfInfo.channels) * sizeof(float);
        mBufferData.reset(new ALbyte[mBufferDataSize]);
        mReadPos.store(0, std::memory_order_relaxed);
        mWritePos.store(0, std::memory_order_relaxed);
        mDecoderOffset = 0;

        return true;
    }

    static ALsizei AL_APIENTRY bufferCallbackC(void *userptr, void *data, ALsizei size)
    { return static_cast<StreamPlayer*>(userptr)->bufferCallback(data, size); }
    ALsizei bufferCallback(void *data, ALsizei size)
    {
        ALsizei got{0};

        size_t roffset{mReadPos.load(std::memory_order_acquire)};
        while(got < size)
        {
            const size_t woffset{mWritePos.load(std::memory_order_relaxed)};
            if(woffset == roffset) break;

            size_t todo{((woffset < roffset) ? mBufferDataSize : woffset) - roffset};
            todo = std::min<size_t>(todo, static_cast<ALuint>(size-got));

            memcpy(data, &mBufferData[roffset], todo);
            data = static_cast<ALbyte*>(data) + todo;
            got += static_cast<ALsizei>(todo);

            roffset += todo;
            if(roffset == mBufferDataSize)
                roffset = 0;
        }
        mReadPos.store(roffset, std::memory_order_release);

        return got;
    }

    bool prepare()
    {
        alBufferCallbackSOFT(mBuffer, mFormat, mSfInfo.samplerate, bufferCallbackC, this, 0);
        alSourcei(mSource, AL_BUFFER, static_cast<ALint>(mBuffer));
        if(ALenum err{alGetError()})
        {
            fprintf(stderr, "Failed to set callback: %s (0x%04x)\n", alGetString(err), err);
            return false;
        }
        return true;
    }

    bool update()
    {
        ALenum state;
        ALint pos;
        alGetSourcei(mSource, AL_SAMPLE_OFFSET, &pos);
        alGetSourcei(mSource, AL_SOURCE_STATE, &state);

        const size_t frame_size{static_cast<ALuint>(mSfInfo.channels) * sizeof(float)};
        size_t woffset{mWritePos.load(std::memory_order_acquire)};
        if(state != AL_INITIAL)
        {
            const size_t roffset{mReadPos.load(std::memory_order_relaxed)};
            const size_t readable{((woffset >= roffset) ? woffset : (mBufferDataSize+woffset)) -
                roffset};
            const size_t curtime{((state==AL_STOPPED) ? (mDecoderOffset-readable) / frame_size
                : (static_cast<ALuint>(pos) + mStartOffset/frame_size))
                / static_cast<ALuint>(mSfInfo.samplerate)};
            printf("\r%3zus (%3zu%% full)", curtime, readable * 100 / mBufferDataSize);
        }
        else
            fputs("Starting...", stdout);
        fflush(stdout);

        while(!sf_error(mSndfile))
        {
            size_t read_bytes;
            const size_t roffset{mReadPos.load(std::memory_order_relaxed)};
            if(roffset > woffset)
            {
                const size_t writable{roffset-woffset-1};
                if(writable < frame_size) break;

                sf_count_t num_frames{sf_readf_float(mSndfile,
                    reinterpret_cast<float*>(&mBufferData[woffset]),
                    static_cast<sf_count_t>(writable/frame_size))};
                if(num_frames < 1) break;

                read_bytes = static_cast<size_t>(num_frames) * frame_size;
                woffset += read_bytes;
            }
            else
            {
                const size_t writable{!roffset ? mBufferDataSize-woffset-1 :
                    (mBufferDataSize-woffset)};
                if(writable < frame_size) break;

                sf_count_t num_frames{sf_readf_float(mSndfile,
                    reinterpret_cast<float*>(&mBufferData[woffset]),
                    static_cast<sf_count_t>(writable/frame_size))};
                if(num_frames < 1) break;

                read_bytes = static_cast<size_t>(num_frames) * frame_size;
                woffset += read_bytes;
                if(woffset == mBufferDataSize)
                    woffset = 0;
            }
            mWritePos.store(woffset, std::memory_order_release);
            mDecoderOffset += read_bytes;
        }

        if(state != AL_PLAYING && state != AL_PAUSED)
        {
            const size_t roffset{mReadPos.load(std::memory_order_relaxed)};
            const size_t readable{((woffset >= roffset) ? woffset : (mBufferDataSize+woffset)) -
                roffset};
            if(readable == 0)
                return false;

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
    /* A simple RAII container for OpenAL startup and shutdown. */
    struct AudioManager {
        AudioManager(char ***argv_, int *argc_)
        {
            if(InitAL(argv_, argc_) != 0)
                throw std::runtime_error{"Failed to initialize OpenAL"};
        }
        ~AudioManager() { CloseAL(); }
    };

    /* Print out usage if no arguments were specified */
    if(argc < 2)
    {
        fprintf(stderr, "Usage: %s [-device <name>] <impulse response sound> [sound files...]\n",
            argv[0]);
        return 1;
    }

    argv++; argc--;
    AudioManager almgr{&argv, &argc};

    if(!alIsExtensionPresent("AL_SOFTX_callback_buffer"))
    {
        fprintf(stderr, "AL_SOFT_callback_buffer extension not available\n");
        return 1;
    }

    /* Define a macro to help load the function pointers. */
#define LOAD_PROC(T, x)  ((x) = reinterpret_cast<T>(alGetProcAddress(#x)))
    LOAD_PROC(LPALBUFFERCALLBACKSOFT, alBufferCallbackSOFT);

    LOAD_PROC(LPALGENEFFECTS, alGenEffects);
    LOAD_PROC(LPALDELETEEFFECTS, alDeleteEffects);
    LOAD_PROC(LPALISEFFECT, alIsEffect);
    LOAD_PROC(LPALEFFECTI, alEffecti);
    LOAD_PROC(LPALEFFECTIV, alEffectiv);
    LOAD_PROC(LPALEFFECTF, alEffectf);
    LOAD_PROC(LPALEFFECTFV, alEffectfv);
    LOAD_PROC(LPALGETEFFECTI, alGetEffecti);
    LOAD_PROC(LPALGETEFFECTIV, alGetEffectiv);
    LOAD_PROC(LPALGETEFFECTF, alGetEffectf);
    LOAD_PROC(LPALGETEFFECTFV, alGetEffectfv);

    LOAD_PROC(LPALGENAUXILIARYEFFECTSLOTS, alGenAuxiliaryEffectSlots);
    LOAD_PROC(LPALDELETEAUXILIARYEFFECTSLOTS, alDeleteAuxiliaryEffectSlots);
    LOAD_PROC(LPALISAUXILIARYEFFECTSLOT, alIsAuxiliaryEffectSlot);
    LOAD_PROC(LPALAUXILIARYEFFECTSLOTI, alAuxiliaryEffectSloti);
    LOAD_PROC(LPALAUXILIARYEFFECTSLOTIV, alAuxiliaryEffectSlotiv);
    LOAD_PROC(LPALAUXILIARYEFFECTSLOTF, alAuxiliaryEffectSlotf);
    LOAD_PROC(LPALAUXILIARYEFFECTSLOTFV, alAuxiliaryEffectSlotfv);
    LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTI, alGetAuxiliaryEffectSloti);
    LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTIV, alGetAuxiliaryEffectSlotiv);
    LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTF, alGetAuxiliaryEffectSlotf);
    LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTFV, alGetAuxiliaryEffectSlotfv);
#undef LOAD_PROC

    /* Load the impulse response sound file into a buffer. */
    ALuint buffer{LoadSound(argv[0])};
    if(!buffer) return 1;

    /* Create the convolution reverb effect. */
    ALuint effect{CreateEffect()};
    if(!effect)
    {
        alDeleteBuffers(1, &buffer);
        return 1;
    }

    /* Create the effect slot object. This is what "plays" an effect on sources
     * that connect to it. */
    ALuint slot{0};
    alGenAuxiliaryEffectSlots(1, &slot);

    /* Set the impulse response sound buffer on the effect slot. This allows
     * effects to access it as needed. In this case, convolution reverb uses it
     * as the filter source. NOTE: Unlike the effect object, the buffer *is*
     * kept referenced and may not be changed or deleted as long as it's set,
     * just like with a source. When another buffer is set, or the effect slot
     * is deleted, the buffer reference is released.
     *
     * The effect slot's gain is reduced because the impulse responses I've
     * tested with result in excessively loud reverb. Is that normal? Even with
     * this, it seems a bit on the loud side.
     *
     * Also note: unlike standard or EAX reverb, there is no automatic
     * attenuation of a source's reverb response with distance, so the reverb
     * will remain full volume regardless of a given sound's distance from the
     * listener. You can use a send filter to alter a given source's
     * contribution to reverb.
     */
    alAuxiliaryEffectSloti(slot, AL_BUFFER, static_cast<ALint>(buffer));
    alAuxiliaryEffectSlotf(slot, AL_EFFECTSLOT_GAIN, 1.0f / 16.0f);
    alAuxiliaryEffectSloti(slot, AL_EFFECTSLOT_EFFECT, static_cast<ALint>(effect));
    assert(alGetError()==AL_NO_ERROR && "Failed to set effect slot");

    ALCint refresh{25};
    alcGetIntegerv(alcGetContextsDevice(alcGetCurrentContext()), ALC_REFRESH, 1, &refresh);

    std::unique_ptr<StreamPlayer> player{new StreamPlayer{}};
    alSource3i(player->mSource, AL_AUXILIARY_SEND_FILTER, static_cast<ALint>(slot), 0,
        AL_FILTER_NULL);

    for(int i{1};i < argc;++i)
    {
        if(!player->open(argv[i]))
            continue;

        const char *namepart{strrchr(argv[i], '/')};
        if(namepart || (namepart=strrchr(argv[i], '\\')))
            ++namepart;
        else
            namepart = argv[i];

        printf("Playing: %s (%s, %dhz)\n", namepart, FormatName(player->mFormat),
            player->mSfInfo.samplerate);
        fflush(stdout);

        if(!player->prepare())
        {
            player->close();
            continue;
        }

        while(player->update())
            std::this_thread::sleep_for(nanoseconds{seconds{1}} / refresh);
        putc('\n', stdout);

        player->close();
    }
    /* All done. */
    printf("Done.\n");

    player = nullptr;
    alDeleteAuxiliaryEffectSlots(1, &slot);
    alDeleteEffects(1, &effect);
    alDeleteBuffers(1, &buffer);

    return 0;
}
