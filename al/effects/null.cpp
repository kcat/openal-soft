
#include "config.h"

#include "AL/al.h"
#include "AL/efx.h"

#include "alc/context.h"
#include "alnumeric.h"
#include "effects.h"

#if ALSOFT_EAX
#include "al/eax/effect.h"
#include "al/eax/exception.h"
#endif // ALSOFT_EAX


namespace {

constexpr EffectProps genDefaultProps() noexcept
{
    return std::monostate{};
}

} // namespace

const EffectProps NullEffectProps{genDefaultProps()};

void NullEffectHandler::SetParami(ALCcontext *context, std::monostate& /*props*/, ALenum param, int /*val*/)
{
    context->throw_error(AL_INVALID_ENUM, "Invalid null effect integer property {:#04x}",
        as_unsigned(param));
}
void NullEffectHandler::SetParamiv(ALCcontext *context, std::monostate &props, ALenum param, const int *vals)
{
    SetParami(context, props, param, *vals);
}
void NullEffectHandler::SetParamf(ALCcontext *context, std::monostate& /*props*/, ALenum param, float /*val*/)
{
    context->throw_error(AL_INVALID_ENUM, "Invalid null effect float property {:#04x}",
        as_unsigned(param));
}
void NullEffectHandler::SetParamfv(ALCcontext *context, std::monostate &props, ALenum param, const float *vals)
{
    SetParamf(context, props, param, *vals);
}

void NullEffectHandler::GetParami(ALCcontext *context, const std::monostate& /*props*/, ALenum param, int* /*val*/)
{
    context->throw_error(AL_INVALID_ENUM, "Invalid null effect integer property {:#04x}",
        as_unsigned(param));
}
void NullEffectHandler::GetParamiv(ALCcontext *context, const std::monostate &props, ALenum param, int *vals)
{
    GetParami(context, props, param, vals);
}
void NullEffectHandler::GetParamf(ALCcontext *context, const std::monostate& /*props*/, ALenum param, float* /*val*/)
{
    context->throw_error(AL_INVALID_ENUM, "Invalid null effect float property {:#04x}",
        as_unsigned(param));
}
void NullEffectHandler::GetParamfv(ALCcontext *context, const std::monostate &props, ALenum param, float *vals)
{
    GetParamf(context, props, param, vals);
}


#if ALSOFT_EAX
namespace {

using NullCommitter = EaxCommitter<EaxNullCommitter>;

} // namespace

template<>
struct NullCommitter::Exception : public EaxException
{
    explicit Exception(const char *message) : EaxException{"EAX_NULL_EFFECT", message}
    { }
};

template<>
[[noreturn]] void NullCommitter::fail(const char *message)
{
    throw Exception{message};
}

bool EaxNullCommitter::commit(const std::monostate &props)
{
    const bool ret{std::holds_alternative<std::monostate>(mEaxProps)};
    mEaxProps = props;
    mAlProps = std::monostate{};
    return ret;
}

void EaxNullCommitter::SetDefaults(EaxEffectProps &props)
{
    props = std::monostate{};
}

void EaxNullCommitter::Get(const EaxCall &call, const std::monostate&)
{
    if(call.get_property_id() != 0)
        fail_unknown_property_id();
}

void EaxNullCommitter::Set(const EaxCall &call, std::monostate&)
{
    if(call.get_property_id() != 0)
        fail_unknown_property_id();
}

#endif // ALSOFT_EAX
