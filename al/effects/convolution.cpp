
#include "config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

#include "AL/al.h"

#include "alc/context.h"
#include "alc/inprogext.h"
#include "alnumeric.h"
#include "effects.h"


namespace {

consteval auto genDefaultProps() noexcept -> EffectProps
{
    return ConvolutionProps{
        .OrientAt = {0.0f,  0.0f, -1.0f},
        .OrientUp = {0.0f,  1.0f,  0.0f}};
}

} // namespace

constinit const EffectProps ConvolutionEffectProps(genDefaultProps());

void ConvolutionEffectHandler::SetParami(ALCcontext *context, ConvolutionProps& /*props*/, ALenum param, int /*val*/)
{ context->throw_error(AL_INVALID_ENUM, "Invalid convolution effect integer property {:#04x}", as_unsigned(param)); }
void ConvolutionEffectHandler::SetParamiv(ALCcontext *context, ConvolutionProps &props, ALenum param, const int *vals)
{ SetParami(context, props, param, *vals); }

void ConvolutionEffectHandler::SetParamf(ALCcontext *context, ConvolutionProps& /*props*/, ALenum param, float /*val*/)
{ context->throw_error(AL_INVALID_ENUM, "Invalid convolution effect float property {:#04x}", as_unsigned(param)); }
void ConvolutionEffectHandler::SetParamfv(ALCcontext *context, ConvolutionProps &props, ALenum param, const float *values)
{
    static constexpr auto is_finite = [](float val) -> bool { return std::isfinite(val); };

    switch(param)
    {
    case AL_CONVOLUTION_ORIENTATION_SOFT:
        const auto vals = std::span{values, 6_uz};
        if(!std::ranges::all_of(vals, is_finite))
            context->throw_error(AL_INVALID_VALUE, "Convolution orientation out of range", param);

        std::copy_n(vals.begin(), props.OrientAt.size(), props.OrientAt.begin());
        std::copy_n(vals.begin()+3, props.OrientUp.size(), props.OrientUp.begin());
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
        const auto vals = std::span{values, 6_uz};
        const auto oiter = std::ranges::copy(props.OrientAt, vals.begin()).out;
        std::ranges::copy(props.OrientUp, oiter);
        return;
    }

    GetParamf(context, props, param, values);
}
