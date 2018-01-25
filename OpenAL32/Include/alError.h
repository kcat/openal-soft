#ifndef _AL_ERROR_H_
#define _AL_ERROR_H_

#include "alMain.h"

#ifdef __cplusplus
extern "C" {
#endif

extern ALboolean TrapALError;

ALvoid alSetError(ALCcontext *context, ALenum errorCode, ALuint objid, const char *msg);

#define SETERR_GOTO(ctx, err, objid, msg, lbl) do {                            \
    alSetError((ctx), (err), (objid), (msg));                                  \
    goto lbl;                                                                  \
} while(0)

#define SETERR_RETURN(ctx, err, objid, msg, retval) do {                       \
    alSetError((ctx), (err), (objid), (msg));                                  \
    return retval;                                                             \
} while(0)

#ifdef __cplusplus
}
#endif

#endif
