#ifndef _ALU_H_
#define _ALU_H_

#define BUFFERSIZE 48000
#define FRACTIONBITS 14
#define FRACTIONMASK ((1L<<FRACTIONBITS)-1)
#define MAX_PITCH 4
#define OUTPUTCHANNELS 6

#include "AL/al.h"
#include "AL/alc.h"

#ifdef __cplusplus
extern "C" {
#endif

__inline ALuint aluBytesFromFormat(ALenum format);
__inline ALuint aluChannelsFromFormat(ALenum format);
ALvoid aluMixData(ALCcontext *context,ALvoid *buffer,ALsizei size,ALenum format);

#ifdef __cplusplus
}
#endif

#endif

