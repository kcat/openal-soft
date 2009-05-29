// NOTE:  The effect structure is getting too large, it may be a good idea to
//        start using a union or another form of unified storage.
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
#define AL_EFFECT_EAXREVERB                                0x8000

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

#define AL_REVERB_MIN_DENSITY                              (0.0f)
#define AL_REVERB_MAX_DENSITY                              (1.0f)
#define AL_REVERB_DEFAULT_DENSITY                          (1.0f)
#define AL_REVERB_MIN_DIFFUSION                            (0.0f)
#define AL_REVERB_MAX_DIFFUSION                            (1.0f)
#define AL_REVERB_DEFAULT_DIFFUSION                        (1.0f)
#define AL_REVERB_MIN_GAIN                                 (0.0f)
#define AL_REVERB_MAX_GAIN                                 (1.0f)
#define AL_REVERB_DEFAULT_GAIN                             (0.32f)
#define AL_REVERB_MIN_GAINHF                               (0.0f)
#define AL_REVERB_MAX_GAINHF                               (1.0f)
#define AL_REVERB_DEFAULT_GAINHF                           (0.89f)
#define AL_REVERB_MIN_DECAY_TIME                           (0.1f)
#define AL_REVERB_MAX_DECAY_TIME                           (20.0f)
#define AL_REVERB_DEFAULT_DECAY_TIME                       (1.49f)
#define AL_REVERB_MIN_DECAY_HFRATIO                        (0.1f)
#define AL_REVERB_MAX_DECAY_HFRATIO                        (2.0f)
#define AL_REVERB_DEFAULT_DECAY_HFRATIO                    (0.83f)
#define AL_REVERB_MIN_REFLECTIONS_GAIN                     (0.0f)
#define AL_REVERB_MAX_REFLECTIONS_GAIN                     (3.16f)
#define AL_REVERB_DEFAULT_REFLECTIONS_GAIN                 (0.05f)
#define AL_REVERB_MIN_REFLECTIONS_DELAY                    (0.0f)
#define AL_REVERB_MAX_REFLECTIONS_DELAY                    (0.3f)
#define AL_REVERB_DEFAULT_REFLECTIONS_DELAY                (0.007f)
#define AL_REVERB_MIN_LATE_REVERB_GAIN                     (0.0f)
#define AL_REVERB_MAX_LATE_REVERB_GAIN                     (10.0f)
#define AL_REVERB_DEFAULT_LATE_REVERB_GAIN                 (1.26f)
#define AL_REVERB_MIN_LATE_REVERB_DELAY                    (0.0f)
#define AL_REVERB_MAX_LATE_REVERB_DELAY                    (0.1f)
#define AL_REVERB_DEFAULT_LATE_REVERB_DELAY                (0.011f)
#define AL_REVERB_MIN_AIR_ABSORPTION_GAINHF                (0.892f)
#define AL_REVERB_MAX_AIR_ABSORPTION_GAINHF                (1.0f)
#define AL_REVERB_DEFAULT_AIR_ABSORPTION_GAINHF            (0.994f)
#define AL_REVERB_MIN_ROOM_ROLLOFF_FACTOR                  (0.0f)
#define AL_REVERB_MAX_ROOM_ROLLOFF_FACTOR                  (10.0f)
#define AL_REVERB_DEFAULT_ROOM_ROLLOFF_FACTOR              (0.0f)
#define AL_REVERB_MIN_DECAY_HFLIMIT                        (AL_FALSE)
#define AL_REVERB_MAX_DECAY_HFLIMIT                        (AL_TRUE)
#define AL_REVERB_DEFAULT_DECAY_HFLIMIT                    (AL_TRUE)

#define AL_ECHO_DELAY                                      0x0001
#define AL_ECHO_LRDELAY                                    0x0002
#define AL_ECHO_DAMPING                                    0x0003
#define AL_ECHO_FEEDBACK                                   0x0004
#define AL_ECHO_SPREAD                                     0x0005

#define AL_ECHO_MIN_DELAY                                  (0.0f)
#define AL_ECHO_MAX_DELAY                                  (0.207f)
#define AL_ECHO_DEFAULT_DELAY                              (0.1f)
#define AL_ECHO_MIN_LRDELAY                                (0.0f)
#define AL_ECHO_MAX_LRDELAY                                (0.404f)
#define AL_ECHO_DEFAULT_LRDELAY                            (0.1f)
#define AL_ECHO_MIN_DAMPING                                (0.0f)
#define AL_ECHO_MAX_DAMPING                                (0.99f)
#define AL_ECHO_DEFAULT_DAMPING                            (0.5f)
#define AL_ECHO_MIN_FEEDBACK                               (0.0f)
#define AL_ECHO_MAX_FEEDBACK                               (1.0f)
#define AL_ECHO_DEFAULT_FEEDBACK                           (0.5f)
#define AL_ECHO_MIN_SPREAD                                 (-1.0f)
#define AL_ECHO_MAX_SPREAD                                 (1.0f)
#define AL_ECHO_DEFAULT_SPREAD                             (-1.0f)

#define AL_EAXREVERB_DENSITY                               0x0001
#define AL_EAXREVERB_DIFFUSION                             0x0002
#define AL_EAXREVERB_GAIN                                  0x0003
#define AL_EAXREVERB_GAINHF                                0x0004
#define AL_EAXREVERB_GAINLF                                0x0005
#define AL_EAXREVERB_DECAY_TIME                            0x0006
#define AL_EAXREVERB_DECAY_HFRATIO                         0x0007
#define AL_EAXREVERB_DECAY_LFRATIO                         0x0008
#define AL_EAXREVERB_REFLECTIONS_GAIN                      0x0009
#define AL_EAXREVERB_REFLECTIONS_DELAY                     0x000A
#define AL_EAXREVERB_REFLECTIONS_PAN                       0x000B
#define AL_EAXREVERB_LATE_REVERB_GAIN                      0x000C
#define AL_EAXREVERB_LATE_REVERB_DELAY                     0x000D
#define AL_EAXREVERB_LATE_REVERB_PAN                       0x000E
#define AL_EAXREVERB_ECHO_TIME                             0x000F
#define AL_EAXREVERB_ECHO_DEPTH                            0x0010
#define AL_EAXREVERB_MODULATION_TIME                       0x0011
#define AL_EAXREVERB_MODULATION_DEPTH                      0x0012
#define AL_EAXREVERB_AIR_ABSORPTION_GAINHF                 0x0013
#define AL_EAXREVERB_HFREFERENCE                           0x0014
#define AL_EAXREVERB_LFREFERENCE                           0x0015
#define AL_EAXREVERB_ROOM_ROLLOFF_FACTOR                   0x0016
#define AL_EAXREVERB_DECAY_HFLIMIT                         0x0017

#define AL_EAXREVERB_MIN_DENSITY                           (0.0f)
#define AL_EAXREVERB_MAX_DENSITY                           (1.0f)
#define AL_EAXREVERB_DEFAULT_DENSITY                       (1.0f)
#define AL_EAXREVERB_MIN_DIFFUSION                         (0.0f)
#define AL_EAXREVERB_MAX_DIFFUSION                         (1.0f)
#define AL_EAXREVERB_DEFAULT_DIFFUSION                     (1.0f)
#define AL_EAXREVERB_MIN_GAIN                              (0.0f)
#define AL_EAXREVERB_MAX_GAIN                              (1.0f)
#define AL_EAXREVERB_DEFAULT_GAIN                          (0.32f)
#define AL_EAXREVERB_MIN_GAINHF                            (0.0f)
#define AL_EAXREVERB_MAX_GAINHF                            (1.0f)
#define AL_EAXREVERB_DEFAULT_GAINHF                        (0.89f)
#define AL_EAXREVERB_MIN_GAINLF                            (0.0f)
#define AL_EAXREVERB_MAX_GAINLF                            (1.0f)
#define AL_EAXREVERB_DEFAULT_GAINLF                        (1.0f)
#define AL_EAXREVERB_MIN_DECAY_TIME                        (0.1f)
#define AL_EAXREVERB_MAX_DECAY_TIME                        (20.0f)
#define AL_EAXREVERB_DEFAULT_DECAY_TIME                    (1.49f)
#define AL_EAXREVERB_MIN_DECAY_HFRATIO                     (0.1f)
#define AL_EAXREVERB_MAX_DECAY_HFRATIO                     (2.0f)
#define AL_EAXREVERB_DEFAULT_DECAY_HFRATIO                 (0.83f)
#define AL_EAXREVERB_MIN_DECAY_LFRATIO                     (0.1f)
#define AL_EAXREVERB_MAX_DECAY_LFRATIO                     (2.0f)
#define AL_EAXREVERB_DEFAULT_DECAY_LFRATIO                 (1.0f)
#define AL_EAXREVERB_MIN_REFLECTIONS_GAIN                  (0.0f)
#define AL_EAXREVERB_MAX_REFLECTIONS_GAIN                  (3.16f)
#define AL_EAXREVERB_DEFAULT_REFLECTIONS_GAIN              (0.05f)
#define AL_EAXREVERB_MIN_REFLECTIONS_DELAY                 (0.0f)
#define AL_EAXREVERB_MAX_REFLECTIONS_DELAY                 (0.3f)
#define AL_EAXREVERB_DEFAULT_REFLECTIONS_DELAY             (0.007f)
#define AL_EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ           (0.0f)
#define AL_EAXREVERB_MIN_LATE_REVERB_GAIN                  (0.0f)
#define AL_EAXREVERB_MAX_LATE_REVERB_GAIN                  (10.0f)
#define AL_EAXREVERB_DEFAULT_LATE_REVERB_GAIN              (1.26f)
#define AL_EAXREVERB_MIN_LATE_REVERB_DELAY                 (0.0f)
#define AL_EAXREVERB_MAX_LATE_REVERB_DELAY                 (0.1f)
#define AL_EAXREVERB_DEFAULT_LATE_REVERB_DELAY             (0.011f)
#define AL_EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ           (0.0f)
#define AL_EAXREVERB_MIN_ECHO_TIME                         (0.075f)
#define AL_EAXREVERB_MAX_ECHO_TIME                         (0.25f)
#define AL_EAXREVERB_DEFAULT_ECHO_TIME                     (0.25f)
#define AL_EAXREVERB_MIN_ECHO_DEPTH                        (0.0f)
#define AL_EAXREVERB_MAX_ECHO_DEPTH                        (1.0f)
#define AL_EAXREVERB_DEFAULT_ECHO_DEPTH                    (0.0f)
#define AL_EAXREVERB_MIN_MODULATION_TIME                   (0.04f)
#define AL_EAXREVERB_MAX_MODULATION_TIME                   (4.0f)
#define AL_EAXREVERB_DEFAULT_MODULATION_TIME               (0.25f)
#define AL_EAXREVERB_MIN_MODULATION_DEPTH                  (0.0f)
#define AL_EAXREVERB_MAX_MODULATION_DEPTH                  (1.0f)
#define AL_EAXREVERB_DEFAULT_MODULATION_DEPTH              (0.0f)
#define AL_EAXREVERB_MIN_AIR_ABSORPTION_GAINHF             (0.892f)
#define AL_EAXREVERB_MAX_AIR_ABSORPTION_GAINHF             (1.0f)
#define AL_EAXREVERB_DEFAULT_AIR_ABSORPTION_GAINHF         (0.994f)
#define AL_EAXREVERB_MIN_HFREFERENCE                       (1000.0f)
#define AL_EAXREVERB_MAX_HFREFERENCE                       (20000.0f)
#define AL_EAXREVERB_DEFAULT_HFREFERENCE                   (5000.0f)
#define AL_EAXREVERB_MIN_LFREFERENCE                       (20.0f)
#define AL_EAXREVERB_MAX_LFREFERENCE                       (1000.0f)
#define AL_EAXREVERB_DEFAULT_LFREFERENCE                   (250.0f)
#define AL_EAXREVERB_MIN_ROOM_ROLLOFF_FACTOR               (0.0f)
#define AL_EAXREVERB_MAX_ROOM_ROLLOFF_FACTOR               (10.0f)
#define AL_EAXREVERB_DEFAULT_ROOM_ROLLOFF_FACTOR           (0.0f)
#define AL_EAXREVERB_MIN_DECAY_HFLIMIT                     (AL_FALSE)
#define AL_EAXREVERB_MAX_DECAY_HFLIMIT                     (AL_TRUE)
#define AL_EAXREVERB_DEFAULT_DECAY_HFLIMIT                 (AL_TRUE)

enum {
    EAXREVERB = 0,
    REVERB,
    ECHO,

    MAX_EFFECTS
};
extern ALboolean DisabledEffects[MAX_EFFECTS];

typedef struct ALeffect_struct
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
