
#include "config.h"

#include "AL/al.h"
#include "AL/efx.h"

#include "alc/context.h"
#include "alnumeric.h"
#include "effects.h"

#if ALSOFT_EAX
#include "al/eax/effect.h"
#include "al/eax/exception.h"
#include "al/eax/utils.h"
#endif // ALSOFT_EAX


namespace {

static_assert(EchoMaxDelay >= AL_ECHO_MAX_DELAY, "Echo max delay too short");
static_assert(EchoMaxLRDelay >= AL_ECHO_MAX_LRDELAY, "Echo max left-right delay too short");

consteval auto genDefaultProps() noexcept -> EffectProps
{
    return EchoProps{
        .Delay    = AL_ECHO_DEFAULT_DELAY,
        .LRDelay  = AL_ECHO_DEFAULT_LRDELAY,
        .Damping  = AL_ECHO_DEFAULT_DAMPING,
        .Feedback = AL_ECHO_DEFAULT_FEEDBACK,
        .Spread   = AL_ECHO_DEFAULT_SPREAD};
}

} // namespace

constinit const EffectProps EchoEffectProps(genDefaultProps());

void EchoEffectHandler::SetParami(al::Context *context, EchoProps&, ALenum param, int)
{ context->throw_error(AL_INVALID_ENUM, "Invalid echo integer property {:#04x}", as_unsigned(param)); }
void EchoEffectHandler::SetParamiv(al::Context *context, EchoProps&, ALenum param, const int*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid echo integer-vector property {:#04x}", as_unsigned(param)); }
void EchoEffectHandler::SetParamf(al::Context *context, EchoProps &props, ALenum param, float val)
{
    switch(param)
    {
    case AL_ECHO_DELAY:
        if(!(val >= AL_ECHO_MIN_DELAY && val <= AL_ECHO_MAX_DELAY))
            context->throw_error(AL_INVALID_VALUE, "Echo delay out of range");
        props.Delay = val;
        return;

    case AL_ECHO_LRDELAY:
        if(!(val >= AL_ECHO_MIN_LRDELAY && val <= AL_ECHO_MAX_LRDELAY))
            context->throw_error(AL_INVALID_VALUE, "Echo LR delay out of range");
        props.LRDelay = val;
        return;

    case AL_ECHO_DAMPING:
        if(!(val >= AL_ECHO_MIN_DAMPING && val <= AL_ECHO_MAX_DAMPING))
            context->throw_error(AL_INVALID_VALUE, "Echo damping out of range");
        props.Damping = val;
        return;

    case AL_ECHO_FEEDBACK:
        if(!(val >= AL_ECHO_MIN_FEEDBACK && val <= AL_ECHO_MAX_FEEDBACK))
            context->throw_error(AL_INVALID_VALUE, "Echo feedback out of range");
        props.Feedback = val;
        return;

    case AL_ECHO_SPREAD:
        if(!(val >= AL_ECHO_MIN_SPREAD && val <= AL_ECHO_MAX_SPREAD))
            context->throw_error(AL_INVALID_VALUE, "Echo spread out of range");
        props.Spread = val;
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid echo float property {:#04x}",
        as_unsigned(param));
}
void EchoEffectHandler::SetParamfv(al::Context *context, EchoProps &props, ALenum param, const float *vals)
{ SetParamf(context, props, param, *vals); }

void EchoEffectHandler::GetParami(al::Context *context, const EchoProps&, ALenum param, int*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid echo integer property {:#04x}", as_unsigned(param)); }
void EchoEffectHandler::GetParamiv(al::Context *context, const EchoProps&, ALenum param, int*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid echo integer-vector property {:#04x}", as_unsigned(param)); }
void EchoEffectHandler::GetParamf(al::Context *context, const EchoProps &props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_ECHO_DELAY: *val = props.Delay; return;
    case AL_ECHO_LRDELAY: *val = props.LRDelay; return;
    case AL_ECHO_DAMPING: *val = props.Damping; return;
    case AL_ECHO_FEEDBACK: *val = props.Feedback; return;
    case AL_ECHO_SPREAD: *val = props.Spread; return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid echo float property {:#04x}",
        as_unsigned(param));
}
void EchoEffectHandler::GetParamfv(al::Context *context, const EchoProps &props, ALenum param, float *vals)
{ GetParamf(context, props, param, vals); }


#if ALSOFT_EAX
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

template<> /* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
struct EchoCommitter::Exception : public EaxException {
    explicit Exception(const std::string_view message) : EaxException{"EAX_ECHO_EFFECT", message}
    { }
};

template<> [[noreturn]]
void EchoCommitter::fail(const std::string_view message)
{ throw Exception{message}; }

auto EaxEchoCommitter::commit(const EAXECHOPROPERTIES &props) const -> bool
{
    if(auto *cur = std::get_if<EAXECHOPROPERTIES>(&mEaxProps); cur && *cur == props)
        return false;

    mEaxProps = props;
    mAlProps = EchoProps{
        .Delay = props.flDelay,
        .LRDelay = props.flLRDelay,
        .Damping = props.flDamping,
        .Feedback = props.flFeedback,
        .Spread = props.flSpread};

    return true;
}

void EaxEchoCommitter::SetDefaults(EaxEffectProps &props)
{
    props = EAXECHOPROPERTIES{
        .flDelay = EAXECHO_DEFAULTDELAY,
        .flLRDelay = EAXECHO_DEFAULTLRDELAY,
        .flDamping = EAXECHO_DEFAULTDAMPING,
        .flFeedback = EAXECHO_DEFAULTFEEDBACK,
        .flSpread = EAXECHO_DEFAULTSPREAD};
}

void EaxEchoCommitter::Get(const EaxCall &call, const EAXECHOPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case EAXECHO_NONE: break;
    case EAXECHO_ALLPARAMETERS: call.store(props); break;
    case EAXECHO_DELAY: call.store(props.flDelay); break;
    case EAXECHO_LRDELAY: call.store(props.flLRDelay); break;
    case EAXECHO_DAMPING: call.store(props.flDamping); break;
    case EAXECHO_FEEDBACK: call.store(props.flFeedback); break;
    case EAXECHO_SPREAD: call.store(props.flSpread); break;
    default: fail_unknown_property_id();
    }
}

void EaxEchoCommitter::Set(const EaxCall &call, EAXECHOPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case EAXECHO_NONE: break;
    case EAXECHO_ALLPARAMETERS: defer<AllValidator>(call, props); break;
    case EAXECHO_DELAY: defer<DelayValidator>(call, props.flDelay); break;
    case EAXECHO_LRDELAY: defer<LrDelayValidator>(call, props.flLRDelay); break;
    case EAXECHO_DAMPING: defer<DampingValidator>(call, props.flDamping); break;
    case EAXECHO_FEEDBACK: defer<FeedbackValidator>(call, props.flFeedback); break;
    case EAXECHO_SPREAD: defer<SpreadValidator>(call, props.flSpread); break;
    default: fail_unknown_property_id();
    }
}

#endif // ALSOFT_EAX
