
#include "config.h"

#include "AL/al.h"
#include "AL/efx.h"

#include "alc/effects/base.h"
#include "effects.h"

#ifdef ALSOFT_EAX
#include "alnumeric.h"
#include "al/eax/exception.h"
#include "al/eax/utils.h"
#endif // ALSOFT_EAX


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

#ifdef ALSOFT_EAX
namespace {

class EaxEqualizerEffectException : public EaxException
{
public:
    explicit EaxEqualizerEffectException(const char* message)
        : EaxException{"EAX_EQUALIZER_EFFECT", message}
    {}
}; // EaxEqualizerEffectException

class EaxEqualizerEffect final : public EaxEffect4<EaxEqualizerEffectException, EAXEQUALIZERPROPERTIES>
{
public:
    EaxEqualizerEffect(const EaxCall& call);

private:
    struct LowGainValidator {
        void operator()(long lLowGain) const
        {
            eax_validate_range<Exception>(
                "Low Gain",
                lLowGain,
                EAXEQUALIZER_MINLOWGAIN,
                EAXEQUALIZER_MAXLOWGAIN);
        }
    }; // LowGainValidator

    struct LowCutOffValidator {
        void operator()(float flLowCutOff) const
        {
            eax_validate_range<Exception>(
                "Low Cutoff",
                flLowCutOff,
                EAXEQUALIZER_MINLOWCUTOFF,
                EAXEQUALIZER_MAXLOWCUTOFF);
        }
    }; // LowCutOffValidator

    struct Mid1GainValidator {
        void operator()(long lMid1Gain) const
        {
            eax_validate_range<Exception>(
                "Mid1 Gain",
                lMid1Gain,
                EAXEQUALIZER_MINMID1GAIN,
                EAXEQUALIZER_MAXMID1GAIN);
        }
    }; // Mid1GainValidator

    struct Mid1CenterValidator {
        void operator()(float flMid1Center) const
        {
            eax_validate_range<Exception>(
                "Mid1 Center",
                flMid1Center,
                EAXEQUALIZER_MINMID1CENTER,
                EAXEQUALIZER_MAXMID1CENTER);
        }
    }; // Mid1CenterValidator

    struct Mid1WidthValidator {
        void operator()(float flMid1Width) const
        {
            eax_validate_range<Exception>(
                "Mid1 Width",
                flMid1Width,
                EAXEQUALIZER_MINMID1WIDTH,
                EAXEQUALIZER_MAXMID1WIDTH);
        }
    }; // Mid1WidthValidator

    struct Mid2GainValidator {
        void operator()(long lMid2Gain) const
        {
            eax_validate_range<Exception>(
                "Mid2 Gain",
                lMid2Gain,
                EAXEQUALIZER_MINMID2GAIN,
                EAXEQUALIZER_MAXMID2GAIN);
        }
    }; // Mid2GainValidator

    struct Mid2CenterValidator {
        void operator()(float flMid2Center) const
        {
            eax_validate_range<Exception>(
                "Mid2 Center",
                flMid2Center,
                EAXEQUALIZER_MINMID2CENTER,
                EAXEQUALIZER_MAXMID2CENTER);
        }
    }; // Mid2CenterValidator

    struct Mid2WidthValidator {
        void operator()(float flMid2Width) const
        {
            eax_validate_range<Exception>(
                "Mid2 Width",
                flMid2Width,
                EAXEQUALIZER_MINMID2WIDTH,
                EAXEQUALIZER_MAXMID2WIDTH);
        }
    }; // Mid2WidthValidator

    struct HighGainValidator {
        void operator()(long lHighGain) const
        {
            eax_validate_range<Exception>(
                "High Gain",
                lHighGain,
                EAXEQUALIZER_MINHIGHGAIN,
                EAXEQUALIZER_MAXHIGHGAIN);
        }
    }; // HighGainValidator

    struct HighCutOffValidator {
        void operator()(float flHighCutOff) const
        {
            eax_validate_range<Exception>(
                "High Cutoff",
                flHighCutOff,
                EAXEQUALIZER_MINHIGHCUTOFF,
                EAXEQUALIZER_MAXHIGHCUTOFF);
        }
    }; // HighCutOffValidator

    struct AllValidator {
        void operator()(const Props& all) const
        {
            LowGainValidator{}(all.lLowGain);
            LowCutOffValidator{}(all.flLowCutOff);
            Mid1GainValidator{}(all.lMid1Gain);
            Mid1CenterValidator{}(all.flMid1Center);
            Mid1WidthValidator{}(all.flMid1Width);
            Mid2GainValidator{}(all.lMid2Gain);
            Mid2CenterValidator{}(all.flMid2Center);
            Mid2WidthValidator{}(all.flMid2Width);
            HighGainValidator{}(all.lHighGain);
            HighCutOffValidator{}(all.flHighCutOff);
        }
    }; // AllValidator

    void set_defaults(Props& props) override;

    void set_efx_low_gain() noexcept;
    void set_efx_low_cutoff() noexcept;
    void set_efx_mid1_gain() noexcept;
    void set_efx_mid1_center() noexcept;
    void set_efx_mid1_width() noexcept;
    void set_efx_mid2_gain() noexcept;
    void set_efx_mid2_center() noexcept;
    void set_efx_mid2_width() noexcept;
    void set_efx_high_gain() noexcept;
    void set_efx_high_cutoff() noexcept;
    void set_efx_defaults() override;

    void get(const EaxCall& call, const Props& props) override;
    void set(const EaxCall& call, Props& props) override;
    bool commit_props(const Props& props) override;
}; // EaxEqualizerEffect

EaxEqualizerEffect::EaxEqualizerEffect(const EaxCall& call)
    : EaxEffect4{AL_EFFECT_EQUALIZER, call}
{}

void EaxEqualizerEffect::set_defaults(Props& props)
{
    props.lLowGain = EAXEQUALIZER_DEFAULTLOWGAIN;
    props.flLowCutOff = EAXEQUALIZER_DEFAULTLOWCUTOFF;
    props.lMid1Gain = EAXEQUALIZER_DEFAULTMID1GAIN;
    props.flMid1Center = EAXEQUALIZER_DEFAULTMID1CENTER;
    props.flMid1Width = EAXEQUALIZER_DEFAULTMID1WIDTH;
    props.lMid2Gain = EAXEQUALIZER_DEFAULTMID2GAIN;
    props.flMid2Center = EAXEQUALIZER_DEFAULTMID2CENTER;
    props.flMid2Width = EAXEQUALIZER_DEFAULTMID2WIDTH;
    props.lHighGain = EAXEQUALIZER_DEFAULTHIGHGAIN;
    props.flHighCutOff = EAXEQUALIZER_DEFAULTHIGHCUTOFF;
}

void EaxEqualizerEffect::set_efx_low_gain() noexcept
{
    al_effect_props_.Equalizer.LowGain = clamp(
        level_mb_to_gain(static_cast<float>(props_.lLowGain)),
        AL_EQUALIZER_MIN_LOW_GAIN,
        AL_EQUALIZER_MAX_LOW_GAIN);
}

void EaxEqualizerEffect::set_efx_low_cutoff() noexcept
{
    al_effect_props_.Equalizer.LowCutoff = clamp(
        props_.flLowCutOff,
        AL_EQUALIZER_MIN_LOW_CUTOFF,
        AL_EQUALIZER_MAX_LOW_CUTOFF);
}

void EaxEqualizerEffect::set_efx_mid1_gain() noexcept
{
    al_effect_props_.Equalizer.Mid1Gain = clamp(
        level_mb_to_gain(static_cast<float>(props_.lMid1Gain)),
        AL_EQUALIZER_MIN_MID1_GAIN,
        AL_EQUALIZER_MAX_MID1_GAIN);
}

void EaxEqualizerEffect::set_efx_mid1_center() noexcept
{
    al_effect_props_.Equalizer.Mid1Center = clamp(
        props_.flMid1Center,
        AL_EQUALIZER_MIN_MID1_CENTER,
        AL_EQUALIZER_MAX_MID1_CENTER);
}

void EaxEqualizerEffect::set_efx_mid1_width() noexcept
{
    al_effect_props_.Equalizer.Mid1Width = clamp(
        props_.flMid1Width,
        AL_EQUALIZER_MIN_MID1_WIDTH,
        AL_EQUALIZER_MAX_MID1_WIDTH);
}

void EaxEqualizerEffect::set_efx_mid2_gain() noexcept
{
    al_effect_props_.Equalizer.Mid2Gain = clamp(
        level_mb_to_gain(static_cast<float>(props_.lMid2Gain)),
        AL_EQUALIZER_MIN_MID2_GAIN,
        AL_EQUALIZER_MAX_MID2_GAIN);
}

void EaxEqualizerEffect::set_efx_mid2_center() noexcept
{
    al_effect_props_.Equalizer.Mid2Center = clamp(
        props_.flMid2Center,
        AL_EQUALIZER_MIN_MID2_CENTER,
        AL_EQUALIZER_MAX_MID2_CENTER);
}

void EaxEqualizerEffect::set_efx_mid2_width() noexcept
{
    al_effect_props_.Equalizer.Mid2Width = clamp(
        props_.flMid2Width,
        AL_EQUALIZER_MIN_MID2_WIDTH,
        AL_EQUALIZER_MAX_MID2_WIDTH);
}

void EaxEqualizerEffect::set_efx_high_gain() noexcept
{
    al_effect_props_.Equalizer.HighGain = clamp(
        level_mb_to_gain(static_cast<float>(props_.lHighGain)),
        AL_EQUALIZER_MIN_HIGH_GAIN,
        AL_EQUALIZER_MAX_HIGH_GAIN);
}

void EaxEqualizerEffect::set_efx_high_cutoff() noexcept
{
    al_effect_props_.Equalizer.HighCutoff = clamp(
        props_.flHighCutOff,
        AL_EQUALIZER_MIN_HIGH_CUTOFF,
        AL_EQUALIZER_MAX_HIGH_CUTOFF);
}

void EaxEqualizerEffect::set_efx_defaults()
{
    set_efx_low_gain();
    set_efx_low_cutoff();
    set_efx_mid1_gain();
    set_efx_mid1_center();
    set_efx_mid1_width();
    set_efx_mid2_gain();
    set_efx_mid2_center();
    set_efx_mid2_width();
    set_efx_high_gain();
    set_efx_high_cutoff();
}

void EaxEqualizerEffect::get(const EaxCall& call, const Props& props)
{
    switch(call.get_property_id())
    {
        case EAXEQUALIZER_NONE: break;
        case EAXEQUALIZER_ALLPARAMETERS: call.set_value<Exception>(props); break;
        case EAXEQUALIZER_LOWGAIN: call.set_value<Exception>(props.lLowGain); break;
        case EAXEQUALIZER_LOWCUTOFF: call.set_value<Exception>(props.flLowCutOff); break;
        case EAXEQUALIZER_MID1GAIN: call.set_value<Exception>(props.lMid1Gain); break;
        case EAXEQUALIZER_MID1CENTER: call.set_value<Exception>(props.flMid1Center); break;
        case EAXEQUALIZER_MID1WIDTH: call.set_value<Exception>(props.flMid1Width); break;
        case EAXEQUALIZER_MID2GAIN: call.set_value<Exception>(props.lMid2Gain); break;
        case EAXEQUALIZER_MID2CENTER: call.set_value<Exception>(props.flMid2Center); break;
        case EAXEQUALIZER_MID2WIDTH: call.set_value<Exception>(props.flMid2Width); break;
        case EAXEQUALIZER_HIGHGAIN: call.set_value<Exception>(props.lHighGain); break;
        case EAXEQUALIZER_HIGHCUTOFF: call.set_value<Exception>(props.flHighCutOff); break;
        default: fail_unknown_property_id();
    }
}

void EaxEqualizerEffect::set(const EaxCall& call, Props& props)
{
    switch(call.get_property_id())
    {
        case EAXEQUALIZER_NONE: break;
        case EAXEQUALIZER_ALLPARAMETERS: defer<AllValidator>(call, props); break;
        case EAXEQUALIZER_LOWGAIN: defer<LowGainValidator>(call, props.lLowGain); break;
        case EAXEQUALIZER_LOWCUTOFF: defer<LowCutOffValidator>(call, props.flLowCutOff); break;
        case EAXEQUALIZER_MID1GAIN: defer<Mid1GainValidator>(call, props.lMid1Gain); break;
        case EAXEQUALIZER_MID1CENTER: defer<Mid1CenterValidator>(call, props.flMid1Center); break;
        case EAXEQUALIZER_MID1WIDTH: defer<Mid1WidthValidator>(call, props.flMid1Width); break;
        case EAXEQUALIZER_MID2GAIN: defer<Mid2GainValidator>(call, props.lMid2Gain); break;
        case EAXEQUALIZER_MID2CENTER: defer<Mid2CenterValidator>(call, props.flMid2Center); break;
        case EAXEQUALIZER_MID2WIDTH: defer<Mid2WidthValidator>(call, props.flMid2Width); break;
        case EAXEQUALIZER_HIGHGAIN: defer<HighGainValidator>(call, props.lHighGain); break;
        case EAXEQUALIZER_HIGHCUTOFF: defer<HighCutOffValidator>(call, props.flHighCutOff); break;
        default: fail_unknown_property_id();
    }
}

bool EaxEqualizerEffect::commit_props(const Props& props)
{
    auto is_dirty = false;

    if (props_.lLowGain != props.lLowGain)
    {
        is_dirty = true;
        set_efx_low_gain();
    }

    if (props_.flLowCutOff != props.flLowCutOff)
    {
        is_dirty = true;
        set_efx_low_cutoff();
    }

    if (props_.lMid1Gain != props.lMid1Gain)
    {
        is_dirty = true;
        set_efx_mid1_gain();
    }

    if (props_.flMid1Center != props.flMid1Center)
    {
        is_dirty = true;
        set_efx_mid1_center();
    }

    if (props_.flMid1Width != props.flMid1Width)
    {
        is_dirty = true;
        set_efx_mid1_width();
    }

    if (props_.lMid2Gain != props.lMid2Gain)
    {
        is_dirty = true;
        set_efx_mid2_gain();
    }

    if (props_.flMid2Center != props.flMid2Center)
    {
        is_dirty = true;
        set_efx_mid2_center();
    }

    if (props_.flMid2Width != props.flMid2Width)
    {
        is_dirty = true;
        set_efx_mid2_width();
    }

    if (props_.lHighGain != props.lHighGain)
    {
        is_dirty = true;
        set_efx_high_gain();
    }

    if (props_.flHighCutOff != props.flHighCutOff)
    {
        is_dirty = true;
        set_efx_high_cutoff();
    }

    return is_dirty;
}

} // namespace

EaxEffectUPtr eax_create_eax_equalizer_effect(const EaxCall& call)
{
    return eax_create_eax4_effect<EaxEqualizerEffect>(call);
}

#endif // ALSOFT_EAX
