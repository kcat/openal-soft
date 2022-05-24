
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
    case AL_RING_MODULATOR_SINUSOID: return al::make_optional(ModulatorWaveform::Sinusoid);
    case AL_RING_MODULATOR_SAWTOOTH: return al::make_optional(ModulatorWaveform::Sawtooth);
    case AL_RING_MODULATOR_SQUARE: return al::make_optional(ModulatorWaveform::Square);
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

class EaxRingModulatorEffectException : public EaxException
{
public:
    explicit EaxRingModulatorEffectException(const char* message)
        : EaxException{"EAX_RING_MODULATOR_EFFECT", message}
    {}
}; // EaxRingModulatorEffectException

class EaxRingModulatorEffect final : public EaxEffect4<EaxRingModulatorEffectException, EAXRINGMODULATORPROPERTIES>
{
public:
    EaxRingModulatorEffect(const EaxCall& call);

private:
    struct FrequencyValidator {
        void operator()(float flFrequency) const
        {
            eax_validate_range<EaxRingModulatorEffectException>(
                "Frequency",
                flFrequency,
                EAXRINGMODULATOR_MINFREQUENCY,
                EAXRINGMODULATOR_MAXFREQUENCY);
        }
    }; // FrequencyValidator

    struct HighPassCutOffValidator {
        void operator()(float flHighPassCutOff) const
        {
            eax_validate_range<EaxRingModulatorEffectException>(
                "High-Pass Cutoff",
                flHighPassCutOff,
                EAXRINGMODULATOR_MINHIGHPASSCUTOFF,
                EAXRINGMODULATOR_MAXHIGHPASSCUTOFF);
        }
    }; // HighPassCutOffValidator

    struct WaveformValidator {
        void operator()(unsigned long ulWaveform) const
        {
            eax_validate_range<EaxRingModulatorEffectException>(
                "Waveform",
                ulWaveform,
                EAXRINGMODULATOR_MINWAVEFORM,
                EAXRINGMODULATOR_MAXWAVEFORM);
        }
    }; // WaveformValidator

    struct AllValidator {
        void operator()(const Props& all) const
        {
            FrequencyValidator{}(all.flFrequency);
            HighPassCutOffValidator{}(all.flHighPassCutOff);
            WaveformValidator{}(all.ulWaveform);
        }
    }; // AllValidator

    void set_defaults(Props& props) override;

    void set_efx_frequency() noexcept;
    void set_efx_high_pass_cutoff() noexcept;
    void set_efx_waveform();
    void set_efx_defaults() override;

    void get(const EaxCall& call, const Props& props) override;
    void set(const EaxCall& call, Props& props) override;
    bool commit_props(const Props& props) override;
}; // EaxRingModulatorEffect

EaxRingModulatorEffect::EaxRingModulatorEffect(const EaxCall& call)
    : EaxEffect4{AL_EFFECT_RING_MODULATOR, call}
{}

void EaxRingModulatorEffect::set_defaults(Props& props)
{
    props.flFrequency = EAXRINGMODULATOR_DEFAULTFREQUENCY;
    props.flHighPassCutOff = EAXRINGMODULATOR_DEFAULTHIGHPASSCUTOFF;
    props.ulWaveform = EAXRINGMODULATOR_DEFAULTWAVEFORM;
}

void EaxRingModulatorEffect::set_efx_frequency() noexcept
{
    al_effect_props_.Modulator.Frequency = clamp(
        props_.flFrequency,
        AL_RING_MODULATOR_MIN_FREQUENCY,
        AL_RING_MODULATOR_MAX_FREQUENCY);
}

void EaxRingModulatorEffect::set_efx_high_pass_cutoff() noexcept
{
    al_effect_props_.Modulator.HighPassCutoff = clamp(
        props_.flHighPassCutOff,
        AL_RING_MODULATOR_MIN_HIGHPASS_CUTOFF,
        AL_RING_MODULATOR_MAX_HIGHPASS_CUTOFF);
}

void EaxRingModulatorEffect::set_efx_waveform()
{
    const auto waveform = clamp(
        static_cast<ALint>(props_.ulWaveform),
        AL_RING_MODULATOR_MIN_WAVEFORM,
        AL_RING_MODULATOR_MAX_WAVEFORM);
    const auto efx_waveform = WaveformFromEmum(waveform);
    assert(efx_waveform.has_value());
    al_effect_props_.Modulator.Waveform = *efx_waveform;
}

void EaxRingModulatorEffect::set_efx_defaults()
{
    set_efx_frequency();
    set_efx_high_pass_cutoff();
    set_efx_waveform();
}

void EaxRingModulatorEffect::get(const EaxCall& call, const Props& props)
{
    switch(call.get_property_id())
    {
        case EAXRINGMODULATOR_NONE: break;
        case EAXRINGMODULATOR_ALLPARAMETERS: call.set_value<Exception>(props); break;
        case EAXRINGMODULATOR_FREQUENCY: call.set_value<Exception>(props.flFrequency); break;
        case EAXRINGMODULATOR_HIGHPASSCUTOFF: call.set_value<Exception>(props.flHighPassCutOff); break;
        case EAXRINGMODULATOR_WAVEFORM: call.set_value<Exception>(props.ulWaveform); break;
        default: fail_unknown_property_id();
    }
}

void EaxRingModulatorEffect::set(const EaxCall& call, Props& props)
{
    switch (call.get_property_id())
    {
        case EAXRINGMODULATOR_NONE: break;
        case EAXRINGMODULATOR_ALLPARAMETERS: defer<AllValidator>(call, props); break;
        case EAXRINGMODULATOR_FREQUENCY: defer<FrequencyValidator>(call, props.flFrequency); break;
        case EAXRINGMODULATOR_HIGHPASSCUTOFF: defer<HighPassCutOffValidator>(call, props.flHighPassCutOff); break;
        case EAXRINGMODULATOR_WAVEFORM: defer<WaveformValidator>(call, props.ulWaveform); break;
        default: fail_unknown_property_id();
    }
}

bool EaxRingModulatorEffect::commit_props(const Props& props)
{
    auto is_dirty = false;

    if (props_.flFrequency != props.flFrequency)
    {
        is_dirty = true;
        set_efx_frequency();
    }

    if (props_.flHighPassCutOff != props.flHighPassCutOff)
    {
        is_dirty = true;
        set_efx_high_pass_cutoff();
    }

    if (props_.ulWaveform != props.ulWaveform)
    {
        is_dirty = true;
        set_efx_waveform();
    }

    return is_dirty;
}

} // namespace

EaxEffectUPtr eax_create_eax_ring_modulator_effect(const EaxCall& call)
{
    return eax_create_eax4_effect<EaxRingModulatorEffect>(call);
}

#endif // ALSOFT_EAX
