#ifndef _AL_SOURCE_H_
#define _AL_SOURCE_H_

#define MAX_SENDS                 4

#include "alFilter.h"
#include "alu.h"
#include "AL/al.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POINT_RESAMPLER = 0,
    LINEAR_RESAMPLER,
    CUBIC_RESAMPLER,

    RESAMPLER_MAX,
    RESAMPLER_MIN = -1,
    RESAMPLER_DEFAULT = LINEAR_RESAMPLER
} resampler_t;
extern resampler_t DefaultResampler;

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
    ALenum       DistanceModel;

    resampler_t  Resampler;

    ALenum       state;
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
    ALfloat HrtfHistory[MAXCHANNELS][HRTF_LENGTH];
    ALuint HrtfOffset;

    // Current target parameters used for mixing
    ALboolean NeedsUpdate;
    struct {
        ALint Step;

        ALfloat HrtfCoeffs[MAXCHANNELS][HRTF_LENGTH][2];

        /* A mixing matrix. First subscript is the channel number of the input
         * data (regardless of channel configuration) and the second is the
         * channel target (eg. FRONT_LEFT) */
        ALfloat DryGains[MAXCHANNELS][MAXCHANNELS];
        FILTER iirFilter;
        ALfloat history[MAXCHANNELS*2];

        struct {
            ALfloat WetGain;
            FILTER iirFilter;
            ALfloat history[MAXCHANNELS];
        } Send[MAX_SENDS];
    } Params;

    ALvoid (*Update)(struct ALsource *self, const ALCcontext *context);
    ALvoid (*DoMix)(struct ALsource *self, ALCdevice *Device,
                    const ALvoid *RESTRICT data,
                    ALuint *DataPosInt, ALuint *DataPosFrac,
                    ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize);
    ALvoid (*DoHrtfMix)(struct ALsource *self, ALCdevice *Device,
                        const ALvoid *RESTRICT data,
                        ALuint *DataPosInt, ALuint *DataPosFrac,
                        ALuint OutPos, ALuint SamplesToDo, ALuint BufferSize);

    // Index to itself
    ALuint source;
} ALsource;
#define ALsource_Update(s,a)                 ((s)->Update(s,a))
#define ALsource_DoMix(s,a,b,c,d,e,f,g)      ((s)->DoMix(s,a,b,c,d,e,f,g))
#define ALsource_DoHrtfMix(s,a,b,c,d,e,f,g)  ((s)->DoHrtfMix(s,a,b,c,d,e,f,g))

ALvoid ReleaseALSources(ALCcontext *Context);

#ifdef __cplusplus
}
#endif

#endif
