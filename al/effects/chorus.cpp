
#include "config.h"

#include <format>
#include <optional>
#include <stdexcept>

#include "AL/al.h"
#include "AL/efx.h"

#include "alc/context.h"
#include "alnumeric.h"
#include "core/logging.h"
#include "effects.h"
#include "gsl/gsl"

#if ALSOFT_EAX
#include "al/eax/effect.h"
#include "al/eax/exception.h"
#include "al/eax/utils.h"
#endif // ALSOFT_EAX


namespace {

static_assert(ChorusMaxDelay >= AL_CHORUS_MAX_DELAY, "Chorus max delay too small");
static_assert(FlangerMaxDelay >= AL_FLANGER_MAX_DELAY, "Flanger max delay too small");

static_assert(AL_CHORUS_WAVEFORM_SINUSOID == AL_FLANGER_WAVEFORM_SINUSOID, "Chorus/Flanger waveform value mismatch");
static_assert(AL_CHORUS_WAVEFORM_TRIANGLE == AL_FLANGER_WAVEFORM_TRIANGLE, "Chorus/Flanger waveform value mismatch");

constexpr std::optional<ChorusWaveform> WaveformFromEnum(ALenum type) noexcept
{
    switch(type)
    {
    case AL_CHORUS_WAVEFORM_SINUSOID: return ChorusWaveform::Sinusoid;
    case AL_CHORUS_WAVEFORM_TRIANGLE: return ChorusWaveform::Triangle;
    }
    return std::nullopt;
}
constexpr ALenum EnumFromWaveform(ChorusWaveform type)
{
    switch(type)
    {
    case ChorusWaveform::Sinusoid: return AL_CHORUS_WAVEFORM_SINUSOID;
    case ChorusWaveform::Triangle: return AL_CHORUS_WAVEFORM_TRIANGLE;
    }
    throw std::runtime_error{std::format("Invalid chorus waveform: {}",
        int{al::to_underlying(type)})};
}

consteval auto genDefaultChorusProps() noexcept -> EffectProps
{
    return ChorusProps{
        /* NOLINTNEXTLINE(bugprone-unchecked-optional-access) */
        .Waveform = WaveformFromEnum(AL_CHORUS_DEFAULT_WAVEFORM).value(),
        .Phase = AL_CHORUS_DEFAULT_PHASE,
        .Rate = AL_CHORUS_DEFAULT_RATE,
        .Depth = AL_CHORUS_DEFAULT_DEPTH,
        .Feedback = AL_CHORUS_DEFAULT_FEEDBACK,
        .Delay = AL_CHORUS_DEFAULT_DELAY};
}

consteval auto genDefaultFlangerProps() noexcept -> EffectProps
{
    return ChorusProps{
        /* NOLINTNEXTLINE(bugprone-unchecked-optional-access) */
        .Waveform = WaveformFromEnum(AL_FLANGER_DEFAULT_WAVEFORM).value(),
        .Phase = AL_FLANGER_DEFAULT_PHASE,
        .Rate = AL_FLANGER_DEFAULT_RATE,
        .Depth = AL_FLANGER_DEFAULT_DEPTH,
        .Feedback = AL_FLANGER_DEFAULT_FEEDBACK,
        .Delay = AL_FLANGER_DEFAULT_DELAY};
}

} // namespace

constinit const EffectProps ChorusEffectProps(genDefaultChorusProps());

void ChorusEffectHandler::SetParami(al::Context *context, ChorusProps &props, ALenum param, int val)
{
    switch(param)
    {
    case AL_CHORUS_WAVEFORM:
        if(auto formopt = WaveformFromEnum(val))
            props.Waveform = *formopt;
        else
            context->throw_error(AL_INVALID_VALUE, "Invalid chorus waveform: {:#04x}",
                as_unsigned(val));
        return;

    case AL_CHORUS_PHASE:
        if(!(val >= AL_CHORUS_MIN_PHASE && val <= AL_CHORUS_MAX_PHASE))
            context->throw_error(AL_INVALID_VALUE, "Chorus phase out of range: {}", val);
        props.Phase = val;
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid chorus integer property {:#04x}",
        as_unsigned(param));
}
void ChorusEffectHandler::SetParamiv(al::Context *context, ChorusProps &props, ALenum param, const int *vals)
{ SetParami(context, props, param, *vals); }
void ChorusEffectHandler::SetParamf(al::Context *context, ChorusProps &props, ALenum param, float val)
{
    switch(param)
    {
    case AL_CHORUS_RATE:
        if(!(val >= AL_CHORUS_MIN_RATE && val <= AL_CHORUS_MAX_RATE))
            context->throw_error(AL_INVALID_VALUE, "Chorus rate out of range: {}", val);
        props.Rate = val;
        return;

    case AL_CHORUS_DEPTH:
        if(!(val >= AL_CHORUS_MIN_DEPTH && val <= AL_CHORUS_MAX_DEPTH))
            context->throw_error(AL_INVALID_VALUE, "Chorus depth out of range: {}", val);
        props.Depth = val;
        return;

    case AL_CHORUS_FEEDBACK:
        if(!(val >= AL_CHORUS_MIN_FEEDBACK && val <= AL_CHORUS_MAX_FEEDBACK))
            context->throw_error(AL_INVALID_VALUE, "Chorus feedback out of range: {}", val);
        props.Feedback = val;
        return;

    case AL_CHORUS_DELAY:
        if(!(val >= AL_CHORUS_MIN_DELAY && val <= AL_CHORUS_MAX_DELAY))
            context->throw_error(AL_INVALID_VALUE, "Chorus delay out of range: {}", val);
        props.Delay = val;
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid chorus float property {:#04x}",
        as_unsigned(param));
}
void ChorusEffectHandler::SetParamfv(al::Context *context, ChorusProps &props, ALenum param, const float *vals)
{ SetParamf(context, props, param, *vals); }

void ChorusEffectHandler::GetParami(al::Context *context, const ChorusProps &props, ALenum param, int *val)
{
    switch(param)
    {
    case AL_CHORUS_WAVEFORM: *val = EnumFromWaveform(props.Waveform); return;
    case AL_CHORUS_PHASE: *val = props.Phase; return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid chorus integer property {:#04x}",
        as_unsigned(param));
}
void ChorusEffectHandler::GetParamiv(al::Context *context, const ChorusProps &props, ALenum param, int *vals)
{ GetParami(context, props, param, vals); }
void ChorusEffectHandler::GetParamf(al::Context *context, const ChorusProps &props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_CHORUS_RATE: *val = props.Rate; return;
    case AL_CHORUS_DEPTH: *val = props.Depth; return;
    case AL_CHORUS_FEEDBACK: *val = props.Feedback; return;
    case AL_CHORUS_DELAY: *val = props.Delay; return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid chorus float property {:#04x}",
        as_unsigned(param));
}
void ChorusEffectHandler::GetParamfv(al::Context *context, const ChorusProps &props, ALenum param, float *vals)
{ GetParamf(context, props, param, vals); }


constinit const EffectProps FlangerEffectProps(genDefaultFlangerProps());

void FlangerEffectHandler::SetParami(al::Context *context, ChorusProps &props, ALenum param, int val)
{
    switch(param)
    {
    case AL_FLANGER_WAVEFORM:
        if(auto formopt = WaveformFromEnum(val))
            props.Waveform = *formopt;
        else
            context->throw_error(AL_INVALID_VALUE, "Invalid flanger waveform: {:#04x}",
                as_unsigned(val));
        return;

    case AL_FLANGER_PHASE:
        if(!(val >= AL_FLANGER_MIN_PHASE && val <= AL_FLANGER_MAX_PHASE))
            context->throw_error(AL_INVALID_VALUE, "Flanger phase out of range: {}", val);
        props.Phase = val;
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid flanger integer property {:#04x}",
        as_unsigned(param));
}
void FlangerEffectHandler::SetParamiv(al::Context *context, ChorusProps &props, ALenum param, const int *vals)
{ SetParami(context, props, param, *vals); }
void FlangerEffectHandler::SetParamf(al::Context *context, ChorusProps &props, ALenum param, float val)
{
    switch(param)
    {
    case AL_FLANGER_RATE:
        if(!(val >= AL_FLANGER_MIN_RATE && val <= AL_FLANGER_MAX_RATE))
            context->throw_error(AL_INVALID_VALUE, "Flanger rate out of range: {}", val);
        props.Rate = val;
        return;

    case AL_FLANGER_DEPTH:
        if(!(val >= AL_FLANGER_MIN_DEPTH && val <= AL_FLANGER_MAX_DEPTH))
            context->throw_error(AL_INVALID_VALUE, "Flanger depth out of range: {}", val);
        props.Depth = val;
        return;

    case AL_FLANGER_FEEDBACK:
        if(!(val >= AL_FLANGER_MIN_FEEDBACK && val <= AL_FLANGER_MAX_FEEDBACK))
            context->throw_error(AL_INVALID_VALUE, "Flanger feedback out of range: {}", val);
        props.Feedback = val;
        return;

    case AL_FLANGER_DELAY:
        if(!(val >= AL_FLANGER_MIN_DELAY && val <= AL_FLANGER_MAX_DELAY))
            context->throw_error(AL_INVALID_VALUE, "Flanger delay out of range: {}", val);
        props.Delay = val;
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid flanger float property {:#04x}",
        as_unsigned(param));
}
void FlangerEffectHandler::SetParamfv(al::Context *context, ChorusProps &props, ALenum param, const float *vals)
{ SetParamf(context, props, param, *vals); }

void FlangerEffectHandler::GetParami(al::Context *context, const ChorusProps &props, ALenum param, int *val)
{
    switch(param)
    {
    case AL_FLANGER_WAVEFORM: *val = EnumFromWaveform(props.Waveform); return;
    case AL_FLANGER_PHASE: *val = props.Phase; return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid flanger integer property {:#04x}",
        as_unsigned(param));
}
void FlangerEffectHandler::GetParamiv(al::Context *context, const ChorusProps &props, ALenum param, int *vals)
{ GetParami(context, props, param, vals); }
void FlangerEffectHandler::GetParamf(al::Context *context, const ChorusProps &props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_FLANGER_RATE: *val = props.Rate; return;
    case AL_FLANGER_DEPTH: *val = props.Depth; return;
    case AL_FLANGER_FEEDBACK: *val = props.Feedback; return;
    case AL_FLANGER_DELAY: *val = props.Delay; return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid flanger float property {:#04x}",
        as_unsigned(param));
}
void FlangerEffectHandler::GetParamfv(al::Context *context, const ChorusProps &props, ALenum param, float *vals)
{ GetParamf(context, props, param, vals); }


#if ALSOFT_EAX
namespace {

struct EaxChorusTraits {
    using EaxProps = EAXCHORUSPROPERTIES;
    using Committer = EaxChorusCommitter;

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

    static constexpr auto eax_waveform(unsigned long type) -> ChorusWaveform
    {
        if(type == EAX_CHORUS_SINUSOID) return ChorusWaveform::Sinusoid;
        if(type == EAX_CHORUS_TRIANGLE) return ChorusWaveform::Triangle;
        return ChorusWaveform::Sinusoid;
    }
}; // EaxChorusTraits

struct EaxFlangerTraits {
    using EaxProps = EAXFLANGERPROPERTIES;
    using Committer = EaxFlangerCommitter;

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

    static constexpr auto eax_waveform(unsigned long type) -> ChorusWaveform
    {
        if(type == EAX_FLANGER_SINUSOID) return ChorusWaveform::Sinusoid;
        if(type == EAX_FLANGER_TRIANGLE) return ChorusWaveform::Triangle;
        return ChorusWaveform::Sinusoid;
    }
}; // EaxFlangerTraits

template<typename TTraits>
struct ChorusFlangerEffect {
    using Traits = TTraits;
    using EaxProps = typename Traits::EaxProps;
    using Committer = typename Traits::Committer;
    using Exception = typename Committer::Exception;

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
        void operator()(const EaxProps& all) const
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
        props = EaxProps{
            .ulWaveform = Traits::eax_default_waveform(),
            .lPhase = Traits::eax_default_phase(),
            .flRate = Traits::eax_default_rate(),
            .flDepth = Traits::eax_default_depth(),
            .flFeedback = Traits::eax_default_feedback(),
            .flDelay = Traits::eax_default_delay()};
    }


    static void Get(const EaxCall &call, const EaxProps &all)
    {
        switch(call.get_property_id())
        {
        case Traits::eax_none_param_id(): break;
        case Traits::eax_allparameters_param_id(): call.store(all); break;
        case Traits::eax_waveform_param_id(): call.store(all.ulWaveform); break;
        case Traits::eax_phase_param_id(): call.store(all.lPhase); break;
        case Traits::eax_rate_param_id(): call.store(all.flRate); break;
        case Traits::eax_depth_param_id(): call.store(all.flDepth); break;
        case Traits::eax_feedback_param_id(): call.store(all.flFeedback); break;
        case Traits::eax_delay_param_id(): call.store(all.flDelay); break;
        default: Committer::fail_unknown_property_id();
        }
    }

    static void Set(const EaxCall &call, EaxProps &all)
    {
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

    static bool Commit(const EaxProps &props, EaxEffectProps &props_, ChorusProps &al_props_)
    {
        if(auto *cur = std::get_if<EaxProps>(&props_); cur && *cur == props)
            return false;

        props_ = props;

        al_props_.Waveform = Traits::eax_waveform(props.ulWaveform);
        al_props_.Phase = gsl::narrow_cast<int>(props.lPhase);
        al_props_.Rate = props.flRate;
        al_props_.Depth = props.flDepth;
        al_props_.Feedback = props.flFeedback;
        al_props_.Delay = props.flDelay;
        if(EaxTraceCommits) [[unlikely]]
        {
            TRACE("Chorus/flanger commit:\n"
                "  Waveform: {}\n"
                "  Phase: {}\n"
                "  Rate: {:f}\n"
                "  Depth: {:f}\n"
                "  Feedback: {:f}\n"
                "  Delay: {:f}", al::to_underlying(al_props_.Waveform), al_props_.Phase,
                al_props_.Rate, al_props_.Depth, al_props_.Feedback, al_props_.Delay);
        }

        return true;
    }
}; // EaxChorusFlangerEffect


using ChorusCommitter = EaxCommitter<EaxChorusCommitter>;
using FlangerCommitter = EaxCommitter<EaxFlangerCommitter>;

} // namespace

template<> /* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
struct ChorusCommitter::Exception : public EaxException {
    explicit Exception(const std::string_view message) : EaxException{"EAX_CHORUS_EFFECT", message}
    { }
};

template<> [[noreturn]]
void ChorusCommitter::fail(const std::string_view message)
{ throw Exception{message}; }

auto EaxChorusCommitter::commit(const EAXCHORUSPROPERTIES &props) const -> bool
{
    using Committer = ChorusFlangerEffect<EaxChorusTraits>;
    return Committer::Commit(props, mEaxProps, mAlProps.emplace<ChorusProps>());
}

void EaxChorusCommitter::SetDefaults(EaxEffectProps &props)
{
    using Committer = ChorusFlangerEffect<EaxChorusTraits>;
    Committer::SetDefaults(props);
}

void EaxChorusCommitter::Get(const EaxCall &call, const EAXCHORUSPROPERTIES &props)
{
    using Committer = ChorusFlangerEffect<EaxChorusTraits>;
    Committer::Get(call, props);
}

void EaxChorusCommitter::Set(const EaxCall &call, EAXCHORUSPROPERTIES &props)
{
    using Committer = ChorusFlangerEffect<EaxChorusTraits>;
    Committer::Set(call, props);
}

template<> /* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
struct FlangerCommitter::Exception : public EaxException {
    explicit Exception(const std::string_view message) : EaxException{"EAX_FLANGER_EFFECT",message}
    { }
};

template<> [[noreturn]]
void FlangerCommitter::fail(const std::string_view message)
{ throw Exception{message}; }

auto EaxFlangerCommitter::commit(const EAXFLANGERPROPERTIES &props) const -> bool
{
    using Committer = ChorusFlangerEffect<EaxFlangerTraits>;
    return Committer::Commit(props, mEaxProps, mAlProps.emplace<ChorusProps>());
}

void EaxFlangerCommitter::SetDefaults(EaxEffectProps &props)
{
    using Committer = ChorusFlangerEffect<EaxFlangerTraits>;
    Committer::SetDefaults(props);
}

void EaxFlangerCommitter::Get(const EaxCall &call, const EAXFLANGERPROPERTIES &props)
{
    using Committer = ChorusFlangerEffect<EaxFlangerTraits>;
    Committer::Get(call, props);
}

void EaxFlangerCommitter::Set(const EaxCall &call, EAXFLANGERPROPERTIES &props)
{
    using Committer = ChorusFlangerEffect<EaxFlangerTraits>;
    Committer::Set(call, props);
}

#endif // ALSOFT_EAX
