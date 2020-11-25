
#include "config.h"

#include "AL/al.h"
#include "AL/efx.h"

#include "effects.h"
#include "effects/base.h"


namespace {

void Pshifter_setParamf(EffectProps*, ALenum param, float)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid pitch shifter float property 0x%04x", param}; }
void Pshifter_setParamfv(EffectProps*, ALenum param, const float*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid pitch shifter float-vector property 0x%04x",
        param};
}

void Pshifter_setParami(EffectProps *props, ALenum param, int val)
{
    switch(param)
    {
    case AL_PITCH_SHIFTER_COARSE_TUNE:
        if(!(val >= AL_PITCH_SHIFTER_MIN_COARSE_TUNE && val <= AL_PITCH_SHIFTER_MAX_COARSE_TUNE))
            throw effect_exception{AL_INVALID_VALUE, "Pitch shifter coarse tune out of range"};
        props->Pshifter.CoarseTune = val;
        break;

    case AL_PITCH_SHIFTER_FINE_TUNE:
        if(!(val >= AL_PITCH_SHIFTER_MIN_FINE_TUNE && val <= AL_PITCH_SHIFTER_MAX_FINE_TUNE))
            throw effect_exception{AL_INVALID_VALUE, "Pitch shifter fine tune out of range"};
        props->Pshifter.FineTune = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid pitch shifter integer property 0x%04x",
            param};
    }
}
void Pshifter_setParamiv(EffectProps *props, ALenum param, const int *vals)
{ Pshifter_setParami(props, param, vals[0]); }

void Pshifter_getParami(const EffectProps *props, ALenum param, int *val)
{
    switch(param)
    {
    case AL_PITCH_SHIFTER_COARSE_TUNE:
        *val = props->Pshifter.CoarseTune;
        break;
    case AL_PITCH_SHIFTER_FINE_TUNE:
        *val = props->Pshifter.FineTune;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid pitch shifter integer property 0x%04x",
            param};
    }
}
void Pshifter_getParamiv(const EffectProps *props, ALenum param, int *vals)
{ Pshifter_getParami(props, param, vals); }

void Pshifter_getParamf(const EffectProps*, ALenum param, float*)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid pitch shifter float property 0x%04x", param}; }
void Pshifter_getParamfv(const EffectProps*, ALenum param, float*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid pitch shifter float vector-property 0x%04x",
        param};
}

EffectProps genDefaultProps() noexcept
{
    EffectProps props{};
    props.Pshifter.CoarseTune = AL_PITCH_SHIFTER_DEFAULT_COARSE_TUNE;
    props.Pshifter.FineTune   = AL_PITCH_SHIFTER_DEFAULT_FINE_TUNE;
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Pshifter);

const EffectProps PshifterEffectProps{genDefaultProps()};
