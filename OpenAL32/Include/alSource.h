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

extern const ALsizei ResamplerPadding[RESAMPLER_MAX];
extern const ALsizei ResamplerPrePadding[RESAMPLER_MAX];


typedef struct ALbufferlistitem
{
    struct ALbuffer         *buffer;
    struct ALbufferlistitem *next;
    struct ALbufferlistitem *prev;
} ALbufferlistitem;

typedef struct ALsource
{
    ALfloat      flPitch;
    ALfloat      flGain;
    ALfloat      flOuterGain;
    ALfloat      flMinGain;
    ALfloat      flMaxGain;
    ALfloat      flInnerAngle;
    ALfloat      flOuterAngle;
    ALfloat      flRefDistance;
    ALfloat      flMaxDistance;
    ALfloat      flRollOffFactor;
    ALfloat      vPosition[3];
    ALfloat      vVelocity[3];
    ALfloat      vOrientation[3];
    ALboolean    bHeadRelative;
    ALboolean    bLooping;
    enum DistanceModel DistanceModel;
    ALboolean    VirtualChannels;

    enum Resampler Resampler;

    ALenum       state;
    ALenum       new_state;
    ALuint       position;
    ALuint       position_fraction;

    struct ALbuffer *Buffer;

    ALbufferlistitem *queue; // Linked list of buffers in queue
    ALuint BuffersInQueue;   // Number of buffers in queue
    ALuint BuffersPlayed;    // Number of buffers played on this loop

    ALfilter DirectFilter;

    struct {
        struct ALeffectslot *Slot;
        ALfilter WetFilter;
    } Send[MAX_SENDS];

    ALboolean DryGainHFAuto;
    ALboolean WetGainAuto;
    ALboolean WetGainHFAuto;
    ALfloat   OuterGainHF;

    ALfloat AirAbsorptionFactor;
    ALfloat RoomRolloffFactor;
    ALfloat DopplerFactor;

    ALint  lOffset;
    ALint  lOffsetType;

    // Source Type (Static, Streaming, or Undetermined)
    ALint  lSourceType;

    ALuint NumChannels;
    ALuint SampleSize;

    /* HRTF info */
    ALboolean HrtfMoving;
    ALuint HrtfCounter;
    ALfloat HrtfHistory[MAXCHANNELS][SRC_HISTORY_LENGTH];
    ALfloat HrtfValues[MAXCHANNELS][HRIR_LENGTH][2];
    ALuint HrtfOffset;

    /* Current target parameters used for mixing */
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
         * channel target (eg. FRONT_LEFT) */
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
    ALboolean NeedsUpdate;

    ALvoid (*Update)(struct ALsource *self, const ALCcontext *context);

    // Index to itself
    ALuint source;
} ALsource;
#define ALsource_Update(s,a)                 ((s)->Update(s,a))

ALvoid SetSourceState(ALsource *Source, ALCcontext *Context, ALenum state);
ALvoid ReleaseALSources(ALCcontext *Context);

#ifdef __cplusplus
}
#endif

#endif
