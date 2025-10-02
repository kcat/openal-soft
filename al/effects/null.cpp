
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

consteval auto genDefaultProps() noexcept -> EffectProps
{
    return std::monostate{};
}

} // namespace

constinit const EffectProps NullEffectProps(genDefaultProps());

void NullEffectHandler::SetParami(al::Context *context, std::monostate& /*props*/, ALenum param, int /*val*/)
{
    context->throw_error(AL_INVALID_ENUM, "Invalid null effect integer property {:#04x}",
        as_unsigned(param));
}
void NullEffectHandler::SetParamiv(al::Context *context, std::monostate &props, ALenum param, const int *vals)
{
    SetParami(context, props, param, *vals);
}
void NullEffectHandler::SetParamf(al::Context *context, std::monostate& /*props*/, ALenum param, float /*val*/)
{
    context->throw_error(AL_INVALID_ENUM, "Invalid null effect float property {:#04x}",
        as_unsigned(param));
}
void NullEffectHandler::SetParamfv(al::Context *context, std::monostate &props, ALenum param, const float *vals)
{
    SetParamf(context, props, param, *vals);
}

void NullEffectHandler::GetParami(al::Context *context, const std::monostate& /*props*/, ALenum param, int* /*val*/)
{
    context->throw_error(AL_INVALID_ENUM, "Invalid null effect integer property {:#04x}",
        as_unsigned(param));
}
void NullEffectHandler::GetParamiv(al::Context *context, const std::monostate &props, ALenum param, int *vals)
{
    GetParami(context, props, param, vals);
}
void NullEffectHandler::GetParamf(al::Context *context, const std::monostate& /*props*/, ALenum param, float* /*val*/)
{
    context->throw_error(AL_INVALID_ENUM, "Invalid null effect float property {:#04x}",
        as_unsigned(param));
}
void NullEffectHandler::GetParamfv(al::Context *context, const std::monostate &props, ALenum param, float *vals)
{
    GetParamf(context, props, param, vals);
}


#if ALSOFT_EAX
namespace {

using NullCommitter = EaxCommitter<EaxNullCommitter>;

} // namespace

template<> /* NOLINTNEXTLINE(clazy-copyable-polymorphic) Exceptions must be copyable. */
struct NullCommitter::Exception : public EaxException {
    explicit Exception(const std::string_view message) : EaxException{"EAX_NULL_EFFECT", message}
    { }
};

template<> [[noreturn]]
void NullCommitter::fail(const std::string_view message)
{ throw Exception{message}; }

auto EaxNullCommitter::commit(const std::monostate &props) const -> bool
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
