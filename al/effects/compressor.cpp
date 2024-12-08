
#include "config.h"

#include "AL/al.h"
#include "AL/efx.h"

#include "alc/context.h"
#include "alnumeric.h"
#include "effects.h"

#if ALSOFT_EAX
#include "al/eax/effect.h"
#include "al/eax/exception.h"
#include "al/eax/utils.h"
#endif // ALSOFT_EAX


namespace {

constexpr EffectProps genDefaultProps() noexcept
{
    CompressorProps props{};
    props.OnOff = AL_COMPRESSOR_DEFAULT_ONOFF;
    return props;
}

} // namespace

const EffectProps CompressorEffectProps{genDefaultProps()};

void CompressorEffectHandler::SetParami(ALCcontext *context, CompressorProps &props, ALenum param, int val)
{
    switch(param)
    {
    case AL_COMPRESSOR_ONOFF:
        if(!(val >= AL_COMPRESSOR_MIN_ONOFF && val <= AL_COMPRESSOR_MAX_ONOFF))
            context->throw_error(AL_INVALID_VALUE, "Compressor state out of range");
        props.OnOff = (val != AL_FALSE);
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid compressor integer property {:#04x}",
        as_unsigned(param));
}
void CompressorEffectHandler::SetParamiv(ALCcontext *context, CompressorProps &props, ALenum param, const int *vals)
{ SetParami(context, props, param, *vals); }
void CompressorEffectHandler::SetParamf(ALCcontext *context, CompressorProps&, ALenum param, float)
{ context->throw_error(AL_INVALID_ENUM, "Invalid compressor float property {:#04x}", as_unsigned(param)); }
void CompressorEffectHandler::SetParamfv(ALCcontext *context, CompressorProps&, ALenum param, const float*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid compressor float-vector property {:#04x}", as_unsigned(param)); }

void CompressorEffectHandler::GetParami(ALCcontext *context, const CompressorProps &props, ALenum param, int *val)
{ 
    switch(param)
    {
    case AL_COMPRESSOR_ONOFF: *val = props.OnOff; return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid compressor integer property {:#04x}",
        as_unsigned(param));
}
void CompressorEffectHandler::GetParamiv(ALCcontext *context, const CompressorProps &props, ALenum param, int *vals)
{ GetParami(context, props, param, vals); }
void CompressorEffectHandler::GetParamf(ALCcontext *context, const CompressorProps&, ALenum param, float*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid compressor float property {:#04x}", as_unsigned(param)); }
void CompressorEffectHandler::GetParamfv(ALCcontext *context, const CompressorProps&, ALenum param, float*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid compressor float-vector property {:#04x}", as_unsigned(param)); }


#if ALSOFT_EAX
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

bool EaxCompressorCommitter::commit(const EAXAGCCOMPRESSORPROPERTIES &props)
{
    if(auto *cur = std::get_if<EAXAGCCOMPRESSORPROPERTIES>(&mEaxProps); cur && *cur == props)
        return false;

    mEaxProps = props;
    mAlProps = CompressorProps{props.ulOnOff != 0};

    return true;
}

void EaxCompressorCommitter::SetDefaults(EaxEffectProps &props)
{
    props = EAXAGCCOMPRESSORPROPERTIES{EAXAGCCOMPRESSOR_DEFAULTONOFF};
}

void EaxCompressorCommitter::Get(const EaxCall &call, const EAXAGCCOMPRESSORPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case EAXAGCCOMPRESSOR_NONE: break;
    case EAXAGCCOMPRESSOR_ALLPARAMETERS: call.set_value<Exception>(props); break;
    case EAXAGCCOMPRESSOR_ONOFF: call.set_value<Exception>(props.ulOnOff); break;
    default: fail_unknown_property_id();
    }
}

void EaxCompressorCommitter::Set(const EaxCall &call, EAXAGCCOMPRESSORPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case EAXAGCCOMPRESSOR_NONE: break;
    case EAXAGCCOMPRESSOR_ALLPARAMETERS: defer<AllValidator>(call, props); break;
    case EAXAGCCOMPRESSOR_ONOFF: defer<OnOffValidator>(call, props.ulOnOff); break;
    default: fail_unknown_property_id();
    }
}

#endif // ALSOFT_EAX
