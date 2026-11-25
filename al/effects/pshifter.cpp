
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
import gsl;
import numeric;

#else

#include <variant>

#include "AL/al.h"
#include "AL/efx.h"

#include "al/effects/handlers.hpp"
#include "alc/context.hpp"
#include "core/effects/base.hpp"
#include "gsl/gsl"
#include "numeric.hpp"
#endif


void PshifterEffectHandler::SetParami(al::Context& context, PshifterProps &props, ALenum param, int val)
{
    switch(param)
    {
    case AL_PITCH_SHIFTER_COARSE_TUNE:
        if(!(val >= AL_PITCH_SHIFTER_MIN_COARSE_TUNE && val <= AL_PITCH_SHIFTER_MAX_COARSE_TUNE))
            context.throw_error(AL_INVALID_VALUE, "Pitch shifter coarse tune out of range");
        props.CoarseTune = val;
        return;

    case AL_PITCH_SHIFTER_FINE_TUNE:
        if(!(val >= AL_PITCH_SHIFTER_MIN_FINE_TUNE && val <= AL_PITCH_SHIFTER_MAX_FINE_TUNE))
            context.throw_error(AL_INVALID_VALUE, "Pitch shifter fine tune out of range");
        props.FineTune = val;
        return;
    }

    context.throw_error(AL_INVALID_ENUM, "Invalid pitch shifter integer property {:#04x}",
        as_unsigned(param));
}
void PshifterEffectHandler::SetParamiv(al::Context& context, PshifterProps &props, ALenum param, const int *vals)
{ SetParami(context, props, param, *vals); }

void PshifterEffectHandler::SetParamf(al::Context& context, PshifterProps&, ALenum param, float)
{ context.throw_error(AL_INVALID_ENUM, "Invalid pitch shifter float property {:#04x}", as_unsigned(param)); }
void PshifterEffectHandler::SetParamfv(al::Context& context, PshifterProps &props, ALenum param, const float *vals)
{ SetParamf(context, props, param, *vals); }

void PshifterEffectHandler::GetParami(al::Context& context, const PshifterProps &props, ALenum param, int *val)
{
    switch(param)
    {
    case AL_PITCH_SHIFTER_COARSE_TUNE: *val = props.CoarseTune; return;
    case AL_PITCH_SHIFTER_FINE_TUNE: *val = props.FineTune; return;
    }

    context.throw_error(AL_INVALID_ENUM, "Invalid pitch shifter integer property {:#04x}",
        as_unsigned(param));
}
void PshifterEffectHandler::GetParamiv(al::Context& context, const PshifterProps &props, ALenum param, int *vals)
{ GetParami(context, props, param, vals); }

void PshifterEffectHandler::GetParamf(al::Context& context, const PshifterProps&, ALenum param, float*)
{ context.throw_error(AL_INVALID_ENUM, "Invalid pitch shifter float property {:#04x}", as_unsigned(param)); }
void PshifterEffectHandler::GetParamfv(al::Context& context, const PshifterProps &props, ALenum param, float *vals)
{ GetParamf(context, props, param, vals); }


#if ALSOFT_EAX
namespace {

/* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
struct EaxPitchShifterException final : EaxException {
    explicit EaxPitchShifterException(std::string_view const message)
        : EaxException{"EAX_PITCH_SHIFTER_EFFECT", message}
    { }
};

struct CoarseTuneValidator {
    void operator()(eax_long const lCoarseTune) const
    {
        eax_validate_range<EaxPitchShifterException>(
            "Coarse Tune",
            lCoarseTune,
            EAXPITCHSHIFTER_MINCOARSETUNE,
            EAXPITCHSHIFTER_MAXCOARSETUNE);
    }
}; // CoarseTuneValidator

struct FineTuneValidator {
    void operator()(eax_long const lFineTune) const
    {
        eax_validate_range<EaxPitchShifterException>(
            "Fine Tune",
            lFineTune,
            EAXPITCHSHIFTER_MINFINETUNE,
            EAXPITCHSHIFTER_MAXFINETUNE);
    }
}; // FineTuneValidator

struct AllValidator {
    void operator()(const EAXPITCHSHIFTERPROPERTIES& all) const
    {
        CoarseTuneValidator{}(all.lCoarseTune);
        FineTuneValidator{}(all.lFineTune);
    }
}; // AllValidator

} // namespace

template<> [[noreturn]]
void EaxPitchShifterCommitter::fail(std::string_view const message)
{ throw EaxPitchShifterException{message}; }

template<>
auto EaxPitchShifterCommitter::commit(const EAXPITCHSHIFTERPROPERTIES &props) const -> bool
{
    if(auto *cur = std::get_if<EAXPITCHSHIFTERPROPERTIES>(&mEaxProps); cur && *cur == props)
        return false;

    mEaxProps = props;
    mAlProps = PshifterProps{
        .CoarseTune = gsl::narrow_cast<int>(props.lCoarseTune),
        .FineTune = gsl::narrow_cast<int>(props.lFineTune)};

    return true;
}

template<>
void EaxPitchShifterCommitter::SetDefaults(EaxEffectProps &props)
{
    props = EAXPITCHSHIFTERPROPERTIES{
        .lCoarseTune = EAXPITCHSHIFTER_DEFAULTCOARSETUNE,
        .lFineTune = EAXPITCHSHIFTER_DEFAULTFINETUNE};
}

template<>
void EaxPitchShifterCommitter::Get(const EaxCall &call, const EAXPITCHSHIFTERPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case EAXPITCHSHIFTER_NONE: break;
    case EAXPITCHSHIFTER_ALLPARAMETERS: call.store(props); break;
    case EAXPITCHSHIFTER_COARSETUNE: call.store(props.lCoarseTune); break;
    case EAXPITCHSHIFTER_FINETUNE: call.store(props.lFineTune); break;
    default: fail_unknown_property_id();
    }
}

template<>
void EaxPitchShifterCommitter::Set(const EaxCall &call, EAXPITCHSHIFTERPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case EAXPITCHSHIFTER_NONE: break;
    case EAXPITCHSHIFTER_ALLPARAMETERS: defer<AllValidator>(call, props); break;
    case EAXPITCHSHIFTER_COARSETUNE: defer<CoarseTuneValidator>(call, props.lCoarseTune); break;
    case EAXPITCHSHIFTER_FINETUNE: defer<FineTuneValidator>(call, props.lFineTune); break;
    default: fail_unknown_property_id();
    }
}

#endif // ALSOFT_EAX
