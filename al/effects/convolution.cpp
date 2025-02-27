
#include "config.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "AL/al.h"

#include "alc/context.h"
#include "alc/inprogext.h"
#include "alnumeric.h"
#include "alspan.h"
#include "effects.h"


namespace {

constexpr EffectProps genDefaultProps() noexcept
{
    ConvolutionProps props{};
    props.OrientAt = {0.0f,  0.0f, -1.0f};
    props.OrientUp = {0.0f,  1.0f,  0.0f};
    return props;
}

} // namespace

const EffectProps ConvolutionEffectProps{genDefaultProps()};

void ConvolutionEffectHandler::SetParami(ALCcontext *context, ConvolutionProps& /*props*/, ALenum param, int /*val*/)
{ context->throw_error(AL_INVALID_ENUM, "Invalid convolution effect integer property {:#04x}", as_unsigned(param)); }
void ConvolutionEffectHandler::SetParamiv(ALCcontext *context, ConvolutionProps &props, ALenum param, const int *vals)
{ SetParami(context, props, param, *vals); }

void ConvolutionEffectHandler::SetParamf(ALCcontext *context, ConvolutionProps& /*props*/, ALenum param, float /*val*/)
{ context->throw_error(AL_INVALID_ENUM, "Invalid convolution effect float property {:#04x}", as_unsigned(param)); }
void ConvolutionEffectHandler::SetParamfv(ALCcontext *context, ConvolutionProps &props, ALenum param, const float *values)
{
    static constexpr auto finite_checker = [](float val) -> bool { return std::isfinite(val); };

    switch(param)
    {
    case AL_CONVOLUTION_ORIENTATION_SOFT:
        auto vals = al::span{values, 6_uz};
        if(!std::all_of(vals.cbegin(), vals.cend(), finite_checker))
            context->throw_error(AL_INVALID_VALUE, "Convolution orientation out of range", param);

        std::copy_n(vals.cbegin(), props.OrientAt.size(), props.OrientAt.begin());
        std::copy_n(vals.cbegin()+3, props.OrientUp.size(), props.OrientUp.begin());
        return;
    }

    SetParamf(context, props, param, *values);
}

void ConvolutionEffectHandler::GetParami(ALCcontext *context, const ConvolutionProps& /*props*/, ALenum param, int* /*val*/)
{ context->throw_error(AL_INVALID_ENUM, "Invalid convolution effect integer property {:#04x}", as_unsigned(param)); }
void ConvolutionEffectHandler::GetParamiv(ALCcontext *context, const ConvolutionProps &props, ALenum param, int *vals)
{ GetParami(context, props, param, vals); }

void ConvolutionEffectHandler::GetParamf(ALCcontext *context, const ConvolutionProps& /*props*/, ALenum param, float* /*val*/)
{ context->throw_error(AL_INVALID_ENUM, "Invalid convolution effect float property {:#04x}", as_unsigned(param)); }
void ConvolutionEffectHandler::GetParamfv(ALCcontext *context, const ConvolutionProps &props, ALenum param, float *values)
{
    switch(param)
    {
    case AL_CONVOLUTION_ORIENTATION_SOFT:
        auto vals = al::span{values, 6_uz};
        std::copy(props.OrientAt.cbegin(), props.OrientAt.cend(), vals.begin());
        std::copy(props.OrientUp.cbegin(), props.OrientUp.cend(), vals.begin()+3);
        return;
    }

    GetParamf(context, props, param, values);
}
