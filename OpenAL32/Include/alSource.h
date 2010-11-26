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

static const ALsizei ResamplerPadding[RESAMPLER_MAX] = {
    0, /* Point */
    1, /* Linear */
    2, /* Cubic */
};
static const ALsizei ResamplerPrePadding[RESAMPLER_MAX] = {
    0, /* Point */
    0, /* Linear */
    1, /* Cubic */
};

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

    struct ALbufferlistitem *queue; // Linked list of buffers in queue
    ALuint       BuffersInQueue;    // Number of buffers in queue
    ALuint       BuffersPlayed;     // Number of buffers played on this loop

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

    // Current target parameters used for mixing
    ALboolean NeedsUpdate;
    struct {
        ALint Step;

        ALfloat DryGains[OUTPUTCHANNELS];
        FILTER iirFilter;
        ALfloat history[OUTPUTCHANNELS*2];

        struct {
            ALfloat WetGain;
            FILTER iirFilter;
            ALfloat history[OUTPUTCHANNELS];
        } Send[MAX_SENDS];
    } Params;

    ALvoid (*Update)(struct ALsource *self, const ALCcontext *context);

    // Index to itself
    ALuint source;
} ALsource;
#define ALsource_Update(s,a)  ((s)->Update(s,a))

ALvoid ReleaseALSources(ALCcontext *Context);

#ifdef __cplusplus
}
#endif

#endif
