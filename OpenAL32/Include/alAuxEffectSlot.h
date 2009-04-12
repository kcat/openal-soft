#ifndef _AL_AUXEFFECTSLOT_H_
#define _AL_AUXEFFECTSLOT_H_

#include "AL/al.h"
#include "alEffect.h"
#include "alFilter.h"
#include "alReverb.h"
#include "alEcho.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AL_EFFECTSLOT_EFFECT                               0x0001
#define AL_EFFECTSLOT_GAIN                                 0x0002
#define AL_EFFECTSLOT_AUXILIARY_SEND_AUTO                  0x0003

#define AL_EFFECTSLOT_NULL                                 0x0000

typedef struct ALeffectslot
{
    ALeffect effect;

    ALfloat Gain;
    ALboolean AuxSendAuto;

    ALverbState *ReverbState;
    ALechoState *EchoState;

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

#ifdef __cplusplus
}
#endif

#endif
