#ifndef CORE_VOICE_H
#define CORE_VOICE_H

#include <array>
#include <atomic>
#include <bitset>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "alnumeric.h"
#include "ambidefs.h"
#include "bufferline.h"
#include "buffer_storage.h"
#include "devformat.h"
#include "filters/biquad.h"
#include "filters/nfc.h"
#include "filters/splitter.h"
#include "mixer/defs.h"
#include "mixer/hrtfdefs.h"
#include "resampler_limits.h"
#include "uhjfilter.h"
#include "vector.h"

struct ContextBase;
struct DeviceBase;
struct EffectSlotBase;
enum class DistanceModel : u8::value_t;

inline constexpr auto MaxSendCount = 6u;

inline constexpr auto MaxPitch = 10u;

inline constinit auto ResamplerDefault = Resampler::Spline;

enum class SpatializeMode : u8::value_t {
    Off,
    On,
    Auto
};

enum class DirectMode : u8::value_t {
    Off,
    DropMismatch,
    RemixMismatch
};


struct DirectParams {
    BiquadInterpFilter LowPass;
    BiquadInterpFilter HighPass;

    NfcFilter NFCtrlFilter;

    struct HrtfParams {
        HrtfFilter Old{};
        HrtfFilter Target{};
        alignas(16) std::array<float, HrtfHistoryLength> History{};
    };
    HrtfParams Hrtf;

    struct GainParams {
        std::array<float, MaxOutputChannels> Current{};
        std::array<float, MaxOutputChannels> Target{};
    };
    GainParams Gains;
};

struct SendParams {
    BiquadInterpFilter LowPass;
    BiquadInterpFilter HighPass;

    struct GainParams {
        std::array<float, MaxAmbiChannels> Current{};
        std::array<float, MaxAmbiChannels> Target{};
    };
    GainParams Gains;
};


struct VoiceBufferItem {
    std::atomic<VoiceBufferItem*> mNext{nullptr};

    CallbackType mCallback{nullptr};
    void *mUserData{nullptr};

    unsigned mBlockAlign{0u};
    unsigned mSampleLen{0u};
    unsigned mLoopStart{0u};
    unsigned mLoopEnd{0u};

    SampleVariant mSamples;

protected:
    ~VoiceBufferItem() = default;
};
using LPVoiceBufferItem = VoiceBufferItem*;


struct VoiceProps {
    float Pitch;
    float Gain;
    float OuterGain;
    float MinGain;
    float MaxGain;
    float InnerAngle;
    float OuterAngle;
    float RefDistance;
    float MaxDistance;
    float RolloffFactor;
    std::array<float, 3> Position;
    std::array<float, 3> Velocity;
    std::array<float, 3> Direction;
    std::array<float, 3> OrientAt;
    std::array<float, 3> OrientUp;
    bool HeadRelative;
    DistanceModel mDistanceModel;
    Resampler mResampler;
    DirectMode DirectChannels;
    SpatializeMode mSpatializeMode;
    bool mPanningEnabled;

    bool DryGainHFAuto;
    bool WetGainAuto;
    bool WetGainHFAuto;
    float OuterGainHF;

    float AirAbsorptionFactor;
    float RoomRolloffFactor;
    float DopplerFactor;

    std::array<float, 2> StereoPan;

    float Radius;
    float EnhWidth;
    float Panning;

    /** Direct filter and auxiliary send info. */
    struct DirectData {
        float Gain;
        float GainHF;
        float HFReference;
        float GainLF;
        float LFReference;
    };
    DirectData Direct;

    struct SendData {
        EffectSlotBase *Slot;
        float Gain;
        float GainHF;
        float HFReference;
        float GainLF;
        float LFReference;
    };
    std::array<SendData, MaxSendCount> Send;
};

struct VoicePropsItem : VoiceProps {
    std::atomic<VoicePropsItem*> next{nullptr};
};

enum : unsigned {
    VoiceIsStatic,
    VoiceIsCallback,
    VoiceIsAmbisonic,
    VoiceCallbackStopped,
    VoiceIsFading,
    VoiceHasHrtf,
    VoiceHasNfc,

    VoiceFlagCount
};

struct Voice {
    enum State {
        Stopped,
        Playing,
        Stopping,
        Pending
    };

    std::atomic<VoicePropsItem*> mUpdate{nullptr};

    VoiceProps mProps{};

    std::atomic<unsigned> mSourceID{0u};
    std::atomic<State> mPlayState{Stopped};
    std::atomic<bool> mPendingChange{false};

    /**
     * Source offset in samples, relative to the currently playing buffer, NOT
     * the whole queue.
     */
    std::atomic<int> mPosition;
    /** Fractional (fixed-point) offset to the next sample. */
    std::atomic<unsigned> mPositionFrac;

    /* Current buffer queue item being played. */
    std::atomic<VoiceBufferItem*> mCurrentBuffer;

    /* Buffer queue item to loop to at end of queue (will be NULL for non-
     * looping voices).
     */
    std::atomic<VoiceBufferItem*> mLoopBuffer;

    std::chrono::nanoseconds mStartTime{};

    /* Properties for the attached buffer(s). */
    FmtChannels mFmtChannels{};
    unsigned mFrequency{};
    unsigned mFrameStep{}; /**< In steps of the sample type size. */
    unsigned mBytesPerBlock{}; /**< Or for PCM formats, BytesPerFrame. */
    unsigned mSamplesPerBlock{}; /**< Always 1 for PCM formats. */
    bool mDuplicateMono{};
    AmbiLayout mAmbiLayout{};
    AmbiScaling mAmbiScaling{};
    unsigned mAmbiOrder{};

    std::unique_ptr<DecoderBase> mDecoder;
    unsigned mDecoderPadding{};

    /** Current target parameters used for mixing. */
    unsigned mStep{0u};

    ResamplerFunc mResampler{};

    InterpState mResampleState;

    std::bitset<VoiceFlagCount> mFlags;
    unsigned mNumCallbackBlocks{0u};
    unsigned mCallbackBlockOffset{0u};

    struct TargetData {
        bool FilterActive{};
        std::span<FloatBufferLine> Buffer;
    };
    TargetData mDirect;
    std::array<TargetData, MaxSendCount> mSend;

    /* The first MaxResamplerPadding/2 elements are the sample history from the
     * previous mix, with an additional MaxResamplerPadding/2 elements that are
     * now current (which may be overwritten if the buffer data is still
     * available).
     */
    using HistoryLine = std::array<float, MaxResamplerPadding>;
    al::vector<HistoryLine, 16> mPrevSamples{2};

    struct ChannelData {
        float mAmbiHFScale{}, mAmbiLFScale{};
        BandSplitter mAmbiSplitter;

        DirectParams mDryParams;
        std::array<SendParams, MaxSendCount> mWetParams;
    };
    al::vector<ChannelData> mChans{2};

    Voice() = default;
    ~Voice() = default;
    Voice(const Voice&) = delete;
    Voice& operator=(Voice const&) = delete;

    void mix(State vstate, ContextBase *context, std::chrono::nanoseconds deviceTime,
        unsigned samplesToDo);

    void prepare(DeviceBase *device);

    static void InitMixer(std::optional<std::string> const &resopt);
};

#endif /* CORE_VOICE_H */
