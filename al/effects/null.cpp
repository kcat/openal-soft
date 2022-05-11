
#include "config.h"

#include "AL/al.h"
#include "AL/efx.h"

#include "alc/effects/base.h"
#include "effects.h"

#ifdef ALSOFT_EAX
#include "al/eax/exception.h"
#endif // ALSOFT_EAX


namespace {

void Null_setParami(EffectProps* /*props*/, ALenum param, int /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid null effect integer property 0x%04x",
            param};
    }
}
void Null_setParamiv(EffectProps *props, ALenum param, const int *vals)
{
    switch(param)
    {
    default:
        Null_setParami(props, param, vals[0]);
    }
}
void Null_setParamf(EffectProps* /*props*/, ALenum param, float /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid null effect float property 0x%04x",
            param};
    }
}
void Null_setParamfv(EffectProps *props, ALenum param, const float *vals)
{
    switch(param)
    {
    default:
        Null_setParamf(props, param, vals[0]);
    }
}

void Null_getParami(const EffectProps* /*props*/, ALenum param, int* /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid null effect integer property 0x%04x",
            param};
    }
}
void Null_getParamiv(const EffectProps *props, ALenum param, int *vals)
{
    switch(param)
    {
    default:
        Null_getParami(props, param, vals);
    }
}
void Null_getParamf(const EffectProps* /*props*/, ALenum param, float* /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid null effect float property 0x%04x",
            param};
    }
}
void Null_getParamfv(const EffectProps *props, ALenum param, float *vals)
{
    switch(param)
    {
    default:
        Null_getParamf(props, param, vals);
    }
}

EffectProps genDefaultProps() noexcept
{
    EffectProps props{};
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Null);

const EffectProps NullEffectProps{genDefaultProps()};


#ifdef ALSOFT_EAX
namespace {

class EaxNullEffect final : public EaxEffect {
public:
    EaxNullEffect(const EaxCall& call) noexcept;

    void dispatch() override;
    /*[[nodiscard]]*/ bool commit() override;

private:
    const EaxCall& call_;
}; // EaxNullEffect


class EaxNullEffectException : public EaxException
{
public:
    explicit EaxNullEffectException(const char* message)
        : EaxException{"EAX_NULL_EFFECT", message}
    {}
}; // EaxNullEffectException

EaxNullEffect::EaxNullEffect(const EaxCall& call) noexcept
    : EaxEffect{AL_EFFECT_NULL}, call_{call}
{}

void EaxNullEffect::dispatch()
{
    if(call_.get_property_id() != 0)
        throw EaxNullEffectException{"Unsupported property id."};
}

bool EaxNullEffect::commit()
{
    return false;
}

} // namespace

EaxEffectUPtr eax_create_eax_null_effect(const EaxCall& call)
{
    return std::make_unique<EaxNullEffect>(call);
}

#endif // ALSOFT_EAX
