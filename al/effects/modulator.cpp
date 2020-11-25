
#include "config.h"

#include "AL/al.h"
#include "AL/efx.h"

#include "effects.h"
#include "effects/base.h"


namespace {

void Modulator_setParamf(EffectProps *props, ALenum param, float val)
{
    switch(param)
    {
    case AL_RING_MODULATOR_FREQUENCY:
        if(!(val >= AL_RING_MODULATOR_MIN_FREQUENCY && val <= AL_RING_MODULATOR_MAX_FREQUENCY))
            throw effect_exception{AL_INVALID_VALUE, "Modulator frequency out of range"};
        props->Modulator.Frequency = val;
        break;

    case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
        if(!(val >= AL_RING_MODULATOR_MIN_HIGHPASS_CUTOFF && val <= AL_RING_MODULATOR_MAX_HIGHPASS_CUTOFF))
            throw effect_exception{AL_INVALID_VALUE, "Modulator high-pass cutoff out of range"};
        props->Modulator.HighPassCutoff = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid modulator float property 0x%04x", param};
    }
}
void Modulator_setParamfv(EffectProps *props, ALenum param, const float *vals)
{ Modulator_setParamf(props, param, vals[0]); }
void Modulator_setParami(EffectProps *props, ALenum param, int val)
{
    switch(param)
    {
    case AL_RING_MODULATOR_FREQUENCY:
    case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
        Modulator_setParamf(props, param, static_cast<float>(val));
        break;

    case AL_RING_MODULATOR_WAVEFORM:
        if(!(val >= AL_RING_MODULATOR_MIN_WAVEFORM && val <= AL_RING_MODULATOR_MAX_WAVEFORM))
            throw effect_exception{AL_INVALID_VALUE, "Invalid modulator waveform"};
        props->Modulator.Waveform = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid modulator integer property 0x%04x",
            param};
    }
}
void Modulator_setParamiv(EffectProps *props, ALenum param, const int *vals)
{ Modulator_setParami(props, param, vals[0]); }

void Modulator_getParami(const EffectProps *props, ALenum param, int *val)
{
    switch(param)
    {
    case AL_RING_MODULATOR_FREQUENCY:
        *val = static_cast<int>(props->Modulator.Frequency);
        break;
    case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
        *val = static_cast<int>(props->Modulator.HighPassCutoff);
        break;
    case AL_RING_MODULATOR_WAVEFORM:
        *val = props->Modulator.Waveform;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid modulator integer property 0x%04x",
            param};
    }
}
void Modulator_getParamiv(const EffectProps *props, ALenum param, int *vals)
{ Modulator_getParami(props, param, vals); }
void Modulator_getParamf(const EffectProps *props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_RING_MODULATOR_FREQUENCY:
        *val = props->Modulator.Frequency;
        break;
    case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
        *val = props->Modulator.HighPassCutoff;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid modulator float property 0x%04x", param};
    }
}
void Modulator_getParamfv(const EffectProps *props, ALenum param, float *vals)
{ Modulator_getParamf(props, param, vals); }

EffectProps genDefaultProps() noexcept
{
    EffectProps props{};
    props.Modulator.Frequency      = AL_RING_MODULATOR_DEFAULT_FREQUENCY;
    props.Modulator.HighPassCutoff = AL_RING_MODULATOR_DEFAULT_HIGHPASS_CUTOFF;
    props.Modulator.Waveform       = AL_RING_MODULATOR_DEFAULT_WAVEFORM;
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Modulator);

const EffectProps ModulatorEffectProps{genDefaultProps()};
