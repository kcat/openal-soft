
#include "config.h"

#include <stdexcept>

#include "AL/al.h"
#include "AL/efx.h"

#include "alc/effects/base.h"
#include "aloptional.h"
#include "core/logging.h"
#include "effects.h"

#ifdef ALSOFT_EAX
#include <cassert>
#include "alnumeric.h"
#include "al/eax/exception.h"
#include "al/eax/utils.h"
#endif // ALSOFT_EAX


namespace {

static_assert(ChorusMaxDelay >= AL_CHORUS_MAX_DELAY, "Chorus max delay too small");
static_assert(FlangerMaxDelay >= AL_FLANGER_MAX_DELAY, "Flanger max delay too small");

static_assert(AL_CHORUS_WAVEFORM_SINUSOID == AL_FLANGER_WAVEFORM_SINUSOID, "Chorus/Flanger waveform value mismatch");
static_assert(AL_CHORUS_WAVEFORM_TRIANGLE == AL_FLANGER_WAVEFORM_TRIANGLE, "Chorus/Flanger waveform value mismatch");

inline al::optional<ChorusWaveform> WaveformFromEnum(ALenum type)
{
    switch(type)
    {
    case AL_CHORUS_WAVEFORM_SINUSOID: return al::make_optional(ChorusWaveform::Sinusoid);
    case AL_CHORUS_WAVEFORM_TRIANGLE: return al::make_optional(ChorusWaveform::Triangle);
    }
    return al::nullopt;
}
inline ALenum EnumFromWaveform(ChorusWaveform type)
{
    switch(type)
    {
    case ChorusWaveform::Sinusoid: return AL_CHORUS_WAVEFORM_SINUSOID;
    case ChorusWaveform::Triangle: return AL_CHORUS_WAVEFORM_TRIANGLE;
    }
    throw std::runtime_error{"Invalid chorus waveform: "+std::to_string(static_cast<int>(type))};
}

void Chorus_setParami(EffectProps *props, ALenum param, int val)
{
    switch(param)
    {
    case AL_CHORUS_WAVEFORM:
        if(auto formopt = WaveformFromEnum(val))
            props->Chorus.Waveform = *formopt;
        else
            throw effect_exception{AL_INVALID_VALUE, "Invalid chorus waveform: 0x%04x", val};
        break;

    case AL_CHORUS_PHASE:
        if(!(val >= AL_CHORUS_MIN_PHASE && val <= AL_CHORUS_MAX_PHASE))
            throw effect_exception{AL_INVALID_VALUE, "Chorus phase out of range: %d", val};
        props->Chorus.Phase = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid chorus integer property 0x%04x", param};
    }
}
void Chorus_setParamiv(EffectProps *props, ALenum param, const int *vals)
{ Chorus_setParami(props, param, vals[0]); }
void Chorus_setParamf(EffectProps *props, ALenum param, float val)
{
    switch(param)
    {
    case AL_CHORUS_RATE:
        if(!(val >= AL_CHORUS_MIN_RATE && val <= AL_CHORUS_MAX_RATE))
            throw effect_exception{AL_INVALID_VALUE, "Chorus rate out of range: %f", val};
        props->Chorus.Rate = val;
        break;

    case AL_CHORUS_DEPTH:
        if(!(val >= AL_CHORUS_MIN_DEPTH && val <= AL_CHORUS_MAX_DEPTH))
            throw effect_exception{AL_INVALID_VALUE, "Chorus depth out of range: %f", val};
        props->Chorus.Depth = val;
        break;

    case AL_CHORUS_FEEDBACK:
        if(!(val >= AL_CHORUS_MIN_FEEDBACK && val <= AL_CHORUS_MAX_FEEDBACK))
            throw effect_exception{AL_INVALID_VALUE, "Chorus feedback out of range: %f", val};
        props->Chorus.Feedback = val;
        break;

    case AL_CHORUS_DELAY:
        if(!(val >= AL_CHORUS_MIN_DELAY && val <= AL_CHORUS_MAX_DELAY))
            throw effect_exception{AL_INVALID_VALUE, "Chorus delay out of range: %f", val};
        props->Chorus.Delay = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid chorus float property 0x%04x", param};
    }
}
void Chorus_setParamfv(EffectProps *props, ALenum param, const float *vals)
{ Chorus_setParamf(props, param, vals[0]); }

void Chorus_getParami(const EffectProps *props, ALenum param, int *val)
{
    switch(param)
    {
    case AL_CHORUS_WAVEFORM:
        *val = EnumFromWaveform(props->Chorus.Waveform);
        break;

    case AL_CHORUS_PHASE:
        *val = props->Chorus.Phase;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid chorus integer property 0x%04x", param};
    }
}
void Chorus_getParamiv(const EffectProps *props, ALenum param, int *vals)
{ Chorus_getParami(props, param, vals); }
void Chorus_getParamf(const EffectProps *props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_CHORUS_RATE:
        *val = props->Chorus.Rate;
        break;

    case AL_CHORUS_DEPTH:
        *val = props->Chorus.Depth;
        break;

    case AL_CHORUS_FEEDBACK:
        *val = props->Chorus.Feedback;
        break;

    case AL_CHORUS_DELAY:
        *val = props->Chorus.Delay;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid chorus float property 0x%04x", param};
    }
}
void Chorus_getParamfv(const EffectProps *props, ALenum param, float *vals)
{ Chorus_getParamf(props, param, vals); }

const EffectProps genDefaultChorusProps() noexcept
{
    EffectProps props{};
    props.Chorus.Waveform = *WaveformFromEnum(AL_CHORUS_DEFAULT_WAVEFORM);
    props.Chorus.Phase = AL_CHORUS_DEFAULT_PHASE;
    props.Chorus.Rate = AL_CHORUS_DEFAULT_RATE;
    props.Chorus.Depth = AL_CHORUS_DEFAULT_DEPTH;
    props.Chorus.Feedback = AL_CHORUS_DEFAULT_FEEDBACK;
    props.Chorus.Delay = AL_CHORUS_DEFAULT_DELAY;
    return props;
}


void Flanger_setParami(EffectProps *props, ALenum param, int val)
{
    switch(param)
    {
    case AL_FLANGER_WAVEFORM:
        if(auto formopt = WaveformFromEnum(val))
            props->Chorus.Waveform = *formopt;
        else
            throw effect_exception{AL_INVALID_VALUE, "Invalid flanger waveform: 0x%04x", val};
        break;

    case AL_FLANGER_PHASE:
        if(!(val >= AL_FLANGER_MIN_PHASE && val <= AL_FLANGER_MAX_PHASE))
            throw effect_exception{AL_INVALID_VALUE, "Flanger phase out of range: %d", val};
        props->Chorus.Phase = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid flanger integer property 0x%04x", param};
    }
}
void Flanger_setParamiv(EffectProps *props, ALenum param, const int *vals)
{ Flanger_setParami(props, param, vals[0]); }
void Flanger_setParamf(EffectProps *props, ALenum param, float val)
{
    switch(param)
    {
    case AL_FLANGER_RATE:
        if(!(val >= AL_FLANGER_MIN_RATE && val <= AL_FLANGER_MAX_RATE))
            throw effect_exception{AL_INVALID_VALUE, "Flanger rate out of range: %f", val};
        props->Chorus.Rate = val;
        break;

    case AL_FLANGER_DEPTH:
        if(!(val >= AL_FLANGER_MIN_DEPTH && val <= AL_FLANGER_MAX_DEPTH))
            throw effect_exception{AL_INVALID_VALUE, "Flanger depth out of range: %f", val};
        props->Chorus.Depth = val;
        break;

    case AL_FLANGER_FEEDBACK:
        if(!(val >= AL_FLANGER_MIN_FEEDBACK && val <= AL_FLANGER_MAX_FEEDBACK))
            throw effect_exception{AL_INVALID_VALUE, "Flanger feedback out of range: %f", val};
        props->Chorus.Feedback = val;
        break;

    case AL_FLANGER_DELAY:
        if(!(val >= AL_FLANGER_MIN_DELAY && val <= AL_FLANGER_MAX_DELAY))
            throw effect_exception{AL_INVALID_VALUE, "Flanger delay out of range: %f", val};
        props->Chorus.Delay = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid flanger float property 0x%04x", param};
    }
}
void Flanger_setParamfv(EffectProps *props, ALenum param, const float *vals)
{ Flanger_setParamf(props, param, vals[0]); }

void Flanger_getParami(const EffectProps *props, ALenum param, int *val)
{
    switch(param)
    {
    case AL_FLANGER_WAVEFORM:
        *val = EnumFromWaveform(props->Chorus.Waveform);
        break;

    case AL_FLANGER_PHASE:
        *val = props->Chorus.Phase;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid flanger integer property 0x%04x", param};
    }
}
void Flanger_getParamiv(const EffectProps *props, ALenum param, int *vals)
{ Flanger_getParami(props, param, vals); }
void Flanger_getParamf(const EffectProps *props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_FLANGER_RATE:
        *val = props->Chorus.Rate;
        break;

    case AL_FLANGER_DEPTH:
        *val = props->Chorus.Depth;
        break;

    case AL_FLANGER_FEEDBACK:
        *val = props->Chorus.Feedback;
        break;

    case AL_FLANGER_DELAY:
        *val = props->Chorus.Delay;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid flanger float property 0x%04x", param};
    }
}
void Flanger_getParamfv(const EffectProps *props, ALenum param, float *vals)
{ Flanger_getParamf(props, param, vals); }

EffectProps genDefaultFlangerProps() noexcept
{
    EffectProps props{};
    props.Chorus.Waveform = *WaveformFromEnum(AL_FLANGER_DEFAULT_WAVEFORM);
    props.Chorus.Phase = AL_FLANGER_DEFAULT_PHASE;
    props.Chorus.Rate = AL_FLANGER_DEFAULT_RATE;
    props.Chorus.Depth = AL_FLANGER_DEFAULT_DEPTH;
    props.Chorus.Feedback = AL_FLANGER_DEFAULT_FEEDBACK;
    props.Chorus.Delay = AL_FLANGER_DEFAULT_DELAY;
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Chorus);

const EffectProps ChorusEffectProps{genDefaultChorusProps()};

DEFINE_ALEFFECT_VTABLE(Flanger);

const EffectProps FlangerEffectProps{genDefaultFlangerProps()};


#ifdef ALSOFT_EAX
namespace {

class EaxChorusEffectException : public EaxException {
public:
    explicit EaxChorusEffectException(const char* message)
        : EaxException{"EAX_CHORUS_EFFECT", message}
    {}
}; // EaxChorusEffectException

class EaxFlangerEffectException : public EaxException {
public:
    explicit EaxFlangerEffectException(const char* message)
        : EaxException{"EAX_FLANGER_EFFECT", message}
    {}
}; // EaxFlangerEffectException

struct EaxChorusTraits
{
    using Exception = EaxChorusEffectException;
    using Props = EAXCHORUSPROPERTIES;

    static constexpr auto efx_effect() { return AL_EFFECT_CHORUS; }

    static constexpr auto eax_none_param_id() { return EAXCHORUS_NONE; }
    static constexpr auto eax_allparameters_param_id() { return EAXCHORUS_ALLPARAMETERS; }
    static constexpr auto eax_waveform_param_id() { return EAXCHORUS_WAVEFORM; }
    static constexpr auto eax_phase_param_id() { return EAXCHORUS_PHASE; }
    static constexpr auto eax_rate_param_id() { return EAXCHORUS_RATE; }
    static constexpr auto eax_depth_param_id() { return EAXCHORUS_DEPTH; }
    static constexpr auto eax_feedback_param_id() { return EAXCHORUS_FEEDBACK; }
    static constexpr auto eax_delay_param_id() { return EAXCHORUS_DELAY; }

    static constexpr auto eax_min_waveform() { return EAXCHORUS_MINWAVEFORM; }
    static constexpr auto eax_min_phase() { return EAXCHORUS_MINPHASE; }
    static constexpr auto eax_min_rate() { return EAXCHORUS_MINRATE; }
    static constexpr auto eax_min_depth() { return EAXCHORUS_MINDEPTH; }
    static constexpr auto eax_min_feedback() { return EAXCHORUS_MINFEEDBACK; }
    static constexpr auto eax_min_delay() { return EAXCHORUS_MINDELAY; }

    static constexpr auto eax_max_waveform() { return EAXCHORUS_MAXWAVEFORM; }
    static constexpr auto eax_max_phase() { return EAXCHORUS_MAXPHASE; }
    static constexpr auto eax_max_rate() { return EAXCHORUS_MAXRATE; }
    static constexpr auto eax_max_depth() { return EAXCHORUS_MAXDEPTH; }
    static constexpr auto eax_max_feedback() { return EAXCHORUS_MAXFEEDBACK; }
    static constexpr auto eax_max_delay() { return EAXCHORUS_MAXDELAY; }

    static constexpr auto eax_default_waveform() { return EAXCHORUS_DEFAULTWAVEFORM; }
    static constexpr auto eax_default_phase() { return EAXCHORUS_DEFAULTPHASE; }
    static constexpr auto eax_default_rate() { return EAXCHORUS_DEFAULTRATE; }
    static constexpr auto eax_default_depth() { return EAXCHORUS_DEFAULTDEPTH; }
    static constexpr auto eax_default_feedback() { return EAXCHORUS_DEFAULTFEEDBACK; }
    static constexpr auto eax_default_delay() { return EAXCHORUS_DEFAULTDELAY; }

    static constexpr auto efx_min_waveform() { return AL_CHORUS_MIN_WAVEFORM; }
    static constexpr auto efx_min_phase() { return AL_CHORUS_MIN_PHASE; }
    static constexpr auto efx_min_rate() { return AL_CHORUS_MIN_RATE; }
    static constexpr auto efx_min_depth() { return AL_CHORUS_MIN_DEPTH; }
    static constexpr auto efx_min_feedback() { return AL_CHORUS_MIN_FEEDBACK; }
    static constexpr auto efx_min_delay() { return AL_CHORUS_MIN_DELAY; }

    static constexpr auto efx_max_waveform() { return AL_CHORUS_MAX_WAVEFORM; }
    static constexpr auto efx_max_phase() { return AL_CHORUS_MAX_PHASE; }
    static constexpr auto efx_max_rate() { return AL_CHORUS_MAX_RATE; }
    static constexpr auto efx_max_depth() { return AL_CHORUS_MAX_DEPTH; }
    static constexpr auto efx_max_feedback() { return AL_CHORUS_MAX_FEEDBACK; }
    static constexpr auto efx_max_delay() { return AL_CHORUS_MAX_DELAY; }

    static constexpr auto efx_default_waveform() { return AL_CHORUS_DEFAULT_WAVEFORM; }
    static constexpr auto efx_default_phase() { return AL_CHORUS_DEFAULT_PHASE; }
    static constexpr auto efx_default_rate() { return AL_CHORUS_DEFAULT_RATE; }
    static constexpr auto efx_default_depth() { return AL_CHORUS_DEFAULT_DEPTH; }
    static constexpr auto efx_default_feedback() { return AL_CHORUS_DEFAULT_FEEDBACK; }
    static constexpr auto efx_default_delay() { return AL_CHORUS_DEFAULT_DELAY; }
}; // EaxChorusTraits

struct EaxFlangerTraits
{
    using Exception = EaxFlangerEffectException;
    using Props = EAXFLANGERPROPERTIES;

    static constexpr auto efx_effect() { return AL_EFFECT_FLANGER; }

    static constexpr auto eax_none_param_id() { return EAXFLANGER_NONE; }
    static constexpr auto eax_allparameters_param_id() { return EAXFLANGER_ALLPARAMETERS; }
    static constexpr auto eax_waveform_param_id() { return EAXFLANGER_WAVEFORM; }
    static constexpr auto eax_phase_param_id() { return EAXFLANGER_PHASE; }
    static constexpr auto eax_rate_param_id() { return EAXFLANGER_RATE; }
    static constexpr auto eax_depth_param_id() { return EAXFLANGER_DEPTH; }
    static constexpr auto eax_feedback_param_id() { return EAXFLANGER_FEEDBACK; }
    static constexpr auto eax_delay_param_id() { return EAXFLANGER_DELAY; }

    static constexpr auto eax_min_waveform() { return EAXFLANGER_MINWAVEFORM; }
    static constexpr auto eax_min_phase() { return EAXFLANGER_MINPHASE; }
    static constexpr auto eax_min_rate() { return EAXFLANGER_MINRATE; }
    static constexpr auto eax_min_depth() { return EAXFLANGER_MINDEPTH; }
    static constexpr auto eax_min_feedback() { return EAXFLANGER_MINFEEDBACK; }
    static constexpr auto eax_min_delay() { return EAXFLANGER_MINDELAY; }

    static constexpr auto eax_max_waveform() { return EAXFLANGER_MAXWAVEFORM; }
    static constexpr auto eax_max_phase() { return EAXFLANGER_MAXPHASE; }
    static constexpr auto eax_max_rate() { return EAXFLANGER_MAXRATE; }
    static constexpr auto eax_max_depth() { return EAXFLANGER_MAXDEPTH; }
    static constexpr auto eax_max_feedback() { return EAXFLANGER_MAXFEEDBACK; }
    static constexpr auto eax_max_delay() { return EAXFLANGER_MAXDELAY; }

    static constexpr auto eax_default_waveform() { return EAXFLANGER_DEFAULTWAVEFORM; }
    static constexpr auto eax_default_phase() { return EAXFLANGER_DEFAULTPHASE; }
    static constexpr auto eax_default_rate() { return EAXFLANGER_DEFAULTRATE; }
    static constexpr auto eax_default_depth() { return EAXFLANGER_DEFAULTDEPTH; }
    static constexpr auto eax_default_feedback() { return EAXFLANGER_DEFAULTFEEDBACK; }
    static constexpr auto eax_default_delay() { return EAXFLANGER_DEFAULTDELAY; }

    static constexpr auto efx_min_waveform() { return AL_FLANGER_MIN_WAVEFORM; }
    static constexpr auto efx_min_phase() { return AL_FLANGER_MIN_PHASE; }
    static constexpr auto efx_min_rate() { return AL_FLANGER_MIN_RATE; }
    static constexpr auto efx_min_depth() { return AL_FLANGER_MIN_DEPTH; }
    static constexpr auto efx_min_feedback() { return AL_FLANGER_MIN_FEEDBACK; }
    static constexpr auto efx_min_delay() { return AL_FLANGER_MIN_DELAY; }

    static constexpr auto efx_max_waveform() { return AL_FLANGER_MAX_WAVEFORM; }
    static constexpr auto efx_max_phase() { return AL_FLANGER_MAX_PHASE; }
    static constexpr auto efx_max_rate() { return AL_FLANGER_MAX_RATE; }
    static constexpr auto efx_max_depth() { return AL_FLANGER_MAX_DEPTH; }
    static constexpr auto efx_max_feedback() { return AL_FLANGER_MAX_FEEDBACK; }
    static constexpr auto efx_max_delay() { return AL_FLANGER_MAX_DELAY; }

    static constexpr auto efx_default_waveform() { return AL_FLANGER_DEFAULT_WAVEFORM; }
    static constexpr auto efx_default_phase() { return AL_FLANGER_DEFAULT_PHASE; }
    static constexpr auto efx_default_rate() { return AL_FLANGER_DEFAULT_RATE; }
    static constexpr auto efx_default_depth() { return AL_FLANGER_DEFAULT_DEPTH; }
    static constexpr auto efx_default_feedback() { return AL_FLANGER_DEFAULT_FEEDBACK; }
    static constexpr auto efx_default_delay() { return AL_FLANGER_DEFAULT_DELAY; }
}; // EaxFlangerTraits

template<typename TTraits>
class EaxChorusFlangerEffect final : public EaxEffect4<typename TTraits::Exception, typename TTraits::Props> {
public:
    using Traits = TTraits;
    using Base = EaxEffect4<typename Traits::Exception, typename Traits::Props>;
    using typename Base::Exception;
    using typename Base::Props;
    using typename Base::State;
    using Base::defer;

    EaxChorusFlangerEffect(const EaxCall& call)
        : Base{Traits::efx_effect(), call}
    {}

private:
    struct WaveformValidator {
        void operator()(unsigned long ulWaveform) const
        {
            eax_validate_range<Exception>(
                "Waveform",
                ulWaveform,
                Traits::eax_min_waveform(),
                Traits::eax_max_waveform());
        }
    }; // WaveformValidator

    struct PhaseValidator {
        void operator()(long lPhase) const
        {
            eax_validate_range<Exception>(
                "Phase",
                lPhase,
                Traits::eax_min_phase(),
                Traits::eax_max_phase());
        }
    }; // PhaseValidator

    struct RateValidator {
        void operator()(float flRate) const
        {
            eax_validate_range<Exception>(
                "Rate",
                flRate,
                Traits::eax_min_rate(),
                Traits::eax_max_rate());
        }
    }; // RateValidator

    struct DepthValidator {
        void operator()(float flDepth) const
        {
            eax_validate_range<Exception>(
                "Depth",
                flDepth,
                Traits::eax_min_depth(),
                Traits::eax_max_depth());
        }
    }; // DepthValidator

    struct FeedbackValidator {
        void operator()(float flFeedback) const
        {
            eax_validate_range<Exception>(
                "Feedback",
                flFeedback,
                Traits::eax_min_feedback(),
                Traits::eax_max_feedback());
        }
    }; // FeedbackValidator

    struct DelayValidator {
        void operator()(float flDelay) const
        {
            eax_validate_range<Exception>(
                "Delay",
                flDelay,
                Traits::eax_min_delay(),
                Traits::eax_max_delay());
        }
    }; // DelayValidator

    struct AllValidator {
        void operator()(const Props& all) const
        {
            WaveformValidator{}(all.ulWaveform);
            PhaseValidator{}(all.lPhase);
            RateValidator{}(all.flRate);
            DepthValidator{}(all.flDepth);
            FeedbackValidator{}(all.flFeedback);
            DelayValidator{}(all.flDelay);
        }
    }; // AllValidator

    void set_defaults(Props& props) override
    {
        props.ulWaveform = Traits::eax_default_waveform();
        props.lPhase = Traits::eax_default_phase();
        props.flRate = Traits::eax_default_rate();
        props.flDepth = Traits::eax_default_depth();
        props.flFeedback = Traits::eax_default_feedback();
        props.flDelay = Traits::eax_default_delay();
    }

    void set_efx_waveform()
    {
        const auto waveform = clamp(
            static_cast<ALint>(Base::props_.ulWaveform),
            Traits::efx_min_waveform(),
            Traits::efx_max_waveform());
        const auto efx_waveform = WaveformFromEnum(waveform);
        assert(efx_waveform.has_value());
        Base::al_effect_props_.Chorus.Waveform = *efx_waveform;
    }

    void set_efx_phase() noexcept
    {
        Base::al_effect_props_.Chorus.Phase = clamp(
            static_cast<ALint>(Base::props_.lPhase),
            Traits::efx_min_phase(),
            Traits::efx_max_phase());
    }

    void set_efx_rate() noexcept
    {
        Base::al_effect_props_.Chorus.Rate = clamp(
            Base::props_.flRate,
            Traits::efx_min_rate(),
            Traits::efx_max_rate());
    }

    void set_efx_depth() noexcept
    {
        Base::al_effect_props_.Chorus.Depth = clamp(
            Base::props_.flDepth,
            Traits::efx_min_depth(),
            Traits::efx_max_depth());
    }

    void set_efx_feedback() noexcept
    {
        Base::al_effect_props_.Chorus.Feedback = clamp(
            Base::props_.flFeedback,
            Traits::efx_min_feedback(),
            Traits::efx_max_feedback());
    }

    void set_efx_delay() noexcept
    {
        Base::al_effect_props_.Chorus.Delay = clamp(
            Base::props_.flDelay,
            Traits::efx_min_delay(),
            Traits::efx_max_delay());
    }

    void set_efx_defaults() override
    {
        set_efx_waveform();
        set_efx_phase();
        set_efx_rate();
        set_efx_depth();
        set_efx_feedback();
        set_efx_delay();
    }

    void get(const EaxCall& call, const Props& props) override
    {
        switch(call.get_property_id())
        {
            case Traits::eax_none_param_id():
                break;

            case Traits::eax_allparameters_param_id():
                call.template set_value<Exception>(props);
                break;

            case Traits::eax_waveform_param_id():
                call.template set_value<Exception>(props.ulWaveform);
                break;

            case Traits::eax_phase_param_id():
                call.template set_value<Exception>(props.lPhase);
                break;

            case Traits::eax_rate_param_id():
                call.template set_value<Exception>(props.flRate);
                break;

            case Traits::eax_depth_param_id():
                call.template set_value<Exception>(props.flDepth);
                break;

            case Traits::eax_feedback_param_id():
                call.template set_value<Exception>(props.flFeedback);
                break;

            case Traits::eax_delay_param_id():
                call.template set_value<Exception>(props.flDelay);
                break;

            default:
                Base::fail_unknown_property_id();
        }
    }

    void set(const EaxCall& call, Props& props) override
    {
        switch(call.get_property_id())
        {
            case Traits::eax_none_param_id():
                break;

            case Traits::eax_allparameters_param_id():
                Base::template defer<AllValidator>(call, props);
                break;

            case Traits::eax_waveform_param_id():
                Base::template defer<WaveformValidator>(call, props.ulWaveform);
                break;

            case Traits::eax_phase_param_id():
                Base::template defer<PhaseValidator>(call, props.lPhase);
                break;

            case Traits::eax_rate_param_id():
                Base::template defer<RateValidator>(call, props.flRate);
                break;

            case Traits::eax_depth_param_id():
                Base::template defer<DepthValidator>(call, props.flDepth);
                break;

            case Traits::eax_feedback_param_id():
                Base::template defer<FeedbackValidator>(call, props.flFeedback);
                break;

            case Traits::eax_delay_param_id():
                Base::template defer<DelayValidator>(call, props.flDelay);
                break;

            default:
                Base::fail_unknown_property_id();
        }
    }

    bool commit_props(const Props& props) override
    {
        auto is_dirty = false;

        if (Base::props_.ulWaveform != props.ulWaveform)
        {
            is_dirty = true;
            set_efx_waveform();
        }

        if (Base::props_.lPhase != props.lPhase)
        {
            is_dirty = true;
            set_efx_phase();
        }

        if (Base::props_.flRate != props.flRate)
        {
            is_dirty = true;
            set_efx_rate();
        }

        if (Base::props_.flDepth != props.flDepth)
        {
            is_dirty = true;
            set_efx_depth();
        }

        if (Base::props_.flFeedback != props.flFeedback)
        {
            is_dirty = true;
            set_efx_feedback();
        }

        if (Base::props_.flDelay != props.flDelay)
        {
            is_dirty = true;
            set_efx_delay();
        }

        return is_dirty;
    }
}; // EaxChorusFlangerEffect

template<typename TTraits>
EaxEffectUPtr eax_create_eax_chorus_flanger_effect(const EaxCall& call)
{
    return eax_create_eax4_effect<EaxChorusFlangerEffect<TTraits>>(call);
}

} // namespace

// ==========================================================================

EaxEffectUPtr eax_create_eax_chorus_effect(const EaxCall& call)
{
    return eax_create_eax_chorus_flanger_effect<EaxChorusTraits>(call);
}

EaxEffectUPtr eax_create_eax_flanger_effect(const EaxCall& call)
{
    return eax_create_eax_chorus_flanger_effect<EaxFlangerTraits>(call);
}

#endif // ALSOFT_EAX
