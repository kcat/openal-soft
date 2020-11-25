
#include "config.h"

#include "AL/al.h"
#include "AL/efx.h"

#include "effects.h"
#include "effects/base.h"


namespace {

void Fshifter_setParamf(EffectProps *props, ALenum param, float val)
{
    switch(param)
    {
    case AL_FREQUENCY_SHIFTER_FREQUENCY:
        if(!(val >= AL_FREQUENCY_SHIFTER_MIN_FREQUENCY && val <= AL_FREQUENCY_SHIFTER_MAX_FREQUENCY))
            throw effect_exception{AL_INVALID_VALUE, "Frequency shifter frequency out of range"};
        props->Fshifter.Frequency = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid frequency shifter float property 0x%04x",
            param};
    }
}
void Fshifter_setParamfv(EffectProps *props, ALenum param, const float *vals)
{ Fshifter_setParamf(props, param, vals[0]); }

void Fshifter_setParami(EffectProps *props, ALenum param, int val)
{
    switch(param)
    {
    case AL_FREQUENCY_SHIFTER_LEFT_DIRECTION:
        if(!(val >= AL_FREQUENCY_SHIFTER_MIN_LEFT_DIRECTION && val <= AL_FREQUENCY_SHIFTER_MAX_LEFT_DIRECTION))
            throw effect_exception{AL_INVALID_VALUE,
                "Frequency shifter left direction out of range"};
        props->Fshifter.LeftDirection = val;
        break;

    case AL_FREQUENCY_SHIFTER_RIGHT_DIRECTION:
        if(!(val >= AL_FREQUENCY_SHIFTER_MIN_RIGHT_DIRECTION && val <= AL_FREQUENCY_SHIFTER_MAX_RIGHT_DIRECTION))
            throw effect_exception{AL_INVALID_VALUE,
                "Frequency shifter right direction out of range"};
        props->Fshifter.RightDirection = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM,
            "Invalid frequency shifter integer property 0x%04x", param};
    }
}
void Fshifter_setParamiv(EffectProps *props, ALenum param, const int *vals)
{ Fshifter_setParami(props, param, vals[0]); }

void Fshifter_getParami(const EffectProps *props, ALenum param, int *val)
{
    switch(param)
    {
    case AL_FREQUENCY_SHIFTER_LEFT_DIRECTION:
        *val = props->Fshifter.LeftDirection;
        break;
    case AL_FREQUENCY_SHIFTER_RIGHT_DIRECTION:
        *val = props->Fshifter.RightDirection;
        break;
    default:
        throw effect_exception{AL_INVALID_ENUM,
            "Invalid frequency shifter integer property 0x%04x", param};
    }
}
void Fshifter_getParamiv(const EffectProps *props, ALenum param, int *vals)
{ Fshifter_getParami(props, param, vals); }

void Fshifter_getParamf(const EffectProps *props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_FREQUENCY_SHIFTER_FREQUENCY:
        *val = props->Fshifter.Frequency;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid frequency shifter float property 0x%04x",
            param};
    }
}
void Fshifter_getParamfv(const EffectProps *props, ALenum param, float *vals)
{ Fshifter_getParamf(props, param, vals); }

EffectProps genDefaultProps() noexcept
{
    EffectProps props{};
    props.Fshifter.Frequency      = AL_FREQUENCY_SHIFTER_DEFAULT_FREQUENCY;
    props.Fshifter.LeftDirection  = AL_FREQUENCY_SHIFTER_DEFAULT_LEFT_DIRECTION;
    props.Fshifter.RightDirection = AL_FREQUENCY_SHIFTER_DEFAULT_RIGHT_DIRECTION;
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Fshifter);

const EffectProps FshifterEffectProps{genDefaultProps()};
