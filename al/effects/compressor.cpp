
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

class EaxCompressorEffectException : public EaxException
{
public:
    explicit EaxCompressorEffectException(const char* message)
        : EaxException{"EAX_COMPRESSOR_EFFECT", message}
    {}
}; // EaxCompressorEffectException

class EaxCompressorEffect final : public EaxEffect4<EaxCompressorEffectException, EAXAGCCOMPRESSORPROPERTIES>
{
public:
    EaxCompressorEffect(const EaxCall& call);

private:
    struct OnOffValidator {
        void operator()(unsigned long ulOnOff) const
        {
            eax_validate_range<Exception>(
                "On-Off",
                ulOnOff,
                EAXAGCCOMPRESSOR_MINONOFF,
                EAXAGCCOMPRESSOR_MAXONOFF);
        }
    }; // OnOffValidator

    struct AllValidator {
        void operator()(const Props& all) const
        {
            OnOffValidator{}(all.ulOnOff);
        }
    }; // AllValidator

    void set_defaults(Props& props) override;

    void set_efx_on_off() noexcept;
    void set_efx_defaults() override;

    void get(const EaxCall& call, const Props& props) override;
    void set(const EaxCall& call, Props& props) override;
    bool commit_props(const Props& props) override;
}; // EaxCompressorEffect

EaxCompressorEffect::EaxCompressorEffect(const EaxCall& call)
    : EaxEffect4{AL_EFFECT_COMPRESSOR, call}
{}

void EaxCompressorEffect::set_defaults(Props& props)
{
    props.ulOnOff = EAXAGCCOMPRESSOR_DEFAULTONOFF;
}

void EaxCompressorEffect::set_efx_on_off() noexcept
{
    const auto on_off = clamp(
        static_cast<ALint>(props_.ulOnOff),
        AL_COMPRESSOR_MIN_ONOFF,
        AL_COMPRESSOR_MAX_ONOFF);
    al_effect_props_.Compressor.OnOff = (on_off != AL_FALSE);
}

void EaxCompressorEffect::set_efx_defaults()
{
    set_efx_on_off();
}

void EaxCompressorEffect::get(const EaxCall& call, const Props& props)
{
    switch(call.get_property_id())
    {
        case EAXAGCCOMPRESSOR_NONE: break;
        case EAXAGCCOMPRESSOR_ALLPARAMETERS: call.set_value<Exception>(props); break;
        case EAXAGCCOMPRESSOR_ONOFF: call.set_value<Exception>(props.ulOnOff); break;
        default: fail_unknown_property_id();
    }
}

void EaxCompressorEffect::set(const EaxCall& call, Props& props)
{
    switch(call.get_property_id())
    {
        case EAXAGCCOMPRESSOR_NONE: break;
        case EAXAGCCOMPRESSOR_ALLPARAMETERS: defer<AllValidator>(call, props); break;
        case EAXAGCCOMPRESSOR_ONOFF: defer<OnOffValidator>(call, props.ulOnOff); break;
        default: fail_unknown_property_id();
    }
}

bool EaxCompressorEffect::commit_props(const Props& props)
{
    auto is_dirty = false;

    if (props_.ulOnOff != props.ulOnOff)
    {
        is_dirty = true;
        set_efx_on_off();
    }

    return is_dirty;
}

} // namespace

EaxEffectUPtr eax_create_eax_compressor_effect(const EaxCall& call)
{
    return eax_create_eax4_effect<EaxCompressorEffect>(call);
}

#endif // ALSOFT_EAX
