
#include "config.h"

#include <cmath>

#include "AL/al.h"
#include "AL/alext.h"

#include "alc/context.h"
#include "alnumeric.h"
#include "effects.h"


namespace {

constexpr EffectProps genDefaultDialogProps() noexcept
{
    DedicatedProps props{};
    props.Target = DedicatedProps::Dialog;
    props.Gain = 1.0f;
    return props;
}

constexpr EffectProps genDefaultLfeProps() noexcept
{
    DedicatedProps props{};
    props.Target = DedicatedProps::Lfe;
    props.Gain = 1.0f;
    return props;
}

} // namespace

const EffectProps DedicatedDialogEffectProps{genDefaultDialogProps()};

void DedicatedDialogEffectHandler::SetParami(ALCcontext *context, DedicatedProps&, ALenum param, int)
{ context->throw_error(AL_INVALID_ENUM, "Invalid dedicated integer property {:#04x}", as_unsigned(param)); }
void DedicatedDialogEffectHandler::SetParamiv(ALCcontext *context, DedicatedProps&, ALenum param, const int*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid dedicated integer-vector property {:#04x}", as_unsigned(param)); }
void DedicatedDialogEffectHandler::SetParamf(ALCcontext *context, DedicatedProps &props, ALenum param, float val)
{
    switch(param)
    {
    case AL_DEDICATED_GAIN:
        if(!(val >= 0.0f && std::isfinite(val)))
            context->throw_error(AL_INVALID_VALUE, "Dedicated gain out of range");
        props.Gain = val;
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid dedicated float property {:#04x}",
        as_unsigned(param));
}
void DedicatedDialogEffectHandler::SetParamfv(ALCcontext *context, DedicatedProps &props, ALenum param, const float *vals)
{ SetParamf(context, props, param, *vals); }

void DedicatedDialogEffectHandler::GetParami(ALCcontext *context, const DedicatedProps&, ALenum param, int*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid dedicated integer property {:#04x}", as_unsigned(param)); }
void DedicatedDialogEffectHandler::GetParamiv(ALCcontext *context, const DedicatedProps&, ALenum param, int*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid dedicated integer-vector property {:#04x}", as_unsigned(param)); }
void DedicatedDialogEffectHandler::GetParamf(ALCcontext *context, const DedicatedProps &props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_DEDICATED_GAIN: *val = props.Gain; return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid dedicated float property {:#04x}",
        as_unsigned(param));
}
void DedicatedDialogEffectHandler::GetParamfv(ALCcontext *context, const DedicatedProps &props, ALenum param, float *vals)
{ GetParamf(context, props, param, vals); }


const EffectProps DedicatedLfeEffectProps{genDefaultLfeProps()};

void DedicatedLfeEffectHandler::SetParami(ALCcontext *context, DedicatedProps&, ALenum param, int)
{ context->throw_error(AL_INVALID_ENUM, "Invalid dedicated integer property {:#04x}", as_unsigned(param)); }
void DedicatedLfeEffectHandler::SetParamiv(ALCcontext *context, DedicatedProps&, ALenum param, const int*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid dedicated integer-vector property {:#04x}", as_unsigned(param)); }
void DedicatedLfeEffectHandler::SetParamf(ALCcontext *context, DedicatedProps &props, ALenum param, float val)
{
    switch(param)
    {
    case AL_DEDICATED_GAIN:
        if(!(val >= 0.0f && std::isfinite(val)))
            context->throw_error(AL_INVALID_VALUE, "Dedicated gain out of range");
        props.Gain = val;
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid dedicated float property {:#04x}",
        as_unsigned(param));
}
void DedicatedLfeEffectHandler::SetParamfv(ALCcontext *context, DedicatedProps &props, ALenum param, const float *vals)
{ SetParamf(context, props, param, *vals); }

void DedicatedLfeEffectHandler::GetParami(ALCcontext *context, const DedicatedProps&, ALenum param, int*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid dedicated integer property {:#04x}", as_unsigned(param)); }
void DedicatedLfeEffectHandler::GetParamiv(ALCcontext *context, const DedicatedProps&, ALenum param, int*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid dedicated integer-vector property {:#04x}", as_unsigned(param)); }
void DedicatedLfeEffectHandler::GetParamf(ALCcontext *context, const DedicatedProps &props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_DEDICATED_GAIN: *val = props.Gain; return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid dedicated float property {:#04x}",
        as_unsigned(param));
}
void DedicatedLfeEffectHandler::GetParamfv(ALCcontext *context, const DedicatedProps &props, ALenum param, float *vals)
{ GetParamf(context, props, param, vals); }
