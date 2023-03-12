
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

void Compressor_setParami(EffectProps *props, ALenum param, int val)
{
    switch(param)
    {
    case AL_COMPRESSOR_ONOFF:
        if(!(val >= AL_COMPRESSOR_MIN_ONOFF && val <= AL_COMPRESSOR_MAX_ONOFF))
            throw effect_exception{AL_INVALID_VALUE, "Compressor state out of range"};
        props->Compressor.OnOff = (val != AL_FALSE);
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid compressor integer property 0x%04x",
            param};
    }
}
void Compressor_setParamiv(EffectProps *props, ALenum param, const int *vals)
{ Compressor_setParami(props, param, vals[0]); }
void Compressor_setParamf(EffectProps*, ALenum param, float)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid compressor float property 0x%04x", param}; }
void Compressor_setParamfv(EffectProps*, ALenum param, const float*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid compressor float-vector property 0x%04x",
        param};
}

void Compressor_getParami(const EffectProps *props, ALenum param, int *val)
{ 
    switch(param)
    {
    case AL_COMPRESSOR_ONOFF:
        *val = props->Compressor.OnOff;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid compressor integer property 0x%04x",
            param};
    }
}
void Compressor_getParamiv(const EffectProps *props, ALenum param, int *vals)
{ Compressor_getParami(props, param, vals); }
void Compressor_getParamf(const EffectProps*, ALenum param, float*)
{ throw effect_exception{AL_INVALID_ENUM, "Invalid compressor float property 0x%04x", param}; }
void Compressor_getParamfv(const EffectProps*, ALenum param, float*)
{
    throw effect_exception{AL_INVALID_ENUM, "Invalid compressor float-vector property 0x%04x",
        param};
}

EffectProps genDefaultProps() noexcept
{
    EffectProps props{};
    props.Compressor.OnOff = AL_COMPRESSOR_DEFAULT_ONOFF;
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Compressor);

const EffectProps CompressorEffectProps{genDefaultProps()};

#ifdef ALSOFT_EAX
namespace {

using CompressorCommitter = EaxCommitter<EaxCompressorCommitter>;

struct OnOffValidator {
    void operator()(unsigned long ulOnOff) const
    {
        eax_validate_range<CompressorCommitter::Exception>(
            "On-Off",
            ulOnOff,
            EAXAGCCOMPRESSOR_MINONOFF,
            EAXAGCCOMPRESSOR_MAXONOFF);
    }
}; // OnOffValidator

struct AllValidator {
    void operator()(const EAXAGCCOMPRESSORPROPERTIES& all) const
    {
        OnOffValidator{}(all.ulOnOff);
    }
}; // AllValidator

} // namespace

template<>
struct CompressorCommitter::Exception : public EaxException
{
    explicit Exception(const char *message) : EaxException{"EAX_CHORUS_EFFECT", message}
    { }
};

template<>
[[noreturn]] void CompressorCommitter::fail(const char *message)
{
    throw Exception{message};
}

template<>
bool CompressorCommitter::commit(const EaxEffectProps &props)
{
    if(props.mType == mEaxProps.mType
        && props.mCompressor.ulOnOff == mEaxProps.mCompressor.ulOnOff)
        return false;

    mEaxProps = props;

    mAlProps.Compressor.OnOff = (props.mCompressor.ulOnOff != 0);
    return true;
}

template<>
void CompressorCommitter::SetDefaults(EaxEffectProps &props)
{
    props.mType = EaxEffectType::Compressor;
    props.mCompressor.ulOnOff = EAXAGCCOMPRESSOR_DEFAULTONOFF;
}

template<>
void CompressorCommitter::Get(const EaxCall &call, const EaxEffectProps &props)
{
    switch(call.get_property_id())
    {
    case EAXAGCCOMPRESSOR_NONE: break;
    case EAXAGCCOMPRESSOR_ALLPARAMETERS: call.set_value<Exception>(props.mCompressor); break;
    case EAXAGCCOMPRESSOR_ONOFF: call.set_value<Exception>(props.mCompressor.ulOnOff); break;
    default: fail_unknown_property_id();
    }
}

template<>
void CompressorCommitter::Set(const EaxCall &call, EaxEffectProps &props)
{
    switch(call.get_property_id())
    {
    case EAXAGCCOMPRESSOR_NONE: break;
    case EAXAGCCOMPRESSOR_ALLPARAMETERS: defer<AllValidator>(call, props.mCompressor); break;
    case EAXAGCCOMPRESSOR_ONOFF: defer<OnOffValidator>(call, props.mCompressor.ulOnOff); break;
    default: fail_unknown_property_id();
    }
}

#endif // ALSOFT_EAX
