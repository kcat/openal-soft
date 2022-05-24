
#include "config.h"

#include <cmath>
#include <cstdlib>

#include <algorithm>

#include "AL/efx.h"

#include "alc/effects/base.h"
#include "effects.h"

#ifdef ALSOFT_EAX
#include "alnumeric.h"
#include "al/eax/exception.h"
#include "al/eax/utils.h"
#endif // ALSOFT_EAX


namespace {

void Autowah_setParamf(EffectProps *props, ALenum param, float val)
{
    switch(param)
    {
    case AL_AUTOWAH_ATTACK_TIME:
        if(!(val >= AL_AUTOWAH_MIN_ATTACK_TIME && val <= AL_AUTOWAH_MAX_ATTACK_TIME))
            throw effect_exception{AL_INVALID_VALUE, "Autowah attack time out of range"};
        props->Autowah.AttackTime = val;
        break;

    case AL_AUTOWAH_RELEASE_TIME:
        if(!(val >= AL_AUTOWAH_MIN_RELEASE_TIME && val <= AL_AUTOWAH_MAX_RELEASE_TIME))
            throw effect_exception{AL_INVALID_VALUE, "Autowah release time out of range"};
        props->Autowah.ReleaseTime = val;
        break;

    case AL_AUTOWAH_RESONANCE:
        if(!(val >= AL_AUTOWAH_MIN_RESONANCE && val <= AL_AUTOWAH_MAX_RESONANCE))
            throw effect_exception{AL_INVALID_VALUE, "Autowah resonance out of range"};
        props->Autowah.Resonance = val;
        break;

    case AL_AUTOWAH_PEAK_GAIN:
        if(!(val >= AL_AUTOWAH_MIN_PEAK_GAIN && val <= AL_AUTOWAH_MAX_PEAK_GAIN))
            throw effect_exception{AL_INVALID_VALUE, "Autowah peak gain out of range"};
        props->Autowah.PeakGain = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid autowah float property 0x%04x", param};
    }
}
void Autowah_setParamfv(EffectProps *props,  ALenum param, const float *vals)
{ Autowah_setParamf(props, param, vals[0]); }

void Autowah_setParami(EffectProps*, ALenum param, int)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid autowah integer property 0x%04x", param}; }
void Autowah_setParamiv(EffectProps*, ALenum param, const int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid autowah integer vector property 0x%04x",
        param};
}

void Autowah_getParamf(const EffectProps *props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_AUTOWAH_ATTACK_TIME:
        *val = props->Autowah.AttackTime;
        break;

    case AL_AUTOWAH_RELEASE_TIME:
        *val = props->Autowah.ReleaseTime;
        break;

    case AL_AUTOWAH_RESONANCE:
        *val = props->Autowah.Resonance;
        break;

    case AL_AUTOWAH_PEAK_GAIN:
        *val = props->Autowah.PeakGain;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid autowah float property 0x%04x", param};
    }

}
void Autowah_getParamfv(const EffectProps *props, ALenum param, float *vals)
{ Autowah_getParamf(props, param, vals); }

void Autowah_getParami(const EffectProps*, ALenum param, int*)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid autowah integer property 0x%04x", param}; }
void Autowah_getParamiv(const EffectProps*, ALenum param, int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid autowah integer vector property 0x%04x",
        param};
}

EffectProps genDefaultProps() noexcept
{
    EffectProps props{};
    props.Autowah.AttackTime = AL_AUTOWAH_DEFAULT_ATTACK_TIME;
    props.Autowah.ReleaseTime = AL_AUTOWAH_DEFAULT_RELEASE_TIME;
    props.Autowah.Resonance = AL_AUTOWAH_DEFAULT_RESONANCE;
    props.Autowah.PeakGain = AL_AUTOWAH_DEFAULT_PEAK_GAIN;
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Autowah);

const EffectProps AutowahEffectProps{genDefaultProps()};

#ifdef ALSOFT_EAX
namespace {

class EaxAutoWahEffectException : public EaxException {
public:
    explicit EaxAutoWahEffectException(const char* message)
        : EaxException{"EAX_AUTO_WAH_EFFECT", message}
    {}
}; // EaxAutoWahEffectException

class EaxAutoWahEffect final : public EaxEffect4<EaxAutoWahEffectException, EAXAUTOWAHPROPERTIES> {
public:
    EaxAutoWahEffect(const EaxCall& call);

private:
    struct AttackTimeValidator {
        void operator()(float flAttackTime) const
        {
            eax_validate_range<Exception>(
                "Attack Time",
                flAttackTime,
                EAXAUTOWAH_MINATTACKTIME,
                EAXAUTOWAH_MAXATTACKTIME);
        }
    }; // AttackTimeValidator

    struct ReleaseTimeValidator {
        void operator()(float flReleaseTime) const
        {
            eax_validate_range<Exception>(
                "Release Time",
                flReleaseTime,
                EAXAUTOWAH_MINRELEASETIME,
                EAXAUTOWAH_MAXRELEASETIME);
        }
    }; // ReleaseTimeValidator

    struct ResonanceValidator {
        void operator()(long lResonance) const
        {
            eax_validate_range<Exception>(
                "Resonance",
                lResonance,
                EAXAUTOWAH_MINRESONANCE,
                EAXAUTOWAH_MAXRESONANCE);
        }
    }; // ResonanceValidator

    struct PeakLevelValidator {
        void operator()(long lPeakLevel) const
        {
            eax_validate_range<Exception>(
                "Peak Level",
                lPeakLevel,
                EAXAUTOWAH_MINPEAKLEVEL,
                EAXAUTOWAH_MAXPEAKLEVEL);
        }
    }; // PeakLevelValidator

    struct AllValidator {
        void operator()(const Props& all) const
        {
            AttackTimeValidator{}(all.flAttackTime);
            ReleaseTimeValidator{}(all.flReleaseTime);
            ResonanceValidator{}(all.lResonance);
            PeakLevelValidator{}(all.lPeakLevel);
        }
    }; // AllValidator

    void set_defaults(Props& props) override;

    void set_efx_attack_time() noexcept;
    void set_efx_release_time() noexcept;
    void set_efx_resonance() noexcept;
    void set_efx_peak_gain() noexcept;
    void set_efx_defaults() override;

    void get(const EaxCall& call, const Props& props) override;
    void set(const EaxCall& call, Props& props) override;
    bool commit_props(const Props& props) override;
}; // EaxAutoWahEffect

EaxAutoWahEffect::EaxAutoWahEffect(const EaxCall& call)
    : EaxEffect4{AL_EFFECT_AUTOWAH, call}
{}

void EaxAutoWahEffect::set_defaults(Props& props)
{
    props.flAttackTime = EAXAUTOWAH_DEFAULTATTACKTIME;
    props.flReleaseTime = EAXAUTOWAH_DEFAULTRELEASETIME;
    props.lResonance = EAXAUTOWAH_DEFAULTRESONANCE;
    props.lPeakLevel = EAXAUTOWAH_DEFAULTPEAKLEVEL;
}

void EaxAutoWahEffect::set_efx_attack_time() noexcept
{
    al_effect_props_.Autowah.AttackTime = clamp(
        props_.flAttackTime,
        AL_AUTOWAH_MIN_ATTACK_TIME,
        AL_AUTOWAH_MAX_ATTACK_TIME);
}

void EaxAutoWahEffect::set_efx_release_time() noexcept
{
    al_effect_props_.Autowah.ReleaseTime = clamp(
        props_.flReleaseTime,
        AL_AUTOWAH_MIN_RELEASE_TIME,
        AL_AUTOWAH_MAX_RELEASE_TIME);
}

void EaxAutoWahEffect::set_efx_resonance() noexcept
{
    al_effect_props_.Autowah.Resonance = clamp(
        level_mb_to_gain(static_cast<float>(props_.lResonance)),
        AL_AUTOWAH_MIN_RESONANCE,
        AL_AUTOWAH_MAX_RESONANCE);
}

void EaxAutoWahEffect::set_efx_peak_gain() noexcept
{
    al_effect_props_.Autowah.PeakGain = clamp(
        level_mb_to_gain(static_cast<float>(props_.lPeakLevel)),
        AL_AUTOWAH_MIN_PEAK_GAIN,
        AL_AUTOWAH_MAX_PEAK_GAIN);
}

void EaxAutoWahEffect::set_efx_defaults()
{
    set_efx_attack_time();
    set_efx_release_time();
    set_efx_resonance();
    set_efx_peak_gain();
}

void EaxAutoWahEffect::get(const EaxCall& call, const Props& props)
{
    switch (call.get_property_id())
    {
        case EAXAUTOWAH_NONE: break;
        case EAXAUTOWAH_ALLPARAMETERS: call.set_value<Exception>(props); break;
        case EAXAUTOWAH_ATTACKTIME: call.set_value<Exception>(props.flAttackTime); break;
        case EAXAUTOWAH_RELEASETIME: call.set_value<Exception>(props.flReleaseTime); break;
        case EAXAUTOWAH_RESONANCE: call.set_value<Exception>(props.lResonance); break;
        case EAXAUTOWAH_PEAKLEVEL: call.set_value<Exception>(props.lPeakLevel); break;
        default: fail_unknown_property_id();
    }
}

void EaxAutoWahEffect::set(const EaxCall& call, Props& props)
{
    switch (call.get_property_id())
    {
        case EAXAUTOWAH_NONE: break;
        case EAXAUTOWAH_ALLPARAMETERS: defer<AllValidator>(call, props); break;
        case EAXAUTOWAH_ATTACKTIME: defer<AttackTimeValidator>(call, props.flAttackTime); break;
        case EAXAUTOWAH_RELEASETIME: defer<ReleaseTimeValidator>(call, props.flReleaseTime); break;
        case EAXAUTOWAH_RESONANCE: defer<ResonanceValidator>(call, props.lResonance); break;
        case EAXAUTOWAH_PEAKLEVEL: defer<PeakLevelValidator>(call, props.lPeakLevel); break;
        default: fail_unknown_property_id();
    }
}

bool EaxAutoWahEffect::commit_props(const Props& props)
{
    auto is_dirty = false;

    if (props_.flAttackTime != props.flAttackTime)
    {
        is_dirty = true;
        set_efx_attack_time();
    }

    if (props_.flReleaseTime != props.flReleaseTime)
    {
        is_dirty = true;
        set_efx_release_time();
    }

    if (props_.lResonance != props.lResonance)
    {
        is_dirty = true;
        set_efx_resonance();
    }

    if (props_.lPeakLevel != props.lPeakLevel)
    {
        is_dirty = true;
        set_efx_peak_gain();
    }

    return is_dirty;
}

} // namespace

EaxEffectUPtr eax_create_eax_auto_wah_effect(const EaxCall& call)
{
    return eax_create_eax4_effect<EaxAutoWahEffect>(call);
}

#endif // ALSOFT_EAX
