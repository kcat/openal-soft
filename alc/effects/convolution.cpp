
#include "config.h"

#include "AL/al.h"
#include "AL/alc.h"

#include "al/auxeffectslot.h"
#include "alcmain.h"
#include "alcontext.h"
#include "almalloc.h"
#include "alspan.h"
#include "effects/base.h"


namespace {

struct ConvolutionState final : public EffectState {
    ConvolutionState() = default;
    ~ConvolutionState() override = default;

    void deviceUpdate(const ALCdevice *device) override;
    EffectBufferBase *createBuffer(const ALCdevice *device, const al::byte *sampleData,
        ALuint sampleRate, FmtType sampleType, FmtChannels channelType, ALuint numSamples) override;
    void update(const ALCcontext *context, const ALeffectslot *slot, const EffectProps *props, const EffectTarget target) override;
    void process(const size_t samplesToDo, const al::span<const FloatBufferLine> samplesIn, const al::span<FloatBufferLine> samplesOut) override;

    DEF_NEWDEL(ConvolutionState)
};

void ConvolutionState::deviceUpdate(const ALCdevice* /*device*/)
{
}

EffectBufferBase *ConvolutionState::createBuffer(const ALCdevice */*device*/,
    const al::byte */*samplesData*/, ALuint /*sampleRate*/, FmtType /*sampleType*/,
    FmtChannels /*channelType*/, ALuint /*numSamples*/)
{
    return nullptr;
}

void ConvolutionState::update(const ALCcontext* /*context*/, const ALeffectslot* /*slot*/,
    const EffectProps* /*props*/, const EffectTarget /*target*/)
{
}

void ConvolutionState::process(const size_t/*samplesToDo*/,
    const al::span<const FloatBufferLine> /*samplesIn*/,
    const al::span<FloatBufferLine> /*samplesOut*/)
{
}


void ConvolutionEffect_setParami(EffectProps* /*props*/, ALenum param, int /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid null effect integer property 0x%04x",
            param};
    }
}
void ConvolutionEffect_setParamiv(EffectProps *props, ALenum param, const int *vals)
{
    switch(param)
    {
    default:
        ConvolutionEffect_setParami(props, param, vals[0]);
    }
}
void ConvolutionEffect_setParamf(EffectProps* /*props*/, ALenum param, float /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid null effect float property 0x%04x",
            param};
    }
}
void ConvolutionEffect_setParamfv(EffectProps *props, ALenum param, const float *vals)
{
    switch(param)
    {
    default:
        ConvolutionEffect_setParamf(props, param, vals[0]);
    }
}

void ConvolutionEffect_getParami(const EffectProps* /*props*/, ALenum param, int* /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid null effect integer property 0x%04x",
            param};
    }
}
void ConvolutionEffect_getParamiv(const EffectProps *props, ALenum param, int *vals)
{
    switch(param)
    {
    default:
        ConvolutionEffect_getParami(props, param, vals);
    }
}
void ConvolutionEffect_getParamf(const EffectProps* /*props*/, ALenum param, float* /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid null effect float property 0x%04x",
            param};
    }
}
void ConvolutionEffect_getParamfv(const EffectProps *props, ALenum param, float *vals)
{
    switch(param)
    {
    default:
        ConvolutionEffect_getParamf(props, param, vals);
    }
}

DEFINE_ALEFFECT_VTABLE(ConvolutionEffect);


struct ConvolutionStateFactory final : public EffectStateFactory {
    EffectState *create() override;
    EffectProps getDefaultProps() const noexcept override;
    const EffectVtable *getEffectVtable() const noexcept override;
};

/* Creates EffectState objects of the appropriate type. */
EffectState *ConvolutionStateFactory::create()
{ return new ConvolutionState{}; }

/* Returns an ALeffectProps initialized with this effect type's default
 * property values.
 */
EffectProps ConvolutionStateFactory::getDefaultProps() const noexcept
{
    EffectProps props{};
    return props;
}

/* Returns a pointer to this effect type's global set/get vtable. */
const EffectVtable *ConvolutionStateFactory::getEffectVtable() const noexcept
{ return &ConvolutionEffect_vtable; }

} // namespace

EffectStateFactory *ConvolutionStateFactory_getFactory()
{
    static ConvolutionStateFactory ConvolutionFactory{};
    return &ConvolutionFactory;
}
