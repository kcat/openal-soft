
#include "config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <variant>

#include "AL/al.h"
#include "AL/efx.h"

#include "alc/context.h"
#include "alnumeric.h"
#include "core/logging.h"
#include "effects.h"
#include "gsl/gsl"

#if ALSOFT_EAX
#include "al/eax/api.h"
#include "al/eax/call.h"
#include "al/eax/effect.h"
#include "al/eax/exception.h"
#include "al/eax/utils.h"
#endif // ALSOFT_EAX


namespace {

consteval auto genDefaultProps() noexcept -> EffectProps
{
    return ReverbProps{
        .Density   = AL_EAXREVERB_DEFAULT_DENSITY,
        .Diffusion = AL_EAXREVERB_DEFAULT_DIFFUSION,
        .Gain   = AL_EAXREVERB_DEFAULT_GAIN,
        .GainHF = AL_EAXREVERB_DEFAULT_GAINHF,
        .GainLF = AL_EAXREVERB_DEFAULT_GAINLF,
        .DecayTime    = AL_EAXREVERB_DEFAULT_DECAY_TIME,
        .DecayHFRatio = AL_EAXREVERB_DEFAULT_DECAY_HFRATIO,
        .DecayLFRatio = AL_EAXREVERB_DEFAULT_DECAY_LFRATIO,
        .ReflectionsGain   = AL_EAXREVERB_DEFAULT_REFLECTIONS_GAIN,
        .ReflectionsDelay  = AL_EAXREVERB_DEFAULT_REFLECTIONS_DELAY,
        .ReflectionsPan    = {AL_EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ,
            AL_EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ, AL_EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ},
        .LateReverbGain   = AL_EAXREVERB_DEFAULT_LATE_REVERB_GAIN,
        .LateReverbDelay  = AL_EAXREVERB_DEFAULT_LATE_REVERB_DELAY,
        .LateReverbPan    = {AL_EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ,
            AL_EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ, AL_EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ},
        .EchoTime  = AL_EAXREVERB_DEFAULT_ECHO_TIME,
        .EchoDepth = AL_EAXREVERB_DEFAULT_ECHO_DEPTH,
        .ModulationTime  = AL_EAXREVERB_DEFAULT_MODULATION_TIME,
        .ModulationDepth = AL_EAXREVERB_DEFAULT_MODULATION_DEPTH,
        .AirAbsorptionGainHF = AL_EAXREVERB_DEFAULT_AIR_ABSORPTION_GAINHF,
        .HFReference = AL_EAXREVERB_DEFAULT_HFREFERENCE,
        .LFReference = AL_EAXREVERB_DEFAULT_LFREFERENCE,
        .RoomRolloffFactor = AL_EAXREVERB_DEFAULT_ROOM_ROLLOFF_FACTOR,
        .DecayHFLimit = AL_EAXREVERB_DEFAULT_DECAY_HFLIMIT};
}

consteval auto genDefaultStdProps() noexcept -> EffectProps
{
    return ReverbProps{
        .Density   = AL_REVERB_DEFAULT_DENSITY,
        .Diffusion = AL_REVERB_DEFAULT_DIFFUSION,
        .Gain   = AL_REVERB_DEFAULT_GAIN,
        .GainHF = AL_REVERB_DEFAULT_GAINHF,
        .GainLF = 1.0f,
        .DecayTime    = AL_REVERB_DEFAULT_DECAY_TIME,
        .DecayHFRatio = AL_REVERB_DEFAULT_DECAY_HFRATIO,
        .DecayLFRatio = 1.0f,
        .ReflectionsGain   = AL_REVERB_DEFAULT_REFLECTIONS_GAIN,
        .ReflectionsDelay  = AL_REVERB_DEFAULT_REFLECTIONS_DELAY,
        .ReflectionsPan    = {0.0f, 0.0f, 0.0f},
        .LateReverbGain   = AL_REVERB_DEFAULT_LATE_REVERB_GAIN,
        .LateReverbDelay  = AL_REVERB_DEFAULT_LATE_REVERB_DELAY,
        .LateReverbPan    = {0.0f, 0.0f, 0.0f},
        .EchoTime  = 0.25f,
        .EchoDepth = 0.0f,
        .ModulationTime  = 0.25f,
        .ModulationDepth = 0.0f,
        .AirAbsorptionGainHF = AL_REVERB_DEFAULT_AIR_ABSORPTION_GAINHF,
        .HFReference = 5'000.0f,
        .LFReference = 250.0f,
        .RoomRolloffFactor = AL_REVERB_DEFAULT_ROOM_ROLLOFF_FACTOR,
        .DecayHFLimit = AL_REVERB_DEFAULT_DECAY_HFLIMIT};
}

} // namespace

constinit const EffectProps ReverbEffectProps(genDefaultProps());

void ReverbEffectHandler::SetParami(al::Context *context, ReverbProps &props, ALenum param, int val)
{
    switch(param)
    {
    case AL_EAXREVERB_DECAY_HFLIMIT:
        if(!(val >= AL_EAXREVERB_MIN_DECAY_HFLIMIT && val <= AL_EAXREVERB_MAX_DECAY_HFLIMIT))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb decay hflimit out of range");
        props.DecayHFLimit = val != AL_FALSE;
        return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid EAX reverb integer property {:#04x}",
        as_unsigned(param));
}
void ReverbEffectHandler::SetParamiv(al::Context *context, ReverbProps &props, ALenum param, const int *vals)
{ SetParami(context, props, param, *vals); }
void ReverbEffectHandler::SetParamf(al::Context *context, ReverbProps &props, ALenum param, float val)
{
    switch(param)
    {
    case AL_EAXREVERB_DENSITY:
        if(!(val >= AL_EAXREVERB_MIN_DENSITY && val <= AL_EAXREVERB_MAX_DENSITY))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb density out of range");
        props.Density = val;
        return;

    case AL_EAXREVERB_DIFFUSION:
        if(!(val >= AL_EAXREVERB_MIN_DIFFUSION && val <= AL_EAXREVERB_MAX_DIFFUSION))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb diffusion out of range");
        props.Diffusion = val;
        return;

    case AL_EAXREVERB_GAIN:
        if(!(val >= AL_EAXREVERB_MIN_GAIN && val <= AL_EAXREVERB_MAX_GAIN))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb gain out of range");
        props.Gain = val;
        return;

    case AL_EAXREVERB_GAINHF:
        if(!(val >= AL_EAXREVERB_MIN_GAINHF && val <= AL_EAXREVERB_MAX_GAINHF))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb gainhf out of range");
        props.GainHF = val;
        return;

    case AL_EAXREVERB_GAINLF:
        if(!(val >= AL_EAXREVERB_MIN_GAINLF && val <= AL_EAXREVERB_MAX_GAINLF))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb gainlf out of range");
        props.GainLF = val;
        return;

    case AL_EAXREVERB_DECAY_TIME:
        if(!(val >= AL_EAXREVERB_MIN_DECAY_TIME && val <= AL_EAXREVERB_MAX_DECAY_TIME))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb decay time out of range");
        props.DecayTime = val;
        return;

    case AL_EAXREVERB_DECAY_HFRATIO:
        if(!(val >= AL_EAXREVERB_MIN_DECAY_HFRATIO && val <= AL_EAXREVERB_MAX_DECAY_HFRATIO))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb decay hfratio out of range");
        props.DecayHFRatio = val;
        return;

    case AL_EAXREVERB_DECAY_LFRATIO:
        if(!(val >= AL_EAXREVERB_MIN_DECAY_LFRATIO && val <= AL_EAXREVERB_MAX_DECAY_LFRATIO))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb decay lfratio out of range");
        props.DecayLFRatio = val;
        return;

    case AL_EAXREVERB_REFLECTIONS_GAIN:
        if(!(val >= AL_EAXREVERB_MIN_REFLECTIONS_GAIN && val <= AL_EAXREVERB_MAX_REFLECTIONS_GAIN))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb reflections gain out of range");
        props.ReflectionsGain = val;
        return;

    case AL_EAXREVERB_REFLECTIONS_DELAY:
        if(!(val >= AL_EAXREVERB_MIN_REFLECTIONS_DELAY && val <= AL_EAXREVERB_MAX_REFLECTIONS_DELAY))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb reflections delay out of range");
        props.ReflectionsDelay = val;
        return;

    case AL_EAXREVERB_LATE_REVERB_GAIN:
        if(!(val >= AL_EAXREVERB_MIN_LATE_REVERB_GAIN && val <= AL_EAXREVERB_MAX_LATE_REVERB_GAIN))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb late reverb gain out of range");
        props.LateReverbGain = val;
        return;

    case AL_EAXREVERB_LATE_REVERB_DELAY:
        if(!(val >= AL_EAXREVERB_MIN_LATE_REVERB_DELAY && val <= AL_EAXREVERB_MAX_LATE_REVERB_DELAY))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb late reverb delay out of range");
        props.LateReverbDelay = val;
        return;

    case AL_EAXREVERB_ECHO_TIME:
        if(!(val >= AL_EAXREVERB_MIN_ECHO_TIME && val <= AL_EAXREVERB_MAX_ECHO_TIME))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb echo time out of range");
        props.EchoTime = val;
        return;

    case AL_EAXREVERB_ECHO_DEPTH:
        if(!(val >= AL_EAXREVERB_MIN_ECHO_DEPTH && val <= AL_EAXREVERB_MAX_ECHO_DEPTH))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb echo depth out of range");
        props.EchoDepth = val;
        return;

    case AL_EAXREVERB_MODULATION_TIME:
        if(!(val >= AL_EAXREVERB_MIN_MODULATION_TIME && val <= AL_EAXREVERB_MAX_MODULATION_TIME))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb modulation time out of range");
        props.ModulationTime = val;
        return;

    case AL_EAXREVERB_MODULATION_DEPTH:
        if(!(val >= AL_EAXREVERB_MIN_MODULATION_DEPTH && val <= AL_EAXREVERB_MAX_MODULATION_DEPTH))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb modulation depth out of range");
        props.ModulationDepth = val;
        return;

    case AL_EAXREVERB_AIR_ABSORPTION_GAINHF:
        if(!(val >= AL_EAXREVERB_MIN_AIR_ABSORPTION_GAINHF && val <= AL_EAXREVERB_MAX_AIR_ABSORPTION_GAINHF))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb air absorption gainhf out of range");
        props.AirAbsorptionGainHF = val;
        return;

    case AL_EAXREVERB_HFREFERENCE:
        if(!(val >= AL_EAXREVERB_MIN_HFREFERENCE && val <= AL_EAXREVERB_MAX_HFREFERENCE))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb hfreference out of range");
        props.HFReference = val;
        return;

    case AL_EAXREVERB_LFREFERENCE:
        if(!(val >= AL_EAXREVERB_MIN_LFREFERENCE && val <= AL_EAXREVERB_MAX_LFREFERENCE))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb lfreference out of range");
        props.LFReference = val;
        return;

    case AL_EAXREVERB_ROOM_ROLLOFF_FACTOR:
        if(!(val >= AL_EAXREVERB_MIN_ROOM_ROLLOFF_FACTOR && val <= AL_EAXREVERB_MAX_ROOM_ROLLOFF_FACTOR))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb room rolloff factor out of range");
        props.RoomRolloffFactor = val;
        return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid EAX reverb float property {:#04x}",
        as_unsigned(param));
}
void ReverbEffectHandler::SetParamfv(al::Context *context, ReverbProps &props, ALenum param, const float *vals)
{
    static constexpr auto is_finite = [](const float f) -> bool { return std::isfinite(f); };
    auto values = std::span<const float>{};
    switch(param)
    {
    case AL_EAXREVERB_REFLECTIONS_PAN:
        values = {vals, 3_uz};
        if(!std::ranges::all_of(values, is_finite))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb reflections pan out of range");
        std::ranges::copy(values, props.ReflectionsPan.begin());
        return;
    case AL_EAXREVERB_LATE_REVERB_PAN:
        values = {vals, 3_uz};
        if(!std::ranges::all_of(values, is_finite))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb late reverb pan out of range");
        std::ranges::copy(values, props.LateReverbPan.begin());
        return;
    }
    SetParamf(context, props, param, *vals);
}

void ReverbEffectHandler::GetParami(al::Context *context, const ReverbProps &props, ALenum param, int *val)
{
    switch(param)
    {
    case AL_EAXREVERB_DECAY_HFLIMIT: *val = props.DecayHFLimit; return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid EAX reverb integer property {:#04x}",
        as_unsigned(param));
}
void ReverbEffectHandler::GetParamiv(al::Context *context, const ReverbProps &props, ALenum param, int *vals)
{ GetParami(context, props, param, vals); }
void ReverbEffectHandler::GetParamf(al::Context *context, const ReverbProps &props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_EAXREVERB_DENSITY: *val = props.Density; return;
    case AL_EAXREVERB_DIFFUSION: *val = props.Diffusion; return;
    case AL_EAXREVERB_GAIN: *val = props.Gain; return;
    case AL_EAXREVERB_GAINHF: *val = props.GainHF; return;
    case AL_EAXREVERB_GAINLF: *val = props.GainLF; return;
    case AL_EAXREVERB_DECAY_TIME: *val = props.DecayTime; return;
    case AL_EAXREVERB_DECAY_HFRATIO: *val = props.DecayHFRatio; return;
    case AL_EAXREVERB_DECAY_LFRATIO: *val = props.DecayLFRatio; return;
    case AL_EAXREVERB_REFLECTIONS_GAIN: *val = props.ReflectionsGain; return;
    case AL_EAXREVERB_REFLECTIONS_DELAY: *val = props.ReflectionsDelay; return;
    case AL_EAXREVERB_LATE_REVERB_GAIN: *val = props.LateReverbGain; return;
    case AL_EAXREVERB_LATE_REVERB_DELAY: *val = props.LateReverbDelay; return;
    case AL_EAXREVERB_ECHO_TIME: *val = props.EchoTime; return;
    case AL_EAXREVERB_ECHO_DEPTH: *val = props.EchoDepth; return;
    case AL_EAXREVERB_MODULATION_TIME: *val = props.ModulationTime; return;
    case AL_EAXREVERB_MODULATION_DEPTH: *val = props.ModulationDepth; return;
    case AL_EAXREVERB_AIR_ABSORPTION_GAINHF: *val = props.AirAbsorptionGainHF; return;
    case AL_EAXREVERB_HFREFERENCE: *val = props.HFReference; return;
    case AL_EAXREVERB_LFREFERENCE: *val = props.LFReference; return;
    case AL_EAXREVERB_ROOM_ROLLOFF_FACTOR: *val = props.RoomRolloffFactor; return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid EAX reverb float property {:#04x}",
        as_unsigned(param));
}
void ReverbEffectHandler::GetParamfv(al::Context *context, const ReverbProps &props, ALenum param, float *vals)
{
    auto values = std::span<float>{};
    switch(param)
    {
    case AL_EAXREVERB_REFLECTIONS_PAN:
        values = {vals, 3_uz};
        std::ranges::copy(props.ReflectionsPan, values.begin());
        return;
    case AL_EAXREVERB_LATE_REVERB_PAN:
        values = {vals, 3_uz};
        std::ranges::copy(props.LateReverbPan, values.begin());
        return;
    }

    GetParamf(context, props, param, vals);
}


constinit const EffectProps StdReverbEffectProps(genDefaultStdProps());

void StdReverbEffectHandler::SetParami(al::Context *context, ReverbProps &props, ALenum param, int val)
{
    switch(param)
    {
    case AL_REVERB_DECAY_HFLIMIT:
        if(!(val >= AL_REVERB_MIN_DECAY_HFLIMIT && val <= AL_REVERB_MAX_DECAY_HFLIMIT))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb decay hflimit out of range");
        props.DecayHFLimit = val != AL_FALSE;
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid EAX reverb integer property {:#04x}",
        as_unsigned(param));
}
void StdReverbEffectHandler::SetParamiv(al::Context *context, ReverbProps &props, ALenum param, const int *vals)
{ SetParami(context, props, param, *vals); }
void StdReverbEffectHandler::SetParamf(al::Context *context, ReverbProps &props, ALenum param, float val)
{
    switch(param)
    {
    case AL_REVERB_DENSITY:
        if(!(val >= AL_REVERB_MIN_DENSITY && val <= AL_REVERB_MAX_DENSITY))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb density out of range");
        props.Density = val;
        return;

    case AL_REVERB_DIFFUSION:
        if(!(val >= AL_REVERB_MIN_DIFFUSION && val <= AL_REVERB_MAX_DIFFUSION))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb diffusion out of range");
        props.Diffusion = val;
        return;

    case AL_REVERB_GAIN:
        if(!(val >= AL_REVERB_MIN_GAIN && val <= AL_REVERB_MAX_GAIN))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb gain out of range");
        props.Gain = val;
        return;

    case AL_REVERB_GAINHF:
        if(!(val >= AL_REVERB_MIN_GAINHF && val <= AL_REVERB_MAX_GAINHF))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb gainhf out of range");
        props.GainHF = val;
        return;

    case AL_REVERB_DECAY_TIME:
        if(!(val >= AL_REVERB_MIN_DECAY_TIME && val <= AL_REVERB_MAX_DECAY_TIME))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb decay time out of range");
        props.DecayTime = val;
        return;

    case AL_REVERB_DECAY_HFRATIO:
        if(!(val >= AL_REVERB_MIN_DECAY_HFRATIO && val <= AL_REVERB_MAX_DECAY_HFRATIO))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb decay hfratio out of range");
        props.DecayHFRatio = val;
        return;

    case AL_REVERB_REFLECTIONS_GAIN:
        if(!(val >= AL_REVERB_MIN_REFLECTIONS_GAIN && val <= AL_REVERB_MAX_REFLECTIONS_GAIN))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb reflections gain out of range");
        props.ReflectionsGain = val;
        return;

    case AL_REVERB_REFLECTIONS_DELAY:
        if(!(val >= AL_REVERB_MIN_REFLECTIONS_DELAY && val <= AL_REVERB_MAX_REFLECTIONS_DELAY))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb reflections delay out of range");
        props.ReflectionsDelay = val;
        return;

    case AL_REVERB_LATE_REVERB_GAIN:
        if(!(val >= AL_REVERB_MIN_LATE_REVERB_GAIN && val <= AL_REVERB_MAX_LATE_REVERB_GAIN))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb late reverb gain out of range");
        props.LateReverbGain = val;
        return;

    case AL_REVERB_LATE_REVERB_DELAY:
        if(!(val >= AL_REVERB_MIN_LATE_REVERB_DELAY && val <= AL_REVERB_MAX_LATE_REVERB_DELAY))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb late reverb delay out of range");
        props.LateReverbDelay = val;
        return;

    case AL_REVERB_AIR_ABSORPTION_GAINHF:
        if(!(val >= AL_REVERB_MIN_AIR_ABSORPTION_GAINHF && val <= AL_REVERB_MAX_AIR_ABSORPTION_GAINHF))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb air absorption gainhf out of range");
        props.AirAbsorptionGainHF = val;
        return;

    case AL_REVERB_ROOM_ROLLOFF_FACTOR:
        if(!(val >= AL_REVERB_MIN_ROOM_ROLLOFF_FACTOR && val <= AL_REVERB_MAX_ROOM_ROLLOFF_FACTOR))
            context->throw_error(AL_INVALID_VALUE, "EAX Reverb room rolloff factor out of range");
        props.RoomRolloffFactor = val;
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid EAX reverb float property {:#04x}",
        as_unsigned(param));
}
void StdReverbEffectHandler::SetParamfv(al::Context *context, ReverbProps &props, ALenum param, const float *vals)
{ SetParamf(context, props, param, *vals); }

void StdReverbEffectHandler::GetParami(al::Context *context, const ReverbProps &props, ALenum param, int *val)
{
    switch(param)
    {
    case AL_REVERB_DECAY_HFLIMIT: *val = props.DecayHFLimit; return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid EAX reverb integer property {:#04x}",
        as_unsigned(param));
}
void StdReverbEffectHandler::GetParamiv(al::Context *context, const ReverbProps &props, ALenum param, int *vals)
{ GetParami(context, props, param, vals); }
void StdReverbEffectHandler::GetParamf(al::Context *context, const ReverbProps &props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_REVERB_DENSITY: *val = props.Density; return;
    case AL_REVERB_DIFFUSION: *val = props.Diffusion; return;
    case AL_REVERB_GAIN: *val = props.Gain; return;
    case AL_REVERB_GAINHF: *val = props.GainHF; return;
    case AL_REVERB_DECAY_TIME: *val = props.DecayTime; return;
    case AL_REVERB_DECAY_HFRATIO: *val = props.DecayHFRatio; return;
    case AL_REVERB_REFLECTIONS_GAIN: *val = props.ReflectionsGain; return;
    case AL_REVERB_REFLECTIONS_DELAY: *val = props.ReflectionsDelay; return;
    case AL_REVERB_LATE_REVERB_GAIN: *val = props.LateReverbGain; return;
    case AL_REVERB_LATE_REVERB_DELAY: *val = props.LateReverbDelay; return;
    case AL_REVERB_AIR_ABSORPTION_GAINHF: *val = props.AirAbsorptionGainHF; return;
    case AL_REVERB_ROOM_ROLLOFF_FACTOR: *val = props.RoomRolloffFactor; return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid EAX reverb float property {:#04x}",
        as_unsigned(param));
}
void StdReverbEffectHandler::GetParamfv(al::Context *context, const ReverbProps &props, ALenum param, float *vals)
{ GetParamf(context, props, param, vals); }


#if ALSOFT_EAX
namespace {

/* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
class EaxReverbEffectException : public EaxException {
public:
    explicit EaxReverbEffectException(const std::string_view message)
        : EaxException{"EAX_REVERB_EFFECT", message}
    { }
};

struct EnvironmentValidator1 {
    void operator()(eax_ulong const ulEnvironment) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Environment",
            ulEnvironment,
            EAXREVERB_MINENVIRONMENT,
            EAX1REVERB_MAXENVIRONMENT);
    }
}; // EnvironmentValidator1

struct VolumeValidator {
    void operator()(float const volume) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Volume",
            volume,
            EAX1REVERB_MINVOLUME,
            EAX1REVERB_MAXVOLUME);
    }
}; // VolumeValidator

struct DecayTimeValidator {
    void operator()(float const flDecayTime) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Decay Time",
            flDecayTime,
            EAXREVERB_MINDECAYTIME,
            EAXREVERB_MAXDECAYTIME);
    }
}; // DecayTimeValidator

struct DampingValidator {
    void operator()(float const damping) const
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
    void operator()(eax_long const lRoom) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Room",
            lRoom,
            EAXREVERB_MINROOM,
            EAXREVERB_MAXROOM);
    }
}; // RoomValidator

struct RoomHFValidator {
    void operator()(eax_long const lRoomHF) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Room HF",
            lRoomHF,
            EAXREVERB_MINROOMHF,
            EAXREVERB_MAXROOMHF);
    }
}; // RoomHFValidator

struct RoomRolloffFactorValidator {
    void operator()(float const flRoomRolloffFactor) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Room Rolloff Factor",
            flRoomRolloffFactor,
            EAXREVERB_MINROOMROLLOFFFACTOR,
            EAXREVERB_MAXROOMROLLOFFFACTOR);
    }
}; // RoomRolloffFactorValidator

struct DecayHFRatioValidator {
    void operator()(float const flDecayHFRatio) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Decay HF Ratio",
            flDecayHFRatio,
            EAXREVERB_MINDECAYHFRATIO,
            EAXREVERB_MAXDECAYHFRATIO);
    }
}; // DecayHFRatioValidator

struct ReflectionsValidator {
    void operator()(eax_long const lReflections) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Reflections",
            lReflections,
            EAXREVERB_MINREFLECTIONS,
            EAXREVERB_MAXREFLECTIONS);
    }
}; // ReflectionsValidator

struct ReflectionsDelayValidator {
    void operator()(float const flReflectionsDelay) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Reflections Delay",
            flReflectionsDelay,
            EAXREVERB_MINREFLECTIONSDELAY,
            EAXREVERB_MAXREFLECTIONSDELAY);
    }
}; // ReflectionsDelayValidator

struct ReverbValidator {
    void operator()(eax_long const lReverb) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Reverb",
            lReverb,
            EAXREVERB_MINREVERB,
            EAXREVERB_MAXREVERB);
    }
}; // ReverbValidator

struct ReverbDelayValidator {
    void operator()(float const flReverbDelay) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Reverb Delay",
            flReverbDelay,
            EAXREVERB_MINREVERBDELAY,
            EAXREVERB_MAXREVERBDELAY);
    }
}; // ReverbDelayValidator

struct EnvironmentSizeValidator {
    void operator()(float const flEnvironmentSize) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Environment Size",
            flEnvironmentSize,
            EAXREVERB_MINENVIRONMENTSIZE,
            EAXREVERB_MAXENVIRONMENTSIZE);
    }
}; // EnvironmentSizeValidator

struct EnvironmentDiffusionValidator {
    void operator()(float const flEnvironmentDiffusion) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Environment Diffusion",
            flEnvironmentDiffusion,
            EAXREVERB_MINENVIRONMENTDIFFUSION,
            EAXREVERB_MAXENVIRONMENTDIFFUSION);
    }
}; // EnvironmentDiffusionValidator

struct AirAbsorptionHFValidator {
    void operator()(float const flAirAbsorptionHF) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Air Absorbtion HF",
            flAirAbsorptionHF,
            EAXREVERB_MINAIRABSORPTIONHF,
            EAXREVERB_MAXAIRABSORPTIONHF);
    }
}; // AirAbsorptionHFValidator

struct FlagsValidator2 {
    void operator()(eax_ulong const ulFlags) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Flags",
            ulFlags,
            0_eax_ulong,
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
    void operator()(eax_ulong const ulEnvironment) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Environment",
            ulEnvironment,
            EAXREVERB_MINENVIRONMENT,
            EAX30REVERB_MAXENVIRONMENT);
    }
}; // EnvironmentValidator1

struct RoomLFValidator {
    void operator()(eax_long const lRoomLF) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Room LF",
            lRoomLF,
            EAXREVERB_MINROOMLF,
            EAXREVERB_MAXROOMLF);
    }
}; // RoomLFValidator

struct DecayLFRatioValidator {
    void operator()(float const flDecayLFRatio) const
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
    void operator()(float const flEchoTime) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Echo Time",
            flEchoTime,
            EAXREVERB_MINECHOTIME,
            EAXREVERB_MAXECHOTIME);
    }
}; // EchoTimeValidator

struct EchoDepthValidator {
    void operator()(float const flEchoDepth) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Echo Depth",
            flEchoDepth,
            EAXREVERB_MINECHODEPTH,
            EAXREVERB_MAXECHODEPTH);
    }
}; // EchoDepthValidator

struct ModulationTimeValidator {
    void operator()(float const flModulationTime) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Modulation Time",
            flModulationTime,
            EAXREVERB_MINMODULATIONTIME,
            EAXREVERB_MAXMODULATIONTIME);
    }
}; // ModulationTimeValidator

struct ModulationDepthValidator {
    void operator()(float const flModulationDepth) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Modulation Depth",
            flModulationDepth,
            EAXREVERB_MINMODULATIONDEPTH,
            EAXREVERB_MAXMODULATIONDEPTH);
    }
}; // ModulationDepthValidator

struct HFReferenceValidator {
    void operator()(float const flHFReference) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "HF Reference",
            flHFReference,
            EAXREVERB_MINHFREFERENCE,
            EAXREVERB_MAXHFREFERENCE);
    }
}; // HFReferenceValidator

struct LFReferenceValidator {
    void operator()(float const flLFReference) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "LF Reference",
            flLFReference,
            EAXREVERB_MINLFREFERENCE,
            EAXREVERB_MAXLFREFERENCE);
    }
}; // LFReferenceValidator

struct FlagsValidator3 {
    void operator()(eax_ulong const ulFlags) const
    {
        eax_validate_range<EaxReverbEffectException>(
            "Flags",
            ulFlags,
            0_eax_ulong,
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
    void operator()(EAX20LISTENERPROPERTIES& props, eax_ulong const dwEnvironment) const
    {
        props = EAX2REVERB_PRESETS[dwEnvironment];
    }
}; // EnvironmentDeferrer2

struct EnvironmentSizeDeferrer2 {
    void operator()(EAX20LISTENERPROPERTIES& props, float const flEnvironmentSize) const
    {
        if (props.flEnvironmentSize == flEnvironmentSize)
        {
            return;
        }

        const auto scale = flEnvironmentSize / props.flEnvironmentSize;
        props.flEnvironmentSize = flEnvironmentSize;

        if ((props.dwFlags & EAX2LISTENERFLAGS_DECAYTIMESCALE) != 0)
        {
            props.flDecayTime = std::clamp(
                props.flDecayTime * scale,
                EAXREVERB_MINDECAYTIME,
                EAXREVERB_MAXDECAYTIME);
        }

        if ((props.dwFlags & EAX2LISTENERFLAGS_REFLECTIONSSCALE) != 0 &&
            (props.dwFlags & EAX2LISTENERFLAGS_REFLECTIONSDELAYSCALE) != 0)
        {
            props.lReflections = std::clamp(
                props.lReflections - gsl::narrow_cast<eax_long>(gain_to_level_mb(scale)),
                EAXREVERB_MINREFLECTIONS,
                EAXREVERB_MAXREFLECTIONS);
        }

        if ((props.dwFlags & EAX2LISTENERFLAGS_REFLECTIONSDELAYSCALE) != 0)
        {
            props.flReflectionsDelay = std::clamp(
                props.flReflectionsDelay * scale,
                EAXREVERB_MINREFLECTIONSDELAY,
                EAXREVERB_MAXREFLECTIONSDELAY);
        }

        if ((props.dwFlags & EAX2LISTENERFLAGS_REVERBSCALE) != 0)
        {
            const auto log_scalar = ((props.dwFlags & EAXREVERBFLAGS_DECAYTIMESCALE) != 0) ? 2'000.0F : 3'000.0F;

            props.lReverb = std::clamp(
                props.lReverb - gsl::narrow_cast<eax_long>(std::log10(scale) * log_scalar),
                EAXREVERB_MINREVERB,
                EAXREVERB_MAXREVERB);
        }

        if ((props.dwFlags & EAX2LISTENERFLAGS_REVERBDELAYSCALE) != 0)
        {
            props.flReverbDelay = std::clamp(
                props.flReverbDelay * scale,
                EAXREVERB_MINREVERBDELAY,
                EAXREVERB_MAXREVERBDELAY);
        }
    }
}; // EnvironmentSizeDeferrer2

struct EnvironmentDeferrer3 {
    void operator()(EAXREVERBPROPERTIES& props, eax_ulong const ulEnvironment) const
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
    void operator()(EAXREVERBPROPERTIES& props, float const flEnvironmentSize) const
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
            props.flDecayTime = std::clamp(
                props.flDecayTime * scale,
                EAXREVERB_MINDECAYTIME,
                EAXREVERB_MAXDECAYTIME);
        }

        if ((props.ulFlags & EAXREVERBFLAGS_REFLECTIONSSCALE) != 0 &&
            (props.ulFlags & EAXREVERBFLAGS_REFLECTIONSDELAYSCALE) != 0)
        {
            props.lReflections = std::clamp(
                props.lReflections - gsl::narrow_cast<eax_long>(gain_to_level_mb(scale)),
                EAXREVERB_MINREFLECTIONS,
                EAXREVERB_MAXREFLECTIONS);
        }

        if ((props.ulFlags & EAXREVERBFLAGS_REFLECTIONSDELAYSCALE) != 0)
        {
            props.flReflectionsDelay = std::clamp(
                props.flReflectionsDelay * scale,
                EAXREVERB_MINREFLECTIONSDELAY,
                EAXREVERB_MAXREFLECTIONSDELAY);
        }

        if ((props.ulFlags & EAXREVERBFLAGS_REVERBSCALE) != 0)
        {
            const auto log_scalar = ((props.ulFlags & EAXREVERBFLAGS_DECAYTIMESCALE) != 0) ? 2'000.0F : 3'000.0F;
            props.lReverb = std::clamp(
                props.lReverb - gsl::narrow_cast<eax_long>(std::log10(scale) * log_scalar),
                EAXREVERB_MINREVERB,
                EAXREVERB_MAXREVERB);
        }

        if ((props.ulFlags & EAXREVERBFLAGS_REVERBDELAYSCALE) != 0)
        {
            props.flReverbDelay = std::clamp(
                props.flReverbDelay * scale,
                EAXREVERB_MINREVERBDELAY,
                EAXREVERB_MAXREVERBDELAY);
        }

        if ((props.ulFlags & EAXREVERBFLAGS_ECHOTIMESCALE) != 0)
        {
            props.flEchoTime = std::clamp(
                props.flEchoTime * scale,
                EAXREVERB_MINECHOTIME,
                EAXREVERB_MAXECHOTIME);
        }

        if ((props.ulFlags & EAXREVERBFLAGS_MODULATIONTIMESCALE) != 0)
        {
            props.flModulationTime = std::clamp(
                props.flModulationTime * scale,
                EAXREVERB_MINMODULATIONTIME,
                EAXREVERB_MAXMODULATIONTIME);
        }
    }
}; // EnvironmentSizeDeferrer3

} // namespace


/* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
struct EaxReverbCommitter::Exception final : EaxReverbEffectException {
    using EaxReverbEffectException::EaxReverbEffectException;
};

[[noreturn]]
void EaxReverbCommitter::fail(const std::string_view message)
{ throw Exception{message}; }

void EaxReverbCommitter::translate(const EAX_REVERBPROPERTIES& src, EAXREVERBPROPERTIES& dst) noexcept
{
    Expects(src.environment <= EAX1REVERB_MAXENVIRONMENT);
    dst = EAXREVERB_PRESETS[src.environment];
    dst.flDecayTime = src.fDecayTime_sec;
    dst.flDecayHFRatio = src.fDamping;
    dst.lReverb = gsl::narrow_cast<int>(std::min(gain_to_level_mb(src.fVolume), 0.0f));
}

void EaxReverbCommitter::translate(const EAX20LISTENERPROPERTIES& src, EAXREVERBPROPERTIES& dst) noexcept
{
    Expects(src.dwEnvironment <= EAX1REVERB_MAXENVIRONMENT);
    dst = EAXREVERB_PRESETS[src.dwEnvironment];
    dst.ulEnvironment = src.dwEnvironment;
    dst.flEnvironmentSize = src.flEnvironmentSize;
    dst.flEnvironmentDiffusion = src.flEnvironmentDiffusion;
    dst.lRoom = src.lRoom;
    dst.lRoomHF = src.lRoomHF;
    dst.flDecayTime = src.flDecayTime;
    dst.flDecayHFRatio = src.flDecayHFRatio;
    dst.lReflections = src.lReflections;
    dst.flReflectionsDelay = src.flReflectionsDelay;
    dst.lReverb = src.lReverb;
    dst.flReverbDelay = src.flReverbDelay;
    dst.flAirAbsorptionHF = src.flAirAbsorptionHF;
    dst.flRoomRolloffFactor = src.flRoomRolloffFactor;
    dst.ulFlags = src.dwFlags;
}

auto EaxReverbCommitter::commit(const EAX_REVERBPROPERTIES &props) const -> bool
{
    EAXREVERBPROPERTIES dst{};
    translate(props, dst);
    return commit(dst);
}

auto EaxReverbCommitter::commit(const EAX20LISTENERPROPERTIES &props) const -> bool
{
    EAXREVERBPROPERTIES dst{};
    translate(props, dst);
    return commit(dst);
}

auto EaxReverbCommitter::commit(const EAXREVERBPROPERTIES &props) const -> bool
{
    if(auto *cur = std::get_if<EAXREVERBPROPERTIES>(&mEaxProps); cur && *cur == props)
        return false;

    mEaxProps = props;

    const auto size = props.flEnvironmentSize;
    const auto density = (size * size * size) / 16.0f;
    mAlProps = ReverbProps{
        .Density = std::min(density, AL_EAXREVERB_MAX_DENSITY),
        .Diffusion = props.flEnvironmentDiffusion,
        .Gain = level_mb_to_gain(gsl::narrow_cast<float>(props.lRoom)),
        .GainHF = level_mb_to_gain(gsl::narrow_cast<float>(props.lRoomHF)),
        .GainLF = level_mb_to_gain(gsl::narrow_cast<float>(props.lRoomLF)),
        .DecayTime = props.flDecayTime,
        .DecayHFRatio = props.flDecayHFRatio,
        .DecayLFRatio = props.flDecayLFRatio,
        .ReflectionsGain = level_mb_to_gain(gsl::narrow_cast<float>(props.lReflections)),
        .ReflectionsDelay = props.flReflectionsDelay,
        .ReflectionsPan = {props.vReflectionsPan.x, props.vReflectionsPan.y,
            props.vReflectionsPan.z},
        .LateReverbGain = level_mb_to_gain(gsl::narrow_cast<float>(props.lReverb)),
        .LateReverbDelay = props.flReverbDelay,
        .LateReverbPan = {props.vReverbPan.x, props.vReverbPan.y, props.vReverbPan.z},
        .EchoTime = props.flEchoTime,
        .EchoDepth = props.flEchoDepth,
        .ModulationTime = props.flModulationTime,
        .ModulationDepth = props.flModulationDepth,
        .AirAbsorptionGainHF = level_mb_to_gain(props.flAirAbsorptionHF),
        .HFReference = props.flHFReference,
        .LFReference = props.flLFReference,
        .RoomRolloffFactor = props.flRoomRolloffFactor,
        .DecayHFLimit = ((props.ulFlags & EAXREVERBFLAGS_DECAYHFLIMIT) != 0)};
    if(EaxTraceCommits) [[unlikely]]
    {
        const auto &ret = std::get<ReverbProps>(mAlProps);
        TRACE("Reverb commit:\n"
            "  Density: {:f}\n"
            "  Diffusion: {:f}\n"
            "  Gain: {:f}\n"
            "  GainHF: {:f}\n"
            "  GainLF: {:f}\n"
            "  DecayTime: {:f}\n"
            "  DecayHFRatio: {:f}\n"
            "  DecayLFRatio: {:f}\n"
            "  ReflectionsGain: {:f}\n"
            "  ReflectionsDelay: {:f}\n"
            "  ReflectionsPan: [{}, {}, {}]\n"
            "  LateReverbGain: {:f}\n"
            "  LateReverbDelay: {:f}\n"
            "  LateRevernPan: [{}, {}, {}]\n"
            "  EchoTime: {:f}\n"
            "  EchoDepth: {:f}\n"
            "  ModulationTime: {:f}\n"
            "  ModulationDepth: {:f}\n"
            "  AirAbsorptionGainHF: {:f}\n"
            "  HFReference: {:f}\n"
            "  LFReference: {:f}\n"
            "  RoomRolloffFactor: {:f}\n"
            "  DecayHFLimit: {}", ret.Density, ret.Diffusion, ret.Gain, ret.GainHF, ret.GainLF,
            ret.DecayTime, ret.DecayHFRatio, ret.DecayLFRatio, ret.ReflectionsGain,
            ret.ReflectionsDelay, ret.ReflectionsPan[0], ret.ReflectionsPan[1],
            ret.ReflectionsPan[2], ret.LateReverbGain, ret.LateReverbDelay, ret.LateReverbPan[0],
            ret.LateReverbPan[1], ret.LateReverbPan[2], ret.EchoTime, ret.EchoDepth,
            ret.ModulationTime, ret.ModulationDepth, ret.AirAbsorptionGainHF, ret.HFReference,
            ret.LFReference, ret.RoomRolloffFactor, ret.DecayHFLimit ? "true" : "false");
    }

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
    SetDefaults(props.emplace<EAXREVERBPROPERTIES>());
}


void EaxReverbCommitter::Get(const EaxCall &call, const EAX_REVERBPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case DSPROPERTY_EAX_ALL: call.store(props); break;
    case DSPROPERTY_EAX_ENVIRONMENT: call.store(props.environment); break;
    case DSPROPERTY_EAX_VOLUME: call.store(props.fVolume); break;
    case DSPROPERTY_EAX_DECAYTIME: call.store(props.fDecayTime_sec); break;
    case DSPROPERTY_EAX_DAMPING: call.store(props.fDamping); break;
    default: fail_unknown_property_id();
    }
}

void EaxReverbCommitter::Get(const EaxCall &call, const EAX20LISTENERPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case DSPROPERTY_EAX20LISTENER_NONE: break;
    case DSPROPERTY_EAX20LISTENER_ALLPARAMETERS: call.store(props); break;
    case DSPROPERTY_EAX20LISTENER_ROOM: call.store(props.lRoom); break;
    case DSPROPERTY_EAX20LISTENER_ROOMHF: call.store(props.lRoomHF); break;
    case DSPROPERTY_EAX20LISTENER_ROOMROLLOFFFACTOR: call.store(props.flRoomRolloffFactor); break;
    case DSPROPERTY_EAX20LISTENER_DECAYTIME: call.store(props.flDecayTime); break;
    case DSPROPERTY_EAX20LISTENER_DECAYHFRATIO: call.store(props.flDecayHFRatio); break;
    case DSPROPERTY_EAX20LISTENER_REFLECTIONS: call.store(props.lReflections); break;
    case DSPROPERTY_EAX20LISTENER_REFLECTIONSDELAY: call.store(props.flReflectionsDelay); break;
    case DSPROPERTY_EAX20LISTENER_REVERB: call.store(props.lReverb); break;
    case DSPROPERTY_EAX20LISTENER_REVERBDELAY: call.store(props.flReverbDelay); break;
    case DSPROPERTY_EAX20LISTENER_ENVIRONMENT: call.store(props.dwEnvironment); break;
    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTSIZE: call.store(props.flEnvironmentSize); break;
    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTDIFFUSION: call.store(props.flEnvironmentDiffusion); break;
    case DSPROPERTY_EAX20LISTENER_AIRABSORPTIONHF: call.store(props.flAirAbsorptionHF); break;
    case DSPROPERTY_EAX20LISTENER_FLAGS: call.store(props.dwFlags); break;
    default: fail_unknown_property_id();
    }
}

void EaxReverbCommitter::Get(const EaxCall &call, const EAXREVERBPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case EAXREVERB_NONE: break;
    case EAXREVERB_ALLPARAMETERS: call.store(props); break;
    case EAXREVERB_ENVIRONMENT: call.store(props.ulEnvironment); break;
    case EAXREVERB_ENVIRONMENTSIZE: call.store(props.flEnvironmentSize); break;
    case EAXREVERB_ENVIRONMENTDIFFUSION: call.store(props.flEnvironmentDiffusion); break;
    case EAXREVERB_ROOM: call.store(props.lRoom); break;
    case EAXREVERB_ROOMHF: call.store(props.lRoomHF); break;
    case EAXREVERB_ROOMLF: call.store(props.lRoomLF); break;
    case EAXREVERB_DECAYTIME: call.store(props.flDecayTime); break;
    case EAXREVERB_DECAYHFRATIO: call.store(props.flDecayHFRatio); break;
    case EAXREVERB_DECAYLFRATIO: call.store(props.flDecayLFRatio); break;
    case EAXREVERB_REFLECTIONS: call.store(props.lReflections); break;
    case EAXREVERB_REFLECTIONSDELAY: call.store(props.flReflectionsDelay); break;
    case EAXREVERB_REFLECTIONSPAN: call.store(props.vReflectionsPan); break;
    case EAXREVERB_REVERB: call.store(props.lReverb); break;
    case EAXREVERB_REVERBDELAY: call.store(props.flReverbDelay); break;
    case EAXREVERB_REVERBPAN: call.store(props.vReverbPan); break;
    case EAXREVERB_ECHOTIME: call.store(props.flEchoTime); break;
    case EAXREVERB_ECHODEPTH: call.store(props.flEchoDepth); break;
    case EAXREVERB_MODULATIONTIME: call.store(props.flModulationTime); break;
    case EAXREVERB_MODULATIONDEPTH: call.store(props.flModulationDepth); break;
    case EAXREVERB_AIRABSORPTIONHF: call.store(props.flAirAbsorptionHF); break;
    case EAXREVERB_HFREFERENCE: call.store(props.flHFReference); break;
    case EAXREVERB_LFREFERENCE: call.store(props.flLFReference); break;
    case EAXREVERB_ROOMROLLOFFFACTOR: call.store(props.flRoomRolloffFactor); break;
    case EAXREVERB_FLAGS: call.store(props.ulFlags); break;
    default: fail_unknown_property_id();
    }
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
    case DSPROPERTY_EAX20LISTENER_NONE: break;
    case DSPROPERTY_EAX20LISTENER_ALLPARAMETERS: defer<AllValidator2>(call, props); break;
    case DSPROPERTY_EAX20LISTENER_ROOM: defer<RoomValidator>(call, props.lRoom); break;
    case DSPROPERTY_EAX20LISTENER_ROOMHF: defer<RoomHFValidator>(call, props.lRoomHF); break;
    case DSPROPERTY_EAX20LISTENER_ROOMROLLOFFFACTOR: defer<RoomRolloffFactorValidator>(call, props.flRoomRolloffFactor); break;
    case DSPROPERTY_EAX20LISTENER_DECAYTIME: defer<DecayTimeValidator>(call, props.flDecayTime); break;
    case DSPROPERTY_EAX20LISTENER_DECAYHFRATIO: defer<DecayHFRatioValidator>(call, props.flDecayHFRatio); break;
    case DSPROPERTY_EAX20LISTENER_REFLECTIONS: defer<ReflectionsValidator>(call, props.lReflections); break;
    case DSPROPERTY_EAX20LISTENER_REFLECTIONSDELAY: defer<ReflectionsDelayValidator>(call, props.flReverbDelay); break;
    case DSPROPERTY_EAX20LISTENER_REVERB: defer<ReverbValidator>(call, props.lReverb); break;
    case DSPROPERTY_EAX20LISTENER_REVERBDELAY: defer<ReverbDelayValidator>(call, props.flReverbDelay); break;
    case DSPROPERTY_EAX20LISTENER_ENVIRONMENT: defer<EnvironmentValidator1, EnvironmentDeferrer2>(call, props, props.dwEnvironment); break;
    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTSIZE: defer<EnvironmentSizeValidator, EnvironmentSizeDeferrer2>(call, props, props.flEnvironmentSize); break;
    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTDIFFUSION: defer<EnvironmentDiffusionValidator>(call, props.flEnvironmentDiffusion); break;
    case DSPROPERTY_EAX20LISTENER_AIRABSORPTIONHF: defer<AirAbsorptionHFValidator>(call, props.flAirAbsorptionHF); break;
    case DSPROPERTY_EAX20LISTENER_FLAGS: defer<FlagsValidator2>(call, props.dwFlags); break;
    default: fail_unknown_property_id();
    }
}

void EaxReverbCommitter::Set(const EaxCall &call, EAXREVERBPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case EAXREVERB_NONE: break;
    case EAXREVERB_ALLPARAMETERS: defer<AllValidator3>(call, props); break;
    case EAXREVERB_ENVIRONMENT: defer<EnvironmentValidator3, EnvironmentDeferrer3>(call, props, props.ulEnvironment); break;
    case EAXREVERB_ENVIRONMENTSIZE: defer<EnvironmentSizeValidator, EnvironmentSizeDeferrer3>(call, props, props.flEnvironmentSize); break;
    case EAXREVERB_ENVIRONMENTDIFFUSION: defer3<EnvironmentDiffusionValidator>(call, props, props.flEnvironmentDiffusion); break;
    case EAXREVERB_ROOM: defer3<RoomValidator>(call, props, props.lRoom); break;
    case EAXREVERB_ROOMHF: defer3<RoomHFValidator>(call, props, props.lRoomHF); break;
    case EAXREVERB_ROOMLF: defer3<RoomLFValidator>(call, props, props.lRoomLF); break;
    case EAXREVERB_DECAYTIME: defer3<DecayTimeValidator>(call, props, props.flDecayTime); break;
    case EAXREVERB_DECAYHFRATIO: defer3<DecayHFRatioValidator>(call, props, props.flDecayHFRatio); break;
    case EAXREVERB_DECAYLFRATIO: defer3<DecayLFRatioValidator>(call, props, props.flDecayLFRatio); break;
    case EAXREVERB_REFLECTIONS: defer3<ReflectionsValidator>(call, props, props.lReflections); break;
    case EAXREVERB_REFLECTIONSDELAY: defer3<ReflectionsDelayValidator>(call, props, props.flReflectionsDelay); break;
    case EAXREVERB_REFLECTIONSPAN: defer3<VectorValidator>(call, props, props.vReflectionsPan); break;
    case EAXREVERB_REVERB: defer3<ReverbValidator>(call, props, props.lReverb); break;
    case EAXREVERB_REVERBDELAY: defer3<ReverbDelayValidator>(call, props, props.flReverbDelay); break;
    case EAXREVERB_REVERBPAN: defer3<VectorValidator>(call, props, props.vReverbPan); break;
    case EAXREVERB_ECHOTIME: defer3<EchoTimeValidator>(call, props, props.flEchoTime); break;
    case EAXREVERB_ECHODEPTH: defer3<EchoDepthValidator>(call, props, props.flEchoDepth); break;
    case EAXREVERB_MODULATIONTIME: defer3<ModulationTimeValidator>(call, props, props.flModulationTime); break;
    case EAXREVERB_MODULATIONDEPTH: defer3<ModulationDepthValidator>(call, props, props.flModulationDepth); break;
    case EAXREVERB_AIRABSORPTIONHF: defer3<AirAbsorptionHFValidator>(call, props, props.flAirAbsorptionHF); break;
    case EAXREVERB_HFREFERENCE: defer3<HFReferenceValidator>(call, props, props.flHFReference); break;
    case EAXREVERB_LFREFERENCE: defer3<LFReferenceValidator>(call, props, props.flLFReference); break;
    case EAXREVERB_ROOMROLLOFFFACTOR: defer3<RoomRolloffFactorValidator>(call, props, props.flRoomRolloffFactor); break;
    case EAXREVERB_FLAGS: defer3<FlagsValidator3>(call, props, props.ulFlags); break;
    default: fail_unknown_property_id();
    }
}

#endif // ALSOFT_EAX
