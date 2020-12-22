
#include "config.h"

#include "AL/al.h"
#include "AL/efx.h"

#include "aloptional.h"
#include "effects.h"
#include "effects/base.h"


namespace {

al::optional<FShifterDirection> DirectionFromEmum(ALenum value)
{
    switch(value)
    {
    case AL_FREQUENCY_SHIFTER_DIRECTION_DOWN: return al::make_optional(FShifterDirection::Down);
    case AL_FREQUENCY_SHIFTER_DIRECTION_UP: return al::make_optional(FShifterDirection::Up);
    case AL_FREQUENCY_SHIFTER_DIRECTION_OFF: return al::make_optional(FShifterDirection::Off);
    }
    return al::nullopt;
}
ALenum EnumFromDirection(FShifterDirection dir)
{
    switch(dir)
    {
    case FShifterDirection::Down: return AL_FREQUENCY_SHIFTER_DIRECTION_DOWN;
    case FShifterDirection::Up: return AL_FREQUENCY_SHIFTER_DIRECTION_UP;
    case FShifterDirection::Off: return AL_FREQUENCY_SHIFTER_DIRECTION_OFF;
    }
    throw std::runtime_error{"Invalid direction: "+std::to_string(static_cast<int>(dir))};
}

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
        if(auto diropt = DirectionFromEmum(val))
            props->Fshifter.LeftDirection = *diropt;
        else
            throw effect_exception{AL_INVALID_VALUE,
                "Unsupported frequency shifter left direction: 0x%04x", val};
        break;

    case AL_FREQUENCY_SHIFTER_RIGHT_DIRECTION:
        if(auto diropt = DirectionFromEmum(val))
            props->Fshifter.RightDirection = *diropt;
        else
            throw effect_exception{AL_INVALID_VALUE,
                "Unsupported frequency shifter right direction: 0x%04x", val};
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
        *val = EnumFromDirection(props->Fshifter.LeftDirection);
        break;
    case AL_FREQUENCY_SHIFTER_RIGHT_DIRECTION:
        *val = EnumFromDirection(props->Fshifter.RightDirection);
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
    props.Fshifter.LeftDirection  = *DirectionFromEmum(AL_FREQUENCY_SHIFTER_DEFAULT_LEFT_DIRECTION);
    props.Fshifter.RightDirection = *DirectionFromEmum(AL_FREQUENCY_SHIFTER_DEFAULT_RIGHT_DIRECTION);
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Fshifter);

const EffectProps FshifterEffectProps{genDefaultProps()};
