#ifndef _AL_AUXEFFECTSLOT_H_
#define _AL_AUXEFFECTSLOT_H_

#include "AL/al.h"
#include "alEffect.h"
#include "alFilter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ALeffectState ALeffectState;

typedef struct ALeffectslot
{
    ALeffect effect;

    ALfloat Gain;
    ALboolean AuxSendAuto;

    ALeffectState *EffectState;

    ALfloat WetBuffer[BUFFERSIZE];

    ALuint refcount;

    // Index to itself
    ALuint effectslot;

    struct ALeffectslot *next;
} ALeffectslot;

ALvoid AL_APIENTRY alGenAuxiliaryEffectSlots(ALsizei n, ALuint *effectslots);
ALvoid AL_APIENTRY alDeleteAuxiliaryEffectSlots(ALsizei n, ALuint *effectslots);
ALboolean AL_APIENTRY alIsAuxiliaryEffectSlot(ALuint effectslot);

ALvoid AL_APIENTRY alAuxiliaryEffectSloti(ALuint effectslot, ALenum param, ALint iValue);
ALvoid AL_APIENTRY alAuxiliaryEffectSlotiv(ALuint effectslot, ALenum param, ALint *piValues);
ALvoid AL_APIENTRY alAuxiliaryEffectSlotf(ALuint effectslot, ALenum param, ALfloat flValue);
ALvoid AL_APIENTRY alAuxiliaryEffectSlotfv(ALuint effectslot, ALenum param, ALfloat *pflValues);

ALvoid AL_APIENTRY alGetAuxiliaryEffectSloti(ALuint effectslot, ALenum param, ALint *piValue);
ALvoid AL_APIENTRY alGetAuxiliaryEffectSlotiv(ALuint effectslot, ALenum param, ALint *piValues);
ALvoid AL_APIENTRY alGetAuxiliaryEffectSlotf(ALuint effectslot, ALenum param, ALfloat *pflValue);
ALvoid AL_APIENTRY alGetAuxiliaryEffectSlotfv(ALuint effectslot, ALenum param, ALfloat *pflValues);

ALvoid ReleaseALAuxiliaryEffectSlots(ALCcontext *Context);


struct ALeffectState {
    ALvoid (*Destroy)(ALeffectState *State);
    ALboolean (*DeviceUpdate)(ALeffectState *State, ALCdevice *Device);
    ALvoid (*Update)(ALeffectState *State, ALCcontext *Context, const ALeffect *Effect);
    ALvoid (*Process)(ALeffectState *State, const ALeffectslot *Slot, ALuint SamplesToDo, const ALfloat *SamplesIn, ALfloat (*SamplesOut)[OUTPUTCHANNELS]);
};

ALeffectState *NoneCreate(void);
ALeffectState *EAXVerbCreate(void);
ALeffectState *VerbCreate(void);
ALeffectState *EchoCreate(void);

#define ALEffect_Destroy(a)         ((a)->Destroy((a)))
#define ALEffect_DeviceUpdate(a,b)  ((a)->DeviceUpdate((a),(b)))
#define ALEffect_Update(a,b,c)      ((a)->Update((a),(b),(c)))
#define ALEffect_Process(a,b,c,d,e) ((a)->Process((a),(b),(c),(d),(e)))


#ifdef __cplusplus
}
#endif

#endif
