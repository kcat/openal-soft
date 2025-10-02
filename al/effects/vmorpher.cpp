
#include "config.h"

#include <format>
#include <optional>
#include <stdexcept>

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

constexpr std::optional<VMorpherPhenome> PhenomeFromEnum(ALenum val) noexcept
{
#define HANDLE_PHENOME(x) case AL_VOCAL_MORPHER_PHONEME_ ## x:                \
    return VMorpherPhenome::x
    switch(val)
    {
    HANDLE_PHENOME(A);
    HANDLE_PHENOME(E);
    HANDLE_PHENOME(I);
    HANDLE_PHENOME(O);
    HANDLE_PHENOME(U);
    HANDLE_PHENOME(AA);
    HANDLE_PHENOME(AE);
    HANDLE_PHENOME(AH);
    HANDLE_PHENOME(AO);
    HANDLE_PHENOME(EH);
    HANDLE_PHENOME(ER);
    HANDLE_PHENOME(IH);
    HANDLE_PHENOME(IY);
    HANDLE_PHENOME(UH);
    HANDLE_PHENOME(UW);
    HANDLE_PHENOME(B);
    HANDLE_PHENOME(D);
    HANDLE_PHENOME(F);
    HANDLE_PHENOME(G);
    HANDLE_PHENOME(J);
    HANDLE_PHENOME(K);
    HANDLE_PHENOME(L);
    HANDLE_PHENOME(M);
    HANDLE_PHENOME(N);
    HANDLE_PHENOME(P);
    HANDLE_PHENOME(R);
    HANDLE_PHENOME(S);
    HANDLE_PHENOME(T);
    HANDLE_PHENOME(V);
    HANDLE_PHENOME(Z);
    }
    return std::nullopt;
#undef HANDLE_PHENOME
}
constexpr ALenum EnumFromPhenome(VMorpherPhenome phenome)
{
#define HANDLE_PHENOME(x) case VMorpherPhenome::x: return AL_VOCAL_MORPHER_PHONEME_ ## x
    switch(phenome)
    {
    HANDLE_PHENOME(A);
    HANDLE_PHENOME(E);
    HANDLE_PHENOME(I);
    HANDLE_PHENOME(O);
    HANDLE_PHENOME(U);
    HANDLE_PHENOME(AA);
    HANDLE_PHENOME(AE);
    HANDLE_PHENOME(AH);
    HANDLE_PHENOME(AO);
    HANDLE_PHENOME(EH);
    HANDLE_PHENOME(ER);
    HANDLE_PHENOME(IH);
    HANDLE_PHENOME(IY);
    HANDLE_PHENOME(UH);
    HANDLE_PHENOME(UW);
    HANDLE_PHENOME(B);
    HANDLE_PHENOME(D);
    HANDLE_PHENOME(F);
    HANDLE_PHENOME(G);
    HANDLE_PHENOME(J);
    HANDLE_PHENOME(K);
    HANDLE_PHENOME(L);
    HANDLE_PHENOME(M);
    HANDLE_PHENOME(N);
    HANDLE_PHENOME(P);
    HANDLE_PHENOME(R);
    HANDLE_PHENOME(S);
    HANDLE_PHENOME(T);
    HANDLE_PHENOME(V);
    HANDLE_PHENOME(Z);
    }
    throw std::runtime_error{std::format("Invalid phenome: {}", int{al::to_underlying(phenome)})};
#undef HANDLE_PHENOME
}

constexpr std::optional<VMorpherWaveform> WaveformFromEmum(ALenum value) noexcept
{
    switch(value)
    {
    case AL_VOCAL_MORPHER_WAVEFORM_SINUSOID: return VMorpherWaveform::Sinusoid;
    case AL_VOCAL_MORPHER_WAVEFORM_TRIANGLE: return VMorpherWaveform::Triangle;
    case AL_VOCAL_MORPHER_WAVEFORM_SAWTOOTH: return VMorpherWaveform::Sawtooth;
    }
    return std::nullopt;
}
constexpr ALenum EnumFromWaveform(VMorpherWaveform type)
{
    switch(type)
    {
    case VMorpherWaveform::Sinusoid: return AL_VOCAL_MORPHER_WAVEFORM_SINUSOID;
    case VMorpherWaveform::Triangle: return AL_VOCAL_MORPHER_WAVEFORM_TRIANGLE;
    case VMorpherWaveform::Sawtooth: return AL_VOCAL_MORPHER_WAVEFORM_SAWTOOTH;
    }
    throw std::runtime_error{std::format("Invalid vocal morpher waveform: {}",
        int{al::to_underlying(type)})};
}

consteval auto genDefaultProps() noexcept -> EffectProps
{
    /* NOLINTBEGIN(bugprone-unchecked-optional-access) */
    return VmorpherProps{
        .Rate                 = AL_VOCAL_MORPHER_DEFAULT_RATE,
        .PhonemeA             = PhenomeFromEnum(AL_VOCAL_MORPHER_DEFAULT_PHONEMEA).value(),
        .PhonemeB             = PhenomeFromEnum(AL_VOCAL_MORPHER_DEFAULT_PHONEMEB).value(),
        .PhonemeACoarseTuning = AL_VOCAL_MORPHER_DEFAULT_PHONEMEA_COARSE_TUNING,
        .PhonemeBCoarseTuning = AL_VOCAL_MORPHER_DEFAULT_PHONEMEB_COARSE_TUNING,
        .Waveform             = WaveformFromEmum(AL_VOCAL_MORPHER_DEFAULT_WAVEFORM).value()};
    /* NOLINTEND(bugprone-unchecked-optional-access) */
}

} // namespace

constinit const EffectProps VmorpherEffectProps(genDefaultProps());

void VmorpherEffectHandler::SetParami(al::Context *context, VmorpherProps &props, ALenum param, int val)
{
    switch(param)
    {
    case AL_VOCAL_MORPHER_PHONEMEA:
        if(auto phenomeopt = PhenomeFromEnum(val))
            props.PhonemeA = *phenomeopt;
        else
            context->throw_error(AL_INVALID_VALUE,
                "Vocal morpher phoneme-a out of range: {:#04x}", as_unsigned(val));
        return;

    case AL_VOCAL_MORPHER_PHONEMEA_COARSE_TUNING:
        if(!(val >= AL_VOCAL_MORPHER_MIN_PHONEMEA_COARSE_TUNING && val <= AL_VOCAL_MORPHER_MAX_PHONEMEA_COARSE_TUNING))
            context->throw_error(AL_INVALID_VALUE,
                "Vocal morpher phoneme-a coarse tuning out of range");
        props.PhonemeACoarseTuning = val;
        return;

    case AL_VOCAL_MORPHER_PHONEMEB:
        if(auto phenomeopt = PhenomeFromEnum(val))
            props.PhonemeB = *phenomeopt;
        else
            context->throw_error(AL_INVALID_VALUE,
                "Vocal morpher phoneme-b out of range: {:#04x}", as_unsigned(val));
        return;

    case AL_VOCAL_MORPHER_PHONEMEB_COARSE_TUNING:
        if(!(val >= AL_VOCAL_MORPHER_MIN_PHONEMEB_COARSE_TUNING && val <= AL_VOCAL_MORPHER_MAX_PHONEMEB_COARSE_TUNING))
            context->throw_error(AL_INVALID_VALUE,
                "Vocal morpher phoneme-b coarse tuning out of range");
        props.PhonemeBCoarseTuning = val;
        return;

    case AL_VOCAL_MORPHER_WAVEFORM:
        if(auto formopt = WaveformFromEmum(val))
            props.Waveform = *formopt;
        else
            context->throw_error(AL_INVALID_VALUE, "Vocal morpher waveform out of range: {:#04x}",
                as_unsigned(val));
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid vocal morpher integer property {:#04x}",
        as_unsigned(param));
}
void VmorpherEffectHandler::SetParamiv(al::Context *context, VmorpherProps &props, ALenum param, const int *vals)
{ SetParami(context, props, param, *vals); }
void VmorpherEffectHandler::SetParamf(al::Context *context, VmorpherProps &props, ALenum param, float val)
{
    switch(param)
    {
    case AL_VOCAL_MORPHER_RATE:
        if(!(val >= AL_VOCAL_MORPHER_MIN_RATE && val <= AL_VOCAL_MORPHER_MAX_RATE))
            context->throw_error(AL_INVALID_VALUE, "Vocal morpher rate out of range");
        props.Rate = val;
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid vocal morpher float property {:#04x}",
        as_unsigned(param));
}
void VmorpherEffectHandler::SetParamfv(al::Context *context, VmorpherProps &props, ALenum param, const float *vals)
{ SetParamf(context, props, param, *vals); }

void VmorpherEffectHandler::GetParami(al::Context *context, const VmorpherProps &props, ALenum param, int* val)
{
    switch(param)
    {
    case AL_VOCAL_MORPHER_PHONEMEA: *val = EnumFromPhenome(props.PhonemeA); return;
    case AL_VOCAL_MORPHER_PHONEMEA_COARSE_TUNING: *val = props.PhonemeACoarseTuning; return;
    case AL_VOCAL_MORPHER_PHONEMEB: *val = EnumFromPhenome(props.PhonemeB); return;
    case AL_VOCAL_MORPHER_PHONEMEB_COARSE_TUNING: *val = props.PhonemeBCoarseTuning; return;
    case AL_VOCAL_MORPHER_WAVEFORM: *val = EnumFromWaveform(props.Waveform); return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid vocal morpher integer property {:#04x}",
        as_unsigned(param));
}
void VmorpherEffectHandler::GetParamiv(al::Context *context, const VmorpherProps &props, ALenum param, int *vals)
{ GetParami(context, props, param, vals); }
void VmorpherEffectHandler::GetParamf(al::Context *context, const VmorpherProps &props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_VOCAL_MORPHER_RATE: *val = props.Rate; return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid vocal morpher float property {:#04x}",
        as_unsigned(param));
}
void VmorpherEffectHandler::GetParamfv(al::Context *context, const VmorpherProps &props, ALenum param, float *vals)
{ GetParamf(context, props, param, vals); }


#if ALSOFT_EAX
namespace {

using VocalMorpherCommitter = EaxCommitter<EaxVocalMorpherCommitter>;

struct PhonemeAValidator {
    void operator()(unsigned long ulPhonemeA) const
    {
        eax_validate_range<VocalMorpherCommitter::Exception>(
            "Phoneme A",
            ulPhonemeA,
            EAXVOCALMORPHER_MINPHONEMEA,
            EAXVOCALMORPHER_MAXPHONEMEA);
    }
}; // PhonemeAValidator

struct PhonemeACoarseTuningValidator {
    void operator()(long lPhonemeACoarseTuning) const
    {
        eax_validate_range<VocalMorpherCommitter::Exception>(
            "Phoneme A Coarse Tuning",
            lPhonemeACoarseTuning,
            EAXVOCALMORPHER_MINPHONEMEACOARSETUNING,
            EAXVOCALMORPHER_MAXPHONEMEACOARSETUNING);
    }
}; // PhonemeACoarseTuningValidator

struct PhonemeBValidator {
    void operator()(unsigned long ulPhonemeB) const
    {
        eax_validate_range<VocalMorpherCommitter::Exception>(
            "Phoneme B",
            ulPhonemeB,
            EAXVOCALMORPHER_MINPHONEMEB,
            EAXVOCALMORPHER_MAXPHONEMEB);
    }
}; // PhonemeBValidator

struct PhonemeBCoarseTuningValidator {
    void operator()(long lPhonemeBCoarseTuning) const
    {
        eax_validate_range<VocalMorpherCommitter::Exception>(
            "Phoneme B Coarse Tuning",
            lPhonemeBCoarseTuning,
            EAXVOCALMORPHER_MINPHONEMEBCOARSETUNING,
            EAXVOCALMORPHER_MAXPHONEMEBCOARSETUNING);
    }
}; // PhonemeBCoarseTuningValidator

struct WaveformValidator {
    void operator()(unsigned long ulWaveform) const
    {
        eax_validate_range<VocalMorpherCommitter::Exception>(
            "Waveform",
            ulWaveform,
            EAXVOCALMORPHER_MINWAVEFORM,
            EAXVOCALMORPHER_MAXWAVEFORM);
    }
}; // WaveformValidator

struct RateValidator {
    void operator()(float flRate) const
    {
        eax_validate_range<VocalMorpherCommitter::Exception>(
            "Rate",
            flRate,
            EAXVOCALMORPHER_MINRATE,
            EAXVOCALMORPHER_MAXRATE);
    }
}; // RateValidator

struct AllValidator {
    void operator()(const EAXVOCALMORPHERPROPERTIES& all) const
    {
        PhonemeAValidator{}(all.ulPhonemeA);
        PhonemeACoarseTuningValidator{}(all.lPhonemeACoarseTuning);
        PhonemeBValidator{}(all.ulPhonemeB);
        PhonemeBCoarseTuningValidator{}(all.lPhonemeBCoarseTuning);
        WaveformValidator{}(all.ulWaveform);
        RateValidator{}(all.flRate);
    }
}; // AllValidator

} // namespace

template<> /* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
struct VocalMorpherCommitter::Exception : public EaxException {
    explicit Exception(const std::string_view message)
        : EaxException{"EAX_VOCAL_MORPHER_EFFECT", message}
    { }
};

template<> [[noreturn]]
void VocalMorpherCommitter::fail(const std::string_view message)
{ throw Exception{message}; }

auto EaxVocalMorpherCommitter::commit(const EAXVOCALMORPHERPROPERTIES &props) const -> bool
{
    if(auto *cur = std::get_if<EAXVOCALMORPHERPROPERTIES>(&mEaxProps); cur && *cur == props)
        return false;

    static constexpr auto get_phoneme = [](unsigned long phoneme) noexcept
    {
#define HANDLE_PHENOME(x) case EAX_VOCALMORPHER_PHONEME_##x: return VMorpherPhenome::x
        switch(phoneme)
        {
        HANDLE_PHENOME(A);
        HANDLE_PHENOME(E);
        HANDLE_PHENOME(I);
        HANDLE_PHENOME(O);
        HANDLE_PHENOME(U);
        HANDLE_PHENOME(AA);
        HANDLE_PHENOME(AE);
        HANDLE_PHENOME(AH);
        HANDLE_PHENOME(AO);
        HANDLE_PHENOME(EH);
        HANDLE_PHENOME(ER);
        HANDLE_PHENOME(IH);
        HANDLE_PHENOME(IY);
        HANDLE_PHENOME(UH);
        HANDLE_PHENOME(UW);
        HANDLE_PHENOME(B);
        HANDLE_PHENOME(D);
        HANDLE_PHENOME(F);
        HANDLE_PHENOME(G);
        HANDLE_PHENOME(J);
        HANDLE_PHENOME(K);
        HANDLE_PHENOME(L);
        HANDLE_PHENOME(M);
        HANDLE_PHENOME(N);
        HANDLE_PHENOME(P);
        HANDLE_PHENOME(R);
        HANDLE_PHENOME(S);
        HANDLE_PHENOME(T);
        HANDLE_PHENOME(V);
        HANDLE_PHENOME(Z);
        default: break;
        }
        return VMorpherPhenome::A;
#undef HANDLE_PHENOME
    };
    static constexpr auto get_waveform = [](unsigned long form) noexcept
    {
        switch(form)
        {
        case EAX_VOCALMORPHER_SINUSOID: return VMorpherWaveform::Sinusoid;
        case EAX_VOCALMORPHER_TRIANGLE: return VMorpherWaveform::Triangle;
        case EAX_VOCALMORPHER_SAWTOOTH: return VMorpherWaveform::Sawtooth;
        default: break;
        }
        return VMorpherWaveform::Sinusoid;
    };

    mEaxProps = props;
    mAlProps = VmorpherProps{
        .Rate = props.flRate,
        .PhonemeA = get_phoneme(props.ulPhonemeA),
        .PhonemeB = get_phoneme(props.ulPhonemeB),
        .PhonemeACoarseTuning = gsl::narrow_cast<int>(props.lPhonemeACoarseTuning),
        .PhonemeBCoarseTuning = gsl::narrow_cast<int>(props.lPhonemeBCoarseTuning),
        .Waveform = get_waveform(props.ulWaveform)};

    return true;
}

void EaxVocalMorpherCommitter::SetDefaults(EaxEffectProps &props)
{
    props = EAXVOCALMORPHERPROPERTIES{
        .ulPhonemeA = EAXVOCALMORPHER_DEFAULTPHONEMEA,
        .lPhonemeACoarseTuning = EAXVOCALMORPHER_DEFAULTPHONEMEACOARSETUNING,
        .ulPhonemeB = EAXVOCALMORPHER_DEFAULTPHONEMEB,
        .lPhonemeBCoarseTuning = EAXVOCALMORPHER_DEFAULTPHONEMEBCOARSETUNING,
        .ulWaveform = EAXVOCALMORPHER_DEFAULTWAVEFORM,
        .flRate = EAXVOCALMORPHER_DEFAULTRATE};
}

void EaxVocalMorpherCommitter::Get(const EaxCall &call, const EAXVOCALMORPHERPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case EAXVOCALMORPHER_NONE: break;
    case EAXVOCALMORPHER_ALLPARAMETERS: call.store(props); break;
    case EAXVOCALMORPHER_PHONEMEA: call.store(props.ulPhonemeA); break;
    case EAXVOCALMORPHER_PHONEMEACOARSETUNING: call.store(props.lPhonemeACoarseTuning); break;
    case EAXVOCALMORPHER_PHONEMEB: call.store(props.ulPhonemeB); break;
    case EAXVOCALMORPHER_PHONEMEBCOARSETUNING: call.store(props.lPhonemeBCoarseTuning); break;
    case EAXVOCALMORPHER_WAVEFORM: call.store(props.ulWaveform); break;
    case EAXVOCALMORPHER_RATE: call.store(props.flRate); break;
    default: fail_unknown_property_id();
    }
}

void EaxVocalMorpherCommitter::Set(const EaxCall &call, EAXVOCALMORPHERPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case EAXVOCALMORPHER_NONE: break;
    case EAXVOCALMORPHER_ALLPARAMETERS: defer<AllValidator>(call, props); break;
    case EAXVOCALMORPHER_PHONEMEA: defer<PhonemeAValidator>(call, props.ulPhonemeA); break;
    case EAXVOCALMORPHER_PHONEMEACOARSETUNING: defer<PhonemeACoarseTuningValidator>(call, props.lPhonemeACoarseTuning); break;
    case EAXVOCALMORPHER_PHONEMEB: defer<PhonemeBValidator>(call, props.ulPhonemeB); break;
    case EAXVOCALMORPHER_PHONEMEBCOARSETUNING: defer<PhonemeBCoarseTuningValidator>(call, props.lPhonemeBCoarseTuning); break;
    case EAXVOCALMORPHER_WAVEFORM: defer<WaveformValidator>(call, props.ulWaveform); break;
    case EAXVOCALMORPHER_RATE: defer<RateValidator>(call, props.flRate); break;
    default: fail_unknown_property_id();
    }
}

#endif // ALSOFT_EAX
