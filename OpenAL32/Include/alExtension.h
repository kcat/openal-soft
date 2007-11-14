#ifndef _AL_EXTENSION_H_
#define _AL_EXTENSION_H_

#include "AL/al.h"
#include "AL/alc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ALextension_struct
{
	ALchar		*extName;
	ALvoid		*address;
} ALextension;

typedef struct ALfunction_struct
{
	ALchar		*funcName;
	ALvoid		*address;
} ALfunction;

typedef struct ALenum_struct
{
	ALchar		*enumName;
	ALenum		value;
} ALenums;

#ifdef __cplusplus
}
#endif

#endif
