#ifndef _AL_SOURCE_H_
#define _AL_SOURCE_H_

#define AL_NUM_SOURCE_PARAMS    128

/* This cannot be changed without working on the code! */
#define MAX_SENDS                 1

#include "alFilter.h"
#include "alu.h"
#include "AL/al.h"

#define AL_DIRECT_FILTER                                   0x20005
#define AL_AUXILIARY_SEND_FILTER                           0x20006
#define AL_AIR_ABSORPTION_FACTOR                           0x20007
#define AL_ROOM_ROLLOFF_FACTOR                             0x20008
#define AL_CONE_OUTER_GAINHF                               0x20009
#define AL_DIRECT_FILTER_GAINHF_AUTO                       0x2000A
#define AL_AUXILIARY_SEND_FILTER_GAIN_AUTO                 0x2000B
#define AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO               0x2000C

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ALbufferlistitem
{
    ALuint                   buffer;
    ALuint                   bufferstate;
    ALuint                   flag;
    struct ALbufferlistitem *next;
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

    ALuint       ulBufferID;

    ALboolean    inuse;
    ALboolean    play;
    ALenum       state;
    ALuint       position;
    ALuint       position_fraction;

    struct ALbufferlistitem *queue; // Linked list of buffers in queue
    ALuint       BuffersInQueue;    // Number of buffers in queue

    ALuint TotalBufferDataSize; // Total amount of data contained in the buffers queued for this source
    ALuint BuffersPlayed;       // Number of buffers played on this loop

    ALfilter DirectFilter;

    struct {
        struct ALeffectslot *Slot;
        ALfilter WetFilter;
        FILTER iirFilter;
    } Send[MAX_SENDS];

    ALboolean DryGainHFAuto;
    ALboolean WetGainAuto;
    ALboolean WetGainHFAuto;
    ALfloat   OuterGainHF;

    FILTER iirFilter;

    ALfloat AirAbsorptionFactor;

    ALfloat RoomRolloffFactor;

    ALfloat DopplerFactor;

    // Index to itself
    ALuint source;

    ALint  lOffset;
    ALint  lOffsetType;

    // Source Type (Static, Streaming, or Undetermined)
    ALint  lSourceType;

    // Current gains, which are ramped while mixed
    ALfloat DryGains[OUTPUTCHANNELS];
    ALfloat WetGain;
    ALboolean FirstStart;

    struct ALsource *next;
} ALsource;

ALvoid ReleaseALSources(ALCcontext *Context);

#ifdef __cplusplus
}
#endif

#endif
