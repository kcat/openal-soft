#ifndef _AL_REVERB_H_
#define _AL_REVERB_H_

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alAuxEffectSlot.h"
#include "alEffect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ALverbState ALverbState;

ALverbState *VerbCreate(ALCcontext *Context);
ALvoid VerbDestroy(ALverbState *State);
ALvoid VerbUpdate(ALCcontext *Context, struct ALeffectslot *Slot, ALeffect *Effect);
ALvoid VerbProcess(ALverbState *State, ALuint SamplesToDo, const ALfloat *SamplesIn, ALfloat (*SamplesOut)[OUTPUTCHANNELS]);

#ifdef __cplusplus
}
#endif

#endif

