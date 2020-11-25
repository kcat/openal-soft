
#include "config.h"

#include "AL/al.h"
#include "AL/efx.h"

#include "effects.h"
#include "effects/base.h"


namespace {

void Distortion_setParami(EffectProps*, ALenum param, int)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid distortion integer property 0x%04x", param}; }
void Distortion_setParamiv(EffectProps*, ALenum param, const int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid distortion integer-vector property 0x%04x",
        param};
}
void Distortion_setParamf(EffectProps *props, ALenum param, float val)
{
    switch(param)
    {
    case AL_DISTORTION_EDGE:
        if(!(val >= AL_DISTORTION_MIN_EDGE && val <= AL_DISTORTION_MAX_EDGE))
            throw effect_exception{AL_INVALID_VALUE, "Distortion edge out of range"};
        props->Distortion.Edge = val;
        break;

    case AL_DISTORTION_GAIN:
        if(!(val >= AL_DISTORTION_MIN_GAIN && val <= AL_DISTORTION_MAX_GAIN))
            throw effect_exception{AL_INVALID_VALUE, "Distortion gain out of range"};
        props->Distortion.Gain = val;
        break;

    case AL_DISTORTION_LOWPASS_CUTOFF:
        if(!(val >= AL_DISTORTION_MIN_LOWPASS_CUTOFF && val <= AL_DISTORTION_MAX_LOWPASS_CUTOFF))
            throw effect_exception{AL_INVALID_VALUE, "Distortion low-pass cutoff out of range"};
        props->Distortion.LowpassCutoff = val;
        break;

    case AL_DISTORTION_EQCENTER:
        if(!(val >= AL_DISTORTION_MIN_EQCENTER && val <= AL_DISTORTION_MAX_EQCENTER))
            throw effect_exception{AL_INVALID_VALUE, "Distortion EQ center out of range"};
        props->Distortion.EQCenter = val;
        break;

    case AL_DISTORTION_EQBANDWIDTH:
        if(!(val >= AL_DISTORTION_MIN_EQBANDWIDTH && val <= AL_DISTORTION_MAX_EQBANDWIDTH))
            throw effect_exception{AL_INVALID_VALUE, "Distortion EQ bandwidth out of range"};
        props->Distortion.EQBandwidth = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid distortion float property 0x%04x", param};
    }
}
void Distortion_setParamfv(EffectProps *props, ALenum param, const float *vals)
{ Distortion_setParamf(props, param, vals[0]); }

void Distortion_getParami(const EffectProps*, ALenum param, int*)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid distortion integer property 0x%04x", param}; }
void Distortion_getParamiv(const EffectProps*, ALenum param, int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid distortion integer-vector property 0x%04x",
        param};
}
void Distortion_getParamf(const EffectProps *props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_DISTORTION_EDGE:
        *val = props->Distortion.Edge;
        break;

    case AL_DISTORTION_GAIN:
        *val = props->Distortion.Gain;
        break;

    case AL_DISTORTION_LOWPASS_CUTOFF:
        *val = props->Distortion.LowpassCutoff;
        break;

    case AL_DISTORTION_EQCENTER:
        *val = props->Distortion.EQCenter;
        break;

    case AL_DISTORTION_EQBANDWIDTH:
        *val = props->Distortion.EQBandwidth;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid distortion float property 0x%04x", param};
    }
}
void Distortion_getParamfv(const EffectProps *props, ALenum param, float *vals)
{ Distortion_getParamf(props, param, vals); }

EffectProps genDefaultProps() noexcept
{
    EffectProps props{};
    props.Distortion.Edge = AL_DISTORTION_DEFAULT_EDGE;
    props.Distortion.Gain = AL_DISTORTION_DEFAULT_GAIN;
    props.Distortion.LowpassCutoff = AL_DISTORTION_DEFAULT_LOWPASS_CUTOFF;
    props.Distortion.EQCenter = AL_DISTORTION_DEFAULT_EQCENTER;
    props.Distortion.EQBandwidth = AL_DISTORTION_DEFAULT_EQBANDWIDTH;
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Distortion);

const EffectProps DistortionEffectProps{genDefaultProps()};
