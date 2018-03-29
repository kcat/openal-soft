/*
 * An example showing how to play a stream sync'd to video, using ffmpeg.
 *
 * Requires C++11.
 */

#include <condition_variable>
#include <functional>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <limits>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <mutex>
#include <deque>
#include <array>
#include <cmath>
#include <string>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavutil/time.h"
#include "libavutil/pixfmt.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
}

#include "SDL.h"

#include "AL/alc.h"
#include "AL/al.h"
#include "AL/alext.h"

extern "C" {
#ifndef AL_SOFT_map_buffer
#define AL_SOFT_map_buffer 1
typedef unsigned int ALbitfieldSOFT;
#define AL_MAP_READ_BIT_SOFT                     0x00000001
#define AL_MAP_WRITE_BIT_SOFT                    0x00000002
#define AL_MAP_PERSISTENT_BIT_SOFT               0x00000004
#define AL_PRESERVE_DATA_BIT_SOFT                0x00000008
typedef void (AL_APIENTRY*LPALBUFFERSTORAGESOFT)(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq, ALbitfieldSOFT flags);
typedef void* (AL_APIENTRY*LPALMAPBUFFERSOFT)(ALuint buffer, ALsizei offset, ALsizei length, ALbitfieldSOFT access);
typedef void (AL_APIENTRY*LPALUNMAPBUFFERSOFT)(ALuint buffer);
typedef void (AL_APIENTRY*LPALFLUSHMAPPEDBUFFERSOFT)(ALuint buffer, ALsizei offset, ALsizei length);
#endif

#ifndef AL_SOFT_events
#define AL_SOFT_events 1
#define AL_EVENT_CALLBACK_FUNCTION_SOFT          0x1220
#define AL_EVENT_CALLBACK_USER_PARAM_SOFT        0x1221
#define AL_EVENT_TYPE_BUFFER_COMPLETED_SOFT      0x1222
#define AL_EVENT_TYPE_SOURCE_STATE_CHANGED_SOFT  0x1223
#define AL_EVENT_TYPE_ERROR_SOFT                 0x1224
#define AL_EVENT_TYPE_PERFORMANCE_SOFT           0x1225
#define AL_EVENT_TYPE_DEPRECATED_SOFT            0x1226
#define AL_EVENT_TYPE_DISCONNECTED_SOFT          0x1227
typedef void (AL_APIENTRY*ALEVENTPROCSOFT)(ALenum eventType, ALuint object, ALuint param,
                                           ALsizei length, const ALchar *message,
                                           void *userParam);
typedef void (AL_APIENTRY*LPALEVENTCONTROLSOFT)(ALsizei count, const ALenum *types, ALboolean enable);
typedef void (AL_APIENTRY*LPALEVENTCALLBACKSOFT)(ALEVENTPROCSOFT callback, void *userParam);
typedef void* (AL_APIENTRY*LPALGETPOINTERSOFT)(ALenum pname);
typedef void (AL_APIENTRY*LPALGETPOINTERVSOFT)(ALenum pname, void **values);
#endif
}

namespace {

using nanoseconds = std::chrono::nanoseconds;
using microseconds = std::chrono::microseconds;
using milliseconds = std::chrono::milliseconds;
using seconds = std::chrono::seconds;
using seconds_d64 = std::chrono::duration<double>;

const std::string AppName("alffplay");

bool EnableDirectOut = false;
LPALGETSOURCEI64VSOFT alGetSourcei64vSOFT;
LPALCGETINTEGER64VSOFT alcGetInteger64vSOFT;

LPALBUFFERSTORAGESOFT alBufferStorageSOFT;
LPALMAPBUFFERSOFT alMapBufferSOFT;
LPALUNMAPBUFFERSOFT alUnmapBufferSOFT;

LPALEVENTCONTROLSOFT alEventControlSOFT;
LPALEVENTCALLBACKSOFT alEventCallbackSOFT;

const seconds AVNoSyncThreshold(10);

const milliseconds VideoSyncThreshold(10);
#define VIDEO_PICTURE_QUEUE_SIZE 16

const seconds_d64 AudioSyncThreshold(0.03);
const milliseconds AudioSampleCorrectionMax(50);
/* Averaging filter coefficient for audio sync. */
#define AUDIO_DIFF_AVG_NB 20
const double AudioAvgFilterCoeff = std::pow(0.01, 1.0/AUDIO_DIFF_AVG_NB);
/* Per-buffer size, in time */
const milliseconds AudioBufferTime(20);
/* Buffer total size, in time (should be divisible by the buffer time) */
const milliseconds AudioBufferTotalTime(800);

#define MAX_QUEUE_SIZE (15 * 1024 * 1024) /* Bytes of compressed data to keep queued */

enum {
    FF_UPDATE_EVENT = SDL_USEREVENT,
    FF_REFRESH_EVENT,
    FF_MOVIE_DONE_EVENT
};

enum class SyncMaster {
    Audio,
    Video,
    External,

    Default = External
};


inline microseconds get_avtime()
{ return microseconds(av_gettime()); }

/* Define unique_ptrs to auto-cleanup associated ffmpeg objects. */
struct AVIOContextDeleter {
    void operator()(AVIOContext *ptr) { avio_closep(&ptr); }
};
using AVIOContextPtr = std::unique_ptr<AVIOContext,AVIOContextDeleter>;

struct AVFormatCtxDeleter {
    void operator()(AVFormatContext *ptr) { avformat_close_input(&ptr); }
};
using AVFormatCtxPtr = std::unique_ptr<AVFormatContext,AVFormatCtxDeleter>;

struct AVCodecCtxDeleter {
    void operator()(AVCodecContext *ptr) { avcodec_free_context(&ptr); }
};
using AVCodecCtxPtr = std::unique_ptr<AVCodecContext,AVCodecCtxDeleter>;

struct AVFrameDeleter {
    void operator()(AVFrame *ptr) { av_frame_free(&ptr); }
};
using AVFramePtr = std::unique_ptr<AVFrame,AVFrameDeleter>;

struct SwrContextDeleter {
    void operator()(SwrContext *ptr) { swr_free(&ptr); }
};
using SwrContextPtr = std::unique_ptr<SwrContext,SwrContextDeleter>;

struct SwsContextDeleter {
    void operator()(SwsContext *ptr) { sws_freeContext(ptr); }
};
using SwsContextPtr = std::unique_ptr<SwsContext,SwsContextDeleter>;


class PacketQueue {
    std::deque<AVPacket> mPackets;
    size_t mTotalSize{0};

public:
    ~PacketQueue() { clear(); }

    bool empty() const noexcept { return mPackets.empty(); }
    size_t totalSize() const noexcept { return mTotalSize; }

    void put(const AVPacket *pkt)
    {
        mPackets.push_back(AVPacket{});
        if(av_packet_ref(&mPackets.back(), pkt) != 0)
            mPackets.pop_back();
        else
            mTotalSize += mPackets.back().size;
    }

    AVPacket *front() noexcept
    { return &mPackets.front(); }

    void pop()
    {
        AVPacket *pkt = &mPackets.front();
        mTotalSize -= pkt->size;
        av_packet_unref(pkt);
        mPackets.pop_front();
    }

    void clear()
    {
        for(AVPacket &pkt : mPackets)
            av_packet_unref(&pkt);
        mPackets.clear();
        mTotalSize = 0;
    }
};


struct MovieState;

struct AudioState {
    MovieState &mMovie;

    AVStream *mStream{nullptr};
    AVCodecCtxPtr mCodecCtx;

    std::mutex mQueueMtx;
    std::condition_variable mQueueCond;

    /* Used for clock difference average computation */
    seconds_d64 mClockDiffAvg{0};

    /* Time of the next sample to be buffered */
    nanoseconds mCurrentPts{0};

    /* Device clock time that the stream started at. */
    nanoseconds mDeviceStartTime{nanoseconds::min()};

    /* Decompressed sample frame, and swresample context for conversion */
    AVFramePtr    mDecodedFrame;
    SwrContextPtr mSwresCtx;

    /* Conversion format, for what gets fed to OpenAL */
    int            mDstChanLayout{0};
    AVSampleFormat mDstSampleFmt{AV_SAMPLE_FMT_NONE};

    /* Storage of converted samples */
    uint8_t *mSamples{nullptr};
    int mSamplesLen{0}; /* In samples */
    int mSamplesPos{0};
    int mSamplesMax{0};

    /* OpenAL format */
    ALenum mFormat{AL_NONE};
    ALsizei mFrameSize{0};

    std::mutex mSrcMutex;
    std::condition_variable mSrcCond;
    std::atomic_flag mConnected;
    ALuint mSource{0};
    std::vector<ALuint> mBuffers;
    ALsizei mBufferIdx{0};

    AudioState(MovieState &movie) : mMovie(movie)
    { mConnected.test_and_set(std::memory_order_relaxed); }
    ~AudioState()
    {
        if(mSource)
            alDeleteSources(1, &mSource);
        if(!mBuffers.empty())
            alDeleteBuffers(mBuffers.size(), mBuffers.data());

        av_freep(&mSamples);
    }

    static void AL_APIENTRY EventCallback(ALenum eventType, ALuint object, ALuint param,
                                          ALsizei length, const ALchar *message,
                                          void *userParam);

    nanoseconds getClockNoLock();
    nanoseconds getClock()
    {
        std::lock_guard<std::mutex> lock(mSrcMutex);
        return getClockNoLock();
    }

    bool isBufferFilled();
    void startPlayback();

    int getSync();
    int decodeFrame();
    bool readAudio(uint8_t *samples, int length);

    int handler();
};

struct VideoState {
    MovieState &mMovie;

    AVStream *mStream{nullptr};
    AVCodecCtxPtr mCodecCtx;

    std::mutex mQueueMtx;
    std::condition_variable mQueueCond;

    nanoseconds mClock{0};
    nanoseconds mFrameTimer{0};
    nanoseconds mFrameLastPts{0};
    nanoseconds mFrameLastDelay{0};
    nanoseconds mCurrentPts{0};
    /* time (av_gettime) at which we updated mCurrentPts - used to have running video pts */
    microseconds mCurrentPtsTime{0};

    /* Decompressed video frame, and swscale context for conversion */
    AVFramePtr    mDecodedFrame;
    SwsContextPtr mSwscaleCtx;

    struct Picture {
        SDL_Texture *mImage{nullptr};
        int mWidth{0}, mHeight{0}; /* Logical image size (actual size may be larger) */
        std::atomic<bool> mUpdated{false};
        nanoseconds mPts{0};

        ~Picture()
        {
            if(mImage)
                SDL_DestroyTexture(mImage);
            mImage = nullptr;
        }
    };
    std::array<Picture,VIDEO_PICTURE_QUEUE_SIZE> mPictQ;
    size_t mPictQSize{0}, mPictQRead{0}, mPictQWrite{0};
    std::mutex mPictQMutex;
    std::condition_variable mPictQCond;
    bool mFirstUpdate{true};
    std::atomic<bool> mEOS{false};
    std::atomic<bool> mFinalUpdate{false};

    VideoState(MovieState &movie) : mMovie(movie) { }

    nanoseconds getClock();
    bool isBufferFilled();

    static Uint32 SDLCALL sdl_refresh_timer_cb(Uint32 interval, void *opaque);
    void schedRefresh(milliseconds delay);
    void display(SDL_Window *screen, SDL_Renderer *renderer);
    void refreshTimer(SDL_Window *screen, SDL_Renderer *renderer);
    void updatePicture(SDL_Window *screen, SDL_Renderer *renderer);
    int queuePicture(nanoseconds pts);
    int handler();
};

struct MovieState {
    AVIOContextPtr mIOContext;
    AVFormatCtxPtr mFormatCtx;

    SyncMaster mAVSyncType{SyncMaster::Default};

    microseconds mClockBase{0};
    std::atomic<bool> mPlaying{false};

    std::mutex mSendMtx;
    std::condition_variable mSendCond;
    /* NOTE: false/clear = need data, true/set = no data needed */
    std::atomic_flag mSendDataGood;

    std::atomic<bool> mQuit{false};

    AudioState mAudio;
    VideoState mVideo;

    std::thread mParseThread;
    std::thread mAudioThread;
    std::thread mVideoThread;

    std::string mFilename;

    MovieState(std::string fname)
      : mAudio(*this), mVideo(*this), mFilename(std::move(fname))
    { }
    ~MovieState()
    {
        mQuit = true;
        if(mParseThread.joinable())
            mParseThread.join();
    }

    static int decode_interrupt_cb(void *ctx);
    bool prepare();
    void setTitle(SDL_Window *window);

    nanoseconds getClock();

    nanoseconds getMasterClock();

    nanoseconds getDuration();

    int streamComponentOpen(int stream_index);
    int parse_handler();
};


nanoseconds AudioState::getClockNoLock()
{
    // The audio clock is the timestamp of the sample currently being heard.
    if(alcGetInteger64vSOFT)
    {
        // If device start time = min, we aren't playing yet.
        if(mDeviceStartTime == nanoseconds::min())
            return nanoseconds::zero();

        // Get the current device clock time and latency.
        auto device = alcGetContextsDevice(alcGetCurrentContext());
        ALCint64SOFT devtimes[2] = {0,0};
        alcGetInteger64vSOFT(device, ALC_DEVICE_CLOCK_LATENCY_SOFT, 2, devtimes);
        auto latency = nanoseconds(devtimes[1]);
        auto device_time = nanoseconds(devtimes[0]);

        // The clock is simply the current device time relative to the recorded
        // start time. We can also subtract the latency to get more a accurate
        // position of where the audio device actually is in the output stream.
        return device_time - mDeviceStartTime - latency;
    }

    /* The source-based clock is based on 4 components:
     * 1 - The timestamp of the next sample to buffer (mCurrentPts)
     * 2 - The length of the source's buffer queue
     *     (AudioBufferTime*AL_BUFFERS_QUEUED)
     * 3 - The offset OpenAL is currently at in the source (the first value
     *     from AL_SAMPLE_OFFSET_LATENCY_SOFT)
     * 4 - The latency between OpenAL and the DAC (the second value from
     *     AL_SAMPLE_OFFSET_LATENCY_SOFT)
     *
     * Subtracting the length of the source queue from the next sample's
     * timestamp gives the timestamp of the sample at the start of the source
     * queue. Adding the source offset to that results in the timestamp for the
     * sample at OpenAL's current position, and subtracting the source latency
     * from that gives the timestamp of the sample currently at the DAC.
     */
    nanoseconds pts = mCurrentPts;
    if(mSource)
    {
        ALint64SOFT offset[2];
        ALint queued;
        ALint status;

        /* NOTE: The source state must be checked last, in case an underrun
         * occurs and the source stops between retrieving the offset+latency
         * and getting the state. */
        if(alGetSourcei64vSOFT)
            alGetSourcei64vSOFT(mSource, AL_SAMPLE_OFFSET_LATENCY_SOFT, offset);
        else
        {
            ALint ioffset;
            alGetSourcei(mSource, AL_SAMPLE_OFFSET, &ioffset);
            offset[0] = (ALint64SOFT)ioffset << 32;
            offset[1] = 0;
        }
        alGetSourcei(mSource, AL_BUFFERS_QUEUED, &queued);
        alGetSourcei(mSource, AL_SOURCE_STATE, &status);

        /* If the source is AL_STOPPED, then there was an underrun and all
         * buffers are processed, so ignore the source queue. The audio thread
         * will put the source into an AL_INITIAL state and clear the queue
         * when it starts recovery. */
        if(status != AL_STOPPED)
        {
            using fixed32 = std::chrono::duration<int64_t,std::ratio<1,(1ll<<32)>>;

            pts -= AudioBufferTime*queued;
            pts += std::chrono::duration_cast<nanoseconds>(
                fixed32(offset[0] / mCodecCtx->sample_rate)
            );
        }
        /* Don't offset by the latency if the source isn't playing. */
        if(status == AL_PLAYING)
            pts -= nanoseconds(offset[1]);
    }

    return std::max(pts, nanoseconds::zero());
}

bool AudioState::isBufferFilled()
{
    /* All of OpenAL's buffer queueing happens under the mSrcMutex lock, as
     * does the source gen. So when we're able to grab the lock and the source
     * is valid, the queue must be full.
     */
    std::lock_guard<std::mutex> lock(mSrcMutex);
    return mSource != 0;
}

void AudioState::startPlayback()
{
    alSourcePlay(mSource);
    if(alcGetInteger64vSOFT)
    {
        using fixed32 = std::chrono::duration<int64_t,std::ratio<1,(1ll<<32)>>;

        // Subtract the total buffer queue time from the current pts to get the
        // pts of the start of the queue.
        nanoseconds startpts = mCurrentPts - AudioBufferTotalTime;
        int64_t srctimes[2]={0,0};
        alGetSourcei64vSOFT(mSource, AL_SAMPLE_OFFSET_CLOCK_SOFT, srctimes);
        auto device_time = nanoseconds(srctimes[1]);
        auto src_offset = std::chrono::duration_cast<nanoseconds>(fixed32(srctimes[0])) /
                          mCodecCtx->sample_rate;

        // The mixer may have ticked and incremented the device time and sample
        // offset, so subtract the source offset from the device time to get
        // the device time the source started at. Also subtract startpts to get
        // the device time the stream would have started at to reach where it
        // is now.
        mDeviceStartTime = device_time - src_offset - startpts;
    }
}

int AudioState::getSync()
{
    if(mMovie.mAVSyncType == SyncMaster::Audio)
        return 0;

    auto ref_clock = mMovie.getMasterClock();
    auto diff = ref_clock - getClockNoLock();

    if(!(diff < AVNoSyncThreshold && diff > -AVNoSyncThreshold))
    {
        /* Difference is TOO big; reset accumulated average */
        mClockDiffAvg = seconds_d64::zero();
        return 0;
    }

    /* Accumulate the diffs */
    mClockDiffAvg = mClockDiffAvg*AudioAvgFilterCoeff + diff;
    auto avg_diff = mClockDiffAvg*(1.0 - AudioAvgFilterCoeff);
    if(avg_diff < AudioSyncThreshold/2.0 && avg_diff > -AudioSyncThreshold)
        return 0;

    /* Constrain the per-update difference to avoid exceedingly large skips */
    diff = std::min<nanoseconds>(std::max<nanoseconds>(diff, -AudioSampleCorrectionMax),
                                 AudioSampleCorrectionMax);
    return (int)std::chrono::duration_cast<seconds>(diff*mCodecCtx->sample_rate).count();
}

int AudioState::decodeFrame()
{
    while(!mMovie.mQuit.load(std::memory_order_relaxed))
    {
        std::unique_lock<std::mutex> lock(mQueueMtx);
        int ret = avcodec_receive_frame(mCodecCtx.get(), mDecodedFrame.get());
        if(ret == AVERROR(EAGAIN))
        {
            mMovie.mSendDataGood.clear(std::memory_order_relaxed);
            std::unique_lock<std::mutex>(mMovie.mSendMtx).unlock();
            mMovie.mSendCond.notify_one();
            do {
                mQueueCond.wait(lock);
                ret = avcodec_receive_frame(mCodecCtx.get(), mDecodedFrame.get());
            } while(ret == AVERROR(EAGAIN));
        }
        lock.unlock();
        if(ret == AVERROR_EOF) break;
        mMovie.mSendDataGood.clear(std::memory_order_relaxed);
        mMovie.mSendCond.notify_one();
        if(ret < 0)
        {
            std::cerr<< "Failed to decode frame: "<<ret <<std::endl;
            return 0;
        }

        if(mDecodedFrame->nb_samples <= 0)
        {
            av_frame_unref(mDecodedFrame.get());
            continue;
        }

        /* If provided, update w/ pts */
        if(mDecodedFrame->best_effort_timestamp != AV_NOPTS_VALUE)
            mCurrentPts = std::chrono::duration_cast<nanoseconds>(
                seconds_d64(av_q2d(mStream->time_base)*mDecodedFrame->best_effort_timestamp)
            );

        if(mDecodedFrame->nb_samples > mSamplesMax)
        {
            av_freep(&mSamples);
            av_samples_alloc(
                &mSamples, nullptr, mCodecCtx->channels,
                mDecodedFrame->nb_samples, mDstSampleFmt, 0
            );
            mSamplesMax = mDecodedFrame->nb_samples;
        }
        /* Return the amount of sample frames converted */
        int data_size = swr_convert(mSwresCtx.get(), &mSamples, mDecodedFrame->nb_samples,
            (const uint8_t**)mDecodedFrame->data, mDecodedFrame->nb_samples
        );

        av_frame_unref(mDecodedFrame.get());
        return data_size;
    }

    return 0;
}

/* Duplicates the sample at in to out, count times. The frame size is a
 * multiple of the template type size.
 */
template<typename T>
static void sample_dup(uint8_t *out, const uint8_t *in, int count, int frame_size)
{
    const T *sample = reinterpret_cast<const T*>(in);
    T *dst = reinterpret_cast<T*>(out);
    if(frame_size == sizeof(T))
        std::fill_n(dst, count, *sample);
    else
    {
        /* NOTE: frame_size is a multiple of sizeof(T). */
        int type_mult = frame_size / sizeof(T);
        int i = 0;
        std::generate_n(dst, count*type_mult,
            [sample,type_mult,&i]() -> T
            {
                T ret = sample[i];
                i = (i+1)%type_mult;
                return ret;
            }
        );
    }
}


bool AudioState::readAudio(uint8_t *samples, int length)
{
    int sample_skip = getSync();
    int audio_size = 0;

    /* Read the next chunk of data, refill the buffer, and queue it
     * on the source */
    length /= mFrameSize;
    while(audio_size < length)
    {
        if(mSamplesLen <= 0 || mSamplesPos >= mSamplesLen)
        {
            int frame_len = decodeFrame();
            if(frame_len <= 0) break;

            mSamplesLen = frame_len;
            mSamplesPos = std::min(mSamplesLen, sample_skip);
            sample_skip -= mSamplesPos;

            // Adjust the device start time and current pts by the amount we're
            // skipping/duplicating, so that the clock remains correct for the
            // current stream position.
            auto skip = nanoseconds(seconds(mSamplesPos)) / mCodecCtx->sample_rate;
            mDeviceStartTime -= skip;
            mCurrentPts += skip;
            continue;
        }

        int rem = length - audio_size;
        if(mSamplesPos >= 0)
        {
            int len = mSamplesLen - mSamplesPos;
            if(rem > len) rem = len;
            memcpy(samples, mSamples + mSamplesPos*mFrameSize, rem*mFrameSize);
        }
        else
        {
            rem = std::min(rem, -mSamplesPos);

            /* Add samples by copying the first sample */
            if((mFrameSize&7) == 0)
                sample_dup<uint64_t>(samples, mSamples, rem, mFrameSize);
            else if((mFrameSize&3) == 0)
                sample_dup<uint32_t>(samples, mSamples, rem, mFrameSize);
            else if((mFrameSize&1) == 0)
                sample_dup<uint16_t>(samples, mSamples, rem, mFrameSize);
            else
                sample_dup<uint8_t>(samples, mSamples, rem, mFrameSize);
        }

        mSamplesPos += rem;
        mCurrentPts += nanoseconds(seconds(rem)) / mCodecCtx->sample_rate;
        samples += rem*mFrameSize;
        audio_size += rem;
    }
    if(audio_size <= 0)
        return false;

    if(audio_size < length)
    {
        int rem = length - audio_size;
        std::fill_n(samples, rem*mFrameSize,
                    (mDstSampleFmt == AV_SAMPLE_FMT_U8) ? 0x80 : 0x00);
        mCurrentPts += nanoseconds(seconds(rem)) / mCodecCtx->sample_rate;
        audio_size += rem;
    }
    return true;
}


void AL_APIENTRY AudioState::EventCallback(ALenum eventType, ALuint object, ALuint param,
                                           ALsizei length, const ALchar *message,
                                           void *userParam)
{
    AudioState *self = reinterpret_cast<AudioState*>(userParam);

    if(eventType == AL_EVENT_TYPE_BUFFER_COMPLETED_SOFT)
    {
        /* Temporarily lock the source mutex to ensure it's not between
         * checking the processed count and going to sleep.
         */
        std::unique_lock<std::mutex>(self->mSrcMutex).unlock();
        self->mSrcCond.notify_one();
        return;
    }

    std::cout<< "---- AL Event on AudioState "<<self<<" ----\nEvent: ";
    switch(eventType)
    {
        case AL_EVENT_TYPE_BUFFER_COMPLETED_SOFT: std::cout<< "Buffer completed"; break;
        case AL_EVENT_TYPE_SOURCE_STATE_CHANGED_SOFT: std::cout<< "Source state changed"; break;
        case AL_EVENT_TYPE_ERROR_SOFT: std::cout<< "API error"; break;
        case AL_EVENT_TYPE_PERFORMANCE_SOFT: std::cout<< "Performance"; break;
        case AL_EVENT_TYPE_DEPRECATED_SOFT: std::cout<< "Deprecated"; break;
        case AL_EVENT_TYPE_DISCONNECTED_SOFT: std::cout<< "Disconnected"; break;
        default: std::cout<< "0x"<<std::hex<<std::setw(4)<<std::setfill('0')<<eventType<<
                             std::dec<<std::setw(0)<<std::setfill(' '); break;
    }
    std::cout<< "\n"
        "Object ID: "<<object<<'\n'<<
        "Parameter: "<<param<<'\n'<<
        "Message: "<<std::string(message, length)<<"\n----"<<
        std::endl;

    if(eventType == AL_EVENT_TYPE_DISCONNECTED_SOFT)
    {
        { std::lock_guard<std::mutex> lock(self->mSrcMutex);
            self->mConnected.clear(std::memory_order_release);
        }
        std::unique_lock<std::mutex>(self->mSrcMutex).unlock();
        self->mSrcCond.notify_one();
    }
}

int AudioState::handler()
{
    const std::array<ALenum,6> types{{
        AL_EVENT_TYPE_BUFFER_COMPLETED_SOFT, AL_EVENT_TYPE_SOURCE_STATE_CHANGED_SOFT,
        AL_EVENT_TYPE_ERROR_SOFT, AL_EVENT_TYPE_PERFORMANCE_SOFT, AL_EVENT_TYPE_DEPRECATED_SOFT,
        AL_EVENT_TYPE_DISCONNECTED_SOFT
    }};
    std::unique_lock<std::mutex> lock(mSrcMutex);
    milliseconds sleep_time = AudioBufferTime / 3;
    ALenum fmt;

    if(alEventControlSOFT)
    {
        alEventControlSOFT(types.size(), types.data(), AL_TRUE);
        alEventCallbackSOFT(EventCallback, this);
        sleep_time = AudioBufferTotalTime;
    }

    /* Find a suitable format for OpenAL. */
    mDstChanLayout = 0;
    if(mCodecCtx->sample_fmt == AV_SAMPLE_FMT_U8 || mCodecCtx->sample_fmt == AV_SAMPLE_FMT_U8P)
    {
        mDstSampleFmt = AV_SAMPLE_FMT_U8;
        mFrameSize = 1;
        if(mCodecCtx->channel_layout == AV_CH_LAYOUT_7POINT1 &&
           alIsExtensionPresent("AL_EXT_MCFORMATS") &&
           (fmt=alGetEnumValue("AL_FORMAT_71CHN8")) != AL_NONE && fmt != -1)
        {
            mDstChanLayout = mCodecCtx->channel_layout;
            mFrameSize *= 8;
            mFormat = fmt;
        }
        if((mCodecCtx->channel_layout == AV_CH_LAYOUT_5POINT1 ||
            mCodecCtx->channel_layout == AV_CH_LAYOUT_5POINT1_BACK) &&
           alIsExtensionPresent("AL_EXT_MCFORMATS") &&
           (fmt=alGetEnumValue("AL_FORMAT_51CHN8")) != AL_NONE && fmt != -1)
        {
            mDstChanLayout = mCodecCtx->channel_layout;
            mFrameSize *= 6;
            mFormat = fmt;
        }
        if(mCodecCtx->channel_layout == AV_CH_LAYOUT_MONO)
        {
            mDstChanLayout = mCodecCtx->channel_layout;
            mFrameSize *= 1;
            mFormat = AL_FORMAT_MONO8;
        }
        if(!mDstChanLayout)
        {
            mDstChanLayout = AV_CH_LAYOUT_STEREO;
            mFrameSize *= 2;
            mFormat = AL_FORMAT_STEREO8;
        }
    }
    if((mCodecCtx->sample_fmt == AV_SAMPLE_FMT_FLT || mCodecCtx->sample_fmt == AV_SAMPLE_FMT_FLTP) &&
       alIsExtensionPresent("AL_EXT_FLOAT32"))
    {
        mDstSampleFmt = AV_SAMPLE_FMT_FLT;
        mFrameSize = 4;
        if(mCodecCtx->channel_layout == AV_CH_LAYOUT_7POINT1 &&
           alIsExtensionPresent("AL_EXT_MCFORMATS") &&
           (fmt=alGetEnumValue("AL_FORMAT_71CHN32")) != AL_NONE && fmt != -1)
        {
            mDstChanLayout = mCodecCtx->channel_layout;
            mFrameSize *= 8;
            mFormat = fmt;
        }
        if((mCodecCtx->channel_layout == AV_CH_LAYOUT_5POINT1 ||
            mCodecCtx->channel_layout == AV_CH_LAYOUT_5POINT1_BACK) &&
           alIsExtensionPresent("AL_EXT_MCFORMATS") &&
           (fmt=alGetEnumValue("AL_FORMAT_51CHN32")) != AL_NONE && fmt != -1)
        {
            mDstChanLayout = mCodecCtx->channel_layout;
            mFrameSize *= 6;
            mFormat = fmt;
        }
        if(mCodecCtx->channel_layout == AV_CH_LAYOUT_MONO)
        {
            mDstChanLayout = mCodecCtx->channel_layout;
            mFrameSize *= 1;
            mFormat = AL_FORMAT_MONO_FLOAT32;
        }
        if(!mDstChanLayout)
        {
            mDstChanLayout = AV_CH_LAYOUT_STEREO;
            mFrameSize *= 2;
            mFormat = AL_FORMAT_STEREO_FLOAT32;
        }
    }
    if(!mDstChanLayout)
    {
        mDstSampleFmt = AV_SAMPLE_FMT_S16;
        mFrameSize = 2;
        if(mCodecCtx->channel_layout == AV_CH_LAYOUT_7POINT1 &&
           alIsExtensionPresent("AL_EXT_MCFORMATS") &&
           (fmt=alGetEnumValue("AL_FORMAT_71CHN16")) != AL_NONE && fmt != -1)
        {
            mDstChanLayout = mCodecCtx->channel_layout;
            mFrameSize *= 8;
            mFormat = fmt;
        }
        if((mCodecCtx->channel_layout == AV_CH_LAYOUT_5POINT1 ||
            mCodecCtx->channel_layout == AV_CH_LAYOUT_5POINT1_BACK) &&
           alIsExtensionPresent("AL_EXT_MCFORMATS") &&
           (fmt=alGetEnumValue("AL_FORMAT_51CHN16")) != AL_NONE && fmt != -1)
        {
            mDstChanLayout = mCodecCtx->channel_layout;
            mFrameSize *= 6;
            mFormat = fmt;
        }
        if(mCodecCtx->channel_layout == AV_CH_LAYOUT_MONO)
        {
            mDstChanLayout = mCodecCtx->channel_layout;
            mFrameSize *= 1;
            mFormat = AL_FORMAT_MONO16;
        }
        if(!mDstChanLayout)
        {
            mDstChanLayout = AV_CH_LAYOUT_STEREO;
            mFrameSize *= 2;
            mFormat = AL_FORMAT_STEREO16;
        }
    }
    void *samples = nullptr;
    ALsizei buffer_len = std::chrono::duration_cast<std::chrono::duration<int>>(
        mCodecCtx->sample_rate * AudioBufferTime).count() * mFrameSize;

    mSamples = NULL;
    mSamplesMax = 0;
    mSamplesPos = 0;
    mSamplesLen = 0;

    mDecodedFrame.reset(av_frame_alloc());
    if(!mDecodedFrame)
    {
        std::cerr<< "Failed to allocate audio frame" <<std::endl;
        goto finish;
    }

    mSwresCtx.reset(swr_alloc_set_opts(nullptr,
        mDstChanLayout, mDstSampleFmt, mCodecCtx->sample_rate,
        mCodecCtx->channel_layout ? mCodecCtx->channel_layout :
            (uint64_t)av_get_default_channel_layout(mCodecCtx->channels),
        mCodecCtx->sample_fmt, mCodecCtx->sample_rate,
        0, nullptr
    ));
    if(!mSwresCtx || swr_init(mSwresCtx.get()) != 0)
    {
        std::cerr<< "Failed to initialize audio converter" <<std::endl;
        goto finish;
    }

    mBuffers.assign(AudioBufferTotalTime / AudioBufferTime, 0);
    alGenBuffers(mBuffers.size(), mBuffers.data());
    alGenSources(1, &mSource);

    if(EnableDirectOut)
        alSourcei(mSource, AL_DIRECT_CHANNELS_SOFT, AL_TRUE);

    if(alGetError() != AL_NO_ERROR)
        goto finish;

    if(!alBufferStorageSOFT)
        samples = av_malloc(buffer_len);
    else
    {
        for(ALuint bufid : mBuffers)
            alBufferStorageSOFT(bufid, mFormat, nullptr, buffer_len, mCodecCtx->sample_rate,
                                AL_MAP_WRITE_BIT_SOFT);
        if(alGetError() != AL_NO_ERROR)
        {
            fprintf(stderr, "Failed to use mapped buffers\n");
            samples = av_malloc(buffer_len);
        }
    }

    while(alGetError() == AL_NO_ERROR && !mMovie.mQuit.load(std::memory_order_relaxed) &&
          mConnected.test_and_set(std::memory_order_relaxed))
    {
        /* First remove any processed buffers. */
        ALint processed;
        alGetSourcei(mSource, AL_BUFFERS_PROCESSED, &processed);
        while(processed > 0)
        {
            std::array<ALuint,4> bids;
            alSourceUnqueueBuffers(mSource, std::min<ALsizei>(bids.size(), processed),
                                   bids.data());
            processed -= std::min<ALsizei>(bids.size(), processed);
        }

        /* Refill the buffer queue. */
        ALint queued;
        alGetSourcei(mSource, AL_BUFFERS_QUEUED, &queued);
        while((ALuint)queued < mBuffers.size())
        {
            ALuint bufid = mBuffers[mBufferIdx];

            uint8_t *ptr = reinterpret_cast<uint8_t*>(
                samples ? samples : alMapBufferSOFT(bufid, 0, buffer_len, AL_MAP_WRITE_BIT_SOFT)
            );
            if(!ptr) break;

            /* Read the next chunk of data, filling the buffer, and queue it on
             * the source */
            bool got_audio = readAudio(ptr, buffer_len);
            if(!samples) alUnmapBufferSOFT(bufid);
            if(!got_audio) break;

            if(samples)
                alBufferData(bufid, mFormat, samples, buffer_len, mCodecCtx->sample_rate);

            alSourceQueueBuffers(mSource, 1, &bufid);
            mBufferIdx = (mBufferIdx+1) % mBuffers.size();
            ++queued;
        }
        if(queued == 0)
            break;

        /* Check that the source is playing. */
        ALint state;
        alGetSourcei(mSource, AL_SOURCE_STATE, &state);
        if(state == AL_STOPPED)
        {
            /* AL_STOPPED means there was an underrun. Clear the buffer queue
             * since this likely means we're late, and rewind the source to get
             * it back into an AL_INITIAL state.
             */
            alSourceRewind(mSource);
            alSourcei(mSource, AL_BUFFER, 0);
            continue;
        }

        /* (re)start the source if needed, and wait for a buffer to finish */
        if(state != AL_PLAYING && state != AL_PAUSED &&
           mMovie.mPlaying.load(std::memory_order_relaxed))
            startPlayback();

        mSrcCond.wait_for(lock, sleep_time);
    }

    alSourceRewind(mSource);
    alSourcei(mSource, AL_BUFFER, 0);

finish:
    av_freep(&samples);

    if(alEventControlSOFT)
    {
        alEventControlSOFT(types.size(), types.data(), AL_FALSE);
        alEventCallbackSOFT(nullptr, nullptr);
    }

    return 0;
}


nanoseconds VideoState::getClock()
{
    /* NOTE: This returns incorrect times while not playing. */
    auto delta = get_avtime() - mCurrentPtsTime;
    return mCurrentPts + delta;
}

bool VideoState::isBufferFilled()
{
    std::unique_lock<std::mutex> lock(mPictQMutex);
    return mPictQSize >= mPictQ.size();
}

Uint32 SDLCALL VideoState::sdl_refresh_timer_cb(Uint32 /*interval*/, void *opaque)
{
    SDL_Event evt{};
    evt.user.type = FF_REFRESH_EVENT;
    evt.user.data1 = opaque;
    SDL_PushEvent(&evt);
    return 0; /* 0 means stop timer */
}

/* Schedules an FF_REFRESH_EVENT event to occur in 'delay' ms. */
void VideoState::schedRefresh(milliseconds delay)
{
    SDL_AddTimer(delay.count(), sdl_refresh_timer_cb, this);
}

/* Called by VideoState::refreshTimer to display the next video frame. */
void VideoState::display(SDL_Window *screen, SDL_Renderer *renderer)
{
    Picture *vp = &mPictQ[mPictQRead];

    if(!vp->mImage)
        return;

    float aspect_ratio;
    int win_w, win_h;
    int w, h, x, y;

    if(mCodecCtx->sample_aspect_ratio.num == 0)
        aspect_ratio = 0.0f;
    else
    {
        aspect_ratio = av_q2d(mCodecCtx->sample_aspect_ratio) * mCodecCtx->width /
                       mCodecCtx->height;
    }
    if(aspect_ratio <= 0.0f)
        aspect_ratio = (float)mCodecCtx->width / (float)mCodecCtx->height;

    SDL_GetWindowSize(screen, &win_w, &win_h);
    h = win_h;
    w = ((int)rint(h * aspect_ratio) + 3) & ~3;
    if(w > win_w)
    {
        w = win_w;
        h = ((int)rint(w / aspect_ratio) + 3) & ~3;
    }
    x = (win_w - w) / 2;
    y = (win_h - h) / 2;

    SDL_Rect src_rect{ 0, 0, vp->mWidth, vp->mHeight };
    SDL_Rect dst_rect{ x, y, w, h };
    SDL_RenderCopy(renderer, vp->mImage, &src_rect, &dst_rect);
    SDL_RenderPresent(renderer);
}

/* FF_REFRESH_EVENT handler called on the main thread where the SDL_Renderer
 * was created. It handles the display of the next decoded video frame (if not
 * falling behind), and sets up the timer for the following video frame.
 */
void VideoState::refreshTimer(SDL_Window *screen, SDL_Renderer *renderer)
{
    if(!mStream)
    {
        if(mEOS)
        {
            mFinalUpdate = true;
            std::unique_lock<std::mutex>(mPictQMutex).unlock();
            mPictQCond.notify_all();
            return;
        }
        schedRefresh(milliseconds(100));
        return;
    }
    if(!mMovie.mPlaying.load(std::memory_order_relaxed))
    {
        schedRefresh(milliseconds(1));
        return;
    }

    std::unique_lock<std::mutex> lock(mPictQMutex);
retry:
    if(mPictQSize == 0)
    {
        if(mEOS)
            mFinalUpdate = true;
        else
            schedRefresh(milliseconds(1));
        lock.unlock();
        mPictQCond.notify_all();
        return;
    }

    Picture *vp = &mPictQ[mPictQRead];
    mCurrentPts = vp->mPts;
    mCurrentPtsTime = get_avtime();

    /* Get delay using the frame pts and the pts from last frame. */
    auto delay = vp->mPts - mFrameLastPts;
    if(delay <= seconds::zero() || delay >= seconds(1))
    {
        /* If incorrect delay, use previous one. */
        delay = mFrameLastDelay;
    }
    /* Save for next frame. */
    mFrameLastDelay = delay;
    mFrameLastPts = vp->mPts;

    /* Update delay to sync to clock if not master source. */
    if(mMovie.mAVSyncType != SyncMaster::Video)
    {
        auto ref_clock = mMovie.getMasterClock();
        auto diff = vp->mPts - ref_clock;

        /* Skip or repeat the frame. Take delay into account. */
        auto sync_threshold = std::min<nanoseconds>(delay, VideoSyncThreshold);
        if(!(diff < AVNoSyncThreshold && diff > -AVNoSyncThreshold))
        {
            if(diff <= -sync_threshold)
                delay = nanoseconds::zero();
            else if(diff >= sync_threshold)
                delay *= 2;
        }
    }

    mFrameTimer += delay;
    /* Compute the REAL delay. */
    auto actual_delay = mFrameTimer - get_avtime();
    if(!(actual_delay >= VideoSyncThreshold))
    {
        /* We don't have time to handle this picture, just skip to the next one. */
        mPictQRead = (mPictQRead+1)%mPictQ.size();
        mPictQSize--;
        goto retry;
    }
    schedRefresh(std::chrono::duration_cast<milliseconds>(actual_delay));

    /* Show the picture! */
    display(screen, renderer);

    /* Update queue for next picture. */
    mPictQRead = (mPictQRead+1)%mPictQ.size();
    mPictQSize--;
    lock.unlock();
    mPictQCond.notify_all();
}

/* FF_UPDATE_EVENT handler, updates the picture's texture. It's called on the
 * main thread where the renderer was created.
 */
void VideoState::updatePicture(SDL_Window *screen, SDL_Renderer *renderer)
{
    Picture *vp = &mPictQ[mPictQWrite];
    bool fmt_updated = false;

    /* allocate or resize the buffer! */
    if(!vp->mImage || vp->mWidth != mCodecCtx->width || vp->mHeight != mCodecCtx->height)
    {
        fmt_updated = true;
        if(vp->mImage)
            SDL_DestroyTexture(vp->mImage);
        vp->mImage = SDL_CreateTexture(
            renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
            mCodecCtx->coded_width, mCodecCtx->coded_height
        );
        if(!vp->mImage)
            std::cerr<< "Failed to create YV12 texture!" <<std::endl;
        vp->mWidth = mCodecCtx->width;
        vp->mHeight = mCodecCtx->height;

        if(mFirstUpdate && vp->mWidth > 0 && vp->mHeight > 0)
        {
            /* For the first update, set the window size to the video size. */
            mFirstUpdate = false;

            int w = vp->mWidth;
            int h = vp->mHeight;
            if(mCodecCtx->sample_aspect_ratio.den != 0)
            {
                double aspect_ratio = av_q2d(mCodecCtx->sample_aspect_ratio);
                if(aspect_ratio >= 1.0)
                    w = (int)(w*aspect_ratio + 0.5);
                else if(aspect_ratio > 0.0)
                    h = (int)(h/aspect_ratio + 0.5);
            }
            SDL_SetWindowSize(screen, w, h);
        }
    }

    if(vp->mImage)
    {
        AVFrame *frame = mDecodedFrame.get();
        void *pixels = nullptr;
        int pitch = 0;

        if(mCodecCtx->pix_fmt == AV_PIX_FMT_YUV420P)
            SDL_UpdateYUVTexture(vp->mImage, nullptr,
                frame->data[0], frame->linesize[0],
                frame->data[1], frame->linesize[1],
                frame->data[2], frame->linesize[2]
            );
        else if(SDL_LockTexture(vp->mImage, nullptr, &pixels, &pitch) != 0)
            std::cerr<< "Failed to lock texture" <<std::endl;
        else
        {
            // Convert the image into YUV format that SDL uses
            int coded_w = mCodecCtx->coded_width;
            int coded_h = mCodecCtx->coded_height;
            int w = mCodecCtx->width;
            int h = mCodecCtx->height;
            if(!mSwscaleCtx || fmt_updated)
            {
                mSwscaleCtx.reset(sws_getContext(
                    w, h, mCodecCtx->pix_fmt,
                    w, h, AV_PIX_FMT_YUV420P, 0,
                    nullptr, nullptr, nullptr
                ));
            }

            /* point pict at the queue */
            uint8_t *pict_data[3];
            pict_data[0] = reinterpret_cast<uint8_t*>(pixels);
            pict_data[1] = pict_data[0] + coded_w*coded_h;
            pict_data[2] = pict_data[1] + coded_w*coded_h/4;

            int pict_linesize[3];
            pict_linesize[0] = pitch;
            pict_linesize[1] = pitch / 2;
            pict_linesize[2] = pitch / 2;

            sws_scale(mSwscaleCtx.get(), (const uint8_t**)frame->data,
                      frame->linesize, 0, h, pict_data, pict_linesize);
            SDL_UnlockTexture(vp->mImage);
        }
    }

    vp->mUpdated.store(true, std::memory_order_release);
    std::unique_lock<std::mutex>(mPictQMutex).unlock();
    mPictQCond.notify_one();
}

int VideoState::queuePicture(nanoseconds pts)
{
    /* Wait until we have space for a new pic */
    std::unique_lock<std::mutex> lock(mPictQMutex);
    while(mPictQSize >= mPictQ.size() && !mMovie.mQuit.load(std::memory_order_relaxed))
        mPictQCond.wait(lock);
    lock.unlock();

    if(mMovie.mQuit.load(std::memory_order_relaxed))
        return -1;

    Picture *vp = &mPictQ[mPictQWrite];

    /* We have to create/update the picture in the main thread  */
    vp->mUpdated.store(false, std::memory_order_relaxed);
    SDL_Event evt{};
    evt.user.type = FF_UPDATE_EVENT;
    evt.user.data1 = this;
    SDL_PushEvent(&evt);

    /* Wait until the picture is updated. */
    lock.lock();
    while(!vp->mUpdated.load(std::memory_order_relaxed))
    {
        if(mMovie.mQuit.load(std::memory_order_relaxed))
            return -1;
        mPictQCond.wait(lock);
    }
    if(mMovie.mQuit.load(std::memory_order_relaxed))
        return -1;
    vp->mPts = pts;

    mPictQWrite = (mPictQWrite+1)%mPictQ.size();
    mPictQSize++;
    lock.unlock();

    return 0;
}

int VideoState::handler()
{
    mDecodedFrame.reset(av_frame_alloc());
    while(!mMovie.mQuit.load(std::memory_order_relaxed))
    {
        std::unique_lock<std::mutex> lock(mQueueMtx);
        /* Decode video frame */
        int ret = avcodec_receive_frame(mCodecCtx.get(), mDecodedFrame.get());
        if(ret == AVERROR(EAGAIN))
        {
            mMovie.mSendDataGood.clear(std::memory_order_relaxed);
            std::unique_lock<std::mutex>(mMovie.mSendMtx).unlock();
            mMovie.mSendCond.notify_one();
            do {
                mQueueCond.wait(lock);
                ret = avcodec_receive_frame(mCodecCtx.get(), mDecodedFrame.get());
            } while(ret == AVERROR(EAGAIN));
        }
        lock.unlock();
        if(ret == AVERROR_EOF) break;
        mMovie.mSendDataGood.clear(std::memory_order_relaxed);
        mMovie.mSendCond.notify_one();
        if(ret < 0)
        {
            std::cerr<< "Failed to decode frame: "<<ret <<std::endl;
            continue;
        }

        /* Get the PTS for this frame. */
        nanoseconds pts;
        if(mDecodedFrame->best_effort_timestamp != AV_NOPTS_VALUE)
            mClock = std::chrono::duration_cast<nanoseconds>(
                seconds_d64(av_q2d(mStream->time_base)*mDecodedFrame->best_effort_timestamp)
            );
        pts = mClock;

        /* Update the video clock to the next expected PTS. */
        auto frame_delay = av_q2d(mCodecCtx->time_base);
        frame_delay += mDecodedFrame->repeat_pict * (frame_delay * 0.5);
        mClock += std::chrono::duration_cast<nanoseconds>(seconds_d64(frame_delay));

        if(queuePicture(pts) < 0)
            break;
        av_frame_unref(mDecodedFrame.get());
    }
    mEOS = true;

    std::unique_lock<std::mutex> lock(mPictQMutex);
    if(mMovie.mQuit.load(std::memory_order_relaxed))
    {
        mPictQRead = 0;
        mPictQWrite = 0;
        mPictQSize = 0;
    }
    while(!mFinalUpdate)
        mPictQCond.wait(lock);

    return 0;
}


int MovieState::decode_interrupt_cb(void *ctx)
{
    return reinterpret_cast<MovieState*>(ctx)->mQuit.load(std::memory_order_relaxed);
}

bool MovieState::prepare()
{
    AVIOContext *avioctx = nullptr;
    AVIOInterruptCB intcb = { decode_interrupt_cb, this };
    if(avio_open2(&avioctx, mFilename.c_str(), AVIO_FLAG_READ, &intcb, nullptr))
    {
        std::cerr<< "Failed to open "<<mFilename <<std::endl;
        return false;
    }
    mIOContext.reset(avioctx);

    /* Open movie file. If avformat_open_input fails it will automatically free
     * this context, so don't set it onto a smart pointer yet.
     */
    AVFormatContext *fmtctx = avformat_alloc_context();
    fmtctx->pb = mIOContext.get();
    fmtctx->interrupt_callback = intcb;
    if(avformat_open_input(&fmtctx, mFilename.c_str(), nullptr, nullptr) != 0)
    {
        std::cerr<< "Failed to open "<<mFilename <<std::endl;
        return false;
    }
    mFormatCtx.reset(fmtctx);

    /* Retrieve stream information */
    if(avformat_find_stream_info(mFormatCtx.get(), nullptr) < 0)
    {
        std::cerr<< mFilename<<": failed to find stream info" <<std::endl;
        return false;
    }

    mVideo.schedRefresh(milliseconds(40));

    mParseThread = std::thread(std::mem_fn(&MovieState::parse_handler), this);
    return true;
}

void MovieState::setTitle(SDL_Window *window)
{
    auto pos1 = mFilename.rfind('/');
    auto pos2 = mFilename.rfind('\\');
    auto fpos = ((pos1 == std::string::npos) ? pos2 :
                 (pos2 == std::string::npos) ? pos1 :
                 std::max(pos1, pos2)) + 1;
    SDL_SetWindowTitle(window, (mFilename.substr(fpos)+" - "+AppName).c_str());
}

nanoseconds MovieState::getClock()
{
    if(!mPlaying.load(std::memory_order_relaxed))
        return nanoseconds::zero();
    return get_avtime() - mClockBase;
}

nanoseconds MovieState::getMasterClock()
{
    if(mAVSyncType == SyncMaster::Video)
        return mVideo.getClock();
    if(mAVSyncType == SyncMaster::Audio)
        return mAudio.getClock();
    return getClock();
}

nanoseconds MovieState::getDuration()
{ return std::chrono::duration<int64_t,std::ratio<1,AV_TIME_BASE>>(mFormatCtx->duration); }

int MovieState::streamComponentOpen(int stream_index)
{
    if(stream_index < 0 || (unsigned int)stream_index >= mFormatCtx->nb_streams)
        return -1;

    /* Get a pointer to the codec context for the stream, and open the
     * associated codec.
     */
    AVCodecCtxPtr avctx(avcodec_alloc_context3(nullptr));
    if(!avctx) return -1;

    if(avcodec_parameters_to_context(avctx.get(), mFormatCtx->streams[stream_index]->codecpar))
        return -1;

    AVCodec *codec = avcodec_find_decoder(avctx->codec_id);
    if(!codec || avcodec_open2(avctx.get(), codec, nullptr) < 0)
    {
        std::cerr<< "Unsupported codec: "<<avcodec_get_name(avctx->codec_id)
                 << " (0x"<<std::hex<<avctx->codec_id<<std::dec<<")" <<std::endl;
        return -1;
    }

    /* Initialize and start the media type handler */
    switch(avctx->codec_type)
    {
        case AVMEDIA_TYPE_AUDIO:
            mAudio.mStream = mFormatCtx->streams[stream_index];
            mAudio.mCodecCtx = std::move(avctx);

            mAudioThread = std::thread(std::mem_fn(&AudioState::handler), &mAudio);
            break;

        case AVMEDIA_TYPE_VIDEO:
            mVideo.mStream = mFormatCtx->streams[stream_index];
            mVideo.mCodecCtx = std::move(avctx);

            mVideoThread = std::thread(std::mem_fn(&VideoState::handler), &mVideo);
            break;

        default:
            return -1;
    }

    return stream_index;
}

int MovieState::parse_handler()
{
    int video_index = -1;
    int audio_index = -1;

    /* Dump information about file onto standard error */
    av_dump_format(mFormatCtx.get(), 0, mFilename.c_str(), 0);

    /* Find the first video and audio streams */
    for(unsigned int i = 0;i < mFormatCtx->nb_streams;i++)
    {
        auto codecpar = mFormatCtx->streams[i]->codecpar;
        if(codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_index < 0)
            video_index = streamComponentOpen(i);
        else if(codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_index < 0)
            audio_index = streamComponentOpen(i);
    }

    if(video_index < 0 && audio_index < 0)
    {
        std::cerr<< mFilename<<": could not open codecs" <<std::endl;
        mQuit = true;
    }

    PacketQueue audio_queue, video_queue;
    bool input_finished = false;

    /* Main packet reading/dispatching loop */
    while(!mQuit.load(std::memory_order_relaxed) && !input_finished)
    {
        AVPacket packet;
        if(av_read_frame(mFormatCtx.get(), &packet) < 0)
            input_finished = true;
        else
        {
            /* Copy the packet into the queue it's meant for. */
            if(packet.stream_index == video_index)
                video_queue.put(&packet);
            else if(packet.stream_index == audio_index)
                audio_queue.put(&packet);
            av_packet_unref(&packet);
        }

        do {
            /* Send whatever queued packets we have. */
            if(!audio_queue.empty())
            {
                std::unique_lock<std::mutex> lock(mAudio.mQueueMtx);
                int ret;
                do {
                    ret = avcodec_send_packet(mAudio.mCodecCtx.get(), audio_queue.front());
                    if(ret != AVERROR(EAGAIN)) audio_queue.pop();
                } while(ret != AVERROR(EAGAIN) && !audio_queue.empty());
                lock.unlock();
                mAudio.mQueueCond.notify_one();
            }
            if(!video_queue.empty())
            {
                std::unique_lock<std::mutex> lock(mVideo.mQueueMtx);
                int ret;
                do {
                    ret = avcodec_send_packet(mVideo.mCodecCtx.get(), video_queue.front());
                    if(ret != AVERROR(EAGAIN)) video_queue.pop();
                } while(ret != AVERROR(EAGAIN) && !video_queue.empty());
                lock.unlock();
                mVideo.mQueueCond.notify_one();
            }
            /* If the queues are completely empty, or it's not full and there's
             * more input to read, go get more.
             */
            size_t queue_size = audio_queue.totalSize() + video_queue.totalSize();
            if(queue_size == 0 || (queue_size < MAX_QUEUE_SIZE && !input_finished))
                break;

            if(!mPlaying.load(std::memory_order_relaxed))
            {
                if((!mAudio.mCodecCtx || mAudio.isBufferFilled()) &&
                   (!mVideo.mCodecCtx || mVideo.isBufferFilled()))
                {
                    /* Set the base time 50ms ahead of the current av time. */
                    mClockBase = get_avtime() + milliseconds(50);
                    mVideo.mCurrentPtsTime = mClockBase;
                    mVideo.mFrameTimer = mVideo.mCurrentPtsTime;
                    mAudio.startPlayback();
                    mPlaying.store(std::memory_order_release);
                }
            }
            /* Nothing to send or get for now, wait a bit and try again. */
            { std::unique_lock<std::mutex> lock(mSendMtx);
                if(mSendDataGood.test_and_set(std::memory_order_relaxed))
                    mSendCond.wait_for(lock, milliseconds(10));
            }
        } while(!mQuit.load(std::memory_order_relaxed));
    }
    /* Pass a null packet to finish the send buffers (the receive functions
     * will get AVERROR_EOF when emptied).
     */
    if(mVideo.mCodecCtx)
    {
        { std::lock_guard<std::mutex> lock(mVideo.mQueueMtx);
            avcodec_send_packet(mVideo.mCodecCtx.get(), nullptr);
        }
        mVideo.mQueueCond.notify_one();
    }
    if(mAudio.mCodecCtx)
    {
        { std::lock_guard<std::mutex> lock(mAudio.mQueueMtx);
            avcodec_send_packet(mAudio.mCodecCtx.get(), nullptr);
        }
        mAudio.mQueueCond.notify_one();
    }
    video_queue.clear();
    audio_queue.clear();

    /* all done - wait for it */
    if(mVideoThread.joinable())
        mVideoThread.join();
    if(mAudioThread.joinable())
        mAudioThread.join();

    mVideo.mEOS = true;
    std::unique_lock<std::mutex> lock(mVideo.mPictQMutex);
    while(!mVideo.mFinalUpdate)
        mVideo.mPictQCond.wait(lock);
    lock.unlock();

    SDL_Event evt{};
    evt.user.type = FF_MOVIE_DONE_EVENT;
    SDL_PushEvent(&evt);

    return 0;
}


// Helper class+method to print the time with human-readable formatting.
struct PrettyTime {
    seconds mTime;
};
inline std::ostream &operator<<(std::ostream &os, const PrettyTime &rhs)
{
    using hours = std::chrono::hours;
    using minutes = std::chrono::minutes;
    using std::chrono::duration_cast;

    seconds t = rhs.mTime;
    if(t.count() < 0)
    {
        os << '-';
        t *= -1;
    }

    // Only handle up to hour formatting
    if(t >= hours(1))
        os << duration_cast<hours>(t).count() << 'h' << std::setfill('0') << std::setw(2)
           << (duration_cast<minutes>(t).count() % 60) << 'm';
    else
        os << duration_cast<minutes>(t).count() << 'm' << std::setfill('0');
    os << std::setw(2) << (duration_cast<seconds>(t).count() % 60) << 's' << std::setw(0)
       << std::setfill(' ');
    return os;
}

} // namespace


int main(int argc, char *argv[])
{
    std::unique_ptr<MovieState> movState;

    if(argc < 2)
    {
        std::cerr<< "Usage: "<<argv[0]<<" [-device <device name>] [-direct] <files...>" <<std::endl;
        return 1;
    }
    /* Register all formats and codecs */
    av_register_all();
    /* Initialize networking protocols */
    avformat_network_init();

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER))
    {
        std::cerr<< "Could not initialize SDL - <<"<<SDL_GetError() <<std::endl;
        return 1;
    }

    /* Make a window to put our video */
    SDL_Window *screen = SDL_CreateWindow(AppName.c_str(), 0, 0, 640, 480, SDL_WINDOW_RESIZABLE);
    if(!screen)
    {
        std::cerr<< "SDL: could not set video mode - exiting" <<std::endl;
        return 1;
    }
    /* Make a renderer to handle the texture image surface and rendering. */
    Uint32 render_flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
    SDL_Renderer *renderer = SDL_CreateRenderer(screen, -1, render_flags);
    if(renderer)
    {
        SDL_RendererInfo rinf{};
        bool ok = false;

        /* Make sure the renderer supports IYUV textures. If not, fallback to a
         * software renderer. */
        if(SDL_GetRendererInfo(renderer, &rinf) == 0)
        {
            for(Uint32 i = 0;!ok && i < rinf.num_texture_formats;i++)
                ok = (rinf.texture_formats[i] == SDL_PIXELFORMAT_IYUV);
        }
        if(!ok)
        {
            std::cerr<< "IYUV pixelformat textures not supported on renderer "<<rinf.name <<std::endl;
            SDL_DestroyRenderer(renderer);
            renderer = nullptr;
        }
    }
    if(!renderer)
    {
        render_flags = SDL_RENDERER_SOFTWARE | SDL_RENDERER_PRESENTVSYNC;
        renderer = SDL_CreateRenderer(screen, -1, render_flags);
    }
    if(!renderer)
    {
        std::cerr<< "SDL: could not create renderer - exiting" <<std::endl;
        return 1;
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, nullptr);
    SDL_RenderPresent(renderer);

    /* Open an audio device */
    int fileidx = 1;
    ALCdevice *device = [argc,argv,&fileidx]() -> ALCdevice*
    {
        ALCdevice *dev = NULL;
        if(argc > 3 && strcmp(argv[1], "-device") == 0)
        {
            fileidx = 3;
            dev = alcOpenDevice(argv[2]);
            if(dev) return dev;
            std::cerr<< "Failed to open \""<<argv[2]<<"\" - trying default" <<std::endl;
        }
        return alcOpenDevice(nullptr);
    }();
    ALCcontext *context = alcCreateContext(device, nullptr);
    if(!context || alcMakeContextCurrent(context) == ALC_FALSE)
    {
        std::cerr<< "Failed to set up audio device" <<std::endl;
        if(context)
            alcDestroyContext(context);
        return 1;
    }

    const ALCchar *name = nullptr;
    if(alcIsExtensionPresent(device, "ALC_ENUMERATE_ALL_EXT"))
        name = alcGetString(device, ALC_ALL_DEVICES_SPECIFIER);
    if(!name || alcGetError(device) != AL_NO_ERROR)
        name = alcGetString(device, ALC_DEVICE_SPECIFIER);
    std::cout<< "Opened \""<<name<<"\"" <<std::endl;

    if(alcIsExtensionPresent(device, "ALC_SOFT_device_clock"))
    {
        std::cout<< "Found ALC_SOFT_device_clock" <<std::endl;
        alcGetInteger64vSOFT = reinterpret_cast<LPALCGETINTEGER64VSOFT>(
            alcGetProcAddress(device, "alcGetInteger64vSOFT")
        );
    }

    if(alIsExtensionPresent("AL_SOFT_source_latency"))
    {
        std::cout<< "Found AL_SOFT_source_latency" <<std::endl;
        alGetSourcei64vSOFT = reinterpret_cast<LPALGETSOURCEI64VSOFT>(
            alGetProcAddress("alGetSourcei64vSOFT")
        );
    }
    if(alIsExtensionPresent("AL_SOFTX_map_buffer"))
    {
        std::cout<< "Found AL_SOFT_map_buffer" <<std::endl;
        alBufferStorageSOFT = reinterpret_cast<LPALBUFFERSTORAGESOFT>(
            alGetProcAddress("alBufferStorageSOFT"));
        alMapBufferSOFT = reinterpret_cast<LPALMAPBUFFERSOFT>(
            alGetProcAddress("alMapBufferSOFT"));
        alUnmapBufferSOFT = reinterpret_cast<LPALUNMAPBUFFERSOFT>(
            alGetProcAddress("alUnmapBufferSOFT"));
    }
    if(alIsExtensionPresent("AL_SOFTX_events"))
    {
        std::cout<< "Found AL_SOFT_events" <<std::endl;
        alEventControlSOFT = reinterpret_cast<LPALEVENTCONTROLSOFT>(
            alGetProcAddress("alEventControlSOFT"));
        alEventCallbackSOFT = reinterpret_cast<LPALEVENTCALLBACKSOFT>(
            alGetProcAddress("alEventCallbackSOFT"));
    }

    if(fileidx < argc && strcmp(argv[fileidx], "-direct") == 0)
    {
        ++fileidx;
        if(!alIsExtensionPresent("AL_SOFT_direct_channels"))
            std::cerr<< "AL_SOFT_direct_channels not supported for direct output" <<std::endl;
        else
        {
            std::cout<< "Found AL_SOFT_direct_channels" <<std::endl;
            EnableDirectOut = true;
        }
    }

    while(fileidx < argc && !movState)
    {
        movState = std::unique_ptr<MovieState>(new MovieState(argv[fileidx++]));
        if(!movState->prepare()) movState = nullptr;
    }
    if(!movState)
    {
        std::cerr<< "Could not start a video" <<std::endl;
        return 1;
    }
    movState->setTitle(screen);

    /* Default to going to the next movie at the end of one. */
    enum class EomAction {
        Next, Quit
    } eom_action = EomAction::Next;
    seconds last_time(-1);
    SDL_Event event;
    while(1)
    {
        int have_evt = SDL_WaitEventTimeout(&event, 10);

        auto cur_time = std::chrono::duration_cast<seconds>(movState->getMasterClock());
        if(cur_time != last_time)
        {
            auto end_time = std::chrono::duration_cast<seconds>(movState->getDuration());
            std::cout<< "\r "<<PrettyTime{cur_time}<<" / "<<PrettyTime{end_time} <<std::flush;
            last_time = cur_time;
        }
        if(!have_evt) continue;

        switch(event.type)
        {
            case SDL_KEYDOWN:
                switch(event.key.keysym.sym)
                {
                    case SDLK_ESCAPE:
                        movState->mQuit = true;
                        eom_action = EomAction::Quit;
                        break;

                    case SDLK_n:
                        movState->mQuit = true;
                        eom_action = EomAction::Next;
                        break;

                    default:
                        break;
                }
                break;

            case SDL_WINDOWEVENT:
                switch(event.window.event)
                {
                    case SDL_WINDOWEVENT_RESIZED:
                        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                        SDL_RenderFillRect(renderer, nullptr);
                        break;

                    default:
                        break;
                }
                break;

            case SDL_QUIT:
                movState->mQuit = true;
                eom_action = EomAction::Quit;
                break;

            case FF_UPDATE_EVENT:
                reinterpret_cast<VideoState*>(event.user.data1)->updatePicture(
                    screen, renderer
                );
                break;

            case FF_REFRESH_EVENT:
                reinterpret_cast<VideoState*>(event.user.data1)->refreshTimer(
                    screen, renderer
                );
                break;

            case FF_MOVIE_DONE_EVENT:
                std::cout<<'\n';
                last_time = seconds(-1);
                if(eom_action != EomAction::Quit)
                {
                    movState = nullptr;
                    while(fileidx < argc && !movState)
                    {
                        movState = std::unique_ptr<MovieState>(new MovieState(argv[fileidx++]));
                        if(!movState->prepare()) movState = nullptr;
                    }
                    if(movState)
                    {
                        movState->setTitle(screen);
                        break;
                    }
                }

                /* Nothing more to play. Shut everything down and quit. */
                movState = nullptr;

                alcMakeContextCurrent(nullptr);
                alcDestroyContext(context);
                alcCloseDevice(device);

                SDL_DestroyRenderer(renderer);
                renderer = nullptr;
                SDL_DestroyWindow(screen);
                screen = nullptr;

                SDL_Quit();
                exit(0);

            default:
                break;
        }
    }

    std::cerr<< "SDL_WaitEvent error - "<<SDL_GetError() <<std::endl;
    return 1;
}
