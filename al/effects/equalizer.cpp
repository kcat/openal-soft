
#include "config.h"

#include "AL/al.h"
#include "AL/efx.h"

#include "effects.h"
#include "effects/base.h"


namespace {

void Equalizer_setParami(EffectProps*, ALenum param, int)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid equalizer integer property 0x%04x", param}; }
void Equalizer_setParamiv(EffectProps*, ALenum param, const int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid equalizer integer-vector property 0x%04x",
        param};
}
void Equalizer_setParamf(EffectProps *props, ALenum param, float val)
{
    switch(param)
    {
    case AL_EQUALIZER_LOW_GAIN:
        if(!(val >= AL_EQUALIZER_MIN_LOW_GAIN && val <= AL_EQUALIZER_MAX_LOW_GAIN))
            throw effect_exception{AL_INVALID_VALUE, "Equalizer low-band gain out of range"};
        props->Equalizer.LowGain = val;
        break;

    case AL_EQUALIZER_LOW_CUTOFF:
        if(!(val >= AL_EQUALIZER_MIN_LOW_CUTOFF && val <= AL_EQUALIZER_MAX_LOW_CUTOFF))
            throw effect_exception{AL_INVALID_VALUE, "Equalizer low-band cutoff out of range"};
        props->Equalizer.LowCutoff = val;
        break;

    case AL_EQUALIZER_MID1_GAIN:
        if(!(val >= AL_EQUALIZER_MIN_MID1_GAIN && val <= AL_EQUALIZER_MAX_MID1_GAIN))
            throw effect_exception{AL_INVALID_VALUE, "Equalizer mid1-band gain out of range"};
        props->Equalizer.Mid1Gain = val;
        break;

    case AL_EQUALIZER_MID1_CENTER:
        if(!(val >= AL_EQUALIZER_MIN_MID1_CENTER && val <= AL_EQUALIZER_MAX_MID1_CENTER))
            throw effect_exception{AL_INVALID_VALUE, "Equalizer mid1-band center out of range"};
        props->Equalizer.Mid1Center = val;
        break;

    case AL_EQUALIZER_MID1_WIDTH:
        if(!(val >= AL_EQUALIZER_MIN_MID1_WIDTH && val <= AL_EQUALIZER_MAX_MID1_WIDTH))
            throw effect_exception{AL_INVALID_VALUE, "Equalizer mid1-band width out of range"};
        props->Equalizer.Mid1Width = val;
        break;

    case AL_EQUALIZER_MID2_GAIN:
        if(!(val >= AL_EQUALIZER_MIN_MID2_GAIN && val <= AL_EQUALIZER_MAX_MID2_GAIN))
            throw effect_exception{AL_INVALID_VALUE, "Equalizer mid2-band gain out of range"};
        props->Equalizer.Mid2Gain = val;
        break;

    case AL_EQUALIZER_MID2_CENTER:
        if(!(val >= AL_EQUALIZER_MIN_MID2_CENTER && val <= AL_EQUALIZER_MAX_MID2_CENTER))
            throw effect_exception{AL_INVALID_VALUE, "Equalizer mid2-band center out of range"};
        props->Equalizer.Mid2Center = val;
        break;

    case AL_EQUALIZER_MID2_WIDTH:
        if(!(val >= AL_EQUALIZER_MIN_MID2_WIDTH && val <= AL_EQUALIZER_MAX_MID2_WIDTH))
            throw effect_exception{AL_INVALID_VALUE, "Equalizer mid2-band width out of range"};
        props->Equalizer.Mid2Width = val;
        break;

    case AL_EQUALIZER_HIGH_GAIN:
        if(!(val >= AL_EQUALIZER_MIN_HIGH_GAIN && val <= AL_EQUALIZER_MAX_HIGH_GAIN))
            throw effect_exception{AL_INVALID_VALUE, "Equalizer high-band gain out of range"};
        props->Equalizer.HighGain = val;
        break;

    case AL_EQUALIZER_HIGH_CUTOFF:
        if(!(val >= AL_EQUALIZER_MIN_HIGH_CUTOFF && val <= AL_EQUALIZER_MAX_HIGH_CUTOFF))
            throw effect_exception{AL_INVALID_VALUE, "Equalizer high-band cutoff out of range"};
        props->Equalizer.HighCutoff = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid equalizer float property 0x%04x", param};
    }
}
void Equalizer_setParamfv(EffectProps *props, ALenum param, const float *vals)
{ Equalizer_setParamf(props, param, vals[0]); }

void Equalizer_getParami(const EffectProps*, ALenum param, int*)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid equalizer integer property 0x%04x", param}; }
void Equalizer_getParamiv(const EffectProps*, ALenum param, int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid equalizer integer-vector property 0x%04x",
        param};
}
void Equalizer_getParamf(const EffectProps *props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_EQUALIZER_LOW_GAIN:
        *val = props->Equalizer.LowGain;
        break;

    case AL_EQUALIZER_LOW_CUTOFF:
        *val = props->Equalizer.LowCutoff;
        break;

    case AL_EQUALIZER_MID1_GAIN:
        *val = props->Equalizer.Mid1Gain;
        break;

    case AL_EQUALIZER_MID1_CENTER:
        *val = props->Equalizer.Mid1Center;
        break;

    case AL_EQUALIZER_MID1_WIDTH:
        *val = props->Equalizer.Mid1Width;
        break;

    case AL_EQUALIZER_MID2_GAIN:
        *val = props->Equalizer.Mid2Gain;
        break;

    case AL_EQUALIZER_MID2_CENTER:
        *val = props->Equalizer.Mid2Center;
        break;

    case AL_EQUALIZER_MID2_WIDTH:
        *val = props->Equalizer.Mid2Width;
        break;

    case AL_EQUALIZER_HIGH_GAIN:
        *val = props->Equalizer.HighGain;
        break;

    case AL_EQUALIZER_HIGH_CUTOFF:
        *val = props->Equalizer.HighCutoff;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid equalizer float property 0x%04x", param};
    }
}
void Equalizer_getParamfv(const EffectProps *props, ALenum param, float *vals)
{ Equalizer_getParamf(props, param, vals); }

EffectProps genDefaultProps() noexcept
{
    EffectProps props{};
    props.Equalizer.LowCutoff = AL_EQUALIZER_DEFAULT_LOW_CUTOFF;
    props.Equalizer.LowGain = AL_EQUALIZER_DEFAULT_LOW_GAIN;
    props.Equalizer.Mid1Center = AL_EQUALIZER_DEFAULT_MID1_CENTER;
    props.Equalizer.Mid1Gain = AL_EQUALIZER_DEFAULT_MID1_GAIN;
    props.Equalizer.Mid1Width = AL_EQUALIZER_DEFAULT_MID1_WIDTH;
    props.Equalizer.Mid2Center = AL_EQUALIZER_DEFAULT_MID2_CENTER;
    props.Equalizer.Mid2Gain = AL_EQUALIZER_DEFAULT_MID2_GAIN;
    props.Equalizer.Mid2Width = AL_EQUALIZER_DEFAULT_MID2_WIDTH;
    props.Equalizer.HighCutoff = AL_EQUALIZER_DEFAULT_HIGH_CUTOFF;
    props.Equalizer.HighGain = AL_EQUALIZER_DEFAULT_HIGH_GAIN;
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Equalizer);

const EffectProps EqualizerEffectProps{genDefaultProps()};
