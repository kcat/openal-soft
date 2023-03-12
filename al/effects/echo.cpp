
#include "config.h"

#include "AL/al.h"
#include "AL/efx.h"

#include "alc/effects/base.h"
#include "effects.h"

#ifdef ALSOFT_EAX
#include "alnumeric.h"
#include "al/eax/exception.h"
#include "al/eax/utils.h"
#endif // ALSOFT_EAX


namespace {

static_assert(EchoMaxDelay >= AL_ECHO_MAX_DELAY, "Echo max delay too short");
static_assert(EchoMaxLRDelay >= AL_ECHO_MAX_LRDELAY, "Echo max left-right delay too short");

void Echo_setParami(EffectProps*, ALenum param, int)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid echo integer property 0x%04x", param}; }
void Echo_setParamiv(EffectProps*, ALenum param, const int*)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid echo integer-vector property 0x%04x", param}; }
void Echo_setParamf(EffectProps *props, ALenum param, float val)
{
    switch(param)
    {
    case AL_ECHO_DELAY:
        if(!(val >= AL_ECHO_MIN_DELAY && val <= AL_ECHO_MAX_DELAY))
            throw effect_exception{AL_INVALID_VALUE, "Echo delay out of range"};
        props->Echo.Delay = val;
        break;

    case AL_ECHO_LRDELAY:
        if(!(val >= AL_ECHO_MIN_LRDELAY && val <= AL_ECHO_MAX_LRDELAY))
            throw effect_exception{AL_INVALID_VALUE, "Echo LR delay out of range"};
        props->Echo.LRDelay = val;
        break;

    case AL_ECHO_DAMPING:
        if(!(val >= AL_ECHO_MIN_DAMPING && val <= AL_ECHO_MAX_DAMPING))
            throw effect_exception{AL_INVALID_VALUE, "Echo damping out of range"};
        props->Echo.Damping = val;
        break;

    case AL_ECHO_FEEDBACK:
        if(!(val >= AL_ECHO_MIN_FEEDBACK && val <= AL_ECHO_MAX_FEEDBACK))
            throw effect_exception{AL_INVALID_VALUE, "Echo feedback out of range"};
        props->Echo.Feedback = val;
        break;

    case AL_ECHO_SPREAD:
        if(!(val >= AL_ECHO_MIN_SPREAD && val <= AL_ECHO_MAX_SPREAD))
            throw effect_exception{AL_INVALID_VALUE, "Echo spread out of range"};
        props->Echo.Spread = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid echo float property 0x%04x", param};
    }
}
void Echo_setParamfv(EffectProps *props, ALenum param, const float *vals)
{ Echo_setParamf(props, param, vals[0]); }

void Echo_getParami(const EffectProps*, ALenum param, int*)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid echo integer property 0x%04x", param}; }
void Echo_getParamiv(const EffectProps*, ALenum param, int*)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid echo integer-vector property 0x%04x", param}; }
void Echo_getParamf(const EffectProps *props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_ECHO_DELAY:
        *val = props->Echo.Delay;
        break;

    case AL_ECHO_LRDELAY:
        *val = props->Echo.LRDelay;
        break;

    case AL_ECHO_DAMPING:
        *val = props->Echo.Damping;
        break;

    case AL_ECHO_FEEDBACK:
        *val = props->Echo.Feedback;
        break;

    case AL_ECHO_SPREAD:
        *val = props->Echo.Spread;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid echo float property 0x%04x", param};
    }
}
void Echo_getParamfv(const EffectProps *props, ALenum param, float *vals)
{ Echo_getParamf(props, param, vals); }

EffectProps genDefaultProps() noexcept
{
    EffectProps props{};
    props.Echo.Delay    = AL_ECHO_DEFAULT_DELAY;
    props.Echo.LRDelay  = AL_ECHO_DEFAULT_LRDELAY;
    props.Echo.Damping  = AL_ECHO_DEFAULT_DAMPING;
    props.Echo.Feedback = AL_ECHO_DEFAULT_FEEDBACK;
    props.Echo.Spread   = AL_ECHO_DEFAULT_SPREAD;
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Echo);

const EffectProps EchoEffectProps{genDefaultProps()};

#ifdef ALSOFT_EAX
namespace {

using EchoCommitter = EaxCommitter<EaxEchoCommitter>;

struct DelayValidator {
    void operator()(float flDelay) const
    {
        eax_validate_range<EchoCommitter::Exception>(
            "Delay",
            flDelay,
            EAXECHO_MINDELAY,
            EAXECHO_MAXDELAY);
    }
}; // DelayValidator

struct LrDelayValidator {
    void operator()(float flLRDelay) const
    {
        eax_validate_range<EchoCommitter::Exception>(
            "LR Delay",
            flLRDelay,
            EAXECHO_MINLRDELAY,
            EAXECHO_MAXLRDELAY);
    }
}; // LrDelayValidator

struct DampingValidator {
    void operator()(float flDamping) const
    {
        eax_validate_range<EchoCommitter::Exception>(
            "Damping",
            flDamping,
            EAXECHO_MINDAMPING,
            EAXECHO_MAXDAMPING);
    }
}; // DampingValidator

struct FeedbackValidator {
    void operator()(float flFeedback) const
    {
        eax_validate_range<EchoCommitter::Exception>(
            "Feedback",
            flFeedback,
            EAXECHO_MINFEEDBACK,
            EAXECHO_MAXFEEDBACK);
    }
}; // FeedbackValidator

struct SpreadValidator {
    void operator()(float flSpread) const
    {
        eax_validate_range<EchoCommitter::Exception>(
            "Spread",
            flSpread,
            EAXECHO_MINSPREAD,
            EAXECHO_MAXSPREAD);
    }
}; // SpreadValidator

struct AllValidator {
    void operator()(const EAXECHOPROPERTIES& all) const
    {
        DelayValidator{}(all.flDelay);
        LrDelayValidator{}(all.flLRDelay);
        DampingValidator{}(all.flDamping);
        FeedbackValidator{}(all.flFeedback);
        SpreadValidator{}(all.flSpread);
    }
}; // AllValidator

} // namespace

template<>
struct EchoCommitter::Exception : public EaxException {
    explicit Exception(const char* message) : EaxException{"EAX_ECHO_EFFECT", message}
    { }
};

template<>
[[noreturn]] void EchoCommitter::fail(const char *message)
{
    throw Exception{message};
}

template<>
bool EchoCommitter::commit(const EaxEffectProps &props)
{
    if(props.mType == mEaxProps.mType && mEaxProps.mEcho.flDelay == props.mEcho.flDelay
        && mEaxProps.mEcho.flLRDelay == props.mEcho.flLRDelay
        && mEaxProps.mEcho.flDamping == props.mEcho.flDamping
        && mEaxProps.mEcho.flFeedback == props.mEcho.flFeedback
        && mEaxProps.mEcho.flSpread == props.mEcho.flSpread)
        return false;

    mEaxProps = props;

    mAlProps.Echo.Delay = props.mEcho.flDelay;
    mAlProps.Echo.LRDelay = props.mEcho.flLRDelay;
    mAlProps.Echo.Damping = props.mEcho.flDamping;
    mAlProps.Echo.Feedback = props.mEcho.flFeedback;
    mAlProps.Echo.Spread = props.mEcho.flSpread;

    return true;
}

template<>
void EchoCommitter::SetDefaults(EaxEffectProps &props)
{
    props.mType = EaxEffectType::Echo;
    props.mEcho.flDelay = EAXECHO_DEFAULTDELAY;
    props.mEcho.flLRDelay = EAXECHO_DEFAULTLRDELAY;
    props.mEcho.flDamping = EAXECHO_DEFAULTDAMPING;
    props.mEcho.flFeedback = EAXECHO_DEFAULTFEEDBACK;
    props.mEcho.flSpread = EAXECHO_DEFAULTSPREAD;
}

template<>
void EchoCommitter::Get(const EaxCall &call, const EaxEffectProps &props)
{
    switch(call.get_property_id())
    {
    case EAXECHO_NONE: break;
    case EAXECHO_ALLPARAMETERS: call.set_value<Exception>(props.mEcho); break;
    case EAXECHO_DELAY: call.set_value<Exception>(props.mEcho.flDelay); break;
    case EAXECHO_LRDELAY: call.set_value<Exception>(props.mEcho.flLRDelay); break;
    case EAXECHO_DAMPING: call.set_value<Exception>(props.mEcho.flDamping); break;
    case EAXECHO_FEEDBACK: call.set_value<Exception>(props.mEcho.flFeedback); break;
    case EAXECHO_SPREAD: call.set_value<Exception>(props.mEcho.flSpread); break;
    default: fail_unknown_property_id();
    }
}

template<>
void EchoCommitter::Set(const EaxCall &call, EaxEffectProps &props)
{
    switch(call.get_property_id())
    {
    case EAXECHO_NONE: break;
    case EAXECHO_ALLPARAMETERS: defer<AllValidator>(call, props.mEcho); break;
    case EAXECHO_DELAY: defer<DelayValidator>(call, props.mEcho.flDelay); break;
    case EAXECHO_LRDELAY: defer<LrDelayValidator>(call, props.mEcho.flLRDelay); break;
    case EAXECHO_DAMPING: defer<DampingValidator>(call, props.mEcho.flDamping); break;
    case EAXECHO_FEEDBACK: defer<FeedbackValidator>(call, props.mEcho.flFeedback); break;
    case EAXECHO_SPREAD: defer<SpreadValidator>(call, props.mEcho.flSpread); break;
    default: fail_unknown_property_id();
    }
}

#endif // ALSOFT_EAX
