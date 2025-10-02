
#include "config.h"

#include "AL/al.h"
#include "AL/efx.h"

#include "alc/context.h"
#include "alnumeric.h"
#include "effects.h"
#include "gsl/gsl"

#if ALSOFT_EAX
#include "al/eax/effect.h"
#include "al/eax/exception.h"
#include "al/eax/utils.h"
#endif // ALSOFT_EAX


namespace {

consteval auto genDefaultProps() noexcept -> EffectProps
{
    return DistortionProps{
        .Edge = AL_DISTORTION_DEFAULT_EDGE,
        .Gain = AL_DISTORTION_DEFAULT_GAIN,
        .LowpassCutoff = AL_DISTORTION_DEFAULT_LOWPASS_CUTOFF,
        .EQCenter = AL_DISTORTION_DEFAULT_EQCENTER,
        .EQBandwidth = AL_DISTORTION_DEFAULT_EQBANDWIDTH};
}

} // namespace

constinit const EffectProps DistortionEffectProps(genDefaultProps());

void DistortionEffectHandler::SetParami(al::Context *context, DistortionProps&, ALenum param, int)
{ context->throw_error(AL_INVALID_ENUM, "Invalid distortion integer property {:#04x}", as_unsigned(param)); }
void DistortionEffectHandler::SetParamiv(al::Context *context, DistortionProps&, ALenum param, const int*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid distortion integer-vector property {:#04x}", as_unsigned(param)); }

void DistortionEffectHandler::SetParamf(al::Context *context, DistortionProps &props, ALenum param, float val)
{
    switch(param)
    {
    case AL_DISTORTION_EDGE:
        if(!(val >= AL_DISTORTION_MIN_EDGE && val <= AL_DISTORTION_MAX_EDGE))
            context->throw_error(AL_INVALID_VALUE, "Distortion edge out of range");
        props.Edge = val;
        return;

    case AL_DISTORTION_GAIN:
        if(!(val >= AL_DISTORTION_MIN_GAIN && val <= AL_DISTORTION_MAX_GAIN))
            context->throw_error(AL_INVALID_VALUE, "Distortion gain out of range");
        props.Gain = val;
        return;

    case AL_DISTORTION_LOWPASS_CUTOFF:
        if(!(val >= AL_DISTORTION_MIN_LOWPASS_CUTOFF && val <= AL_DISTORTION_MAX_LOWPASS_CUTOFF))
            context->throw_error(AL_INVALID_VALUE, "Distortion low-pass cutoff out of range");
        props.LowpassCutoff = val;
        return;

    case AL_DISTORTION_EQCENTER:
        if(!(val >= AL_DISTORTION_MIN_EQCENTER && val <= AL_DISTORTION_MAX_EQCENTER))
            context->throw_error(AL_INVALID_VALUE, "Distortion EQ center out of range");
        props.EQCenter = val;
        return;

    case AL_DISTORTION_EQBANDWIDTH:
        if(!(val >= AL_DISTORTION_MIN_EQBANDWIDTH && val <= AL_DISTORTION_MAX_EQBANDWIDTH))
            context->throw_error(AL_INVALID_VALUE, "Distortion EQ bandwidth out of range");
        props.EQBandwidth = val;
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid distortion float property {:#04x}",
        as_unsigned(param));
}
void DistortionEffectHandler::SetParamfv(al::Context *context, DistortionProps &props, ALenum param, const float *vals)
{ SetParamf(context, props, param, *vals); }

void DistortionEffectHandler::GetParami(al::Context *context, const DistortionProps&, ALenum param, int*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid distortion integer property {:#04x}", as_unsigned(param)); }
void DistortionEffectHandler::GetParamiv(al::Context *context, const DistortionProps&, ALenum param, int*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid distortion integer-vector property {:#04x}", as_unsigned(param)); }

void DistortionEffectHandler::GetParamf(al::Context *context, const DistortionProps &props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_DISTORTION_EDGE: *val = props.Edge; return;
    case AL_DISTORTION_GAIN: *val = props.Gain; return;
    case AL_DISTORTION_LOWPASS_CUTOFF: *val = props.LowpassCutoff; return;
    case AL_DISTORTION_EQCENTER: *val = props.EQCenter; return;
    case AL_DISTORTION_EQBANDWIDTH: *val = props.EQBandwidth; return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid distortion float property {:#04x}",
        as_unsigned(param));
}
void DistortionEffectHandler::GetParamfv(al::Context *context, const DistortionProps &props, ALenum param, float *vals)
{ GetParamf(context, props, param, vals); }


#if ALSOFT_EAX
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

template<> /* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
struct DistortionCommitter::Exception : public EaxException {
    explicit Exception(const std::string_view message)
        : EaxException{"EAX_DISTORTION_EFFECT", message}
    { }
};

template<> [[noreturn]]
void DistortionCommitter::fail(const std::string_view message)
{ throw Exception{message}; }

auto EaxDistortionCommitter::commit(const EAXDISTORTIONPROPERTIES &props) const -> bool
{
    if(auto *cur = std::get_if<EAXDISTORTIONPROPERTIES>(&mEaxProps); cur && *cur == props)
        return false;

    mEaxProps = props;
    mAlProps = DistortionProps{
        .Edge = props.flEdge,
        .Gain = level_mb_to_gain(gsl::narrow_cast<float>(props.lGain)),
        .LowpassCutoff = props.flLowPassCutOff,
        .EQCenter = props.flEQCenter,
        .EQBandwidth = props.flEdge};

    return true;
}

void EaxDistortionCommitter::SetDefaults(EaxEffectProps &props)
{
    props = EAXDISTORTIONPROPERTIES{
        .flEdge = EAXDISTORTION_DEFAULTEDGE,
        .lGain = EAXDISTORTION_DEFAULTGAIN,
        .flLowPassCutOff = EAXDISTORTION_DEFAULTLOWPASSCUTOFF,
        .flEQCenter = EAXDISTORTION_DEFAULTEQCENTER,
        .flEQBandwidth = EAXDISTORTION_DEFAULTEQBANDWIDTH};
}

void EaxDistortionCommitter::Get(const EaxCall &call, const EAXDISTORTIONPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case EAXDISTORTION_NONE: break;
    case EAXDISTORTION_ALLPARAMETERS: call.store(props); break;
    case EAXDISTORTION_EDGE: call.store(props.flEdge); break;
    case EAXDISTORTION_GAIN: call.store(props.lGain); break;
    case EAXDISTORTION_LOWPASSCUTOFF: call.store(props.flLowPassCutOff); break;
    case EAXDISTORTION_EQCENTER: call.store(props.flEQCenter); break;
    case EAXDISTORTION_EQBANDWIDTH: call.store(props.flEQBandwidth); break;
    default: fail_unknown_property_id();
    }
}

void EaxDistortionCommitter::Set(const EaxCall &call, EAXDISTORTIONPROPERTIES &props)
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

#endif // ALSOFT_EAX
