#include "config.h"

#include <cstdlib>

#include "AL/al.h"
#include "AL/alc.h"

#include "alMain.h"
#include "alcontext.h"
#include "alAuxEffectSlot.h"
#include "alError.h"


namespace {

struct NullState final : public EffectState {
    NullState();
    ~NullState() override;

    ALboolean deviceUpdate(const ALCdevice *device) override;
    void update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target) override;
    void process(const ALsizei samplesToDo, const FloatBufferLine *RESTRICT samplesIn, const ALsizei numInput, const al::span<FloatBufferLine> samplesOut) override;

    DEF_NEWDEL(NullState)
};

/* This constructs the effect state. It's called when the object is first
 * created.
 */
NullState::NullState() = default;

/* This destructs the effect state. It's called only when the effect instance
 * is no longer used.
 */
NullState::~NullState() = default;

/* This updates the device-dependant effect state. This is called on
 * initialization and any time the device parameters (e.g. playback frequency,
 * format) have been changed. Will always be followed by a call to the update
 * method, if successful.
 */
ALboolean NullState::deviceUpdate(const ALCdevice* UNUSED(device))
{
    return AL_TRUE;
}

/* This updates the effect state. This is called any time the effect is
 * (re)loaded into a slot.
 */
void NullState::update(const ALCcontext* UNUSED(context), const ALeffectslot* UNUSED(slot), const EffectProps* UNUSED(props), const EffectTarget UNUSED(target))
{
}

/* This processes the effect state, for the given number of samples from the
 * input to the output buffer. The result should be added to the output buffer,
 * not replace it.
 */
void NullState::process(const ALsizei /*samplesToDo*/, const FloatBufferLine *RESTRICT /*samplesIn*/, const ALsizei /*numInput*/, const al::span<FloatBufferLine> /*samplesOut*/)
{
}


void NullEffect_setParami(EffectProps *UNUSED(props), ALCcontext *context, ALenum param, ALint UNUSED(val))
{
    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid null effect integer property 0x%04x", param);
    }
}
void NullEffect_setParamiv(EffectProps *props, ALCcontext *context, ALenum param, const ALint *vals)
{
    switch(param)
    {
    default:
        NullEffect_setParami(props, context, param, vals[0]);
    }
}
void NullEffect_setParamf(EffectProps *UNUSED(props), ALCcontext *context, ALenum param, ALfloat UNUSED(val))
{
    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid null effect float property 0x%04x", param);
    }
}
void NullEffect_setParamfv(EffectProps *props, ALCcontext *context, ALenum param, const ALfloat *vals)
{
    switch(param)
    {
    default:
        NullEffect_setParamf(props, context, param, vals[0]);
    }
}

void NullEffect_getParami(const EffectProps *UNUSED(props), ALCcontext *context, ALenum param, ALint* UNUSED(val))
{
    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid null effect integer property 0x%04x", param);
    }
}
void NullEffect_getParamiv(const EffectProps *props, ALCcontext *context, ALenum param, ALint *vals)
{
    switch(param)
    {
    default:
        NullEffect_getParami(props, context, param, vals);
    }
}
void NullEffect_getParamf(const EffectProps *UNUSED(props), ALCcontext *context, ALenum param, ALfloat* UNUSED(val))
{
    switch(param)
    {
    default:
        alSetError(context, AL_INVALID_ENUM, "Invalid null effect float property 0x%04x", param);
    }
}
void NullEffect_getParamfv(const EffectProps *props, ALCcontext *context, ALenum param, ALfloat *vals)
{
    switch(param)
    {
    default:
        NullEffect_getParamf(props, context, param, vals);
    }
}

DEFINE_ALEFFECT_VTABLE(NullEffect);


struct NullStateFactory final : public EffectStateFactory {
    EffectState *create() override;
    EffectProps getDefaultProps() const noexcept override;
    const EffectVtable *getEffectVtable() const noexcept override;
};

/* Creates EffectState objects of the appropriate type. */
EffectState *NullStateFactory::create()
{ return new NullState{}; }

/* Returns an ALeffectProps initialized with this effect type's default
 * property values.
 */
EffectProps NullStateFactory::getDefaultProps() const noexcept
{
    EffectProps props{};
    return props;
}

/* Returns a pointer to this effect type's global set/get vtable. */
const EffectVtable *NullStateFactory::getEffectVtable() const noexcept
{ return &NullEffect_vtable; }

} // namespace

EffectStateFactory *NullStateFactory_getFactory()
{
    static NullStateFactory NullFactory{};
    return &NullFactory;
}
