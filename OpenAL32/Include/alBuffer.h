#ifndef _AL_BUFFER_H_
#define _AL_BUFFER_H_

#include "AL/al.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUFFER_PADDING 2

typedef struct ALbuffer
{
    ALvoid  *data;
    ALsizei  size;

    ALenum   format;
    ALenum   eOriginalFormat;
    ALsizei  frequency;

    ALsizei  OriginalSize;
    ALsizei  OriginalAlign;

    ALsizei  LoopStart;
    ALsizei  LoopEnd;

    ALuint   refcount; // Number of sources using this buffer (deletion can only occur when this is 0)

    // Index to itself
    ALuint buffer;
} ALbuffer;

ALvoid ReleaseALBuffers(ALCdevice *device);

#ifdef __cplusplus
}
#endif

#endif
