
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

class EaxEchoEffectException : public EaxException
{
public:
    explicit EaxEchoEffectException(const char* message)
        : EaxException{"EAX_ECHO_EFFECT", message}
    {}
}; // EaxEchoEffectException

class EaxEchoEffect final : public EaxEffect4<EaxEchoEffectException, EAXECHOPROPERTIES>
{
public:
    EaxEchoEffect(const EaxCall& call);

private:
    struct DelayValidator {
        void operator()(float flDelay) const
        {
            eax_validate_range<Exception>(
                "Delay",
                flDelay,
                EAXECHO_MINDELAY,
                EAXECHO_MAXDELAY);
        }
    }; // DelayValidator

    struct LrDelayValidator {
        void operator()(float flLRDelay) const
        {
            eax_validate_range<Exception>(
                "LR Delay",
                flLRDelay,
                EAXECHO_MINLRDELAY,
                EAXECHO_MAXLRDELAY);
        }
    }; // LrDelayValidator

    struct DampingValidator {
        void operator()(float flDamping) const
        {
            eax_validate_range<Exception>(
                "Damping",
                flDamping,
                EAXECHO_MINDAMPING,
                EAXECHO_MAXDAMPING);
        }
    }; // DampingValidator

    struct FeedbackValidator {
        void operator()(float flFeedback) const
        {
            eax_validate_range<Exception>(
                "Feedback",
                flFeedback,
                EAXECHO_MINFEEDBACK,
                EAXECHO_MAXFEEDBACK);
        }
    }; // FeedbackValidator

    struct SpreadValidator {
        void operator()(float flSpread) const
        {
            eax_validate_range<Exception>(
                "Spread",
                flSpread,
                EAXECHO_MINSPREAD,
                EAXECHO_MAXSPREAD);
        }
    }; // SpreadValidator

    struct AllValidator {
        void operator()(const Props& all) const
        {
            DelayValidator{}(all.flDelay);
            LrDelayValidator{}(all.flLRDelay);
            DampingValidator{}(all.flDamping);
            FeedbackValidator{}(all.flFeedback);
            SpreadValidator{}(all.flSpread);
        }
    }; // AllValidator

    void set_defaults(Props& props) override;

    void set_efx_delay() noexcept;
    void set_efx_lr_delay() noexcept;
    void set_efx_damping() noexcept;
    void set_efx_feedback() noexcept;
    void set_efx_spread() noexcept;
    void set_efx_defaults() override;

    void get(const EaxCall& call, const Props& props) override;
    void set(const EaxCall& call, Props& props) override;
    bool commit_props(const Props& props) override;
}; // EaxEchoEffect

EaxEchoEffect::EaxEchoEffect(const EaxCall& call)
    : EaxEffect4{AL_EFFECT_ECHO, call}
{}

void EaxEchoEffect::set_defaults(Props& props)
{
    props.flDelay = EAXECHO_DEFAULTDELAY;
    props.flLRDelay = EAXECHO_DEFAULTLRDELAY;
    props.flDamping = EAXECHO_DEFAULTDAMPING;
    props.flFeedback = EAXECHO_DEFAULTFEEDBACK;
    props.flSpread = EAXECHO_DEFAULTSPREAD;
}

void EaxEchoEffect::set_efx_delay() noexcept
{
    al_effect_props_.Echo.Delay = clamp(
        props_.flDelay,
        AL_ECHO_MIN_DELAY,
        AL_ECHO_MAX_DELAY);
}

void EaxEchoEffect::set_efx_lr_delay() noexcept
{
    al_effect_props_.Echo.LRDelay = clamp(
        props_.flLRDelay,
        AL_ECHO_MIN_LRDELAY,
        AL_ECHO_MAX_LRDELAY);
}

void EaxEchoEffect::set_efx_damping() noexcept
{
    al_effect_props_.Echo.Damping = clamp(
        props_.flDamping,
        AL_ECHO_MIN_DAMPING,
        AL_ECHO_MAX_DAMPING);
}

void EaxEchoEffect::set_efx_feedback() noexcept
{
    al_effect_props_.Echo.Feedback = clamp(
        props_.flFeedback,
        AL_ECHO_MIN_FEEDBACK,
        AL_ECHO_MAX_FEEDBACK);
}

void EaxEchoEffect::set_efx_spread() noexcept
{
    al_effect_props_.Echo.Spread = clamp(
        props_.flSpread,
        AL_ECHO_MIN_SPREAD,
        AL_ECHO_MAX_SPREAD);
}

void EaxEchoEffect::set_efx_defaults()
{
    set_efx_delay();
    set_efx_lr_delay();
    set_efx_damping();
    set_efx_feedback();
    set_efx_spread();
}

void EaxEchoEffect::get(const EaxCall& call, const Props& props)
{
    switch(call.get_property_id())
    {
        case EAXECHO_NONE: break;
        case EAXECHO_ALLPARAMETERS: call.set_value<Exception>(props); break;
        case EAXECHO_DELAY: call.set_value<Exception>(props.flDelay); break;
        case EAXECHO_LRDELAY: call.set_value<Exception>(props.flLRDelay); break;
        case EAXECHO_DAMPING: call.set_value<Exception>(props.flDamping); break;
        case EAXECHO_FEEDBACK: call.set_value<Exception>(props.flFeedback); break;
        case EAXECHO_SPREAD: call.set_value<Exception>(props.flSpread); break;
        default: fail_unknown_property_id();
    }
}

void EaxEchoEffect::set(const EaxCall& call, Props& props)
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

bool EaxEchoEffect::commit_props(const Props& props)
{
    auto is_dirty = false;

    if (props_.flDelay != props.flDelay)
    {
        is_dirty = true;
        set_efx_delay();
    }

    if (props_.flLRDelay != props.flLRDelay)
    {
        is_dirty = true;
        set_efx_lr_delay();
    }

    if (props_.flDamping != props.flDamping)
    {
        is_dirty = true;
        set_efx_damping();
    }

    if (props_.flFeedback != props.flFeedback)
    {
        is_dirty = true;
        set_efx_feedback();
    }

    if (props_.flSpread != props.flSpread)
    {
        is_dirty = true;
        set_efx_spread();
    }

    return is_dirty;
}

} // namespace

EaxEffectUPtr eax_create_eax_echo_effect(const EaxCall& call)
{
    return eax_create_eax4_effect<EaxEchoEffect>(call);
}

#endif // ALSOFT_EAX
