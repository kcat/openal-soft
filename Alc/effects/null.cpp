#include "config.h"

#include <cstdlib>

#include "AL/al.h"
#include "AL/alc.h"

#include "alMain.h"
#include "alcontext.h"
#include "alAuxEffectSlot.h"
#include "alError.h"


struct ALnullState final : public EffectState {
    ALnullState();
    ~ALnullState() override;

    ALboolean deviceUpdate(const ALCdevice *device) override;
    void update(const ALCcontext *context, const ALeffectslot *slot, const ALeffectProps *props, const EffectTarget target) override;
    void process(ALsizei samplesToDo, const ALfloat (*RESTRICT samplesIn)[BUFFERSIZE], ALfloat (*RESTRICT samplesOut)[BUFFERSIZE], ALsizei numChannels) override;

    DEF_NEWDEL(ALnullState)
};

/* This constructs the effect state. It's called when the object is first
 * created.
 */
ALnullState::ALnullState() = default;

/* This destructs the effect state. It's called only when the effect slot is no
 * longer used prior to being freed.
 */
ALnullState::~ALnullState() = default;

/* This updates the device-dependant effect state. This is called on
 * initialization and any time the device parameters (eg. playback frequency,
 * format) have been changed.
 */
ALboolean ALnullState::deviceUpdate(const ALCdevice* UNUSED(device))
{
    return AL_TRUE;
}

/* This updates the effect state. This is called any time the effect is
 * (re)loaded into a slot.
 */
void ALnullState::update(const ALCcontext* UNUSED(context), const ALeffectslot* UNUSED(slot), const ALeffectProps* UNUSED(props), const EffectTarget UNUSED(target))
{
}

/* This processes the effect state, for the given number of samples from the
 * input to the output buffer. The result should be added to the output buffer,
 * not replace it.
 */
void ALnullState::process(ALsizei UNUSED(samplesToDo), const ALfloat (*RESTRICT UNUSED(samplesIn))[BUFFERSIZE], ALfloat (*RESTRICT UNUSED(samplesOut))[BUFFERSIZE], ALsizei UNUSED(numChannels))
{
}


struct NullStateFactory final : public EffectStateFactory {
    EffectState *create() override;
};

/* Creates ALeffectState objects of the appropriate type. */
EffectState *NullStateFactory::create()
{ return new ALnullState{}; }

EffectStateFactory *NullStateFactory_getFactory()
{
    static NullStateFactory NullFactory{};
    return &NullFactory;
}


void ALnull_setParami(ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALint UNUSED(val))
{
    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid null effect integer property 0x%04x", param);
    }
}
void ALnull_setParamiv(ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, const ALint* UNUSED(vals))
{
    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid null effect integer-vector property 0x%04x", param);
    }
}
void ALnull_setParamf(ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALfloat UNUSED(val))
{
    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid null effect float property 0x%04x", param);
    }
}
void ALnull_setParamfv(ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, const ALfloat* UNUSED(vals))
{
    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid null effect float-vector property 0x%04x", param);
    }
}

void ALnull_getParami(const ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALint* UNUSED(val))
{
    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid null effect integer property 0x%04x", param);
    }
}
void ALnull_getParamiv(const ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALint* UNUSED(vals))
{
    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid null effect integer-vector property 0x%04x", param);
    }
}
void ALnull_getParamf(const ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALfloat* UNUSED(val))
{
    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid null effect float property 0x%04x", param);
    }
}
void ALnull_getParamfv(const ALeffect *UNUSED(effect), ALCcontext *context, ALenum param, ALfloat* UNUSED(vals))
{
    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid null effect float-vector property 0x%04x", param);
    }
}

DEFINE_ALEFFECT_VTABLE(ALnull);
