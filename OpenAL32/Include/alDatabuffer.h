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

ALvoid ALAPIENTRY alGenDatabuffersEXT(ALsizei n,ALuint *puiBuffers);
ALvoid ALAPIENTRY alDeleteDatabuffersEXT(ALsizei n, const ALuint *puiBuffers);
ALboolean ALAPIENTRY alIsDatabufferEXT(ALuint uiBuffer);

ALvoid ALAPIENTRY alDatabufferDataEXT(ALuint buffer,const ALvoid *data,ALsizeiptrEXT size,ALenum usage);
ALvoid ALAPIENTRY alDatabufferSubDataEXT(ALuint buffer, ALintptrEXT start, ALsizeiptrEXT length, const ALvoid *data);
ALvoid ALAPIENTRY alGetDatabufferSubDataEXT(ALuint buffer, ALintptrEXT start, ALsizeiptrEXT length, ALvoid *data);

ALvoid ALAPIENTRY alDatabufferfEXT(ALuint buffer, ALenum eParam, ALfloat flValue);
ALvoid ALAPIENTRY alDatabufferfvEXT(ALuint buffer, ALenum eParam, const ALfloat* flValues);
ALvoid ALAPIENTRY alDatabufferiEXT(ALuint buffer, ALenum eParam, ALint lValue);
ALvoid ALAPIENTRY alDatabufferivEXT(ALuint buffer, ALenum eParam, const ALint* plValues);
ALvoid ALAPIENTRY alGetDatabufferfEXT(ALuint buffer, ALenum eParam, ALfloat *pflValue);
ALvoid ALAPIENTRY alGetDatabufferfvEXT(ALuint buffer, ALenum eParam, ALfloat* pflValues);
ALvoid ALAPIENTRY alGetDatabufferiEXT(ALuint buffer, ALenum eParam, ALint *plValue);
ALvoid ALAPIENTRY alGetDatabufferivEXT(ALuint buffer, ALenum eParam, ALint* plValues);

ALvoid ALAPIENTRY alSelectDatabufferEXT(ALenum target, ALuint uiBuffer);

ALvoid* ALAPIENTRY alMapDatabufferEXT(ALuint uiBuffer, ALintptrEXT start, ALsizeiptrEXT length, ALenum access);
ALvoid ALAPIENTRY alUnmapDatabufferEXT(ALuint uiBuffer);

ALvoid ReleaseALDatabuffers(ALCdevice *device);

#ifdef __cplusplus
}
#endif

#endif
