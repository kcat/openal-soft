#ifndef _AL_EFFECT_H_
#define _AL_EFFECT_H_

#include "alMain.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EAXREVERB = 0,
    REVERB,
    ECHO,
    MODULATOR,
    DEDICATED,
    CHORUS,
    FLANGER,

    MAX_EFFECTS
};
extern ALboolean DisabledEffects[MAX_EFFECTS];

extern ALfloat ReverbBoost;
extern ALboolean EmulateEAXReverb;

typedef struct ALeffect
{
    // Effect type (AL_EFFECT_NULL, ...)
    ALenum type;

    struct {
        // Shared Reverb Properties
        ALfloat Density;
        ALfloat Diffusion;
        ALfloat Gain;
        ALfloat GainHF;
        ALfloat DecayTime;
        ALfloat DecayHFRatio;
        ALfloat ReflectionsGain;
        ALfloat ReflectionsDelay;
        ALfloat LateReverbGain;
        ALfloat LateReverbDelay;
        ALfloat AirAbsorptionGainHF;
        ALfloat RoomRolloffFactor;
        ALboolean DecayHFLimit;

        // Additional EAX Reverb Properties
        ALfloat GainLF;
        ALfloat DecayLFRatio;
        ALfloat ReflectionsPan[3];
        ALfloat LateReverbPan[3];
        ALfloat EchoTime;
        ALfloat EchoDepth;
        ALfloat ModulationTime;
        ALfloat ModulationDepth;
        ALfloat HFReference;
        ALfloat LFReference;
    } Reverb;

    struct {
        ALfloat Delay;
        ALfloat LRDelay;

        ALfloat Damping;
        ALfloat Feedback;

        ALfloat Spread;
    } Echo;

    struct {
        ALfloat Frequency;
        ALfloat HighPassCutoff;
        ALint Waveform;
    } Modulator;

    struct {
        ALfloat Gain;
    } Dedicated;

    struct {
        ALint Waveform;
        ALint Phase;
        ALfloat Rate;
        ALfloat Depth;
        ALfloat Feedback;
        ALfloat Delay;
    } Chorus;

    struct {
        ALint Waveform;
        ALint Phase;
        ALfloat Rate;
        ALfloat Depth;
        ALfloat Feedback;
        ALfloat Delay;
    } Flanger;

    void (*SetParami)(struct ALeffect *effect, ALCcontext *context, ALenum param, ALint val);
    void (*SetParamiv)(struct ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals);
    void (*SetParamf)(struct ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val);
    void (*SetParamfv)(struct ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals);

    void (*GetParami)(struct ALeffect *effect, ALCcontext *context, ALenum param, ALint *val);
    void (*GetParamiv)(struct ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals);
    void (*GetParamf)(struct ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val);
    void (*GetParamfv)(struct ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals);

    /* Self ID */
    ALuint id;
} ALeffect;

#define ALeffect_SetParami(x, c, p, v)  ((x)->SetParami((x),(c),(p),(v)))
#define ALeffect_SetParamiv(x, c, p, v) ((x)->SetParamiv((x),(c),(p),(v)))
#define ALeffect_SetParamf(x, c, p, v)  ((x)->SetParamf((x),(c),(p),(v)))
#define ALeffect_SetParamfv(x, c, p, v) ((x)->SetParamfv((x),(c),(p),(v)))

#define ALeffect_GetParami(x, c, p, v)  ((x)->GetParami((x),(c),(p),(v)))
#define ALeffect_GetParamiv(x, c, p, v) ((x)->GetParamiv((x),(c),(p),(v)))
#define ALeffect_GetParamf(x, c, p, v)  ((x)->GetParamf((x),(c),(p),(v)))
#define ALeffect_GetParamfv(x, c, p, v) ((x)->GetParamfv((x),(c),(p),(v)))

static __inline ALboolean IsReverbEffect(ALenum type)
{ return type == AL_EFFECT_REVERB || type == AL_EFFECT_EAXREVERB; }

void eaxreverb_SetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val);
void eaxreverb_SetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals);
void eaxreverb_SetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val);
void eaxreverb_SetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals);
void eaxreverb_GetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint *val);
void eaxreverb_GetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals);
void eaxreverb_GetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val);
void eaxreverb_GetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals);

void reverb_SetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val);
void reverb_SetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals);
void reverb_SetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val);
void reverb_SetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals);
void reverb_GetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint *val);
void reverb_GetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals);
void reverb_GetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val);
void reverb_GetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals);

void chorus_SetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val);
void chorus_SetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals);
void chorus_SetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val);
void chorus_SetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals);
void chorus_GetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint *val);
void chorus_GetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals);
void chorus_GetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val);
void chorus_GetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals);

void echo_SetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val);
void echo_SetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals);
void echo_SetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val);
void echo_SetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals);
void echo_GetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint *val);
void echo_GetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals);
void echo_GetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val);
void echo_GetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals);

void flanger_SetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val);
void flanger_SetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals);
void flanger_SetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val);
void flanger_SetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals);
void flanger_GetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint *val);
void flanger_GetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals);
void flanger_GetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val);
void flanger_GetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals);

void mod_SetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val);
void mod_SetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals);
void mod_SetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val);
void mod_SetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals);
void mod_GetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint *val);
void mod_GetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals);
void mod_GetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val);
void mod_GetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals);

void ded_SetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val);
void ded_SetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals);
void ded_SetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val);
void ded_SetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals);
void ded_GetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint *val);
void ded_GetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals);
void ded_GetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val);
void ded_GetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals);

void null_SetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint val);
void null_SetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, const ALint *vals);
void null_SetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat val);
void null_SetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, const ALfloat *vals);
void null_GetParami(ALeffect *effect, ALCcontext *context, ALenum param, ALint *val);
void null_GetParamiv(ALeffect *effect, ALCcontext *context, ALenum param, ALint *vals);
void null_GetParamf(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *val);
void null_GetParamfv(ALeffect *effect, ALCcontext *context, ALenum param, ALfloat *vals);

ALenum InitEffect(ALeffect *effect);
ALvoid ReleaseALEffects(ALCdevice *device);

ALvoid LoadReverbPreset(const char *name, ALeffect *effect);

#ifdef __cplusplus
}
#endif

#endif
