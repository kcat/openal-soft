
#include "config.h"

#include <cmath>

#include "AL/al.h"
#include "AL/alext.h"

#include "alc/context.h"
#include "alnumeric.h"
#include "effects.h"


namespace {

consteval auto genDefaultDialogProps() noexcept -> EffectProps
{
    return DedicatedProps{
        .Target = DedicatedProps::Dialog,
        .Gain = 1.0f};
}

consteval auto genDefaultLfeProps() noexcept -> EffectProps
{
    return DedicatedProps{
        .Target = DedicatedProps::Lfe,
        .Gain = 1.0f};
}

} // namespace

constinit const EffectProps DedicatedDialogEffectProps(genDefaultDialogProps());

void DedicatedDialogEffectHandler::SetParami(al::Context *context, DedicatedProps&, ALenum param, int)
{ context->throw_error(AL_INVALID_ENUM, "Invalid dedicated integer property {:#04x}", as_unsigned(param)); }
void DedicatedDialogEffectHandler::SetParamiv(al::Context *context, DedicatedProps&, ALenum param, const int*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid dedicated integer-vector property {:#04x}", as_unsigned(param)); }
void DedicatedDialogEffectHandler::SetParamf(al::Context *context, DedicatedProps &props, ALenum param, float val)
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
void DedicatedDialogEffectHandler::SetParamfv(al::Context *context, DedicatedProps &props, ALenum param, const float *vals)
{ SetParamf(context, props, param, *vals); }

void DedicatedDialogEffectHandler::GetParami(al::Context *context, const DedicatedProps&, ALenum param, int*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid dedicated integer property {:#04x}", as_unsigned(param)); }
void DedicatedDialogEffectHandler::GetParamiv(al::Context *context, const DedicatedProps&, ALenum param, int*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid dedicated integer-vector property {:#04x}", as_unsigned(param)); }
void DedicatedDialogEffectHandler::GetParamf(al::Context *context, const DedicatedProps &props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_DEDICATED_GAIN: *val = props.Gain; return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid dedicated float property {:#04x}",
        as_unsigned(param));
}
void DedicatedDialogEffectHandler::GetParamfv(al::Context *context, const DedicatedProps &props, ALenum param, float *vals)
{ GetParamf(context, props, param, vals); }


constinit const EffectProps DedicatedLfeEffectProps(genDefaultLfeProps());

void DedicatedLfeEffectHandler::SetParami(al::Context *context, DedicatedProps&, ALenum param, int)
{ context->throw_error(AL_INVALID_ENUM, "Invalid dedicated integer property {:#04x}", as_unsigned(param)); }
void DedicatedLfeEffectHandler::SetParamiv(al::Context *context, DedicatedProps&, ALenum param, const int*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid dedicated integer-vector property {:#04x}", as_unsigned(param)); }
void DedicatedLfeEffectHandler::SetParamf(al::Context *context, DedicatedProps &props, ALenum param, float val)
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
void DedicatedLfeEffectHandler::SetParamfv(al::Context *context, DedicatedProps &props, ALenum param, const float *vals)
{ SetParamf(context, props, param, *vals); }

void DedicatedLfeEffectHandler::GetParami(al::Context *context, const DedicatedProps&, ALenum param, int*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid dedicated integer property {:#04x}", as_unsigned(param)); }
void DedicatedLfeEffectHandler::GetParamiv(al::Context *context, const DedicatedProps&, ALenum param, int*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid dedicated integer-vector property {:#04x}", as_unsigned(param)); }
void DedicatedLfeEffectHandler::GetParamf(al::Context *context, const DedicatedProps &props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_DEDICATED_GAIN: *val = props.Gain; return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid dedicated float property {:#04x}",
        as_unsigned(param));
}
void DedicatedLfeEffectHandler::GetParamfv(al::Context *context, const DedicatedProps &props, ALenum param, float *vals)
{ GetParamf(context, props, param, vals); }
