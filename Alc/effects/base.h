#ifndef EFFECTS_BASE_H
#define EFFECTS_BASE_H

#include "alMain.h"

#include "almalloc.h"
#include "atomic.h"


struct ALeffectslot;
union ALeffectProps;


struct EffectVtable {
    void (*const setParami)(ALeffectProps *props, ALCcontext *context, ALenum param, ALint val);
    void (*const setParamiv)(ALeffectProps *props, ALCcontext *context, ALenum param, const ALint *vals);
    void (*const setParamf)(ALeffectProps *props, ALCcontext *context, ALenum param, ALfloat val);
    void (*const setParamfv)(ALeffectProps *props, ALCcontext *context, ALenum param, const ALfloat *vals);

    void (*const getParami)(const ALeffectProps *props, ALCcontext *context, ALenum param, ALint *val);
    void (*const getParamiv)(const ALeffectProps *props, ALCcontext *context, ALenum param, ALint *vals);
    void (*const getParamf)(const ALeffectProps *props, ALCcontext *context, ALenum param, ALfloat *val);
    void (*const getParamfv)(const ALeffectProps *props, ALCcontext *context, ALenum param, ALfloat *vals);
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

struct EffectState {
    RefCount mRef{1u};

    ALfloat (*mOutBuffer)[BUFFERSIZE]{nullptr};
    ALsizei mOutChannels{0};


    virtual ~EffectState() = default;

    virtual ALboolean deviceUpdate(const ALCdevice *device) = 0;
    virtual void update(const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props, const EffectTarget target) = 0;
    virtual void process(ALsizei samplesToDo, const ALfloat (*RESTRICT samplesIn)[BUFFERSIZE], const ALsizei numInput, ALfloat (*RESTRICT samplesOut)[BUFFERSIZE], const ALsizei numOutput) = 0;

    void IncRef() noexcept;
    void DecRef() noexcept;
};


struct EffectStateFactory {
    virtual ~EffectStateFactory() { }

    virtual EffectState *create() = 0;
    virtual ALeffectProps getDefaultProps() const noexcept = 0;
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

EffectStateFactory *DedicatedStateFactory_getFactory(void);


#endif /* EFFECTS_BASE_H */
