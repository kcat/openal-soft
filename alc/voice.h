#ifndef VOICE_H
#define VOICE_H

#include <array>

#include "AL/al.h"
#include "AL/alext.h"

#include "al/buffer.h"
#include "alspan.h"
#include "alu.h"
#include "devformat.h"
#include "filters/biquad.h"
#include "filters/nfc.h"
#include "filters/splitter.h"
#include "hrtf.h"

enum class DistanceModel;


enum SpatializeMode {
    SpatializeOff = AL_FALSE,
    SpatializeOn = AL_TRUE,
    SpatializeAuto = AL_AUTO_SOFT
};

enum class DirectMode : unsigned char {
    Off = AL_FALSE,
    DropMismatch = AL_DROP_UNMATCHED_SOFT,
    RemixMismatch = AL_REMIX_UNMATCHED_SOFT
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

/* The number of distinct scale and phase intervals within the bsinc filter
 * table.
 */
#define BSINC_SCALE_BITS  4
#define BSINC_SCALE_COUNT (1<<BSINC_SCALE_BITS)
#define BSINC_PHASE_BITS  5
#define BSINC_PHASE_COUNT (1<<BSINC_PHASE_BITS)

/* Interpolator state.  Kind of a misnomer since the interpolator itself is
 * stateless.  This just keeps it from having to recompute scale-related
 * mappings for every sample.
 */
struct BsincState {
    float sf; /* Scale interpolation factor. */
    ALuint m; /* Coefficient count. */
    ALuint l; /* Left coefficient offset. */
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
    ALuint frac, ALuint increment, const al::span<float> dst);

ResamplerFunc PrepareResampler(Resampler resampler, ALuint increment, InterpState *state);


enum {
    AF_None = 0,
    AF_LowPass = 1,
    AF_HighPass = 2,
    AF_BandPass = AF_LowPass | AF_HighPass
};


struct MixHrtfFilter {
    const HrirArray *Coeffs;
    ALuint Delay[2];
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


struct ALvoicePropsBase {
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
        ALeffectslot *Slot;
        float Gain;
        float GainHF;
        float HFReference;
        float GainLF;
        float LFReference;
    } Send[MAX_SENDS];
};

struct ALvoiceProps : public ALvoicePropsBase {
    std::atomic<ALvoiceProps*> next{nullptr};

    DEF_NEWDEL(ALvoiceProps)
};

#define VOICE_IS_STATIC    (1u<<0)
#define VOICE_IS_FADING    (1u<<1) /* Fading sources use gain stepping for smooth transitions. */
#define VOICE_IS_AMBISONIC (1u<<2) /* Voice needs HF scaling for ambisonic upsampling. */
#define VOICE_HAS_HRTF     (1u<<3)
#define VOICE_HAS_NFC      (1u<<4)

struct ALvoice {
    enum State {
        Stopped = 0,
        Playing = 1,
        Stopping = 2
    };

    std::atomic<ALvoiceProps*> mUpdate{nullptr};

    std::atomic<ALuint> mSourceID{0u};
    std::atomic<State> mPlayState{Stopped};

    ALvoicePropsBase mProps;

    /**
     * Source offset in samples, relative to the currently playing buffer, NOT
     * the whole queue.
     */
    std::atomic<ALuint> mPosition;
    /** Fractional (fixed-point) offset to the next sample. */
    std::atomic<ALuint> mPositionFrac;

    /* Current buffer queue item being played. */
    std::atomic<ALbufferlistitem*> mCurrentBuffer;

    /* Buffer queue item to loop to at end of queue (will be NULL for non-
     * looping voices).
     */
    std::atomic<ALbufferlistitem*> mLoopBuffer;

    /* Properties for the attached buffer(s). */
    FmtChannels mFmtChannels;
    ALuint mFrequency;
    ALuint mNumChannels;
    ALuint mSampleSize;
    AmbiLayout mAmbiLayout;
    AmbiNorm mAmbiScaling;
    ALuint mAmbiOrder;

    /** Current target parameters used for mixing. */
    ALuint mStep;

    ResamplerFunc mResampler;

    InterpState mResampleState;

    ALuint mFlags;

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
    std::array<ChannelData,MAX_INPUT_CHANNELS> mChans;

    ALvoice() = default;
    ALvoice(const ALvoice&) = delete;
    ALvoice(ALvoice&& rhs) noexcept { *this = std::move(rhs); }
    ~ALvoice() { delete mUpdate.exchange(nullptr, std::memory_order_acq_rel); }
    ALvoice& operator=(const ALvoice&) = delete;
    ALvoice& operator=(ALvoice&& rhs) noexcept
    {
        ALvoiceProps *old_update{mUpdate.load(std::memory_order_relaxed)};
        mUpdate.store(rhs.mUpdate.exchange(old_update, std::memory_order_relaxed),
            std::memory_order_relaxed);

        mSourceID.store(rhs.mSourceID.load(std::memory_order_relaxed), std::memory_order_relaxed);
        mPlayState.store(rhs.mPlayState.load(std::memory_order_relaxed),
            std::memory_order_relaxed);

        mProps = rhs.mProps;

        mPosition.store(rhs.mPosition.load(std::memory_order_relaxed), std::memory_order_relaxed);
        mPositionFrac.store(rhs.mPositionFrac.load(std::memory_order_relaxed),
            std::memory_order_relaxed);

        mCurrentBuffer.store(rhs.mCurrentBuffer.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        mLoopBuffer.store(rhs.mLoopBuffer.load(std::memory_order_relaxed),
            std::memory_order_relaxed);

        mFmtChannels = rhs.mFmtChannels;
        mFrequency = rhs.mFrequency;
        mNumChannels = rhs.mNumChannels;
        mSampleSize = rhs.mSampleSize;
        mAmbiLayout = rhs.mAmbiLayout;
        mAmbiScaling = rhs.mAmbiScaling;
        mAmbiOrder = rhs.mAmbiOrder;

        mStep = rhs.mStep;
        mResampler = rhs.mResampler;

        mResampleState = rhs.mResampleState;

        mFlags = rhs.mFlags;

        mDirect = rhs.mDirect;
        mSend = rhs.mSend;
        mChans = rhs.mChans;

        return *this;
    }

    void mix(const State vstate, ALCcontext *Context, const ALuint SamplesToDo);
};

#endif /* VOICE_H */
