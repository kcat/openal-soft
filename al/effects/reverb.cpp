
#include "config.h"

#include <cmath>

#include "AL/al.h"
#include "AL/efx.h"

#include "effects.h"
#include "effects/base.h"


namespace {

void Reverb_setParami(EffectProps *props, ALenum param, int val)
{
    switch(param)
    {
    case AL_EAXREVERB_DECAY_HFLIMIT:
        if(!(val >= AL_EAXREVERB_MIN_DECAY_HFLIMIT && val <= AL_EAXREVERB_MAX_DECAY_HFLIMIT))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb decay hflimit out of range"};
        props->Reverb.DecayHFLimit = val != AL_FALSE;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid EAX reverb integer property 0x%04x",
            param};
    }
}
void Reverb_setParamiv(EffectProps *props, ALenum param, const int *vals)
{ Reverb_setParami(props, param, vals[0]); }
void Reverb_setParamf(EffectProps *props, ALenum param, float val)
{
    switch(param)
    {
    case AL_EAXREVERB_DENSITY:
        if(!(val >= AL_EAXREVERB_MIN_DENSITY && val <= AL_EAXREVERB_MAX_DENSITY))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb density out of range"};
        props->Reverb.Density = val;
        break;

    case AL_EAXREVERB_DIFFUSION:
        if(!(val >= AL_EAXREVERB_MIN_DIFFUSION && val <= AL_EAXREVERB_MAX_DIFFUSION))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb diffusion out of range"};
        props->Reverb.Diffusion = val;
        break;

    case AL_EAXREVERB_GAIN:
        if(!(val >= AL_EAXREVERB_MIN_GAIN && val <= AL_EAXREVERB_MAX_GAIN))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb gain out of range"};
        props->Reverb.Gain = val;
        break;

    case AL_EAXREVERB_GAINHF:
        if(!(val >= AL_EAXREVERB_MIN_GAINHF && val <= AL_EAXREVERB_MAX_GAINHF))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb gainhf out of range"};
        props->Reverb.GainHF = val;
        break;

    case AL_EAXREVERB_GAINLF:
        if(!(val >= AL_EAXREVERB_MIN_GAINLF && val <= AL_EAXREVERB_MAX_GAINLF))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb gainlf out of range"};
        props->Reverb.GainLF = val;
        break;

    case AL_EAXREVERB_DECAY_TIME:
        if(!(val >= AL_EAXREVERB_MIN_DECAY_TIME && val <= AL_EAXREVERB_MAX_DECAY_TIME))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb decay time out of range"};
        props->Reverb.DecayTime = val;
        break;

    case AL_EAXREVERB_DECAY_HFRATIO:
        if(!(val >= AL_EAXREVERB_MIN_DECAY_HFRATIO && val <= AL_EAXREVERB_MAX_DECAY_HFRATIO))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb decay hfratio out of range"};
        props->Reverb.DecayHFRatio = val;
        break;

    case AL_EAXREVERB_DECAY_LFRATIO:
        if(!(val >= AL_EAXREVERB_MIN_DECAY_LFRATIO && val <= AL_EAXREVERB_MAX_DECAY_LFRATIO))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb decay lfratio out of range"};
        props->Reverb.DecayLFRatio = val;
        break;

    case AL_EAXREVERB_REFLECTIONS_GAIN:
        if(!(val >= AL_EAXREVERB_MIN_REFLECTIONS_GAIN && val <= AL_EAXREVERB_MAX_REFLECTIONS_GAIN))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb reflections gain out of range"};
        props->Reverb.ReflectionsGain = val;
        break;

    case AL_EAXREVERB_REFLECTIONS_DELAY:
        if(!(val >= AL_EAXREVERB_MIN_REFLECTIONS_DELAY && val <= AL_EAXREVERB_MAX_REFLECTIONS_DELAY))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb reflections delay out of range"};
        props->Reverb.ReflectionsDelay = val;
        break;

    case AL_EAXREVERB_LATE_REVERB_GAIN:
        if(!(val >= AL_EAXREVERB_MIN_LATE_REVERB_GAIN && val <= AL_EAXREVERB_MAX_LATE_REVERB_GAIN))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb late reverb gain out of range"};
        props->Reverb.LateReverbGain = val;
        break;

    case AL_EAXREVERB_LATE_REVERB_DELAY:
        if(!(val >= AL_EAXREVERB_MIN_LATE_REVERB_DELAY && val <= AL_EAXREVERB_MAX_LATE_REVERB_DELAY))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb late reverb delay out of range"};
        props->Reverb.LateReverbDelay = val;
        break;

    case AL_EAXREVERB_AIR_ABSORPTION_GAINHF:
        if(!(val >= AL_EAXREVERB_MIN_AIR_ABSORPTION_GAINHF && val <= AL_EAXREVERB_MAX_AIR_ABSORPTION_GAINHF))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb air absorption gainhf out of range"};
        props->Reverb.AirAbsorptionGainHF = val;
        break;

    case AL_EAXREVERB_ECHO_TIME:
        if(!(val >= AL_EAXREVERB_MIN_ECHO_TIME && val <= AL_EAXREVERB_MAX_ECHO_TIME))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb echo time out of range"};
        props->Reverb.EchoTime = val;
        break;

    case AL_EAXREVERB_ECHO_DEPTH:
        if(!(val >= AL_EAXREVERB_MIN_ECHO_DEPTH && val <= AL_EAXREVERB_MAX_ECHO_DEPTH))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb echo depth out of range"};
        props->Reverb.EchoDepth = val;
        break;

    case AL_EAXREVERB_MODULATION_TIME:
        if(!(val >= AL_EAXREVERB_MIN_MODULATION_TIME && val <= AL_EAXREVERB_MAX_MODULATION_TIME))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb modulation time out of range"};
        props->Reverb.ModulationTime = val;
        break;

    case AL_EAXREVERB_MODULATION_DEPTH:
        if(!(val >= AL_EAXREVERB_MIN_MODULATION_DEPTH && val <= AL_EAXREVERB_MAX_MODULATION_DEPTH))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb modulation depth out of range"};
        props->Reverb.ModulationDepth = val;
        break;

    case AL_EAXREVERB_HFREFERENCE:
        if(!(val >= AL_EAXREVERB_MIN_HFREFERENCE && val <= AL_EAXREVERB_MAX_HFREFERENCE))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb hfreference out of range"};
        props->Reverb.HFReference = val;
        break;

    case AL_EAXREVERB_LFREFERENCE:
        if(!(val >= AL_EAXREVERB_MIN_LFREFERENCE && val <= AL_EAXREVERB_MAX_LFREFERENCE))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb lfreference out of range"};
        props->Reverb.LFReference = val;
        break;

    case AL_EAXREVERB_ROOM_ROLLOFF_FACTOR:
        if(!(val >= AL_EAXREVERB_MIN_ROOM_ROLLOFF_FACTOR && val <= AL_EAXREVERB_MAX_ROOM_ROLLOFF_FACTOR))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb room rolloff factor out of range"};
        props->Reverb.RoomRolloffFactor = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid EAX reverb float property 0x%04x", param};
    }
}
void Reverb_setParamfv(EffectProps *props, ALenum param, const float *vals)
{
    switch(param)
    {
    case AL_EAXREVERB_REFLECTIONS_PAN:
        if(!(std::isfinite(vals[0]) && std::isfinite(vals[1]) && std::isfinite(vals[2])))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb reflections pan out of range"};
        props->Reverb.ReflectionsPan[0] = vals[0];
        props->Reverb.ReflectionsPan[1] = vals[1];
        props->Reverb.ReflectionsPan[2] = vals[2];
        break;
    case AL_EAXREVERB_LATE_REVERB_PAN:
        if(!(std::isfinite(vals[0]) && std::isfinite(vals[1]) && std::isfinite(vals[2])))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb late reverb pan out of range"};
        props->Reverb.LateReverbPan[0] = vals[0];
        props->Reverb.LateReverbPan[1] = vals[1];
        props->Reverb.LateReverbPan[2] = vals[2];
        break;

    default:
        Reverb_setParamf(props, param, vals[0]);
        break;
    }
}

void Reverb_getParami(const EffectProps *props, ALenum param, int *val)
{
    switch(param)
    {
    case AL_EAXREVERB_DECAY_HFLIMIT:
        *val = props->Reverb.DecayHFLimit;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid EAX reverb integer property 0x%04x",
            param};
    }
}
void Reverb_getParamiv(const EffectProps *props, ALenum param, int *vals)
{ Reverb_getParami(props, param, vals); }
void Reverb_getParamf(const EffectProps *props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_EAXREVERB_DENSITY:
        *val = props->Reverb.Density;
        break;

    case AL_EAXREVERB_DIFFUSION:
        *val = props->Reverb.Diffusion;
        break;

    case AL_EAXREVERB_GAIN:
        *val = props->Reverb.Gain;
        break;

    case AL_EAXREVERB_GAINHF:
        *val = props->Reverb.GainHF;
        break;

    case AL_EAXREVERB_GAINLF:
        *val = props->Reverb.GainLF;
        break;

    case AL_EAXREVERB_DECAY_TIME:
        *val = props->Reverb.DecayTime;
        break;

    case AL_EAXREVERB_DECAY_HFRATIO:
        *val = props->Reverb.DecayHFRatio;
        break;

    case AL_EAXREVERB_DECAY_LFRATIO:
        *val = props->Reverb.DecayLFRatio;
        break;

    case AL_EAXREVERB_REFLECTIONS_GAIN:
        *val = props->Reverb.ReflectionsGain;
        break;

    case AL_EAXREVERB_REFLECTIONS_DELAY:
        *val = props->Reverb.ReflectionsDelay;
        break;

    case AL_EAXREVERB_LATE_REVERB_GAIN:
        *val = props->Reverb.LateReverbGain;
        break;

    case AL_EAXREVERB_LATE_REVERB_DELAY:
        *val = props->Reverb.LateReverbDelay;
        break;

    case AL_EAXREVERB_AIR_ABSORPTION_GAINHF:
        *val = props->Reverb.AirAbsorptionGainHF;
        break;

    case AL_EAXREVERB_ECHO_TIME:
        *val = props->Reverb.EchoTime;
        break;

    case AL_EAXREVERB_ECHO_DEPTH:
        *val = props->Reverb.EchoDepth;
        break;

    case AL_EAXREVERB_MODULATION_TIME:
        *val = props->Reverb.ModulationTime;
        break;

    case AL_EAXREVERB_MODULATION_DEPTH:
        *val = props->Reverb.ModulationDepth;
        break;

    case AL_EAXREVERB_HFREFERENCE:
        *val = props->Reverb.HFReference;
        break;

    case AL_EAXREVERB_LFREFERENCE:
        *val = props->Reverb.LFReference;
        break;

    case AL_EAXREVERB_ROOM_ROLLOFF_FACTOR:
        *val = props->Reverb.RoomRolloffFactor;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid EAX reverb float property 0x%04x", param};
    }
}
void Reverb_getParamfv(const EffectProps *props, ALenum param, float *vals)
{
    switch(param)
    {
    case AL_EAXREVERB_REFLECTIONS_PAN:
        vals[0] = props->Reverb.ReflectionsPan[0];
        vals[1] = props->Reverb.ReflectionsPan[1];
        vals[2] = props->Reverb.ReflectionsPan[2];
        break;
    case AL_EAXREVERB_LATE_REVERB_PAN:
        vals[0] = props->Reverb.LateReverbPan[0];
        vals[1] = props->Reverb.LateReverbPan[1];
        vals[2] = props->Reverb.LateReverbPan[2];
        break;

    default:
        Reverb_getParamf(props, param, vals);
        break;
    }
}

EffectProps genDefaultProps() noexcept
{
    EffectProps props{};
    props.Reverb.Density   = AL_EAXREVERB_DEFAULT_DENSITY;
    props.Reverb.Diffusion = AL_EAXREVERB_DEFAULT_DIFFUSION;
    props.Reverb.Gain   = AL_EAXREVERB_DEFAULT_GAIN;
    props.Reverb.GainHF = AL_EAXREVERB_DEFAULT_GAINHF;
    props.Reverb.GainLF = AL_EAXREVERB_DEFAULT_GAINLF;
    props.Reverb.DecayTime    = AL_EAXREVERB_DEFAULT_DECAY_TIME;
    props.Reverb.DecayHFRatio = AL_EAXREVERB_DEFAULT_DECAY_HFRATIO;
    props.Reverb.DecayLFRatio = AL_EAXREVERB_DEFAULT_DECAY_LFRATIO;
    props.Reverb.ReflectionsGain   = AL_EAXREVERB_DEFAULT_REFLECTIONS_GAIN;
    props.Reverb.ReflectionsDelay  = AL_EAXREVERB_DEFAULT_REFLECTIONS_DELAY;
    props.Reverb.ReflectionsPan[0] = AL_EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ;
    props.Reverb.ReflectionsPan[1] = AL_EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ;
    props.Reverb.ReflectionsPan[2] = AL_EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ;
    props.Reverb.LateReverbGain   = AL_EAXREVERB_DEFAULT_LATE_REVERB_GAIN;
    props.Reverb.LateReverbDelay  = AL_EAXREVERB_DEFAULT_LATE_REVERB_DELAY;
    props.Reverb.LateReverbPan[0] = AL_EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ;
    props.Reverb.LateReverbPan[1] = AL_EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ;
    props.Reverb.LateReverbPan[2] = AL_EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ;
    props.Reverb.EchoTime  = AL_EAXREVERB_DEFAULT_ECHO_TIME;
    props.Reverb.EchoDepth = AL_EAXREVERB_DEFAULT_ECHO_DEPTH;
    props.Reverb.ModulationTime  = AL_EAXREVERB_DEFAULT_MODULATION_TIME;
    props.Reverb.ModulationDepth = AL_EAXREVERB_DEFAULT_MODULATION_DEPTH;
    props.Reverb.AirAbsorptionGainHF = AL_EAXREVERB_DEFAULT_AIR_ABSORPTION_GAINHF;
    props.Reverb.HFReference = AL_EAXREVERB_DEFAULT_HFREFERENCE;
    props.Reverb.LFReference = AL_EAXREVERB_DEFAULT_LFREFERENCE;
    props.Reverb.RoomRolloffFactor = AL_EAXREVERB_DEFAULT_ROOM_ROLLOFF_FACTOR;
    props.Reverb.DecayHFLimit = AL_EAXREVERB_DEFAULT_DECAY_HFLIMIT;
    return props;
}


void StdReverb_setParami(EffectProps *props, ALenum param, int val)
{
    switch(param)
    {
    case AL_REVERB_DECAY_HFLIMIT:
        if(!(val >= AL_REVERB_MIN_DECAY_HFLIMIT && val <= AL_REVERB_MAX_DECAY_HFLIMIT))
            throw effect_exception{AL_INVALID_VALUE, "Reverb decay hflimit out of range"};
        props->Reverb.DecayHFLimit = val != AL_FALSE;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid reverb integer property 0x%04x", param};
    }
}
void StdReverb_setParamiv(EffectProps *props, ALenum param, const int *vals)
{ StdReverb_setParami(props, param, vals[0]); }
void StdReverb_setParamf(EffectProps *props, ALenum param, float val)
{
    switch(param)
    {
    case AL_REVERB_DENSITY:
        if(!(val >= AL_REVERB_MIN_DENSITY && val <= AL_REVERB_MAX_DENSITY))
            throw effect_exception{AL_INVALID_VALUE, "Reverb density out of range"};
        props->Reverb.Density = val;
        break;

    case AL_REVERB_DIFFUSION:
        if(!(val >= AL_REVERB_MIN_DIFFUSION && val <= AL_REVERB_MAX_DIFFUSION))
            throw effect_exception{AL_INVALID_VALUE, "Reverb diffusion out of range"};
        props->Reverb.Diffusion = val;
        break;

    case AL_REVERB_GAIN:
        if(!(val >= AL_REVERB_MIN_GAIN && val <= AL_REVERB_MAX_GAIN))
            throw effect_exception{AL_INVALID_VALUE, "Reverb gain out of range"};
        props->Reverb.Gain = val;
        break;

    case AL_REVERB_GAINHF:
        if(!(val >= AL_REVERB_MIN_GAINHF && val <= AL_REVERB_MAX_GAINHF))
            throw effect_exception{AL_INVALID_VALUE, "Reverb gainhf out of range"};
        props->Reverb.GainHF = val;
        break;

    case AL_REVERB_DECAY_TIME:
        if(!(val >= AL_REVERB_MIN_DECAY_TIME && val <= AL_REVERB_MAX_DECAY_TIME))
            throw effect_exception{AL_INVALID_VALUE, "Reverb decay time out of range"};
        props->Reverb.DecayTime = val;
        break;

    case AL_REVERB_DECAY_HFRATIO:
        if(!(val >= AL_REVERB_MIN_DECAY_HFRATIO && val <= AL_REVERB_MAX_DECAY_HFRATIO))
            throw effect_exception{AL_INVALID_VALUE, "Reverb decay hfratio out of range"};
        props->Reverb.DecayHFRatio = val;
        break;

    case AL_REVERB_REFLECTIONS_GAIN:
        if(!(val >= AL_REVERB_MIN_REFLECTIONS_GAIN && val <= AL_REVERB_MAX_REFLECTIONS_GAIN))
            throw effect_exception{AL_INVALID_VALUE, "Reverb reflections gain out of range"};
        props->Reverb.ReflectionsGain = val;
        break;

    case AL_REVERB_REFLECTIONS_DELAY:
        if(!(val >= AL_REVERB_MIN_REFLECTIONS_DELAY && val <= AL_REVERB_MAX_REFLECTIONS_DELAY))
            throw effect_exception{AL_INVALID_VALUE, "Reverb reflections delay out of range"};
        props->Reverb.ReflectionsDelay = val;
        break;

    case AL_REVERB_LATE_REVERB_GAIN:
        if(!(val >= AL_REVERB_MIN_LATE_REVERB_GAIN && val <= AL_REVERB_MAX_LATE_REVERB_GAIN))
            throw effect_exception{AL_INVALID_VALUE, "Reverb late reverb gain out of range"};
        props->Reverb.LateReverbGain = val;
        break;

    case AL_REVERB_LATE_REVERB_DELAY:
        if(!(val >= AL_REVERB_MIN_LATE_REVERB_DELAY && val <= AL_REVERB_MAX_LATE_REVERB_DELAY))
            throw effect_exception{AL_INVALID_VALUE, "Reverb late reverb delay out of range"};
        props->Reverb.LateReverbDelay = val;
        break;

    case AL_REVERB_AIR_ABSORPTION_GAINHF:
        if(!(val >= AL_REVERB_MIN_AIR_ABSORPTION_GAINHF && val <= AL_REVERB_MAX_AIR_ABSORPTION_GAINHF))
            throw effect_exception{AL_INVALID_VALUE, "Reverb air absorption gainhf out of range"};
        props->Reverb.AirAbsorptionGainHF = val;
        break;

    case AL_REVERB_ROOM_ROLLOFF_FACTOR:
        if(!(val >= AL_REVERB_MIN_ROOM_ROLLOFF_FACTOR && val <= AL_REVERB_MAX_ROOM_ROLLOFF_FACTOR))
            throw effect_exception{AL_INVALID_VALUE, "Reverb room rolloff factor out of range"};
        props->Reverb.RoomRolloffFactor = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid reverb float property 0x%04x", param};
    }
}
void StdReverb_setParamfv(EffectProps *props, ALenum param, const float *vals)
{ StdReverb_setParamf(props, param, vals[0]); }

void StdReverb_getParami(const EffectProps *props, ALenum param, int *val)
{
    switch(param)
    {
    case AL_REVERB_DECAY_HFLIMIT:
        *val = props->Reverb.DecayHFLimit;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid reverb integer property 0x%04x", param};
    }
}
void StdReverb_getParamiv(const EffectProps *props, ALenum param, int *vals)
{ StdReverb_getParami(props, param, vals); }
void StdReverb_getParamf(const EffectProps *props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_REVERB_DENSITY:
        *val = props->Reverb.Density;
        break;

    case AL_REVERB_DIFFUSION:
        *val = props->Reverb.Diffusion;
        break;

    case AL_REVERB_GAIN:
        *val = props->Reverb.Gain;
        break;

    case AL_REVERB_GAINHF:
        *val = props->Reverb.GainHF;
        break;

    case AL_REVERB_DECAY_TIME:
        *val = props->Reverb.DecayTime;
        break;

    case AL_REVERB_DECAY_HFRATIO:
        *val = props->Reverb.DecayHFRatio;
        break;

    case AL_REVERB_REFLECTIONS_GAIN:
        *val = props->Reverb.ReflectionsGain;
        break;

    case AL_REVERB_REFLECTIONS_DELAY:
        *val = props->Reverb.ReflectionsDelay;
        break;

    case AL_REVERB_LATE_REVERB_GAIN:
        *val = props->Reverb.LateReverbGain;
        break;

    case AL_REVERB_LATE_REVERB_DELAY:
        *val = props->Reverb.LateReverbDelay;
        break;

    case AL_REVERB_AIR_ABSORPTION_GAINHF:
        *val = props->Reverb.AirAbsorptionGainHF;
        break;

    case AL_REVERB_ROOM_ROLLOFF_FACTOR:
        *val = props->Reverb.RoomRolloffFactor;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid reverb float property 0x%04x", param};
    }
}
void StdReverb_getParamfv(const EffectProps *props, ALenum param, float *vals)
{ StdReverb_getParamf(props, param, vals); }

EffectProps genDefaultStdProps() noexcept
{
    EffectProps props{};
    props.Reverb.Density   = AL_REVERB_DEFAULT_DENSITY;
    props.Reverb.Diffusion = AL_REVERB_DEFAULT_DIFFUSION;
    props.Reverb.Gain   = AL_REVERB_DEFAULT_GAIN;
    props.Reverb.GainHF = AL_REVERB_DEFAULT_GAINHF;
    props.Reverb.GainLF = 1.0f;
    props.Reverb.DecayTime    = AL_REVERB_DEFAULT_DECAY_TIME;
    props.Reverb.DecayHFRatio = AL_REVERB_DEFAULT_DECAY_HFRATIO;
    props.Reverb.DecayLFRatio = 1.0f;
    props.Reverb.ReflectionsGain   = AL_REVERB_DEFAULT_REFLECTIONS_GAIN;
    props.Reverb.ReflectionsDelay  = AL_REVERB_DEFAULT_REFLECTIONS_DELAY;
    props.Reverb.ReflectionsPan[0] = 0.0f;
    props.Reverb.ReflectionsPan[1] = 0.0f;
    props.Reverb.ReflectionsPan[2] = 0.0f;
    props.Reverb.LateReverbGain   = AL_REVERB_DEFAULT_LATE_REVERB_GAIN;
    props.Reverb.LateReverbDelay  = AL_REVERB_DEFAULT_LATE_REVERB_DELAY;
    props.Reverb.LateReverbPan[0] = 0.0f;
    props.Reverb.LateReverbPan[1] = 0.0f;
    props.Reverb.LateReverbPan[2] = 0.0f;
    props.Reverb.EchoTime  = 0.25f;
    props.Reverb.EchoDepth = 0.0f;
    props.Reverb.ModulationTime  = 0.25f;
    props.Reverb.ModulationDepth = 0.0f;
    props.Reverb.AirAbsorptionGainHF = AL_REVERB_DEFAULT_AIR_ABSORPTION_GAINHF;
    props.Reverb.HFReference = 5000.0f;
    props.Reverb.LFReference = 250.0f;
    props.Reverb.RoomRolloffFactor = AL_REVERB_DEFAULT_ROOM_ROLLOFF_FACTOR;
    props.Reverb.DecayHFLimit = AL_REVERB_DEFAULT_DECAY_HFLIMIT;
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Reverb);

const EffectProps ReverbEffectProps{genDefaultProps()};

DEFINE_ALEFFECT_VTABLE(StdReverb);

const EffectProps StdReverbEffectProps{genDefaultStdProps()};
