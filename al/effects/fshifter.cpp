
#include "config.h"

#include <stdexcept>

#include "AL/al.h"
#include "AL/efx.h"

#include "alc/effects/base.h"
#include "aloptional.h"
#include "effects.h"

#ifdef ALSOFT_EAX
#include <cassert>
#include "alnumeric.h"
#include "al/eax/exception.h"
#include "al/eax/utils.h"
#endif // ALSOFT_EAX


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

#ifdef ALSOFT_EAX
namespace {

class EaxFrequencyShifterEffectException : public EaxException
{
public:
    explicit EaxFrequencyShifterEffectException(const char* message)
        : EaxException{"EAX_FREQUENCY_SHIFTER_EFFECT", message}
    {}
}; // EaxFrequencyShifterEffectException

class EaxFrequencyShifterEffect final : public EaxEffect4<EaxFrequencyShifterEffectException, EAXFREQUENCYSHIFTERPROPERTIES> {
public:
    EaxFrequencyShifterEffect(int eax_version);

private:
    struct FrequencyValidator {
        void operator()(float flFrequency) const
        {
            eax_validate_range<Exception>(
                "Frequency",
                flFrequency,
                EAXFREQUENCYSHIFTER_MINFREQUENCY,
                EAXFREQUENCYSHIFTER_MAXFREQUENCY);
        }
    }; // FrequencyValidator

    struct LeftDirectionValidator {
        void operator()(unsigned long ulLeftDirection) const
        {
            eax_validate_range<Exception>(
                "Left Direction",
                ulLeftDirection,
                EAXFREQUENCYSHIFTER_MINLEFTDIRECTION,
                EAXFREQUENCYSHIFTER_MAXLEFTDIRECTION);
        }
    }; // LeftDirectionValidator

    struct RightDirectionValidator {
        void operator()(unsigned long ulRightDirection) const
        {
            eax_validate_range<Exception>(
                "Right Direction",
                ulRightDirection,
                EAXFREQUENCYSHIFTER_MINRIGHTDIRECTION,
                EAXFREQUENCYSHIFTER_MAXRIGHTDIRECTION);
        }
    }; // RightDirectionValidator

    struct AllValidator {
        void operator()(const Props& all) const
        {
            FrequencyValidator{}(all.flFrequency);
            LeftDirectionValidator{}(all.ulLeftDirection);
            RightDirectionValidator{}(all.ulRightDirection);
        }
    }; // AllValidator

    void set_defaults(Props& props) override;

    void set_efx_frequency() noexcept;
    void set_efx_left_direction();
    void set_efx_right_direction();
    void set_efx_defaults() override;

    void get(const EaxCall& call, const Props& props) override;
    void set(const EaxCall& call, Props& props) override;
    bool commit_props(const Props& props) override;
}; // EaxFrequencyShifterEffect


EaxFrequencyShifterEffect::EaxFrequencyShifterEffect(int eax_version)
    : EaxEffect4{AL_EFFECT_FREQUENCY_SHIFTER, eax_version}
{}

void EaxFrequencyShifterEffect::set_defaults(Props& props)
{
    props.flFrequency = EAXFREQUENCYSHIFTER_DEFAULTFREQUENCY;
    props.ulLeftDirection = EAXFREQUENCYSHIFTER_DEFAULTLEFTDIRECTION;
    props.ulRightDirection = EAXFREQUENCYSHIFTER_DEFAULTRIGHTDIRECTION;
}

void EaxFrequencyShifterEffect::set_efx_frequency() noexcept
{
    al_effect_props_.Fshifter.Frequency = clamp(
        props_.flFrequency,
        AL_FREQUENCY_SHIFTER_MIN_FREQUENCY,
        AL_FREQUENCY_SHIFTER_MAX_FREQUENCY);
}

void EaxFrequencyShifterEffect::set_efx_left_direction()
{
    const auto left_direction = clamp(
        static_cast<ALint>(props_.ulLeftDirection),
        AL_FREQUENCY_SHIFTER_MIN_LEFT_DIRECTION,
        AL_FREQUENCY_SHIFTER_MAX_LEFT_DIRECTION);
    const auto efx_left_direction = DirectionFromEmum(left_direction);
    assert(efx_left_direction.has_value());
    al_effect_props_.Fshifter.LeftDirection = *efx_left_direction;
}

void EaxFrequencyShifterEffect::set_efx_right_direction()
{
    const auto right_direction = clamp(
        static_cast<ALint>(props_.ulRightDirection),
        AL_FREQUENCY_SHIFTER_MIN_RIGHT_DIRECTION,
        AL_FREQUENCY_SHIFTER_MAX_RIGHT_DIRECTION);
    const auto efx_right_direction = DirectionFromEmum(right_direction);
    assert(efx_right_direction.has_value());
    al_effect_props_.Fshifter.RightDirection = *efx_right_direction;
}

void EaxFrequencyShifterEffect::set_efx_defaults()
{
    set_efx_frequency();
    set_efx_left_direction();
    set_efx_right_direction();
}

void EaxFrequencyShifterEffect::get(const EaxCall& call, const Props& props)
{
    switch(call.get_property_id())
    {
        case EAXFREQUENCYSHIFTER_NONE: break;
        case EAXFREQUENCYSHIFTER_ALLPARAMETERS: call.set_value<Exception>(props); break;
        case EAXFREQUENCYSHIFTER_FREQUENCY: call.set_value<Exception>(props.flFrequency); break;
        case EAXFREQUENCYSHIFTER_LEFTDIRECTION: call.set_value<Exception>(props.ulLeftDirection); break;
        case EAXFREQUENCYSHIFTER_RIGHTDIRECTION: call.set_value<Exception>(props.ulRightDirection); break;
        default: fail_unknown_property_id();
    }
}

void EaxFrequencyShifterEffect::set(const EaxCall& call, Props& props)
{
    switch(call.get_property_id())
    {
        case EAXFREQUENCYSHIFTER_NONE: break;
        case EAXFREQUENCYSHIFTER_ALLPARAMETERS: defer<AllValidator>(call, props); break;
        case EAXFREQUENCYSHIFTER_FREQUENCY: defer<FrequencyValidator>(call, props.flFrequency); break;
        case EAXFREQUENCYSHIFTER_LEFTDIRECTION: defer<LeftDirectionValidator>(call, props.ulLeftDirection); break;
        case EAXFREQUENCYSHIFTER_RIGHTDIRECTION: defer<RightDirectionValidator>(call, props.ulRightDirection); break;
        default: fail_unknown_property_id();
    }
}

bool EaxFrequencyShifterEffect::commit_props(const Props& props)
{
    auto is_dirty = false;

    if (props_.flFrequency != props.flFrequency)
    {
        is_dirty = true;
        set_efx_frequency();
    }

    if (props_.ulLeftDirection != props.ulLeftDirection)
    {
        is_dirty = true;
        set_efx_left_direction();
    }

    if (props_.ulRightDirection != props.ulRightDirection)
    {
        is_dirty = true;
        set_efx_right_direction();
    }

    return is_dirty;
}

} // namespace

EaxEffectUPtr eax_create_eax_frequency_shifter_effect(int eax_version)
{
    return eax_create_eax4_effect<EaxFrequencyShifterEffect>(eax_version);
}

#endif // ALSOFT_EAX
