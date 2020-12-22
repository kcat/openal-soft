
#include "config.h"

#include "AL/al.h"
#include "AL/efx.h"

#include "aloptional.h"
#include "effects.h"
#include "effects/base.h"


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
