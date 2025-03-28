/*
 * An example showing how to play a stream sync'd to video, using ffmpeg.
 *
 * Requires C++14.
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <ratio>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef __GNUC__
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wconversion\"")
_Pragma("GCC diagnostic ignored \"-Wold-style-cast\"")
#endif
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavutil/avutil.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/mem.h"
#include "libavutil/pixfmt.h"
#include "libavutil/rational.h"
#include "libavutil/samplefmt.h"
#include "libavutil/time.h"
#include "libavutil/channel_layout.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"

struct SwsContext;
}

#define SDL_MAIN_HANDLED
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_main.h"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_video.h"

namespace {
constexpr auto DefineSDLColorspace(SDL_ColorType type, SDL_ColorRange range,
    SDL_ColorPrimaries primaries, SDL_TransferCharacteristics transfer,
    SDL_MatrixCoefficients matrix, SDL_ChromaLocation chromaloc) noexcept
{
    return SDL_DEFINE_COLORSPACE(type, range, primaries, transfer, matrix, chromaloc);
}
} // namespace
#ifdef __GNUC__
_Pragma("GCC diagnostic pop")
#endif

#include "AL/alc.h"
#include "AL/al.h"
#include "AL/alext.h"

#include "almalloc.h"
#include "alnumbers.h"
#include "alnumeric.h"
#include "alspan.h"
#include "common/alhelpers.h"
#include "fmt/core.h"
#include "fmt/format.h"


namespace {

using voidp = void*;
using fixed32 = std::chrono::duration<int64_t,std::ratio<1,(1_i64<<32)>>;
using nanoseconds = std::chrono::nanoseconds;
using microseconds = std::chrono::microseconds;
using milliseconds = std::chrono::milliseconds;
using seconds = std::chrono::seconds;
using seconds_d64 = std::chrono::duration<double>;
using std::chrono::duration_cast;

#ifdef __GNUC__
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wold-style-cast\"")
#endif
constexpr auto AVNoPtsValue = AV_NOPTS_VALUE;
constexpr auto AVErrorEOF = AVERROR_EOF;
#ifdef __GNUC__
_Pragma("GCC diagnostic pop")
#endif

const std::string AppName{"alffplay"};

ALenum DirectOutMode{AL_FALSE};
bool EnableWideStereo{false};
bool EnableUhj{false};
bool EnableSuperStereo{false};
bool DisableVideo{false};
LPALGETSOURCEI64VSOFT alGetSourcei64vSOFT;
LPALCGETINTEGER64VSOFT alcGetInteger64vSOFT;
LPALEVENTCONTROLSOFT alEventControlSOFT;
LPALEVENTCALLBACKSOFT alEventCallbackSOFT;

LPALBUFFERCALLBACKSOFT alBufferCallbackSOFT;

const seconds AVNoSyncThreshold{10};

#define VIDEO_PICTURE_QUEUE_SIZE 24

const seconds_d64 AudioSyncThreshold{0.03};
const milliseconds AudioSampleCorrectionMax{50};
/* Averaging filter coefficient for audio sync. */
#define AUDIO_DIFF_AVG_NB 20
const double AudioAvgFilterCoeff{std::pow(0.01, 1.0/AUDIO_DIFF_AVG_NB)};
/* Per-buffer size, in time */
constexpr milliseconds AudioBufferTime{20};
/* Buffer total size, in time (should be divisible by the buffer time) */
constexpr milliseconds AudioBufferTotalTime{800};
constexpr auto AudioBufferCount = AudioBufferTotalTime / AudioBufferTime;

enum {
    FF_MOVIE_DONE_EVENT = SDL_EVENT_USER
};

enum class SyncMaster {
    Audio,
    Video,
    External,

    Default = Audio
};


inline microseconds get_avtime()
{ return microseconds{av_gettime()}; }

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

struct AVPacketDeleter {
    void operator()(AVPacket *pkt) { av_packet_free(&pkt); }
};
using AVPacketPtr = std::unique_ptr<AVPacket,AVPacketDeleter>;

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


struct SDLProps {
    SDL_PropertiesID mProperties{};

    SDLProps() : mProperties{SDL_CreateProperties()} { }
    ~SDLProps() { SDL_DestroyProperties(mProperties); }

    SDLProps(const SDLProps&) = delete;
    auto operator=(const SDLProps&) -> SDLProps& = delete;

    [[nodiscard]]
    auto getid() const noexcept -> SDL_PropertiesID { return mProperties; }

    auto setPointer(const char *name, void *value) const
    { return SDL_SetPointerProperty(mProperties, name, value); }

    auto setString(const char *name, const char *value) const
    { return SDL_SetStringProperty(mProperties, name, value); }

    auto setInt(const char *name, Sint64 value) const
    { return SDL_SetNumberProperty(mProperties, name, value); }
};

struct TextureFormatEntry {
    AVPixelFormat avformat;
    SDL_PixelFormat sdlformat;
};
constexpr auto TextureFormatMap = std::array{
    TextureFormatEntry{AV_PIX_FMT_RGB8,           SDL_PIXELFORMAT_RGB332},
    TextureFormatEntry{AV_PIX_FMT_RGB444,         SDL_PIXELFORMAT_XRGB4444},
    TextureFormatEntry{AV_PIX_FMT_RGB555,         SDL_PIXELFORMAT_XRGB1555},
    TextureFormatEntry{AV_PIX_FMT_BGR555,         SDL_PIXELFORMAT_XBGR1555},
    TextureFormatEntry{AV_PIX_FMT_RGB565,         SDL_PIXELFORMAT_RGB565},
    TextureFormatEntry{AV_PIX_FMT_BGR565,         SDL_PIXELFORMAT_BGR565},
    TextureFormatEntry{AV_PIX_FMT_RGB24,          SDL_PIXELFORMAT_RGB24},
    TextureFormatEntry{AV_PIX_FMT_BGR24,          SDL_PIXELFORMAT_BGR24},
    TextureFormatEntry{AV_PIX_FMT_0RGB32,         SDL_PIXELFORMAT_XRGB8888},
    TextureFormatEntry{AV_PIX_FMT_0BGR32,         SDL_PIXELFORMAT_XBGR8888},
    TextureFormatEntry{AV_PIX_FMT_NE(RGB0, 0BGR), SDL_PIXELFORMAT_RGBX8888},
    TextureFormatEntry{AV_PIX_FMT_NE(BGR0, 0RGB), SDL_PIXELFORMAT_BGRX8888},
    TextureFormatEntry{AV_PIX_FMT_RGB32,          SDL_PIXELFORMAT_ARGB8888},
    TextureFormatEntry{AV_PIX_FMT_RGB32_1,        SDL_PIXELFORMAT_RGBA8888},
    TextureFormatEntry{AV_PIX_FMT_BGR32,          SDL_PIXELFORMAT_ABGR8888},
    TextureFormatEntry{AV_PIX_FMT_BGR32_1,        SDL_PIXELFORMAT_BGRA8888},
    TextureFormatEntry{AV_PIX_FMT_YUV420P,        SDL_PIXELFORMAT_IYUV},
    TextureFormatEntry{AV_PIX_FMT_YUYV422,        SDL_PIXELFORMAT_YUY2},
    TextureFormatEntry{AV_PIX_FMT_UYVY422,        SDL_PIXELFORMAT_UYVY},
    TextureFormatEntry{AV_PIX_FMT_NV12,           SDL_PIXELFORMAT_NV12},
    TextureFormatEntry{AV_PIX_FMT_NV21,           SDL_PIXELFORMAT_NV21},
};


struct ChannelLayout : public AVChannelLayout {
    ChannelLayout() : AVChannelLayout{} { }
    ChannelLayout(const ChannelLayout &rhs) : AVChannelLayout{}
    { av_channel_layout_copy(this, &rhs); }
    ~ChannelLayout() { av_channel_layout_uninit(this); }

    auto operator=(const ChannelLayout &rhs) -> ChannelLayout&
    { av_channel_layout_copy(this, &rhs); return *this; }
};


class DataQueue {
    const size_t mSizeLimit;
    std::mutex mPacketMutex, mFrameMutex;
    std::condition_variable mPacketCond;
    std::condition_variable mInFrameCond, mOutFrameCond;

    std::deque<AVPacketPtr> mPackets;
    size_t mTotalSize{0};
    bool mFinished{false};

    auto getPacket() -> AVPacketPtr
    {
        auto plock = std::unique_lock{mPacketMutex};
        mPacketCond.wait(plock, [this] { return !mPackets.empty() || mFinished; });
        if(mPackets.empty())
            return nullptr;

        auto ret = std::move(mPackets.front());
        mPackets.pop_front();
        mTotalSize -= static_cast<unsigned int>(ret->size);
        return ret;
    }

public:
    explicit DataQueue(size_t size_limit) : mSizeLimit{size_limit} { }

    int sendPacket(AVCodecContext *codecctx)
    {
        auto packet = getPacket();

        auto ret = int{};
        {
            auto flock = std::unique_lock{mFrameMutex};
            mInFrameCond.wait(flock, [this,codecctx,pkt=packet.get(),&ret]
            {
                ret = avcodec_send_packet(codecctx, pkt);
                if(ret != AVERROR(EAGAIN)) return true;
                mOutFrameCond.notify_one();
                return false;
            });
        }
        mOutFrameCond.notify_one();

        if(!packet)
        {
            if(!ret) return AVErrorEOF;
            fmt::println(stderr, "Failed to send flush packet: {}", ret);
            return ret;
        }
        if(ret < 0)
            fmt::println(stderr, "Failed to send packet: {}", ret);
        return ret;
    }

    int receiveFrame(AVCodecContext *codecctx, AVFrame *frame)
    {
        auto ret = int{};
        {
            auto flock = std::unique_lock{mFrameMutex};
            mOutFrameCond.wait(flock, [this,codecctx,frame,&ret]
            {
                ret = avcodec_receive_frame(codecctx, frame);
                if(ret != AVERROR(EAGAIN)) return true;
                mInFrameCond.notify_one();
                return false;
            });
        }
        mInFrameCond.notify_one();
        return ret;
    }

    void setFinished()
    {
        {
            auto plock = std::lock_guard{mPacketMutex};
            mFinished = true;
        }
        mPacketCond.notify_one();
    }

    void flush()
    {
        {
            auto plock = std::lock_guard{mPacketMutex};
            mFinished = true;

            mPackets.clear();
            mTotalSize = 0;
        }
        mPacketCond.notify_one();
    }

    auto put(const AVPacket *pkt) -> bool
    {
        {
            auto plock = std::lock_guard{mPacketMutex};
            if(mTotalSize >= mSizeLimit || mFinished)
                return false;

            auto *newpkt = mPackets.emplace_back(AVPacketPtr{av_packet_alloc()}).get();
            if(av_packet_ref(newpkt, pkt) == 0)
                mTotalSize += static_cast<unsigned int>(newpkt->size);
            else
                mPackets.pop_back();
        }
        mPacketCond.notify_one();
        return true;
    }
};


struct MovieState;

struct AudioState {
    MovieState &mMovie;

    AVStream *mStream{nullptr};
    AVCodecCtxPtr mCodecCtx;

    DataQueue mQueue{2_uz*1024_uz*1024_uz};

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
    uint64_t       mDstChanLayout{0};
    AVSampleFormat mDstSampleFmt{AV_SAMPLE_FMT_NONE};

    /* Storage of converted samples */
    std::array<uint8_t*,1> mSamples{};
    al::span<uint8_t> mSamplesSpan;
    int mSamplesLen{0}; /* In samples */
    int mSamplesPos{0};
    int mSamplesMax{0};

    std::vector<uint8_t> mBufferData;
    std::atomic<size_t> mReadPos{0};
    std::atomic<size_t> mWritePos{0};

    /* OpenAL format */
    ALenum mFormat{AL_NONE};
    ALuint mFrameSize{0};

    std::mutex mSrcMutex;
    std::condition_variable mSrcCond;
    std::atomic_flag mConnected{};
    ALuint mSource{0};
    std::array<ALuint,AudioBufferCount> mBuffers{};
    ALuint mBufferIdx{0};

    explicit AudioState(MovieState &movie) : mMovie(movie)
    { mConnected.test_and_set(std::memory_order_relaxed); }
    ~AudioState()
    {
        if(mSource)
            alDeleteSources(1, &mSource);
        if(mBuffers[0])
            alDeleteBuffers(static_cast<ALsizei>(mBuffers.size()), mBuffers.data());

        av_freep(static_cast<void*>(mSamples.data()));
    }

    static void AL_APIENTRY eventCallbackC(ALenum eventType, ALuint object, ALuint param,
        ALsizei length, const ALchar *message, void *userParam) noexcept
    { static_cast<AudioState*>(userParam)->eventCallback(eventType, object, param, length, message); }
    void eventCallback(ALenum eventType, ALuint object, ALuint param, ALsizei length,
        const ALchar *message) noexcept;

    static ALsizei AL_APIENTRY bufferCallbackC(void *userptr, void *data, ALsizei size) noexcept
    { return static_cast<AudioState*>(userptr)->bufferCallback(data, size); }
    ALsizei bufferCallback(void *data, ALsizei size) noexcept;

    nanoseconds getClockNoLock();
    nanoseconds getClock()
    {
        std::lock_guard<std::mutex> lock{mSrcMutex};
        return getClockNoLock();
    }

    bool startPlayback();

    int getSync();
    int decodeFrame();
    bool readAudio(al::span<uint8_t> samples, unsigned int length, int &sample_skip);
    bool readAudio(int sample_skip);

    int handler();
};

struct VideoState {
    MovieState &mMovie;

    AVStream *mStream{nullptr};
    AVCodecCtxPtr mCodecCtx;

    DataQueue mQueue{14_uz*1024_uz*1024_uz};

    /* The pts of the currently displayed frame, and the time (av_gettime) it
     * was last updated - used to have running video pts
     */
    nanoseconds mDisplayPts{0};
    microseconds mDisplayPtsTime{microseconds::min()};
    std::mutex mDispPtsMutex;

    /* Swscale context for format conversion */
    SwsContextPtr mSwscaleCtx;

    struct Picture {
        AVFramePtr mFrame;
        nanoseconds mPts{nanoseconds::min()};
    };
    std::array<Picture,VIDEO_PICTURE_QUEUE_SIZE> mPictQ;
    std::atomic<size_t> mPictQRead{0u}, mPictQWrite{1u};
    std::mutex mPictQMutex;
    std::condition_variable mPictQCond;

    SDL_Texture *mImage{nullptr};
    int mWidth{0}, mHeight{0}; /* Full texture size */
    unsigned int mSDLFormat{SDL_PIXELFORMAT_UNKNOWN};
    int mAVFormat{AV_PIX_FMT_NONE};
    bool mFirstUpdate{true};

    std::atomic<bool> mEOS{false};
    std::atomic<bool> mFinalUpdate{false};

    explicit VideoState(MovieState &movie) : mMovie(movie) { }
    ~VideoState()
    {
        if(mImage)
            SDL_DestroyTexture(mImage);
        mImage = nullptr;
    }

    nanoseconds getClock();

    void display(SDL_Renderer *renderer, AVFrame *frame) const;
    void updateVideo(SDL_Window *screen, SDL_Renderer *renderer, bool redraw);
    int handler();
};

struct MovieState {
    AVIOContextPtr mIOContext;
    AVFormatCtxPtr mFormatCtx;

    SyncMaster mAVSyncType{SyncMaster::Default};

    microseconds mClockBase{microseconds::min()};

    std::atomic<bool> mQuit{false};

    AudioState mAudio;
    VideoState mVideo;

    std::mutex mStartupMutex;
    std::condition_variable mStartupCond;
    bool mStartupDone{false};

    std::thread mParseThread;
    std::thread mAudioThread;
    std::thread mVideoThread;

    std::string mFilename;

    explicit MovieState(std::string_view fname) : mAudio{*this}, mVideo{*this}, mFilename{fname}
    { }
    ~MovieState()
    {
        stop();
        if(mParseThread.joinable())
            mParseThread.join();
    }

    static int decode_interrupt_cb(void *ctx);
    bool prepare();
    void setTitle(SDL_Window *window) const;
    void stop();

    [[nodiscard]] nanoseconds getClock() const;
    [[nodiscard]] nanoseconds getMasterClock();
    [[nodiscard]] nanoseconds getDuration() const;

    bool streamComponentOpen(AVStream *stream);
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
        std::array<ALCint64SOFT,2> devtimes{};
        alcGetInteger64vSOFT(device, ALC_DEVICE_CLOCK_LATENCY_SOFT, 2, devtimes.data());
        auto latency = nanoseconds{devtimes[1]};
        auto device_time = nanoseconds{devtimes[0]};

        // The clock is simply the current device time relative to the recorded
        // start time. We can also subtract the latency to get more a accurate
        // position of where the audio device actually is in the output stream.
        return device_time - mDeviceStartTime - latency;
    }

    if(!mBufferData.empty())
    {
        if(mDeviceStartTime == nanoseconds::min())
            return nanoseconds::zero();

        /* With a callback buffer and no device clock, mDeviceStartTime is
         * actually the timestamp of the first sample frame played. The audio
         * clock, then, is that plus the current source offset.
         */
        std::array<ALint64SOFT,2> offset{};
        if(alGetSourcei64vSOFT)
            alGetSourcei64vSOFT(mSource, AL_SAMPLE_OFFSET_LATENCY_SOFT, offset.data());
        else
        {
            ALint ioffset;
            alGetSourcei(mSource, AL_SAMPLE_OFFSET, &ioffset);
            offset[0] = ALint64SOFT{ioffset} << 32;
        }
        /* NOTE: The source state must be checked last, in case an underrun
         * occurs and the source stops between getting the state and retrieving
         * the offset+latency.
         */
        ALint status;
        alGetSourcei(mSource, AL_SOURCE_STATE, &status);

        nanoseconds pts{};
        if(status == AL_PLAYING || status == AL_PAUSED)
            pts = mDeviceStartTime - nanoseconds{offset[1]} +
                duration_cast<nanoseconds>(fixed32{offset[0] / mCodecCtx->sample_rate});
        else
        {
            /* If the source is stopped, the pts of the next sample to be heard
             * is the pts of the next sample to be buffered, minus the amount
             * already in the buffer ready to play.
             */
            const size_t woffset{mWritePos.load(std::memory_order_acquire)};
            const size_t roffset{mReadPos.load(std::memory_order_relaxed)};
            const size_t readable{((woffset>=roffset) ? woffset : (mBufferData.size()+woffset)) -
                roffset};

            pts = mCurrentPts - nanoseconds{seconds{readable/mFrameSize}}/mCodecCtx->sample_rate;
        }

        return pts;
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
    nanoseconds pts{mCurrentPts};
    if(mSource)
    {
        std::array<ALint64SOFT,2> offset{};
        if(alGetSourcei64vSOFT)
            alGetSourcei64vSOFT(mSource, AL_SAMPLE_OFFSET_LATENCY_SOFT, offset.data());
        else
        {
            ALint ioffset;
            alGetSourcei(mSource, AL_SAMPLE_OFFSET, &ioffset);
            offset[0] = ALint64SOFT{ioffset} << 32;
        }
        ALint queued, status;
        alGetSourcei(mSource, AL_BUFFERS_QUEUED, &queued);
        alGetSourcei(mSource, AL_SOURCE_STATE, &status);

        /* If the source is AL_STOPPED, then there was an underrun and all
         * buffers are processed, so ignore the source queue. The audio thread
         * will put the source into an AL_INITIAL state and clear the queue
         * when it starts recovery.
         */
        if(status != AL_STOPPED)
        {
            pts -= AudioBufferTime*queued;
            pts += duration_cast<nanoseconds>(fixed32{offset[0] / mCodecCtx->sample_rate});
        }
        /* Don't offset by the latency if the source isn't playing. */
        if(status == AL_PLAYING)
            pts -= nanoseconds{offset[1]};
    }

    return std::max(pts, nanoseconds::zero());
}

bool AudioState::startPlayback()
{
    const size_t woffset{mWritePos.load(std::memory_order_acquire)};
    const size_t roffset{mReadPos.load(std::memory_order_relaxed)};
    const size_t readable{((woffset >= roffset) ? woffset : (mBufferData.size()+woffset)) -
        roffset};

    if(!mBufferData.empty())
    {
        if(readable == 0)
            return false;
        if(!alcGetInteger64vSOFT)
            mDeviceStartTime = mCurrentPts -
                nanoseconds{seconds{readable/mFrameSize}}/mCodecCtx->sample_rate;
    }
    else
    {
        ALint queued{};
        alGetSourcei(mSource, AL_BUFFERS_QUEUED, &queued);
        if(queued == 0) return false;
    }

    alSourcePlay(mSource);
    if(alcGetInteger64vSOFT)
    {
        /* Subtract the total buffer queue time from the current pts to get the
         * pts of the start of the queue.
         */
        std::array<int64_t,2> srctimes{};
        alGetSourcei64vSOFT(mSource, AL_SAMPLE_OFFSET_CLOCK_SOFT, srctimes.data());
        auto device_time = nanoseconds{srctimes[1]};
        auto src_offset = duration_cast<nanoseconds>(fixed32{srctimes[0]}) /
            mCodecCtx->sample_rate;

        /* The mixer may have ticked and incremented the device time and sample
         * offset, so subtract the source offset from the device time to get
         * the device time the source started at. Also subtract startpts to get
         * the device time the stream would have started at to reach where it
         * is now.
         */
        if(!mBufferData.empty())
        {
            nanoseconds startpts{mCurrentPts -
                nanoseconds{seconds{readable/mFrameSize}}/mCodecCtx->sample_rate};
            mDeviceStartTime = device_time - src_offset - startpts;
        }
        else
        {
            nanoseconds startpts{mCurrentPts - AudioBufferTotalTime};
            mDeviceStartTime = device_time - src_offset - startpts;
        }
    }
    return true;
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
    diff = std::min<nanoseconds>(diff, AudioSampleCorrectionMax);
    return static_cast<int>(duration_cast<seconds>(diff*mCodecCtx->sample_rate).count());
}

int AudioState::decodeFrame()
{
    do {
        while(int ret{mQueue.receiveFrame(mCodecCtx.get(), mDecodedFrame.get())})
        {
            if(ret == AVErrorEOF) return 0;
            fmt::println(stderr, "Failed to receive frame: {}", ret);
        }
    } while(mDecodedFrame->nb_samples <= 0);

    /* If provided, update w/ pts */
    if(mDecodedFrame->best_effort_timestamp != AVNoPtsValue)
        mCurrentPts = duration_cast<nanoseconds>(seconds_d64{av_q2d(mStream->time_base) *
            static_cast<double>(mDecodedFrame->best_effort_timestamp)});

    if(mDecodedFrame->nb_samples > mSamplesMax)
    {
        av_freep(static_cast<void*>(mSamples.data()));
        av_samples_alloc(mSamples.data(), nullptr, mCodecCtx->ch_layout.nb_channels,
            mDecodedFrame->nb_samples, mDstSampleFmt, 0);
        mSamplesMax = mDecodedFrame->nb_samples;
        mSamplesSpan = {mSamples[0], static_cast<size_t>(mSamplesMax)*mFrameSize};
    }
    /* Return the amount of sample frames converted */
    const int data_size{swr_convert(mSwresCtx.get(), mSamples.data(), mDecodedFrame->nb_samples,
        mDecodedFrame->extended_data, mDecodedFrame->nb_samples)};

    av_frame_unref(mDecodedFrame.get());
    return data_size;
}

/* Duplicates the sample at in to out, count times. The frame size is a
 * multiple of the template type size.
 */
template<typename T>
void sample_dup(al::span<uint8_t> out, al::span<const uint8_t> in, size_t count, size_t frame_size)
{
    auto sample = al::span{reinterpret_cast<const T*>(in.data()), in.size()/sizeof(T)};
    auto dst = al::span{reinterpret_cast<T*>(out.data()), out.size()/sizeof(T)};

    /* NOTE: frame_size is a multiple of sizeof(T). */
    const size_t type_mult{frame_size / sizeof(T)};
    if(type_mult == 1)
        std::fill_n(dst.begin(), count, sample.front());
    else for(size_t i{0};i < count;++i)
    {
        for(size_t j{0};j < type_mult;++j)
            dst[i*type_mult + j] = sample[j];
    }
}

void sample_dup(al::span<uint8_t> out, al::span<const uint8_t> in, size_t count, size_t frame_size)
{
    if((frame_size&7) == 0)
        sample_dup<uint64_t>(out, in, count, frame_size);
    else if((frame_size&3) == 0)
        sample_dup<uint32_t>(out, in, count, frame_size);
    else if((frame_size&1) == 0)
        sample_dup<uint16_t>(out, in, count, frame_size);
    else
        sample_dup<uint8_t>(out, in, count, frame_size);
}

bool AudioState::readAudio(al::span<uint8_t> samples, unsigned int length, int &sample_skip)
{
    unsigned int audio_size{0};

    /* Read the next chunk of data, refill the buffer, and queue it
     * on the source */
    length /= mFrameSize;
    while(mSamplesLen > 0 && audio_size < length)
    {
        unsigned int rem{length - audio_size};
        if(mSamplesPos >= 0)
        {
            rem = std::min(rem, static_cast<unsigned int>(mSamplesLen - mSamplesPos));
            const size_t boffset{static_cast<ALuint>(mSamplesPos) * size_t{mFrameSize}};
            std::copy_n(mSamplesSpan.cbegin()+ptrdiff_t(boffset), rem*size_t{mFrameSize},
                samples.begin());
        }
        else
        {
            rem = std::min(rem, static_cast<unsigned int>(-mSamplesPos));

            /* Add samples by copying the first sample */
            sample_dup(samples, mSamplesSpan, rem, mFrameSize);
        }

        mSamplesPos += static_cast<int>(rem);
        mCurrentPts += nanoseconds{seconds{rem}} / mCodecCtx->sample_rate;
        samples = samples.subspan(rem*size_t{mFrameSize});
        audio_size += rem;

        while(mSamplesPos >= mSamplesLen)
        {
            mSamplesLen = decodeFrame();
            mSamplesPos = std::min(mSamplesLen, sample_skip);
            if(mSamplesLen <= 0) break;

            sample_skip -= mSamplesPos;

            // Adjust the device start time and current pts by the amount we're
            // skipping/duplicating, so that the clock remains correct for the
            // current stream position.
            auto skip = nanoseconds{seconds{mSamplesPos}} / mCodecCtx->sample_rate;
            mDeviceStartTime -= skip;
            mCurrentPts += skip;
        }
    }
    if(audio_size <= 0)
        return false;

    if(audio_size < length)
    {
        const unsigned int rem{length - audio_size};
        std::fill_n(samples.begin(), rem*mFrameSize,
            (mDstSampleFmt == AV_SAMPLE_FMT_U8) ? 0x80 : 0x00);
        mCurrentPts += nanoseconds{seconds{rem}} / mCodecCtx->sample_rate;
    }
    return true;
}

bool AudioState::readAudio(int sample_skip)
{
    size_t woffset{mWritePos.load(std::memory_order_acquire)};
    const size_t roffset{mReadPos.load(std::memory_order_relaxed)};
    while(mSamplesLen > 0)
    {
        const size_t nsamples{((roffset > woffset) ? roffset-woffset-1
            : (roffset == 0) ? (mBufferData.size()-woffset-1)
            : (mBufferData.size()-woffset)) / mFrameSize};
        if(!nsamples) break;

        if(mSamplesPos < 0)
        {
            const size_t rem{std::min<size_t>(nsamples, static_cast<ALuint>(-mSamplesPos))};

            sample_dup(al::span{mBufferData}.subspan(woffset), mSamplesSpan, rem, mFrameSize);
            woffset += rem * mFrameSize;
            if(woffset == mBufferData.size()) woffset = 0;
            mWritePos.store(woffset, std::memory_order_release);

            mCurrentPts += nanoseconds{seconds{rem}} / mCodecCtx->sample_rate;
            mSamplesPos += static_cast<int>(rem);
            continue;
        }

        const size_t rem{std::min<size_t>(nsamples, static_cast<ALuint>(mSamplesLen-mSamplesPos))};
        const size_t boffset{static_cast<ALuint>(mSamplesPos) * size_t{mFrameSize}};
        const size_t nbytes{rem * mFrameSize};

        std::copy_n(mSamplesSpan.cbegin()+ptrdiff_t(boffset), nbytes,
            mBufferData.begin()+ptrdiff_t(woffset));
        woffset += nbytes;
        if(woffset == mBufferData.size()) woffset = 0;
        mWritePos.store(woffset, std::memory_order_release);

        mCurrentPts += nanoseconds{seconds{rem}} / mCodecCtx->sample_rate;
        mSamplesPos += static_cast<int>(rem);

        while(mSamplesPos >= mSamplesLen)
        {
            mSamplesLen = decodeFrame();
            mSamplesPos = std::min(mSamplesLen, sample_skip);
            if(mSamplesLen <= 0) return false;

            sample_skip -= mSamplesPos;

            auto skip = nanoseconds{seconds{mSamplesPos}} / mCodecCtx->sample_rate;
            mDeviceStartTime -= skip;
            mCurrentPts += skip;
        }
    }

    return true;
}


void AL_APIENTRY AudioState::eventCallback(ALenum eventType, ALuint object, ALuint param,
    ALsizei length, const ALchar *message) noexcept
{
    if(eventType == AL_EVENT_TYPE_BUFFER_COMPLETED_SOFT)
    {
        /* Temporarily lock the source mutex to ensure it's not between
         * checking the processed count and going to sleep.
         */
        std::unique_lock<std::mutex>{mSrcMutex}.unlock();
        mSrcCond.notify_one();
        return;
    }

    fmt::print("\n---- AL Event on AudioState {:p} ----\nEvent: ", voidp{this});
    switch(eventType)
    {
    case AL_EVENT_TYPE_BUFFER_COMPLETED_SOFT: fmt::print("Buffer completed"); break;
    case AL_EVENT_TYPE_SOURCE_STATE_CHANGED_SOFT: fmt::print("Source state changed"); break;
    case AL_EVENT_TYPE_DISCONNECTED_SOFT: fmt::print("Disconnected"); break;
    default: fmt::print("{:#x}", as_unsigned(eventType)); break;
    }
    fmt::println("\n"
        "Object ID: {}\n"
        "Parameter: {}\n"
        "Message: {}\n----",
        object, param, std::string_view{message, static_cast<ALuint>(length)});

    if(eventType == AL_EVENT_TYPE_DISCONNECTED_SOFT)
    {
        {
            std::lock_guard<std::mutex> lock{mSrcMutex};
            mConnected.clear(std::memory_order_release);
        }
        mSrcCond.notify_one();
    }
}

ALsizei AudioState::bufferCallback(void *data, ALsizei size) noexcept
{
    auto dst = al::span{static_cast<ALbyte*>(data), static_cast<ALuint>(size)};
    ALsizei got{0};

    size_t roffset{mReadPos.load(std::memory_order_acquire)};
    while(!dst.empty())
    {
        const size_t woffset{mWritePos.load(std::memory_order_relaxed)};
        if(woffset == roffset) break;

        size_t todo{((woffset < roffset) ? mBufferData.size() : woffset) - roffset};
        todo = std::min(todo, dst.size());

        std::copy_n(mBufferData.cbegin()+ptrdiff_t(roffset), todo, dst.begin());
        dst = dst.subspan(todo);
        got += static_cast<ALsizei>(todo);

        roffset += todo;
        if(roffset == mBufferData.size())
            roffset = 0;
    }
    mReadPos.store(roffset, std::memory_order_release);

    return got;
}

int AudioState::handler()
{
    std::unique_lock<std::mutex> srclock{mSrcMutex, std::defer_lock};
    milliseconds sleep_time{AudioBufferTime / 3};

    struct EventControlManager {
        const std::array<ALenum,3> evt_types{{
            AL_EVENT_TYPE_BUFFER_COMPLETED_SOFT, AL_EVENT_TYPE_SOURCE_STATE_CHANGED_SOFT,
            AL_EVENT_TYPE_DISCONNECTED_SOFT}};

        explicit EventControlManager(milliseconds &sleep_time)
        {
            if(alEventControlSOFT)
            {
                alEventControlSOFT(static_cast<ALsizei>(evt_types.size()), evt_types.data(),
                    AL_TRUE);
                alEventCallbackSOFT(&AudioState::eventCallbackC, this);
                sleep_time = AudioBufferTotalTime;
            }
        }
        ~EventControlManager()
        {
            if(alEventControlSOFT)
            {
                alEventControlSOFT(static_cast<ALsizei>(evt_types.size()), evt_types.data(),
                    AL_FALSE);
                alEventCallbackSOFT(nullptr, nullptr);
            }
        }
    };
    EventControlManager event_controller{sleep_time};

    std::vector<uint8_t> samples;
    ALsizei buffer_len{0};

    /* Note that ffmpeg assumes AmbiX (ACN layout, SN3D normalization). Only
     * support HOA when OpenAL can take AmbiX natively (if AmbiX -> FuMa
     * conversion is needed, we don't bother with higher order channels).
     */
    const auto has_bfmt = bool{alIsExtensionPresent("AL_EXT_BFORMAT") != AL_FALSE};
    const auto has_bfmt_ex = bool{alIsExtensionPresent("AL_SOFT_bformat_ex") != AL_FALSE};
    const auto has_bfmt_hoa = bool{has_bfmt_ex
        && alIsExtensionPresent("AL_SOFT_bformat_hoa") != AL_FALSE};
    /* AL_SOFT_bformat_hoa supports up to 14th order (225 channels). */
    const auto max_ambi_order = has_bfmt_hoa ? 14 : 1;
    auto ambi_order = 0;

    /* Find a suitable format for OpenAL. */
    const auto layoutmask = mCodecCtx->ch_layout.u.mask; /* NOLINT(*-union-access) */
    mDstChanLayout = 0;
    mFormat = AL_NONE;
    if((mCodecCtx->sample_fmt == AV_SAMPLE_FMT_FLT || mCodecCtx->sample_fmt == AV_SAMPLE_FMT_FLTP
            || mCodecCtx->sample_fmt == AV_SAMPLE_FMT_DBL
            || mCodecCtx->sample_fmt == AV_SAMPLE_FMT_DBLP
            || mCodecCtx->sample_fmt == AV_SAMPLE_FMT_S32
            || mCodecCtx->sample_fmt == AV_SAMPLE_FMT_S32P
            || mCodecCtx->sample_fmt == AV_SAMPLE_FMT_S64
            || mCodecCtx->sample_fmt == AV_SAMPLE_FMT_S64P)
        && alIsExtensionPresent("AL_EXT_FLOAT32"))
    {
        mDstSampleFmt = AV_SAMPLE_FMT_FLT;
        mFrameSize = 4;
        if(mCodecCtx->ch_layout.order == AV_CHANNEL_ORDER_NATIVE)
        {
            if(alIsExtensionPresent("AL_EXT_MCFORMATS"))
            {
                if(layoutmask == AV_CH_LAYOUT_7POINT1)
                {
                    mDstChanLayout = layoutmask;
                    mFrameSize *= 8;
                    mFormat = alGetEnumValue("AL_FORMAT_71CHN32");
                }
                if(layoutmask == AV_CH_LAYOUT_5POINT1 || layoutmask == AV_CH_LAYOUT_5POINT1_BACK)
                {
                    mDstChanLayout = layoutmask;
                    mFrameSize *= 6;
                    mFormat = alGetEnumValue("AL_FORMAT_51CHN32");
                }
                if(layoutmask == AV_CH_LAYOUT_QUAD)
                {
                    mDstChanLayout = layoutmask;
                    mFrameSize *= 4;
                    mFormat = EnableUhj ? AL_FORMAT_UHJ4CHN_FLOAT32_SOFT
                        : alGetEnumValue("AL_FORMAT_QUAD32");
                }
            }
            if(layoutmask == AV_CH_LAYOUT_MONO)
            {
                mDstChanLayout = layoutmask;
                mFrameSize *= 1;
                mFormat = AL_FORMAT_MONO_FLOAT32;
            }
        }
        else if(mCodecCtx->ch_layout.order == AV_CHANNEL_ORDER_AMBISONIC && has_bfmt)
        {
            /* Calculate what should be the ambisonic order from the number of
             * channels, and confirm that's the number of channels. Opus allows
             * an optional non-diegetic stereo stream with the B-Format stream,
             * which we can ignore, so check for that too.
             */
            auto order = static_cast<int>(std::sqrt(mCodecCtx->ch_layout.nb_channels)) - 1;
            auto channels = ALuint(order+1) * ALuint(order+1);
            if(channels == ALuint(mCodecCtx->ch_layout.nb_channels)
                || channels+2 == ALuint(mCodecCtx->ch_layout.nb_channels))
            {
                /* OpenAL only supports first-order with AL_EXT_BFORMAT, which
                 * is 4 channels for 3D buffers, unless AL_SOFT_bformat_hoa is
                 * also supported.
                 */
                ambi_order = std::min(order, max_ambi_order);
                mFrameSize *= ALuint(ambi_order+1) * ALuint(ambi_order+1);
                mFormat = alGetEnumValue("AL_FORMAT_BFORMAT3D_FLOAT32");
            }
        }
        if(!mFormat || mFormat == -1)
        {
            mDstChanLayout = AV_CH_LAYOUT_STEREO;
            mFrameSize *= 2;
            mFormat = EnableUhj ? AL_FORMAT_UHJ2CHN_FLOAT32_SOFT : AL_FORMAT_STEREO_FLOAT32;
        }
    }
    if(mCodecCtx->sample_fmt == AV_SAMPLE_FMT_U8 || mCodecCtx->sample_fmt == AV_SAMPLE_FMT_U8P)
    {
        mDstSampleFmt = AV_SAMPLE_FMT_U8;
        mFrameSize = 1;
        if(mCodecCtx->ch_layout.order == AV_CHANNEL_ORDER_NATIVE)
        {
            if(alIsExtensionPresent("AL_EXT_MCFORMATS"))
            {
                if(layoutmask == AV_CH_LAYOUT_7POINT1)
                {
                    mDstChanLayout = layoutmask;
                    mFrameSize *= 8;
                    mFormat = alGetEnumValue("AL_FORMAT_71CHN8");
                }
                if(layoutmask == AV_CH_LAYOUT_5POINT1 || layoutmask == AV_CH_LAYOUT_5POINT1_BACK)
                {
                    mDstChanLayout = layoutmask;
                    mFrameSize *= 6;
                    mFormat = alGetEnumValue("AL_FORMAT_51CHN8");
                }
                if(layoutmask == AV_CH_LAYOUT_QUAD)
                {
                    mDstChanLayout = layoutmask;
                    mFrameSize *= 4;
                    mFormat = EnableUhj ? AL_FORMAT_UHJ4CHN8_SOFT
                        : alGetEnumValue("AL_FORMAT_QUAD8");
                }
            }
            if(layoutmask == AV_CH_LAYOUT_MONO)
            {
                mDstChanLayout = layoutmask;
                mFrameSize *= 1;
                mFormat = AL_FORMAT_MONO8;
            }
        }
        else if(mCodecCtx->ch_layout.order == AV_CHANNEL_ORDER_AMBISONIC && has_bfmt)
        {
            auto order = static_cast<int>(std::sqrt(mCodecCtx->ch_layout.nb_channels)) - 1;
            auto channels = (order+1) * (order+1);
            if(channels == mCodecCtx->ch_layout.nb_channels
                || channels+2 == mCodecCtx->ch_layout.nb_channels)
            {
                ambi_order = std::min(order, max_ambi_order);
                mFrameSize *= ALuint(ambi_order+1) * ALuint(ambi_order+1);
                mFormat = alGetEnumValue("AL_FORMAT_BFORMAT3D_8");
            }
        }
        if(!mFormat || mFormat == -1)
        {
            mDstChanLayout = AV_CH_LAYOUT_STEREO;
            mFrameSize *= 2;
            mFormat = EnableUhj ? AL_FORMAT_UHJ2CHN8_SOFT : AL_FORMAT_STEREO8;
        }
    }
    if(!mFormat || mFormat == -1)
    {
        mDstSampleFmt = AV_SAMPLE_FMT_S16;
        mFrameSize = 2;
        if(mCodecCtx->ch_layout.order == AV_CHANNEL_ORDER_NATIVE)
        {
            if(alIsExtensionPresent("AL_EXT_MCFORMATS"))
            {
                if(layoutmask == AV_CH_LAYOUT_7POINT1)
                {
                    mDstChanLayout = layoutmask;
                    mFrameSize *= 8;
                    mFormat = alGetEnumValue("AL_FORMAT_71CHN16");
                }
                if(layoutmask == AV_CH_LAYOUT_5POINT1 || layoutmask == AV_CH_LAYOUT_5POINT1_BACK)
                {
                    mDstChanLayout = layoutmask;
                    mFrameSize *= 6;
                    mFormat = alGetEnumValue("AL_FORMAT_51CHN16");
                }
                if(layoutmask == AV_CH_LAYOUT_QUAD)
                {
                    mDstChanLayout = layoutmask;
                    mFrameSize *= 4;
                    mFormat = EnableUhj ? AL_FORMAT_UHJ4CHN16_SOFT
                        : alGetEnumValue("AL_FORMAT_QUAD16");
                }
            }
            if(layoutmask == AV_CH_LAYOUT_MONO)
            {
                mDstChanLayout = layoutmask;
                mFrameSize *= 1;
                mFormat = AL_FORMAT_MONO16;
            }
        }
        else if(mCodecCtx->ch_layout.order == AV_CHANNEL_ORDER_AMBISONIC && has_bfmt)
        {
            auto order = static_cast<int>(std::sqrt(mCodecCtx->ch_layout.nb_channels)) - 1;
            auto channels = (order+1) * (order+1);
            if(channels == mCodecCtx->ch_layout.nb_channels
                || channels+2 == mCodecCtx->ch_layout.nb_channels)
            {
                ambi_order = std::min(order, max_ambi_order);
                mFrameSize *= ALuint(ambi_order+1) * ALuint(ambi_order+1);
                mFormat = alGetEnumValue("AL_FORMAT_BFORMAT3D_16");
            }
        }
        if(!mFormat || mFormat == -1)
        {
            mDstChanLayout = AV_CH_LAYOUT_STEREO;
            mFrameSize *= 2;
            mFormat = EnableUhj ? AL_FORMAT_UHJ2CHN16_SOFT : AL_FORMAT_STEREO16;
        }
    }

    mSamples.fill(nullptr);
    mSamplesSpan = {};
    mSamplesMax = 0;
    mSamplesPos = 0;
    mSamplesLen = 0;

    mDecodedFrame.reset(av_frame_alloc());
    if(!mDecodedFrame)
    {
        fmt::println(stderr, "Failed to allocate audio frame");
        return 0;
    }

    if(!mDstChanLayout)
    {
        auto layout = ChannelLayout{};
        av_channel_layout_from_string(&layout, fmt::format("ambisonic {}", ambi_order).c_str());

        const auto err = swr_alloc_set_opts2(al::out_ptr(mSwresCtx), &layout, mDstSampleFmt,
            mCodecCtx->sample_rate, &mCodecCtx->ch_layout, mCodecCtx->sample_fmt,
            mCodecCtx->sample_rate, 0, nullptr);
        if(err != 0)
        {
            std::array<char,AV_ERROR_MAX_STRING_SIZE> errstr{};
            fmt::println(stderr, "Failed to allocate SwrContext: {}",
                av_make_error_string(errstr.data(), AV_ERROR_MAX_STRING_SIZE, err));
            return 0;
        }

        if(has_bfmt_hoa && ambi_order > 1)
            fmt::println("Found AL_SOFT_bformat_hoa (order {})", ambi_order);
        else if(has_bfmt_ex)
            fmt::println("Found AL_SOFT_bformat_ex");
        else
        {
            fmt::println("Found AL_EXT_BFORMAT");
            /* Without AL_SOFT_bformat_ex, OpenAL only supports FuMa channel
             * ordering and normalization, so a custom matrix is needed to
             * scale and reorder the source from AmbiX.
             */
            std::vector<double> mtx(64_uz*64_uz, 0.0);
            mtx[0 + 0*64] = std::sqrt(0.5);
            mtx[3 + 1*64] = 1.0;
            mtx[1 + 2*64] = 1.0;
            mtx[2 + 3*64] = 1.0;
            swr_set_matrix(mSwresCtx.get(), mtx.data(), 64);
        }
    }
    else
    {
        ChannelLayout layout{};
        av_channel_layout_from_mask(&layout, mDstChanLayout);

        int err{swr_alloc_set_opts2(al::out_ptr(mSwresCtx), &layout, mDstSampleFmt,
            mCodecCtx->sample_rate, &mCodecCtx->ch_layout, mCodecCtx->sample_fmt,
            mCodecCtx->sample_rate, 0, nullptr)};
        if(err != 0)
        {
            std::array<char,AV_ERROR_MAX_STRING_SIZE> errstr{};
            fmt::println(stderr, "Failed to allocate SwrContext: {}",
                av_make_error_string(errstr.data(), AV_ERROR_MAX_STRING_SIZE, err));
            return 0;
        }
    }
    if(int err{swr_init(mSwresCtx.get())})
    {
        std::array<char,AV_ERROR_MAX_STRING_SIZE> errstr{};
        fmt::println(stderr, "Failed to initialize audio converter: {}",
            av_make_error_string(errstr.data(), AV_ERROR_MAX_STRING_SIZE, err));
        return 0;
    }

    alGenBuffers(static_cast<ALsizei>(mBuffers.size()), mBuffers.data());
    alGenSources(1, &mSource);

    if(DirectOutMode)
        alSourcei(mSource, AL_DIRECT_CHANNELS_SOFT, DirectOutMode);
    if(EnableWideStereo)
    {
        static constexpr std::array angles{static_cast<float>(al::numbers::pi / 3.0),
            static_cast<float>(-al::numbers::pi / 3.0)};
        alSourcefv(mSource, AL_STEREO_ANGLES, angles.data());
    }
    if(has_bfmt_ex)
    {
        for(ALuint bufid : mBuffers)
        {
            alBufferi(bufid, AL_AMBISONIC_LAYOUT_SOFT, AL_ACN_SOFT);
            alBufferi(bufid, AL_AMBISONIC_SCALING_SOFT, AL_SN3D_SOFT);
        }
    }
    if(ambi_order > 1)
    {
        for(ALuint bufid : mBuffers)
            alBufferi(bufid, AL_UNPACK_AMBISONIC_ORDER_SOFT, ambi_order);
    }
#ifdef AL_SOFT_UHJ
    if(EnableSuperStereo)
        alSourcei(mSource, AL_STEREO_MODE_SOFT, AL_SUPER_STEREO_SOFT);
#endif

    if(alGetError() != AL_NO_ERROR)
        return 0;

    bool callback_ok{false};
    if(alBufferCallbackSOFT)
    {
        alBufferCallbackSOFT(mBuffers[0], mFormat, mCodecCtx->sample_rate, bufferCallbackC, this);
        alSourcei(mSource, AL_BUFFER, static_cast<ALint>(mBuffers[0]));
        if(alGetError() != AL_NO_ERROR)
        {
            fmt::println(stderr, "Failed to set buffer callback");
            alSourcei(mSource, AL_BUFFER, 0);
        }
        else
        {
            mBufferData.resize(static_cast<size_t>(duration_cast<seconds>(mCodecCtx->sample_rate *
                AudioBufferTotalTime).count()) * mFrameSize);
            std::fill(mBufferData.begin(), mBufferData.end(), uint8_t{});

            mReadPos.store(0, std::memory_order_relaxed);
            mWritePos.store(mBufferData.size()/mFrameSize/2*mFrameSize, std::memory_order_relaxed);

            ALCint refresh{};
            alcGetIntegerv(alcGetContextsDevice(alcGetCurrentContext()), ALC_REFRESH, 1, &refresh);
            sleep_time = milliseconds{seconds{1}} / refresh;
            callback_ok = true;
        }
    }
    if(!callback_ok)
        buffer_len = static_cast<int>(duration_cast<seconds>(mCodecCtx->sample_rate *
            AudioBufferTime).count() * mFrameSize);
    if(buffer_len > 0)
        samples.resize(static_cast<ALuint>(buffer_len));

    /* Prefill the codec buffer. */
    auto packet_sender = [this]()
    {
        while(true)
        {
            const int ret{mQueue.sendPacket(mCodecCtx.get())};
            if(ret == AVErrorEOF) break;
        }
    };
    auto sender [[maybe_unused]] = std::async(std::launch::async, packet_sender);

    srclock.lock();
    if(alcGetInteger64vSOFT)
    {
        int64_t devtime{};
        alcGetInteger64vSOFT(alcGetContextsDevice(alcGetCurrentContext()), ALC_DEVICE_CLOCK_SOFT,
            1, &devtime);
        mDeviceStartTime = nanoseconds{devtime} - mCurrentPts;
    }

    mSamplesLen = decodeFrame();
    if(mSamplesLen > 0)
    {
        mSamplesPos = std::min(mSamplesLen, getSync());

        auto skip = nanoseconds{seconds{mSamplesPos}} / mCodecCtx->sample_rate;
        mDeviceStartTime -= skip;
        mCurrentPts += skip;
    }

    while(true)
    {
        if(mMovie.mQuit.load(std::memory_order_relaxed))
        {
            /* If mQuit is set, drain frames until we can't get more audio,
             * indicating we've reached the flush packet and the packet sender
             * will also quit.
             */
            do {
                mSamplesLen = decodeFrame();
                mSamplesPos = mSamplesLen;
            } while(mSamplesLen > 0);
            break;
        }

        ALenum state;
        if(!mBufferData.empty())
        {
            alGetSourcei(mSource, AL_SOURCE_STATE, &state);

            /* If mQuit is not set, don't quit even if there's no more audio,
             * so what's buffered has a chance to play to the real end.
             */
            readAudio(getSync());
        }
        else
        {
            ALint processed, queued;

            /* First remove any processed buffers. */
            alGetSourcei(mSource, AL_BUFFERS_PROCESSED, &processed);
            while(processed > 0)
            {
                ALuint bid;
                alSourceUnqueueBuffers(mSource, 1, &bid);
                --processed;
            }

            /* Refill the buffer queue. */
            int sync_skip{getSync()};
            alGetSourcei(mSource, AL_BUFFERS_QUEUED, &queued);
            while(static_cast<ALuint>(queued) < mBuffers.size())
            {
                /* Read the next chunk of data, filling the buffer, and queue
                 * it on the source.
                 */
                if(!readAudio(samples, static_cast<ALuint>(buffer_len), sync_skip))
                    break;

                const ALuint bufid{mBuffers[mBufferIdx]};
                mBufferIdx = static_cast<ALuint>((mBufferIdx+1) % mBuffers.size());

                alBufferData(bufid, mFormat, samples.data(), buffer_len, mCodecCtx->sample_rate);
                alSourceQueueBuffers(mSource, 1, &bufid);
                ++queued;
            }

            /* Check that the source is playing. */
            alGetSourcei(mSource, AL_SOURCE_STATE, &state);
            if(state == AL_STOPPED)
            {
                /* AL_STOPPED means there was an underrun. Clear the buffer
                 * queue since this likely means we're late, and rewind the
                 * source to get it back into an AL_INITIAL state.
                 */
                alSourceRewind(mSource);
                alSourcei(mSource, AL_BUFFER, 0);
                if(alcGetInteger64vSOFT)
                {
                    /* Also update the device start time with the current
                     * device clock, so the decoder knows we're running behind.
                     */
                    int64_t devtime{};
                    alcGetInteger64vSOFT(alcGetContextsDevice(alcGetCurrentContext()),
                        ALC_DEVICE_CLOCK_SOFT, 1, &devtime);
                    mDeviceStartTime = nanoseconds{devtime} - mCurrentPts;
                }
                continue;
            }
        }

        /* (re)start the source if needed, and wait for a buffer to finish */
        if(state != AL_PLAYING && state != AL_PAUSED)
        {
            if(!startPlayback())
                break;
        }
        if(ALenum err{alGetError()})
            fmt::println(stderr, "Got AL error: {:#x} ({})", as_unsigned(err), alGetString(err));

        mSrcCond.wait_for(srclock, sleep_time);
    }

    alSourceRewind(mSource);
    alSourcei(mSource, AL_BUFFER, 0);
    srclock.unlock();

    return 0;
}


nanoseconds VideoState::getClock()
{
    /* NOTE: This returns incorrect times while not playing. */
    std::lock_guard<std::mutex> displock{mDispPtsMutex};
    if(mDisplayPtsTime == microseconds::min())
        return nanoseconds::zero();
    auto delta = get_avtime() - mDisplayPtsTime;
    return mDisplayPts + delta;
}

/* Called by VideoState::updateVideo to display the next video frame. */
void VideoState::display(SDL_Renderer *renderer, AVFrame *frame) const
{
    if(!mImage)
        return;

    auto frame_width = frame->width - static_cast<int>(frame->crop_left + frame->crop_right);
    auto frame_height = frame->height - static_cast<int>(frame->crop_top + frame->crop_bottom);

    const auto src_rect = SDL_FRect{ static_cast<float>(frame->crop_left),
        static_cast<float>(frame->crop_top), static_cast<float>(frame_width),
        static_cast<float>(frame_height) };

    SDL_RenderTexture(renderer, mImage, &src_rect, nullptr);
    SDL_RenderPresent(renderer);
}

/* Called regularly on the main thread where the SDL_Renderer was created. It
 * handles updating the textures of decoded frames and displaying the latest
 * frame.
 */
void VideoState::updateVideo(SDL_Window *screen, SDL_Renderer *renderer, bool redraw)
{
    size_t read_idx{mPictQRead.load(std::memory_order_relaxed)};
    Picture *vp{&mPictQ[read_idx]};

    auto clocktime = mMovie.getMasterClock();
    bool updated{false};
    while(true)
    {
        size_t next_idx{(read_idx+1)%mPictQ.size()};
        if(next_idx == mPictQWrite.load(std::memory_order_acquire))
            break;
        Picture *nextvp{&mPictQ[next_idx]};
        if(clocktime < nextvp->mPts && !mMovie.mQuit.load(std::memory_order_relaxed))
        {
            /* For the first update, ensure the first frame gets shown.  */
            if(!mFirstUpdate || updated)
                break;
        }

        vp = nextvp;
        updated = true;
        read_idx = next_idx;
    }
    if(mMovie.mQuit.load(std::memory_order_relaxed))
    {
        if(mEOS)
            mFinalUpdate = true;
        mPictQRead.store(read_idx, std::memory_order_release);
        std::unique_lock<std::mutex>{mPictQMutex}.unlock();
        mPictQCond.notify_one();
        return;
    }

    AVFrame *frame{vp->mFrame.get()};
    if(updated)
    {
        mPictQRead.store(read_idx, std::memory_order_release);
        std::unique_lock<std::mutex>{mPictQMutex}.unlock();
        mPictQCond.notify_one();

        /* allocate or resize the buffer! */
        if(!mImage || mWidth != frame->width || mHeight != frame->height
            || frame->format != mAVFormat)
        {
            if(mImage)
                SDL_DestroyTexture(mImage);
            mImage = nullptr;
            mSwscaleCtx = nullptr;

            auto fmtiter = std::find_if(TextureFormatMap.begin(), TextureFormatMap.end(),
                [frame](const TextureFormatEntry &entry) noexcept
                { return frame->format == entry.avformat; });
            if(fmtiter != TextureFormatMap.end())
            {
                auto props = SDLProps{};
                props.setInt(SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, fmtiter->sdlformat);
                props.setInt(SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STREAMING);
                props.setInt(SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, frame->width);
                props.setInt(SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, frame->height);

                /* Should be a better way to check YCbCr vs RGB. */
                const auto ctype = (frame->format == AV_PIX_FMT_YUV420P
                    || frame->format == AV_PIX_FMT_YUYV422
                    || frame->format == AV_PIX_FMT_UYVY422 || frame->format == AV_PIX_FMT_NV12
                    || frame->format == AV_PIX_FMT_NV21) ? SDL_COLOR_TYPE_YCBCR
                    : SDL_COLOR_TYPE_RGB;
                const auto crange = std::invoke([frame]
                {
                    switch(frame->color_range)
                    {
                    case AVCOL_RANGE_UNSPECIFIED: return SDL_COLOR_RANGE_UNKNOWN;
                    case AVCOL_RANGE_MPEG: return SDL_COLOR_RANGE_LIMITED;
                    case AVCOL_RANGE_JPEG: return SDL_COLOR_RANGE_FULL;
                    case AVCOL_RANGE_NB: break;
                    }
                    return SDL_COLOR_RANGE_UNKNOWN;
                });
                const auto cprims = std::invoke([frame]
                {
                    switch(frame->color_primaries)
                    {
                    case AVCOL_PRI_RESERVED0: break;
                    case AVCOL_PRI_BT709: return SDL_COLOR_PRIMARIES_BT709;
                    case AVCOL_PRI_UNSPECIFIED: return SDL_COLOR_PRIMARIES_UNSPECIFIED;
                    case AVCOL_PRI_RESERVED: break;
                    case AVCOL_PRI_BT470M: return SDL_COLOR_PRIMARIES_BT470M;
                    case AVCOL_PRI_BT470BG: return SDL_COLOR_PRIMARIES_BT470BG;
                    case AVCOL_PRI_SMPTE170M: return SDL_COLOR_PRIMARIES_BT601;
                    case AVCOL_PRI_SMPTE240M: return SDL_COLOR_PRIMARIES_SMPTE240;
                    case AVCOL_PRI_FILM: return SDL_COLOR_PRIMARIES_GENERIC_FILM;
                    case AVCOL_PRI_BT2020: return SDL_COLOR_PRIMARIES_BT2020;
                    case AVCOL_PRI_SMPTE428: return SDL_COLOR_PRIMARIES_XYZ;
                    case AVCOL_PRI_SMPTE431: return SDL_COLOR_PRIMARIES_SMPTE431;
                    case AVCOL_PRI_SMPTE432: return SDL_COLOR_PRIMARIES_SMPTE432;
                    case AVCOL_PRI_EBU3213: return SDL_COLOR_PRIMARIES_EBU3213;
                    case AVCOL_PRI_NB: break;
                    }
                    return SDL_COLOR_PRIMARIES_UNKNOWN;
                });
                const auto ctransfer = std::invoke([frame]
                {
                    switch(frame->color_trc)
                    {
                    case AVCOL_TRC_RESERVED0: break;
                    case AVCOL_TRC_BT709: return SDL_TRANSFER_CHARACTERISTICS_BT709;
                    case AVCOL_TRC_UNSPECIFIED: return SDL_TRANSFER_CHARACTERISTICS_UNSPECIFIED;
                    case AVCOL_TRC_RESERVED: break;
                    case AVCOL_TRC_GAMMA22: return SDL_TRANSFER_CHARACTERISTICS_GAMMA22;
                    case AVCOL_TRC_GAMMA28: return SDL_TRANSFER_CHARACTERISTICS_GAMMA28;
                    case AVCOL_TRC_SMPTE170M: return SDL_TRANSFER_CHARACTERISTICS_BT601;
                    case AVCOL_TRC_SMPTE240M: return SDL_TRANSFER_CHARACTERISTICS_SMPTE240;
                    case AVCOL_TRC_LINEAR: return SDL_TRANSFER_CHARACTERISTICS_LINEAR;
                    case AVCOL_TRC_LOG: return SDL_TRANSFER_CHARACTERISTICS_LOG100;
                    case AVCOL_TRC_LOG_SQRT: return SDL_TRANSFER_CHARACTERISTICS_LOG100_SQRT10;
                    case AVCOL_TRC_IEC61966_2_4: return SDL_TRANSFER_CHARACTERISTICS_IEC61966;
                    case AVCOL_TRC_BT1361_ECG: return SDL_TRANSFER_CHARACTERISTICS_BT1361;
                    case AVCOL_TRC_IEC61966_2_1: return SDL_TRANSFER_CHARACTERISTICS_SRGB;
                    case AVCOL_TRC_BT2020_10: return SDL_TRANSFER_CHARACTERISTICS_BT2020_10BIT;
                    case AVCOL_TRC_BT2020_12: return SDL_TRANSFER_CHARACTERISTICS_BT2020_12BIT;
                    case AVCOL_TRC_SMPTE2084: return SDL_TRANSFER_CHARACTERISTICS_PQ;
                    case AVCOL_TRC_SMPTE428: return SDL_TRANSFER_CHARACTERISTICS_SMPTE428;
                    case AVCOL_TRC_ARIB_STD_B67: return SDL_TRANSFER_CHARACTERISTICS_HLG;
                    case AVCOL_TRC_NB: break;
                    }
                    return SDL_TRANSFER_CHARACTERISTICS_UNKNOWN;
                });
                const auto cmatrix = std::invoke([frame]
                {
                    switch(frame->colorspace)
                    {
                    case AVCOL_SPC_RGB: return SDL_MATRIX_COEFFICIENTS_IDENTITY;
                    case AVCOL_SPC_BT709: return SDL_MATRIX_COEFFICIENTS_BT709;
                    case AVCOL_SPC_UNSPECIFIED: return SDL_MATRIX_COEFFICIENTS_UNSPECIFIED;
                    case AVCOL_SPC_RESERVED: break;
                    case AVCOL_SPC_FCC: return SDL_MATRIX_COEFFICIENTS_FCC;
                    case AVCOL_SPC_BT470BG: return SDL_MATRIX_COEFFICIENTS_BT470BG;
                    case AVCOL_SPC_SMPTE170M: return SDL_MATRIX_COEFFICIENTS_BT601;
                    case AVCOL_SPC_SMPTE240M: return SDL_MATRIX_COEFFICIENTS_SMPTE240;
                    case AVCOL_SPC_YCGCO: return SDL_MATRIX_COEFFICIENTS_YCGCO;
                    case AVCOL_SPC_BT2020_NCL: return SDL_MATRIX_COEFFICIENTS_BT2020_NCL;
                    case AVCOL_SPC_BT2020_CL: return SDL_MATRIX_COEFFICIENTS_BT2020_CL;
                    case AVCOL_SPC_SMPTE2085: return SDL_MATRIX_COEFFICIENTS_SMPTE2085;
                    case AVCOL_SPC_CHROMA_DERIVED_NCL: return SDL_MATRIX_COEFFICIENTS_CHROMA_DERIVED_NCL;
                    case AVCOL_SPC_CHROMA_DERIVED_CL: return SDL_MATRIX_COEFFICIENTS_CHROMA_DERIVED_CL;
                    case AVCOL_SPC_ICTCP: return SDL_MATRIX_COEFFICIENTS_ICTCP;
                    case AVCOL_SPC_IPT_C2: break; // ???
                    case AVCOL_SPC_YCGCO_RE: return SDL_MATRIX_COEFFICIENTS_YCGCO; // ???
                    case AVCOL_SPC_YCGCO_RO: return SDL_MATRIX_COEFFICIENTS_YCGCO; // ???
                    case AVCOL_SPC_NB: break;
                    }
                    return SDL_MATRIX_COEFFICIENTS_UNSPECIFIED;
                });
                const auto cchromaloc = std::invoke([frame]
                {
                    switch(frame->chroma_location)
                    {
                    case AVCHROMA_LOC_UNSPECIFIED: return SDL_CHROMA_LOCATION_NONE;
                    case AVCHROMA_LOC_LEFT: return SDL_CHROMA_LOCATION_LEFT;
                    case AVCHROMA_LOC_CENTER: return SDL_CHROMA_LOCATION_CENTER;
                    case AVCHROMA_LOC_TOPLEFT: return SDL_CHROMA_LOCATION_TOPLEFT;
                    case AVCHROMA_LOC_TOP: return SDL_CHROMA_LOCATION_TOPLEFT; // ???
                    case AVCHROMA_LOC_BOTTOMLEFT: return SDL_CHROMA_LOCATION_LEFT; // ???
                    case AVCHROMA_LOC_BOTTOM: return SDL_CHROMA_LOCATION_CENTER; // ???
                    case AVCHROMA_LOC_NB: break;
                    }
                    return SDL_CHROMA_LOCATION_NONE;
                });

                const auto colorspace = DefineSDLColorspace(ctype, crange, cprims, ctransfer,
                    cmatrix, cchromaloc);
                props.setInt(SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, colorspace);

                mImage = SDL_CreateTextureWithProperties(renderer, props.getid());
                if(!mImage)
                    fmt::println(stderr, "Failed to create texture!");
                mWidth = frame->width;
                mHeight = frame->height;
                mSDLFormat = fmtiter->sdlformat;
                mAVFormat = fmtiter->avformat;
            }
            else
            {
                /* If there's no matching format, convert to RGB24. */
                fmt::println(stderr, "Could not find SDL texture format for pix_fmt {0:#x} ({0})",
                    as_unsigned(frame->format));

                auto props = SDLProps{};
                props.setInt(SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGB24);
                props.setInt(SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STREAMING);
                props.setInt(SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, frame->width);
                props.setInt(SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, frame->height);

                mImage = SDL_CreateTextureWithProperties(renderer, props.getid());
                if(!mImage)
                    fmt::println(stderr, "Failed to create texture!");
                mWidth = frame->width;
                mHeight = frame->height;
                mSDLFormat = SDL_PIXELFORMAT_RGB24;
                mAVFormat = frame->format;

                mSwscaleCtx = SwsContextPtr{sws_getContext(
                    frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                    frame->width, frame->height, AV_PIX_FMT_RGB24, 0,
                    nullptr, nullptr, nullptr)};

                sws_setColorspaceDetails(mSwscaleCtx.get(), sws_getCoefficients(frame->colorspace),
                    (frame->color_range==AVCOL_RANGE_JPEG), sws_getCoefficients(SWS_CS_DEFAULT), 1,
                    0<<16, 1<<16, 1<<16);
            }
        }

        int frame_width{frame->width - static_cast<int>(frame->crop_left + frame->crop_right)};
        int frame_height{frame->height - static_cast<int>(frame->crop_top + frame->crop_bottom)};
        if(mFirstUpdate && frame_width > 0 && frame_height > 0)
        {
            /* For the first update, set the window size to the video size. */
            mFirstUpdate = false;

            if(frame->sample_aspect_ratio.den != 0)
            {
                double aspect_ratio = av_q2d(frame->sample_aspect_ratio);
                if(aspect_ratio >= 1.0)
                    frame_width = static_cast<int>(std::lround(frame_width * aspect_ratio));
                else if(aspect_ratio > 0.0)
                    frame_height = static_cast<int>(std::lround(frame_height / aspect_ratio));
            }
            if(SDL_SetWindowSize(screen, frame_width, frame_height))
                SDL_SyncWindow(screen);
            SDL_SetRenderLogicalPresentation(renderer, frame_width, frame_height,
                SDL_LOGICAL_PRESENTATION_LETTERBOX);
        }

        if(mImage)
        {
            if(mSDLFormat == SDL_PIXELFORMAT_IYUV || mSDLFormat == SDL_PIXELFORMAT_YV12)
                SDL_UpdateYUVTexture(mImage, nullptr,
                    frame->data[0], frame->linesize[0],
                    frame->data[1], frame->linesize[1],
                    frame->data[2], frame->linesize[2]);
            else if(mSDLFormat == SDL_PIXELFORMAT_NV12 || mSDLFormat == SDL_PIXELFORMAT_NV21)
                SDL_UpdateNVTexture(mImage, nullptr,
                    frame->data[0], frame->linesize[0],
                    frame->data[1], frame->linesize[1]);
            else if(mSwscaleCtx)
            {
                auto pixels = voidp{};
                auto pitch = int{};
                if(!SDL_LockTexture(mImage, nullptr, &pixels, &pitch))
                    fmt::println(stderr, "Failed to lock texture: {}", SDL_GetError());
                else
                {
                    /* Formats passing through mSwscaleCtx are converted to
                     * 24-bit RGB, which is interleaved/non-planar.
                     */
                    const auto pict_data = std::array{static_cast<uint8_t*>(pixels)};
                    const auto pict_linesize = std::array{pitch};

                    sws_scale(mSwscaleCtx.get(), std::data(frame->data),
                        std::data(frame->linesize), 0, frame->height, pict_data.data(),
                        pict_linesize.data());
                    SDL_UnlockTexture(mImage);
                }
            }
            else
                SDL_UpdateTexture(mImage, nullptr, frame->data[0], frame->linesize[0]);

            redraw = true;
        }
    }

    if(redraw)
    {
        /* Show the picture! */
        display(renderer, frame);
    }

    if(updated)
    {
        auto disp_time = get_avtime();

        std::lock_guard<std::mutex> displock{mDispPtsMutex};
        mDisplayPts = vp->mPts;
        mDisplayPtsTime = disp_time;
    }
    if(mEOS.load(std::memory_order_acquire))
    {
        if((read_idx+1)%mPictQ.size() == mPictQWrite.load(std::memory_order_acquire))
        {
            mFinalUpdate = true;
            std::unique_lock<std::mutex>{mPictQMutex}.unlock();
            mPictQCond.notify_one();
        }
    }
}

int VideoState::handler()
{
    std::for_each(mPictQ.begin(), mPictQ.end(),
        [](Picture &pict) -> void
        { pict.mFrame = AVFramePtr{av_frame_alloc()}; });

    /* Prefill the codec buffer. */
    auto sender [[maybe_unused]] = std::async(std::launch::async, [this]()
    {
        while(true)
        {
            const int ret{mQueue.sendPacket(mCodecCtx.get())};
            if(ret == AVErrorEOF) break;
        }
    });

    {
        std::lock_guard<std::mutex> displock{mDispPtsMutex};
        mDisplayPtsTime = get_avtime();
    }

    auto current_pts = nanoseconds::zero();
    while(true)
    {
        size_t write_idx{mPictQWrite.load(std::memory_order_relaxed)};
        Picture *vp{&mPictQ[write_idx]};

        /* Retrieve video frame. */
        auto get_frame = [this](AVFrame *frame) -> AVFrame*
        {
            while(int ret{mQueue.receiveFrame(mCodecCtx.get(), frame)})
            {
                if(ret == AVErrorEOF) return nullptr;
                fmt::println(stderr, "Failed to receive frame: {}", ret);
            }
            return frame;
        };
        auto *decoded_frame = get_frame(vp->mFrame.get());
        if(!decoded_frame) break;

        /* Get the PTS for this frame. */
        if(decoded_frame->best_effort_timestamp != AVNoPtsValue)
            current_pts = duration_cast<nanoseconds>(seconds_d64{av_q2d(mStream->time_base) *
                static_cast<double>(decoded_frame->best_effort_timestamp)});
        vp->mPts = current_pts;

        /* Update the video clock to the next expected PTS. */
        auto frame_delay = av_q2d(mCodecCtx->time_base);
        frame_delay += decoded_frame->repeat_pict * (frame_delay * 0.5);
        current_pts += duration_cast<nanoseconds>(seconds_d64{frame_delay});

        /* Put the frame in the queue to be loaded into a texture and displayed
         * by the rendering thread.
         */
        write_idx = (write_idx+1)%mPictQ.size();
        mPictQWrite.store(write_idx, std::memory_order_release);

        if(write_idx == mPictQRead.load(std::memory_order_acquire))
        {
            /* Wait until we have space for a new pic */
            auto lock = std::unique_lock{mPictQMutex};
            mPictQCond.wait(lock, [write_idx,this]() noexcept
                { return write_idx != mPictQRead.load(std::memory_order_acquire); });
        }
    }

    mEOS = true;

    auto lock = std::unique_lock{mPictQMutex};
    mPictQCond.wait(lock, [this]() noexcept { return mFinalUpdate.load(); });

    return 0;
}


int MovieState::decode_interrupt_cb(void *ctx)
{
    return static_cast<MovieState*>(ctx)->mQuit.load(std::memory_order_relaxed);
}

bool MovieState::prepare()
{
    auto intcb = AVIOInterruptCB{decode_interrupt_cb, this};
    if(avio_open2(al::out_ptr(mIOContext), mFilename.c_str(), AVIO_FLAG_READ, &intcb, nullptr) < 0)
    {
        fmt::println(stderr, "Failed to open {}", mFilename);
        return false;
    }

    /* Open movie file. If avformat_open_input fails it will automatically free
     * this context.
     */
    mFormatCtx.reset(avformat_alloc_context());
    mFormatCtx->pb = mIOContext.get();
    mFormatCtx->interrupt_callback = intcb;
    if(avformat_open_input(al::inout_ptr(mFormatCtx), mFilename.c_str(), nullptr, nullptr) < 0)
    {
        fmt::println(stderr, "Failed to open {}", mFilename);
        return false;
    }

    /* Retrieve stream information */
    if(avformat_find_stream_info(mFormatCtx.get(), nullptr) < 0)
    {
        fmt::println(stderr, "{}: failed to find stream info", mFilename);
        return false;
    }

    /* Dump information about file onto standard error */
    av_dump_format(mFormatCtx.get(), 0, mFilename.c_str(), 0);

    mParseThread = std::thread{&MovieState::parse_handler, this};

    std::unique_lock<std::mutex> slock{mStartupMutex};
    while(!mStartupDone) mStartupCond.wait(slock);
    return true;
}

void MovieState::setTitle(SDL_Window *window) const
{
    /* rfind returns npos if the char isn't found, and npos+1==0, which will
     * give the desired result for finding the filename portion.
     */
    const auto fpos = std::max(mFilename.rfind('/')+1, mFilename.rfind('\\')+1);
    const auto title = fmt::format("{} - {}", std::string_view{mFilename}.substr(fpos), AppName);
    SDL_SetWindowTitle(window, title.c_str());
}

nanoseconds MovieState::getClock() const
{
    if(mClockBase == microseconds::min())
        return nanoseconds::zero();
    return get_avtime() - mClockBase;
}

nanoseconds MovieState::getMasterClock()
{
    if(mAVSyncType == SyncMaster::Video && mVideo.mStream)
        return mVideo.getClock();
    if(mAVSyncType == SyncMaster::Audio && mAudio.mStream)
        return mAudio.getClock();
    return getClock();
}

nanoseconds MovieState::getDuration() const
{ return std::chrono::duration<int64_t,std::ratio<1,AV_TIME_BASE>>(mFormatCtx->duration); }

bool MovieState::streamComponentOpen(AVStream *stream)
{
    /* Get a pointer to the codec context for the stream, and open the
     * associated codec.
     */
    AVCodecCtxPtr avctx{avcodec_alloc_context3(nullptr)};
    if(!avctx) return false;

    if(avcodec_parameters_to_context(avctx.get(), stream->codecpar))
        return false;

    const AVCodec *codec{avcodec_find_decoder(avctx->codec_id)};
    if(!codec || avcodec_open2(avctx.get(), codec, nullptr) < 0)
    {
        fmt::println(stderr, "Unsupported codec: {} ({:#x})", avcodec_get_name(avctx->codec_id),
            al::to_underlying(avctx->codec_id));
        return false;
    }

    /* Initialize and start the media type handler */
    switch(avctx->codec_type)
    {
        case AVMEDIA_TYPE_AUDIO:
            mAudio.mStream = stream;
            mAudio.mCodecCtx = std::move(avctx);
            return true;

        case AVMEDIA_TYPE_VIDEO:
            mVideo.mStream = stream;
            mVideo.mCodecCtx = std::move(avctx);
            return true;

        default:
            break;
    }

    return false;
}

int MovieState::parse_handler()
{
    auto &audio_queue = mAudio.mQueue;
    auto &video_queue = mVideo.mQueue;

    int video_index{-1};
    int audio_index{-1};

    /* Find the first video and audio streams */
    const auto ctxstreams = al::span{mFormatCtx->streams, mFormatCtx->nb_streams};
    for(size_t i{0};i < ctxstreams.size();++i)
    {
        auto codecpar = ctxstreams[i]->codecpar;
        if(codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !DisableVideo && video_index < 0
            && streamComponentOpen(ctxstreams[i]))
                video_index = static_cast<int>(i);
        else if(codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_index < 0
            && streamComponentOpen(ctxstreams[i]))
            audio_index = static_cast<int>(i);
    }

    {
        std::unique_lock<std::mutex> slock{mStartupMutex};
        mStartupDone = true;
    }
    mStartupCond.notify_all();

    if(video_index < 0 && audio_index < 0)
    {
        fmt::println(stderr, "{}: could not open codecs", mFilename);
        mQuit = true;
    }

    /* Set the base time 750ms ahead of the current av time. */
    mClockBase = get_avtime() + milliseconds{750};

    if(audio_index >= 0)
        mAudioThread = std::thread{&AudioState::handler, &mAudio};
    if(video_index >= 0)
        mVideoThread = std::thread{&VideoState::handler, &mVideo};

    /* Main packet reading/dispatching loop */
    AVPacketPtr packet{av_packet_alloc()};
    while(!mQuit.load(std::memory_order_relaxed))
    {
        if(av_read_frame(mFormatCtx.get(), packet.get()) < 0)
            break;

        /* Copy the packet into the queue it's meant for. */
        if(packet->stream_index == video_index)
        {
            while(!mQuit.load(std::memory_order_acquire) && !video_queue.put(packet.get()))
                std::this_thread::sleep_for(milliseconds{100});
        }
        else if(packet->stream_index == audio_index)
        {
            while(!mQuit.load(std::memory_order_acquire) && !audio_queue.put(packet.get()))
                std::this_thread::sleep_for(milliseconds{100});
        }

        av_packet_unref(packet.get());
    }
    /* Finish the queues so the receivers know nothing more is coming. */
    video_queue.setFinished();
    audio_queue.setFinished();

    /* all done - wait for it */
    if(mVideoThread.joinable())
        mVideoThread.join();
    if(mAudioThread.joinable())
        mAudioThread.join();

    mVideo.mEOS = true;
    std::unique_lock<std::mutex> lock{mVideo.mPictQMutex};
    while(!mVideo.mFinalUpdate)
        mVideo.mPictQCond.wait(lock);
    lock.unlock();

    SDL_Event evt{};
    evt.user.type = FF_MOVIE_DONE_EVENT;
    SDL_PushEvent(&evt);

    return 0;
}

void MovieState::stop()
{
    mQuit = true;
    mAudio.mQueue.flush();
    mVideo.mQueue.flush();
}


// Helper method to print the time with human-readable formatting.
auto PrettyTime(seconds t) -> std::string
{
    using minutes = std::chrono::minutes;
    using hours = std::chrono::hours;

    if(t.count() < 0)
        return "0s";

    // Only handle up to hour formatting
    if(t >= hours{1})
        return fmt::format("{}h{:02}m{:02}s", duration_cast<hours>(t).count(),
            duration_cast<minutes>(t).count()%60, t.count()%60);
    return fmt::format("{}m{:02}s", duration_cast<minutes>(t).count(), t.count()%60);
}

int main(al::span<std::string_view> args)
{
    SDL_SetMainReady();

    if(args.size() < 2)
    {
        fmt::println(stderr, "Usage: {} [-device <device name>] [-direct] <files...>", args[0]);
        return 1;
    }

    /* Initialize networking protocols */
    avformat_network_init();

    if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        fmt::println(stderr, "Could not initialize SDL - {}", SDL_GetError());
        return 1;
    }

    /* Make a window to put our video */
    auto *screen = SDL_CreateWindow(AppName.c_str(), 640, 480, SDL_WINDOW_RESIZABLE);
    if(!screen)
    {
        fmt::println(stderr, "SDL: could not set video mode - exiting");
        return 1;
    }
    SDL_SetWindowSurfaceVSync(screen, 1);

    /* Make a renderer to handle the texture image surface and rendering. */
    auto *renderer = SDL_CreateRenderer(screen, nullptr);
    if(!renderer)
    {
        fmt::println(stderr, "SDL: could not create renderer - exiting");
        return 1;
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, nullptr);
    SDL_RenderPresent(renderer);

    /* Open an audio device */
    args = args.subspan(1);
    if(InitAL(args) != 0)
        return 1;

    {
        ALCdevice *device{alcGetContextsDevice(alcGetCurrentContext())};
        if(alcIsExtensionPresent(device,"ALC_SOFT_device_clock"))
        {
            fmt::println("Found ALC_SOFT_device_clock");
            alcGetInteger64vSOFT = reinterpret_cast<LPALCGETINTEGER64VSOFT>(
                alcGetProcAddress(device, "alcGetInteger64vSOFT"));
        }
    }

    if(alIsExtensionPresent("AL_SOFT_source_latency"))
    {
        fmt::println("Found AL_SOFT_source_latency");
        alGetSourcei64vSOFT = reinterpret_cast<LPALGETSOURCEI64VSOFT>(
            alGetProcAddress("alGetSourcei64vSOFT"));
    }
    if(alIsExtensionPresent("AL_SOFT_events"))
    {
        fmt::println("Found AL_SOFT_events");
        alEventControlSOFT = reinterpret_cast<LPALEVENTCONTROLSOFT>(
            alGetProcAddress("alEventControlSOFT"));
        alEventCallbackSOFT = reinterpret_cast<LPALEVENTCALLBACKSOFT>(
            alGetProcAddress("alEventCallbackSOFT"));
    }
    if(alIsExtensionPresent("AL_SOFT_callback_buffer"))
    {
        fmt::println("Found AL_SOFT_callback_buffer");
        alBufferCallbackSOFT = reinterpret_cast<LPALBUFFERCALLBACKSOFT>(
            alGetProcAddress("alBufferCallbackSOFT"));
    }

    size_t fileidx{0};
    for(;fileidx < args.size();++fileidx)
    {
        if(args[fileidx] == "-direct")
        {
            if(alIsExtensionPresent("AL_SOFT_direct_channels_remix"))
            {
                fmt::println("Found AL_SOFT_direct_channels_remix");
                DirectOutMode = AL_REMIX_UNMATCHED_SOFT;
            }
            else if(alIsExtensionPresent("AL_SOFT_direct_channels"))
            {
                fmt::println("Found AL_SOFT_direct_channels");
                DirectOutMode = AL_DROP_UNMATCHED_SOFT;
            }
            else
                fmt::println(stderr, "AL_SOFT_direct_channels not supported for direct output");
        }
        else if(args[fileidx] == "-wide")
        {
            if(!alIsExtensionPresent("AL_EXT_STEREO_ANGLES"))
                fmt::println(stderr, "AL_EXT_STEREO_ANGLES not supported for wide stereo");
            else
            {
                fmt::println("Found AL_EXT_STEREO_ANGLES");
                EnableWideStereo = true;
            }
        }
        else if(args[fileidx] == "-uhj")
        {
            if(!alIsExtensionPresent("AL_SOFT_UHJ"))
                fmt::println(stderr, "AL_SOFT_UHJ not supported for UHJ decoding");
            else
            {
                fmt::println("Found AL_SOFT_UHJ");
                EnableUhj = true;
            }
        }
        else if(args[fileidx] == "-superstereo")
        {
            if(!alIsExtensionPresent("AL_SOFT_UHJ"))
                fmt::println(stderr, "AL_SOFT_UHJ not supported for Super Stereo decoding");
            else
            {
                fmt::println("Found AL_SOFT_UHJ (Super Stereo)");
                EnableSuperStereo = true;
            }
        }
        else if(args[fileidx] == "-novideo")
            DisableVideo = true;
        else
            break;
    }

    auto movState = std::unique_ptr<MovieState>{};
    while(fileidx < args.size() && !movState)
    {
        movState = std::make_unique<MovieState>(args[fileidx++]);
        if(!movState->prepare()) movState = nullptr;
    }
    if(!movState)
    {
        fmt::println(stderr, "Could not start a video");
        return 1;
    }
    movState->setTitle(screen);

    /* Default to going to the next movie at the end of one. */
    enum class EomAction {
        Next, Quit
    } eom_action{EomAction::Next};
    seconds last_time{seconds::min()};
    while(true)
    {
        auto event = SDL_Event{};
        auto have_event = SDL_WaitEventTimeout(&event, 10);

        const auto cur_time = duration_cast<seconds>(movState->getMasterClock());
        if(cur_time != last_time)
        {
            const auto end_time = duration_cast<seconds>(movState->getDuration());
            fmt::print("    \r {} / {}", PrettyTime(cur_time), PrettyTime(end_time));
            fflush(stdout);
            last_time = cur_time;
        }

        auto force_redraw = false;
        while(have_event)
        {
            switch(event.type)
            {
            case SDL_EVENT_KEY_DOWN:
                switch(event.key.key)
                {
                case SDLK_ESCAPE:
                    movState->stop();
                    eom_action = EomAction::Quit;
                    break;

                case SDLK_N:
                    movState->stop();
                    eom_action = EomAction::Next;
                    break;

                default:
                    break;
                }
                break;

            case SDL_EVENT_WINDOW_SHOWN:
            case SDL_EVENT_WINDOW_EXPOSED:
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
            case SDL_EVENT_RENDER_TARGETS_RESET:
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderFillRect(renderer, nullptr);
                force_redraw = true;
                break;

            case SDL_EVENT_QUIT:
                movState->stop();
                eom_action = EomAction::Quit;
                break;

            case FF_MOVIE_DONE_EVENT:
                fmt::println("");
                last_time = seconds::min();
                if(eom_action != EomAction::Quit)
                {
                    movState = nullptr;
                    while(fileidx < args.size() && !movState)
                    {
                        movState = std::make_unique<MovieState>(args[fileidx++]);
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

                CloseAL();

                SDL_DestroyRenderer(renderer);
                renderer = nullptr;
                SDL_DestroyWindow(screen);
                screen = nullptr;

                SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
                exit(0);

            default:
                break;
            }
            have_event = SDL_PollEvent(&event);
        }

        movState->mVideo.updateVideo(screen, renderer, force_redraw);
    }

    fmt::println(stderr, "SDL_WaitEvent error - {}", SDL_GetError());
    return 1;
}

} // namespace

int main(int argc, char *argv[])
{
    assert(argc >= 0);
    auto args = std::vector<std::string_view>(static_cast<unsigned int>(argc));
    std::copy_n(argv, args.size(), args.begin());
    return main(al::span{args});
}
