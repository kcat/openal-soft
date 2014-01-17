#ifndef ALMIDI_H
#define ALMIDI_H

#include "alMain.h"
#include "atomic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ALsfmodulator {
    struct {
        ALenum Input;
        ALenum Type;
        ALenum Form;
    } Source[2];
    ALint Amount;
    ALenum TransformOp;
    ALenum Dest;
} ALsfmodulator;

typedef struct ALenvelope {
    ALint DelayTime;
    ALint AttackTime;
    ALint HoldTime;
    ALint DecayTime;
    ALint SustainAttn;
    ALint ReleaseTime;
    ALint KeyToHoldTime;
    ALint KeyToDecayTime;
} ALenvelope;


typedef struct ALfontsound {
    volatile RefCount ref;

    ALint MinKey, MaxKey;
    ALint MinVelocity, MaxVelocity;

    ALint ModLfoToPitch;
    ALint VibratoLfoToPitch;
    ALint ModEnvToPitch;

    ALint FilterCutoff;
    ALint FilterQ;
    ALint ModLfoToFilterCutoff;
    ALint ModEnvToFilterCutoff;
    ALint ModLfoToVolume;

    ALint ChorusSend;
    ALint ReverbSend;

    ALint Pan;

    struct {
        ALint Delay;
        ALint Frequency;
    } ModLfo;
    struct {
        ALint Delay;
        ALint Frequency;
    } VibratoLfo;

    ALenvelope ModEnv;
    ALenvelope VolEnv;

    ALint Attenuation;

    ALint CoarseTuning;
    ALint FineTuning;

    ALenum LoopMode;

    ALint TuningScale;

    ALint ExclusiveClass;

    ALuint Start;
    ALuint End;
    ALuint LoopStart;
    ALuint LoopEnd;
    ALuint SampleRate;
    ALubyte PitchKey;
    ALbyte PitchCorrection;
    ALenum SampleType;
    struct ALfontsound *Link;

    UIntMap ModulatorMap;

    ALuint id;
} ALfontsound;

void ALfontsound_Destruct(ALfontsound *self);
void ALfontsound_setPropi(ALfontsound *self, ALCcontext *context, ALenum param, ALint value);
void ALfontsound_setModStagei(ALfontsound *self, ALCcontext *context, ALsizei stage, ALenum param, ALint value);

ALfontsound *NewFontsound(ALCcontext *context);

inline struct ALfontsound *LookupFontsound(ALCdevice *device, ALuint id)
{ return (struct ALfontsound*)LookupUIntMapKey(&device->FontsoundMap, id); }
inline struct ALfontsound *RemoveFontsound(ALCdevice *device, ALuint id)
{ return (struct ALfontsound*)RemoveUIntMapKey(&device->FontsoundMap, id); }

inline struct ALsfmodulator *LookupModulator(ALfontsound *sound, ALuint id)
{ return (struct ALsfmodulator*)LookupUIntMapKey(&sound->ModulatorMap, id); }
inline struct ALsfmodulator *RemoveModulator(ALfontsound *sound, ALuint id)
{ return (struct ALsfmodulator*)RemoveUIntMapKey(&sound->ModulatorMap, id); }

void ReleaseALFontsounds(ALCdevice *device);


typedef struct ALsfpreset {
    volatile RefCount ref;

    ALint Preset; /* a.k.a. MIDI program number */
    ALint Bank; /* MIDI bank 0...127, or percussion (bank 128) */

    ALfontsound **Sounds;
    ALsizei NumSounds;

    ALuint id;
} ALsfpreset;

ALsfpreset *NewPreset(ALCcontext *context);
void DeletePreset(ALsfpreset *preset, ALCdevice *device);

inline struct ALsfpreset *LookupPreset(ALCdevice *device, ALuint id)
{ return (struct ALsfpreset*)LookupUIntMapKey(&device->PresetMap, id); }
inline struct ALsfpreset *RemovePreset(ALCdevice *device, ALuint id)
{ return (struct ALsfpreset*)RemoveUIntMapKey(&device->PresetMap, id); }

void ReleaseALPresets(ALCdevice *device);


typedef struct ALsoundfont {
    volatile RefCount ref;

    ALsfpreset **Presets;
    ALsizei NumPresets;

    ALshort *Samples;
    ALint NumSamples;

    RWLock Lock;
    volatile ALenum Mapped;

    ALuint id;
} ALsoundfont;

void ALsoundfont_Construct(ALsoundfont *self);
void ALsoundfont_Destruct(ALsoundfont *self);
ALsoundfont *ALsoundfont_getDefSoundfont(ALCcontext *context);
void ALsoundfont_deleteSoundfont(ALsoundfont *self, ALCdevice *device);

inline struct ALsoundfont *LookupSfont(ALCdevice *device, ALuint id)
{ return (struct ALsoundfont*)LookupUIntMapKey(&device->SfontMap, id); }
inline struct ALsoundfont *RemoveSfont(ALCdevice *device, ALuint id)
{ return (struct ALsoundfont*)RemoveUIntMapKey(&device->SfontMap, id); }

void ReleaseALSoundfonts(ALCdevice *device);

#ifdef __cplusplus
}
#endif

#endif /* ALMIDI_H */
