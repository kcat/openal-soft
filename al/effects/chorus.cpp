
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
    case AL_CHORUS_WAVEFORM_SINUSOID: return ChorusWaveform::Sinusoid;
    case AL_CHORUS_WAVEFORM_TRIANGLE: return ChorusWaveform::Triangle;
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

EffectProps genDefaultChorusProps() noexcept
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

struct EaxChorusTraits {
    using Props = EAXCHORUSPROPERTIES;
    using Committer = EaxChorusCommitter;
    static constexpr auto Field = &EaxEffectProps::mChorus;

    static constexpr auto eax_effect_type() { return EaxEffectType::Chorus; }
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

    static ChorusWaveform eax_waveform(unsigned long type)
    {
        if(type == EAX_CHORUS_SINUSOID) return ChorusWaveform::Sinusoid;
        if(type == EAX_CHORUS_TRIANGLE) return ChorusWaveform::Triangle;
        return ChorusWaveform::Sinusoid;
    }
}; // EaxChorusTraits

struct EaxFlangerTraits {
    using Props = EAXFLANGERPROPERTIES;
    using Committer = EaxFlangerCommitter;
    static constexpr auto Field = &EaxEffectProps::mFlanger;

    static constexpr auto eax_effect_type() { return EaxEffectType::Flanger; }
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

    static ChorusWaveform eax_waveform(unsigned long type)
    {
        if(type == EAX_FLANGER_SINUSOID) return ChorusWaveform::Sinusoid;
        if(type == EAX_FLANGER_TRIANGLE) return ChorusWaveform::Triangle;
        return ChorusWaveform::Sinusoid;
    }
}; // EaxFlangerTraits

template<typename TTraits>
struct ChorusFlangerEffect {
    using Traits = TTraits;
    using Committer = typename Traits::Committer;
    using Exception = typename Committer::Exception;

    static constexpr auto Field = Traits::Field;

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
        void operator()(const typename Traits::Props& all) const
        {
            WaveformValidator{}(all.ulWaveform);
            PhaseValidator{}(all.lPhase);
            RateValidator{}(all.flRate);
            DepthValidator{}(all.flDepth);
            FeedbackValidator{}(all.flFeedback);
            DelayValidator{}(all.flDelay);
        }
    }; // AllValidator

public:
    static void SetDefaults(EaxEffectProps &props)
    {
        auto&& all = props.*Field;
        props.mType = Traits::eax_effect_type();
        all.ulWaveform = Traits::eax_default_waveform();
        all.lPhase = Traits::eax_default_phase();
        all.flRate = Traits::eax_default_rate();
        all.flDepth = Traits::eax_default_depth();
        all.flFeedback = Traits::eax_default_feedback();
        all.flDelay = Traits::eax_default_delay();
    }


    static void Get(const EaxCall &call, const EaxEffectProps &props)
    {
        auto&& all = props.*Field;
        switch(call.get_property_id())
        {
        case Traits::eax_none_param_id():
            break;

        case Traits::eax_allparameters_param_id():
            call.template set_value<Exception>(all);
            break;

        case Traits::eax_waveform_param_id():
            call.template set_value<Exception>(all.ulWaveform);
            break;

        case Traits::eax_phase_param_id():
            call.template set_value<Exception>(all.lPhase);
            break;

        case Traits::eax_rate_param_id():
            call.template set_value<Exception>(all.flRate);
            break;

        case Traits::eax_depth_param_id():
            call.template set_value<Exception>(all.flDepth);
            break;

        case Traits::eax_feedback_param_id():
            call.template set_value<Exception>(all.flFeedback);
            break;

        case Traits::eax_delay_param_id():
            call.template set_value<Exception>(all.flDelay);
            break;

        default:
            Committer::fail_unknown_property_id();
        }
    }

    static void Set(const EaxCall &call, EaxEffectProps &props)
    {
        auto&& all = props.*Field;
        switch(call.get_property_id())
        {
        case Traits::eax_none_param_id():
            break;

        case Traits::eax_allparameters_param_id():
            Committer::template defer<AllValidator>(call, all);
            break;

        case Traits::eax_waveform_param_id():
            Committer::template defer<WaveformValidator>(call, all.ulWaveform);
            break;

        case Traits::eax_phase_param_id():
            Committer::template defer<PhaseValidator>(call, all.lPhase);
            break;

        case Traits::eax_rate_param_id():
            Committer::template defer<RateValidator>(call, all.flRate);
            break;

        case Traits::eax_depth_param_id():
            Committer::template defer<DepthValidator>(call, all.flDepth);
            break;

        case Traits::eax_feedback_param_id():
            Committer::template defer<FeedbackValidator>(call, all.flFeedback);
            break;

        case Traits::eax_delay_param_id():
            Committer::template defer<DelayValidator>(call, all.flDelay);
            break;

        default:
            Committer::fail_unknown_property_id();
        }
    }

    static bool Commit(const EaxEffectProps &props, EaxEffectProps &props_, EffectProps &al_props_)
    {
        if(props.mType == props_.mType)
        {
            auto&& src = props_.*Field;
            auto&& dst = props.*Field;
            if(dst.ulWaveform == src.ulWaveform && dst.lPhase == src.lPhase
                && dst.flRate == src.flRate && dst.flDepth == src.flDepth
                && dst.flFeedback == src.flFeedback && dst.flDelay == src.flDelay)
                return false;
        }

        props_ = props;
        auto&& dst = props.*Field;

        al_props_.Chorus.Waveform = Traits::eax_waveform(dst.ulWaveform);
        al_props_.Chorus.Phase = static_cast<int>(dst.lPhase);
        al_props_.Chorus.Rate = dst.flRate;
        al_props_.Chorus.Depth = dst.flDepth;
        al_props_.Chorus.Feedback = dst.flFeedback;
        al_props_.Chorus.Delay = dst.flDelay;

        return true;
    }
}; // EaxChorusFlangerEffect


using ChorusCommitter = EaxCommitter<EaxChorusCommitter>;
using FlangerCommitter = EaxCommitter<EaxFlangerCommitter>;

} // namespace

template<>
struct ChorusCommitter::Exception : public EaxException
{
    explicit Exception(const char *message) : EaxException{"EAX_CHORUS_EFFECT", message}
    { }
};

template<>
[[noreturn]] void ChorusCommitter::fail(const char *message)
{
    throw Exception{message};
}

template<>
bool ChorusCommitter::commit(const EaxEffectProps &props)
{
    using Committer = ChorusFlangerEffect<EaxChorusTraits>;
    return Committer::Commit(props, mEaxProps, mAlProps);
}

template<>
void ChorusCommitter::SetDefaults(EaxEffectProps &props)
{
    using Committer = ChorusFlangerEffect<EaxChorusTraits>;
    Committer::SetDefaults(props);
}

template<>
void ChorusCommitter::Get(const EaxCall &call, const EaxEffectProps &props)
{
    using Committer = ChorusFlangerEffect<EaxChorusTraits>;
    Committer::Get(call, props);
}

template<>
void ChorusCommitter::Set(const EaxCall &call, EaxEffectProps &props)
{
    using Committer = ChorusFlangerEffect<EaxChorusTraits>;
    Committer::Set(call, props);
}

template<>
struct FlangerCommitter::Exception : public EaxException
{
    explicit Exception(const char *message) : EaxException{"EAX_FLANGER_EFFECT", message}
    { }
};

template<>
[[noreturn]] void FlangerCommitter::fail(const char *message)
{
    throw Exception{message};
}

template<>
bool FlangerCommitter::commit(const EaxEffectProps &props)
{
    using Committer = ChorusFlangerEffect<EaxFlangerTraits>;
    return Committer::Commit(props, mEaxProps, mAlProps);
}

template<>
void FlangerCommitter::SetDefaults(EaxEffectProps &props)
{
    using Committer = ChorusFlangerEffect<EaxFlangerTraits>;
    Committer::SetDefaults(props);
}

template<>
void FlangerCommitter::Get(const EaxCall &call, const EaxEffectProps &props)
{
    using Committer = ChorusFlangerEffect<EaxFlangerTraits>;
    Committer::Get(call, props);
}

template<>
void FlangerCommitter::Set(const EaxCall &call, EaxEffectProps &props)
{
    using Committer = ChorusFlangerEffect<EaxFlangerTraits>;
    Committer::Set(call, props);
}

#endif // ALSOFT_EAX
