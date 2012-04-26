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
        ALfloat WetGain;
        ALfloat WetGainHF;
    } Send[MAX_SENDS];

    /** HRTF info. */
    ALboolean HrtfMoving;
    ALuint HrtfCounter;
    ALfloat HrtfHistory[MAXCHANNELS][SRC_HISTORY_LENGTH];
    ALfloat HrtfValues[MAXCHANNELS][HRIR_LENGTH][2];
    ALuint HrtfOffset;

    /** Current target parameters used for mixing. */
    struct {
        MixerFunc DoMix;

        ALint Step;

        ALfloat HrtfGain;
        ALfloat HrtfDir[3];
        ALfloat HrtfCoeffs[MAXCHANNELS][HRIR_LENGTH][2];
        ALuint HrtfDelay[MAXCHANNELS][2];
        ALfloat HrtfCoeffStep[HRIR_LENGTH][2];
        ALint HrtfDelayStep[2];

        /* A mixing matrix. First subscript is the channel number of the input
         * data (regardless of channel configuration) and the second is the
         * channel target (eg. FRONT_LEFT). */
        ALfloat DryGains[MAXCHANNELS][MAXCHANNELS];

        FILTER iirFilter;
        ALfloat history[MAXCHANNELS*2];

        struct {
            struct ALeffectslot *Slot;
            ALfloat WetGain;
            FILTER iirFilter;
            ALfloat history[MAXCHANNELS];
        } Send[MAX_SENDS];
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
