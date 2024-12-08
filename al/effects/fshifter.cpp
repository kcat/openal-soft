
#include "config.h"

#include <optional>
#include <stdexcept>

#include "AL/al.h"
#include "AL/efx.h"

#include "alc/context.h"
#include "alnumeric.h"
#include "effects.h"

#if ALSOFT_EAX
#include <cassert>

#include "al/eax/effect.h"
#include "al/eax/exception.h"
#include "al/eax/utils.h"
#endif // ALSOFT_EAX


namespace {

constexpr std::optional<FShifterDirection> DirectionFromEmum(ALenum value) noexcept
{
    switch(value)
    {
    case AL_FREQUENCY_SHIFTER_DIRECTION_DOWN: return FShifterDirection::Down;
    case AL_FREQUENCY_SHIFTER_DIRECTION_UP: return FShifterDirection::Up;
    case AL_FREQUENCY_SHIFTER_DIRECTION_OFF: return FShifterDirection::Off;
    }
    return std::nullopt;
}
constexpr ALenum EnumFromDirection(FShifterDirection dir)
{
    switch(dir)
    {
    case FShifterDirection::Down: return AL_FREQUENCY_SHIFTER_DIRECTION_DOWN;
    case FShifterDirection::Up: return AL_FREQUENCY_SHIFTER_DIRECTION_UP;
    case FShifterDirection::Off: return AL_FREQUENCY_SHIFTER_DIRECTION_OFF;
    }
    throw std::runtime_error{fmt::format("Invalid direction: {}", int{al::to_underlying(dir)})};
}

constexpr EffectProps genDefaultProps() noexcept
{
    FshifterProps props{};
    props.Frequency      = AL_FREQUENCY_SHIFTER_DEFAULT_FREQUENCY;
    props.LeftDirection  = DirectionFromEmum(AL_FREQUENCY_SHIFTER_DEFAULT_LEFT_DIRECTION).value();
    props.RightDirection = DirectionFromEmum(AL_FREQUENCY_SHIFTER_DEFAULT_RIGHT_DIRECTION).value();
    return props;
}

} // namespace

const EffectProps FshifterEffectProps{genDefaultProps()};

void FshifterEffectHandler::SetParami(ALCcontext *context, FshifterProps &props, ALenum param, int val)
{
    switch(param)
    {
    case AL_FREQUENCY_SHIFTER_LEFT_DIRECTION:
        if(auto diropt = DirectionFromEmum(val))
            props.LeftDirection = *diropt;
        else
            context->throw_error(AL_INVALID_VALUE,
                "Unsupported frequency shifter left direction: {:#04x}", as_unsigned(val));
        return;

    case AL_FREQUENCY_SHIFTER_RIGHT_DIRECTION:
        if(auto diropt = DirectionFromEmum(val))
            props.RightDirection = *diropt;
        else
            context->throw_error(AL_INVALID_VALUE,
                "Unsupported frequency shifter right direction: {:#04x}", as_unsigned(val));
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid frequency shifter integer property {:#04x}",
        as_unsigned(param));
}
void FshifterEffectHandler::SetParamiv(ALCcontext *context, FshifterProps &props, ALenum param, const int *vals)
{ SetParami(context, props, param, *vals); }

void FshifterEffectHandler::SetParamf(ALCcontext *context, FshifterProps &props, ALenum param, float val)
{
    switch(param)
    {
    case AL_FREQUENCY_SHIFTER_FREQUENCY:
        if(!(val >= AL_FREQUENCY_SHIFTER_MIN_FREQUENCY && val <= AL_FREQUENCY_SHIFTER_MAX_FREQUENCY))
            context->throw_error(AL_INVALID_VALUE, "Frequency shifter frequency out of range");
        props.Frequency = val;
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid frequency shifter float property {:#04x}",
        as_unsigned(param));
}
void FshifterEffectHandler::SetParamfv(ALCcontext *context, FshifterProps &props, ALenum param, const float *vals)
{ SetParamf(context, props, param, *vals); }

void FshifterEffectHandler::GetParami(ALCcontext *context, const FshifterProps &props, ALenum param, int *val)
{
    switch(param)
    {
    case AL_FREQUENCY_SHIFTER_LEFT_DIRECTION:
        *val = EnumFromDirection(props.LeftDirection);
        return;
    case AL_FREQUENCY_SHIFTER_RIGHT_DIRECTION:
        *val = EnumFromDirection(props.RightDirection);
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid frequency shifter integer property {:#04x}",
        as_unsigned(param));
}
void FshifterEffectHandler::GetParamiv(ALCcontext *context, const FshifterProps &props, ALenum param, int *vals)
{ GetParami(context, props, param, vals); }

void FshifterEffectHandler::GetParamf(ALCcontext *context, const FshifterProps &props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_FREQUENCY_SHIFTER_FREQUENCY: *val = props.Frequency; return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid frequency shifter float property {:#04x}",
        as_unsigned(param));
}
void FshifterEffectHandler::GetParamfv(ALCcontext *context, const FshifterProps &props, ALenum param, float *vals)
{ GetParamf(context, props, param, vals); }


#if ALSOFT_EAX
namespace {

using FrequencyShifterCommitter = EaxCommitter<EaxFrequencyShifterCommitter>;

struct FrequencyValidator {
    void operator()(float flFrequency) const
    {
        eax_validate_range<FrequencyShifterCommitter::Exception>(
            "Frequency",
            flFrequency,
            EAXFREQUENCYSHIFTER_MINFREQUENCY,
            EAXFREQUENCYSHIFTER_MAXFREQUENCY);
    }
}; // FrequencyValidator

struct LeftDirectionValidator {
    void operator()(unsigned long ulLeftDirection) const
    {
        eax_validate_range<FrequencyShifterCommitter::Exception>(
            "Left Direction",
            ulLeftDirection,
            EAXFREQUENCYSHIFTER_MINLEFTDIRECTION,
            EAXFREQUENCYSHIFTER_MAXLEFTDIRECTION);
    }
}; // LeftDirectionValidator

struct RightDirectionValidator {
    void operator()(unsigned long ulRightDirection) const
    {
        eax_validate_range<FrequencyShifterCommitter::Exception>(
            "Right Direction",
            ulRightDirection,
            EAXFREQUENCYSHIFTER_MINRIGHTDIRECTION,
            EAXFREQUENCYSHIFTER_MAXRIGHTDIRECTION);
    }
}; // RightDirectionValidator

struct AllValidator {
    void operator()(const EAXFREQUENCYSHIFTERPROPERTIES& all) const
    {
        FrequencyValidator{}(all.flFrequency);
        LeftDirectionValidator{}(all.ulLeftDirection);
        RightDirectionValidator{}(all.ulRightDirection);
    }
}; // AllValidator

} // namespace

template<>
struct FrequencyShifterCommitter::Exception : public EaxException {
    explicit Exception(const char *message) : EaxException{"EAX_FREQUENCY_SHIFTER_EFFECT", message}
    { }
};

template<>
[[noreturn]] void FrequencyShifterCommitter::fail(const char *message)
{
    throw Exception{message};
}

bool EaxFrequencyShifterCommitter::commit(const EAXFREQUENCYSHIFTERPROPERTIES &props)
{
    if(auto *cur = std::get_if<EAXFREQUENCYSHIFTERPROPERTIES>(&mEaxProps); cur && *cur == props)
        return false;

    mEaxProps = props;

    auto get_direction = [](unsigned long dir) noexcept
    {
        if(dir == EAX_FREQUENCYSHIFTER_DOWN)
            return FShifterDirection::Down;
        if(dir == EAX_FREQUENCYSHIFTER_UP)
            return FShifterDirection::Up;
        return FShifterDirection::Off;
    };

    mAlProps = [&]{
        FshifterProps ret{};
        ret.Frequency = props.flFrequency;
        ret.LeftDirection = get_direction(props.ulLeftDirection);
        ret.RightDirection = get_direction(props.ulRightDirection);
        return ret;
    }();

    return true;
}

void EaxFrequencyShifterCommitter::SetDefaults(EaxEffectProps &props)
{
    static constexpr EAXFREQUENCYSHIFTERPROPERTIES defprops{[]
    {
        EAXFREQUENCYSHIFTERPROPERTIES ret{};
        ret.flFrequency = EAXFREQUENCYSHIFTER_DEFAULTFREQUENCY;
        ret.ulLeftDirection = EAXFREQUENCYSHIFTER_DEFAULTLEFTDIRECTION;
        ret.ulRightDirection = EAXFREQUENCYSHIFTER_DEFAULTRIGHTDIRECTION;
        return ret;
    }()};
    props = defprops;
}

void EaxFrequencyShifterCommitter::Get(const EaxCall &call, const EAXFREQUENCYSHIFTERPROPERTIES &props)
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

void EaxFrequencyShifterCommitter::Set(const EaxCall &call, EAXFREQUENCYSHIFTERPROPERTIES &props)
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

#endif // ALSOFT_EAX
