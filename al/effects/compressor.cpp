
#include "config.h"

#include "AL/al.h"
#include "AL/efx.h"

#include "effects.h"
#include "effects/base.h"


namespace {

void Compressor_setParami(EffectProps *props, ALenum param, int val)
{
    switch(param)
    {
    case AL_COMPRESSOR_ONOFF:
        if(!(val >= AL_COMPRESSOR_MIN_ONOFF && val <= AL_COMPRESSOR_MAX_ONOFF))
            throw effect_exception{AL_INVALID_VALUE, "Compressor state out of range"};
        props->Compressor.OnOff = (val != AL_FALSE);
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid compressor integer property 0x%04x",
            param};
    }
}
void Compressor_setParamiv(EffectProps *props, ALenum param, const int *vals)
{ Compressor_setParami(props, param, vals[0]); }
void Compressor_setParamf(EffectProps*, ALenum param, float)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid compressor float property 0x%04x", param}; }
void Compressor_setParamfv(EffectProps*, ALenum param, const float*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid compressor float-vector property 0x%04x",
        param};
}

void Compressor_getParami(const EffectProps *props, ALenum param, int *val)
{ 
    switch(param)
    {
    case AL_COMPRESSOR_ONOFF:
        *val = props->Compressor.OnOff;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid compressor integer property 0x%04x",
            param};
    }
}
void Compressor_getParamiv(const EffectProps *props, ALenum param, int *vals)
{ Compressor_getParami(props, param, vals); }
void Compressor_getParamf(const EffectProps*, ALenum param, float*)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid compressor float property 0x%04x", param}; }
void Compressor_getParamfv(const EffectProps*, ALenum param, float*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid compressor float-vector property 0x%04x",
        param};
}

EffectProps genDefaultProps() noexcept
{
    EffectProps props{};
    props.Compressor.OnOff = AL_COMPRESSOR_DEFAULT_ONOFF;
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Compressor);

const EffectProps CompressorEffectProps{genDefaultProps()};
