#ifndef VOICE_H
#define VOICE_H

#include <array>
#include <atomic>

#include "almalloc.h"
#include "alspan.h"
#include "alu.h"
#include "buffer_storage.h"
#include "core/bufferline.h"
#include "core/devformat.h"
#include "core/filters/biquad.h"
#include "core/filters/nfc.h"
#include "core/filters/splitter.h"
#include "core/mixer/defs.h"
#include "core/mixer/hrtfdefs.h"
#include "vector.h"

struct ALCcontext;
struct EffectSlot;
enum class DistanceModel : unsigned char;

using uint = unsigned int;


enum class SpatializeMode : unsigned char {
    Off,
    On,
    Auto
};

enum class DirectMode : unsigned char {
    Off,
    DropMismatch,
    RemixMismatch
};


enum {
    AF_None = 0,
    AF_LowPass = 1,
    AF_HighPass = 2,
    AF_BandPass = AF_LowPass | AF_HighPass
};


struct DirectParams {
    BiquadFilter LowPass;
    BiquadFilter HighPass;

    NfcFilter NFCtrlFilter;

    struct {
        HrtfFilter Old;
        HrtfFilter Target;
        alignas(16) std::array<float,HrtfHistoryLength> History;
    } Hrtf;

    struct {
        std::array<float,MAX_OUTPUT_CHANNELS> Current;
        std::array<float,MAX_OUTPUT_CHANNELS> Target;
    } Gains;
};

struct SendParams {
    BiquadFilter LowPass;
    BiquadFilter HighPass;

    struct {
        std::array<float,MAX_OUTPUT_CHANNELS> Current;
        std::array<float,MAX_OUTPUT_CHANNELS> Target;
    } Gains;
};


struct VoiceBufferItem {
    std::atomic<VoiceBufferItem*> mNext{nullptr};

    CallbackType mCallback{nullptr};
    void *mUserData{nullptr};

    uint mSampleLen{0u};
    uint mLoopStart{0u};
    uint mLoopEnd{0u};

    al::byte *mSamples{nullptr};
};


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
    std::array<float,3> Position;
    std::array<float,3> Velocity;
    std::array<float,3> Direction;
    std::array<float,3> OrientAt;
    std::array<float,3> OrientUp;
    bool HeadRelative;
    DistanceModel mDistanceModel;
    Resampler mResampler;
    DirectMode DirectChannels;
    SpatializeMode mSpatializeMode;

    bool DryGainHFAuto;
    bool WetGainAuto;
    bool WetGainHFAuto;
    float OuterGainHF;

    float AirAbsorptionFactor;
    float RoomRolloffFactor;
    float DopplerFactor;

    std::array<float,2> StereoPan;

    float Radius;

    /** Direct filter and auxiliary send info. */
    struct {
        float Gain;
        float GainHF;
        float HFReference;
        float GainLF;
        float LFReference;
    } Direct;
    struct SendData {
        EffectSlot *Slot;
        float Gain;
        float GainHF;
        float HFReference;
        float GainLF;
        float LFReference;
    } Send[MAX_SENDS];
};

struct VoicePropsItem : public VoiceProps {
    std::atomic<VoicePropsItem*> next{nullptr};

    DEF_NEWDEL(VoicePropsItem)
};

constexpr uint VoiceIsStatic{       1u<<0};
constexpr uint VoiceIsCallback{     1u<<1};
constexpr uint VoiceIsAmbisonic{    1u<<2}; /* Needs HF scaling for ambisonic upsampling. */
constexpr uint VoiceCallbackStopped{1u<<3};
constexpr uint VoiceIsFading{       1u<<4}; /* Use gain stepping for smooth transitions. */
constexpr uint VoiceHasHrtf{        1u<<5};
constexpr uint VoiceHasNfc{         1u<<6};

struct Voice {
    enum State {
        Stopped,
        Playing,
        Stopping,
        Pending
    };

    std::atomic<VoicePropsItem*> mUpdate{nullptr};

    VoiceProps mProps;

    std::atomic<uint> mSourceID{0u};
    std::atomic<State> mPlayState{Stopped};
    std::atomic<bool> mPendingChange{false};

    /**
     * Source offset in samples, relative to the currently playing buffer, NOT
     * the whole queue.
     */
    std::atomic<uint> mPosition;
    /** Fractional (fixed-point) offset to the next sample. */
    std::atomic<uint> mPositionFrac;

    /* Current buffer queue item being played. */
    std::atomic<VoiceBufferItem*> mCurrentBuffer;

    /* Buffer queue item to loop to at end of queue (will be NULL for non-
     * looping voices).
     */
    std::atomic<VoiceBufferItem*> mLoopBuffer;

    /* Properties for the attached buffer(s). */
    FmtChannels mFmtChannels;
    FmtType mFmtType;
    uint mFrequency;
    uint mSampleSize;
    AmbiLayout mAmbiLayout;
    AmbiScaling mAmbiScaling;
    uint mAmbiOrder;

    /** Current target parameters used for mixing. */
    uint mStep{0};

    ResamplerFunc mResampler;

    InterpState mResampleState;

    uint mFlags{};
    uint mNumCallbackSamples{0};

    struct TargetData {
        int FilterType;
        al::span<FloatBufferLine> Buffer;
    };
    TargetData mDirect;
    std::array<TargetData,MAX_SENDS> mSend;

    struct ChannelData {
        alignas(16) std::array<float,MaxResamplerPadding> mPrevSamples;

        float mAmbiScale;
        BandSplitter mAmbiSplitter;

        DirectParams mDryParams;
        std::array<SendParams,MAX_SENDS> mWetParams;
    };
    al::vector<ChannelData> mChans{2};

    Voice() = default;
    ~Voice() { delete mUpdate.exchange(nullptr, std::memory_order_acq_rel); }

    Voice(const Voice&) = delete;
    Voice& operator=(const Voice&) = delete;

    void mix(const State vstate, ALCcontext *Context, const uint SamplesToDo);

    DEF_NEWDEL(Voice)
};

extern Resampler ResamplerDefault;

#endif /* VOICE_H */
