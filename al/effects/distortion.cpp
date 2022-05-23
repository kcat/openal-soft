
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

void Distortion_setParami(EffectProps*, ALenum param, int)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid distortion integer property 0x%04x", param}; }
void Distortion_setParamiv(EffectProps*, ALenum param, const int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid distortion integer-vector property 0x%04x",
        param};
}
void Distortion_setParamf(EffectProps *props, ALenum param, float val)
{
    switch(param)
    {
    case AL_DISTORTION_EDGE:
        if(!(val >= AL_DISTORTION_MIN_EDGE && val <= AL_DISTORTION_MAX_EDGE))
            throw effect_exception{AL_INVALID_VALUE, "Distortion edge out of range"};
        props->Distortion.Edge = val;
        break;

    case AL_DISTORTION_GAIN:
        if(!(val >= AL_DISTORTION_MIN_GAIN && val <= AL_DISTORTION_MAX_GAIN))
            throw effect_exception{AL_INVALID_VALUE, "Distortion gain out of range"};
        props->Distortion.Gain = val;
        break;

    case AL_DISTORTION_LOWPASS_CUTOFF:
        if(!(val >= AL_DISTORTION_MIN_LOWPASS_CUTOFF && val <= AL_DISTORTION_MAX_LOWPASS_CUTOFF))
            throw effect_exception{AL_INVALID_VALUE, "Distortion low-pass cutoff out of range"};
        props->Distortion.LowpassCutoff = val;
        break;

    case AL_DISTORTION_EQCENTER:
        if(!(val >= AL_DISTORTION_MIN_EQCENTER && val <= AL_DISTORTION_MAX_EQCENTER))
            throw effect_exception{AL_INVALID_VALUE, "Distortion EQ center out of range"};
        props->Distortion.EQCenter = val;
        break;

    case AL_DISTORTION_EQBANDWIDTH:
        if(!(val >= AL_DISTORTION_MIN_EQBANDWIDTH && val <= AL_DISTORTION_MAX_EQBANDWIDTH))
            throw effect_exception{AL_INVALID_VALUE, "Distortion EQ bandwidth out of range"};
        props->Distortion.EQBandwidth = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid distortion float property 0x%04x", param};
    }
}
void Distortion_setParamfv(EffectProps *props, ALenum param, const float *vals)
{ Distortion_setParamf(props, param, vals[0]); }

void Distortion_getParami(const EffectProps*, ALenum param, int*)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid distortion integer property 0x%04x", param}; }
void Distortion_getParamiv(const EffectProps*, ALenum param, int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid distortion integer-vector property 0x%04x",
        param};
}
void Distortion_getParamf(const EffectProps *props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_DISTORTION_EDGE:
        *val = props->Distortion.Edge;
        break;

    case AL_DISTORTION_GAIN:
        *val = props->Distortion.Gain;
        break;

    case AL_DISTORTION_LOWPASS_CUTOFF:
        *val = props->Distortion.LowpassCutoff;
        break;

    case AL_DISTORTION_EQCENTER:
        *val = props->Distortion.EQCenter;
        break;

    case AL_DISTORTION_EQBANDWIDTH:
        *val = props->Distortion.EQBandwidth;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid distortion float property 0x%04x", param};
    }
}
void Distortion_getParamfv(const EffectProps *props, ALenum param, float *vals)
{ Distortion_getParamf(props, param, vals); }

EffectProps genDefaultProps() noexcept
{
    EffectProps props{};
    props.Distortion.Edge = AL_DISTORTION_DEFAULT_EDGE;
    props.Distortion.Gain = AL_DISTORTION_DEFAULT_GAIN;
    props.Distortion.LowpassCutoff = AL_DISTORTION_DEFAULT_LOWPASS_CUTOFF;
    props.Distortion.EQCenter = AL_DISTORTION_DEFAULT_EQCENTER;
    props.Distortion.EQBandwidth = AL_DISTORTION_DEFAULT_EQBANDWIDTH;
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Distortion);

const EffectProps DistortionEffectProps{genDefaultProps()};

#ifdef ALSOFT_EAX
namespace {

class EaxDistortionEffectException : public EaxException
{
public:
    explicit EaxDistortionEffectException(const char* message)
        : EaxException{"EAX_DISTORTION_EFFECT", message}
    {}
}; // EaxDistortionEffectException

class EaxDistortionEffect final : public EaxEffect4<EaxDistortionEffectException, EAXDISTORTIONPROPERTIES>
{
public:
    EaxDistortionEffect(const EaxCall& call);

private:
    struct EdgeValidator {
        void operator()(float flEdge) const
        {
            eax_validate_range<Exception>(
                "Edge",
                flEdge,
                EAXDISTORTION_MINEDGE,
                EAXDISTORTION_MAXEDGE);
        }
    }; // EdgeValidator

    struct GainValidator {
        void operator()(long lGain) const
        {
            eax_validate_range<Exception>(
                "Gain",
                lGain,
                EAXDISTORTION_MINGAIN,
                EAXDISTORTION_MAXGAIN);
        }
    }; // GainValidator

    struct LowPassCutOffValidator {
        void operator()(float flLowPassCutOff) const
        {
            eax_validate_range<Exception>(
                "Low-pass Cut-off",
                flLowPassCutOff,
                EAXDISTORTION_MINLOWPASSCUTOFF,
                EAXDISTORTION_MAXLOWPASSCUTOFF);
        }
    }; // LowPassCutOffValidator

    struct EqCenterValidator {
        void operator()(float flEQCenter) const
        {
            eax_validate_range<Exception>(
                "EQ Center",
                flEQCenter,
                EAXDISTORTION_MINEQCENTER,
                EAXDISTORTION_MAXEQCENTER);
        }
    }; // EqCenterValidator

    struct EqBandwidthValidator {
        void operator()(float flEQBandwidth) const
        {
            eax_validate_range<Exception>(
                "EQ Bandwidth",
                flEQBandwidth,
                EAXDISTORTION_MINEQBANDWIDTH,
                EAXDISTORTION_MAXEQBANDWIDTH);
        }
    }; // EqBandwidthValidator

    struct AllValidator {
        void operator()(const Props& all) const
        {
            EdgeValidator{}(all.flEdge);
            GainValidator{}(all.lGain);
            LowPassCutOffValidator{}(all.flLowPassCutOff);
            EqCenterValidator{}(all.flEQCenter);
            EqBandwidthValidator{}(all.flEQBandwidth);
        }
    }; // AllValidator

    void set_defaults(Props& props) override;

    void set_efx_edge() noexcept;
    void set_efx_gain() noexcept;
    void set_efx_lowpass_cutoff() noexcept;
    void set_efx_eq_center() noexcept;
    void set_efx_eq_bandwidth() noexcept;
    void set_efx_defaults() override;

    void get(const EaxCall& call, const Props& props) override;
    void set(const EaxCall& call, Props& props) override;
    bool commit_props(const Props& props) override;
}; // EaxDistortionEffect

EaxDistortionEffect::EaxDistortionEffect(const EaxCall& call)
    : EaxEffect4{AL_EFFECT_DISTORTION, call}
{}

void EaxDistortionEffect::set_defaults(Props& props)
{
    props.flEdge = EAXDISTORTION_DEFAULTEDGE;
    props.lGain = EAXDISTORTION_DEFAULTGAIN;
    props.flLowPassCutOff = EAXDISTORTION_DEFAULTLOWPASSCUTOFF;
    props.flEQCenter = EAXDISTORTION_DEFAULTEQCENTER;
    props.flEQBandwidth = EAXDISTORTION_DEFAULTEQBANDWIDTH;
}

void EaxDistortionEffect::set_efx_edge() noexcept
{
    al_effect_props_.Distortion.Edge = clamp(
        props_.flEdge,
        AL_DISTORTION_MIN_EDGE,
        AL_DISTORTION_MAX_EDGE);
}

void EaxDistortionEffect::set_efx_gain() noexcept
{
    al_effect_props_.Distortion.Gain = clamp(
        level_mb_to_gain(static_cast<float>(props_.lGain)),
        AL_DISTORTION_MIN_GAIN,
        AL_DISTORTION_MAX_GAIN);
}

void EaxDistortionEffect::set_efx_lowpass_cutoff() noexcept
{
    al_effect_props_.Distortion.LowpassCutoff = clamp(
        props_.flLowPassCutOff,
        AL_DISTORTION_MIN_LOWPASS_CUTOFF,
        AL_DISTORTION_MAX_LOWPASS_CUTOFF);
}

void EaxDistortionEffect::set_efx_eq_center() noexcept
{
    al_effect_props_.Distortion.EQCenter = clamp(
        props_.flEQCenter,
        AL_DISTORTION_MIN_EQCENTER,
        AL_DISTORTION_MAX_EQCENTER);
}

void EaxDistortionEffect::set_efx_eq_bandwidth() noexcept
{
    al_effect_props_.Distortion.EQBandwidth = clamp(
        props_.flEdge,
        AL_DISTORTION_MIN_EQBANDWIDTH,
        AL_DISTORTION_MAX_EQBANDWIDTH);
}

void EaxDistortionEffect::set_efx_defaults()
{
    set_efx_edge();
    set_efx_gain();
    set_efx_lowpass_cutoff();
    set_efx_eq_center();
    set_efx_eq_bandwidth();
}

void EaxDistortionEffect::get(const EaxCall& call, const Props& props)
{
    switch(call.get_property_id())
    {
        case EAXDISTORTION_NONE: break;
        case EAXDISTORTION_ALLPARAMETERS: call.set_value<Exception>(props); break;
        case EAXDISTORTION_EDGE: call.set_value<Exception>(props.flEdge); break;
        case EAXDISTORTION_GAIN: call.set_value<Exception>(props.lGain); break;
        case EAXDISTORTION_LOWPASSCUTOFF: call.set_value<Exception>(props.flLowPassCutOff); break;
        case EAXDISTORTION_EQCENTER: call.set_value<Exception>(props.flEQCenter); break;
        case EAXDISTORTION_EQBANDWIDTH: call.set_value<Exception>(props.flEQBandwidth); break;
        default: fail_unknown_property_id();
    }
}

void EaxDistortionEffect::set(const EaxCall& call, Props& props)
{
    switch(call.get_property_id())
    {
        case EAXDISTORTION_NONE: break;
        case EAXDISTORTION_ALLPARAMETERS: defer<AllValidator>(call, props); break;
        case EAXDISTORTION_EDGE: defer<EdgeValidator>(call, props.flEdge); break;
        case EAXDISTORTION_GAIN: defer<GainValidator>(call, props.lGain); break;
        case EAXDISTORTION_LOWPASSCUTOFF: defer<LowPassCutOffValidator>(call, props.flLowPassCutOff); break;
        case EAXDISTORTION_EQCENTER: defer<EqCenterValidator>(call, props.flEQCenter); break;
        case EAXDISTORTION_EQBANDWIDTH: defer<EqBandwidthValidator>(call, props.flEQBandwidth); break;
        default: fail_unknown_property_id();
    }
}

bool EaxDistortionEffect::commit_props(const Props& props)
{
    auto is_dirty = false;

    if (props_.flEdge != props.flEdge)
    {
        is_dirty = true;
        set_efx_edge();
    }

    if (props_.lGain != props.lGain)
    {
        is_dirty = true;
        set_efx_gain();
    }

    if (props_.flLowPassCutOff != props.flLowPassCutOff)
    {
        is_dirty = true;
        set_efx_lowpass_cutoff();
    }

    if (props_.flEQCenter != props.flEQCenter)
    {
        is_dirty = true;
        set_efx_eq_center();
    }

    if (props_.flEQBandwidth != props.flEQBandwidth)
    {
        is_dirty = true;
        set_efx_eq_bandwidth();
    }

    return is_dirty;
}

} // namespace

EaxEffectUPtr eax_create_eax_distortion_effect(const EaxCall& call)
{
    return eax_create_eax4_effect<EaxDistortionEffect>(call);
}

#endif // ALSOFT_EAX
