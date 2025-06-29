
#include "config.h"

#include <optional>
#include <stdexcept>

#include "AL/al.h"
#include "AL/efx.h"

#include "alc/context.h"
#include "alnumeric.h"
#include "effects.h"
#include "gsl/gsl"

#if ALSOFT_EAX
#include <cassert>

#include "al/eax/effect.h"
#include "al/eax/exception.h"
#include "al/eax/utils.h"
#endif // ALSOFT_EAX


namespace {

constexpr auto WaveformFromEmum(ALenum value) noexcept -> std::optional<ModulatorWaveform>
{
    switch(value)
    {
    case AL_RING_MODULATOR_SINUSOID: return ModulatorWaveform::Sinusoid;
    case AL_RING_MODULATOR_SAWTOOTH: return ModulatorWaveform::Sawtooth;
    case AL_RING_MODULATOR_SQUARE: return ModulatorWaveform::Square;
    }
    return std::nullopt;
}
constexpr auto EnumFromWaveform(ModulatorWaveform type) -> ALenum
{
    switch(type)
    {
    case ModulatorWaveform::Sinusoid: return AL_RING_MODULATOR_SINUSOID;
    case ModulatorWaveform::Sawtooth: return AL_RING_MODULATOR_SAWTOOTH;
    case ModulatorWaveform::Square: return AL_RING_MODULATOR_SQUARE;
    }
    throw std::runtime_error{fmt::format("Invalid modulator waveform: {}",
        int{al::to_underlying(type)})};
}

consteval auto genDefaultProps() noexcept -> EffectProps
{
    return ModulatorProps{
        .Frequency      = AL_RING_MODULATOR_DEFAULT_FREQUENCY,
        .HighPassCutoff = AL_RING_MODULATOR_DEFAULT_HIGHPASS_CUTOFF,
        /* NOLINTNEXTLINE(bugprone-unchecked-optional-access) */
        .Waveform       = WaveformFromEmum(AL_RING_MODULATOR_DEFAULT_WAVEFORM).value()};
}

} // namespace

constinit const EffectProps ModulatorEffectProps(genDefaultProps());

void ModulatorEffectHandler::SetParami(ALCcontext *context, ModulatorProps &props, ALenum param, int val)
{
    switch(param)
    {
    case AL_RING_MODULATOR_FREQUENCY:
    case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
        SetParamf(context, props, param, gsl::narrow_cast<float>(val));
        return;

    case AL_RING_MODULATOR_WAVEFORM:
        if(auto formopt = WaveformFromEmum(val))
            props.Waveform = *formopt;
        else
            context->throw_error(AL_INVALID_VALUE, "Invalid modulator waveform: {:#04x}",
                as_unsigned(val));
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid modulator integer property {:#04x}",
        as_unsigned(param));
}
void ModulatorEffectHandler::SetParamiv(ALCcontext *context, ModulatorProps &props, ALenum param, const int *vals)
{ SetParami(context, props, param, *vals); }

void ModulatorEffectHandler::SetParamf(ALCcontext *context, ModulatorProps &props, ALenum param, float val)
{
    switch(param)
    {
    case AL_RING_MODULATOR_FREQUENCY:
        if(!(val >= AL_RING_MODULATOR_MIN_FREQUENCY && val <= AL_RING_MODULATOR_MAX_FREQUENCY))
            context->throw_error(AL_INVALID_VALUE, "Modulator frequency out of range: {:f}", val);
        props.Frequency = val;
        return;

    case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
        if(!(val >= AL_RING_MODULATOR_MIN_HIGHPASS_CUTOFF && val <= AL_RING_MODULATOR_MAX_HIGHPASS_CUTOFF))
            context->throw_error(AL_INVALID_VALUE, "Modulator high-pass cutoff out of range: {:f}",
                val);
        props.HighPassCutoff = val;
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid modulator float property {:#04x}",
        as_unsigned(param));
}
void ModulatorEffectHandler::SetParamfv(ALCcontext *context, ModulatorProps &props, ALenum param, const float *vals)
{ SetParamf(context, props, param, *vals); }

void ModulatorEffectHandler::GetParami(ALCcontext *context, const ModulatorProps &props, ALenum param, int *val)
{
    switch(param)
    {
    case AL_RING_MODULATOR_FREQUENCY: *val = gsl::narrow_cast<int>(props.Frequency); return;
    case AL_RING_MODULATOR_HIGHPASS_CUTOFF: *val = gsl::narrow_cast<int>(props.HighPassCutoff); return;
    case AL_RING_MODULATOR_WAVEFORM: *val = EnumFromWaveform(props.Waveform); return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid modulator integer property {:#04x}",
        as_unsigned(param));
}
void ModulatorEffectHandler::GetParamiv(ALCcontext *context, const ModulatorProps &props, ALenum param, int *vals)
{ GetParami(context, props, param, vals); }
void ModulatorEffectHandler::GetParamf(ALCcontext *context, const ModulatorProps &props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_RING_MODULATOR_FREQUENCY: *val = props.Frequency; return;
    case AL_RING_MODULATOR_HIGHPASS_CUTOFF: *val = props.HighPassCutoff; return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid modulator float property {:#04x}",
        as_unsigned(param));
}
void ModulatorEffectHandler::GetParamfv(ALCcontext *context, const ModulatorProps &props, ALenum param, float *vals)
{ GetParamf(context, props, param, vals); }


#if ALSOFT_EAX
namespace {

using ModulatorCommitter = EaxCommitter<EaxModulatorCommitter>;

struct FrequencyValidator {
    void operator()(float flFrequency) const
    {
        eax_validate_range<ModulatorCommitter::Exception>(
            "Frequency",
            flFrequency,
            EAXRINGMODULATOR_MINFREQUENCY,
            EAXRINGMODULATOR_MAXFREQUENCY);
    }
}; // FrequencyValidator

struct HighPassCutOffValidator {
    void operator()(float flHighPassCutOff) const
    {
        eax_validate_range<ModulatorCommitter::Exception>(
            "High-Pass Cutoff",
            flHighPassCutOff,
            EAXRINGMODULATOR_MINHIGHPASSCUTOFF,
            EAXRINGMODULATOR_MAXHIGHPASSCUTOFF);
    }
}; // HighPassCutOffValidator

struct WaveformValidator {
    void operator()(unsigned long ulWaveform) const
    {
        eax_validate_range<ModulatorCommitter::Exception>(
            "Waveform",
            ulWaveform,
            EAXRINGMODULATOR_MINWAVEFORM,
            EAXRINGMODULATOR_MAXWAVEFORM);
    }
}; // WaveformValidator

struct AllValidator {
    void operator()(const EAXRINGMODULATORPROPERTIES& all) const
    {
        FrequencyValidator{}(all.flFrequency);
        HighPassCutOffValidator{}(all.flHighPassCutOff);
        WaveformValidator{}(all.ulWaveform);
    }
}; // AllValidator

} // namespace

template<>
struct ModulatorCommitter::Exception : public EaxException {
    explicit Exception(const std::string_view message)
        : EaxException{"EAX_RING_MODULATOR_EFFECT", message}
    { }
};

template<> [[noreturn]]
void ModulatorCommitter::fail(const std::string_view message)
{ throw Exception{message}; }

bool EaxModulatorCommitter::commit(const EAXRINGMODULATORPROPERTIES &props)
{
    if(auto *cur = std::get_if<EAXRINGMODULATORPROPERTIES>(&mEaxProps); cur && *cur == props)
        return false;

    static constexpr auto get_waveform = [](unsigned long form)
    {
        switch(form)
        {
        case EAX_RINGMODULATOR_SINUSOID: return ModulatorWaveform::Sinusoid;
        case EAX_RINGMODULATOR_SAWTOOTH: return ModulatorWaveform::Sawtooth;
        case EAX_RINGMODULATOR_SQUARE: return ModulatorWaveform::Square;
        default: break;
        }
        return ModulatorWaveform::Sinusoid;
    };

    mEaxProps = props;
    mAlProps = ModulatorProps{
        .Frequency = props.flFrequency,
        .HighPassCutoff = props.flHighPassCutOff,
        .Waveform = get_waveform(props.ulWaveform)};

    return true;
}

void EaxModulatorCommitter::SetDefaults(EaxEffectProps &props)
{
    props = EAXRINGMODULATORPROPERTIES{
        .flFrequency = EAXRINGMODULATOR_DEFAULTFREQUENCY,
        .flHighPassCutOff = EAXRINGMODULATOR_DEFAULTHIGHPASSCUTOFF,
        .ulWaveform = EAXRINGMODULATOR_DEFAULTWAVEFORM};
}

void EaxModulatorCommitter::Get(const EaxCall &call, const EAXRINGMODULATORPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case EAXRINGMODULATOR_NONE: break;
    case EAXRINGMODULATOR_ALLPARAMETERS: call.store(props); break;
    case EAXRINGMODULATOR_FREQUENCY: call.store(props.flFrequency); break;
    case EAXRINGMODULATOR_HIGHPASSCUTOFF: call.store(props.flHighPassCutOff); break;
    case EAXRINGMODULATOR_WAVEFORM: call.store(props.ulWaveform); break;
    default: fail_unknown_property_id();
    }
}

void EaxModulatorCommitter::Set(const EaxCall &call, EAXRINGMODULATORPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case EAXRINGMODULATOR_NONE: break;
    case EAXRINGMODULATOR_ALLPARAMETERS: defer<AllValidator>(call, props); break;
    case EAXRINGMODULATOR_FREQUENCY: defer<FrequencyValidator>(call, props.flFrequency); break;
    case EAXRINGMODULATOR_HIGHPASSCUTOFF: defer<HighPassCutOffValidator>(call, props.flHighPassCutOff); break;
    case EAXRINGMODULATOR_WAVEFORM: defer<WaveformValidator>(call, props.ulWaveform); break;
    default: fail_unknown_property_id();
    }
}

#endif // ALSOFT_EAX
