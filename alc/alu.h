#ifndef ALU_H
#define ALU_H

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "al/buffer.h"
#include "alcmain.h"
#include "almalloc.h"
#include "alspan.h"
#include "ambidefs.h"
#include "devformat.h"
#include "filters/biquad.h"
#include "filters/nfc.h"
#include "filters/splitter.h"
#include "hrtf.h"
#include "logging.h"

struct ALbufferlistitem;
struct ALeffectslot;
struct BSincTable;


enum class DistanceModel;

#define MAX_PITCH  255
#define MAX_SENDS  16


#define DITHER_RNG_SEED 22222


enum SpatializeMode {
    SpatializeOff = AL_FALSE,
    SpatializeOn = AL_TRUE,
    SpatializeAuto = AL_AUTO_SOFT
};

enum Resampler {
    PointResampler,
    LinearResampler,
    FIR4Resampler,
    BSinc12Resampler,
    BSinc24Resampler,

    ResamplerMax = BSinc24Resampler
};
extern Resampler ResamplerDefault;

/* The number of distinct scale and phase intervals within the bsinc filter
 * table.
 */
#define BSINC_SCALE_BITS  4
#define BSINC_SCALE_COUNT (1<<BSINC_SCALE_BITS)
#define BSINC_PHASE_BITS  4
#define BSINC_PHASE_COUNT (1<<BSINC_PHASE_BITS)

/* Interpolator state.  Kind of a misnomer since the interpolator itself is
 * stateless.  This just keeps it from having to recompute scale-related
 * mappings for every sample.
 */
struct BsincState {
    ALfloat sf; /* Scale interpolation factor. */
    ALsizei m;  /* Coefficient count. */
    ALsizei l;  /* Left coefficient offset. */
    /* Filter coefficients, followed by the scale, phase, and scale-phase
     * delta coefficients. Starting at phase index 0, each subsequent phase
     * index follows contiguously.
     */
    const ALfloat *filter;
};

union InterpState {
    BsincState bsinc;
};

using ResamplerFunc = const ALfloat*(*)(const InterpState *state,
    const ALfloat *RESTRICT src, ALsizei frac, ALint increment,
    ALfloat *RESTRICT dst, ALsizei dstlen);

void BsincPrepare(const ALuint increment, BsincState *state, const BSincTable *table);

extern const BSincTable bsinc12;
extern const BSincTable bsinc24;


enum {
    AF_None = 0,
    AF_LowPass = 1,
    AF_HighPass = 2,
    AF_BandPass = AF_LowPass | AF_HighPass
};


struct MixHrtfFilter {
    const HrirArray *Coeffs;
    ALsizei Delay[2];
    ALfloat Gain;
    ALfloat GainStep;
};


struct DirectParams {
    BiquadFilter LowPass;
    BiquadFilter HighPass;

    NfcFilter NFCtrlFilter;

    struct {
        HrtfFilter Old;
        HrtfFilter Target;
        HrtfState State;
    } Hrtf;

    struct {
        ALfloat Current[MAX_OUTPUT_CHANNELS];
        ALfloat Target[MAX_OUTPUT_CHANNELS];
    } Gains;
};

struct SendParams {
    BiquadFilter LowPass;
    BiquadFilter HighPass;

    struct {
        ALfloat Current[MAX_OUTPUT_CHANNELS];
        ALfloat Target[MAX_OUTPUT_CHANNELS];
    } Gains;
};


struct ALvoicePropsBase {
    ALfloat Pitch;
    ALfloat Gain;
    ALfloat OuterGain;
    ALfloat MinGain;
    ALfloat MaxGain;
    ALfloat InnerAngle;
    ALfloat OuterAngle;
    ALfloat RefDistance;
    ALfloat MaxDistance;
    ALfloat RolloffFactor;
    std::array<ALfloat,3> Position;
    std::array<ALfloat,3> Velocity;
    std::array<ALfloat,3> Direction;
    std::array<ALfloat,3> OrientAt;
    std::array<ALfloat,3> OrientUp;
    ALboolean HeadRelative;
    DistanceModel mDistanceModel;
    Resampler mResampler;
    ALboolean DirectChannels;
    SpatializeMode mSpatializeMode;

    ALboolean DryGainHFAuto;
    ALboolean WetGainAuto;
    ALboolean WetGainHFAuto;
    ALfloat   OuterGainHF;

    ALfloat AirAbsorptionFactor;
    ALfloat RoomRolloffFactor;
    ALfloat DopplerFactor;

    std::array<ALfloat,2> StereoPan;

    ALfloat Radius;

    /** Direct filter and auxiliary send info. */
    struct {
        ALfloat Gain;
        ALfloat GainHF;
        ALfloat HFReference;
        ALfloat GainLF;
        ALfloat LFReference;
    } Direct;
    struct SendData {
        ALeffectslot *Slot;
        ALfloat Gain;
        ALfloat GainHF;
        ALfloat HFReference;
        ALfloat GainLF;
        ALfloat LFReference;
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
    std::atomic<ALsizei> mPositionFrac;

    /* Current buffer queue item being played. */
    std::atomic<ALbufferlistitem*> mCurrentBuffer;

    /* Buffer queue item to loop to at end of queue (will be NULL for non-
     * looping voices).
     */
    std::atomic<ALbufferlistitem*> mLoopBuffer;

    /* Properties for the attached buffer(s). */
    FmtChannels mFmtChannels;
    ALuint mFrequency;
    ALsizei mNumChannels;
    ALsizei mSampleSize;

    /** Current target parameters used for mixing. */
    ALint mStep;

    ResamplerFunc mResampler;

    InterpState mResampleState;

    ALuint mFlags;

    struct DirectData {
        int FilterType;
        al::span<FloatBufferLine> Buffer;
    };
    DirectData mDirect;

    struct SendData {
        int FilterType;
        al::span<FloatBufferLine> Buffer;
    };
    std::array<SendData,MAX_SENDS> mSend;

    struct ChannelData {
        alignas(16) std::array<ALfloat,MAX_RESAMPLE_PADDING*2> mPrevSamples;

        ALfloat mAmbiScale;
        BandSplitter mAmbiSplitter;

        DirectParams mDryParams;
        std::array<SendParams,MAX_SENDS> mWetParams;
    };
    std::array<ChannelData,MAX_INPUT_CHANNELS> mChans;

    ALvoice() = default;
    ALvoice(const ALvoice&) = delete;
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

        mStep = rhs.mStep;
        mResampler = rhs.mResampler;

        mResampleState = rhs.mResampleState;

        mFlags = rhs.mFlags;

        mDirect = rhs.mDirect;
        mSend = rhs.mSend;
        mChans = rhs.mChans;

        return *this;
    }
};


using MixerFunc = void(*)(const ALfloat *InSamples, const al::span<FloatBufferLine> OutBuffer,
    ALfloat *CurrentGains, const ALfloat *TargetGains, const ALsizei Counter, const ALsizei OutPos,
    const ALsizei BufferSize);
using RowMixerFunc = void(*)(const al::span<float> OutBuffer, const al::span<const float> Gains,
    const float *InSamples, const size_t InStride);
using HrtfMixerFunc = void(*)(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const ALfloat *InSamples, float2 *AccumSamples, const ALsizei OutPos, const ALsizei IrSize,
    MixHrtfFilter *hrtfparams, const ALsizei BufferSize);
using HrtfMixerBlendFunc = void(*)(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const ALfloat *InSamples, float2 *AccumSamples, const ALsizei OutPos, const ALsizei IrSize,
    const HrtfFilter *oldparams, MixHrtfFilter *newparams, const ALsizei BufferSize);
using HrtfDirectMixerFunc = void(*)(FloatBufferLine &LeftOut, FloatBufferLine &RightOut,
    const al::span<const FloatBufferLine> InSamples, float2 *AccumSamples, DirectHrtfState *State,
    const ALsizei BufferSize);


#define GAIN_MIX_MAX  (1000.0f) /* +60dB */

#define GAIN_SILENCE_THRESHOLD  (0.00001f) /* -100dB */

#define SPEEDOFSOUNDMETRESPERSEC  (343.3f)
#define AIRABSORBGAINHF           (0.99426f) /* -0.05dB */

/* Target gain for the reverb decay feedback reaching the decay time. */
#define REVERB_DECAY_GAIN  (0.001f) /* -60 dB */

#define FRACTIONBITS (12)
#define FRACTIONONE  (1<<FRACTIONBITS)
#define FRACTIONMASK (FRACTIONONE-1)


inline ALfloat lerp(ALfloat val1, ALfloat val2, ALfloat mu) noexcept
{ return val1 + (val2-val1)*mu; }
inline ALfloat cubic(ALfloat val1, ALfloat val2, ALfloat val3, ALfloat val4, ALfloat mu) noexcept
{
    ALfloat mu2 = mu*mu, mu3 = mu2*mu;
    ALfloat a0 = -0.5f*mu3 +       mu2 + -0.5f*mu;
    ALfloat a1 =  1.5f*mu3 + -2.5f*mu2            + 1.0f;
    ALfloat a2 = -1.5f*mu3 +  2.0f*mu2 +  0.5f*mu;
    ALfloat a3 =  0.5f*mu3 + -0.5f*mu2;
    return val1*a0 + val2*a1 + val3*a2 + val4*a3;
}


enum HrtfRequestMode {
    Hrtf_Default = 0,
    Hrtf_Enable = 1,
    Hrtf_Disable = 2,
};

void aluInit(void);

void aluInitMixer(void);

ResamplerFunc SelectResampler(Resampler resampler);

/* aluInitRenderer
 *
 * Set up the appropriate panning method and mixing method given the device
 * properties.
 */
void aluInitRenderer(ALCdevice *device, ALint hrtf_id, HrtfRequestMode hrtf_appreq, HrtfRequestMode hrtf_userreq);

void aluInitEffectPanning(ALeffectslot *slot, ALCdevice *device);

/**
 * Calculates ambisonic encoder coefficients using the X, Y, and Z direction
 * components, which must represent a normalized (unit length) vector, and the
 * spread is the angular width of the sound (0...tau).
 *
 * NOTE: The components use ambisonic coordinates. As a result:
 *
 * Ambisonic Y = OpenAL -X
 * Ambisonic Z = OpenAL Y
 * Ambisonic X = OpenAL -Z
 *
 * The components are ordered such that OpenAL's X, Y, and Z are the first,
 * second, and third parameters respectively -- simply negate X and Z.
 */
void CalcAmbiCoeffs(const ALfloat y, const ALfloat z, const ALfloat x, const ALfloat spread,
                    ALfloat (&coeffs)[MAX_AMBI_CHANNELS]);

/**
 * CalcDirectionCoeffs
 *
 * Calculates ambisonic coefficients based on an OpenAL direction vector. The
 * vector must be normalized (unit length), and the spread is the angular width
 * of the sound (0...tau).
 */
inline void CalcDirectionCoeffs(const ALfloat (&dir)[3], ALfloat spread, ALfloat (&coeffs)[MAX_AMBI_CHANNELS])
{
    /* Convert from OpenAL coords to Ambisonics. */
    CalcAmbiCoeffs(-dir[0], dir[1], -dir[2], spread, coeffs);
}

/**
 * CalcAngleCoeffs
 *
 * Calculates ambisonic coefficients based on azimuth and elevation. The
 * azimuth and elevation parameters are in radians, going right and up
 * respectively.
 */
inline void CalcAngleCoeffs(ALfloat azimuth, ALfloat elevation, ALfloat spread, ALfloat (&coeffs)[MAX_AMBI_CHANNELS])
{
    ALfloat x = -std::sin(azimuth) * std::cos(elevation);
    ALfloat y = std::sin(elevation);
    ALfloat z = std::cos(azimuth) * std::cos(elevation);

    CalcAmbiCoeffs(x, y, z, spread, coeffs);
}


/**
 * ComputePanGains
 *
 * Computes panning gains using the given channel decoder coefficients and the
 * pre-calculated direction or angle coefficients. For B-Format sources, the
 * coeffs are a 'slice' of a transform matrix for the input channel, used to
 * scale and orient the sound samples.
 */
void ComputePanGains(const MixParams *mix, const ALfloat*RESTRICT coeffs, ALfloat ingain, ALfloat (&gains)[MAX_OUTPUT_CHANNELS]);


inline std::array<ALfloat,MAX_AMBI_CHANNELS> GetAmbiIdentityRow(size_t i) noexcept
{
    std::array<ALfloat,MAX_AMBI_CHANNELS> ret{};
    ret[i] = 1.0f;
    return ret;
}


void MixVoice(ALvoice *voice, ALvoice::State vstate, const ALuint SourceID, ALCcontext *Context, const ALsizei SamplesToDo);

void aluMixData(ALCdevice *device, ALvoid *OutBuffer, ALsizei NumSamples);
/* Caller must lock the device state, and the mixer must not be running. */
void aluHandleDisconnect(ALCdevice *device, const char *msg, ...) DECL_FORMAT(printf, 2, 3);

extern MixerFunc MixSamples;
extern RowMixerFunc MixRowSamples;

extern const ALfloat ConeScale;
extern const ALfloat ZScale;

#endif
