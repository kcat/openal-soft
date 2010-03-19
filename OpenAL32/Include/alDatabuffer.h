#ifndef _AL_DATABUFFER_H_
#define _AL_DATABUFFER_H_

#include "AL/al.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UNMAPPED 0
#define MAPPED   1

typedef struct ALdatabuffer
{
    ALubyte     *data;
    ALintptrEXT size;

    ALenum state;
    ALenum usage;

    /* Index to self */
    ALuint databuffer;

    struct ALdatabuffer *next;
} ALdatabuffer;

ALvoid AL_APIENTRY alGenDatabuffersEXT(ALsizei n,ALuint *puiBuffers);
ALvoid AL_APIENTRY alDeleteDatabuffersEXT(ALsizei n, const ALuint *puiBuffers);
ALboolean AL_APIENTRY alIsDatabufferEXT(ALuint uiBuffer);

ALvoid AL_APIENTRY alDatabufferDataEXT(ALuint buffer,const ALvoid *data,ALsizeiptrEXT size,ALenum usage);
ALvoid AL_APIENTRY alDatabufferSubDataEXT(ALuint buffer, ALintptrEXT start, ALsizeiptrEXT length, const ALvoid *data);
ALvoid AL_APIENTRY alGetDatabufferSubDataEXT(ALuint buffer, ALintptrEXT start, ALsizeiptrEXT length, ALvoid *data);

ALvoid AL_APIENTRY alDatabufferfEXT(ALuint buffer, ALenum eParam, ALfloat flValue);
ALvoid AL_APIENTRY alDatabufferfvEXT(ALuint buffer, ALenum eParam, const ALfloat* flValues);
ALvoid AL_APIENTRY alDatabufferiEXT(ALuint buffer, ALenum eParam, ALint lValue);
ALvoid AL_APIENTRY alDatabufferivEXT(ALuint buffer, ALenum eParam, const ALint* plValues);
ALvoid AL_APIENTRY alGetDatabufferfEXT(ALuint buffer, ALenum eParam, ALfloat *pflValue);
ALvoid AL_APIENTRY alGetDatabufferfvEXT(ALuint buffer, ALenum eParam, ALfloat* pflValues);
ALvoid AL_APIENTRY alGetDatabufferiEXT(ALuint buffer, ALenum eParam, ALint *plValue);
ALvoid AL_APIENTRY alGetDatabufferivEXT(ALuint buffer, ALenum eParam, ALint* plValues);

ALvoid AL_APIENTRY alSelectDatabufferEXT(ALenum target, ALuint uiBuffer);

ALvoid* AL_APIENTRY alMapDatabufferEXT(ALuint uiBuffer, ALintptrEXT start, ALsizeiptrEXT length, ALenum access);
ALvoid AL_APIENTRY alUnmapDatabufferEXT(ALuint uiBuffer);

ALvoid ReleaseALDatabuffers(ALCdevice *device);

#ifdef __cplusplus
}
#endif

#endif
