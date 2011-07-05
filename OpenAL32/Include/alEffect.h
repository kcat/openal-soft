#ifndef _AL_EFFECT_H_
#define _AL_EFFECT_H_

#include "AL/al.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EAXREVERB = 0,
    REVERB,
    ECHO,
    MODULATOR,
    DEDICATED,

    MAX_EFFECTS
};
extern ALboolean DisabledEffects[MAX_EFFECTS];

typedef struct ALeffect
{
    // Effect type (AL_EFFECT_NULL, ...)
    ALenum type;

    union {
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
    } Params;

    // Index to itself
    ALuint effect;
} ALeffect;

static __inline ALboolean IsReverbEffect(ALenum type)
{ return type == AL_EFFECT_REVERB || type == AL_EFFECT_EAXREVERB; }

ALvoid ReleaseALEffects(ALCdevice *device);

#ifdef __cplusplus
}
#endif

#endif
