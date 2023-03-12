
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
    case AL_VOCAL_MORPHER_WAVEFORM_SINUSOID: return VMorpherWaveform::Sinusoid;
    case AL_VOCAL_MORPHER_WAVEFORM_TRIANGLE: return VMorpherWaveform::Triangle;
    case AL_VOCAL_MORPHER_WAVEFORM_SAWTOOTH: return VMorpherWaveform::Sawtooth;
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

template<>
struct VocalMorpherCommitter::Exception : public EaxException {
    explicit Exception(const char *message) : EaxException{"EAX_VOCAL_MORPHER_EFFECT", message}
    { }
};

template<>
[[noreturn]] void VocalMorpherCommitter::fail(const char *message)
{
    throw Exception{message};
}

template<>
bool VocalMorpherCommitter::commit(const EaxEffectProps &props)
{
    if(props.mType == mEaxProps.mType
        && mEaxProps.mVocalMorpher.ulPhonemeA == props.mVocalMorpher.ulPhonemeA
        && mEaxProps.mVocalMorpher.lPhonemeACoarseTuning == props.mVocalMorpher.lPhonemeACoarseTuning
        && mEaxProps.mVocalMorpher.ulPhonemeB == props.mVocalMorpher.ulPhonemeB
        && mEaxProps.mVocalMorpher.lPhonemeBCoarseTuning == props.mVocalMorpher.lPhonemeBCoarseTuning
        && mEaxProps.mVocalMorpher.ulWaveform == props.mVocalMorpher.ulWaveform
        && mEaxProps.mVocalMorpher.flRate == props.mVocalMorpher.flRate)
        return false;

    mEaxProps = props;

    auto get_phoneme = [](unsigned long phoneme) noexcept
    {
#define HANDLE_PHENOME(x) case x: return VMorpherPhenome::x
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
        }
        return VMorpherPhenome::A;
#undef HANDLE_PHENOME
    };
    auto get_waveform = [](unsigned long form) noexcept
    {
        if(form == EAX_VOCALMORPHER_SINUSOID) return VMorpherWaveform::Sinusoid;
        if(form == EAX_VOCALMORPHER_TRIANGLE) return VMorpherWaveform::Triangle;
        if(form == EAX_VOCALMORPHER_SAWTOOTH) return VMorpherWaveform::Sawtooth;
        return VMorpherWaveform::Sinusoid;
    };

    mAlProps.Vmorpher.PhonemeA = get_phoneme(props.mVocalMorpher.ulPhonemeA);
    mAlProps.Vmorpher.PhonemeACoarseTuning = static_cast<int>(props.mVocalMorpher.lPhonemeACoarseTuning);
    mAlProps.Vmorpher.PhonemeB = get_phoneme(props.mVocalMorpher.ulPhonemeB);
    mAlProps.Vmorpher.PhonemeBCoarseTuning = static_cast<int>(props.mVocalMorpher.lPhonemeBCoarseTuning);
    mAlProps.Vmorpher.Waveform = get_waveform(props.mVocalMorpher.ulWaveform);
    mAlProps.Vmorpher.Rate = props.mVocalMorpher.flRate;

    return true;
}

template<>
void VocalMorpherCommitter::SetDefaults(EaxEffectProps &props)
{
    props.mType = EaxEffectType::VocalMorpher;
    props.mVocalMorpher.ulPhonemeA = EAXVOCALMORPHER_DEFAULTPHONEMEA;
    props.mVocalMorpher.lPhonemeACoarseTuning = EAXVOCALMORPHER_DEFAULTPHONEMEACOARSETUNING;
    props.mVocalMorpher.ulPhonemeB = EAXVOCALMORPHER_DEFAULTPHONEMEB;
    props.mVocalMorpher.lPhonemeBCoarseTuning = EAXVOCALMORPHER_DEFAULTPHONEMEBCOARSETUNING;
    props.mVocalMorpher.ulWaveform = EAXVOCALMORPHER_DEFAULTWAVEFORM;
    props.mVocalMorpher.flRate = EAXVOCALMORPHER_DEFAULTRATE;
}

template<>
void VocalMorpherCommitter::Get(const EaxCall &call, const EaxEffectProps &props)
{
    switch(call.get_property_id())
    {
    case EAXVOCALMORPHER_NONE:
        break;

    case EAXVOCALMORPHER_ALLPARAMETERS:
        call.set_value<Exception>(props.mVocalMorpher);
        break;

    case EAXVOCALMORPHER_PHONEMEA:
        call.set_value<Exception>(props.mVocalMorpher.ulPhonemeA);
        break;

    case EAXVOCALMORPHER_PHONEMEACOARSETUNING:
        call.set_value<Exception>(props.mVocalMorpher.lPhonemeACoarseTuning);
        break;

    case EAXVOCALMORPHER_PHONEMEB:
        call.set_value<Exception>(props.mVocalMorpher.ulPhonemeB);
        break;

    case EAXVOCALMORPHER_PHONEMEBCOARSETUNING:
        call.set_value<Exception>(props.mVocalMorpher.lPhonemeBCoarseTuning);
        break;

    case EAXVOCALMORPHER_WAVEFORM:
        call.set_value<Exception>(props.mVocalMorpher.ulWaveform);
        break;

    case EAXVOCALMORPHER_RATE:
        call.set_value<Exception>(props.mVocalMorpher.flRate);
        break;

    default:
        fail_unknown_property_id();
    }
}

template<>
void VocalMorpherCommitter::Set(const EaxCall &call, EaxEffectProps &props)
{
    switch(call.get_property_id())
    {
    case EAXVOCALMORPHER_NONE:
        break;

    case EAXVOCALMORPHER_ALLPARAMETERS:
        defer<AllValidator>(call, props.mVocalMorpher);
        break;

    case EAXVOCALMORPHER_PHONEMEA:
        defer<PhonemeAValidator>(call, props.mVocalMorpher.ulPhonemeA);
        break;

    case EAXVOCALMORPHER_PHONEMEACOARSETUNING:
        defer<PhonemeACoarseTuningValidator>(call, props.mVocalMorpher.lPhonemeACoarseTuning);
        break;

    case EAXVOCALMORPHER_PHONEMEB:
        defer<PhonemeBValidator>(call, props.mVocalMorpher.ulPhonemeB);
        break;

    case EAXVOCALMORPHER_PHONEMEBCOARSETUNING:
        defer<PhonemeBCoarseTuningValidator>(call, props.mVocalMorpher.lPhonemeBCoarseTuning);
        break;

    case EAXVOCALMORPHER_WAVEFORM:
        defer<WaveformValidator>(call, props.mVocalMorpher.ulWaveform);
        break;

    case EAXVOCALMORPHER_RATE:
        defer<RateValidator>(call, props.mVocalMorpher.flRate);
        break;

    default:
        fail_unknown_property_id();
    }
}

#endif // ALSOFT_EAX
