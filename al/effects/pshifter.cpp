
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

void Pshifter_setParamf(EffectProps*, ALenum param, float)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid pitch shifter float property 0x%04x", param}; }
void Pshifter_setParamfv(EffectProps*, ALenum param, const float*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid pitch shifter float-vector property 0x%04x",
        param};
}

void Pshifter_setParami(EffectProps *props, ALenum param, int val)
{
    switch(param)
    {
    case AL_PITCH_SHIFTER_COARSE_TUNE:
        if(!(val >= AL_PITCH_SHIFTER_MIN_COARSE_TUNE && val <= AL_PITCH_SHIFTER_MAX_COARSE_TUNE))
            throw effect_exception{AL_INVALID_VALUE, "Pitch shifter coarse tune out of range"};
        props->Pshifter.CoarseTune = val;
        break;

    case AL_PITCH_SHIFTER_FINE_TUNE:
        if(!(val >= AL_PITCH_SHIFTER_MIN_FINE_TUNE && val <= AL_PITCH_SHIFTER_MAX_FINE_TUNE))
            throw effect_exception{AL_INVALID_VALUE, "Pitch shifter fine tune out of range"};
        props->Pshifter.FineTune = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid pitch shifter integer property 0x%04x",
            param};
    }
}
void Pshifter_setParamiv(EffectProps *props, ALenum param, const int *vals)
{ Pshifter_setParami(props, param, vals[0]); }

void Pshifter_getParami(const EffectProps *props, ALenum param, int *val)
{
    switch(param)
    {
    case AL_PITCH_SHIFTER_COARSE_TUNE:
        *val = props->Pshifter.CoarseTune;
        break;
    case AL_PITCH_SHIFTER_FINE_TUNE:
        *val = props->Pshifter.FineTune;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid pitch shifter integer property 0x%04x",
            param};
    }
}
void Pshifter_getParamiv(const EffectProps *props, ALenum param, int *vals)
{ Pshifter_getParami(props, param, vals); }

void Pshifter_getParamf(const EffectProps*, ALenum param, float*)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid pitch shifter float property 0x%04x", param}; }
void Pshifter_getParamfv(const EffectProps*, ALenum param, float*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid pitch shifter float vector-property 0x%04x",
        param};
}

EffectProps genDefaultProps() noexcept
{
    EffectProps props{};
    props.Pshifter.CoarseTune = AL_PITCH_SHIFTER_DEFAULT_COARSE_TUNE;
    props.Pshifter.FineTune   = AL_PITCH_SHIFTER_DEFAULT_FINE_TUNE;
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Pshifter);

const EffectProps PshifterEffectProps{genDefaultProps()};

#ifdef ALSOFT_EAX
namespace {

class EaxPitchShifterEffectException : public EaxException
{
public:
    explicit EaxPitchShifterEffectException(const char* message)
        : EaxException{"EAX_PITCH_SHIFTER_EFFECT", message}
    {}
}; // EaxPitchShifterEffectException

class EaxPitchShifterEffect final : public EaxEffect4<EaxPitchShifterEffectException, EAXPITCHSHIFTERPROPERTIES> {
public:
    EaxPitchShifterEffect(int eax_version);

private:
    struct CoarseTuneValidator {
        void operator()(long lCoarseTune) const
        {
            eax_validate_range<Exception>(
                "Coarse Tune",
                lCoarseTune,
                EAXPITCHSHIFTER_MINCOARSETUNE,
                EAXPITCHSHIFTER_MAXCOARSETUNE);
        }
    }; // CoarseTuneValidator

    struct FineTuneValidator {
        void operator()(long lFineTune) const
        {
            eax_validate_range<Exception>(
                "Fine Tune",
                lFineTune,
                EAXPITCHSHIFTER_MINFINETUNE,
                EAXPITCHSHIFTER_MAXFINETUNE);
        }
    }; // FineTuneValidator

    struct AllValidator {
        void operator()(const Props& all) const
        {
            CoarseTuneValidator{}(all.lCoarseTune);
            FineTuneValidator{}(all.lFineTune);
        }
    }; // AllValidator

    void set_defaults(Props& props) override;

    void set_efx_coarse_tune() noexcept;
    void set_efx_fine_tune() noexcept;
    void set_efx_defaults() override;

    void get(const EaxCall& call, const Props& props) override;
    void set(const EaxCall& call, Props& props) override;
    bool commit_props(const Props& old_i) override;
}; // EaxPitchShifterEffect

EaxPitchShifterEffect::EaxPitchShifterEffect(int eax_version)
    : EaxEffect4{AL_EFFECT_PITCH_SHIFTER, eax_version}
{}

void EaxPitchShifterEffect::set_defaults(Props& props)
{
    props.lCoarseTune = EAXPITCHSHIFTER_DEFAULTCOARSETUNE;
    props.lFineTune = EAXPITCHSHIFTER_DEFAULTFINETUNE;
}

void EaxPitchShifterEffect::set_efx_coarse_tune() noexcept
{
    al_effect_props_.Pshifter.CoarseTune = clamp(
        static_cast<ALint>(props_.lCoarseTune),
        AL_PITCH_SHIFTER_MIN_COARSE_TUNE,
        AL_PITCH_SHIFTER_MAX_COARSE_TUNE);
}

void EaxPitchShifterEffect::set_efx_fine_tune() noexcept
{
    al_effect_props_.Pshifter.FineTune = clamp(
        static_cast<ALint>(props_.lFineTune),
        AL_PITCH_SHIFTER_MIN_FINE_TUNE,
        AL_PITCH_SHIFTER_MAX_FINE_TUNE);
}

void EaxPitchShifterEffect::set_efx_defaults()
{
    set_efx_coarse_tune();
    set_efx_fine_tune();
}

void EaxPitchShifterEffect::get(const EaxCall& call, const Props& props)
{
    switch(call.get_property_id())
    {
        case EAXPITCHSHIFTER_NONE: break;
        case EAXPITCHSHIFTER_ALLPARAMETERS: call.set_value<Exception>(props); break;
        case EAXPITCHSHIFTER_COARSETUNE: call.set_value<Exception>(props.lCoarseTune); break;
        case EAXPITCHSHIFTER_FINETUNE: call.set_value<Exception>(props.lFineTune); break;
        default: fail_unknown_property_id();
    }
}

void EaxPitchShifterEffect::set(const EaxCall& call, Props& props)
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

bool EaxPitchShifterEffect::commit_props(const Props& props)
{
    auto is_dirty = false;

    if (props_.lCoarseTune != props.lCoarseTune)
    {
        is_dirty = true;
        set_efx_coarse_tune();
    }

    if (props_.lFineTune != props.lFineTune)
    {
        is_dirty = true;
        set_efx_fine_tune();
    }

    return is_dirty;
}

} // namespace

EaxEffectUPtr eax_create_eax_pitch_shifter_effect(int eax_version)
{
    return eax_create_eax4_effect<EaxPitchShifterEffect>(eax_version);
}

#endif // ALSOFT_EAX
