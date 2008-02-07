#ifndef _ALU_H_
#define _ALU_H_

#include "AL/al.h"
#include "AL/alc.h"

#ifdef __cplusplus
extern "C" {
#endif

extern ALboolean DuplicateStereo;

__inline ALuint aluBytesFromFormat(ALenum format);
__inline ALuint aluChannelsFromFormat(ALenum format);
ALvoid aluMixData(ALCcontext *context,ALvoid *buffer,ALsizei size,ALenum format);

#ifdef __cplusplus
}
#endif

#endif

