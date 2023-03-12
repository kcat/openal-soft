
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

using DistortionCommitter = EaxCommitter<EaxDistortionCommitter>;

struct EdgeValidator {
    void operator()(float flEdge) const
    {
        eax_validate_range<DistortionCommitter::Exception>(
            "Edge",
            flEdge,
            EAXDISTORTION_MINEDGE,
            EAXDISTORTION_MAXEDGE);
    }
}; // EdgeValidator

struct GainValidator {
    void operator()(long lGain) const
    {
        eax_validate_range<DistortionCommitter::Exception>(
            "Gain",
            lGain,
            EAXDISTORTION_MINGAIN,
            EAXDISTORTION_MAXGAIN);
    }
}; // GainValidator

struct LowPassCutOffValidator {
    void operator()(float flLowPassCutOff) const
    {
        eax_validate_range<DistortionCommitter::Exception>(
            "Low-pass Cut-off",
            flLowPassCutOff,
            EAXDISTORTION_MINLOWPASSCUTOFF,
            EAXDISTORTION_MAXLOWPASSCUTOFF);
    }
}; // LowPassCutOffValidator

struct EqCenterValidator {
    void operator()(float flEQCenter) const
    {
        eax_validate_range<DistortionCommitter::Exception>(
            "EQ Center",
            flEQCenter,
            EAXDISTORTION_MINEQCENTER,
            EAXDISTORTION_MAXEQCENTER);
    }
}; // EqCenterValidator

struct EqBandwidthValidator {
    void operator()(float flEQBandwidth) const
    {
        eax_validate_range<DistortionCommitter::Exception>(
            "EQ Bandwidth",
            flEQBandwidth,
            EAXDISTORTION_MINEQBANDWIDTH,
            EAXDISTORTION_MAXEQBANDWIDTH);
    }
}; // EqBandwidthValidator

struct AllValidator {
    void operator()(const EAXDISTORTIONPROPERTIES& all) const
    {
        EdgeValidator{}(all.flEdge);
        GainValidator{}(all.lGain);
        LowPassCutOffValidator{}(all.flLowPassCutOff);
        EqCenterValidator{}(all.flEQCenter);
        EqBandwidthValidator{}(all.flEQBandwidth);
    }
}; // AllValidator

} // namespace

template<>
struct DistortionCommitter::Exception : public EaxException {
    explicit Exception(const char *message) : EaxException{"EAX_DISTORTION_EFFECT", message}
    { }
};

template<>
[[noreturn]] void DistortionCommitter::fail(const char *message)
{
    throw Exception{message};
}

template<>
bool DistortionCommitter::commit(const EaxEffectProps &props)
{
    if(props.mType == mEaxProps.mType && mEaxProps.mDistortion.flEdge == props.mDistortion.flEdge
        && mEaxProps.mDistortion.lGain == props.mDistortion.lGain
        && mEaxProps.mDistortion.flLowPassCutOff == props.mDistortion.flLowPassCutOff
        && mEaxProps.mDistortion.flEQCenter == props.mDistortion.flEQCenter
        && mEaxProps.mDistortion.flEQBandwidth == props.mDistortion.flEQBandwidth)
        return false;

    mEaxProps = props;

    mAlProps.Distortion.Edge = props.mDistortion.flEdge;
    mAlProps.Distortion.Gain = level_mb_to_gain(static_cast<float>(props.mDistortion.lGain));
    mAlProps.Distortion.LowpassCutoff = props.mDistortion.flLowPassCutOff;
    mAlProps.Distortion.EQCenter = props.mDistortion.flEQCenter;
    mAlProps.Distortion.EQBandwidth = props.mDistortion.flEdge;

    return true;
}

template<>
void DistortionCommitter::SetDefaults(EaxEffectProps &props)
{
    props.mType = EaxEffectType::Distortion;
    props.mDistortion.flEdge = EAXDISTORTION_DEFAULTEDGE;
    props.mDistortion.lGain = EAXDISTORTION_DEFAULTGAIN;
    props.mDistortion.flLowPassCutOff = EAXDISTORTION_DEFAULTLOWPASSCUTOFF;
    props.mDistortion.flEQCenter = EAXDISTORTION_DEFAULTEQCENTER;
    props.mDistortion.flEQBandwidth = EAXDISTORTION_DEFAULTEQBANDWIDTH;
}

template<>
void DistortionCommitter::Get(const EaxCall &call, const EaxEffectProps &props)
{
    switch(call.get_property_id())
    {
    case EAXDISTORTION_NONE: break;
    case EAXDISTORTION_ALLPARAMETERS: call.set_value<Exception>(props.mDistortion); break;
    case EAXDISTORTION_EDGE: call.set_value<Exception>(props.mDistortion.flEdge); break;
    case EAXDISTORTION_GAIN: call.set_value<Exception>(props.mDistortion.lGain); break;
    case EAXDISTORTION_LOWPASSCUTOFF: call.set_value<Exception>(props.mDistortion.flLowPassCutOff); break;
    case EAXDISTORTION_EQCENTER: call.set_value<Exception>(props.mDistortion.flEQCenter); break;
    case EAXDISTORTION_EQBANDWIDTH: call.set_value<Exception>(props.mDistortion.flEQBandwidth); break;
    default: fail_unknown_property_id();
    }
}

template<>
void DistortionCommitter::Set(const EaxCall &call, EaxEffectProps &props)
{
    switch(call.get_property_id())
    {
    case EAXDISTORTION_NONE: break;
    case EAXDISTORTION_ALLPARAMETERS: defer<AllValidator>(call, props.mDistortion); break;
    case EAXDISTORTION_EDGE: defer<EdgeValidator>(call, props.mDistortion.flEdge); break;
    case EAXDISTORTION_GAIN: defer<GainValidator>(call, props.mDistortion.lGain); break;
    case EAXDISTORTION_LOWPASSCUTOFF: defer<LowPassCutOffValidator>(call, props.mDistortion.flLowPassCutOff); break;
    case EAXDISTORTION_EQCENTER: defer<EqCenterValidator>(call, props.mDistortion.flEQCenter); break;
    case EAXDISTORTION_EQBANDWIDTH: defer<EqBandwidthValidator>(call, props.mDistortion.flEQBandwidth); break;
    default: fail_unknown_property_id();
    }
}

#endif // ALSOFT_EAX
