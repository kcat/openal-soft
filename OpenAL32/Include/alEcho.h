#ifndef AL_ECHO_H
#define AL_ECHO_H

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alEffect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ALechoState ALechoState;

ALechoState *EchoCreate(ALCcontext *Context);
ALvoid EchoDestroy(ALechoState *State);
ALvoid EchoUpdate(ALCcontext *Context, struct ALeffectslot *Slot, ALeffect *Effect);
ALvoid EchoProcess(ALechoState *State, ALuint SamplesToDo, const ALfloat *SamplesIn, ALfloat (*SamplesOut)[OUTPUTCHANNELS]);

#ifdef __cplusplus
}
#endif

#endif
