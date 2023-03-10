
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

class EaxEchoEffect final : public EaxEffect4<EaxEchoEffectException>
{
public:
    EaxEchoEffect(int eax_version);

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
        void operator()(const EAXECHOPROPERTIES& all) const
        {
            DelayValidator{}(all.flDelay);
            LrDelayValidator{}(all.flLRDelay);
            DampingValidator{}(all.flDamping);
            FeedbackValidator{}(all.flFeedback);
            SpreadValidator{}(all.flSpread);
        }
    }; // AllValidator

    void set_defaults(Props4& props) override;

    void set_efx_delay() noexcept;
    void set_efx_lr_delay() noexcept;
    void set_efx_damping() noexcept;
    void set_efx_feedback() noexcept;
    void set_efx_spread() noexcept;
    void set_efx_defaults() override;

    void get(const EaxCall& call, const Props4& props) override;
    void set(const EaxCall& call, Props4& props) override;
    bool commit_props(const Props4& props) override;
}; // EaxEchoEffect

EaxEchoEffect::EaxEchoEffect(int eax_version)
    : EaxEffect4{AL_EFFECT_ECHO, eax_version}
{}

void EaxEchoEffect::set_defaults(Props4& props)
{
    props.mType = EaxEffectType::Echo;
    props.mEcho.flDelay = EAXECHO_DEFAULTDELAY;
    props.mEcho.flLRDelay = EAXECHO_DEFAULTLRDELAY;
    props.mEcho.flDamping = EAXECHO_DEFAULTDAMPING;
    props.mEcho.flFeedback = EAXECHO_DEFAULTFEEDBACK;
    props.mEcho.flSpread = EAXECHO_DEFAULTSPREAD;
}

void EaxEchoEffect::set_efx_delay() noexcept
{
    al_effect_props_.Echo.Delay = clamp(
        props_.mEcho.flDelay,
        AL_ECHO_MIN_DELAY,
        AL_ECHO_MAX_DELAY);
}

void EaxEchoEffect::set_efx_lr_delay() noexcept
{
    al_effect_props_.Echo.LRDelay = clamp(
        props_.mEcho.flLRDelay,
        AL_ECHO_MIN_LRDELAY,
        AL_ECHO_MAX_LRDELAY);
}

void EaxEchoEffect::set_efx_damping() noexcept
{
    al_effect_props_.Echo.Damping = clamp(
        props_.mEcho.flDamping,
        AL_ECHO_MIN_DAMPING,
        AL_ECHO_MAX_DAMPING);
}

void EaxEchoEffect::set_efx_feedback() noexcept
{
    al_effect_props_.Echo.Feedback = clamp(
        props_.mEcho.flFeedback,
        AL_ECHO_MIN_FEEDBACK,
        AL_ECHO_MAX_FEEDBACK);
}

void EaxEchoEffect::set_efx_spread() noexcept
{
    al_effect_props_.Echo.Spread = clamp(
        props_.mEcho.flSpread,
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

void EaxEchoEffect::get(const EaxCall& call, const Props4& props)
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

void EaxEchoEffect::set(const EaxCall& call, Props4& props)
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

bool EaxEchoEffect::commit_props(const Props4& props)
{
    auto is_dirty = false;

    if (props_.mEcho.flDelay != props.mEcho.flDelay)
    {
        is_dirty = true;
        set_efx_delay();
    }

    if (props_.mEcho.flLRDelay != props.mEcho.flLRDelay)
    {
        is_dirty = true;
        set_efx_lr_delay();
    }

    if (props_.mEcho.flDamping != props.mEcho.flDamping)
    {
        is_dirty = true;
        set_efx_damping();
    }

    if (props_.mEcho.flFeedback != props.mEcho.flFeedback)
    {
        is_dirty = true;
        set_efx_feedback();
    }

    if (props_.mEcho.flSpread != props.mEcho.flSpread)
    {
        is_dirty = true;
        set_efx_spread();
    }

    return is_dirty;
}

} // namespace

EaxEffectUPtr eax_create_eax_echo_effect(int eax_version)
{
    return eax_create_eax4_effect<EaxEchoEffect>(eax_version);
}

#endif // ALSOFT_EAX
