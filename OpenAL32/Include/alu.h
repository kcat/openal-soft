#ifndef _ALU_H_
#define _ALU_H_

#include <limits.h>
#include <math.h>
#ifdef HAVE_FLOAT_H
#include <float.h>
#endif
#ifdef HAVE_IEEEFP_H
#include <ieeefp.h>
#endif

#include <array>

#include "alMain.h"
#include "alBuffer.h"

#include "hrtf.h"
#include "math_defs.h"
#include "filters/defs.h"
#include "filters/nfc.h"
#include "almalloc.h"


enum class DistanceModel;

#define MAX_PITCH  255
#define MAX_SENDS  16

/* Maximum number of samples to pad on either end of a buffer for resampling.
 * Note that both the beginning and end need padding!
 */
#define MAX_RESAMPLE_PADDING 24


struct BSincTable;
struct ALsource;
struct ALbufferlistitem;
struct ALvoice;
struct ALeffectslot;


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
extern enum Resampler ResamplerDefault;

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
typedef struct BsincState {
    ALfloat sf; /* Scale interpolation factor. */
    ALsizei m;  /* Coefficient count. */
    ALsizei l;  /* Left coefficient offset. */
    /* Filter coefficients, followed by the scale, phase, and scale-phase
     * delta coefficients. Starting at phase index 0, each subsequent phase
     * index follows contiguously.
     */
    const ALfloat *filter;
} BsincState;

typedef union InterpState {
    BsincState bsinc;
} InterpState;

typedef const ALfloat* (*ResamplerFunc)(const InterpState *state,
    const ALfloat *RESTRICT src, ALsizei frac, ALint increment,
    ALfloat *RESTRICT dst, ALsizei dstlen
);

void BsincPrepare(const ALuint increment, BsincState *state, const struct BSincTable *table);

extern const struct BSincTable bsinc12;
extern const struct BSincTable bsinc24;


enum {
    AF_None = 0,
    AF_LowPass = 1,
    AF_HighPass = 2,
    AF_BandPass = AF_LowPass | AF_HighPass
};


typedef struct MixHrtfParams {
    const ALfloat (*Coeffs)[2];
    ALsizei Delay[2];
    ALfloat Gain;
    ALfloat GainStep;
} MixHrtfParams;


typedef struct DirectParams {
    BiquadFilter LowPass;
    BiquadFilter HighPass;

    NfcFilter NFCtrlFilter;

    struct {
        HrtfParams Old;
        HrtfParams Target;
        HrtfState State;
    } Hrtf;

    struct {
        ALfloat Current[MAX_OUTPUT_CHANNELS];
        ALfloat Target[MAX_OUTPUT_CHANNELS];
    } Gains;
} DirectParams;

typedef struct SendParams {
    BiquadFilter LowPass;
    BiquadFilter HighPass;

    struct {
        ALfloat Current[MAX_OUTPUT_CHANNELS];
        ALfloat Target[MAX_OUTPUT_CHANNELS];
    } Gains;
} SendParams;


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
    ALfloat Position[3];
    ALfloat Velocity[3];
    ALfloat Direction[3];
    ALfloat Orientation[2][3];
    ALboolean HeadRelative;
    DistanceModel mDistanceModel;
    enum Resampler Resampler;
    ALboolean DirectChannels;
    enum SpatializeMode SpatializeMode;

    ALboolean DryGainHFAuto;
    ALboolean WetGainAuto;
    ALboolean WetGainHFAuto;
    ALfloat   OuterGainHF;

    ALfloat AirAbsorptionFactor;
    ALfloat RoomRolloffFactor;
    ALfloat DopplerFactor;

    ALfloat StereoPan[2];

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
        struct ALeffectslot *Slot;
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

#define VOICE_IS_STATIC (1<<0)
#define VOICE_IS_FADING (1<<1) /* Fading sources use gain stepping for smooth transitions. */
#define VOICE_HAS_HRTF  (1<<2)
#define VOICE_HAS_NFC   (1<<3)

struct ALvoice {
    std::atomic<ALvoiceProps*> Update{nullptr};

    std::atomic<ALuint> SourceID{0u};
    std::atomic<bool> Playing{false};

    ALvoicePropsBase Props;

    /**
     * Source offset in samples, relative to the currently playing buffer, NOT
     * the whole queue, and the fractional (fixed-point) offset to the next
     * sample.
     */
    std::atomic<ALuint> position;
    std::atomic<ALsizei> position_fraction;

    /* Current buffer queue item being played. */
    std::atomic<ALbufferlistitem*> current_buffer;

    /* Buffer queue item to loop to at end of queue (will be NULL for non-
     * looping voices).
     */
    std::atomic<ALbufferlistitem*> loop_buffer;

    /**
     * Number of channels and bytes-per-sample for the attached source's
     * buffer(s).
     */
    ALsizei NumChannels;
    ALsizei SampleSize;

    /** Current target parameters used for mixing. */
    ALint Step;

    ResamplerFunc Resampler;

    ALuint Flags;

    ALuint Offset; /* Number of output samples mixed since starting. */

    alignas(16) std::array<std::array<ALfloat,MAX_RESAMPLE_PADDING>,MAX_INPUT_CHANNELS> PrevSamples;

    InterpState ResampleState;

    struct {
        int FilterType;
        DirectParams Params[MAX_INPUT_CHANNELS];

        ALfloat (*Buffer)[BUFFERSIZE];
        ALsizei Channels;
        ALsizei ChannelsPerOrder[MAX_AMBI_ORDER+1];
    } Direct;

    struct {
        int FilterType;
        SendParams Params[MAX_INPUT_CHANNELS];

        ALfloat (*Buffer)[BUFFERSIZE];
        ALsizei Channels;
    } Send[];
};

void DeinitVoice(ALvoice *voice) noexcept;


typedef void (*MixerFunc)(const ALfloat *data, ALsizei OutChans,
                          ALfloat (*RESTRICT OutBuffer)[BUFFERSIZE], ALfloat *CurrentGains,
                          const ALfloat *TargetGains, ALsizei Counter, ALsizei OutPos,
                          ALsizei BufferSize);
typedef void (*RowMixerFunc)(ALfloat *OutBuffer, const ALfloat *gains,
                             const ALfloat (*RESTRICT data)[BUFFERSIZE], ALsizei InChans,
                             ALsizei InPos, ALsizei BufferSize);
typedef void (*HrtfMixerFunc)(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut,
                              const ALfloat *data, ALsizei Offset, ALsizei OutPos,
                              const ALsizei IrSize, MixHrtfParams *hrtfparams,
                              HrtfState *hrtfstate, ALsizei BufferSize);
typedef void (*HrtfMixerBlendFunc)(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut,
                                   const ALfloat *data, ALsizei Offset, ALsizei OutPos,
                                   const ALsizei IrSize, const HrtfParams *oldparams,
                                   MixHrtfParams *newparams, HrtfState *hrtfstate,
                                   ALsizei BufferSize);
typedef void (*HrtfDirectMixerFunc)(ALfloat *RESTRICT LeftOut, ALfloat *RESTRICT RightOut,
                                    const ALfloat *data, ALsizei Offset, const ALsizei IrSize,
                                    const ALfloat (*RESTRICT Coeffs)[2],
                                    ALfloat (*RESTRICT Values)[2], ALsizei BufferSize);


#define GAIN_MIX_MAX  (16.0f) /* +24dB */

#define GAIN_SILENCE_THRESHOLD  (0.00001f) /* -100dB */

#define SPEEDOFSOUNDMETRESPERSEC  (343.3f)
#define AIRABSORBGAINHF           (0.99426f) /* -0.05dB */

/* Target gain for the reverb decay feedback reaching the decay time. */
#define REVERB_DECAY_GAIN  (0.001f) /* -60 dB */

#define FRACTIONBITS (12)
#define FRACTIONONE  (1<<FRACTIONBITS)
#define FRACTIONMASK (FRACTIONONE-1)


inline ALfloat minf(ALfloat a, ALfloat b) noexcept
{ return ((a > b) ? b : a); }
inline ALfloat maxf(ALfloat a, ALfloat b) noexcept
{ return ((a > b) ? a : b); }
inline ALfloat clampf(ALfloat val, ALfloat min, ALfloat max) noexcept
{ return minf(max, maxf(min, val)); }

inline ALdouble mind(ALdouble a, ALdouble b) noexcept
{ return ((a > b) ? b : a); }
inline ALdouble maxd(ALdouble a, ALdouble b) noexcept
{ return ((a > b) ? a : b); }
inline ALdouble clampd(ALdouble val, ALdouble min, ALdouble max) noexcept
{ return mind(max, maxd(min, val)); }

inline ALuint minu(ALuint a, ALuint b) noexcept
{ return ((a > b) ? b : a); }
inline ALuint maxu(ALuint a, ALuint b) noexcept
{ return ((a > b) ? a : b); }
inline ALuint clampu(ALuint val, ALuint min, ALuint max) noexcept
{ return minu(max, maxu(min, val)); }

inline ALint mini(ALint a, ALint b) noexcept
{ return ((a > b) ? b : a); }
inline ALint maxi(ALint a, ALint b) noexcept
{ return ((a > b) ? a : b); }
inline ALint clampi(ALint val, ALint min, ALint max) noexcept
{ return mini(max, maxi(min, val)); }

inline ALint64 mini64(ALint64 a, ALint64 b) noexcept
{ return ((a > b) ? b : a); }
inline ALint64 maxi64(ALint64 a, ALint64 b) noexcept
{ return ((a > b) ? a : b); }
inline ALint64 clampi64(ALint64 val, ALint64 min, ALint64 max) noexcept
{ return mini64(max, maxi64(min, val)); }

inline ALuint64 minu64(ALuint64 a, ALuint64 b) noexcept
{ return ((a > b) ? b : a); }
inline ALuint64 maxu64(ALuint64 a, ALuint64 b) noexcept
{ return ((a > b) ? a : b); }
inline ALuint64 clampu64(ALuint64 val, ALuint64 min, ALuint64 max) noexcept
{ return minu64(max, maxu64(min, val)); }

inline size_t minz(size_t a, size_t b) noexcept
{ return ((a > b) ? b : a); }
inline size_t maxz(size_t a, size_t b) noexcept
{ return ((a > b) ? a : b); }
inline size_t clampz(size_t val, size_t min, size_t max) noexcept
{ return minz(max, maxz(min, val)); }


inline ALfloat lerp(ALfloat val1, ALfloat val2, ALfloat mu) noexcept
{
    return val1 + (val2-val1)*mu;
}
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

ResamplerFunc SelectResampler(enum Resampler resampler);

/* aluInitRenderer
 *
 * Set up the appropriate panning method and mixing method given the device
 * properties.
 */
void aluInitRenderer(ALCdevice *device, ALint hrtf_id, enum HrtfRequestMode hrtf_appreq, enum HrtfRequestMode hrtf_userreq);

void aluInitEffectPanning(struct ALeffectslot *slot);

void aluSelectPostProcess(ALCdevice *device);

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
                    ALfloat coeffs[MAX_AMBI_COEFFS]);

/**
 * CalcDirectionCoeffs
 *
 * Calculates ambisonic coefficients based on an OpenAL direction vector. The
 * vector must be normalized (unit length), and the spread is the angular width
 * of the sound (0...tau).
 */
inline void CalcDirectionCoeffs(const ALfloat dir[3], ALfloat spread, ALfloat coeffs[MAX_AMBI_COEFFS])
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
inline void CalcAngleCoeffs(ALfloat azimuth, ALfloat elevation, ALfloat spread, ALfloat coeffs[MAX_AMBI_COEFFS])
{
    ALfloat x = -sinf(azimuth) * cosf(elevation);
    ALfloat y = sinf(elevation);
    ALfloat z = cosf(azimuth) * cosf(elevation);

    CalcAmbiCoeffs(x, y, z, spread, coeffs);
}

/**
 * ScaleAzimuthFront
 *
 * Scales the given azimuth toward the side (+/- pi/2 radians) for positions in
 * front.
 */
inline float ScaleAzimuthFront(float azimuth, float scale)
{
    ALfloat sign = copysignf(1.0f, azimuth);
    if(!(fabsf(azimuth) > F_PI_2))
        return minf(fabsf(azimuth) * scale, F_PI_2) * sign;
    return azimuth;
}


void ComputePanningGainsMC(const ChannelConfig *chancoeffs, ALsizei numchans, ALsizei numcoeffs, const ALfloat*RESTRICT coeffs, ALfloat ingain, ALfloat gains[MAX_OUTPUT_CHANNELS]);
void ComputePanningGainsBF(const BFChannelConfig *chanmap, ALsizei numchans, const ALfloat*RESTRICT coeffs, ALfloat ingain, ALfloat gains[MAX_OUTPUT_CHANNELS]);

/**
 * ComputePanGains
 *
 * Computes panning gains using the given channel decoder coefficients and the
 * pre-calculated direction or angle coefficients. For B-Format sources, the
 * coeffs are a 'slice' of a transform matrix for the input channel, used to
 * scale and orient the sound samples.
 */
inline void ComputePanGains(const MixParams *dry, const ALfloat*RESTRICT coeffs, ALfloat ingain, ALfloat gains[MAX_OUTPUT_CHANNELS])
{
    if(dry->CoeffCount > 0)
        ComputePanningGainsMC(dry->Ambi.Coeffs, dry->NumChannels, dry->CoeffCount,
                              coeffs, ingain, gains);
    else
        ComputePanningGainsBF(dry->Ambi.Map, dry->NumChannels, coeffs, ingain, gains);
}


ALboolean MixSource(struct ALvoice *voice, ALuint SourceID, ALCcontext *Context, ALsizei SamplesToDo);

void aluMixData(ALCdevice *device, ALvoid *OutBuffer, ALsizei NumSamples);
/* Caller must lock the device, and the mixer must not be running. */
void aluHandleDisconnect(ALCdevice *device, const char *msg, ...) DECL_FORMAT(printf, 2, 3);

extern MixerFunc MixSamples;
extern RowMixerFunc MixRowSamples;

extern ALfloat ConeScale;
extern ALfloat ZScale;
extern ALboolean OverrideReverbSpeedOfSound;

#endif
