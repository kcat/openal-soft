#ifndef _AL_BUFFER_H_
#define _AL_BUFFER_H_

#include "AL/al.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UNUSED    0
#define PENDING   1
#define PROCESSED 2

typedef struct ALbuffer_struct 
{
    ALenum   format;
    ALenum   eOriginalFormat;
    ALshort *data;
    ALsizei  size;
    ALsizei  frequency;
    ALsizei  padding;
    ALenum   state;
    ALuint   refcount; // Number of sources using this buffer (deletion can only occur when this is 0)
    struct ALbuffer_struct *next;
} ALbuffer;

ALvoid ReleaseALBuffers(ALvoid);

#ifdef __cplusplus
}
#endif

#endif
