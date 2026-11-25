
#include "config.h"

#include <string_view>

#if ALSOFT_EAX
#include "al/eax/api.h"
#if HAVE_CXXMODULES
import eax.call;
import eax.effect;
import eax.exception;
import eax.validator;

#else

#include "al/eax/call.hpp"
#include "al/eax/effect.hpp"
#include "al/eax/exception.hpp"
#include "al/eax/validator.hpp"
#endif
#endif // ALSOFT_EAX

#if HAVE_CXXMODULES
import common.std;
import openal;

import al.effects.handlers;
import alc.context;
import core.effects.base;
import numeric;
#else

#include <variant>

#include "AL/al.h"
#include "AL/efx.h"

#include "al/effects/handlers.hpp"
#include "alc/context.hpp"
#include "core/effects/base.hpp"
#include "numeric.hpp"
#endif


void CompressorEffectHandler::SetParami(al::Context& context, CompressorProps &props, ALenum param, int val)
{
    switch(param)
    {
    case AL_COMPRESSOR_ONOFF:
        if(!(val >= AL_COMPRESSOR_MIN_ONOFF && val <= AL_COMPRESSOR_MAX_ONOFF))
            context.throw_error(AL_INVALID_VALUE, "Compressor state out of range");
        props.OnOff = (val != AL_FALSE);
        return;
    }

    context.throw_error(AL_INVALID_ENUM, "Invalid compressor integer property {:#04x}",
        as_unsigned(param));
}
void CompressorEffectHandler::SetParamiv(al::Context& context, CompressorProps &props, ALenum param, const int *vals)
{ SetParami(context, props, param, *vals); }
void CompressorEffectHandler::SetParamf(al::Context& context, CompressorProps&, ALenum param, float)
{ context.throw_error(AL_INVALID_ENUM, "Invalid compressor float property {:#04x}", as_unsigned(param)); }
void CompressorEffectHandler::SetParamfv(al::Context& context, CompressorProps&, ALenum param, const float*)
{ context.throw_error(AL_INVALID_ENUM, "Invalid compressor float-vector property {:#04x}", as_unsigned(param)); }

void CompressorEffectHandler::GetParami(al::Context& context, const CompressorProps &props, ALenum param, int *val)
{ 
    switch(param)
    {
    case AL_COMPRESSOR_ONOFF: *val = props.OnOff; return;
    }

    context.throw_error(AL_INVALID_ENUM, "Invalid compressor integer property {:#04x}",
        as_unsigned(param));
}
void CompressorEffectHandler::GetParamiv(al::Context& context, const CompressorProps &props, ALenum param, int *vals)
{ GetParami(context, props, param, vals); }
void CompressorEffectHandler::GetParamf(al::Context& context, const CompressorProps&, ALenum param, float*)
{ context.throw_error(AL_INVALID_ENUM, "Invalid compressor float property {:#04x}", as_unsigned(param)); }
void CompressorEffectHandler::GetParamfv(al::Context& context, const CompressorProps&, ALenum param, float*)
{ context.throw_error(AL_INVALID_ENUM, "Invalid compressor float-vector property {:#04x}", as_unsigned(param)); }


#if ALSOFT_EAX
namespace {

/* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
struct EaxCompressorException final : EaxException {
    explicit EaxCompressorException(std::string_view const message)
        : EaxException{"EAX_COMPRESSOR_EFFECT", message}
    { }
};

struct OnOffValidator {
    void operator()(eax_ulong const ulOnOff) const
    {
        eax_validate_range<EaxCompressorException>(
            "On-Off",
            ulOnOff,
            EAXAGCCOMPRESSOR_MINONOFF,
            EAXAGCCOMPRESSOR_MAXONOFF);
    }
}; // OnOffValidator

struct AllValidator {
    void operator()(const EAXAGCCOMPRESSORPROPERTIES& all) const
    {
        OnOffValidator{}(all.ulOnOff);
    }
}; // AllValidator

} // namespace

template<> [[noreturn]]
void EaxCompressorCommitter::fail(std::string_view const message)
{ throw EaxCompressorException{message}; }

template<>
auto EaxCompressorCommitter::commit(const EAXAGCCOMPRESSORPROPERTIES &props) const -> bool
{
    if(auto *cur = std::get_if<EAXAGCCOMPRESSORPROPERTIES>(&mEaxProps); cur && *cur == props)
        return false;

    mEaxProps = props;
    mAlProps = CompressorProps{.OnOff = props.ulOnOff != 0};

    return true;
}

template<>
void EaxCompressorCommitter::SetDefaults(EaxEffectProps &props)
{
    props = EAXAGCCOMPRESSORPROPERTIES{.ulOnOff = EAXAGCCOMPRESSOR_DEFAULTONOFF};
}

template<>
void EaxCompressorCommitter::Get(const EaxCall &call, const EAXAGCCOMPRESSORPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case EAXAGCCOMPRESSOR_NONE: break;
    case EAXAGCCOMPRESSOR_ALLPARAMETERS: call.store(props); break;
    case EAXAGCCOMPRESSOR_ONOFF: call.store(props.ulOnOff); break;
    default: fail_unknown_property_id();
    }
}

template<>
void EaxCompressorCommitter::Set(const EaxCall &call, EAXAGCCOMPRESSORPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case EAXAGCCOMPRESSOR_NONE: break;
    case EAXAGCCOMPRESSOR_ALLPARAMETERS: defer<AllValidator>(call, props); break;
    case EAXAGCCOMPRESSOR_ONOFF: defer<OnOffValidator>(call, props.ulOnOff); break;
    default: fail_unknown_property_id();
    }
}

#endif // ALSOFT_EAX
