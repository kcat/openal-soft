
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

using AutowahCommitter = EaxCommitter<EaxAutowahCommitter>;

struct AttackTimeValidator {
    void operator()(float flAttackTime) const
    {
        eax_validate_range<AutowahCommitter::Exception>(
            "Attack Time",
            flAttackTime,
            EAXAUTOWAH_MINATTACKTIME,
            EAXAUTOWAH_MAXATTACKTIME);
    }
}; // AttackTimeValidator

struct ReleaseTimeValidator {
    void operator()(float flReleaseTime) const
    {
        eax_validate_range<AutowahCommitter::Exception>(
            "Release Time",
            flReleaseTime,
            EAXAUTOWAH_MINRELEASETIME,
            EAXAUTOWAH_MAXRELEASETIME);
    }
}; // ReleaseTimeValidator

struct ResonanceValidator {
    void operator()(long lResonance) const
    {
        eax_validate_range<AutowahCommitter::Exception>(
            "Resonance",
            lResonance,
            EAXAUTOWAH_MINRESONANCE,
            EAXAUTOWAH_MAXRESONANCE);
    }
}; // ResonanceValidator

struct PeakLevelValidator {
    void operator()(long lPeakLevel) const
    {
        eax_validate_range<AutowahCommitter::Exception>(
            "Peak Level",
            lPeakLevel,
            EAXAUTOWAH_MINPEAKLEVEL,
            EAXAUTOWAH_MAXPEAKLEVEL);
    }
}; // PeakLevelValidator

struct AllValidator {
    void operator()(const EAXAUTOWAHPROPERTIES& all) const
    {
        AttackTimeValidator{}(all.flAttackTime);
        ReleaseTimeValidator{}(all.flReleaseTime);
        ResonanceValidator{}(all.lResonance);
        PeakLevelValidator{}(all.lPeakLevel);
    }
}; // AllValidator

} // namespace

template<>
struct AutowahCommitter::Exception : public EaxException
{
    explicit Exception(const char *message) : EaxException{"EAX_AUTOWAH_EFFECT", message}
    { }
};

template<>
[[noreturn]] void AutowahCommitter::fail(const char *message)
{
    throw Exception{message};
}

template<>
bool AutowahCommitter::commit(const EaxEffectProps &props)
{
    if(props.mType == mEaxProps.mType
        && mEaxProps.mAutowah.flAttackTime == props.mAutowah.flAttackTime
        && mEaxProps.mAutowah.flReleaseTime == props.mAutowah.flReleaseTime
        && mEaxProps.mAutowah.lResonance == props.mAutowah.lResonance
        && mEaxProps.mAutowah.lPeakLevel == props.mAutowah.lPeakLevel)
        return false;

    mEaxProps = props;

    mAlProps.Autowah.AttackTime = props.mAutowah.flAttackTime;
    mAlProps.Autowah.ReleaseTime = props.mAutowah.flReleaseTime;
    mAlProps.Autowah.Resonance = level_mb_to_gain(static_cast<float>(props.mAutowah.lResonance));
    mAlProps.Autowah.PeakGain = level_mb_to_gain(static_cast<float>(props.mAutowah.lPeakLevel));

    return true;
}

template<>
void AutowahCommitter::SetDefaults(EaxEffectProps &props)
{
    props.mType = EaxEffectType::Autowah;
    props.mAutowah.flAttackTime = EAXAUTOWAH_DEFAULTATTACKTIME;
    props.mAutowah.flReleaseTime = EAXAUTOWAH_DEFAULTRELEASETIME;
    props.mAutowah.lResonance = EAXAUTOWAH_DEFAULTRESONANCE;
    props.mAutowah.lPeakLevel = EAXAUTOWAH_DEFAULTPEAKLEVEL;
}

template<>
void AutowahCommitter::Get(const EaxCall &call, const EaxEffectProps &props)
{
    switch(call.get_property_id())
    {
    case EAXAUTOWAH_NONE: break;
    case EAXAUTOWAH_ALLPARAMETERS: call.set_value<Exception>(props.mAutowah); break;
    case EAXAUTOWAH_ATTACKTIME: call.set_value<Exception>(props.mAutowah.flAttackTime); break;
    case EAXAUTOWAH_RELEASETIME: call.set_value<Exception>(props.mAutowah.flReleaseTime); break;
    case EAXAUTOWAH_RESONANCE: call.set_value<Exception>(props.mAutowah.lResonance); break;
    case EAXAUTOWAH_PEAKLEVEL: call.set_value<Exception>(props.mAutowah.lPeakLevel); break;
    default: fail_unknown_property_id();
    }
}

template<>
void AutowahCommitter::Set(const EaxCall &call, EaxEffectProps &props)
{
    switch(call.get_property_id())
    {
    case EAXAUTOWAH_NONE: break;
    case EAXAUTOWAH_ALLPARAMETERS: defer<AllValidator>(call, props.mAutowah); break;
    case EAXAUTOWAH_ATTACKTIME: defer<AttackTimeValidator>(call, props.mAutowah.flAttackTime); break;
    case EAXAUTOWAH_RELEASETIME: defer<ReleaseTimeValidator>(call, props.mAutowah.flReleaseTime); break;
    case EAXAUTOWAH_RESONANCE: defer<ResonanceValidator>(call, props.mAutowah.lResonance); break;
    case EAXAUTOWAH_PEAKLEVEL: defer<PeakLevelValidator>(call, props.mAutowah.lPeakLevel); break;
    default: fail_unknown_property_id();
    }
}

#endif // ALSOFT_EAX
