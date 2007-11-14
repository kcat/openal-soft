#ifndef _AL_SOURCE_H_
#define _AL_SOURCE_H_

#define AL_NUM_SOURCE_PARAMS    128

#include "AL/al.h"

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

typedef struct ALsource_struct
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
    ALuint       BuffersProcessed;  // Number of buffers already processed (played)

    ALuint TotalBufferDataSize; // Total amount of data contained in the buffers queued for this source
    ALuint BuffersPlayed;       // Number of buffers played on this loop
    ALuint BufferPosition;      // Read position in audio data of current buffer

    // Index to itself
    ALuint source;

    ALint  lBytesPlayed;

    ALint  lOffset;
    ALint  lOffsetType;

    // Source Type (Static, Streaming, or Undetermined)
    ALint  lSourceType;

    struct ALsource_struct *next;
} ALsource;

#ifdef __cplusplus
}
#endif

#endif
