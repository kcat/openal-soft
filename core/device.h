#ifndef CORE_DEVICE_H
#define CORE_DEVICE_H

#include <array>
#include <atomic>
#include <bitset>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <variant>

#include "almalloc.h"
#include "alnumeric.h"
#include "ambidefs.h"
#include "atomic.h"
#include "bufferline.h"
#include "devformat.h"
#include "filters/nfc.h"
#include "flexarray.h"
#include "fmt/core.h"
#include "intrusive_ptr.h"
#include "mixer/hrtfdefs.h"
#include "opthelpers.h"
#include "resampler_limits.h"
#include "uhjfilter.h"
#include "vector.h"

class BFormatDec;
namespace Bs2b {
struct bs2b;
} // namespace Bs2b
class Compressor;
struct ContextBase;
struct DirectHrtfState;
class FrontStablizer;
struct HrtfStore;

using uint = unsigned int;


inline constexpr auto MinOutputRate = 8000_uz;
inline constexpr auto MaxOutputRate = 192000_uz;
inline constexpr auto DefaultOutputRate = 48000_uz;

inline constexpr auto DefaultUpdateSize = 512_uz; /* ~10.7ms */
inline constexpr auto DefaultNumUpdates = 3_uz;


enum class DeviceType : std::uint8_t {
    Playback,
    Capture,
    Loopback
};


enum class RenderMode : std::uint8_t {
    Normal,
    Pairwise,
    Hrtf
};

enum class StereoEncoding : std::uint8_t {
    Basic,
    Uhj,
    Hrtf,

    Default = Basic
};


struct InputRemixMap {
    struct TargetMix { Channel channel; float mix; };

    Channel channel;
    std::span<const TargetMix> targets;
};


class DistanceComp {
    explicit DistanceComp(std::size_t count) : mSamples{count} { }

public:
    /* Maximum delay in samples for speaker distance compensation. */
    static constexpr uint MaxDelay{1024};

    struct ChanData {
        std::span<float> Buffer; /* Valid size is [0...MaxDelay). */
        float Gain{1.0f};
    };

    std::array<ChanData,MaxOutputChannels> mChannels;
    al::FlexArray<float,16> mSamples;

    static auto Create(std::size_t numsamples) -> std::unique_ptr<DistanceComp>
    { return std::unique_ptr<DistanceComp>{new(FamCount(numsamples)) DistanceComp{numsamples}}; }

    DEF_FAM_NEWDEL(DistanceComp, mSamples)
};


constexpr auto InvalidChannelIndex = static_cast<std::uint8_t>(~0u);

struct BFChannelConfig {
    float Scale;
    uint Index;
};

struct MixParams {
    /* Coefficient channel mapping for mixing to the buffer. */
    std::array<BFChannelConfig,MaxAmbiChannels> AmbiMap{};

    std::span<FloatBufferLine> Buffer;

    /**
     * Helper to set an identity/pass-through panning for ambisonic mixing. The
     * source is expected to be a 3D ACN/N3D ambisonic buffer, and for each
     * channel [0...count), the given functor is called with the source channel
     * index, destination channel index, and the gain for that channel. If the
     * destination channel is InvalidChannelIndex, the given source channel is
     * not used for output.
     */
    template<std::invocable<std::size_t,std::uint8_t,float> F>
    void setAmbiMixParams(const MixParams &inmix, const float gainbase, F func) const
    {
        const auto numIn = inmix.Buffer.size();
        const auto numOut = Buffer.size();
        for(const auto i : std::views::iota(0_uz, numIn))
        {
            auto idx = InvalidChannelIndex;
            auto gain = 0.0f;

            for(const auto j : std::views::iota(0_uz, numOut))
            {
                if(AmbiMap[j].Index == inmix.AmbiMap[i].Index)
                {
                    idx = static_cast<std::uint8_t>(j);
                    gain = AmbiMap[j].Scale * gainbase;
                    break;
                }
            }
            std::invoke(func, i, idx, gain);
        }
    }
};

struct RealMixParams {
    std::span<const InputRemixMap> RemixMap;
    std::array<std::uint8_t,MaxChannels> ChannelIndex{};

    std::span<FloatBufferLine> Buffer;
};

using AmbiRotateMatrix = std::array<std::array<float,MaxAmbiChannels>,MaxAmbiChannels>;


struct AmbiDecPostProcess {
    std::unique_ptr<BFormatDec> mAmbiDecoder;
};

struct HrtfPostProcess {
    std::unique_ptr<DirectHrtfState> mHrtfState;
};

struct UhjPostProcess {
    std::unique_ptr<UhjEncoderBase> mUhjEncoder;
};

struct StablizerPostProcess {
    std::unique_ptr<BFormatDec> mAmbiDecoder;
    std::unique_ptr<FrontStablizer> mStablizer;
};

struct Bs2bPostProcess {
    std::unique_ptr<BFormatDec> mAmbiDecoder;
    std::unique_ptr<Bs2b::bs2b> mBs2b;
};

using PostProcess = std::variant<std::monostate,
    AmbiDecPostProcess,
    HrtfPostProcess,
    UhjPostProcess,
    StablizerPostProcess,
    Bs2bPostProcess>;


enum {
    // Frequency was requested by the app or config file
    FrequencyRequest,
    // Channel configuration was requested by the app or config file
    ChannelsRequest,
    // Sample type was requested by the config file
    SampleTypeRequest,

    // Specifies if the DSP is paused at user request
    DevicePaused,

    // Specifies if the output plays directly on/in ears (headphones, headset,
    // ear buds, etc).
    DirectEar,

    /* Specifies if output is using speaker virtualization (e.g. Windows
     * Spatial Audio).
     */
    Virtualization,

    DeviceFlagsCount
};

enum class DeviceState : std::uint8_t {
    Unprepared,
    Configured,
    Playing
};

/* NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding) */
struct SIMDALIGN DeviceBase {
    std::atomic<bool> Connected{true};
    const DeviceType Type{};

    std::string mDeviceName;

    uint mSampleRate{};
    uint mUpdateSize{};
    uint mBufferSize{};

    DevFmtChannels FmtChans{};
    DevFmtType FmtType{};
    uint mAmbiOrder{0u};
    float mXOverFreq{400.0f};
    /* If the main device mix is horizontal/2D only. */
    bool m2DMixing{false};
    /* For DevFmtAmbi* output only, specifies the channel order and
     * normalization.
     */
    DevAmbiLayout mAmbiLayout{DevAmbiLayout::Default};
    DevAmbiScaling mAmbiScale{DevAmbiScaling::Default};

    // Device flags
    std::bitset<DeviceFlagsCount> Flags;
    DeviceState mDeviceState{DeviceState::Unprepared};

    uint NumAuxSends{};

    /* Rendering mode. */
    RenderMode mRenderMode{RenderMode::Normal};

    /* The average speaker distance as determined by the ambdec configuration,
     * HRTF data set, or the NFC-HOA reference delay. Only used for NFC.
     */
    float AvgSpeakerDist{0.0f};

    /* The default NFC filter. Not used directly, but is pre-initialized with
     * the control distance from AvgSpeakerDist.
     */
    NfcFilter mNFCtrlFilter{};

    using seconds32 = std::chrono::duration<int32_t>;
    using nanoseconds32 = std::chrono::duration<int32_t, std::nano>;

    std::atomic<uint> mSamplesDone{0u};
    /* Split the clock to avoid a 64-bit atomic for certain 32-bit targets. */
    std::atomic<seconds32> mClockBaseSec{seconds32{}};
    std::atomic<nanoseconds32> mClockBaseNSec{nanoseconds32{}};
    std::chrono::nanoseconds FixedLatency{0};

    AmbiRotateMatrix mAmbiRotateMatrix{};
    AmbiRotateMatrix mAmbiRotateMatrix2{};

    /* Temp storage used for mixer processing. */
    static constexpr std::size_t MixerLineSize{BufferLineSize + DecoderBase::sMaxPadding};
    static constexpr std::size_t MixerChannelsMax{16};
    alignas(16) std::array<float,MixerLineSize*MixerChannelsMax> mSampleData{};
    alignas(16) std::array<float,MixerLineSize+MaxResamplerPadding> mResampleData{};

    alignas(16) std::array<float,BufferLineSize> FilteredData{};
    alignas(16) std::array<float,BufferLineSize+HrtfHistoryLength> ExtraSampleData{};

    /* Persistent storage for HRTF mixing. */
    alignas(16) std::array<float2,BufferLineSize+HrirLength> HrtfAccumData{};

    /* Mixing buffer used by the Dry mix and Real output. */
    al::vector<FloatBufferLine, 16> MixBuffer;

    /* The "dry" path corresponds to the main output. */
    MixParams Dry;
    std::array<uint,MaxAmbiOrder+1> NumChannelsPerOrder{};

    /* "Real" output, which will be written to the device buffer. May alias the
     * dry buffer.
     */
    RealMixParams RealOut;

    /* HRTF state and info */
    al::intrusive_ptr<HrtfStore> mHrtf;
    uint mIrSize{0u};

    PostProcess mPostProcess;

    std::unique_ptr<Compressor> Limiter;

    /* Delay buffers used to compensate for speaker distances. */
    std::unique_ptr<DistanceComp> ChannelDelays;

    /* Dithering control. */
    float DitherDepth{0.0f};
    uint DitherSeed{0u};

    /* Running count of the mixer invocations, in 31.1 fixed point. This
     * actually increments *twice* when mixing, first at the start and then at
     * the end, so the bottom bit indicates if the device is currently mixing
     * and the upper bits indicates how many mixes have been done.
     */
    std::atomic<uint> mMixCount{0u};

    // Contexts created on this device
    using ContextArray = al::FlexArray<ContextBase*>;
    al::atomic_unique_ptr<ContextArray> mContexts;

    /** Returns the number of contexts remaining on the device. */
    [[nodiscard]] auto removeContext(ContextBase *context) -> size_t;

    [[nodiscard]] auto bytesFromFmt() const noexcept -> uint { return BytesFromDevFmt(FmtType); }
    [[nodiscard]] auto channelsFromFmt() const noexcept -> uint { return ChannelsFromDevFmt(FmtChans, mAmbiOrder); }
    [[nodiscard]] auto frameSizeFromFmt() const noexcept -> uint { return bytesFromFmt() * channelsFromFmt(); }

    struct MixLock {
        DeviceBase *const self;
        const uint mEndVal;

        MixLock(DeviceBase *device, const uint endval) noexcept : self{device}, mEndVal{endval} { }
        MixLock(const MixLock&) = delete;
        void operator=(const MixLock&) = delete;
        /* Update the mix count when the lock goes out of scope to "release" it
         * (lsb should be 0).
         */
        ~MixLock() { self->mMixCount.store(mEndVal, std::memory_order_release); }
    };
    auto getWriteMixLock() noexcept -> MixLock
    {
        /* Increment the mix count at the start of mixing and writing clock
         * info (lsb should be 1).
         */
        const auto oldCount = mMixCount.fetch_add(1u, std::memory_order_acq_rel);
        return MixLock{this, oldCount+2};
    }

    /** Waits for the mixer to not be mixing or updating the clock. */
    [[nodiscard]] auto waitForMix() const noexcept -> uint
    {
        uint refcount{mMixCount.load(std::memory_order_acquire)};
        while((refcount&1)) refcount = mMixCount.load(std::memory_order_acquire);
        return refcount;
    }

    /**
     * Helper to get the current clock time from the device's ClockBase, and
     * SamplesDone converted from the sample rate. Should only be called while
     * watching the MixCount.
     */
    [[nodiscard]] auto getClockTime() const noexcept -> std::chrono::nanoseconds
    {
        using std::chrono::seconds;
        using std::chrono::nanoseconds;

        auto ns = nanoseconds{seconds{mSamplesDone.load(std::memory_order_relaxed)}} / mSampleRate;
        return nanoseconds{mClockBaseNSec.load(std::memory_order_relaxed)}
            + mClockBaseSec.load(std::memory_order_relaxed) + ns;
    }

    void Process(std::monostate&, const std::size_t) { }
    void Process(AmbiDecPostProcess &proc, const std::size_t SamplesToDo) const;
    void Process(HrtfPostProcess &proc, const std::size_t SamplesToDo);
    void Process(UhjPostProcess &proc, const std::size_t SamplesToDo);
    void Process(StablizerPostProcess &proc, const std::size_t SamplesToDo);
    void Process(Bs2bPostProcess &proc, const std::size_t SamplesToDo);

    void renderSamples(const std::span<void*> outBuffers, const uint numSamples);
    void renderSamples(void *outBuffer, const uint numSamples, const std::size_t frameStep);

    /* Caller must lock the device state, and the mixer must not be running. */
    void doDisconnect(std::string msg);

    template<typename ...Args>
    void handleDisconnect(fmt::format_string<Args...> fmt, Args&& ...args)
    { doDisconnect(fmt::format(std::move(fmt), std::forward<Args>(args)...)); }

    /**
     * Returns the index for the given channel name (e.g. FrontCenter), or
     * InvalidChannelIndex if it doesn't exist.
     */
    [[nodiscard]] auto channelIdxByName(Channel chan) const noexcept -> std::uint8_t
    { return RealOut.ChannelIndex[chan]; }

private:
    uint renderSamples(const uint numSamples);

protected:
    explicit DeviceBase(DeviceType type);
    ~DeviceBase();

public:
    DeviceBase(const DeviceBase&) = delete;
    DeviceBase& operator=(const DeviceBase&) = delete;
};

/* Must be less than 15 characters (16 including terminating null) for
 * compatibility with pthread_setname_np limitations. */
[[nodiscard]] constexpr
auto GetMixerThreadName() noexcept -> const char* { return "alsoft-mixer"; }

[[nodiscard]] constexpr
auto GetRecordThreadName() noexcept -> const char* { return "alsoft-record"; }

#endif /* CORE_DEVICE_H */
