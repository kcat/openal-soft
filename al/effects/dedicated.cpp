
#include "config.h"

#include <cmath>

#include "AL/al.h"
#include "AL/alext.h"

#include "alc/effects/base.h"
#include "effects.h"


namespace {

void DedicatedDialog_setParami(EffectProps*, ALenum param, int)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated integer property 0x%04x", param}; }
void DedicatedDialog_setParamiv(EffectProps*, ALenum param, const int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated integer-vector property 0x%04x",
        param};
}
void DedicatedDialog_setParamf(EffectProps *props, ALenum param, float val)
{
    switch(param)
    {
    case AL_DEDICATED_GAIN:
        if(!(val >= 0.0f && std::isfinite(val)))
            throw effect_exception{AL_INVALID_VALUE, "Dedicated gain out of range"};
        props->DedicatedDialog.Gain = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated float property 0x%04x", param};
    }
}
void DedicatedDialog_setParamfv(EffectProps *props, ALenum param, const float *vals)
{ DedicatedDialog_setParamf(props, param, vals[0]); }

void DedicatedDialog_getParami(const EffectProps*, ALenum param, int*)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated integer property 0x%04x", param}; }
void DedicatedDialog_getParamiv(const EffectProps*, ALenum param, int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated integer-vector property 0x%04x",
        param};
}
void DedicatedDialog_getParamf(const EffectProps *props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_DEDICATED_GAIN:
        *val = props->DedicatedDialog.Gain;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated float property 0x%04x", param};
    }
}
void DedicatedDialog_getParamfv(const EffectProps *props, ALenum param, float *vals)
{ DedicatedDialog_getParamf(props, param, vals); }


void DedicatedLfe_setParami(EffectProps*, ALenum param, int)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated integer property 0x%04x", param}; }
void DedicatedLfe_setParamiv(EffectProps*, ALenum param, const int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated integer-vector property 0x%04x",
        param};
}
void DedicatedLfe_setParamf(EffectProps *props, ALenum param, float val)
{
    switch(param)
    {
    case AL_DEDICATED_GAIN:
        if(!(val >= 0.0f && std::isfinite(val)))
            throw effect_exception{AL_INVALID_VALUE, "Dedicated gain out of range"};
        props->DedicatedLfe.Gain = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated float property 0x%04x", param};
    }
}
void DedicatedLfe_setParamfv(EffectProps *props, ALenum param, const float *vals)
{ DedicatedLfe_setParamf(props, param, vals[0]); }

void DedicatedLfe_getParami(const EffectProps*, ALenum param, int*)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated integer property 0x%04x", param}; }
void DedicatedLfe_getParamiv(const EffectProps*, ALenum param, int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated integer-vector property 0x%04x",
        param};
}
void DedicatedLfe_getParamf(const EffectProps *props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_DEDICATED_GAIN:
        *val = props->DedicatedLfe.Gain;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid dedicated float property 0x%04x", param};
    }
}
void DedicatedLfe_getParamfv(const EffectProps *props, ALenum param, float *vals)
{ DedicatedLfe_getParamf(props, param, vals); }


EffectProps genDefaultDialogProps() noexcept
{
    EffectProps props{};
    props.DedicatedDialog.Gain = 1.0f;
    return props;
}

EffectProps genDefaultLfeProps() noexcept
{
    EffectProps props{};
    props.DedicatedLfe.Gain = 1.0f;
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(DedicatedDialog);

const EffectProps DedicatedDialogEffectProps{genDefaultDialogProps()};

DEFINE_ALEFFECT_VTABLE(DedicatedLfe);

const EffectProps DedicatedLfeEffectProps{genDefaultLfeProps()};
