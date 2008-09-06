#ifndef _AL_EFFECT_H_
#define _AL_EFFECT_H_

#include "AL/al.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AL_EFFECT_TYPE                                     0x8001

#define AL_EFFECT_NULL                                     0x0000
#define AL_EFFECT_REVERB                                   0x0001
#define AL_EFFECT_CHORUS                                   0x0002
#define AL_EFFECT_DISTORTION                               0x0003
#define AL_EFFECT_ECHO                                     0x0004
#define AL_EFFECT_FLANGER                                  0x0005
#define AL_EFFECT_FREQUENCY_SHIFTER                        0x0006
#define AL_EFFECT_VOCAL_MORPHER                            0x0007
#define AL_EFFECT_PITCH_SHIFTER                            0x0008
#define AL_EFFECT_RING_MODULATOR                           0x0009
#define AL_EFFECT_AUTOWAH                                  0x000A
#define AL_EFFECT_COMPRESSOR                               0x000B
#define AL_EFFECT_EQUALIZER                                0x000C

#define AL_REVERB_DENSITY                                  0x0001
#define AL_REVERB_DIFFUSION                                0x0002
#define AL_REVERB_GAIN                                     0x0003
#define AL_REVERB_GAINHF                                   0x0004
#define AL_REVERB_DECAY_TIME                               0x0005
#define AL_REVERB_DECAY_HFRATIO                            0x0006
#define AL_REVERB_REFLECTIONS_GAIN                         0x0007
#define AL_REVERB_REFLECTIONS_DELAY                        0x0008
#define AL_REVERB_LATE_REVERB_GAIN                         0x0009
#define AL_REVERB_LATE_REVERB_DELAY                        0x000A
#define AL_REVERB_AIR_ABSORPTION_GAINHF                    0x000B
#define AL_REVERB_ROOM_ROLLOFF_FACTOR                      0x000C
#define AL_REVERB_DECAY_HFLIMIT                            0x000D


typedef struct ALeffect_struct
{
    // Effect type (AL_EFFECT_NULL, ...)
    ALenum type;

    struct {
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
    } Reverb;

    // Index to itself
    ALuint effect;

    struct ALeffect_struct *next;
} ALeffect;

ALvoid AL_APIENTRY alGenEffects(ALsizei n, ALuint *effects);
ALvoid AL_APIENTRY alDeleteEffects(ALsizei n, ALuint *effects);
ALboolean AL_APIENTRY alIsEffect(ALuint effect);

ALvoid AL_APIENTRY alEffecti(ALuint effect, ALenum param, ALint iValue);
ALvoid AL_APIENTRY alEffectiv(ALuint effect, ALenum param, ALint *piValues);
ALvoid AL_APIENTRY alEffectf(ALuint effect, ALenum param, ALfloat flValue);
ALvoid AL_APIENTRY alEffectfv(ALuint effect, ALenum param, ALfloat *pflValues);

ALvoid AL_APIENTRY alGetEffecti(ALuint effect, ALenum param, ALint *piValue);
ALvoid AL_APIENTRY alGetEffectiv(ALuint effect, ALenum param, ALint *piValues);
ALvoid AL_APIENTRY alGetEffectf(ALuint effect, ALenum param, ALfloat *pflValue);
ALvoid AL_APIENTRY alGetEffectfv(ALuint effect, ALenum param, ALfloat *pflValues);

ALvoid ReleaseALEffects(ALvoid);

#ifdef __cplusplus
}
#endif

#endif
