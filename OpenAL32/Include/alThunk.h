#ifndef _AL_THUNK_H_
#define _AL_THUNK_H_

#include "config.h"

#include "AL/al.h"
#include "AL/alc.h"

#ifdef __cplusplus
extern "C" {
#endif

void alThunkInit(void);
void alThunkExit(void);
ALenum alThunkNewEntry(ALuint *idx);
void alThunkRemoveEntry(ALuint index);

#if (SIZEOF_VOIDP > SIZEOF_UINT)

#define ALTHUNK_INIT()          alThunkInit()
#define ALTHUNK_EXIT()          alThunkExit()
#define ALTHUNK_NEWENTRY(p,i)   alThunkNewEntry(i)
#define ALTHUNK_REMOVEENTRY(i)  alThunkRemoveEntry(i)

#else

#define ALTHUNK_INIT()
#define ALTHUNK_EXIT()
#define ALTHUNK_NEWENTRY(p,i)   ((*(i) = (ALuint)p),AL_NO_ERROR)
#define ALTHUNK_REMOVEENTRY(i)  ((ALvoid)i)

#endif // (SIZEOF_VOIDP > SIZEOF_INT)

#ifdef __cplusplus
}
#endif

#endif //_AL_THUNK_H_

