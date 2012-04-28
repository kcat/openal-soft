#ifndef _AL_SOURCE_H_
#define _AL_SOURCE_H_

#define MAX_SENDS                 4

#include "alFilter.h"
#include "alu.h"
#include "AL/al.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SRC_HISTORY_BITS   (6)
#define SRC_HISTORY_LENGTH (1<<SRC_HISTORY_BITS)
#define SRC_HISTORY_MASK   (SRC_HISTORY_LENGTH-1)

extern enum Resampler DefaultResampler;

extern const ALsizei ResamplerPadding[ResamplerMax];
extern const ALsizei ResamplerPrePadding[ResamplerMax];


typedef struct ALbufferlistitem
{
    struct ALbuffer         *buffer;
    struct ALbufferlistitem *next;
    struct ALbufferlistitem *prev;
} ALbufferlistitem;

typedef struct HrtfState {
    ALboolean Moving;
    ALuint Counter;
    ALfloat History[MAXCHANNELS][SRC_HISTORY_LENGTH];
    ALfloat Values[MAXCHANNELS][HRIR_LENGTH][2];
    ALuint Offset;
} HrtfState;

typedef struct HrtfParams {
    ALfloat Gain;
    ALfloat Dir[3];
    ALfloat Coeffs[MAXCHANNELS][HRIR_LENGTH][2];
    ALuint Delay[MAXCHANNELS][2];
    ALfloat CoeffStep[HRIR_LENGTH][2];
    ALint DelayStep[2];
} HrtfParams;

typedef struct DirectParams {
    HrtfParams Hrtf;

    /* A mixing matrix. First subscript is the channel number of the input data
     * (regardless of channel configuration) and the second is the channel
     * target (eg. FRONT_LEFT). Not used with HRTF. */
    ALfloat Gains[MAXCHANNELS][MAXCHANNELS];

    /* A low-pass filter, using 2 chained one-pole filters. */
    FILTER iirFilter;
    ALfloat history[MAXCHANNELS*2];
} DirectParams;

typedef struct SendParams {
    /* Gain control, which applies to all input channels to a single (mono)
     * output buffer. */
    ALfloat Gain;

    /* A low-pass filter, using a one-pole filter. */
    FILTER iirFilter;
    ALfloat history[MAXCHANNELS];
} SendParams;


typedef struct ALsource
{
    /** Source properties. */
    volatile ALfloat   Pitch;
    volatile ALfloat   Gain;
    volatile ALfloat   OuterGain;
    volatile ALfloat   MinGain;
    volatile ALfloat   MaxGain;
    volatile ALfloat   InnerAngle;
    volatile ALfloat   OuterAngle;
    volatile ALfloat   RefDistance;
    volatile ALfloat   MaxDistance;
    volatile ALfloat   RollOffFactor;
    volatile ALfloat   Position[3];
    volatile ALfloat   Velocity[3];
    volatile ALfloat   Orientation[3];
    volatile ALboolean HeadRelative;
    volatile ALboolean Looping;
    volatile enum DistanceModel DistanceModel;
    volatile ALboolean DirectChannels;

    volatile ALboolean DryGainHFAuto;
    volatile ALboolean WetGainAuto;
    volatile ALboolean WetGainHFAuto;
    volatile ALfloat   OuterGainHF;

    volatile ALfloat AirAbsorptionFactor;
    volatile ALfloat RoomRolloffFactor;
    volatile ALfloat DopplerFactor;

    enum Resampler Resampler;

    /**
     * Last user-specified offset, and the offset type (bytes, samples, or
     * seconds).
     */
    ALdouble Offset;
    ALenum   OffsetType;

    /** Source type (static, streaming, or undetermined) */
    volatile ALint SourceType;

    /** Source state (initial, playing, paused, or stopped) */
    volatile ALenum state;
    ALenum new_state;

    /**
     * Source offset in samples, relative to the currently playing buffer, NOT
     * the whole queue, and the fractional (fixed-point) offset to the next
     * sample.
     */
    ALuint position;
    ALuint position_fraction;

    /** Source Buffer Queue info. */
    ALbufferlistitem *queue;
    ALuint BuffersInQueue;
    ALuint BuffersPlayed;

    /** Current buffer sample info. */
    ALuint NumChannels;
    ALuint SampleSize;

    /** Direct filter and auxiliary send info. */
    ALfloat DirectGain;
    ALfloat DirectGainHF;

    struct {
        struct ALeffectslot *Slot;
        ALfloat Gain;
        ALfloat GainHF;
    } Send[MAX_SENDS];

    /** HRTF info. */
    HrtfState Hrtf;

    /** Current target parameters used for mixing. */
    struct {
        DryMixerFunc DryMix;
        WetMixerFunc WetMix;

        ALint Step;

        DirectParams Direct;

        struct ALeffectslot *Slot[MAX_SENDS];
        SendParams Send[MAX_SENDS];
    } Params;
    /** Source needs to update its mixing parameters. */
    volatile ALenum NeedsUpdate;

    /** Method to update mixing parameters. */
    ALvoid (*Update)(struct ALsource *self, const ALCcontext *context);

    /** Self ID */
    ALuint id;
} ALsource;
#define ALsource_Update(s,a)                 ((s)->Update(s,a))

ALvoid SetSourceState(ALsource *Source, ALCcontext *Context, ALenum state);
ALboolean ApplyOffset(ALsource *Source);

ALvoid ReleaseALSources(ALCcontext *Context);

#ifdef __cplusplus
}
#endif

#endif
