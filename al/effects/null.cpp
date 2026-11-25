
#include "config.h"

#include <string_view>
#include <variant>

#if ALSOFT_EAX
#if HAVE_CXXMODULES
import eax.call;
import eax.effect;
import eax.exception;

#else

#include "al/eax/call.hpp"
#include "al/eax/effect.hpp"
#include "al/eax/exception.hpp"
#endif
#endif // ALSOFT_EAX

#if HAVE_CXXMODULES
import openal;

import al.effects.handlers;
import alc.context;
import core.effects.base;
import numeric;

#else

#include "AL/al.h"
#include "AL/efx.h"

#include "al/effects/handlers.hpp"
#include "alc/context.hpp"
#include "core/effects/base.hpp"
#include "numeric.hpp"
#endif


void NullEffectHandler::SetParami(al::Context& context, std::monostate& /*props*/, ALenum param, int /*val*/)
{
    context.throw_error(AL_INVALID_ENUM, "Invalid null effect integer property {:#04x}",
        as_unsigned(param));
}
void NullEffectHandler::SetParamiv(al::Context& context, std::monostate &props, ALenum param, const int *vals)
{
    SetParami(context, props, param, *vals);
}
void NullEffectHandler::SetParamf(al::Context& context, std::monostate& /*props*/, ALenum param, float /*val*/)
{
    context.throw_error(AL_INVALID_ENUM, "Invalid null effect float property {:#04x}",
        as_unsigned(param));
}
void NullEffectHandler::SetParamfv(al::Context& context, std::monostate &props, ALenum param, const float *vals)
{
    SetParamf(context, props, param, *vals);
}

void NullEffectHandler::GetParami(al::Context& context, const std::monostate& /*props*/, ALenum param, int* /*val*/)
{
    context.throw_error(AL_INVALID_ENUM, "Invalid null effect integer property {:#04x}",
        as_unsigned(param));
}
void NullEffectHandler::GetParamiv(al::Context& context, const std::monostate &props, ALenum param, int *vals)
{
    GetParami(context, props, param, vals);
}
void NullEffectHandler::GetParamf(al::Context& context, const std::monostate& /*props*/, ALenum param, float* /*val*/)
{
    context.throw_error(AL_INVALID_ENUM, "Invalid null effect float property {:#04x}",
        as_unsigned(param));
}
void NullEffectHandler::GetParamfv(al::Context& context, const std::monostate &props, ALenum param, float *vals)
{
    GetParamf(context, props, param, vals);
}


#if ALSOFT_EAX
namespace {

/* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
struct EaxNullEffectException final : EaxException {
    explicit EaxNullEffectException(std::string_view const message)
        : EaxException{"EAX_NULL_EFFECT", message}
    { }
};

} // namespace

template<> [[noreturn]]
void EaxNullCommitter::fail(std::string_view const message)
{ throw EaxNullEffectException{message}; }

template<>
auto EaxNullCommitter::commit(const std::monostate &props) const -> bool
{
    const bool ret{std::holds_alternative<std::monostate>(mEaxProps)};
    mEaxProps = props;
    mAlProps = std::monostate{};
    return ret;
}

template<>
void EaxNullCommitter::SetDefaults(EaxEffectProps &props)
{
    props = std::monostate{};
}

template<>
void EaxNullCommitter::Get(const EaxCall &call, const std::monostate&)
{
    if(call.get_property_id() != 0)
        fail_unknown_property_id();
}

template<>
void EaxNullCommitter::Set(const EaxCall &call, std::monostate&)
{
    if(call.get_property_id() != 0)
        fail_unknown_property_id();
}

#endif // ALSOFT_EAX
