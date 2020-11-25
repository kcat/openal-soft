
#include "config.h"

#include "AL/al.h"
#include "AL/efx.h"

#include "effects.h"
#include "effects/base.h"


namespace {

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
