
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

using NullCommitter = EaxCommitter<EaxNullCommitter>;

} // namespace

template<>
struct NullCommitter::Exception : public EaxException
{
    explicit Exception(const char *message) : EaxException{"EAX_NULL_EFFECT", message}
    { }
};

template<>
[[noreturn]] void NullCommitter::fail(const char *message)
{
    throw Exception{message};
}

template<>
bool NullCommitter::commit(const EaxEffectProps &props)
{
    const bool ret{props.mType != mEaxProps.mType};
    mEaxProps = props;
    return ret;
}

template<>
void NullCommitter::SetDefaults(EaxEffectProps &props)
{
    props = EaxEffectProps{};
    props.mType = EaxEffectType::None;
}

template<>
void NullCommitter::Get(const EaxCall &call, const EaxEffectProps&)
{
    if(call.get_property_id() != 0)
        fail_unknown_property_id();
}

template<>
void NullCommitter::Set(const EaxCall &call, EaxEffectProps&)
{
    if(call.get_property_id() != 0)
        fail_unknown_property_id();
}

#endif // ALSOFT_EAX
