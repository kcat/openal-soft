
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

al::optional<VMorpherPhenome> PhenomeFromEnum(ALenum val)
{
#define HANDLE_PHENOME(x) case AL_VOCAL_MORPHER_PHONEME_ ## x:                \
    return al::make_optional(VMorpherPhenome::x)
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
    return al::nullopt;
#undef HANDLE_PHENOME
}
ALenum EnumFromPhenome(VMorpherPhenome phenome)
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
    throw std::runtime_error{"Invalid phenome: "+std::to_string(static_cast<int>(phenome))};
#undef HANDLE_PHENOME
}

al::optional<VMorpherWaveform> WaveformFromEmum(ALenum value)
{
    switch(value)
    {
    case AL_VOCAL_MORPHER_WAVEFORM_SINUSOID: return al::make_optional(VMorpherWaveform::Sinusoid);
    case AL_VOCAL_MORPHER_WAVEFORM_TRIANGLE: return al::make_optional(VMorpherWaveform::Triangle);
    case AL_VOCAL_MORPHER_WAVEFORM_SAWTOOTH: return al::make_optional(VMorpherWaveform::Sawtooth);
    }
    return al::nullopt;
}
ALenum EnumFromWaveform(VMorpherWaveform type)
{
    switch(type)
    {
    case VMorpherWaveform::Sinusoid: return AL_VOCAL_MORPHER_WAVEFORM_SINUSOID;
    case VMorpherWaveform::Triangle: return AL_VOCAL_MORPHER_WAVEFORM_TRIANGLE;
    case VMorpherWaveform::Sawtooth: return AL_VOCAL_MORPHER_WAVEFORM_SAWTOOTH;
    }
    throw std::runtime_error{"Invalid vocal morpher waveform: " +
        std::to_string(static_cast<int>(type))};
}

void Vmorpher_setParami(EffectProps *props, ALenum param, int val)
{
    switch(param)
    {
    case AL_VOCAL_MORPHER_PHONEMEA:
        if(auto phenomeopt = PhenomeFromEnum(val))
            props->Vmorpher.PhonemeA = *phenomeopt;
        else
            throw effect_exception{AL_INVALID_VALUE, "Vocal morpher phoneme-a out of range: 0x%04x", val};
        break;

    case AL_VOCAL_MORPHER_PHONEMEA_COARSE_TUNING:
        if(!(val >= AL_VOCAL_MORPHER_MIN_PHONEMEA_COARSE_TUNING && val <= AL_VOCAL_MORPHER_MAX_PHONEMEA_COARSE_TUNING))
            throw effect_exception{AL_INVALID_VALUE, "Vocal morpher phoneme-a coarse tuning out of range"};
        props->Vmorpher.PhonemeACoarseTuning = val;
        break;

    case AL_VOCAL_MORPHER_PHONEMEB:
        if(auto phenomeopt = PhenomeFromEnum(val))
            props->Vmorpher.PhonemeB = *phenomeopt;
        else
            throw effect_exception{AL_INVALID_VALUE, "Vocal morpher phoneme-b out of range: 0x%04x", val};
        break;

    case AL_VOCAL_MORPHER_PHONEMEB_COARSE_TUNING:
        if(!(val >= AL_VOCAL_MORPHER_MIN_PHONEMEB_COARSE_TUNING && val <= AL_VOCAL_MORPHER_MAX_PHONEMEB_COARSE_TUNING))
            throw effect_exception{AL_INVALID_VALUE, "Vocal morpher phoneme-b coarse tuning out of range"};
        props->Vmorpher.PhonemeBCoarseTuning = val;
        break;

    case AL_VOCAL_MORPHER_WAVEFORM:
        if(auto formopt = WaveformFromEmum(val))
            props->Vmorpher.Waveform = *formopt;
        else
            throw effect_exception{AL_INVALID_VALUE, "Vocal morpher waveform out of range: 0x%04x", val};
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid vocal morpher integer property 0x%04x",
            param};
    }
}
void Vmorpher_setParamiv(EffectProps*, ALenum param, const int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid vocal morpher integer-vector property 0x%04x",
        param};
}
void Vmorpher_setParamf(EffectProps *props, ALenum param, float val)
{
    switch(param)
    {
    case AL_VOCAL_MORPHER_RATE:
        if(!(val >= AL_VOCAL_MORPHER_MIN_RATE && val <= AL_VOCAL_MORPHER_MAX_RATE))
            throw effect_exception{AL_INVALID_VALUE, "Vocal morpher rate out of range"};
        props->Vmorpher.Rate = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid vocal morpher float property 0x%04x",
            param};
    }
}
void Vmorpher_setParamfv(EffectProps *props, ALenum param, const float *vals)
{ Vmorpher_setParamf(props, param, vals[0]); }

void Vmorpher_getParami(const EffectProps *props, ALenum param, int* val)
{
    switch(param)
    {
    case AL_VOCAL_MORPHER_PHONEMEA:
        *val = EnumFromPhenome(props->Vmorpher.PhonemeA);
        break;

    case AL_VOCAL_MORPHER_PHONEMEA_COARSE_TUNING:
        *val = props->Vmorpher.PhonemeACoarseTuning;
        break;

    case AL_VOCAL_MORPHER_PHONEMEB:
        *val = EnumFromPhenome(props->Vmorpher.PhonemeB);
        break;

    case AL_VOCAL_MORPHER_PHONEMEB_COARSE_TUNING:
        *val = props->Vmorpher.PhonemeBCoarseTuning;
        break;

    case AL_VOCAL_MORPHER_WAVEFORM:
        *val = EnumFromWaveform(props->Vmorpher.Waveform);
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid vocal morpher integer property 0x%04x",
            param};
    }
}
void Vmorpher_getParamiv(const EffectProps*, ALenum param, int*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid vocal morpher integer-vector property 0x%04x",
        param};
}
void Vmorpher_getParamf(const EffectProps *props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_VOCAL_MORPHER_RATE:
        *val = props->Vmorpher.Rate;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid vocal morpher float property 0x%04x",
            param};
    }
}
void Vmorpher_getParamfv(const EffectProps *props, ALenum param, float *vals)
{ Vmorpher_getParamf(props, param, vals); }

EffectProps genDefaultProps() noexcept
{
    EffectProps props{};
    props.Vmorpher.Rate                 = AL_VOCAL_MORPHER_DEFAULT_RATE;
    props.Vmorpher.PhonemeA             = *PhenomeFromEnum(AL_VOCAL_MORPHER_DEFAULT_PHONEMEA);
    props.Vmorpher.PhonemeB             = *PhenomeFromEnum(AL_VOCAL_MORPHER_DEFAULT_PHONEMEB);
    props.Vmorpher.PhonemeACoarseTuning = AL_VOCAL_MORPHER_DEFAULT_PHONEMEA_COARSE_TUNING;
    props.Vmorpher.PhonemeBCoarseTuning = AL_VOCAL_MORPHER_DEFAULT_PHONEMEB_COARSE_TUNING;
    props.Vmorpher.Waveform             = *WaveformFromEmum(AL_VOCAL_MORPHER_DEFAULT_WAVEFORM);
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Vmorpher);

const EffectProps VmorpherEffectProps{genDefaultProps()};

#ifdef ALSOFT_EAX
namespace {

class EaxVocalMorpherEffectException : public EaxException {
public:
    explicit EaxVocalMorpherEffectException(const char* message)
        : EaxException{"EAX_VOCAL_MORPHER_EFFECT", message}
    {}
}; // EaxVocalMorpherEffectException

class EaxVocalMorpherEffect final : public EaxEffect4<EaxVocalMorpherEffectException, EAXVOCALMORPHERPROPERTIES> {
public:
    EaxVocalMorpherEffect(int eax_version);

private:
    struct PhonemeAValidator {
        void operator()(unsigned long ulPhonemeA) const
        {
            eax_validate_range<Exception>(
                "Phoneme A",
                ulPhonemeA,
                EAXVOCALMORPHER_MINPHONEMEA,
                EAXVOCALMORPHER_MAXPHONEMEA);
        }
    }; // PhonemeAValidator

    struct PhonemeACoarseTuningValidator {
        void operator()(long lPhonemeACoarseTuning) const
        {
            eax_validate_range<Exception>(
                "Phoneme A Coarse Tuning",
                lPhonemeACoarseTuning,
                EAXVOCALMORPHER_MINPHONEMEACOARSETUNING,
                EAXVOCALMORPHER_MAXPHONEMEACOARSETUNING);
        }
    }; // PhonemeACoarseTuningValidator

    struct PhonemeBValidator {
        void operator()(unsigned long ulPhonemeB) const
        {
            eax_validate_range<Exception>(
                "Phoneme B",
                ulPhonemeB,
                EAXVOCALMORPHER_MINPHONEMEB,
                EAXVOCALMORPHER_MAXPHONEMEB);
        }
    }; // PhonemeBValidator

    struct PhonemeBCoarseTuningValidator {
        void operator()(long lPhonemeBCoarseTuning) const
        {
            eax_validate_range<Exception>(
                "Phoneme B Coarse Tuning",
                lPhonemeBCoarseTuning,
                EAXVOCALMORPHER_MINPHONEMEBCOARSETUNING,
                EAXVOCALMORPHER_MAXPHONEMEBCOARSETUNING);
        }
    }; // PhonemeBCoarseTuningValidator

    struct WaveformValidator {
        void operator()(unsigned long ulWaveform) const
        {
            eax_validate_range<Exception>(
                "Waveform",
                ulWaveform,
                EAXVOCALMORPHER_MINWAVEFORM,
                EAXVOCALMORPHER_MAXWAVEFORM);
        }
    }; // WaveformValidator

    struct RateValidator {
        void operator()(float flRate) const
        {
            eax_validate_range<Exception>(
                "Rate",
                flRate,
                EAXVOCALMORPHER_MINRATE,
                EAXVOCALMORPHER_MAXRATE);
        }
    }; // RateValidator

    struct AllValidator {
        void operator()(const Props& all) const
        {
            PhonemeAValidator{}(all.ulPhonemeA);
            PhonemeACoarseTuningValidator{}(all.lPhonemeACoarseTuning);
            PhonemeBValidator{}(all.ulPhonemeB);
            PhonemeBCoarseTuningValidator{}(all.lPhonemeBCoarseTuning);
            WaveformValidator{}(all.ulWaveform);
            RateValidator{}(all.flRate);
        }
    }; // AllValidator

    void set_defaults(Props& props) override;

    void set_efx_phoneme_a();
    void set_efx_phoneme_a_coarse_tuning() noexcept;
    void set_efx_phoneme_b();
    void set_efx_phoneme_b_coarse_tuning() noexcept;
    void set_efx_waveform();
    void set_efx_rate() noexcept;
    void set_efx_defaults() override;

    void get(const EaxCall& call, const Props& props) override;
    void set(const EaxCall& call, Props& props) override;
    bool commit_props(const Props& props) override;
}; // EaxVocalMorpherEffect

EaxVocalMorpherEffect::EaxVocalMorpherEffect(int eax_version)
    : EaxEffect4{AL_EFFECT_VOCAL_MORPHER, eax_version}
{}

void EaxVocalMorpherEffect::set_defaults(Props& props)
{
    props.ulPhonemeA = EAXVOCALMORPHER_DEFAULTPHONEMEA;
    props.lPhonemeACoarseTuning = EAXVOCALMORPHER_DEFAULTPHONEMEACOARSETUNING;
    props.ulPhonemeB = EAXVOCALMORPHER_DEFAULTPHONEMEB;
    props.lPhonemeBCoarseTuning = EAXVOCALMORPHER_DEFAULTPHONEMEBCOARSETUNING;
    props.ulWaveform = EAXVOCALMORPHER_DEFAULTWAVEFORM;
    props.flRate = EAXVOCALMORPHER_DEFAULTRATE;
}

void EaxVocalMorpherEffect::set_efx_phoneme_a()
{
    const auto phoneme_a = clamp(
        static_cast<ALint>(props_.ulPhonemeA),
        AL_VOCAL_MORPHER_MIN_PHONEMEA,
        AL_VOCAL_MORPHER_MAX_PHONEMEA);
    const auto efx_phoneme_a = PhenomeFromEnum(phoneme_a);
    assert(efx_phoneme_a.has_value());
    al_effect_props_.Vmorpher.PhonemeA = *efx_phoneme_a;
}

void EaxVocalMorpherEffect::set_efx_phoneme_a_coarse_tuning() noexcept
{
    const auto phoneme_a_coarse_tuning = clamp(
        static_cast<ALint>(props_.lPhonemeACoarseTuning),
        AL_VOCAL_MORPHER_MIN_PHONEMEA_COARSE_TUNING,
        AL_VOCAL_MORPHER_MAX_PHONEMEA_COARSE_TUNING);
    al_effect_props_.Vmorpher.PhonemeACoarseTuning = phoneme_a_coarse_tuning;
}

void EaxVocalMorpherEffect::set_efx_phoneme_b()
{
    const auto phoneme_b = clamp(
        static_cast<ALint>(props_.ulPhonemeB),
        AL_VOCAL_MORPHER_MIN_PHONEMEB,
        AL_VOCAL_MORPHER_MAX_PHONEMEB);
    const auto efx_phoneme_b = PhenomeFromEnum(phoneme_b);
    assert(efx_phoneme_b.has_value());
    al_effect_props_.Vmorpher.PhonemeB = *efx_phoneme_b;
}

void EaxVocalMorpherEffect::set_efx_phoneme_b_coarse_tuning() noexcept
{
    al_effect_props_.Vmorpher.PhonemeBCoarseTuning = clamp(
        static_cast<ALint>(props_.lPhonemeBCoarseTuning),
        AL_VOCAL_MORPHER_MIN_PHONEMEB_COARSE_TUNING,
        AL_VOCAL_MORPHER_MAX_PHONEMEB_COARSE_TUNING);
}

void EaxVocalMorpherEffect::set_efx_waveform()
{
    const auto waveform = clamp(
        static_cast<ALint>(props_.ulWaveform),
        AL_VOCAL_MORPHER_MIN_WAVEFORM,
        AL_VOCAL_MORPHER_MAX_WAVEFORM);
    const auto wfx_waveform = WaveformFromEmum(waveform);
    assert(wfx_waveform.has_value());
    al_effect_props_.Vmorpher.Waveform = *wfx_waveform;
}

void EaxVocalMorpherEffect::set_efx_rate() noexcept
{
    al_effect_props_.Vmorpher.Rate = clamp(
        props_.flRate,
        AL_VOCAL_MORPHER_MIN_RATE,
        AL_VOCAL_MORPHER_MAX_RATE);
}

void EaxVocalMorpherEffect::set_efx_defaults()
{
    set_efx_phoneme_a();
    set_efx_phoneme_a_coarse_tuning();
    set_efx_phoneme_b();
    set_efx_phoneme_b_coarse_tuning();
    set_efx_waveform();
    set_efx_rate();
}

void EaxVocalMorpherEffect::get(const EaxCall& call, const Props& props)
{
    switch(call.get_property_id())
    {
        case EAXVOCALMORPHER_NONE:
            break;

        case EAXVOCALMORPHER_ALLPARAMETERS:
            call.set_value<Exception>(props);
            break;

        case EAXVOCALMORPHER_PHONEMEA:
            call.set_value<Exception>(props.ulPhonemeA);
            break;

        case EAXVOCALMORPHER_PHONEMEACOARSETUNING:
            call.set_value<Exception>(props.lPhonemeACoarseTuning);
            break;

        case EAXVOCALMORPHER_PHONEMEB:
            call.set_value<Exception>(props.ulPhonemeB);
            break;

        case EAXVOCALMORPHER_PHONEMEBCOARSETUNING:
            call.set_value<Exception>(props.lPhonemeBCoarseTuning);
            break;

        case EAXVOCALMORPHER_WAVEFORM:
            call.set_value<Exception>(props.ulWaveform);
            break;

        case EAXVOCALMORPHER_RATE:
            call.set_value<Exception>(props.flRate);
            break;

        default:
            fail_unknown_property_id();
    }
}

void EaxVocalMorpherEffect::set(const EaxCall& call, Props& props)
{
    switch(call.get_property_id())
    {
        case EAXVOCALMORPHER_NONE:
            break;

        case EAXVOCALMORPHER_ALLPARAMETERS:
            defer<AllValidator>(call, props);
            break;

        case EAXVOCALMORPHER_PHONEMEA:
            defer<PhonemeAValidator>(call, props.ulPhonemeA);
            break;

        case EAXVOCALMORPHER_PHONEMEACOARSETUNING:
            defer<PhonemeACoarseTuningValidator>(call, props.lPhonemeACoarseTuning);
            break;

        case EAXVOCALMORPHER_PHONEMEB:
            defer<PhonemeBValidator>(call, props.ulPhonemeB);
            break;

        case EAXVOCALMORPHER_PHONEMEBCOARSETUNING:
            defer<PhonemeBCoarseTuningValidator>(call, props.lPhonemeBCoarseTuning);
            break;

        case EAXVOCALMORPHER_WAVEFORM:
            defer<WaveformValidator>(call, props.ulWaveform);
            break;

        case EAXVOCALMORPHER_RATE:
            defer<RateValidator>(call, props.flRate);
            break;

        default:
            fail_unknown_property_id();
    }
}

bool EaxVocalMorpherEffect::commit_props(const Props& props)
{
    auto is_dirty = false;

    if (props_.ulPhonemeA != props.ulPhonemeA)
    {
        is_dirty = true;
        set_efx_phoneme_a();
    }

    if (props_.lPhonemeACoarseTuning != props.lPhonemeACoarseTuning)
    {
        is_dirty = true;
        set_efx_phoneme_a_coarse_tuning();
    }

    if (props_.ulPhonemeB != props.ulPhonemeB)
    {
        is_dirty = true;
        set_efx_phoneme_b();
    }

    if (props_.lPhonemeBCoarseTuning != props.lPhonemeBCoarseTuning)
    {
        is_dirty = true;
        set_efx_phoneme_b_coarse_tuning();
    }

    if (props_.ulWaveform != props.ulWaveform)
    {
        is_dirty = true;
        set_efx_waveform();
    }

    if (props_.flRate != props.flRate)
    {
        is_dirty = true;
        set_efx_rate();
    }

    return is_dirty;
}

} // namespace

EaxEffectUPtr eax_create_eax_vocal_morpher_effect(int eax_version)
{
    return eax_create_eax4_effect<EaxVocalMorpherEffect>(eax_version);
}

#endif // ALSOFT_EAX
