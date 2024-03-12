
#include "config.h"

#include <cmath>

#include "AL/al.h"
#include "AL/alext.h"

#include "alc/effects/base.h"
#include "effects.h"


namespace {

constexpr EffectProps genDefaultDialogProps() noexcept
{
    DedicatedDialogProps props{};
    props.Gain = 1.0f;
    return props;
}

constexpr EffectProps genDefaultLfeProps() noexcept
{
    DedicatedLfeProps props{};
    props.Gain = 1.0f;
    return props;
}

} // namespace

const EffectProps DedicatedDialogEffectProps{genDefaultDialogProps()};

void EffectHandler::SetParami(DedicatedDialogProps&, ALenum param, int)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated integer property 0x%04x", param}; }
void EffectHandler::SetParamiv(DedicatedDialogProps&, ALenum param, const int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated integer-vector property 0x%04x",
        param};
}
void EffectHandler::SetParamf(DedicatedDialogProps &props, ALenum param, float val)
{
    switch(param)
    {
    case AL_DEDICATED_GAIN:
        if(!(val >= 0.0f && std::isfinite(val)))
            throw effect_exception{AL_INVALID_VALUE, "Dedicated gain out of range"};
        props.Gain = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated float property 0x%04x", param};
    }
}
void EffectHandler::SetParamfv(DedicatedDialogProps &props, ALenum param, const float *vals)
{ SetParamf(props, param, *vals); }

void EffectHandler::GetParami(const DedicatedDialogProps&, ALenum param, int*)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated integer property 0x%04x", param}; }
void EffectHandler::GetParamiv(const DedicatedDialogProps&, ALenum param, int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated integer-vector property 0x%04x",
        param};
}
void EffectHandler::GetParamf(const DedicatedDialogProps &props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_DEDICATED_GAIN: *val = props.Gain; break;
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated float property 0x%04x", param};
    }
}
void EffectHandler::GetParamfv(const DedicatedDialogProps &props, ALenum param, float *vals)
{ GetParamf(props, param, vals); }


const EffectProps DedicatedLfeEffectProps{genDefaultLfeProps()};

void EffectHandler::SetParami(DedicatedLfeProps&, ALenum param, int)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated integer property 0x%04x", param}; }
void EffectHandler::SetParamiv(DedicatedLfeProps&, ALenum param, const int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated integer-vector property 0x%04x",
        param};
}
void EffectHandler::SetParamf(DedicatedLfeProps &props, ALenum param, float val)
{
    switch(param)
    {
    case AL_DEDICATED_GAIN:
        if(!(val >= 0.0f && std::isfinite(val)))
            throw effect_exception{AL_INVALID_VALUE, "Dedicated gain out of range"};
        props.Gain = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated float property 0x%04x", param};
    }
}
void EffectHandler::SetParamfv(DedicatedLfeProps &props, ALenum param, const float *vals)
{ SetParamf(props, param, *vals); }

void EffectHandler::GetParami(const DedicatedLfeProps&, ALenum param, int*)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated integer property 0x%04x", param}; }
void EffectHandler::GetParamiv(const DedicatedLfeProps&, ALenum param, int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated integer-vector property 0x%04x",
        param};
}
void EffectHandler::GetParamf(const DedicatedLfeProps &props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_DEDICATED_GAIN: *val = props.Gain; break;
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated float property 0x%04x", param};
    }
}
void EffectHandler::GetParamfv(const DedicatedLfeProps &props, ALenum param, float *vals)
{ GetParamf(props, param, vals); }
