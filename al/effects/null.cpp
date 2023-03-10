
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

class EaxNullEffectException : public EaxException
{
public:
    explicit EaxNullEffectException(const char* message)
        : EaxException{"EAX_NULL_EFFECT", message}
    {}
}; // EaxNullEffectException

class EaxNullEffect final : public EaxEffect4<EaxNullEffectException>
{
public:
    EaxNullEffect(int eax_version);

private:
    void set_defaults(Props4& props) override;

    void set_efx_defaults() override;

    void get(const EaxCall& call, const Props4& props) override;
    void set(const EaxCall& call, Props4& props) override;
    bool commit_props(const Props4& props) override;
}; // EaxCompressorEffect

EaxNullEffect::EaxNullEffect(int eax_version)
    : EaxEffect4{AL_EFFECT_NULL, eax_version}
{}

void EaxNullEffect::set_defaults(Props4& props)
{
    props.mType = EaxEffectType::None;
}

void EaxNullEffect::set_efx_defaults()
{
}

void EaxNullEffect::get(const EaxCall& call, const Props4&)
{
    if(call.get_property_id() != 0)
        fail_unknown_property_id();
}

void EaxNullEffect::set(const EaxCall& call, Props4&)
{
    if(call.get_property_id() != 0)
        fail_unknown_property_id();
}

bool EaxNullEffect::commit_props(const Props4&)
{
    return false;
}

} // namespace

EaxEffectUPtr eax_create_eax_null_effect(int eax_version)
{
    return std::make_unique<EaxNullEffect>(eax_version);
}

#endif // ALSOFT_EAX
