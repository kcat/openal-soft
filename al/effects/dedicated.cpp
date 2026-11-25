
#include "config.h"

#include <variant>

#if HAVE_CXXMODULES
import common.std;
import openal;

import al.effects.handlers;
import alc.context;
import core.effects.base;
import numeric;

#else

#include <cmath>

#include "AL/al.h"
#include "AL/alext.h"

#include "al/effects/handlers.hpp"
#include "alc/context.hpp"
#include "core/effects/base.hpp"
#include "numeric.hpp"
#endif


void DedicatedDialogEffectHandler::SetParami(al::Context& context, DedicatedProps&, ALenum param, int)
{ context.throw_error(AL_INVALID_ENUM, "Invalid dedicated integer property {:#04x}", as_unsigned(param)); }
void DedicatedDialogEffectHandler::SetParamiv(al::Context& context, DedicatedProps&, ALenum param, const int*)
{ context.throw_error(AL_INVALID_ENUM, "Invalid dedicated integer-vector property {:#04x}", as_unsigned(param)); }
void DedicatedDialogEffectHandler::SetParamf(al::Context& context, DedicatedProps &props, ALenum param, float val)
{
    switch(param)
    {
    case AL_DEDICATED_GAIN:
        if(!(val >= 0.0f && std::isfinite(val)))
            context.throw_error(AL_INVALID_VALUE, "Dedicated gain out of range");
        props.Gain = val;
        return;
    }

    context.throw_error(AL_INVALID_ENUM, "Invalid dedicated float property {:#04x}",
        as_unsigned(param));
}
void DedicatedDialogEffectHandler::SetParamfv(al::Context& context, DedicatedProps &props, ALenum param, const float *vals)
{ SetParamf(context, props, param, *vals); }

void DedicatedDialogEffectHandler::GetParami(al::Context& context, const DedicatedProps&, ALenum param, int*)
{ context.throw_error(AL_INVALID_ENUM, "Invalid dedicated integer property {:#04x}", as_unsigned(param)); }
void DedicatedDialogEffectHandler::GetParamiv(al::Context& context, const DedicatedProps&, ALenum param, int*)
{ context.throw_error(AL_INVALID_ENUM, "Invalid dedicated integer-vector property {:#04x}", as_unsigned(param)); }
void DedicatedDialogEffectHandler::GetParamf(al::Context& context, const DedicatedProps &props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_DEDICATED_GAIN: *val = props.Gain; return;
    }

    context.throw_error(AL_INVALID_ENUM, "Invalid dedicated float property {:#04x}",
        as_unsigned(param));
}
void DedicatedDialogEffectHandler::GetParamfv(al::Context& context, const DedicatedProps &props, ALenum param, float *vals)
{ GetParamf(context, props, param, vals); }


void DedicatedLfeEffectHandler::SetParami(al::Context& context, DedicatedProps&, ALenum param, int)
{ context.throw_error(AL_INVALID_ENUM, "Invalid dedicated integer property {:#04x}", as_unsigned(param)); }
void DedicatedLfeEffectHandler::SetParamiv(al::Context& context, DedicatedProps&, ALenum param, const int*)
{ context.throw_error(AL_INVALID_ENUM, "Invalid dedicated integer-vector property {:#04x}", as_unsigned(param)); }
void DedicatedLfeEffectHandler::SetParamf(al::Context& context, DedicatedProps &props, ALenum param, float val)
{
    switch(param)
    {
    case AL_DEDICATED_GAIN:
        if(!(val >= 0.0f && std::isfinite(val)))
            context.throw_error(AL_INVALID_VALUE, "Dedicated gain out of range");
        props.Gain = val;
        return;
    }

    context.throw_error(AL_INVALID_ENUM, "Invalid dedicated float property {:#04x}",
        as_unsigned(param));
}
void DedicatedLfeEffectHandler::SetParamfv(al::Context& context, DedicatedProps &props, ALenum param, const float *vals)
{ SetParamf(context, props, param, *vals); }

void DedicatedLfeEffectHandler::GetParami(al::Context& context, const DedicatedProps&, ALenum param, int*)
{ context.throw_error(AL_INVALID_ENUM, "Invalid dedicated integer property {:#04x}", as_unsigned(param)); }
void DedicatedLfeEffectHandler::GetParamiv(al::Context& context, const DedicatedProps&, ALenum param, int*)
{ context.throw_error(AL_INVALID_ENUM, "Invalid dedicated integer-vector property {:#04x}", as_unsigned(param)); }
void DedicatedLfeEffectHandler::GetParamf(al::Context& context, const DedicatedProps &props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_DEDICATED_GAIN: *val = props.Gain; return;
    }

    context.throw_error(AL_INVALID_ENUM, "Invalid dedicated float property {:#04x}",
        as_unsigned(param));
}
void DedicatedLfeEffectHandler::GetParamfv(al::Context& context, const DedicatedProps &props, ALenum param, float *vals)
{ GetParamf(context, props, param, vals); }
