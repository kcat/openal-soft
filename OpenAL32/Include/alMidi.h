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
    RefCount ref;

    struct ALbuffer *Buffer;

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

    /* NOTE: Each map entry contains *four* (4) ALsfmodulator objects. */
    UIntMap ModulatorMap;

    ALuint id;
} ALfontsound;

void ALfontsound_setPropi(ALfontsound *self, ALCcontext *context, ALenum param, ALint value);
void ALfontsound_setModStagei(ALfontsound *self, ALCcontext *context, ALsizei stage, ALenum param, ALint value);

ALfontsound *NewFontsound(ALCcontext *context);
void DeleteFontsound(ALCdevice *device, ALfontsound *sound);

inline struct ALfontsound *LookupFontsound(ALCdevice *device, ALuint id)
{ return (struct ALfontsound*)LookupUIntMapKey(&device->FontsoundMap, id); }
inline struct ALfontsound *RemoveFontsound(ALCdevice *device, ALuint id)
{ return (struct ALfontsound*)RemoveUIntMapKey(&device->FontsoundMap, id); }

void ReleaseALFontsounds(ALCdevice *device);


typedef struct ALsfpreset {
    RefCount ref;

    ALint Preset; /* a.k.a. MIDI program number */
    ALint Bank; /* MIDI bank 0...127, or percussion (bank 128) */

    ALfontsound **Sounds;
    ALsizei NumSounds;

    ALuint id;
} ALsfpreset;

ALsfpreset *NewPreset(ALCcontext *context);
void DeletePreset(ALCdevice *device, ALsfpreset *preset);

inline struct ALsfpreset *LookupPreset(ALCdevice *device, ALuint id)
{ return (struct ALsfpreset*)LookupUIntMapKey(&device->PresetMap, id); }
inline struct ALsfpreset *RemovePreset(ALCdevice *device, ALuint id)
{ return (struct ALsfpreset*)RemoveUIntMapKey(&device->PresetMap, id); }

void ReleaseALPresets(ALCdevice *device);


typedef struct ALsoundfont {
    RefCount ref;

    ALsfpreset **Presets;
    ALsizei NumPresets;

    RWLock Lock;

    ALuint id;
} ALsoundfont;

ALsoundfont *ALsoundfont_getDefSoundfont(ALCcontext *context);
void ALsoundfont_deleteSoundfont(ALsoundfont *self, ALCdevice *device);

inline struct ALsoundfont *LookupSfont(ALCdevice *device, ALuint id)
{ return (struct ALsoundfont*)LookupUIntMapKey(&device->SfontMap, id); }
inline struct ALsoundfont *RemoveSfont(ALCdevice *device, ALuint id)
{ return (struct ALsoundfont*)RemoveUIntMapKey(&device->SfontMap, id); }

void ReleaseALSoundfonts(ALCdevice *device);


inline ALboolean IsValidCtrlInput(int cc)
{
    /* These correspond to MIDI functions, not real controller values. */
    if(cc == 0 || cc == 6 || cc == 32 || cc == 38 || (cc >= 98 && cc <= 101) || cc >= 120)
        return AL_FALSE;
    /* These are the LSB components of CC0...CC31, which are automatically used when
     * reading the MSB controller value. */
    if(cc >= 32 && cc <= 63)
        return AL_FALSE;
    /* All the rest are okay! */
    return AL_TRUE;
}

#ifdef __cplusplus
}
#endif

#endif /* ALMIDI_H */
