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

namespace {

const std::string AppName("alffplay");

bool do_direct_out = false;
LPALGETSOURCEI64VSOFT alGetSourcei64vSOFT;

const std::chrono::seconds AVNoSyncThreshold(10);

const std::chrono::duration<double> VideoSyncThreshold(0.01);
#define VIDEO_PICTURE_QUEUE_SIZE 16

const std::chrono::duration<double> AudioSyncThreshold(0.03);
const std::chrono::duration<double> AudioSampleCorrectionMax(0.05);
/* Averaging filter coefficient for audio sync. */
#define AUDIO_DIFF_AVG_NB 20
const double AudioAvgFilterCoeff = std::pow(0.01, 1.0/AUDIO_DIFF_AVG_NB);
/* Per-buffer size, in time */
const std::chrono::milliseconds AudioBufferTime(20);
/* Buffer total size, in time (should be divisible by the buffer time) */
const std::chrono::milliseconds AudioBufferTotalTime(800);

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
    AVCodecContext *mCodecCtx{nullptr};

    std::mutex mQueueMtx;
    std::condition_variable mQueueCond;

    /* Used for clock difference average computation */
    std::chrono::duration<double> mClockDiffAvg{0};

    /* Time (in nanoseconds) of the next sample to be buffered */
    std::chrono::nanoseconds mCurrentPts{0};

    /* Decompressed sample frame, and swresample context for conversion */
    AVFrame           *mDecodedFrame{nullptr};
    struct SwrContext *mSwresCtx{nullptr};

    /* Conversion format, for what gets fed to Alure */
    int                 mDstChanLayout{0};
    enum AVSampleFormat mDstSampleFmt{AV_SAMPLE_FMT_NONE};

    /* Storage of converted samples */
    uint8_t *mSamples{nullptr};
    int mSamplesLen{0}; /* In samples */
    int mSamplesPos{0};
    int mSamplesMax{0};

    /* OpenAL format */
    ALenum mFormat{AL_NONE};
    ALsizei mFrameSize{0};

    std::recursive_mutex mSrcMutex;
    ALuint mSource{0};
    std::vector<ALuint> mBuffers;
    ALsizei mBufferIdx{0};

    AudioState(MovieState &movie) : mMovie(movie)
    { }
    ~AudioState()
    {
        if(mSource)
            alDeleteSources(1, &mSource);
        if(!mBuffers.empty())
            alDeleteBuffers(mBuffers.size(), mBuffers.data());

        av_frame_free(&mDecodedFrame);
        swr_free(&mSwresCtx);

        av_freep(&mSamples);

        avcodec_free_context(&mCodecCtx);
    }

    std::chrono::nanoseconds getClock();

    int getSync();
    int decodeFrame();
    int readAudio(uint8_t *samples, int length);

    int handler();
};

struct VideoState {
    MovieState &mMovie;

    AVStream *mStream{nullptr};
    AVCodecContext *mCodecCtx{nullptr};

    std::mutex mQueueMtx;
    std::condition_variable mQueueCond;

    std::chrono::nanoseconds mClock{0};
    std::chrono::duration<double> mFrameTimer{0};
    std::chrono::nanoseconds mFrameLastPts{0};
    std::chrono::nanoseconds mFrameLastDelay{0};
    std::chrono::nanoseconds mCurrentPts{0};
    /* time (av_gettime) at which we updated mCurrentPts - used to have running video pts */
    std::chrono::microseconds mCurrentPtsTime{0};

    /* Decompressed video frame, and swscale context for conversion */
    AVFrame           *mDecodedFrame{nullptr};
    struct SwsContext *mSwscaleCtx{nullptr};

    struct Picture {
        SDL_Texture *mImage{nullptr};
        int mWidth{0}, mHeight{0}; /* Logical image size (actual size may be larger) */
        std::atomic<bool> mUpdated{false};
        std::chrono::nanoseconds mPts{0};

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
    ~VideoState()
    {
        sws_freeContext(mSwscaleCtx);
        mSwscaleCtx = nullptr;
        av_frame_free(&mDecodedFrame);
        avcodec_free_context(&mCodecCtx);
    }

    std::chrono::nanoseconds getClock();

    static Uint32 SDLCALL sdl_refresh_timer_cb(Uint32 interval, void *opaque);
    void schedRefresh(std::chrono::milliseconds delay);
    void display(SDL_Window *screen, SDL_Renderer *renderer);
    void refreshTimer(SDL_Window *screen, SDL_Renderer *renderer);
    void updatePicture(SDL_Window *screen, SDL_Renderer *renderer);
    int queuePicture(std::chrono::nanoseconds pts);
    std::chrono::nanoseconds synchronize(std::chrono::nanoseconds pts);
    int handler();
};

struct MovieState {
    AVFormatContext *mFormatCtx{nullptr};

    SyncMaster mAVSyncType{SyncMaster::Default};

    std::chrono::microseconds mClockBase{0};

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
        avformat_close_input(&mFormatCtx);
    }

    static int decode_interrupt_cb(void *ctx);
    bool prepare();
    void setTitle(SDL_Window *window);

    std::chrono::nanoseconds getClock();

    std::chrono::nanoseconds getMasterClock();

    int streamComponentOpen(int stream_index);
    int parse_handler();
};


std::chrono::nanoseconds AudioState::getClock()
{
    using fixed32 = std::chrono::duration<ALint64SOFT,std::ratio<1,(1ll<<32)>>;
    using nanoseconds = std::chrono::nanoseconds;

    std::unique_lock<std::recursive_mutex> lock(mSrcMutex);
    /* The audio clock is the timestamp of the sample currently being heard.
     * It's based on 4 components:
     * 1 - The timestamp of the next sample to buffer (mCurrentPts)
     * 2 - The length of the source's buffer queue
     *     (AudioBufferTime*AL_BUFFERS_QUEUED)
     * 3 - The offset OpenAL is currently at in the source (the first value
     *     from AL_SAMPLE_OFFSET_LATENCY_SOFT)
     * 4 - The latency between OpenAL and the DAC (the second value from
     *     AL_SAMPLE_OFFSET_LATENCY_SOFT)
     *
     * Subtracting the length of the source queue from the next sample's
     * timestamp gives the timestamp of the sample at start of the source
     * queue. Adding the source offset to that results in the timestamp for
     * OpenAL's current position, and subtracting the source latency from that
     * gives the timestamp of the sample currently at the DAC.
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
            pts -= AudioBufferTime*queued;
            pts += std::chrono::duration_cast<nanoseconds>(
                fixed32(offset[0] / mCodecCtx->sample_rate)
            );
        }
        if(status == AL_PLAYING)
            pts -= nanoseconds(offset[1]);
    }
    lock.unlock();

    return std::max(pts, std::chrono::nanoseconds::zero());
}

int AudioState::getSync()
{
    using seconds = std::chrono::duration<double>;

    if(mMovie.mAVSyncType == SyncMaster::Audio)
        return 0;

    auto ref_clock = mMovie.getMasterClock();
    auto diff = seconds(ref_clock - getClock());

    if(!(diff < AVNoSyncThreshold && diff > -AVNoSyncThreshold))
    {
        /* Difference is TOO big; reset accumulated average */
        mClockDiffAvg = std::chrono::duration<double>::zero();
        return 0;
    }

    /* Accumulate the diffs */
    mClockDiffAvg = mClockDiffAvg*AudioAvgFilterCoeff + diff;
    auto avg_diff = mClockDiffAvg*(1.0 - AudioAvgFilterCoeff);
    if(avg_diff < AudioSyncThreshold/2.0 && avg_diff > -AudioSyncThreshold)
        return 0;

    /* Constrain the per-update difference to avoid exceedingly large skips */
    if(!(diff < AudioSampleCorrectionMax))
        return (int)(AudioSampleCorrectionMax * mCodecCtx->sample_rate).count();
    if(!(diff > -AudioSampleCorrectionMax))
        return (int)(-AudioSampleCorrectionMax * mCodecCtx->sample_rate).count();
    return (int)(diff.count()*mCodecCtx->sample_rate);
}

int AudioState::decodeFrame()
{
    while(!mMovie.mQuit.load(std::memory_order_relaxed))
    {
        std::unique_lock<std::mutex> lock(mQueueMtx);
        int ret = avcodec_receive_frame(mCodecCtx, mDecodedFrame);
        if(ret == AVERROR(EAGAIN))
        {
            mMovie.mSendDataGood.clear(std::memory_order_relaxed);
            std::unique_lock<std::mutex>(mMovie.mSendMtx).unlock();
            mMovie.mSendCond.notify_one();
            do {
                mQueueCond.wait(lock);
                ret = avcodec_receive_frame(mCodecCtx, mDecodedFrame);
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
            av_frame_unref(mDecodedFrame);
            continue;
        }

        /* If provided, update w/ pts */
        int64_t pts = av_frame_get_best_effort_timestamp(mDecodedFrame);
        if(pts != AV_NOPTS_VALUE)
            mCurrentPts = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::duration<double>(av_q2d(mStream->time_base)*pts)
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
        int data_size = swr_convert(mSwresCtx, &mSamples, mDecodedFrame->nb_samples,
            (const uint8_t**)mDecodedFrame->data, mDecodedFrame->nb_samples
        );

        av_frame_unref(mDecodedFrame);
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


int AudioState::readAudio(uint8_t *samples, int length)
{
    using seconds = std::chrono::duration<int64_t>;
    using nanoseconds = std::chrono::nanoseconds;

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

            mCurrentPts += nanoseconds(seconds(mSamplesPos)) / mCodecCtx->sample_rate;
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

    if(audio_size < length && audio_size > 0)
    {
        int rem = length - audio_size;
        std::fill_n(samples, rem*mFrameSize,
                    (mDstSampleFmt == AV_SAMPLE_FMT_U8) ? 0x80 : 0x00);
        mCurrentPts += nanoseconds(seconds(rem)) / mCodecCtx->sample_rate;
        audio_size += rem;
    }

    return audio_size * mFrameSize;
}


int AudioState::handler()
{
    std::unique_lock<std::recursive_mutex> lock(mSrcMutex);
    ALenum fmt;

    /* Find a suitable format for Alure. */
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
    ALsizei buffer_len = std::chrono::duration_cast<std::chrono::duration<int>>(
        mCodecCtx->sample_rate * AudioBufferTime).count() * mFrameSize;
    void *samples = av_malloc(buffer_len);

    mSamples = NULL;
    mSamplesMax = 0;
    mSamplesPos = 0;
    mSamplesLen = 0;

    if(!(mDecodedFrame=av_frame_alloc()))
    {
        std::cerr<< "Failed to allocate audio frame" <<std::endl;
        goto finish;
    }

    mSwresCtx = swr_alloc_set_opts(nullptr,
        mDstChanLayout, mDstSampleFmt, mCodecCtx->sample_rate,
        mCodecCtx->channel_layout ? mCodecCtx->channel_layout :
            (uint64_t)av_get_default_channel_layout(mCodecCtx->channels),
        mCodecCtx->sample_fmt, mCodecCtx->sample_rate,
        0, nullptr
    );
    if(!mSwresCtx || swr_init(mSwresCtx) != 0)
    {
        std::cerr<< "Failed to initialize audio converter" <<std::endl;
        goto finish;
    }

    mBuffers.assign(AudioBufferTotalTime / AudioBufferTime, 0);
    alGenBuffers(mBuffers.size(), mBuffers.data());
    alGenSources(1, &mSource);

    if(do_direct_out)
        alSourcei(mSource, AL_DIRECT_CHANNELS_SOFT, AL_TRUE);

    while(alGetError() == AL_NO_ERROR && !mMovie.mQuit.load(std::memory_order_relaxed))
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
            int audio_size;

            /* Read the next chunk of data, fill the buffer, and queue it on
             * the source */
            audio_size = readAudio(reinterpret_cast<uint8_t*>(samples), buffer_len);
            if(audio_size <= 0) break;

            ALuint bufid = mBuffers[mBufferIdx++];
            mBufferIdx %= mBuffers.size();

            alBufferData(bufid, mFormat, samples, audio_size, mCodecCtx->sample_rate);
            alSourceQueueBuffers(mSource, 1, &bufid);
            queued++;
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

        lock.unlock();

        /* (re)start the source if needed, and wait for a buffer to finish */
        if(state != AL_PLAYING && state != AL_PAUSED)
            alSourcePlay(mSource);
        SDL_Delay((AudioBufferTime/3).count());

        lock.lock();
    }

finish:
    alSourceRewind(mSource);
    alSourcei(mSource, AL_BUFFER, 0);

    av_frame_free(&mDecodedFrame);
    swr_free(&mSwresCtx);

    av_freep(&mSamples);

    return 0;
}


std::chrono::nanoseconds VideoState::getClock()
{
    auto delta = std::chrono::microseconds(av_gettime()) - mCurrentPtsTime;
    return mCurrentPts + delta;
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
void VideoState::schedRefresh(std::chrono::milliseconds delay)
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
        schedRefresh(std::chrono::milliseconds(100));
        return;
    }

    std::unique_lock<std::mutex> lock(mPictQMutex);
retry:
    if(mPictQSize == 0)
    {
        if(mEOS)
            mFinalUpdate = true;
        else
            schedRefresh(std::chrono::milliseconds(1));
        lock.unlock();
        mPictQCond.notify_all();
        return;
    }

    Picture *vp = &mPictQ[mPictQRead];
    mCurrentPts = vp->mPts;
    mCurrentPtsTime = std::chrono::microseconds(av_gettime());

    /* Get delay using the frame pts and the pts from last frame. */
    auto delay = vp->mPts - mFrameLastPts;
    if(delay <= std::chrono::seconds::zero() || delay >= std::chrono::seconds(1))
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
        using seconds = std::chrono::duration<double>;

        auto ref_clock = mMovie.getMasterClock();
        auto diff = seconds(vp->mPts - ref_clock);

        /* Skip or repeat the frame. Take delay into account. */
        auto sync_threshold = std::min(seconds(delay), VideoSyncThreshold);
        if(!(diff < AVNoSyncThreshold && diff > -AVNoSyncThreshold))
        {
            if(diff <= -sync_threshold)
                delay = std::chrono::nanoseconds::zero();
            else if(diff >= sync_threshold)
                delay *= 2;
        }
    }

    mFrameTimer += delay;
    /* Compute the REAL delay. */
    auto actual_delay = mFrameTimer - std::chrono::microseconds(av_gettime());
    if(!(actual_delay >= VideoSyncThreshold))
    {
        /* We don't have time to handle this picture, just skip to the next one. */
        mPictQRead = (mPictQRead+1)%mPictQ.size();
        mPictQSize--;
        goto retry;
    }
    schedRefresh(std::chrono::duration_cast<std::chrono::milliseconds>(actual_delay));

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
        AVFrame *frame = mDecodedFrame;
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
                sws_freeContext(mSwscaleCtx);
                mSwscaleCtx = sws_getContext(
                    w, h, mCodecCtx->pix_fmt,
                    w, h, AV_PIX_FMT_YUV420P, 0,
                    nullptr, nullptr, nullptr
                );
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

            sws_scale(mSwscaleCtx, (const uint8_t**)frame->data,
                      frame->linesize, 0, h, pict_data, pict_linesize);
            SDL_UnlockTexture(vp->mImage);
        }
    }

    std::unique_lock<std::mutex> lock(mPictQMutex);
    vp->mUpdated = true;
    lock.unlock();
    mPictQCond.notify_one();
}

int VideoState::queuePicture(std::chrono::nanoseconds pts)
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
    vp->mUpdated = false;
    SDL_Event evt{};
    evt.user.type = FF_UPDATE_EVENT;
    evt.user.data1 = this;
    SDL_PushEvent(&evt);

    /* Wait until the picture is updated. */
    lock.lock();
    while(!vp->mUpdated && !mMovie.mQuit.load(std::memory_order_relaxed))
        mPictQCond.wait(lock);
    if(mMovie.mQuit.load(std::memory_order_relaxed))
        return -1;
    vp->mPts = pts;

    mPictQWrite = (mPictQWrite+1)%mPictQ.size();
    mPictQSize++;
    lock.unlock();

    return 0;
}

std::chrono::nanoseconds VideoState::synchronize(std::chrono::nanoseconds pts)
{
    if(pts == std::chrono::nanoseconds::zero()) /* if we aren't given a pts, set it to the clock */
        pts = mClock;
    else /* if we have pts, set video clock to it */
        mClock = pts;

    /* update the video clock */
    auto frame_delay = av_q2d(mCodecCtx->time_base);
    /* if we are repeating a frame, adjust clock accordingly */
    frame_delay += mDecodedFrame->repeat_pict * (frame_delay * 0.5);

    mClock += std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(frame_delay));
    return pts;
}

int VideoState::handler()
{
    mDecodedFrame = av_frame_alloc();
    while(!mMovie.mQuit.load(std::memory_order_relaxed))
    {
        std::unique_lock<std::mutex> lock(mQueueMtx);
        /* Decode video frame */
        int ret = avcodec_receive_frame(mCodecCtx, mDecodedFrame);
        if(ret == AVERROR(EAGAIN))
        {
            mMovie.mSendDataGood.clear(std::memory_order_relaxed);
            std::unique_lock<std::mutex>(mMovie.mSendMtx).unlock();
            mMovie.mSendCond.notify_one();
            do {
                mQueueCond.wait(lock);
                ret = avcodec_receive_frame(mCodecCtx, mDecodedFrame);
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

        std::chrono::nanoseconds pts = synchronize(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::duration<double>(
                    av_q2d(mStream->time_base) * av_frame_get_best_effort_timestamp(mDecodedFrame)
                )
            )
        );
        if(queuePicture(pts) < 0)
            break;
        av_frame_unref(mDecodedFrame);
    }
    mEOS = true;
    av_frame_free(&mDecodedFrame);

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
    mFormatCtx = avformat_alloc_context();
    mFormatCtx->interrupt_callback.callback = decode_interrupt_cb;
    mFormatCtx->interrupt_callback.opaque = this;
    if(avio_open2(&mFormatCtx->pb, mFilename.c_str(), AVIO_FLAG_READ,
                  &mFormatCtx->interrupt_callback, nullptr))
    {
        std::cerr<< "Failed to open "<<mFilename <<std::endl;
        return false;
    }

    /* Open movie file */
    if(avformat_open_input(&mFormatCtx, mFilename.c_str(), nullptr, nullptr) != 0)
    {
        std::cerr<< "Failed to open "<<mFilename <<std::endl;
        return false;
    }

    /* Retrieve stream information */
    if(avformat_find_stream_info(mFormatCtx, nullptr) < 0)
    {
        std::cerr<< mFilename<<": failed to find stream info" <<std::endl;
        return false;
    }

    mVideo.schedRefresh(std::chrono::milliseconds(40));

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

std::chrono::nanoseconds MovieState::getClock()
{
    using microseconds = std::chrono::microseconds;
    return microseconds(av_gettime()) - mClockBase;
}

std::chrono::nanoseconds MovieState::getMasterClock()
{
    if(mAVSyncType == SyncMaster::Video)
        return mVideo.getClock();
    if(mAVSyncType == SyncMaster::Audio)
        return mAudio.getClock();
    return getClock();
}

int MovieState::streamComponentOpen(int stream_index)
{
    if(stream_index < 0 || (unsigned int)stream_index >= mFormatCtx->nb_streams)
        return -1;

    /* Get a pointer to the codec context for the stream, and open the
     * associated codec.
     */
    AVCodecContext *avctx = avcodec_alloc_context3(nullptr);
    if(!avctx) return -1;

    if(avcodec_parameters_to_context(avctx, mFormatCtx->streams[stream_index]->codecpar))
    {
        avcodec_free_context(&avctx);
        return -1;
    }

    AVCodec *codec = avcodec_find_decoder(avctx->codec_id);
    if(!codec || avcodec_open2(avctx, codec, nullptr) < 0)
    {
        std::cerr<< "Unsupported codec: "<<avcodec_get_name(avctx->codec_id)
                 << " (0x"<<std::hex<<avctx->codec_id<<std::dec<<")" <<std::endl;
        avcodec_free_context(&avctx);
        return -1;
    }

    /* Initialize and start the media type handler */
    switch(avctx->codec_type)
    {
        case AVMEDIA_TYPE_AUDIO:
            mAudio.mStream = mFormatCtx->streams[stream_index];
            mAudio.mCodecCtx = avctx;

            mAudioThread = std::thread(std::mem_fn(&AudioState::handler), &mAudio);
            break;

        case AVMEDIA_TYPE_VIDEO:
            mVideo.mStream = mFormatCtx->streams[stream_index];
            mVideo.mCodecCtx = avctx;

            mVideo.mCurrentPtsTime = std::chrono::microseconds(av_gettime());
            mVideo.mFrameTimer = mVideo.mCurrentPtsTime;
            mVideo.mFrameLastDelay = std::chrono::milliseconds(40);

            mVideoThread = std::thread(std::mem_fn(&VideoState::handler), &mVideo);
            break;

        default:
            avcodec_free_context(&avctx);
            return -1;
    }

    return stream_index;
}

int MovieState::parse_handler()
{
    int video_index = -1;
    int audio_index = -1;

    /* Dump information about file onto standard error */
    av_dump_format(mFormatCtx, 0, mFilename.c_str(), 0);

    /* Find the first video and audio streams */
    for(unsigned int i = 0;i < mFormatCtx->nb_streams;i++)
    {
        if(mFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_index < 0)
            video_index = i;
        else if(mFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_index < 0)
            audio_index = i;
    }
    /* Start the external clock in 50ms, to give the audio and video
     * components time to start without needing to skip ahead.
     */
    mClockBase = std::chrono::microseconds(av_gettime() + 50000);
    if(audio_index >= 0) audio_index = streamComponentOpen(audio_index);
    if(video_index >= 0) video_index = streamComponentOpen(video_index);

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
        if(av_read_frame(mFormatCtx, &packet) < 0)
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
                    ret = avcodec_send_packet(mAudio.mCodecCtx, audio_queue.front());
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
                    ret = avcodec_send_packet(mVideo.mCodecCtx, video_queue.front());
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

            /* Nothing to send or get for now, wait a bit and try again. */
            { std::unique_lock<std::mutex> lock(mSendMtx);
                if(mSendDataGood.test_and_set(std::memory_order_relaxed))
                    mSendCond.wait_for(lock, std::chrono::milliseconds(10));
            }
        } while(!mQuit.load(std::memory_order_relaxed));
    }
    /* Pass a null packet to finish the send buffers (the receive functions
     * will get AVERROR_EOF when emptied).
     */
    if(mVideo.mCodecCtx != nullptr)
    {
        { std::lock_guard<std::mutex> lock(mVideo.mQueueMtx);
            avcodec_send_packet(mVideo.mCodecCtx, nullptr);
        }
        mVideo.mQueueCond.notify_one();
    }
    if(mAudio.mCodecCtx != nullptr)
    {
        { std::lock_guard<std::mutex> lock(mAudio.mQueueMtx);
            avcodec_send_packet(mAudio.mCodecCtx, nullptr);
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
    SDL_Renderer *renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED);
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
        renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_SOFTWARE);
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

    if(alIsExtensionPresent("AL_SOFT_source_latency"))
    {
        std::cout<< "Found AL_SOFT_source_latency" <<std::endl;
        alGetSourcei64vSOFT = reinterpret_cast<LPALGETSOURCEI64VSOFT>(
            alGetProcAddress("alGetSourcei64vSOFT")
        );
    }

    if(fileidx < argc && strcmp(argv[fileidx], "-direct") == 0)
    {
        ++fileidx;
        if(!alIsExtensionPresent("AL_SOFT_direct_channels"))
            std::cerr<< "AL_SOFT_direct_channels not supported for direct output" <<std::endl;
        else
        {
            std::cout<< "Found AL_SOFT_direct_channels" <<std::endl;
            do_direct_out = true;
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
    SDL_Event event;
    while(SDL_WaitEvent(&event) == 1)
    {
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
