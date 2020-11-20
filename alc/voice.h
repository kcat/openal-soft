#ifndef VOICE_H
#define VOICE_H

#include <array>

#include "almalloc.h"
#include "alspan.h"
#include "alu.h"
#include "buffer_storage.h"
#include "devformat.h"
#include "filters/biquad.h"
#include "filters/nfc.h"
#include "filters/splitter.h"
#include "hrtf.h"

struct EffectSlot;
struct BufferlistItem;
enum class DistanceModel;

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

enum class Resampler {
    Point,
    Linear,
    Cubic,
    FastBSinc12,
    BSinc12,
    FastBSinc24,
    BSinc24,

    Max = BSinc24
};
extern Resampler ResamplerDefault;


/* Interpolator state. Kind of a misnomer since the interpolator itself is
 * stateless. This just keeps it from having to recompute scale-related
 * mappings for every sample.
 */
struct BsincState {
    float sf; /* Scale interpolation factor. */
    uint m; /* Coefficient count. */
    uint l; /* Left coefficient offset. */
    /* Filter coefficients, followed by the phase, scale, and scale-phase
     * delta coefficients. Starting at phase index 0, each subsequent phase
     * index follows contiguously.
     */
    const float *filter;
};

union InterpState {
    BsincState bsinc;
};

using ResamplerFunc = const float*(*)(const InterpState *state, const float *RESTRICT src,
    uint frac, uint increment, const al::span<float> dst);

ResamplerFunc PrepareResampler(Resampler resampler, uint increment, InterpState *state);


enum {
    AF_None = 0,
    AF_LowPass = 1,
    AF_HighPass = 2,
    AF_BandPass = AF_LowPass | AF_HighPass
};


struct MixHrtfFilter {
    const HrirArray *Coeffs;
    std::array<uint,2> Delay;
    float Gain;
    float GainStep;
};


struct DirectParams {
    BiquadFilter LowPass;
    BiquadFilter HighPass;

    NfcFilter NFCtrlFilter;

    struct {
        HrtfFilter Old;
        HrtfFilter Target;
        alignas(16) std::array<float,HRTF_HISTORY_LENGTH> History;
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
    std::atomic<BufferlistItem*> mCurrentBuffer;

    /* Buffer queue item to loop to at end of queue (will be NULL for non-
     * looping voices).
     */
    std::atomic<BufferlistItem*> mLoopBuffer;

    /* Properties for the attached buffer(s). */
    FmtChannels mFmtChannels;
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
        alignas(16) std::array<float,MAX_RESAMPLER_PADDING> mPrevSamples;

        float mAmbiScale;
        BandSplitter mAmbiSplitter;

        DirectParams mDryParams;
        std::array<SendParams,MAX_SENDS> mWetParams;
    };
    al::vector<ChannelData> mChans{2};

    Voice() = default;
    Voice(const Voice&) = delete;
    ~Voice() { delete mUpdate.exchange(nullptr, std::memory_order_acq_rel); }
    Voice& operator=(const Voice&) = delete;

    void mix(const State vstate, ALCcontext *Context, const uint SamplesToDo);

    DEF_NEWDEL(Voice)
};

#endif /* VOICE_H */
