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

AL_API ALvoid AL_APIENTRY alGenDatabuffersEXT(ALsizei n,ALuint *puiBuffers);
AL_API ALvoid AL_APIENTRY alDeleteDatabuffersEXT(ALsizei n, const ALuint *puiBuffers);
AL_API ALboolean AL_APIENTRY alIsDatabufferEXT(ALuint uiBuffer);

AL_API ALvoid AL_APIENTRY alDatabufferDataEXT(ALuint buffer,const ALvoid *data,ALsizeiptrEXT size,ALenum usage);
AL_API ALvoid AL_APIENTRY alDatabufferSubDataEXT(ALuint buffer, ALintptrEXT start, ALsizeiptrEXT length, const ALvoid *data);
AL_API ALvoid AL_APIENTRY alGetDatabufferSubDataEXT(ALuint buffer, ALintptrEXT start, ALsizeiptrEXT length, ALvoid *data);

AL_API ALvoid AL_APIENTRY alDatabufferfEXT(ALuint buffer, ALenum eParam, ALfloat flValue);
AL_API ALvoid AL_APIENTRY alDatabufferfvEXT(ALuint buffer, ALenum eParam, const ALfloat* flValues);
AL_API ALvoid AL_APIENTRY alDatabufferiEXT(ALuint buffer, ALenum eParam, ALint lValue);
AL_API ALvoid AL_APIENTRY alDatabufferivEXT(ALuint buffer, ALenum eParam, const ALint* plValues);
AL_API ALvoid AL_APIENTRY alGetDatabufferfEXT(ALuint buffer, ALenum eParam, ALfloat *pflValue);
AL_API ALvoid AL_APIENTRY alGetDatabufferfvEXT(ALuint buffer, ALenum eParam, ALfloat* pflValues);
AL_API ALvoid AL_APIENTRY alGetDatabufferiEXT(ALuint buffer, ALenum eParam, ALint *plValue);
AL_API ALvoid AL_APIENTRY alGetDatabufferivEXT(ALuint buffer, ALenum eParam, ALint* plValues);

AL_API ALvoid AL_APIENTRY alSelectDatabufferEXT(ALenum target, ALuint uiBuffer);

AL_API ALvoid* AL_APIENTRY alMapDatabufferEXT(ALuint uiBuffer, ALintptrEXT start, ALsizeiptrEXT length, ALenum access);
AL_API ALvoid AL_APIENTRY alUnmapDatabufferEXT(ALuint uiBuffer);

ALvoid ReleaseALDatabuffers(ALCdevice *device);

#ifdef __cplusplus
}
#endif

#endif
