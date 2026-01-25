/*
 * An example showing how to play a stream sync'd to video, using ffmpeg.
 */

#include "config.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <numbers>
#include <ranges>
#include <ratio>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "almalloc.h"
#include "alnumeric.h"
#include "alstring.h"
#include "common/alhelpers.hpp"
#include "fmt/base.h"
#include "fmt/ostream.h"
#include "opthelpers.h"
#include "pragmadefs.h"

DIAGNOSTIC_PUSH
std_pragma("GCC diagnostic ignored \"-Wconversion\"")
std_pragma("GCC diagnostic ignored \"-Wold-style-cast\"")
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
} /* extern "C" */

#define SDL_MAIN_HANDLED
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_main.h"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_video.h"

#if HAVE_CXXMODULES
import gsl;
import openal;

/* AL_APIENTRY is needed, but not exported from the module. */
#ifdef _WIN32
 #define AL_APIENTRY __cdecl
#else
 #define AL_APIENTRY
#endif

#else

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "gsl/gsl"
#endif


namespace {

[[nodiscard]]
constexpr auto DefineSDLColorspace(SDL_ColorType type, SDL_ColorRange range,
    SDL_ColorPrimaries primaries, SDL_TransferCharacteristics transfer,
    SDL_MatrixCoefficients matrix, SDL_ChromaLocation chromaloc) noexcept
{
    return SDL_DEFINE_COLORSPACE(type, range, primaries, transfer, matrix, chromaloc);
}

constexpr auto AVNoPtsValue = AV_NOPTS_VALUE;
constexpr auto AVErrorEOF = AVERROR_EOF;

} /* namespace */
DIAGNOSTIC_POP

namespace {

using voidp = void*;
using fixed32 = std::chrono::duration<int64_t, std::ratio<1, (int64_t{1}<<32)>>;
using nanoseconds = std::chrono::nanoseconds;
using microseconds = std::chrono::microseconds;
using milliseconds = std::chrono::milliseconds;
using seconds = std::chrono::seconds;
using seconds_d64 = std::chrono::duration<double>;
using std::chrono::duration_cast;


constexpr auto AppName = std::to_array("alffplay");

auto PlaybackGain = 1.0f;
auto DirectOutMode = ALenum{AL_FALSE};
auto EnableWideStereo = false;
auto EnableUhj = false;
auto EnableSuperStereo = false;
auto DisableVideo = false;
auto alGetSourcei64vSOFT = LPALGETSOURCEI64VSOFT{};
auto alEventControlSOFT = LPALEVENTCONTROLSOFT{};
auto alEventCallbackSOFT = LPALEVENTCALLBACKSOFT{};

auto alBufferCallbackSOFT = LPALBUFFERCALLBACKSOFT{};

constexpr auto AVNoSyncThreshold = seconds{10};

constexpr auto VideoPictureQueueSize = 24;

constexpr auto AudioSyncThreshold = seconds_d64{0.03};
constexpr auto AudioSampleCorrectionMax = milliseconds{50};
/* Averaging filter coefficient for audio sync. */
constexpr auto AudioDiffAvgNB = 20.0;
const auto AudioAvgFilterCoeff = std::pow(0.01, 1.0/AudioDiffAvgNB); /* NOLINT(cert-err58-cpp) */

/* Per-buffer size, in time */
constexpr auto AudioBufferTime = milliseconds{20};
/* Buffer total size, in time (should be divisible by the buffer time) */
constexpr auto AudioBufferTotalTime = milliseconds{800};
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


inline auto get_avtime() -> microseconds
{ return microseconds{av_gettime()}; }

/* Define unique_ptrs to auto-cleanup associated ffmpeg objects. */
using AVIOContextPtr = std::unique_ptr<AVIOContext, decltype([](AVIOContext *ptr)
    { avio_closep(&ptr); })>;

using AVFormatCtxPtr = std::unique_ptr<AVFormatContext, decltype([](AVFormatContext *ptr)
    { avformat_close_input(&ptr); })>;

using AVCodecCtxPtr = std::unique_ptr<AVCodecContext, decltype([](AVCodecContext *ptr)
    { avcodec_free_context(&ptr); })>;

using AVPacketPtr = std::unique_ptr<AVPacket,decltype([](AVPacket *pkt){ av_packet_free(&pkt); })>;

using AVFramePtr = std::unique_ptr<AVFrame, decltype([](AVFrame *ptr) { av_frame_free(&ptr); })>;

using SwrContextPtr = std::unique_ptr<SwrContext,decltype([](SwrContext *ptr){ swr_free(&ptr); })>;

using SwsContextPtr = std::unique_ptr<SwsContext, decltype([](SwsContext *ptr)
    { sws_freeContext(ptr); })>;


struct SDLProps {
    SDL_PropertiesID mProperties{};

    SDLProps() : mProperties{SDL_CreateProperties()} { }
    ~SDLProps() { SDL_DestroyProperties(mProperties); }

    SDLProps(const SDLProps&) = delete;
    auto operator=(const SDLProps&) -> SDLProps& = delete;

    [[nodiscard]]
    auto getid() const noexcept -> SDL_PropertiesID { return mProperties; }

    [[nodiscard]]
    auto setPointer(gsl::czstring const name, void *value) const
    { return SDL_SetPointerProperty(mProperties, name, value); }

    [[nodiscard]]
    auto setString(gsl::czstring const name, gsl::czstring const value) const
    { return SDL_SetStringProperty(mProperties, name, value); }

    [[nodiscard]]
    auto setInt(gsl::czstring const name, Sint64 const value) const
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


using ChannelData = std::variant<uint64_t, std::span<AVChannelCustom>>;

struct ChannelLayout : public AVChannelLayout {
    ChannelLayout() noexcept : AVChannelLayout{} { }
    ChannelLayout(const ChannelLayout &rhs) : AVChannelLayout{}
    { av_channel_layout_copy(this, &rhs); }
    explicit ChannelLayout(const AVChannelLayout &rhs) : AVChannelLayout{}
    { av_channel_layout_copy(this, &rhs); }
    ~ChannelLayout() { av_channel_layout_uninit(this); }

    auto operator=(const ChannelLayout &rhs) & -> ChannelLayout&
    { av_channel_layout_copy(this, &rhs); return *this; }

    [[nodiscard]]
    auto getChannels() const noexcept LIFETIMEBOUND -> ChannelData
    {
        /* NOLINTBEGIN(*-union-access) */
        if(this->order == AV_CHANNEL_ORDER_CUSTOM)
        {
            if(this->u.map && this->nb_channels > 0)
                return std::span{this->u.map, gsl::narrow_cast<size_t>(this->nb_channels)};
            return std::span<AVChannelCustom>{};
        }
        return this->u.mask;
        /* NOLINTEND(*-union-access) */
    }
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
        mTotalSize -= gsl::narrow_cast<unsigned int>(ret->size);
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
                mOutFrameCond.notify_all();
                return false;
            });
        }
        mOutFrameCond.notify_all();

        if(!packet)
        {
            if(!ret) return AVErrorEOF;
            fmt::println(std::cerr, "Failed to send flush packet: {}", ret);
            return ret;
        }
        if(ret < 0)
            fmt::println(std::cerr, "Failed to send packet: {}", ret);
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
                mInFrameCond.notify_all();
                return false;
            });
        }
        mInFrameCond.notify_all();
        return ret;
    }

    void setFinished()
    {
        {
            auto plock = std::lock_guard{mPacketMutex};
            mFinished = true;
        }
        mPacketCond.notify_all();
    }

    void flush()
    {
        {
            auto plock = std::lock_guard{mPacketMutex};
            mFinished = true;

            mPackets.clear();
            mTotalSize = 0;
        }
        mPacketCond.notify_all();
    }

    auto put(const AVPacket *pkt) -> bool
    {
        {
            auto plock = std::lock_guard{mPacketMutex};
            if(mTotalSize >= mSizeLimit || mFinished)
                return false;

            auto *newpkt = mPackets.emplace_back(AVPacketPtr{av_packet_alloc()}).get();
            if(av_packet_ref(newpkt, pkt) == 0)
                mTotalSize += gsl::narrow_cast<unsigned int>(newpkt->size);
            else
                mPackets.pop_back();
        }
        mPacketCond.notify_all();
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

    /* The PTS of the start of the source playback. */
    nanoseconds mStartPts{nanoseconds::min()};

    /* The steady_clock time point the audio stream stopped at. */
    nanoseconds mEndTime{nanoseconds::min()};

    /* Decompressed sample frame, and swresample context for conversion */
    AVFramePtr    mDecodedFrame;
    SwrContextPtr mSwresCtx;

    /* Conversion format, for what gets fed to OpenAL */
    uint64_t       mDstChanLayout{0};
    AVSampleFormat mDstSampleFmt{AV_SAMPLE_FMT_NONE};

    /* Storage of converted samples */
    std::array<uint8_t*,1> mSamples{};
    std::span<uint8_t> mSamplesSpan;
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
    std::atomic_flag mConnected;
    ALuint mSource{0};
    std::array<ALuint,AudioBufferCount> mBuffers{};
    ALuint mBufferIdx{0};

    explicit AudioState(MovieState &movie LIFETIMEBOUND) : mMovie(movie)
    { mConnected.test_and_set(std::memory_order_relaxed); }
    ~AudioState()
    {
        if(mSource)
            alDeleteSources(1, &mSource);
        if(mBuffers[0])
            alDeleteBuffers(gsl::narrow_cast<ALsizei>(mBuffers.size()), mBuffers.data());

        av_freep(static_cast<void*>(mSamples.data()));
    }

    auto eventCallback(ALenum eventType, ALuint object, ALuint param, std::string_view message)
        noexcept -> void;

    auto bufferCallback(const std::span<ALubyte> data) noexcept -> ALsizei;

    [[nodiscard]] auto getClockNoLock() const -> nanoseconds;
    [[nodiscard]] auto getClock() -> nanoseconds
    {
        const auto lock = std::lock_guard{mSrcMutex};
        return getClockNoLock();
    }

    [[nodiscard]] auto startPlayback() -> bool;

    [[nodiscard]] auto getSync() -> int;
    [[nodiscard]] auto decodeFrame() -> int;
    [[nodiscard]] auto readAudio(std::span<uint8_t> samples, int &sample_skip) -> bool;
    auto readAudio(int sample_skip) -> void;

    void handler();
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
    std::array<Picture,VideoPictureQueueSize> mPictQ;
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

    explicit VideoState(MovieState &movie LIFETIMEBOUND) : mMovie(movie) { }
    ~VideoState()
    {
        if(mImage)
            SDL_DestroyTexture(mImage);
        mImage = nullptr;
    }

    auto getClock() -> nanoseconds;

    void display(SDL_Renderer *renderer, AVFrame *frame) const;
    void updateVideo(SDL_Window *screen, SDL_Renderer *renderer, bool redraw);
    void handler();
};

struct MovieState {
    AVIOContextPtr mIOContext;
    AVFormatCtxPtr mFormatCtx;

    SyncMaster mAVSyncType{SyncMaster::Default};

    microseconds mClockBase{microseconds::min()};

    std::atomic<bool> mQuit{false};

    AudioState mAudio;
    VideoState mVideo;

    std::atomic<bool> mStartupDone{false};

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

    static auto decode_interrupt_cb(void *ctx) -> int;
    auto prepare() -> bool;
    void setTitle(SDL_Window *window) const;
    void stop();

    [[nodiscard]] auto getClock() const -> nanoseconds;
    [[nodiscard]] auto getMasterClock() -> nanoseconds;
    [[nodiscard]] auto getDuration() const -> nanoseconds;

    auto streamComponentOpen(AVStream *stream) -> bool;
    void parse_handler();
};


auto AudioState::getClockNoLock() const -> nanoseconds
{
    /* The audio clock is the timestamp of the sample currently being heard. */
    if(mStartPts == nanoseconds::min())
        return nanoseconds::zero();

    /* If the stream ended, count from the ending time to ensure any video can
     * keep going.
     */
    if(mEndTime > nanoseconds::min())
        return std::chrono::steady_clock::now().time_since_epoch() - mEndTime + mCurrentPts;

    /* This more safely converts fixed32 to nanoseconds, avoiding overflow
     * unlike a normal duration_cast call.
     */
    static constexpr auto sec32_to_nanoseconds = [](const fixed32 s) -> nanoseconds
    {
        static constexpr auto one32 = fixed32{seconds{1}};
        return seconds{s/one32} + duration_cast<nanoseconds>(s%one32);
    };

    if(!mBufferData.empty())
    {
        /* With a callback buffer, mStartPts is the timestamp of the first
         * sample frame played. The audio clock, then, is that plus the current
         * source offset.
         */
        auto offset = std::array<ALint64SOFT,2>{};
        if(alGetSourcei64vSOFT)
            alGetSourcei64vSOFT(mSource, AL_SAMPLE_OFFSET_LATENCY_SOFT, offset.data());
        else
        {
            auto ioffset = ALint{};
            alGetSourcei(mSource, AL_SAMPLE_OFFSET, &ioffset);
            offset[0] = ALint64SOFT{ioffset} << 32;
        }

        /* NOTE: The source state must be checked last, in case an underrun
         * occurs and the source stops between getting the state and retrieving
         * the offset+latency.
         */
        auto status = ALint{};
        alGetSourcei(mSource, AL_SOURCE_STATE, &status);

        auto pts = nanoseconds{};
        if(status == AL_PLAYING || status == AL_PAUSED)
        {
            const auto sec_fixed32 = fixed32{offset[0] / mCodecCtx->sample_rate};
            pts = mStartPts + sec32_to_nanoseconds(sec_fixed32) - nanoseconds{offset[1]};
        }
        else
        {
            /* If the source is stopped, the pts of the next sample to be heard
             * is the pts of the next sample to be buffered, minus the amount
             * already in the buffer ready to play.
             */
            const auto woffset = mWritePos.load(std::memory_order_acquire);
            const auto roffset = mReadPos.load(std::memory_order_relaxed);
            /* Account for the write offset wrapping behind the read offset. */
            const auto readable = (woffset < roffset)*mBufferData.size() + woffset - roffset;

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
    auto pts = mCurrentPts;
    if(mSource)
    {
        auto offset = std::array<ALint64SOFT,2>{};
        if(alGetSourcei64vSOFT)
            alGetSourcei64vSOFT(mSource, AL_SAMPLE_OFFSET_LATENCY_SOFT, offset.data());
        else
        {
            auto ioffset = ALint{};
            alGetSourcei(mSource, AL_SAMPLE_OFFSET, &ioffset);
            offset[0] = ALint64SOFT{ioffset} << 32;
        }
        auto queued = ALint{};
        auto status = ALint{};
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
            pts += sec32_to_nanoseconds(fixed32{offset[0] / mCodecCtx->sample_rate});
        }
        /* Don't offset by the latency if the source isn't playing. */
        if(status == AL_PLAYING)
            pts -= nanoseconds{offset[1]};
    }

    return pts;
}

auto AudioState::startPlayback() -> bool
{
    if(!mBufferData.empty())
    {
        const auto woffset = mWritePos.load(std::memory_order_acquire);
        const auto roffset = mReadPos.load(std::memory_order_relaxed);
        /* Account for the write offset wrapping behind the read offset. */
        const auto readable = (woffset < roffset)*mBufferData.size() + woffset - roffset;
        if(readable == 0) return false;

        const auto nanosamples = nanoseconds{seconds{readable / mFrameSize}};
        mStartPts = mCurrentPts - nanosamples/mCodecCtx->sample_rate;
    }
    else
    {
        auto queued = ALint{};
        alGetSourcei(mSource, AL_BUFFERS_QUEUED, &queued);
        if(queued == 0) return false;

        /* Subtract the total buffer queue time from the current pts to get the
         * pts of the start of the queue.
         */
        mStartPts = mCurrentPts - AudioBufferTime*queued;
    }

    alSourcePlay(mSource);
    return true;
}

auto AudioState::getSync() -> int
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
    return gsl::narrow_cast<int>(duration_cast<seconds>(diff*mCodecCtx->sample_rate).count());
}

auto AudioState::decodeFrame() -> int
{
    do {
        while(const auto ret = mQueue.receiveFrame(mCodecCtx.get(), mDecodedFrame.get()))
        {
            if(ret == AVErrorEOF) return 0;
            fmt::println(std::cerr, "Failed to receive frame: {}", ret);
        }
    } while(mDecodedFrame->nb_samples <= 0);

    /* If provided, update w/ pts */
    if(mDecodedFrame->best_effort_timestamp != AVNoPtsValue)
        mCurrentPts = duration_cast<nanoseconds>(seconds_d64{av_q2d(mStream->time_base) *
            gsl::narrow_cast<double>(mDecodedFrame->best_effort_timestamp)});

    if(mDecodedFrame->nb_samples > mSamplesMax)
    {
        av_freep(static_cast<void*>(mSamples.data()));
        if(av_samples_alloc(mSamples.data(), nullptr, mCodecCtx->ch_layout.nb_channels,
            mDecodedFrame->nb_samples, mDstSampleFmt, 0) < 0)
        {
            mSamplesMax = 0;
            mSamplesSpan = {};
            return 0;
        }
        mSamplesMax = mDecodedFrame->nb_samples;
        mSamplesSpan = {mSamples[0], gsl::narrow_cast<size_t>(mSamplesMax)*mFrameSize};
    }
    /* Return the amount of sample frames converted */
    const auto data_size = swr_convert(mSwresCtx.get(), mSamples.data(), mDecodedFrame->nb_samples,
        mDecodedFrame->extended_data, mDecodedFrame->nb_samples);

    av_frame_unref(mDecodedFrame.get());
    return data_size;
}

/* Duplicates the sample at in to out, count times. */
void sample_dup(std::span<uint8_t> out, std::span<const uint8_t> in, size_t count)
{
    auto dstiter = out.begin();
    std::ranges::for_each(std::views::iota(0_uz, count), [in,&dstiter](size_t)
    {
        dstiter = std::ranges::copy(in, dstiter).out;
    });
}

auto AudioState::readAudio(std::span<uint8_t> samples, int &sample_skip) -> bool
{
    auto audio_size = 0u;

    /* Read the next chunk of data, refill the buffer, and queue it
     * on the source.
     */
    const auto length = samples.size() / mFrameSize;
    while(mSamplesLen > 0 && audio_size < length)
    {
        auto rem = length - audio_size;
        if(mSamplesPos >= 0)
        {
            rem = std::min(rem, gsl::narrow_cast<size_t>(mSamplesLen - mSamplesPos));

            const auto boffset = gsl::narrow_cast<ALuint>(mSamplesPos) * size_t{mFrameSize};
            std::ranges::copy(mSamplesSpan | std::views::drop(boffset)
                | std::views::take(rem*mFrameSize), samples.begin());
        }
        else
        {
            rem = std::min(rem, gsl::narrow_cast<size_t>(-mSamplesPos));

            /* Add samples by copying the first sample */
            sample_dup(samples, mSamplesSpan.first(mFrameSize), rem);
        }

        mSamplesPos += gsl::narrow_cast<int>(rem);
        mCurrentPts += nanoseconds{seconds{rem}} / mCodecCtx->sample_rate;
        samples = samples.subspan(rem * mFrameSize);
        audio_size += rem;

        while(mSamplesPos >= mSamplesLen)
        {
            mSamplesLen = decodeFrame();
            mSamplesPos = std::min(mSamplesLen, sample_skip);
            if(mSamplesLen <= 0) break;

            sample_skip -= mSamplesPos;

            /* Adjust the start time and current pts by the amount we're
             * skipping/duplicating, so that the clock remains correct for the
             * current stream position.
             */
            const auto skip = nanoseconds{seconds{mSamplesPos}} / mCodecCtx->sample_rate;
            mStartPts -= skip;
            mCurrentPts += skip;
        }
    }
    if(audio_size <= 0)
        return false;

    if(audio_size < length)
    {
        const auto rem = length - audio_size;
        const auto audio_data = std::array{samples.data()};
        av_samples_set_silence(audio_data.data(), gsl::narrow_cast<int>(audio_size),
            gsl::narrow_cast<int>(rem), mCodecCtx->ch_layout.nb_channels, mDstSampleFmt);
        mCurrentPts += nanoseconds{seconds{rem}} / mCodecCtx->sample_rate;
    }
    return true;
}

auto AudioState::readAudio(int sample_skip) -> void
{
    auto woffset = mWritePos.load(std::memory_order_acquire);
    const auto roffset = mReadPos.load(std::memory_order_relaxed);
    while(mSamplesLen > 0)
    {
        const auto nsamples = ((roffset > woffset) ? roffset-woffset-1
            : (roffset == 0) ? (mBufferData.size()-woffset-1)
            : (mBufferData.size()-woffset)) / mFrameSize;
        if(!nsamples) break;

        if(mSamplesPos < 0)
        {
            const auto rem = std::min<size_t>(nsamples, gsl::narrow_cast<ALuint>(-mSamplesPos));

            sample_dup(mBufferData|std::views::drop(woffset), mSamplesSpan.first(mFrameSize), rem);
            woffset += rem * mFrameSize;
            if(woffset == mBufferData.size()) woffset = 0;
            mWritePos.store(woffset, std::memory_order_release);

            mCurrentPts += nanoseconds{seconds{rem}} / mCodecCtx->sample_rate;
            mSamplesPos += gsl::narrow_cast<int>(rem);
            continue;
        }

        if(const auto rem = std::min(nsamples, gsl::narrow_cast<size_t>(mSamplesLen-mSamplesPos)))
        {
            const auto boffset = gsl::narrow_cast<ALuint>(mSamplesPos) * size_t{mFrameSize};
            const auto nbytes = rem * mFrameSize;

            std::ranges::copy(mSamplesSpan | std::views::drop(boffset) | std::views::take(nbytes),
                (mBufferData | std::views::drop(woffset)).begin());
            woffset += nbytes;
            if(woffset == mBufferData.size()) woffset = 0;
            mWritePos.store(woffset, std::memory_order_release);

            mCurrentPts += nanoseconds{seconds{rem}} / mCodecCtx->sample_rate;
            mSamplesPos += gsl::narrow_cast<int>(rem);
        }

        while(mSamplesPos >= mSamplesLen)
        {
            mSamplesLen = decodeFrame();
            mSamplesPos = std::min(mSamplesLen, sample_skip);
            if(mSamplesLen <= 0) return;

            sample_skip -= mSamplesPos;

            const auto skip = nanoseconds{seconds{mSamplesPos}} / mCodecCtx->sample_rate;
            mStartPts -= skip;
            mCurrentPts += skip;
        }
    }
}


auto AL_APIENTRY AudioState::eventCallback(ALenum eventType, ALuint object, ALuint param,
    std::string_view message) noexcept -> void
{
    if(eventType == AL_EVENT_TYPE_BUFFER_COMPLETED_SOFT)
    {
        /* Temporarily lock the source mutex to ensure it's not between
         * checking the processed count and going to sleep.
         */
        std::unique_lock{mSrcMutex}.unlock();
        mSrcCond.notify_all();
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
        object, param, message);

    if(eventType == AL_EVENT_TYPE_DISCONNECTED_SOFT)
    {
        {
            auto lock = std::lock_guard{mSrcMutex};
            mConnected.clear(std::memory_order_release);
        }
        mSrcCond.notify_all();
    }
}

auto AudioState::bufferCallback(const std::span<ALubyte> data) noexcept -> ALsizei
{
    auto output = data.begin();

    auto roffset = mReadPos.load(std::memory_order_acquire);
    while(const auto rem = gsl::narrow_cast<size_t>(std::distance(output, data.end())))
    {
        const auto woffset = mWritePos.load(std::memory_order_relaxed);
        if(woffset == roffset) break;

        auto todo = ((woffset < roffset) ? mBufferData.size() : woffset) - roffset;
        todo = std::min(todo, rem);

        output = std::ranges::copy(mBufferData | std::views::drop(roffset)
            | std::views::take(todo), output).out;

        roffset += todo;
        if(roffset == mBufferData.size())
            roffset = 0;
    }
    mReadPos.store(roffset, std::memory_order_release);

    return gsl::narrow_cast<ALsizei>(std::distance(data.begin(), output));
}

void AudioState::handler()
{
    static constexpr auto evt_types = std::array<ALenum,3>{{
        AL_EVENT_TYPE_BUFFER_COMPLETED_SOFT, AL_EVENT_TYPE_SOURCE_STATE_CHANGED_SOFT,
        AL_EVENT_TYPE_DISCONNECTED_SOFT}};
    auto srclock = std::unique_lock{mSrcMutex, std::defer_lock};
    auto sleep_time = milliseconds{AudioBufferTime / 2};

    if(alEventControlSOFT)
    {
        static constexpr auto callback = [](ALenum eventType, ALuint object, ALuint param,
            ALsizei length, const ALchar *message, void *userParam) noexcept -> void
        {
            static_cast<AudioState*>(userParam)->eventCallback(eventType, object, param,
                std::string_view{message, gsl::narrow_cast<ALuint>(length)});
        };

        alEventControlSOFT(evt_types.size(), evt_types.data(), AL_TRUE);
        alEventCallbackSOFT(callback, this);
        sleep_time = AudioBufferTotalTime;
    }
    const auto _ = gsl::finally([]
    {
        if(alEventControlSOFT)
        {
            alEventControlSOFT(evt_types.size(), evt_types.data(), AL_FALSE);
            alEventCallbackSOFT(nullptr, nullptr);
        }
    });

    /* Note that ffmpeg assumes AmbiX (ACN layout, SN3D normalization). Only
     * support HOA when OpenAL can take AmbiX natively (if AmbiX -> FuMa
     * conversion is needed, we don't bother with higher order channels).
     */
    const auto has_bfmt = bool{alIsExtensionPresent("AL_EXT_BFORMAT") != AL_FALSE};
    const auto has_bfmt_ex = bool{alIsExtensionPresent("AL_SOFT_bformat_ex") != AL_FALSE};
    const auto has_bfmt_hoa = bool{has_bfmt_ex
        && alIsExtensionPresent("AL_SOFT_bformat_hoa") != AL_FALSE};
    /* AL_SOFT_bformat_hoa supports up to 14th order (225 channels), otherwise
     * only 1st order is supported with AL_EXT_BFORMAT.
     */
    const auto max_ambi_order = has_bfmt_hoa ? 14u : 1u;
    auto ambi_order = 0u;

    /* Find a suitable format for OpenAL. */
    const auto layoutmask = std::invoke([layout=ChannelLayout{mCodecCtx->ch_layout}]
    {
        auto chansvar = layout.getChannels();
        if(auto *mask = std::get_if<uint64_t>(&chansvar))
            return *mask;
        return uint64_t{0};
    });
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
            if(layoutmask == AV_CH_LAYOUT_SURROUND /* a.k.a. 3.0 */ && EnableUhj)
            {
                mDstChanLayout = layoutmask;
                mFrameSize *= 3;
                mFormat = AL_FORMAT_UHJ3CHN_FLOAT32_SOFT;
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
            const auto order = gsl::narrow_cast<unsigned>(
                std::sqrt(mCodecCtx->ch_layout.nb_channels)) - 1u;
            if(const auto channels = (order+1u) * (order+1u);
                std::cmp_equal(channels, mCodecCtx->ch_layout.nb_channels)
                || std::cmp_equal(channels+2u, mCodecCtx->ch_layout.nb_channels))
            {
                ambi_order = std::min(order, max_ambi_order);
                mFrameSize *= (ambi_order+1u) * (ambi_order+1u);
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
            if(layoutmask == AV_CH_LAYOUT_SURROUND && EnableUhj)
            {
                mDstChanLayout = layoutmask;
                mFrameSize *= 3;
                mFormat = AL_FORMAT_UHJ3CHN8_SOFT;
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
            const auto order = gsl::narrow_cast<unsigned>(
                std::sqrt(mCodecCtx->ch_layout.nb_channels)) - 1u;
            if(const auto channels = (order+1u) * (order+1u);
                std::cmp_equal(channels, mCodecCtx->ch_layout.nb_channels)
                || std::cmp_equal(channels+2u, mCodecCtx->ch_layout.nb_channels))
            {
                ambi_order = std::min(order, max_ambi_order);
                mFrameSize *= (ambi_order+1u) * (ambi_order+1u);
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
            if(layoutmask == AV_CH_LAYOUT_SURROUND && EnableUhj)
            {
                mDstChanLayout = layoutmask;
                mFrameSize *= 3;
                mFormat = AL_FORMAT_UHJ3CHN16_SOFT;
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
            const auto order = gsl::narrow_cast<unsigned>(
                std::sqrt(mCodecCtx->ch_layout.nb_channels)) - 1u;
            if(const auto channels = (order+1u) * (order+1u);
                std::cmp_equal(channels, mCodecCtx->ch_layout.nb_channels)
                || std::cmp_equal(channels+2u, mCodecCtx->ch_layout.nb_channels))
            {
                ambi_order = std::min(order, max_ambi_order);
                mFrameSize *= (ambi_order+1u) * (ambi_order+1u);
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
        fmt::println(std::cerr, "Failed to allocate audio frame");
        return;
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
            auto errstr = std::array<char,AV_ERROR_MAX_STRING_SIZE>{};
            fmt::println(std::cerr, "Failed to allocate SwrContext: {}",
                av_make_error_string(errstr.data(), errstr.size(), err));
            return;
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
            auto mtx = std::vector(64_uz*64_uz, 0.0);
            mtx[0 + 0*64] = std::sqrt(0.5);
            mtx[3 + 1*64] = 1.0;
            mtx[1 + 2*64] = 1.0;
            mtx[2 + 3*64] = 1.0;
            swr_set_matrix(mSwresCtx.get(), mtx.data(), 64);
        }
    }
    else
    {
        auto layout = ChannelLayout{};
        av_channel_layout_from_mask(&layout, mDstChanLayout);

        const auto err = swr_alloc_set_opts2(al::out_ptr(mSwresCtx), &layout, mDstSampleFmt,
            mCodecCtx->sample_rate, &mCodecCtx->ch_layout, mCodecCtx->sample_fmt,
            mCodecCtx->sample_rate, 0, nullptr);
        if(err != 0)
        {
            auto errstr = std::array<char,AV_ERROR_MAX_STRING_SIZE>{};
            fmt::println(std::cerr, "Failed to allocate SwrContext: {}",
                av_make_error_string(errstr.data(), errstr.size(), err));
            return;
        }
    }
    if(const auto err = swr_init(mSwresCtx.get()))
    {
        auto errstr = std::array<char,AV_ERROR_MAX_STRING_SIZE>{};
        fmt::println(std::cerr, "Failed to initialize audio converter: {}",
            av_make_error_string(errstr.data(), errstr.size(), err));
        return;
    }

    alGenBuffers(gsl::narrow_cast<ALsizei>(mBuffers.size()), mBuffers.data());
    alGenSources(1, &mSource);

    /* The gain limit is the internal max that the calculated source gain is
     * clamped to after cone and distance attenuation, the filter gain, and
     * listener gain are applied. Since none of those apply here, there's no
     * need to raise the source's max gain beyond that limit.
     */
    const auto maxgain = alIsExtensionPresent("AL_SOFT_gain_clamp_ex")
        ? alGetFloat(AL_GAIN_LIMIT_SOFT) : 1.0f;
    alSourcef(mSource, AL_MAX_GAIN, maxgain);

    /* The source's AL_GAIN can really be set to any non-negative finite value,
     * but without cone and distance attenuation, there's no real point to
     * setting it greater than the max gain.
     */
    auto gain = PlaybackGain;
    if(gain > maxgain)
    {
        fmt::println(std::cerr, "Limiting requested gain {:+}dB ({}) to max {:+}dB ({})",
            std::round(std::log10(gain)*2000.0f) / 100.0f, gain,
            std::round(std::log10(maxgain)*2000.0f) / 100.0f, maxgain);
        gain = maxgain;
    }
    else
        fmt::println("Setting gain {:+}dB ({})", std::round(std::log10(gain)*2000.0f) / 100.0f,
            gain);
    alSourcef(mSource, AL_GAIN, gain);

    if(DirectOutMode)
        alSourcei(mSource, AL_DIRECT_CHANNELS_SOFT, DirectOutMode);
    if(EnableWideStereo)
    {
        static constexpr auto angles = std::array{gsl::narrow_cast<float>(std::numbers::pi / 3.0),
            gsl::narrow_cast<float>(-std::numbers::pi / 3.0)};
        alSourcefv(mSource, AL_STEREO_ANGLES, angles.data());
    }
    if(has_bfmt_ex)
    {
        std::ranges::for_each(mBuffers, [](const ALuint bufid)
        {
            alBufferi(bufid, AL_AMBISONIC_LAYOUT_SOFT, AL_ACN_SOFT);
            alBufferi(bufid, AL_AMBISONIC_SCALING_SOFT, AL_SN3D_SOFT);
        });
    }
    if(ambi_order > 1)
    {
        std::ranges::for_each(mBuffers, [ambi_order](const ALuint bufid)
        { alBufferi(bufid, AL_UNPACK_AMBISONIC_ORDER_SOFT, gsl::narrow_cast<int>(ambi_order)); });
    }
    if(EnableSuperStereo)
        alSourcei(mSource, AL_STEREO_MODE_SOFT, AL_SUPER_STEREO_SOFT);

    if(alGetError() != AL_NO_ERROR)
        return;

    auto samples = std::vector<uint8_t>{};
    auto callback_ok = false;
    if(alBufferCallbackSOFT)
    {
        static constexpr auto callback = [](void *userptr, void *data, ALsizei size) noexcept
            -> ALsizei
        {
            return static_cast<AudioState*>(userptr)->bufferCallback(
                std::views::counted(static_cast<ALubyte*>(data), size));
        };
        alBufferCallbackSOFT(mBuffers[0], mFormat, mCodecCtx->sample_rate, callback, this);

        alSourcei(mSource, AL_BUFFER, as_signed(mBuffers[0]));
        if(alGetError() != AL_NO_ERROR)
        {
            fmt::println(std::cerr, "Failed to set buffer callback");
            alSourcei(mSource, AL_BUFFER, 0);
        }
        else
        {
            const auto numsamples = duration_cast<seconds>(mCodecCtx->sample_rate
                * AudioBufferTotalTime).count();
            mBufferData.resize(gsl::narrow_cast<size_t>(numsamples) * mFrameSize);
            std::ranges::fill(mBufferData, uint8_t{});

            mReadPos.store(0, std::memory_order_relaxed);
            mWritePos.store(0, std::memory_order_relaxed);

            auto refresh = ALCint{};
            alcGetIntegerv(alcGetContextsDevice(alcGetCurrentContext()), ALC_REFRESH, 1, &refresh);
            sleep_time = milliseconds{seconds{1}} / refresh;
            callback_ok = true;
        }
    }
    if(!callback_ok)
    {
        auto buffer_len = duration_cast<seconds>(mCodecCtx->sample_rate * AudioBufferTime).count();
        if(buffer_len > 0)
            samples.resize(gsl::narrow_cast<size_t>(buffer_len) * mFrameSize);
    }

    /* Prefill the codec buffer. */
    auto sender [[maybe_unused]] = std::async(std::launch::async, [this]
    {
        while(true)
        {
            const auto ret = mQueue.sendPacket(mCodecCtx.get());
            if(ret == AVErrorEOF)
                break;
        }
    });

    if(alIsExtensionPresent("AL_SOFT_source_start_delay"))
    {
        /* Start after a short delay, to give other threads a chance to get
         * buffered. Prerolling would be better here, but short of that, this
         * will do.
         */
        const auto start_delay = round<seconds>(AudioBufferTotalTime/2
            * mCodecCtx->sample_rate).count();
        alSourcei(mSource, AL_SAMPLE_OFFSET, -gsl::narrow_cast<int>(start_delay));
    }

    srclock.lock();
    mSamplesLen = decodeFrame();
    mSamplesPos = 0;
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

        auto state = ALenum{};
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
            auto processed = ALint{};
            auto queued = ALint{};

            /* First remove any processed buffers. */
            alGetSourcei(mSource, AL_BUFFERS_PROCESSED, &processed);
            while(processed > 0)
            {
                auto bid = ALuint{};
                alSourceUnqueueBuffers(mSource, 1, &bid);
                --processed;
            }

            /* Refill the buffer queue. */
            auto sync_skip = getSync();
            alGetSourcei(mSource, AL_BUFFERS_QUEUED, &queued);
            while(gsl::narrow_cast<ALuint>(queued) < mBuffers.size())
            {
                /* Read the next chunk of data, filling the buffer, and queue
                 * it on the source.
                 */
                if(!readAudio(samples, sync_skip))
                    break;

                const auto bufid = mBuffers[mBufferIdx];
                mBufferIdx = gsl::narrow_cast<ALuint>((mBufferIdx+1_uz) % mBuffers.size());

                alBufferData(bufid, mFormat, samples.data(),
                    gsl::narrow_cast<ALsizei>(samples.size()), mCodecCtx->sample_rate);
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
                continue;
            }
        }

        /* (re)start the source if needed, and wait for a buffer to finish */
        if(state != AL_PLAYING && state != AL_PAUSED)
        {
            if(!startPlayback())
                break;
        }
        if(const auto err = alGetError())
            fmt::println(std::cerr, "Got AL error: {:#x} ({})", as_unsigned(err),
                alGetString(err));

        mSrcCond.wait_for(srclock, sleep_time);
    }

    mEndTime = std::chrono::steady_clock::now().time_since_epoch();

    alSourceRewind(mSource);
    alSourcei(mSource, AL_BUFFER, 0);
}


auto VideoState::getClock() -> nanoseconds
{
    /* NOTE: This returns incorrect times while not playing. */
    auto displock = std::lock_guard{mDispPtsMutex};
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

    auto frame_width = frame->width - gsl::narrow_cast<int>(frame->crop_left+frame->crop_right);
    auto frame_height = frame->height - gsl::narrow_cast<int>(frame->crop_top+frame->crop_bottom);

    const auto src_rect = SDL_FRect{gsl::narrow_cast<float>(frame->crop_left),
        gsl::narrow_cast<float>(frame->crop_top), gsl::narrow_cast<float>(frame_width),
        gsl::narrow_cast<float>(frame_height)};

    SDL_RenderTexture(renderer, mImage, &src_rect, nullptr);
    SDL_RenderPresent(renderer);
}

/* Called regularly on the main thread where the SDL_Renderer was created. It
 * handles updating the textures of decoded frames and displaying the latest
 * frame.
 */
void VideoState::updateVideo(SDL_Window *screen, SDL_Renderer *renderer, bool redraw)
{
    auto read_idx = mPictQRead.load(std::memory_order_relaxed);
    auto *vp = &mPictQ[read_idx];

    auto clocktime = mMovie.getMasterClock();
    auto updated = false;
    while(true)
    {
        auto next_idx = (read_idx+1) % mPictQ.size();
        if(next_idx == mPictQWrite.load(std::memory_order_acquire))
            break;
        auto *nextvp = &mPictQ[next_idx];
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
        std::unique_lock{mPictQMutex}.unlock();
        mPictQCond.notify_all();
        return;
    }

    auto *frame = vp->mFrame.get();
    if(updated)
    {
        mPictQRead.store(read_idx, std::memory_order_release);
        std::unique_lock{mPictQMutex}.unlock();
        mPictQCond.notify_all();

        /* allocate or resize the buffer! */
        if(!mImage || mWidth != frame->width || mHeight != frame->height
            || frame->format != mAVFormat)
        {
            if(mImage)
                SDL_DestroyTexture(mImage);
            mImage = nullptr;
            mSwscaleCtx = nullptr;

            const auto fmtiter = std::ranges::find(TextureFormatMap, frame->format,
                &TextureFormatEntry::avformat);
            if(fmtiter != TextureFormatMap.end())
            {
                auto const props = SDLProps{};
                std::ignore = props.setInt(SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,
                    fmtiter->sdlformat);
                std::ignore = props.setInt(SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER,
                    SDL_TEXTUREACCESS_STREAMING);
                std::ignore = props.setInt(SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, frame->width);
                std::ignore = props.setInt(SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, frame->height);

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
                std::ignore = props.setInt(SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, colorspace);

                mImage = SDL_CreateTextureWithProperties(renderer, props.getid());
                if(!mImage)
                    fmt::println(std::cerr, "Failed to create texture!");
                mWidth = frame->width;
                mHeight = frame->height;
                mSDLFormat = fmtiter->sdlformat;
                mAVFormat = fmtiter->avformat;
            }
            else
            {
                /* If there's no matching format, convert to RGB24. */
                fmt::println(std::cerr, "Could not find SDL format for pix_fmt {0:#x} ({0})",
                    as_unsigned(frame->format));

                auto const props = SDLProps{};
                std::ignore = props.setInt(SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,
                    SDL_PIXELFORMAT_RGB24);
                std::ignore = props.setInt(SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER,
                    SDL_TEXTUREACCESS_STREAMING);
                std::ignore = props.setInt(SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, frame->width);
                std::ignore = props.setInt(SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, frame->height);

                mImage = SDL_CreateTextureWithProperties(renderer, props.getid());
                if(!mImage)
                    fmt::println(std::cerr, "Failed to create texture!");
                mWidth = frame->width;
                mHeight = frame->height;
                mSDLFormat = SDL_PIXELFORMAT_RGB24;
                mAVFormat = frame->format;

                mSwscaleCtx = SwsContextPtr{sws_getContext(
                    frame->width, frame->height, gsl::narrow_cast<AVPixelFormat>(frame->format),
                    frame->width, frame->height, AV_PIX_FMT_RGB24, 0,
                    nullptr, nullptr, nullptr)};

                sws_setColorspaceDetails(mSwscaleCtx.get(), sws_getCoefficients(frame->colorspace),
                    (frame->color_range==AVCOL_RANGE_JPEG), sws_getCoefficients(SWS_CS_DEFAULT), 1,
                    0<<16, 1<<16, 1<<16);
            }
        }

        auto frame_width = frame->width - gsl::narrow_cast<int>(frame->crop_left
            + frame->crop_right);
        auto frame_height = frame->height - gsl::narrow_cast<int>(frame->crop_top
            + frame->crop_bottom);
        if(mFirstUpdate && frame_width > 0 && frame_height > 0)
        {
            /* For the first update, set the window size to the video size. */
            mFirstUpdate = false;

            if(frame->sample_aspect_ratio.den != 0)
            {
                const auto aspect_ratio = av_q2d(frame->sample_aspect_ratio);
                if(aspect_ratio >= 1.0)
                    frame_width = gsl::narrow_cast<int>(std::lround(frame_width * aspect_ratio));
                else if(aspect_ratio > 0.0)
                    frame_height = gsl::narrow_cast<int>(std::lround(frame_height / aspect_ratio));
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
                    fmt::println(std::cerr, "Failed to lock texture: {}", SDL_GetError());
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

        auto displock = std::lock_guard{mDispPtsMutex};
        mDisplayPts = vp->mPts;
        mDisplayPtsTime = disp_time;
    }
    if(mEOS.load(std::memory_order_acquire))
    {
        if((read_idx+1)%mPictQ.size() == mPictQWrite.load(std::memory_order_acquire))
        {
            mFinalUpdate = true;
            std::unique_lock{mPictQMutex}.unlock();
            mPictQCond.notify_all();
        }
    }
}

void VideoState::handler()
{
    std::ranges::for_each(mPictQ, [](Picture &pict) -> void
    { pict.mFrame = AVFramePtr{av_frame_alloc()}; });

    /* Prefill the codec buffer. */
    auto sender [[maybe_unused]] = std::async(std::launch::async, [this]
    {
        while(true)
        {
            const auto ret = mQueue.sendPacket(mCodecCtx.get());
            if(ret == AVErrorEOF)
                break;
        }
    });

    {
        auto displock = std::lock_guard{mDispPtsMutex};
        mDisplayPtsTime = get_avtime();
    }

    auto current_pts = nanoseconds::zero();
    while(true)
    {
        auto write_idx = mPictQWrite.load(std::memory_order_relaxed);
        auto *vp = &mPictQ[write_idx];

        /* Retrieve video frame. */
        auto *decoded_frame = std::invoke([this](AVFrame *frame) -> AVFrame*
        {
            while(const auto ret = mQueue.receiveFrame(mCodecCtx.get(), frame))
            {
                if(ret == AVErrorEOF) return nullptr;
                fmt::println(std::cerr, "Failed to receive frame: {}", ret);
            }
            return frame;
        }, vp->mFrame.get());
        if(!decoded_frame) break;

        /* Get the PTS for this frame. */
        if(decoded_frame->best_effort_timestamp != AVNoPtsValue)
            current_pts = duration_cast<nanoseconds>(seconds_d64{av_q2d(mStream->time_base) *
                gsl::narrow_cast<double>(decoded_frame->best_effort_timestamp)});
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
            mPictQCond.wait(lock, [write_idx,this]
            {
                return write_idx != mPictQRead.load(std::memory_order_acquire);
            });
        }
    }

    mEOS = true;

    auto lock = std::unique_lock{mPictQMutex};
    mPictQCond.wait(lock, [this]() noexcept { return mFinalUpdate.load(); });
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
        fmt::println(std::cerr, "Failed to open {}", mFilename);
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
        fmt::println(std::cerr, "Failed to open {}", mFilename);
        return false;
    }

    /* Retrieve stream information */
    if(avformat_find_stream_info(mFormatCtx.get(), nullptr) < 0)
    {
        fmt::println(std::cerr, "{}: failed to find stream info", mFilename);
        return false;
    }

    /* Dump information about file onto standard error */
    av_dump_format(mFormatCtx.get(), 0, mFilename.c_str(), 0);

    mParseThread = std::thread{&MovieState::parse_handler, this};

    mStartupDone.wait(false, std::memory_order_acquire);
    return true;
}

void MovieState::setTitle(SDL_Window *window) const
{
    /* rfind returns npos if the char isn't found, and npos+1==0, which will
     * give the desired result for finding the filename portion.
     */
    const auto fpos = std::max(mFilename.rfind('/')+1, mFilename.rfind('\\')+1);
    const auto title = fmt::format("{} - {}", std::string_view{mFilename}.substr(fpos),
        AppName.data());
    SDL_SetWindowTitle(window, title.c_str());
}

auto MovieState::getClock() const -> nanoseconds
{
    if(mClockBase == microseconds::min())
        return nanoseconds::zero();
    return get_avtime() - mClockBase;
}

auto MovieState::getMasterClock() -> nanoseconds
{
    if(mAVSyncType == SyncMaster::Video && mVideo.mStream)
        return mVideo.getClock();
    if(mAVSyncType == SyncMaster::Audio && mAudio.mStream)
        return mAudio.getClock();
    return getClock();
}

auto MovieState::getDuration() const -> nanoseconds
{ return std::chrono::duration<int64_t,std::ratio<1,AV_TIME_BASE>>(mFormatCtx->duration); }

auto MovieState::streamComponentOpen(AVStream *stream) -> bool
{
    /* Get a pointer to the codec context for the stream, and open the
     * associated codec.
     */
    auto avctx = AVCodecCtxPtr{avcodec_alloc_context3(nullptr)};
    if(!avctx) return false;

    if(avcodec_parameters_to_context(avctx.get(), stream->codecpar))
        return false;

    const auto *codec = avcodec_find_decoder(avctx->codec_id);
    if(!codec || avcodec_open2(avctx.get(), codec, nullptr) < 0)
    {
        fmt::println(std::cerr, "Unsupported codec: {} ({:#x})", avcodec_get_name(avctx->codec_id),
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

void MovieState::parse_handler()
{
    auto &audio_queue = mAudio.mQueue;
    auto &video_queue = mVideo.mQueue;

    auto video_index = -1;
    auto audio_index = -1;

    /* Find the first video and audio streams */
    const auto ctxstreams = std::span{mFormatCtx->streams, mFormatCtx->nb_streams};
    for(const auto i : std::views::iota(0_uz, ctxstreams.size()))
    {
        auto codecpar = ctxstreams[i]->codecpar;
        if(codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !DisableVideo && video_index < 0
            && streamComponentOpen(ctxstreams[i]))
                video_index = gsl::narrow_cast<int>(i);
        else if(codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_index < 0
            && streamComponentOpen(ctxstreams[i]))
            audio_index = gsl::narrow_cast<int>(i);
    }

    mStartupDone.store(true, std::memory_order_release);
    mStartupDone.notify_all();

    if(video_index < 0 && audio_index < 0)
    {
        fmt::println(std::cerr, "{}: could not open codecs", mFilename);
        mQuit = true;
    }

    /* Set the base time 750ms ahead of the current av time. */
    mClockBase = get_avtime() + milliseconds{750};

    if(audio_index >= 0)
        mAudioThread = std::thread{&AudioState::handler, &mAudio};
    if(video_index >= 0)
        mVideoThread = std::thread{&VideoState::handler, &mVideo};

    /* Main packet reading/dispatching loop */
    auto packet = AVPacketPtr{av_packet_alloc()};
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
    {
        auto lock = std::unique_lock{mVideo.mPictQMutex};
        while(!mVideo.mFinalUpdate)
            mVideo.mPictQCond.wait(lock);
    }

    auto evt = SDL_Event{};
    evt.user.type = FF_MOVIE_DONE_EVENT;
    SDL_PushEvent(&evt);
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

auto main(std::span<std::string_view> args) -> int
{
    SDL_SetMainReady();

    if(args.size() < 2)
    {
        fmt::println(std::cerr, "Usage: {} [-device <device name>] [options] <files...>", args[0]);
        fmt::println(std::cerr, "\n  Options:\n"
            "    -gain <g>     Set audio playback gain (prepend +/- or append \"dB\" to \n"
            "                  indicate decibels, otherwise it's linear amplitude)\n"
            "    -novideo      Disable video playback\n"
            "    -direct       Play audio directly on the output, bypassing virtualization\n"
            "    -superstereo  Apply Super Stereo processing to stereo tracks\n"
            "    -uhj          Decode as UHJ (stereo = UHJ2, 3.0 = UHJ3, quad = UHJ4)");
        return 1;
    }
    args = args.subspan(1);

    /* Initialize networking protocols */
    avformat_network_init();

    if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        fmt::println(std::cerr, "Could not initialize SDL - {}", SDL_GetError());
        return 1;
    }

    /* Make a window to put our video */
    auto *screen = SDL_CreateWindow(AppName.data(), 640, 480, SDL_WINDOW_RESIZABLE);
    if(!screen)
    {
        fmt::println(std::cerr, "SDL: could not set video mode - exiting");
        return 1;
    }
    SDL_SetWindowSurfaceVSync(screen, 1);

    /* Make a renderer to handle the texture image surface and rendering. */
    auto *renderer = SDL_CreateRenderer(screen, nullptr);
    if(!renderer)
    {
        fmt::println(std::cerr, "SDL: could not create renderer - exiting");
        return 1;
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, nullptr);
    SDL_RenderPresent(renderer);

    /* Open an audio device */
    auto almgr = InitAL(args);
    almgr.printName();

    /* NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast) */
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
    /* NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast) */

    auto curarg = args.begin();
    for(auto args_end=args.end();curarg != args_end;++curarg)
    {
        const auto argval = *curarg;
        if(argval == "-direct")
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
                fmt::println(std::cerr, "AL_SOFT_direct_channels not supported for direct output");
            continue;
        }
        if(argval == "-wide")
        {
            if(!alIsExtensionPresent("AL_EXT_STEREO_ANGLES"))
                fmt::println(std::cerr, "AL_EXT_STEREO_ANGLES not supported for wide stereo");
            else
            {
                fmt::println("Found AL_EXT_STEREO_ANGLES");
                EnableWideStereo = true;
            }
            continue;
        }
        if(argval == "-uhj")
        {
            if(!alIsExtensionPresent("AL_SOFT_UHJ"))
                fmt::println(std::cerr, "AL_SOFT_UHJ not supported for UHJ decoding");
            else
            {
                fmt::println("Found AL_SOFT_UHJ");
                EnableUhj = true;
            }
            continue;
        }
        if(argval == "-superstereo")
        {
            if(!alIsExtensionPresent("AL_SOFT_UHJ"))
                fmt::println(std::cerr, "AL_SOFT_UHJ not supported for Super Stereo decoding");
            else
            {
                fmt::println("Found AL_SOFT_UHJ (Super Stereo)");
                EnableSuperStereo = true;
            }
            continue;
        }
        if(argval == "-novideo")
        {
            DisableVideo = true;
            continue;
        }
        if(argval == "-gain")
        {
            if(curarg+1 == args_end)
                fmt::println(std::cerr, "Missing argument for -gain");
            else
            {
                const auto optarg = *++curarg;

                auto endpos = size_t{};
                const auto gainval = std::invoke([optarg,&endpos]
                {
                    try { return std::stof(std::string{optarg}, &endpos); }
                    catch(std::exception &e) {
                        fmt::println(std::cerr, "Exception reading gain value: {}", e.what());
                    }
                    return std::numeric_limits<float>::quiet_NaN();
                });
                if(optarg.starts_with("+") || optarg.starts_with("-")
                    || al::case_compare(optarg.substr(endpos), "db") == 0)
                {
                    if(!std::isfinite(gainval) || (endpos != optarg.size()
                            && al::case_compare(optarg.substr(endpos), "db") != 0))
                        fmt::println(std::cerr, "Invalid dB gain value: {}", optarg);
                    else
                        PlaybackGain = std::pow(10.0f, gainval/20.0f);
                }
                else
                {
                    if(endpos != optarg.size() || !(gainval >= 0.0f) || !std::isfinite(gainval))
                        fmt::println(std::cerr, "Invalid linear gain value: {}", optarg);
                    else
                        PlaybackGain = gainval;
                }
            }
            continue;
        }
        break;
    }

    auto movState = std::unique_ptr<MovieState>{};
    curarg = std::ranges::find_if(curarg, args.end(), [&movState](const std::string_view argval)
    {
        auto movie = std::make_unique<MovieState>(argval);
        if(!movie->prepare())
            return false;
        movState = std::move(movie);
        return true;
    });
    if(curarg == args.end())
    {
        fmt::println(std::cerr, "Could not start a video");
        return 1;
    }
    ++curarg;
    movState->setTitle(screen);

    /* Default to going to the next movie at the end of one. */
    enum class EomAction {
        Next, Quit
    } eom_action{EomAction::Next};
    auto last_time = seconds::min();
    while(true)
    {
        auto event = SDL_Event{};
        auto have_event = SDL_WaitEventTimeout(&event, 10);

        const auto cur_time = duration_cast<seconds>(movState->getMasterClock());
        if(cur_time != last_time)
        {
            const auto end_time = duration_cast<seconds>(movState->getDuration());
            fmt::print("    \r {} / {}", PrettyTime(cur_time), PrettyTime(end_time));
            std::cout.flush();
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
                    curarg = std::ranges::find_if(curarg, args.end(),
                        [&movState](const std::string_view argval)
                    {
                        auto movie = std::make_unique<MovieState>(argval);
                        if(!movie->prepare())
                            return false;
                        movState = std::move(movie);
                        return true;
                    });
                    if(curarg != args.end())
                    {
                        ++curarg;
                        movState->setTitle(screen);
                        break;
                    }
                }

                /* Nothing more to play. Shut everything down and quit. */
                movState = nullptr;

                almgr.close();

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

    fmt::println(std::cerr, "SDL_WaitEvent error - {}", SDL_GetError());
    return 1;
}

} // namespace

auto main(int argc, char *argv[]) -> int
{
    auto args = std::vector<std::string_view>(gsl::narrow<unsigned int>(argc));
    std::ranges::copy(std::views::counted(argv, argc), args.begin());
    return main(std::span{args});
}
