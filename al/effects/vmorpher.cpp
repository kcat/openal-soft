
#include "config.h"

#include "AL/al.h"
#include "AL/efx.h"

#include "effects.h"
#include "effects/base.h"


namespace {

void Vmorpher_setParami(EffectProps *props, ALenum param, int val)
{
    switch(param)
    {
    case AL_VOCAL_MORPHER_WAVEFORM:
        if(!(val >= AL_VOCAL_MORPHER_MIN_WAVEFORM && val <= AL_VOCAL_MORPHER_MAX_WAVEFORM))
            throw effect_exception{AL_INVALID_VALUE, "Vocal morpher waveform out of range"};
        props->Vmorpher.Waveform = val;
        break;

    case AL_VOCAL_MORPHER_PHONEMEA:
        if(!(val >= AL_VOCAL_MORPHER_MIN_PHONEMEA && val <= AL_VOCAL_MORPHER_MAX_PHONEMEA))
            throw effect_exception{AL_INVALID_VALUE, "Vocal morpher phoneme-a out of range"};
        props->Vmorpher.PhonemeA = val;
        break;

    case AL_VOCAL_MORPHER_PHONEMEB:
        if(!(val >= AL_VOCAL_MORPHER_MIN_PHONEMEB && val <= AL_VOCAL_MORPHER_MAX_PHONEMEB))
            throw effect_exception{AL_INVALID_VALUE, "Vocal morpher phoneme-b out of range"};
        props->Vmorpher.PhonemeB = val;
        break;

    case AL_VOCAL_MORPHER_PHONEMEA_COARSE_TUNING:
        if(!(val >= AL_VOCAL_MORPHER_MIN_PHONEMEA_COARSE_TUNING && val <= AL_VOCAL_MORPHER_MAX_PHONEMEA_COARSE_TUNING))
            throw effect_exception{AL_INVALID_VALUE, "Vocal morpher phoneme-a coarse tuning out of range"};
        props->Vmorpher.PhonemeACoarseTuning = val;
        break;

    case AL_VOCAL_MORPHER_PHONEMEB_COARSE_TUNING:
        if(!(val >= AL_VOCAL_MORPHER_MIN_PHONEMEB_COARSE_TUNING && val <= AL_VOCAL_MORPHER_MAX_PHONEMEB_COARSE_TUNING))
            throw effect_exception{AL_INVALID_VALUE, "Vocal morpher phoneme-b coarse tuning out of range"};
        props->Vmorpher.PhonemeBCoarseTuning = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid vocal morpher integer property 0x%04x",
            param};
    }
}
void Vmorpher_setParamiv(EffectProps*, ALenum param, const int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid vocal morpher integer-vector property 0x%04x",
        param};
}
void Vmorpher_setParamf(EffectProps *props, ALenum param, float val)
{
    switch(param)
    {
    case AL_VOCAL_MORPHER_RATE:
        if(!(val >= AL_VOCAL_MORPHER_MIN_RATE && val <= AL_VOCAL_MORPHER_MAX_RATE))
            throw effect_exception{AL_INVALID_VALUE, "Vocal morpher rate out of range"};
        props->Vmorpher.Rate = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid vocal morpher float property 0x%04x",
            param};
    }
}
void Vmorpher_setParamfv(EffectProps *props, ALenum param, const float *vals)
{ Vmorpher_setParamf(props, param, vals[0]); }

void Vmorpher_getParami(const EffectProps *props, ALenum param, int* val)
{
    switch(param)
    {
    case AL_VOCAL_MORPHER_PHONEMEA:
        *val = props->Vmorpher.PhonemeA;
        break;

    case AL_VOCAL_MORPHER_PHONEMEB:
        *val = props->Vmorpher.PhonemeB;
        break;

    case AL_VOCAL_MORPHER_PHONEMEA_COARSE_TUNING:
        *val = props->Vmorpher.PhonemeACoarseTuning;
        break;

    case AL_VOCAL_MORPHER_PHONEMEB_COARSE_TUNING:
        *val = props->Vmorpher.PhonemeBCoarseTuning;
        break;

    case AL_VOCAL_MORPHER_WAVEFORM:
        *val = props->Vmorpher.Waveform;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid vocal morpher integer property 0x%04x",
            param};
    }
}
void Vmorpher_getParamiv(const EffectProps*, ALenum param, int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid vocal morpher integer-vector property 0x%04x",
        param};
}
void Vmorpher_getParamf(const EffectProps *props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_VOCAL_MORPHER_RATE:
        *val = props->Vmorpher.Rate;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid vocal morpher float property 0x%04x",
            param};
    }
}
void Vmorpher_getParamfv(const EffectProps *props, ALenum param, float *vals)
{ Vmorpher_getParamf(props, param, vals); }

EffectProps genDefaultProps() noexcept
{
    EffectProps props{};
    props.Vmorpher.Rate                 = AL_VOCAL_MORPHER_DEFAULT_RATE;
    props.Vmorpher.PhonemeA             = AL_VOCAL_MORPHER_DEFAULT_PHONEMEA;
    props.Vmorpher.PhonemeB             = AL_VOCAL_MORPHER_DEFAULT_PHONEMEB;
    props.Vmorpher.PhonemeACoarseTuning = AL_VOCAL_MORPHER_DEFAULT_PHONEMEA_COARSE_TUNING;
    props.Vmorpher.PhonemeBCoarseTuning = AL_VOCAL_MORPHER_DEFAULT_PHONEMEB_COARSE_TUNING;
    props.Vmorpher.Waveform             = AL_VOCAL_MORPHER_DEFAULT_WAVEFORM;
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Vmorpher);

const EffectProps VmorpherEffectProps{genDefaultProps()};
