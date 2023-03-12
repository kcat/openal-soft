
#include "config.h"

#include <stdexcept>

#include "AL/al.h"
#include "AL/efx.h"

#include "alc/effects/base.h"
#include "aloptional.h"
#include "effects.h"

#ifdef ALSOFT_EAX
#include <cassert>
#include "alnumeric.h"
#include "al/eax/exception.h"
#include "al/eax/utils.h"
#endif // ALSOFT_EAX


namespace {

al::optional<ModulatorWaveform> WaveformFromEmum(ALenum value)
{
    switch(value)
    {
    case AL_RING_MODULATOR_SINUSOID: return ModulatorWaveform::Sinusoid;
    case AL_RING_MODULATOR_SAWTOOTH: return ModulatorWaveform::Sawtooth;
    case AL_RING_MODULATOR_SQUARE: return ModulatorWaveform::Square;
    }
    return al::nullopt;
}
ALenum EnumFromWaveform(ModulatorWaveform type)
{
    switch(type)
    {
    case ModulatorWaveform::Sinusoid: return AL_RING_MODULATOR_SINUSOID;
    case ModulatorWaveform::Sawtooth: return AL_RING_MODULATOR_SAWTOOTH;
    case ModulatorWaveform::Square: return AL_RING_MODULATOR_SQUARE;
    }
    throw std::runtime_error{"Invalid modulator waveform: " +
        std::to_string(static_cast<int>(type))};
}

void Modulator_setParamf(EffectProps *props, ALenum param, float val)
{
    switch(param)
    {
    case AL_RING_MODULATOR_FREQUENCY:
        if(!(val >= AL_RING_MODULATOR_MIN_FREQUENCY && val <= AL_RING_MODULATOR_MAX_FREQUENCY))
            throw effect_exception{AL_INVALID_VALUE, "Modulator frequency out of range: %f", val};
        props->Modulator.Frequency = val;
        break;

    case AL_RING_MODULATOR_HIGHPASS_CUTOFF:
        if(!(val >= AL_RING_MODULATOR_MIN_HIGHPASS_CUTOFF && val <= AL_RING_MODULATOR_MAX_HIGHPASS_CUTOFF))
            throw effect_exception{AL_INVALID_VALUE, "Modulator high-pass cutoff out of range: %f", val};
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
        if(auto formopt = WaveformFromEmum(val))
            props->Modulator.Waveform = *formopt;
        else
            throw effect_exception{AL_INVALID_VALUE, "Invalid modulator waveform: 0x%04x", val};
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
        *val = EnumFromWaveform(props->Modulator.Waveform);
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
    props.Modulator.Waveform       = *WaveformFromEmum(AL_RING_MODULATOR_DEFAULT_WAVEFORM);
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Modulator);

const EffectProps ModulatorEffectProps{genDefaultProps()};

#ifdef ALSOFT_EAX
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
    explicit Exception(const char *message) : EaxException{"EAX_RING_MODULATOR_EFFECT", message}
    { }
};

template<>
[[noreturn]] void ModulatorCommitter::fail(const char *message)
{
    throw Exception{message};
}

template<>
bool ModulatorCommitter::commit(const EaxEffectProps &props)
{
    if(props.mType == mEaxProps.mType
        && mEaxProps.mModulator.flFrequency == props.mModulator.flFrequency
        && mEaxProps.mModulator.flHighPassCutOff == props.mModulator.flHighPassCutOff
        && mEaxProps.mModulator.ulWaveform == props.mModulator.ulWaveform)
        return false;

    mEaxProps = props;

    auto get_waveform = [](unsigned long form)
    {
        if(form == EAX_RINGMODULATOR_SINUSOID)
            return ModulatorWaveform::Sinusoid;
        if(form == EAX_RINGMODULATOR_SAWTOOTH)
            return ModulatorWaveform::Sawtooth;
        if(form == EAX_RINGMODULATOR_SQUARE)
            return ModulatorWaveform::Square;
        return ModulatorWaveform::Sinusoid;
    };

    mAlProps.Modulator.Frequency = props.mModulator.flFrequency;
    mAlProps.Modulator.HighPassCutoff = props.mModulator.flHighPassCutOff;
    mAlProps.Modulator.Waveform = get_waveform(props.mModulator.ulWaveform);

    return true;
}

template<>
void ModulatorCommitter::SetDefaults(EaxEffectProps &props)
{
    props.mType = EaxEffectType::Modulator;
    props.mModulator.flFrequency = EAXRINGMODULATOR_DEFAULTFREQUENCY;
    props.mModulator.flHighPassCutOff = EAXRINGMODULATOR_DEFAULTHIGHPASSCUTOFF;
    props.mModulator.ulWaveform = EAXRINGMODULATOR_DEFAULTWAVEFORM;
}

template<>
void ModulatorCommitter::Get(const EaxCall &call, const EaxEffectProps &props)
{
    switch(call.get_property_id())
    {
    case EAXRINGMODULATOR_NONE: break;
    case EAXRINGMODULATOR_ALLPARAMETERS: call.set_value<Exception>(props.mModulator); break;
    case EAXRINGMODULATOR_FREQUENCY: call.set_value<Exception>(props.mModulator.flFrequency); break;
    case EAXRINGMODULATOR_HIGHPASSCUTOFF: call.set_value<Exception>(props.mModulator.flHighPassCutOff); break;
    case EAXRINGMODULATOR_WAVEFORM: call.set_value<Exception>(props.mModulator.ulWaveform); break;
    default: fail_unknown_property_id();
    }
}

template<>
void ModulatorCommitter::Set(const EaxCall &call, EaxEffectProps &props)
{
    switch (call.get_property_id())
    {
    case EAXRINGMODULATOR_NONE: break;
    case EAXRINGMODULATOR_ALLPARAMETERS: defer<AllValidator>(call, props.mModulator); break;
    case EAXRINGMODULATOR_FREQUENCY: defer<FrequencyValidator>(call, props.mModulator.flFrequency); break;
    case EAXRINGMODULATOR_HIGHPASSCUTOFF: defer<HighPassCutOffValidator>(call, props.mModulator.flHighPassCutOff); break;
    case EAXRINGMODULATOR_WAVEFORM: defer<WaveformValidator>(call, props.mModulator.ulWaveform); break;
    default: fail_unknown_property_id();
    }
}

#endif // ALSOFT_EAX
