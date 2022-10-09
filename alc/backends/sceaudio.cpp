/**
 * OpenAL cross platform audio library
 * Copyright (C) 2018 by authors.
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

/*
 * - Due to the nature of the platform this backend is designed for,
 *   I figured it'd be best if I elaborate some things here just in case.
 * 
 * - This backend is not maintained or in any way supported by kcat,
 *   You are on your own :)
 * 
 * - The maintainer is Nikita Krapivin, or @nkrapivin on GitHub,
 *   reach out to him about usage and compilation.
 * 
 * - No build scripts or project files are provided,
 *   Make yours :) don't forget to -O3 and -march=btver2 heh heh heh
 * 
 * - No documentation for the platform is provided,
 *   not even constants or their names, just in case...
 * 
 * - This backend assumes you know a thing or two about SceUserService,
 *   ahd physical limitations of DualShock 4's microphone.
 *
 * - The compilation may (or, well, WILL) fail if you're trying to use an official SDK,
 *   I can't test on it because... obviously, you will need to tweak a few things.
 *   For instance, you will need to stub al::getenv() to always return nullptr.
 *   PS4 has no environment variables and my weird toolchain installation has a dummy getenv()
 *   in libc already... it's a huge mess, really, this backend does 90% of the work for you.
 */

#include "config.h"

#include "sceaudio.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <mutex>

#include "albyte.h"
#include "alc/alconfig.h"
#include "aloptional.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "core/device.h"
#include "core/logging.h"
#include "core/helpers.h"
#include "threads.h"
#include "vector.h"
#include "ringbuffer.h"


namespace {


extern "C" {
    // not gonna explain any of these, just take these for granted
	// official SDK users can probably just get rid of these and include the appropriate headers?????

    extern int sceAudioInOpen(int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int);
    extern int sceAudioInHqOpen(int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int);
    extern int sceAudioInClose(int);
    extern int sceAudioInInput(int, void *);
    extern int sceAudioInGetSilentState(int);
    extern int sceUserServiceGetLoginUserIdList(int *);
    extern int sceUserServiceInitialize(int *);
    extern int sceAudioOutInit();
    extern int sceAudioOutOpen(int, int, int, unsigned int, unsigned int, unsigned int);
    extern int sceAudioOutOutput(int, const void *);
    extern int sceAudioOutClose(int);
    extern int scePthreadSetschedparam(pthread_t, int, const int *);
    extern int scePthreadRename(pthread_t, const char *);
    extern pthread_t scePthreadSelf();
}

const std::string DeviceNames[] = {
    /* these ports do not require a specific userid and operate under SYSTEM */
    "MAIN",
    "BGM",
    "AUX",
    /* these ports require a non-SYSTEM valid user id in order to operate */
    "VOICE1", "VOICE2", "VOICE3", "VOICE4",
    "PERSONAL1", "PERSONAL2", "PERSONAL3", "PERSONAL4",
    "PADSPK1", "PADSPK2", "PADSPK3", "PADSPK4"
};

/* device -> port */
int DevicePorts[] = {
    0, /* "MAIN" */
    1, /* "BGM" */
    127, /* "AUX" */
    2, 2, 2, 2, /* "VOICE-" */
    3, 3, 3, 3, /* "PERSONAL-" */
    4, 4, 4, 4 /* "PADSPK-" */
};

/* device -> required user id */
int DeviceUserIds[] = {
    /* SYSTEM user id */
    0xFF, /* "MAIN" */
    0xFF, /* "BGM" */
    0xFF, /* "AUX" */
    /* Look-up from users list */
    1, 2, 3, 4, /* "VOICE" 1,2,3,4 */
    1, 2, 3, 4, /* "PERSONAL" 1,2,3,4 */
    1, 2, 3, 4 /* "PERSONAL" 1,2,3,4 */
};

struct SceAudioOutBackend final : public BackendBase {
    SceAudioOutBackend(DeviceBase *device) noexcept : BackendBase{device} { }
    ~SceAudioOutBackend() override;

    void open(const char *name) override;
    bool reset() override;
    void start() override;
    void stop() override;

    /* SceAudioOut handle, must be closed when not in use */
    int mDeviceID{-1};

    uint mFrameSize{0};

    uint mFrequency{0u};
    DevFmtChannels mFmtChans{};
    DevFmtType     mFmtType{};
    uint mUpdateSize{0u};

    al::vector<al::byte> mBuffer;
    std::atomic<bool> mKillNow{true};
    std::thread mThread;

    /* SceAudioOut mixer thread */
    int mixerProc();

    DEF_NEWDEL(SceAudioOutBackend)
};

SceAudioOutBackend::~SceAudioOutBackend() {
    int ok{-1};

    /* be sure we're not trying to kill ourselves twice a row */
    if (mDeviceID >= 0) {
        TRACE("SceAudioOutBackend dtor");
        /* will wait for the thread to quit gracefully */
        stop();
        /* kill it with fire */
        TRACE("SceAudioOutBackend closing audio handle...");
        ok = sceAudioOutClose(mDeviceID);
        if (ok < 0) {
            /* uh oh.... we did wait and we're unable to close the port? */
            ERR("SceAudioOut Port closure failure 0x%X", ok);
        }
        /* unset the handle so it's never used again */
        mDeviceID = -1;
    }

    TRACE("SceAudioOutBackend dtor ok");
}

int SceAudioOutBackend::mixerProc() {
    /*
        not sure if this is even possible
    */
    int ok{-1}, aaa{256}; /* SCE error code or 0 */
    ok = scePthreadRename(scePthreadSelf(), MIXER_THREAD_NAME);
    if (ok < 0) {
        ERR("scePthreadRename fail: 0x%X", ok);
        // not returning since this isn't fatal
    }
    ok = scePthreadSetschedparam(scePthreadSelf(), 3, &aaa);
    if (ok < 0) {
        ERR("scePthreadSetschedparam fail: 0x%X", ok);
        // not returning since this isn't fatal
    }

    /* i have no idea what I'm doing */
    const size_t frame_step{mDevice->channelsFromFmt()};

    while (!mKillNow.load(std::memory_order_acquire) && mDevice->Connected.load(std::memory_order_acquire)) {
        mDevice->renderSamples(mBuffer.data(), static_cast<uint>(mBuffer.size() / mFrameSize), frame_step);
        
        /* should output GRAIN(256) * CHANNCOUNT() */
        ok = sceAudioOutOutput(mDeviceID, mBuffer.data());
        if (ok < 0) {
            /* failed to output sound! */
            mDevice->handleDisconnect("SceAudioOutError .data() 0x%X", ok);
            break;
        }
    }

    /* wait for samples to finish playing (if any) */
    ok = sceAudioOutOutput(mDeviceID, nullptr);
    if (ok < 0) {
        ERR("SceAudioOutError nullptr wait fail: 0x%X", ok);
    }

    /* no sound should be playing when we reach this line, can return */
    return 0;
}

void SceAudioOutBackend::open(const char *name) {
    int indexInTable{-1},
        i{0},
        userId{-1}, /* target user id */
        ok{-1}, /* SCE error code */
        freq{48000}, /* SceAudioOut only supports 48000hz, nothing more, nothing less */
        sonyDataFmt{-1}, /* SceAudioOut channformat constant */
        alChanFmt{-1}, /* AL channel format constant */
        alDataFmt{-1}, /* AL data format constant */
        scehandle{-1}, /* SceAudioOut port handle */
        porttype{-1},
        fallbackChanFmt{-1},
        fallbackSonySFmt{-1},
        fallbackSonyFFmt{-1};
    
    int usersList[4]{-1, -1, -1, -1};

    if (!name || 0 == strlen(name)) {
        /* assume "MAIN" device as default */
        indexInTable = 0; /* first device index is "MAIN" */
        name = DeviceNames[indexInTable].c_str();
    }
    else {
        for (const auto& sname : DeviceNames) {
            if (sname == name) {
                indexInTable = i;
                break;
            }

            ++i;
        }
    }

    if (indexInTable < 0) {
        throw al::backend_exception{al::backend_error::NoDevice, "Invalid device name '%s'", name};
    }

    /* either SYSTEM or a 1-index in the list */
    userId = DeviceUserIds[indexInTable];
    if (userId != 0xFF /* SYSTEM */) {
        /* only fetch users if we have to, since user stuff is multi-threaded */
        ok = sceUserServiceGetLoginUserIdList(usersList);
        if (ok < 0) {
            throw al::backend_exception{al::backend_error::DeviceError, "Unable to enumerate users 0x%X", ok};
        }
        /* 1 becomes [0], the first user's index */
        userId = usersList[userId - 1];
    }

    /* negative user ids are invalid, what? maybe not signed in... */
    if (userId < 0) {
        throw al::backend_exception{al::backend_error::DeviceError, "Invalid user id 0x%X", userId};
    }

    /*
        SceAudioOut only supports either Short16LE or Float32LE as data format.
        Mono, Stereo, or 7.1(STD?)

        MAIN - 7.1, stereo, mono
        BGM - 7.1, stereo, mono
        VOICE - stereo, mono
        PERSONAL - stereo, mono
        PADSPK - mono
        AUX - 7.1, stereo, mono
    */
    porttype = DevicePorts[indexInTable];
    // MAIN, BGM, AUX, 7.1 is supported usually
    fallbackChanFmt = DevFmtX71;
    fallbackSonySFmt = 6; // S16 8CH STD
    fallbackSonyFFmt = 7; // Float 8CH STD
    if (porttype == 4) {
        // PADSPK, mono only
        fallbackChanFmt = DevFmtMono;
        fallbackSonySFmt = 0; // S16 Mono
        fallbackSonyFFmt = 3; // Float Mono
    }
    else if (porttype == 2 || porttype == 3) {
        // PERSONAL or VOICE, stereo or mono only
        fallbackChanFmt = DevFmtStereo;
        fallbackSonySFmt = 1; // S16 Stereo
        fallbackSonyFFmt = 4; // Float Stereo
    }

    /* a very very ugly hack... */
    switch (mDevice->FmtType) {
        case DevFmtUByte:
        case DevFmtByte:
        case DevFmtUShort:
        case DevFmtShort: {
            /* use s16 if possible for s16 and smaller types */
            alDataFmt = DevFmtShort;

            switch (mDevice->FmtChans) {
                case DevFmtMono: {
                    sonyDataFmt = 0; /* S16 Mono */
                    alChanFmt = DevFmtMono;
                    break;
                }

                case DevFmtStereo: {
                    if (porttype != 4 /* PADSPK */) {
                        sonyDataFmt = 1; /* S16 Stereo */
                        alChanFmt = DevFmtStereo;
                        break;
                    }
                    /* in case of PADSPK fall through the fallback format. */
                }

                default: {
                    sonyDataFmt = fallbackSonySFmt;
                    alChanFmt = fallbackChanFmt;
                    break;
                }
            }

            break;
        }

        default: {
            /* use float32 for int32 and higher */
            alDataFmt = DevFmtFloat;

            switch (mDevice->FmtChans) {
                case DevFmtMono: {
                    sonyDataFmt = 3; /* Float Mono */
                    alChanFmt = DevFmtMono;
                    break;
                }

                case DevFmtStereo: {
                    if (porttype != 4 /* PADSPK */) {
                        sonyDataFmt = 4; /* Float Stereo */
                        alChanFmt = DevFmtStereo;
                        break;
                    }
                    /* in case of PADSPK fall through the fallback format. */
                }

                default: {
                    sonyDataFmt = fallbackSonyFFmt;
                    alChanFmt = fallbackChanFmt;
                    break;
                }
            }

            break;
        }
    }

    mFrequency = static_cast<uint>(freq);
    mFmtChans = static_cast<DevFmtChannels>(alChanFmt);
    mFmtType = static_cast<DevFmtType>(alDataFmt);
    mFrameSize = BytesFromDevFmt(mFmtType) * ChannelsFromDevFmt(mFmtChans, mDevice->mAmbiOrder);

    /*
        Valid port granularity values:
    */
    const uint validGranulas[]{
        /* 256, 512,    768,     1024,    1280,    1536,    1792,    2048 */
        64 * 4, 64 * 8, 64 * 12, 64 * 16, 64 * 20, 64 * 24, 64 * 28, 64 * 32
    };
    const uint validGranulasLen{sizeof(validGranulas) / sizeof(validGranulas[0])};

    /* make a very quick initial guess */
    mUpdateSize =
        (mDevice->UpdateSize <= validGranulas[0])
            ? validGranulas[0]
            : validGranulas[validGranulasLen - 1];

    /* and then attempt to round to largest (or the same) if in-between ... */
    for (size_t g{0}; g < (validGranulasLen - 1); ++g) {
        auto v{validGranulas[g]}, vn{validGranulas[g + 1]};

        if (mDevice->UpdateSize > v && mDevice->UpdateSize <= vn) {
            mUpdateSize = vn;
            break;
        }
    }
    /*
        so if you pass 9999 it will choose 2048,
        if you pass 1024 which is a valid len, it will choose 1024,
        if you pass 960 it will choose 1024,
        if you pass 100 it will choose 256,
        if you pass 257 it will choose 512 and so on and so on...
    */

    TRACE("lol userId=%d,porttype=%d,updsize=%u,mfreq=%u,datafmt=%d", userId, porttype, mUpdateSize, mFrequency, sonyDataFmt);
    scehandle = sceAudioOutOpen(userId, porttype, 0, mUpdateSize, mFrequency, static_cast<uint>(sonyDataFmt));
    if (scehandle < 0) {
        throw al::backend_exception{al::backend_error::DeviceError, "Unable to open audio handle 0x%X", scehandle};
    }

    /* a buffer to hold one update */
    mBuffer.resize(mUpdateSize * mFrameSize);

    /* fill the buffer with zeroes */
    std::fill(mBuffer.begin(), mBuffer.end(), al::byte{});
    /* sooo for a stereo s16 this buffer should be 256*2 in size */

    mDeviceID = scehandle;
    mDevice->DeviceName = name;
    mDevice->Frequency = mFrequency;
    mDevice->FmtChans = mFmtChans;
    mDevice->FmtType = mFmtType;
    mDevice->UpdateSize = mUpdateSize;
    mDevice->BufferSize = mUpdateSize;
}

bool SceAudioOutBackend::reset() {
    mDevice->Frequency = mFrequency;
    mDevice->FmtChans = mFmtChans;
    mDevice->FmtType = mFmtType;
    mDevice->UpdateSize = mUpdateSize;
    mDevice->BufferSize = mUpdateSize;
    setDefaultWFXChannelOrder();
    return true;
}

void SceAudioOutBackend::start() {
    try {
        TRACE("SceAudioOutBackend start() is called.");
        mKillNow.store(false, std::memory_order_release);
        TRACE("SceAudioOutBackend right about to start the mixer thread...");
        mThread = std::thread{std::mem_fn(&SceAudioOutBackend::mixerProc), this};
    }
    catch (std::exception& e) {
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start mixing thread: %s", e.what()};
    }
}

void SceAudioOutBackend::stop() {
    TRACE("SceAudioOutBackend stop() is called.");

    if (mKillNow.exchange(true, std::memory_order_acq_rel) || !mThread.joinable()) {
        TRACE("SceAudioOutBackend stop(): the backend is already stopped.");
        return;
    }

    /* the thread will wait for SceAudio to complete and only then return */
    TRACE("SceAudioOutBackend waiting for the thread...");
    mThread.join();
    TRACE("SceAudioOutBackend stopped...");
}

struct SceAudioInCapture final : public BackendBase {
    SceAudioInCapture(DeviceBase *device) noexcept : BackendBase{device} { }
    ~SceAudioInCapture() override;

    /* AudioIn record thread func */
    int recordProc();

    void open(const char *name) override;
    void start() override;
    void stop() override;
    void captureSamples(al::byte *buffer, uint samples) override;
    uint availableSamples() override;

    /* The output from mCaptureBuffer is written into mRing at once */
    RingBufferPtr mRing{nullptr};

    std::atomic<bool> mKillNow{true};
    std::thread mThread;

    int mDeviceID{-1};
    DevFmtType mFmtType{};
    DevFmtChannels mFmtChannels{};
    uint mFrequency{};
    uint mFrameSize{};
    uint mUpdateSize{};

    /* stores up to one AudioIn update (or less, if there's less samples) */
    al::vector<al::byte> mCaptureBuffer{};

    DEF_NEWDEL(SceAudioInCapture)
};

const std::string CaptureDeviceNames[] = {
    /* all names require a user id */
    "GENERAL1", "GENERAL2", "GENERAL3", "GENERAL4",
    "VOICE_CHAT1", "VOICE_CHAT2", "VOICE_CHAT3", "VOICE_CHAT4",
    "VOICE_RECOGNITION1", "VOICE_RECOGNITION2", "VOICE_RECOGNITION3", "VOICE_RECOGNITION4"
};

const int CaptureDevicePorts[] = {
    1, 1, 1, 1,
    0, 0, 0, 0,
    5, 5, 5, 5
};

const int CaptureDeviceUserIds[] = {
    1, 2, 3, 4,
    1, 2, 3, 4,
    1, 2, 3, 4
};

SceAudioInCapture::~SceAudioInCapture() {
    int ok{-1};

    if (mDeviceID >= 0) {
        TRACE("Stopping SceAudioInCapture from dtor");
        /* must wait until all processing is done, the thread will do that for us */
        stop();
        /* kill it with fire */
        TRACE("Closing audio in handle...");
        ok = sceAudioInClose(mDeviceID);
        if (ok < 0) {
            ERR("sceAudioInClose error 0x%X", ok);
        }
        /* unset the handle so it's never used again... */
        mDeviceID = -1;
    }

    TRACE("SceAudioInCapture dtor okay");
}

int SceAudioInCapture::recordProc() {
    /*
        does this even work
    */
    int ok{-1}, aaa{256}; /* SCE error code or 0 */
    ok = scePthreadRename(scePthreadSelf(), RECORD_THREAD_NAME);
    if (ok < 0) {
        ERR("scePthreadRename fail: 0x%X", ok);
        // not returning since this isn't fatal
    }
    ok = scePthreadSetschedparam(scePthreadSelf(), 3, &aaa);
    if (ok < 0) {
        ERR("scePthreadSetschedparam fail: 0x%X", ok);
        // not returning since this isn't fatal
    }

    while (!mKillNow.load(std::memory_order_acquire) && mDevice->Connected.load(std::memory_order_acquire)) {
        /* get the current state of the input port */
        ok = sceAudioInGetSilentState(mDeviceID);
        if (ok < 0) {
            mDevice->handleDisconnect("SceAudioInCapture get silent state fail: 0x%X", ok);
            break;
        }

        if (ok != 0) {
            /*
                the port is fine (`ok` is positive), but is either occupied or in low priority...
                this means it may become available later under the same handle,
                it's up to the application to handle that. if no samples are available in... 10 seconds for example
                either stop capturing or warn the user.
            */
            continue;
        }

        /* read one port update into a temp buffer */
        ok = sceAudioInInput(mDeviceID, mCaptureBuffer.data());
        if (ok < 0) {
            mDevice->handleDisconnect("SceAudioInCapture backend read fail: 0x%X", ok);
            break;
        }

        /* `ok` should be the amount of samples read, no need to divide */
        mRing->write(mCaptureBuffer.data(), static_cast<size_t>(ok));
    }

    /* must wait until all input is sent for the port to close */
    ok = sceAudioInInput(mDeviceID, nullptr);
    if (ok < 0) {
        ERR("SceAudioInCapture wait fail: 0x%X", ok);
    }

    return 0;
}

void SceAudioInCapture::open(const char *name) {
    int indexInTable{-1},
        ok{-1},
        i{0},
        sonyDataFmt{-1},
        alChannFmt{-1},
        alDataFmt{-1},
        freq{16000}, /* SceAudioIn only supports this frequency */
        scehandle{-1},
        userId{-1},
        granularity{256},
        type{-1};
    
    int usersList[4]{-1,-1,-1,-1};

    if (!name || 0 == strlen(name)) {
        // assume "GENERAL1" port by default
        // if the game has "App does not support initial user sign out"
        // then the first user index will point to the initial user and it's technically valid.
        // but the game really should specify the user here manually.... :|
        indexInTable = 0;
        name = CaptureDeviceNames[indexInTable].c_str();
    }
    else {
        for (const auto& sname : CaptureDeviceNames) {
            if (sname == name) {
                indexInTable = i;
                break;
            }

            ++i;
        }
    }

    if (indexInTable < 0) {
        throw al::backend_exception{al::backend_error::NoDevice, "Invalid device name '%s'", name};
    }

    /* SceAudioIn always requires a valid user handle, SYSTEM is invalid. */
    ok = sceUserServiceGetLoginUserIdList(usersList);
    if (ok < 0) {
        throw al::backend_exception{al::backend_error::DeviceError, "Unable to enumerate users 0x%X", ok};
    }

    userId = CaptureDeviceUserIds[indexInTable];
    userId = usersList[userId - 1];
    if (userId < 0) {
        throw al::backend_exception{al::backend_error::DeviceError, "Invalid user id 0x%X", ok};
    }

    type = CaptureDevicePorts[indexInTable];

    /* Either use the regular s16 mono 16khz or the HQ s16 stereo 48khz port */
    alDataFmt = static_cast<int>(mDevice->FmtType);
    alChannFmt = static_cast<int>(mDevice->FmtChans);
    freq = static_cast<int>(mDevice->Frequency);
    /* a very very ugly hack */
    if (alDataFmt == DevFmtShort && alChannFmt == DevFmtMono && freq == 16000) {
        sonyDataFmt = 0; /* S16 Mono, Normal port only */
        granularity = 256;
    }
    else if (alDataFmt == DevFmtShort && alChannFmt == DevFmtStereo && freq == 48000) {
        sonyDataFmt = 2; /* S16 Stereo, Hq port only */
        granularity = 128;
    }
    else {
        /* too lazy to resample stuff, meh */
        throw al::backend_exception{al::backend_error::DeviceError,
            "Invalid capture parameters, you MUST use freq=16000,format=AL_FORMAT_MONO16 or freq=48000,format=AL_FORMAT_STEREO16."
        };
    }

    mFmtType = static_cast<DevFmtType>(alDataFmt);
    mFmtChannels = static_cast<DevFmtChannels>(alChannFmt);
    mFrequency = static_cast<uint>(freq);
    /* let's hope this is correct... */
    mFrameSize = BytesFromDevFmt(mFmtType) * ChannelsFromDevFmt(mFmtChannels, mDevice->mAmbiOrder);
    mUpdateSize = static_cast<uint>(granularity);

    TRACE("lol userId=%d,type=%d,updsiz=%u,freq=%u,sonyfmt=%d", userId, type, mUpdateSize, mFrequency, sonyDataFmt);
    if (sonyDataFmt == 2) {
        scehandle = sceAudioInHqOpen(
            userId,
            static_cast<uint>(type), 
            0,
            mUpdateSize,
            mFrequency,
            static_cast<uint>(sonyDataFmt)
        );
    }
    else {
        scehandle = sceAudioInOpen(
            userId,
            static_cast<uint>(type), 
            0,
            mUpdateSize,
            mFrequency,
            static_cast<uint>(sonyDataFmt)
        );
    }

    if (scehandle < 0) {
        throw al::backend_exception{al::backend_error::DeviceError,
            "sceAudioInOpen failure: 0x%X", scehandle};
    }

    /*
        Ensure that the BufferSize is at least large enough to hold one Update.
    */
    mDevice->UpdateSize = mUpdateSize;
    mDevice->BufferSize = maxu(mDevice->BufferSize, mUpdateSize);
    mRing = RingBuffer::Create(static_cast<size_t>(mDevice->BufferSize), mFrameSize, false);

    /* allocate a bytebuffer to store one update */
    mCaptureBuffer.resize(mUpdateSize * mFrameSize);
    std::fill(mCaptureBuffer.begin(), mCaptureBuffer.end(), al::byte{});

    mDevice->FmtType = mFmtType;
    mDevice->FmtChans = mFmtChannels;
    mDevice->Frequency = mFrequency;
    mDevice->DeviceName = name;
    mDeviceID = scehandle;
}

void SceAudioInCapture::start() {
    try {
        TRACE("SceAudioInCapture starting capture thread...");
        mKillNow.store(false, std::memory_order_release);
        TRACE("SceAudioInCapture right about to start the capture thread...");
        mThread = std::thread{std::mem_fn(&SceAudioInCapture::recordProc), this};
    }
    catch(std::exception& e) {
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start capture thread: %s", e.what()};
    }
}

void SceAudioInCapture::stop() {
    TRACE("SceAudioInCapture stopping...");
    if (mKillNow.exchange(true, std::memory_order_acq_rel) || !mThread.joinable()) {
        TRACE("SceAudioInCapture the thread is already stopped.");
        return;
    }
    
    TRACE("SceAudioInCapture about to join record thread...");
    mThread.join();
    TRACE("SceAudioInCapture stop successful.");
}

uint SceAudioInCapture::availableSamples() {
    return static_cast<uint>(mRing->readSpace());
}

void SceAudioInCapture::captureSamples(al::byte *buffer, uint samples) {
    mRing->read(buffer, samples);
}

} // namespace

BackendFactory &SceAudioBackendFactory::getFactory() {
    static SceAudioBackendFactory factory{};
    return factory;
}

bool SceAudioBackendFactory::init() {
    int ok{-1};

    TRACE("SceAudio backend is initializing");

    ok = sceUserServiceInitialize(nullptr);
    /* allow double-initialization SCE error code just in case some code already did that for us... */
    if (ok < 0 && ok != static_cast<int>(0x80960003)) {
        ERR("SceUserService init fail 0x%X", ok);
        return false;
    }

    ok = sceAudioOutInit();
    if (ok < 0 && ok != static_cast<int>(0x8026000e)) {
        ERR("SceAudioOut init fail 0x%X", ok);
        return false;
    }

    TRACE("Backend init() OK...");
    return true;
}

bool SceAudioBackendFactory::querySupport(BackendType type) {
    return
        type == BackendType::Playback ||
        type == BackendType::Capture;
}

std::string SceAudioBackendFactory::probe(BackendType type) {
    std::string outnames;

    if (type == BackendType::Playback) {
        for (const auto& sname : DeviceNames) {
            /* should include the nullbyte */
            outnames.append(sname.c_str(), sname.length() + 1);
        }
    }
    else if (type == BackendType::Capture) {
        for (const auto& sname : CaptureDeviceNames) {
            /* should include the nullbyte */
            outnames.append(sname.c_str(), sname.length() + 1);
        }
    }

    return outnames;
}

BackendPtr SceAudioBackendFactory::createBackend(DeviceBase *device, BackendType type) {
    if (type == BackendType::Playback) {
        return BackendPtr{new SceAudioOutBackend{device}};
    }
    else if (type == BackendType::Capture) {
        return BackendPtr{new SceAudioInCapture{device}};
    }

    ERR("SceAudioOutBackendFactory unsupported backend type.");
    return BackendPtr{};
}
