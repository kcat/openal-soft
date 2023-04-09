
#include "config.h"

#include <cmath>

#include "AL/al.h"
#include "AL/efx.h"

#include "alc/effects/base.h"
#include "effects.h"

#ifdef ALSOFT_EAX
#include <cassert>
#include "alnumeric.h"
#include "AL/efx-presets.h"
#include "al/eax/exception.h"
#include "al/eax/utils.h"
#endif // ALSOFT_EAX


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

    case AL_EAXREVERB_AIR_ABSORPTION_GAINHF:
        if(!(val >= AL_EAXREVERB_MIN_AIR_ABSORPTION_GAINHF && val <= AL_EAXREVERB_MAX_AIR_ABSORPTION_GAINHF))
            throw effect_exception{AL_INVALID_VALUE, "EAX Reverb air absorption gainhf out of range"};
        props->Reverb.AirAbsorptionGainHF = val;
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

    case AL_EAXREVERB_AIR_ABSORPTION_GAINHF:
        *val = props->Reverb.AirAbsorptionGainHF;
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

#ifdef ALSOFT_EAX
namespace {

class EaxReverbEffectException : public EaxException
{
public:
    explicit EaxReverbEffectException(const char* message)
        : EaxException{"EAX_REVERB_EFFECT", message}
    {}
}; // EaxReverbEffectException

struct EnvironmentValidator1 {
    void operator()(unsigned long ulEnvironment) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Environment",
            ulEnvironment,
            EAXREVERB_MINENVIRONMENT,
            EAX1REVERB_MAXENVIRONMENT);
    }
}; // EnvironmentValidator1

struct VolumeValidator {
    void operator()(float volume) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Volume",
            volume,
            EAX1REVERB_MINVOLUME,
            EAX1REVERB_MAXVOLUME);
    }
}; // VolumeValidator

struct DecayTimeValidator {
    void operator()(float flDecayTime) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Decay Time",
            flDecayTime,
            EAXREVERB_MINDECAYTIME,
            EAXREVERB_MAXDECAYTIME);
    }
}; // DecayTimeValidator

struct DampingValidator {
    void operator()(float damping) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Damping",
            damping,
            EAX1REVERB_MINDAMPING,
            EAX1REVERB_MAXDAMPING);
    }
}; // DampingValidator

struct AllValidator1 {
    void operator()(const EAX_REVERBPROPERTIES& all) const
    {
        EnvironmentValidator1{}(all.environment);
        VolumeValidator{}(all.fVolume);
        DecayTimeValidator{}(all.fDecayTime_sec);
        DampingValidator{}(all.fDamping);
    }
}; // AllValidator1

struct RoomValidator {
    void operator()(long lRoom) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Room",
            lRoom,
            EAXREVERB_MINROOM,
            EAXREVERB_MAXROOM);
    }
}; // RoomValidator

struct RoomHFValidator {
    void operator()(long lRoomHF) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Room HF",
            lRoomHF,
            EAXREVERB_MINROOMHF,
            EAXREVERB_MAXROOMHF);
    }
}; // RoomHFValidator

struct RoomRolloffFactorValidator {
    void operator()(float flRoomRolloffFactor) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Room Rolloff Factor",
            flRoomRolloffFactor,
            EAXREVERB_MINROOMROLLOFFFACTOR,
            EAXREVERB_MAXROOMROLLOFFFACTOR);
    }
}; // RoomRolloffFactorValidator

struct DecayHFRatioValidator {
    void operator()(float flDecayHFRatio) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Decay HF Ratio",
            flDecayHFRatio,
            EAXREVERB_MINDECAYHFRATIO,
            EAXREVERB_MAXDECAYHFRATIO);
    }
}; // DecayHFRatioValidator

struct ReflectionsValidator {
    void operator()(long lReflections) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Reflections",
            lReflections,
            EAXREVERB_MINREFLECTIONS,
            EAXREVERB_MAXREFLECTIONS);
    }
}; // ReflectionsValidator

struct ReflectionsDelayValidator {
    void operator()(float flReflectionsDelay) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Reflections Delay",
            flReflectionsDelay,
            EAXREVERB_MINREFLECTIONSDELAY,
            EAXREVERB_MAXREFLECTIONSDELAY);
    }
}; // ReflectionsDelayValidator

struct ReverbValidator {
    void operator()(long lReverb) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Reverb",
            lReverb,
            EAXREVERB_MINREVERB,
            EAXREVERB_MAXREVERB);
    }
}; // ReverbValidator

struct ReverbDelayValidator {
    void operator()(float flReverbDelay) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Reverb Delay",
            flReverbDelay,
            EAXREVERB_MINREVERBDELAY,
            EAXREVERB_MAXREVERBDELAY);
    }
}; // ReverbDelayValidator

struct EnvironmentSizeValidator {
    void operator()(float flEnvironmentSize) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Environment Size",
            flEnvironmentSize,
            EAXREVERB_MINENVIRONMENTSIZE,
            EAXREVERB_MAXENVIRONMENTSIZE);
    }
}; // EnvironmentSizeValidator

struct EnvironmentDiffusionValidator {
    void operator()(float flEnvironmentDiffusion) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Environment Diffusion",
            flEnvironmentDiffusion,
            EAXREVERB_MINENVIRONMENTDIFFUSION,
            EAXREVERB_MAXENVIRONMENTDIFFUSION);
    }
}; // EnvironmentDiffusionValidator

struct AirAbsorptionHFValidator {
    void operator()(float flAirAbsorptionHF) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Air Absorbtion HF",
            flAirAbsorptionHF,
            EAXREVERB_MINAIRABSORPTIONHF,
            EAXREVERB_MAXAIRABSORPTIONHF);
    }
}; // AirAbsorptionHFValidator

struct FlagsValidator2 {
    void operator()(unsigned long ulFlags) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Flags",
            ulFlags,
            0UL,
            ~EAX2LISTENERFLAGS_RESERVED);
    }
}; // FlagsValidator2

struct AllValidator2 {
    void operator()(const EAX20LISTENERPROPERTIES& all) const
    {
        RoomValidator{}(all.lRoom);
        RoomHFValidator{}(all.lRoomHF);
        RoomRolloffFactorValidator{}(all.flRoomRolloffFactor);
        DecayTimeValidator{}(all.flDecayTime);
        DecayHFRatioValidator{}(all.flDecayHFRatio);
        ReflectionsValidator{}(all.lReflections);
        ReflectionsDelayValidator{}(all.flReflectionsDelay);
        ReverbValidator{}(all.lReverb);
        ReverbDelayValidator{}(all.flReverbDelay);
        EnvironmentValidator1{}(all.dwEnvironment);
        EnvironmentSizeValidator{}(all.flEnvironmentSize);
        EnvironmentDiffusionValidator{}(all.flEnvironmentDiffusion);
        AirAbsorptionHFValidator{}(all.flAirAbsorptionHF);
        FlagsValidator2{}(all.dwFlags);
    }
}; // AllValidator2

struct EnvironmentValidator3 {
    void operator()(unsigned long ulEnvironment) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Environment",
            ulEnvironment,
            EAXREVERB_MINENVIRONMENT,
            EAX30REVERB_MAXENVIRONMENT);
    }
}; // EnvironmentValidator1

struct RoomLFValidator {
    void operator()(long lRoomLF) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Room LF",
            lRoomLF,
            EAXREVERB_MINROOMLF,
            EAXREVERB_MAXROOMLF);
    }
}; // RoomLFValidator

struct DecayLFRatioValidator {
    void operator()(float flDecayLFRatio) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Decay LF Ratio",
            flDecayLFRatio,
            EAXREVERB_MINDECAYLFRATIO,
            EAXREVERB_MAXDECAYLFRATIO);
    }
}; // DecayLFRatioValidator

struct VectorValidator {
    void operator()(const EAXVECTOR&) const
    {}
}; // VectorValidator

struct EchoTimeValidator {
    void operator()(float flEchoTime) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Echo Time",
            flEchoTime,
            EAXREVERB_MINECHOTIME,
            EAXREVERB_MAXECHOTIME);
    }
}; // EchoTimeValidator

struct EchoDepthValidator {
    void operator()(float flEchoDepth) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Echo Depth",
            flEchoDepth,
            EAXREVERB_MINECHODEPTH,
            EAXREVERB_MAXECHODEPTH);
    }
}; // EchoDepthValidator

struct ModulationTimeValidator {
    void operator()(float flModulationTime) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Modulation Time",
            flModulationTime,
            EAXREVERB_MINMODULATIONTIME,
            EAXREVERB_MAXMODULATIONTIME);
    }
}; // ModulationTimeValidator

struct ModulationDepthValidator {
    void operator()(float flModulationDepth) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Modulation Depth",
            flModulationDepth,
            EAXREVERB_MINMODULATIONDEPTH,
            EAXREVERB_MAXMODULATIONDEPTH);
    }
}; // ModulationDepthValidator

struct HFReferenceValidator {
    void operator()(float flHFReference) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "HF Reference",
            flHFReference,
            EAXREVERB_MINHFREFERENCE,
            EAXREVERB_MAXHFREFERENCE);
    }
}; // HFReferenceValidator

struct LFReferenceValidator {
    void operator()(float flLFReference) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "LF Reference",
            flLFReference,
            EAXREVERB_MINLFREFERENCE,
            EAXREVERB_MAXLFREFERENCE);
    }
}; // LFReferenceValidator

struct FlagsValidator3 {
    void operator()(unsigned long ulFlags) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Flags",
            ulFlags,
            0UL,
            ~EAXREVERBFLAGS_RESERVED);
    }
}; // FlagsValidator3

struct AllValidator3 {
    void operator()(const EAXREVERBPROPERTIES& all) const
    {
        EnvironmentValidator3{}(all.ulEnvironment);
        EnvironmentSizeValidator{}(all.flEnvironmentSize);
        EnvironmentDiffusionValidator{}(all.flEnvironmentDiffusion);
        RoomValidator{}(all.lRoom);
        RoomHFValidator{}(all.lRoomHF);
        RoomLFValidator{}(all.lRoomLF);
        DecayTimeValidator{}(all.flDecayTime);
        DecayHFRatioValidator{}(all.flDecayHFRatio);
        DecayLFRatioValidator{}(all.flDecayLFRatio);
        ReflectionsValidator{}(all.lReflections);
        ReflectionsDelayValidator{}(all.flReflectionsDelay);
        VectorValidator{}(all.vReflectionsPan);
        ReverbValidator{}(all.lReverb);
        ReverbDelayValidator{}(all.flReverbDelay);
        VectorValidator{}(all.vReverbPan);
        EchoTimeValidator{}(all.flEchoTime);
        EchoDepthValidator{}(all.flEchoDepth);
        ModulationTimeValidator{}(all.flModulationTime);
        ModulationDepthValidator{}(all.flModulationDepth);
        AirAbsorptionHFValidator{}(all.flAirAbsorptionHF);
        HFReferenceValidator{}(all.flHFReference);
        LFReferenceValidator{}(all.flLFReference);
        RoomRolloffFactorValidator{}(all.flRoomRolloffFactor);
        FlagsValidator3{}(all.ulFlags);
    }
}; // AllValidator3

struct EnvironmentDeferrer2 {
    void operator()(EAX20LISTENERPROPERTIES& props, unsigned long dwEnvironment) const
    {
        props = EAX2REVERB_PRESETS[dwEnvironment];
    }
}; // EnvironmentDeferrer2

struct EnvironmentSizeDeferrer2 {
    void operator()(EAX20LISTENERPROPERTIES& props, float flEnvironmentSize) const
    {
        if (props.flEnvironmentSize == flEnvironmentSize)
        {
            return;
        }

        const auto scale = flEnvironmentSize / props.flEnvironmentSize;
        props.flEnvironmentSize = flEnvironmentSize;

        if ((props.dwFlags & EAX2LISTENERFLAGS_DECAYTIMESCALE) != 0)
        {
            props.flDecayTime = clamp(
                props.flDecayTime * scale,
                EAXREVERB_MINDECAYTIME,
                EAXREVERB_MAXDECAYTIME);
        }

        if ((props.dwFlags & EAX2LISTENERFLAGS_REFLECTIONSSCALE) != 0 &&
            (props.dwFlags & EAX2LISTENERFLAGS_REFLECTIONSDELAYSCALE) != 0)
        {
            props.lReflections = clamp(
                props.lReflections - static_cast<long>(gain_to_level_mb(scale)),
                EAXREVERB_MINREFLECTIONS,
                EAXREVERB_MAXREFLECTIONS);
        }

        if ((props.dwFlags & EAX2LISTENERFLAGS_REFLECTIONSDELAYSCALE) != 0)
        {
            props.flReflectionsDelay = clamp(
                props.flReflectionsDelay * scale,
                EAXREVERB_MINREFLECTIONSDELAY,
                EAXREVERB_MAXREFLECTIONSDELAY);
        }

        if ((props.dwFlags & EAX2LISTENERFLAGS_REVERBSCALE) != 0)
        {
            const auto log_scalar = ((props.dwFlags & EAXREVERBFLAGS_DECAYTIMESCALE) != 0) ? 2'000.0F : 3'000.0F;

            props.lReverb = clamp(
                props.lReverb - static_cast<long>(std::log10(scale) * log_scalar),
                EAXREVERB_MINREVERB,
                EAXREVERB_MAXREVERB);
        }

        if ((props.dwFlags & EAX2LISTENERFLAGS_REVERBDELAYSCALE) != 0)
        {
            props.flReverbDelay = clamp(
                props.flReverbDelay * scale,
                EAXREVERB_MINREVERBDELAY,
                EAXREVERB_MAXREVERBDELAY);
        }
    }
}; // EnvironmentSizeDeferrer2

struct EnvironmentDeferrer3 {
    void operator()(EAXREVERBPROPERTIES& props, unsigned long ulEnvironment) const
    {
        if (ulEnvironment == EAX_ENVIRONMENT_UNDEFINED)
        {
            props.ulEnvironment = EAX_ENVIRONMENT_UNDEFINED;
            return;
        }

        props = EAXREVERB_PRESETS[ulEnvironment];
    }
}; // EnvironmentDeferrer3

struct EnvironmentSizeDeferrer3 {
    void operator()(EAXREVERBPROPERTIES& props, float flEnvironmentSize) const
    {
        if (props.flEnvironmentSize == flEnvironmentSize)
        {
            return;
        }

        const auto scale = flEnvironmentSize / props.flEnvironmentSize;
        props.ulEnvironment = EAX_ENVIRONMENT_UNDEFINED;
        props.flEnvironmentSize = flEnvironmentSize;

        if ((props.ulFlags & EAXREVERBFLAGS_DECAYTIMESCALE) != 0)
        {
            props.flDecayTime = clamp(
                props.flDecayTime * scale,
                EAXREVERB_MINDECAYTIME,
                EAXREVERB_MAXDECAYTIME);
        }

        if ((props.ulFlags & EAXREVERBFLAGS_REFLECTIONSSCALE) != 0 &&
            (props.ulFlags & EAXREVERBFLAGS_REFLECTIONSDELAYSCALE) != 0)
        {
            props.lReflections = clamp(
                props.lReflections - static_cast<long>(gain_to_level_mb(scale)),
                EAXREVERB_MINREFLECTIONS,
                EAXREVERB_MAXREFLECTIONS);
        }

        if ((props.ulFlags & EAXREVERBFLAGS_REFLECTIONSDELAYSCALE) != 0)
        {
            props.flReflectionsDelay = clamp(
                props.flReflectionsDelay * scale,
                EAXREVERB_MINREFLECTIONSDELAY,
                EAXREVERB_MAXREFLECTIONSDELAY);
        }

        if ((props.ulFlags & EAXREVERBFLAGS_REVERBSCALE) != 0)
        {
            const auto log_scalar = ((props.ulFlags & EAXREVERBFLAGS_DECAYTIMESCALE) != 0) ? 2'000.0F : 3'000.0F;
            props.lReverb = clamp(
                props.lReverb - static_cast<long>(std::log10(scale) * log_scalar),
                EAXREVERB_MINREVERB,
                EAXREVERB_MAXREVERB);
        }

        if ((props.ulFlags & EAXREVERBFLAGS_REVERBDELAYSCALE) != 0)
        {
            props.flReverbDelay = clamp(
                props.flReverbDelay * scale,
                EAXREVERB_MINREVERBDELAY,
                EAXREVERB_MAXREVERBDELAY);
        }

        if ((props.ulFlags & EAXREVERBFLAGS_ECHOTIMESCALE) != 0)
        {
            props.flEchoTime = clamp(
                props.flEchoTime * scale,
                EAXREVERB_MINECHOTIME,
                EAXREVERB_MAXECHOTIME);
        }

        if ((props.ulFlags & EAXREVERBFLAGS_MODULATIONTIMESCALE) != 0)
        {
            props.flModulationTime = clamp(
                props.flModulationTime * scale,
                EAXREVERB_MINMODULATIONTIME,
                EAXREVERB_MAXMODULATIONTIME);
        }
    }
}; // EnvironmentSizeDeferrer3

} // namespace


struct EaxReverbCommitter::Exception : public EaxReverbEffectException
{
    using EaxReverbEffectException::EaxReverbEffectException;
};

[[noreturn]] void EaxReverbCommitter::fail(const char* message)
{
    throw Exception{message};
}

void EaxReverbCommitter::translate(const EAX_REVERBPROPERTIES& src, EaxEffectProps& dst) noexcept
{
    assert(src.environment <= EAX1REVERB_MAXENVIRONMENT);
    dst.mType = EaxEffectType::Reverb;
    dst.mReverb = EAXREVERB_PRESETS[src.environment];
    dst.mReverb.flDecayTime = src.fDecayTime_sec;
    dst.mReverb.flDecayHFRatio = src.fDamping;
    dst.mReverb.lReverb = mini(static_cast<int>(gain_to_level_mb(src.fVolume)), 0);
}

void EaxReverbCommitter::translate(const EAX20LISTENERPROPERTIES& src, EaxEffectProps& dst) noexcept
{
    assert(src.dwEnvironment <= EAX1REVERB_MAXENVIRONMENT);
    const auto& env = EAXREVERB_PRESETS[src.dwEnvironment];
    dst.mType = EaxEffectType::Reverb;
    dst.mReverb.ulEnvironment = src.dwEnvironment;
    dst.mReverb.flEnvironmentSize = src.flEnvironmentSize;
    dst.mReverb.flEnvironmentDiffusion = src.flEnvironmentDiffusion;
    dst.mReverb.lRoom = src.lRoom;
    dst.mReverb.lRoomHF = src.lRoomHF;
    dst.mReverb.lRoomLF = env.lRoomLF;
    dst.mReverb.flDecayTime = src.flDecayTime;
    dst.mReverb.flDecayHFRatio = src.flDecayHFRatio;
    dst.mReverb.flDecayLFRatio = env.flDecayLFRatio;
    dst.mReverb.lReflections = src.lReflections;
    dst.mReverb.flReflectionsDelay = src.flReflectionsDelay;
    dst.mReverb.vReflectionsPan = env.vReflectionsPan;
    dst.mReverb.lReverb = src.lReverb;
    dst.mReverb.flReverbDelay = src.flReverbDelay;
    dst.mReverb.vReverbPan = env.vReverbPan;
    dst.mReverb.flEchoTime = env.flEchoTime;
    dst.mReverb.flEchoDepth = env.flEchoDepth;
    dst.mReverb.flModulationTime = env.flModulationTime;
    dst.mReverb.flModulationDepth = env.flModulationDepth;
    dst.mReverb.flAirAbsorptionHF = src.flAirAbsorptionHF;
    dst.mReverb.flHFReference = env.flHFReference;
    dst.mReverb.flLFReference = env.flLFReference;
    dst.mReverb.flRoomRolloffFactor = src.flRoomRolloffFactor;
    dst.mReverb.ulFlags = src.dwFlags;
}

void EaxReverbCommitter::translate(const EAXREVERBPROPERTIES& src, EaxEffectProps& dst) noexcept
{
    dst.mType = EaxEffectType::Reverb;
    dst.mReverb = src;
}

bool EaxReverbCommitter::commit(const EAX_REVERBPROPERTIES &props)
{
    EaxEffectProps dst{};
    translate(props, dst);
    return commit(dst);
}

bool EaxReverbCommitter::commit(const EAX20LISTENERPROPERTIES &props)
{
    EaxEffectProps dst{};
    translate(props, dst);
    return commit(dst);
}

bool EaxReverbCommitter::commit(const EAXREVERBPROPERTIES &props)
{
    EaxEffectProps dst{};
    translate(props, dst);
    return commit(dst);
}

bool EaxReverbCommitter::commit(const EaxEffectProps &props)
{
    if(props.mType == mEaxProps.mType
        && memcmp(&props.mReverb, &mEaxProps.mReverb, sizeof(mEaxProps.mReverb)) == 0)
        return false;

    mEaxProps = props;

    const auto size = props.mReverb.flEnvironmentSize;
    const auto density = (size * size * size) / 16.0F;
    mAlProps.Reverb.Density = std::min(density, AL_EAXREVERB_MAX_DENSITY);
    mAlProps.Reverb.Diffusion = props.mReverb.flEnvironmentDiffusion;
    mAlProps.Reverb.Gain = level_mb_to_gain(static_cast<float>(props.mReverb.lRoom));
    mAlProps.Reverb.GainHF = level_mb_to_gain(static_cast<float>(props.mReverb.lRoomHF));
    mAlProps.Reverb.GainLF = level_mb_to_gain(static_cast<float>(props.mReverb.lRoomLF));
    mAlProps.Reverb.DecayTime = props.mReverb.flDecayTime;
    mAlProps.Reverb.DecayHFRatio = props.mReverb.flDecayHFRatio;
    mAlProps.Reverb.DecayLFRatio = mEaxProps.mReverb.flDecayLFRatio;
    mAlProps.Reverb.ReflectionsGain = level_mb_to_gain(static_cast<float>(props.mReverb.lReflections));
    mAlProps.Reverb.ReflectionsDelay = props.mReverb.flReflectionsDelay;
    mAlProps.Reverb.ReflectionsPan[0] = props.mReverb.vReflectionsPan.x;
    mAlProps.Reverb.ReflectionsPan[1] = props.mReverb.vReflectionsPan.y;
    mAlProps.Reverb.ReflectionsPan[2] = props.mReverb.vReflectionsPan.z;
    mAlProps.Reverb.LateReverbGain = level_mb_to_gain(static_cast<float>(props.mReverb.lReverb));
    mAlProps.Reverb.LateReverbDelay = props.mReverb.flReverbDelay;
    mAlProps.Reverb.LateReverbPan[0] = props.mReverb.vReverbPan.x;
    mAlProps.Reverb.LateReverbPan[1] = props.mReverb.vReverbPan.y;
    mAlProps.Reverb.LateReverbPan[2] = props.mReverb.vReverbPan.z;
    mAlProps.Reverb.EchoTime = props.mReverb.flEchoTime;
    mAlProps.Reverb.EchoDepth = props.mReverb.flEchoDepth;
    mAlProps.Reverb.ModulationTime = props.mReverb.flModulationTime;
    mAlProps.Reverb.ModulationDepth = props.mReverb.flModulationDepth;
    mAlProps.Reverb.AirAbsorptionGainHF = level_mb_to_gain(props.mReverb.flAirAbsorptionHF);
    mAlProps.Reverb.HFReference = props.mReverb.flHFReference;
    mAlProps.Reverb.LFReference = props.mReverb.flLFReference;
    mAlProps.Reverb.RoomRolloffFactor = props.mReverb.flRoomRolloffFactor;
    mAlProps.Reverb.DecayHFLimit = ((props.mReverb.ulFlags & EAXREVERBFLAGS_DECAYHFLIMIT) != 0);
    return true;
}

void EaxReverbCommitter::SetDefaults(EAX_REVERBPROPERTIES &props)
{
    props = EAX1REVERB_PRESETS[EAX_ENVIRONMENT_GENERIC];
}

void EaxReverbCommitter::SetDefaults(EAX20LISTENERPROPERTIES &props)
{
    props = EAX2REVERB_PRESETS[EAX2_ENVIRONMENT_GENERIC];
    props.lRoom = -10'000L;
}

void EaxReverbCommitter::SetDefaults(EAXREVERBPROPERTIES &props)
{
    props = EAXREVERB_PRESETS[EAX_ENVIRONMENT_GENERIC];
}

void EaxReverbCommitter::SetDefaults(EaxEffectProps &props)
{
    props.mType = EaxEffectType::Reverb;
    SetDefaults(props.mReverb);
}


void EaxReverbCommitter::Get(const EaxCall &call, const EAX_REVERBPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case DSPROPERTY_EAX_ALL: call.set_value<Exception>(props); break;
    case DSPROPERTY_EAX_ENVIRONMENT: call.set_value<Exception>(props.environment); break;
    case DSPROPERTY_EAX_VOLUME: call.set_value<Exception>(props.fVolume); break;
    case DSPROPERTY_EAX_DECAYTIME: call.set_value<Exception>(props.fDecayTime_sec); break;
    case DSPROPERTY_EAX_DAMPING: call.set_value<Exception>(props.fDamping); break;
    default: fail_unknown_property_id();
    }
}

void EaxReverbCommitter::Get(const EaxCall &call, const EAX20LISTENERPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case DSPROPERTY_EAX20LISTENER_NONE: break;
    case DSPROPERTY_EAX20LISTENER_ALLPARAMETERS: call.set_value<Exception>(props); break;
    case DSPROPERTY_EAX20LISTENER_ROOM: call.set_value<Exception>(props.lRoom); break;
    case DSPROPERTY_EAX20LISTENER_ROOMHF: call.set_value<Exception>(props.lRoomHF); break;
    case DSPROPERTY_EAX20LISTENER_ROOMROLLOFFFACTOR: call.set_value<Exception>(props.flRoomRolloffFactor); break;
    case DSPROPERTY_EAX20LISTENER_DECAYTIME: call.set_value<Exception>(props.flDecayTime); break;
    case DSPROPERTY_EAX20LISTENER_DECAYHFRATIO: call.set_value<Exception>(props.flDecayHFRatio); break;
    case DSPROPERTY_EAX20LISTENER_REFLECTIONS: call.set_value<Exception>(props.lReflections); break;
    case DSPROPERTY_EAX20LISTENER_REFLECTIONSDELAY: call.set_value<Exception>(props.flReflectionsDelay); break;
    case DSPROPERTY_EAX20LISTENER_REVERB: call.set_value<Exception>(props.lReverb); break;
    case DSPROPERTY_EAX20LISTENER_REVERBDELAY: call.set_value<Exception>(props.flReverbDelay); break;
    case DSPROPERTY_EAX20LISTENER_ENVIRONMENT: call.set_value<Exception>(props.dwEnvironment); break;
    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTSIZE: call.set_value<Exception>(props.flEnvironmentSize); break;
    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTDIFFUSION: call.set_value<Exception>(props.flEnvironmentDiffusion); break;
    case DSPROPERTY_EAX20LISTENER_AIRABSORPTIONHF: call.set_value<Exception>(props.flAirAbsorptionHF); break;
    case DSPROPERTY_EAX20LISTENER_FLAGS: call.set_value<Exception>(props.dwFlags); break;
    default: fail_unknown_property_id();
    }
}

void EaxReverbCommitter::Get(const EaxCall &call, const EAXREVERBPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case EAXREVERB_NONE: break;
    case EAXREVERB_ALLPARAMETERS: call.set_value<Exception>(props); break;
    case EAXREVERB_ENVIRONMENT: call.set_value<Exception>(props.ulEnvironment); break;
    case EAXREVERB_ENVIRONMENTSIZE: call.set_value<Exception>(props.flEnvironmentSize); break;
    case EAXREVERB_ENVIRONMENTDIFFUSION: call.set_value<Exception>(props.flEnvironmentDiffusion); break;
    case EAXREVERB_ROOM: call.set_value<Exception>(props.lRoom); break;
    case EAXREVERB_ROOMHF: call.set_value<Exception>(props.lRoomHF); break;
    case EAXREVERB_ROOMLF: call.set_value<Exception>(props.lRoomLF); break;
    case EAXREVERB_DECAYTIME: call.set_value<Exception>(props.flDecayTime); break;
    case EAXREVERB_DECAYHFRATIO: call.set_value<Exception>(props.flDecayHFRatio); break;
    case EAXREVERB_DECAYLFRATIO: call.set_value<Exception>(props.flDecayLFRatio); break;
    case EAXREVERB_REFLECTIONS: call.set_value<Exception>(props.lReflections); break;
    case EAXREVERB_REFLECTIONSDELAY: call.set_value<Exception>(props.flReflectionsDelay); break;
    case EAXREVERB_REFLECTIONSPAN: call.set_value<Exception>(props.vReflectionsPan); break;
    case EAXREVERB_REVERB: call.set_value<Exception>(props.lReverb); break;
    case EAXREVERB_REVERBDELAY: call.set_value<Exception>(props.flReverbDelay); break;
    case EAXREVERB_REVERBPAN: call.set_value<Exception>(props.vReverbPan); break;
    case EAXREVERB_ECHOTIME: call.set_value<Exception>(props.flEchoTime); break;
    case EAXREVERB_ECHODEPTH: call.set_value<Exception>(props.flEchoDepth); break;
    case EAXREVERB_MODULATIONTIME: call.set_value<Exception>(props.flModulationTime); break;
    case EAXREVERB_MODULATIONDEPTH: call.set_value<Exception>(props.flModulationDepth); break;
    case EAXREVERB_AIRABSORPTIONHF: call.set_value<Exception>(props.flAirAbsorptionHF); break;
    case EAXREVERB_HFREFERENCE: call.set_value<Exception>(props.flHFReference); break;
    case EAXREVERB_LFREFERENCE: call.set_value<Exception>(props.flLFReference); break;
    case EAXREVERB_ROOMROLLOFFFACTOR: call.set_value<Exception>(props.flRoomRolloffFactor); break;
    case EAXREVERB_FLAGS: call.set_value<Exception>(props.ulFlags); break;
    default: fail_unknown_property_id();
    }
}

void EaxReverbCommitter::Get(const EaxCall &call, const EaxEffectProps &props)
{
    Get(call, props.mReverb);
}


void EaxReverbCommitter::Set(const EaxCall &call, EAX_REVERBPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case DSPROPERTY_EAX_ALL: defer<AllValidator1>(call, props); break;
    case DSPROPERTY_EAX_ENVIRONMENT: defer<EnvironmentValidator1>(call, props.environment); break;
    case DSPROPERTY_EAX_VOLUME: defer<VolumeValidator>(call, props.fVolume); break;
    case DSPROPERTY_EAX_DECAYTIME: defer<DecayTimeValidator>(call, props.fDecayTime_sec); break;
    case DSPROPERTY_EAX_DAMPING: defer<DampingValidator>(call, props.fDamping); break;
    default: fail_unknown_property_id();
    }
}

void EaxReverbCommitter::Set(const EaxCall &call, EAX20LISTENERPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case DSPROPERTY_EAX20LISTENER_NONE:
        break;

    case DSPROPERTY_EAX20LISTENER_ALLPARAMETERS:
        defer<AllValidator2>(call, props);
        break;

    case DSPROPERTY_EAX20LISTENER_ROOM:
        defer<RoomValidator>(call, props.lRoom);
        break;

    case DSPROPERTY_EAX20LISTENER_ROOMHF:
        defer<RoomHFValidator>(call, props.lRoomHF);
        break;

    case DSPROPERTY_EAX20LISTENER_ROOMROLLOFFFACTOR:
        defer<RoomRolloffFactorValidator>(call, props.flRoomRolloffFactor);
        break;

    case DSPROPERTY_EAX20LISTENER_DECAYTIME:
        defer<DecayTimeValidator>(call, props.flDecayTime);
        break;

    case DSPROPERTY_EAX20LISTENER_DECAYHFRATIO:
        defer<DecayHFRatioValidator>(call, props.flDecayHFRatio);
        break;

    case DSPROPERTY_EAX20LISTENER_REFLECTIONS:
        defer<ReflectionsValidator>(call, props.lReflections);
        break;

    case DSPROPERTY_EAX20LISTENER_REFLECTIONSDELAY:
        defer<ReflectionsDelayValidator>(call, props.flReverbDelay);
        break;

    case DSPROPERTY_EAX20LISTENER_REVERB:
        defer<ReverbValidator>(call, props.lReverb);
        break;

    case DSPROPERTY_EAX20LISTENER_REVERBDELAY:
        defer<ReverbDelayValidator>(call, props.flReverbDelay);
        break;

    case DSPROPERTY_EAX20LISTENER_ENVIRONMENT:
        defer<EnvironmentValidator1, EnvironmentDeferrer2>(call, props, props.dwEnvironment);
        break;

    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTSIZE:
        defer<EnvironmentSizeValidator, EnvironmentSizeDeferrer2>(call, props, props.flEnvironmentSize);
        break;

    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTDIFFUSION:
        defer<EnvironmentDiffusionValidator>(call, props.flEnvironmentDiffusion);
        break;

    case DSPROPERTY_EAX20LISTENER_AIRABSORPTIONHF:
        defer<AirAbsorptionHFValidator>(call, props.flAirAbsorptionHF);
        break;

    case DSPROPERTY_EAX20LISTENER_FLAGS:
        defer<FlagsValidator2>(call, props.dwFlags);
        break;

    default:
        fail_unknown_property_id();
    }
}

void EaxReverbCommitter::Set(const EaxCall &call, EAXREVERBPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case EAXREVERB_NONE:
        break;

    case EAXREVERB_ALLPARAMETERS:
        defer<AllValidator3>(call, props);
        break;

    case EAXREVERB_ENVIRONMENT:
        defer<EnvironmentValidator3, EnvironmentDeferrer3>(call, props, props.ulEnvironment);
        break;

    case EAXREVERB_ENVIRONMENTSIZE:
        defer<EnvironmentSizeValidator, EnvironmentSizeDeferrer3>(call, props, props.flEnvironmentSize);
        break;

    case EAXREVERB_ENVIRONMENTDIFFUSION:
        defer3<EnvironmentDiffusionValidator>(call, props, props.flEnvironmentDiffusion);
        break;

    case EAXREVERB_ROOM:
        defer3<RoomValidator>(call, props, props.lRoom);
        break;

    case EAXREVERB_ROOMHF:
        defer3<RoomHFValidator>(call, props, props.lRoomHF);
        break;

    case EAXREVERB_ROOMLF:
        defer3<RoomLFValidator>(call, props, props.lRoomLF);
        break;

    case EAXREVERB_DECAYTIME:
        defer3<DecayTimeValidator>(call, props, props.flDecayTime);
        break;

    case EAXREVERB_DECAYHFRATIO:
        defer3<DecayHFRatioValidator>(call, props, props.flDecayHFRatio);
        break;

    case EAXREVERB_DECAYLFRATIO:
        defer3<DecayLFRatioValidator>(call, props, props.flDecayLFRatio);
        break;

    case EAXREVERB_REFLECTIONS:
        defer3<ReflectionsValidator>(call, props, props.lReflections);
        break;

    case EAXREVERB_REFLECTIONSDELAY:
        defer3<ReflectionsDelayValidator>(call, props, props.flReflectionsDelay);
        break;

    case EAXREVERB_REFLECTIONSPAN:
        defer3<VectorValidator>(call, props, props.vReflectionsPan);
        break;

    case EAXREVERB_REVERB:
        defer3<ReverbValidator>(call, props, props.lReverb);
        break;

    case EAXREVERB_REVERBDELAY:
        defer3<ReverbDelayValidator>(call, props, props.flReverbDelay);
        break;

    case EAXREVERB_REVERBPAN:
        defer3<VectorValidator>(call, props, props.vReverbPan);
        break;

    case EAXREVERB_ECHOTIME:
        defer3<EchoTimeValidator>(call, props, props.flEchoTime);
        break;

    case EAXREVERB_ECHODEPTH:
        defer3<EchoDepthValidator>(call, props, props.flEchoDepth);
        break;

    case EAXREVERB_MODULATIONTIME:
        defer3<ModulationTimeValidator>(call, props, props.flModulationTime);
        break;

    case EAXREVERB_MODULATIONDEPTH:
        defer3<ModulationDepthValidator>(call, props, props.flModulationDepth);
        break;

    case EAXREVERB_AIRABSORPTIONHF:
        defer3<AirAbsorptionHFValidator>(call, props, props.flAirAbsorptionHF);
        break;

    case EAXREVERB_HFREFERENCE:
        defer3<HFReferenceValidator>(call, props, props.flHFReference);
        break;

    case EAXREVERB_LFREFERENCE:
        defer3<LFReferenceValidator>(call, props, props.flLFReference);
        break;

    case EAXREVERB_ROOMROLLOFFFACTOR:
        defer3<RoomRolloffFactorValidator>(call, props, props.flRoomRolloffFactor);
        break;

    case EAXREVERB_FLAGS:
        defer3<FlagsValidator3>(call, props, props.ulFlags);
        break;

    default:
        fail_unknown_property_id();
    }
}

void EaxReverbCommitter::Set(const EaxCall &call, EaxEffectProps &props)
{
    Set(call, props.mReverb);
}

#endif // ALSOFT_EAX
