
#include "config.h"

#include <cmath>
#include <cstdlib>

#include <algorithm>

#include "AL/efx.h"

#include "effects/base.h"
#include "effects.h"

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
