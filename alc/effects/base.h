#ifndef EFFECTS_BASE_H
#define EFFECTS_BASE_H

#include <cstddef>

#include "alcmain.h"
#include "almalloc.h"
#include "alspan.h"
#include "atomic.h"
#include "intrusive_ptr.h"

struct ALeffectslot;


union EffectProps {
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
        bool DecayHFLimit;

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
        ALfloat AttackTime;
        ALfloat ReleaseTime;
        ALfloat Resonance;
        ALfloat PeakGain;
    } Autowah;

    struct {
        ALint Waveform;
        ALint Phase;
        ALfloat Rate;
        ALfloat Depth;
        ALfloat Feedback;
        ALfloat Delay;
    } Chorus; /* Also Flanger */

    struct {
        bool OnOff;
    } Compressor;

    struct {
        ALfloat Edge;
        ALfloat Gain;
        ALfloat LowpassCutoff;
        ALfloat EQCenter;
        ALfloat EQBandwidth;
    } Distortion;

    struct {
        ALfloat Delay;
        ALfloat LRDelay;

        ALfloat Damping;
        ALfloat Feedback;

        ALfloat Spread;
    } Echo;

    struct {
        ALfloat LowCutoff;
        ALfloat LowGain;
        ALfloat Mid1Center;
        ALfloat Mid1Gain;
        ALfloat Mid1Width;
        ALfloat Mid2Center;
        ALfloat Mid2Gain;
        ALfloat Mid2Width;
        ALfloat HighCutoff;
        ALfloat HighGain;
    } Equalizer;

    struct {
        ALfloat Frequency;
        ALint LeftDirection;
        ALint RightDirection;
    } Fshifter;

    struct {
        ALfloat Frequency;
        ALfloat HighPassCutoff;
        ALint Waveform;
    } Modulator;

    struct {
        ALint CoarseTune;
        ALint FineTune;
    } Pshifter;

    struct {
        ALfloat Rate;
        ALint PhonemeA;
        ALint PhonemeB;
        ALint PhonemeACoarseTuning;
        ALint PhonemeBCoarseTuning;
        ALint Waveform;
    } Vmorpher;

    struct {
        ALfloat Gain;
    } Dedicated;
};


struct EffectVtable {
    void (*const setParami)(EffectProps *props, ALCcontext *context, ALenum param, ALint val);
    void (*const setParamiv)(EffectProps *props, ALCcontext *context, ALenum param, const ALint *vals);
    void (*const setParamf)(EffectProps *props, ALCcontext *context, ALenum param, ALfloat val);
    void (*const setParamfv)(EffectProps *props, ALCcontext *context, ALenum param, const ALfloat *vals);

    void (*const getParami)(const EffectProps *props, ALCcontext *context, ALenum param, ALint *val);
    void (*const getParamiv)(const EffectProps *props, ALCcontext *context, ALenum param, ALint *vals);
    void (*const getParamf)(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *val);
    void (*const getParamfv)(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *vals);
};

#define DEFINE_ALEFFECT_VTABLE(T)           \
const EffectVtable T##_vtable = {           \
    T##_setParami, T##_setParamiv,          \
    T##_setParamf, T##_setParamfv,          \
    T##_getParami, T##_getParamiv,          \
    T##_getParamf, T##_getParamfv,          \
}


struct EffectTarget {
    MixParams *Main;
    RealMixParams *RealOut;
};

struct EffectState : public al::intrusive_ref<EffectState> {
    al::span<FloatBufferLine> mOutTarget;


    virtual ~EffectState() = default;

    virtual ALboolean deviceUpdate(const ALCdevice *device) = 0;
    virtual void update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target) = 0;
    virtual void process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut) = 0;
};


struct EffectStateFactory {
    virtual ~EffectStateFactory() { }

    virtual EffectState *create() = 0;
    virtual EffectProps getDefaultProps() const noexcept = 0;
    virtual const EffectVtable *getEffectVtable() const noexcept = 0;
};


EffectStateFactory *NullStateFactory_getFactory(void);
EffectStateFactory *ReverbStateFactory_getFactory(void);
EffectStateFactory *StdReverbStateFactory_getFactory(void);
EffectStateFactory *AutowahStateFactory_getFactory(void);
EffectStateFactory *ChorusStateFactory_getFactory(void);
EffectStateFactory *CompressorStateFactory_getFactory(void);
EffectStateFactory *DistortionStateFactory_getFactory(void);
EffectStateFactory *EchoStateFactory_getFactory(void);
EffectStateFactory *EqualizerStateFactory_getFactory(void);
EffectStateFactory *FlangerStateFactory_getFactory(void);
EffectStateFactory *FshifterStateFactory_getFactory(void);
EffectStateFactory *ModulatorStateFactory_getFactory(void);
EffectStateFactory *PshifterStateFactory_getFactory(void);
EffectStateFactory* VmorpherStateFactory_getFactory(void);

EffectStateFactory *DedicatedStateFactory_getFactory(void);


#endif /* EFFECTS_BASE_H */
