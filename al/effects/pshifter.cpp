
#include "config.h"

#include "AL/al.h"
#include "AL/efx.h"

#include "alc/context.h"
#include "alnumeric.h"
#include "effects.h"
#include "gsl/gsl"

#if ALSOFT_EAX
#include "al/eax/effect.h"
#include "al/eax/exception.h"
#include "al/eax/utils.h"
#endif // ALSOFT_EAX


namespace {

consteval auto genDefaultProps() noexcept -> EffectProps
{
    return PshifterProps{
        .CoarseTune = AL_PITCH_SHIFTER_DEFAULT_COARSE_TUNE,
        .FineTune = AL_PITCH_SHIFTER_DEFAULT_FINE_TUNE};
}

} // namespace

constinit const EffectProps PshifterEffectProps(genDefaultProps());

void PshifterEffectHandler::SetParami(al::Context *context, PshifterProps &props, ALenum param, int val)
{
    switch(param)
    {
    case AL_PITCH_SHIFTER_COARSE_TUNE:
        if(!(val >= AL_PITCH_SHIFTER_MIN_COARSE_TUNE && val <= AL_PITCH_SHIFTER_MAX_COARSE_TUNE))
            context->throw_error(AL_INVALID_VALUE, "Pitch shifter coarse tune out of range");
        props.CoarseTune = val;
        return;

    case AL_PITCH_SHIFTER_FINE_TUNE:
        if(!(val >= AL_PITCH_SHIFTER_MIN_FINE_TUNE && val <= AL_PITCH_SHIFTER_MAX_FINE_TUNE))
            context->throw_error(AL_INVALID_VALUE, "Pitch shifter fine tune out of range");
        props.FineTune = val;
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid pitch shifter integer property {:#04x}",
        as_unsigned(param));
}
void PshifterEffectHandler::SetParamiv(al::Context *context, PshifterProps &props, ALenum param, const int *vals)
{ SetParami(context, props, param, *vals); }

void PshifterEffectHandler::SetParamf(al::Context *context, PshifterProps&, ALenum param, float)
{ context->throw_error(AL_INVALID_ENUM, "Invalid pitch shifter float property {:#04x}", as_unsigned(param)); }
void PshifterEffectHandler::SetParamfv(al::Context *context, PshifterProps &props, ALenum param, const float *vals)
{ SetParamf(context, props, param, *vals); }

void PshifterEffectHandler::GetParami(al::Context *context, const PshifterProps &props, ALenum param, int *val)
{
    switch(param)
    {
    case AL_PITCH_SHIFTER_COARSE_TUNE: *val = props.CoarseTune; return;
    case AL_PITCH_SHIFTER_FINE_TUNE: *val = props.FineTune; return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid pitch shifter integer property {:#04x}",
        as_unsigned(param));
}
void PshifterEffectHandler::GetParamiv(al::Context *context, const PshifterProps &props, ALenum param, int *vals)
{ GetParami(context, props, param, vals); }

void PshifterEffectHandler::GetParamf(al::Context *context, const PshifterProps&, ALenum param, float*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid pitch shifter float property {:#04x}", as_unsigned(param)); }
void PshifterEffectHandler::GetParamfv(al::Context *context, const PshifterProps &props, ALenum param, float *vals)
{ GetParamf(context, props, param, vals); }


#if ALSOFT_EAX
namespace {

using PitchShifterCommitter = EaxCommitter<EaxPitchShifterCommitter>;

struct CoarseTuneValidator {
    void operator()(long lCoarseTune) const
    {
        eax_validate_range<PitchShifterCommitter::Exception>(
            "Coarse Tune",
            lCoarseTune,
            EAXPITCHSHIFTER_MINCOARSETUNE,
            EAXPITCHSHIFTER_MAXCOARSETUNE);
    }
}; // CoarseTuneValidator

struct FineTuneValidator {
    void operator()(long lFineTune) const
    {
        eax_validate_range<PitchShifterCommitter::Exception>(
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

template<> /* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
struct PitchShifterCommitter::Exception : public EaxException {
    explicit Exception(const std::string_view message)
        : EaxException{"EAX_PITCH_SHIFTER_EFFECT", message}
    { }
};

template<> [[noreturn]]
void PitchShifterCommitter::fail(const std::string_view message)
{ throw Exception{message}; }

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

void EaxPitchShifterCommitter::SetDefaults(EaxEffectProps &props)
{
    props = EAXPITCHSHIFTERPROPERTIES{
        .lCoarseTune = EAXPITCHSHIFTER_DEFAULTCOARSETUNE,
        .lFineTune = EAXPITCHSHIFTER_DEFAULTFINETUNE};
}

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
