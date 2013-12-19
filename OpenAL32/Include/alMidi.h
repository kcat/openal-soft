#ifndef ALMIDI_H
#define ALMIDI_H

#include "alMain.h"
#include "atomic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ALsfgenerator {
    ALenum Generator;
    ALint Value;
} ALsfgenerator;

typedef struct ALsfmodulator {
    ALenum SourceOp;
    ALenum DestOp;
    ALint Amount;
    ALenum AmountSourceOp;
    ALenum TransformOp;
} ALsfmodulator;

typedef struct ALsfzone {
    ALsfgenerator *Generators;
    ALsizei NumGenerators;
    ALsizei GeneratorsMax;

    ALsfmodulator *Modulators;
    ALsizei NumModulators;
    ALsizei ModulatorsMax;

    /* NOTE: Preset zones may have a reference to an ALsfinstrument. Instrument
     * zones may have a reference to an ALsfsample. */
    ALvoid *Object;
} ALsfzone;

void ALsfzone_Construct(ALsfzone *self);
void ALsfzone_Destruct(ALsfzone *self);
ALenum ALsfzone_addGenerator(ALsfzone *self, ALenum generator, ALint value);
ALenum ALsfzone_addModulator(ALsfzone *self, ALenum sourceop, ALenum destop, ALint amount, ALenum amtsourceop, ALenum transop);
/* Stores a new object pointer in the zone. Returns the old object pointer. */
ALvoid *ALsfzone_setRefObject(ALsfzone *self, ALvoid *object);


typedef struct ALsfsample {
    volatile RefCount ref;

    ALuint Start;
    ALuint End;
    ALuint LoopStart;
    ALuint LoopEnd;
    ALuint SampleRate;
    ALubyte PitchKey;
    ALbyte PitchCorrection;
    ALushort SampleLink;
    ALenum SampleType;

    ALuint id;
} ALsfsample;

void ALsfsample_Construct(ALsfsample *self);
void ALsfsample_Destruct(ALsfsample *self);


typedef struct ALsfinstrument {
    volatile RefCount ref;

    ALsfzone *Zones;
    ALsizei NumZones;

    ALuint id;
} ALsfinstrument;

void ALsfinstrument_Construct(ALsfinstrument *self);
void ALsfinstrument_Destruct(ALsfinstrument *self);


typedef struct ALsfpreset {
    volatile RefCount ref;

    ALint Program;
    ALint Bank;

    ALsfzone *Zones;
    ALsizei NumZones;

    ALuint id;
} ALsfpreset;

void ALsfpreset_Construct(ALsfpreset *self);
void ALsfpreset_Destruct(ALsfpreset *self);


typedef struct ALsoundfont {
    volatile RefCount ref;

    ALsfpreset **Presets;
    ALsizei NumPresets;

    ALshort *SampleData;
    ALint SampleDataLen;

    ALuint id;
} ALsoundfont;

void ALsoundfont_Construct(ALsoundfont *self);
void ALsoundfont_Destruct(ALsoundfont *self);

inline struct ALsoundfont *LookupSfont(ALCdevice *device, ALuint id)
{ return (struct ALsoundfont*)LookupUIntMapKey(&device->SfontMap, id); }
inline struct ALsoundfont *RemoveSfont(ALCdevice *device, ALuint id)
{ return (struct ALsoundfont*)RemoveUIntMapKey(&device->SfontMap, id); }


#ifdef __cplusplus
}
#endif

#endif /* ALMIDI_H */
