#ifndef ALMIDI_H
#define ALMIDI_H

#include "alMain.h"
#include "atomic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ALsfmodulator {
    ALenum SourceOp;
    ALenum DestOp;
    ALint Amount;
    ALenum AmountSourceOp;
    ALenum TransformOp;
} ALsfmodulator;


typedef struct ALfontsound {
    volatile RefCount ref;

    ALint MinKey, MaxKey;
    ALint MinVelocity, MaxVelocity;

    ALuint Start;
    ALuint End;
    ALuint LoopStart;
    ALuint LoopEnd;
    ALuint SampleRate;
    ALubyte PitchKey;
    ALbyte PitchCorrection;
    ALenum SampleType;
    struct ALfontsound *Link;

    ALsfmodulator *Modulators;
    ALsizei NumModulators;
    ALsizei ModulatorsMax;

    ALuint id;
} ALfontsound;

void ALfontsound_Construct(ALfontsound *self);
void ALfontsound_Destruct(ALfontsound *self);
ALenum ALfontsound_addGenerator(ALfontsound *self, ALenum generator, ALint value);
ALenum ALfontsound_addModulator(ALfontsound *self, ALenum sourceop, ALenum destop, ALint amount, ALenum amtsourceop, ALenum transop);


inline struct ALfontsound *LookupFontsound(ALCdevice *device, ALuint id)
{ return (struct ALfontsound*)LookupUIntMapKey(&device->FontsoundMap, id); }
inline struct ALfontsound *RemoveFontsound(ALCdevice *device, ALuint id)
{ return (struct ALfontsound*)RemoveUIntMapKey(&device->FontsoundMap, id); }

void ReleaseALFontsounds(ALCdevice *device);


typedef struct ALsfpreset {
    volatile RefCount ref;

    ALint Preset; /* a.k.a. MIDI program number */
    ALint Bank; /* MIDI bank 0...127, or percussion (bank 128) */

    ALfontsound **Sounds;
    ALsizei NumSounds;

    ALuint id;
} ALsfpreset;

void ALsfpreset_Construct(ALsfpreset *self);
void ALsfpreset_Destruct(ALsfpreset *self);


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

inline struct ALsoundfont *LookupSfont(ALCdevice *device, ALuint id)
{ return (struct ALsoundfont*)LookupUIntMapKey(&device->SfontMap, id); }
inline struct ALsoundfont *RemoveSfont(ALCdevice *device, ALuint id)
{ return (struct ALsoundfont*)RemoveUIntMapKey(&device->SfontMap, id); }

void ReleaseALSoundfonts(ALCdevice *device);

#ifdef __cplusplus
}
#endif

#endif /* ALMIDI_H */
