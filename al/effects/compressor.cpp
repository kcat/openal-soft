
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

consteval auto genDefaultProps() noexcept -> EffectProps
{
    return CompressorProps{.OnOff = AL_COMPRESSOR_DEFAULT_ONOFF};
}

} // namespace

constinit const EffectProps CompressorEffectProps(genDefaultProps());

void CompressorEffectHandler::SetParami(al::Context *context, CompressorProps &props, ALenum param, int val)
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
void CompressorEffectHandler::SetParamiv(al::Context *context, CompressorProps &props, ALenum param, const int *vals)
{ SetParami(context, props, param, *vals); }
void CompressorEffectHandler::SetParamf(al::Context *context, CompressorProps&, ALenum param, float)
{ context->throw_error(AL_INVALID_ENUM, "Invalid compressor float property {:#04x}", as_unsigned(param)); }
void CompressorEffectHandler::SetParamfv(al::Context *context, CompressorProps&, ALenum param, const float*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid compressor float-vector property {:#04x}", as_unsigned(param)); }

void CompressorEffectHandler::GetParami(al::Context *context, const CompressorProps &props, ALenum param, int *val)
{ 
    switch(param)
    {
    case AL_COMPRESSOR_ONOFF: *val = props.OnOff; return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid compressor integer property {:#04x}",
        as_unsigned(param));
}
void CompressorEffectHandler::GetParamiv(al::Context *context, const CompressorProps &props, ALenum param, int *vals)
{ GetParami(context, props, param, vals); }
void CompressorEffectHandler::GetParamf(al::Context *context, const CompressorProps&, ALenum param, float*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid compressor float property {:#04x}", as_unsigned(param)); }
void CompressorEffectHandler::GetParamfv(al::Context *context, const CompressorProps&, ALenum param, float*)
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

template<> /* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
struct CompressorCommitter::Exception : public EaxException {
    explicit Exception(const std::string_view message) : EaxException{"EAX_CHORUS_EFFECT", message}
    { }
};

template<> [[noreturn]]
void CompressorCommitter::fail(const std::string_view message)
{ throw Exception{message}; }

auto EaxCompressorCommitter::commit(const EAXAGCCOMPRESSORPROPERTIES &props) const -> bool
{
    if(auto *cur = std::get_if<EAXAGCCOMPRESSORPROPERTIES>(&mEaxProps); cur && *cur == props)
        return false;

    mEaxProps = props;
    mAlProps = CompressorProps{.OnOff = props.ulOnOff != 0};

    return true;
}

void EaxCompressorCommitter::SetDefaults(EaxEffectProps &props)
{
    props = EAXAGCCOMPRESSORPROPERTIES{.ulOnOff = EAXAGCCOMPRESSOR_DEFAULTONOFF};
}

void EaxCompressorCommitter::Get(const EaxCall &call, const EAXAGCCOMPRESSORPROPERTIES &props)
{
    switch(call.get_property_id())
    {
    case EAXAGCCOMPRESSOR_NONE: break;
    case EAXAGCCOMPRESSOR_ALLPARAMETERS: call.store(props); break;
    case EAXAGCCOMPRESSOR_ONOFF: call.store(props.ulOnOff); break;
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
