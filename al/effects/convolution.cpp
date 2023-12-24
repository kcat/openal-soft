
#include "config.h"

#include <cmath>

#include "AL/al.h"
#include "alc/inprogext.h"

#include "alc/effects/base.h"
#include "effects.h"


namespace {

EffectProps genDefaultProps() noexcept
{
    ConvolutionProps props{};
    props.OrientAt = {0.0f,  0.0f, -1.0f};
    props.OrientUp = {0.0f,  1.0f,  0.0f};
    return props;
}

} // namespace

const EffectProps ConvolutionEffectProps{genDefaultProps()};

void EffectHandler::SetParami(ConvolutionProps& /*props*/, ALenum param, int /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid convolution effect integer property 0x%04x",
            param};
    }
}
void EffectHandler::SetParamiv(ConvolutionProps &props, ALenum param, const int *vals)
{
    switch(param)
    {
    default:
        SetParami(props, param, vals[0]);
    }
}
void EffectHandler::SetParamf(ConvolutionProps& /*props*/, ALenum param, float /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid convolution effect float property 0x%04x",
            param};
    }
}
void EffectHandler::SetParamfv(ConvolutionProps &props, ALenum param, const float *values)
{
    switch(param)
    {
    case AL_CONVOLUTION_ORIENTATION_SOFT:
        if(!(std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2])
            && std::isfinite(values[3]) && std::isfinite(values[4]) && std::isfinite(values[5])))
            throw effect_exception{AL_INVALID_VALUE, "Property 0x%04x value out of range", param};

        props.OrientAt[0] = values[0];
        props.OrientAt[1] = values[1];
        props.OrientAt[2] = values[2];
        props.OrientUp[0] = values[3];
        props.OrientUp[1] = values[4];
        props.OrientUp[2] = values[5];
        break;

    default:
        SetParamf(props, param, values[0]);
    }
}

void EffectHandler::GetParami(const ConvolutionProps& /*props*/, ALenum param, int* /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid convolution effect integer property 0x%04x",
            param};
    }
}
void EffectHandler::GetParamiv(const ConvolutionProps &props, ALenum param, int *vals)
{
    switch(param)
    {
    default:
        GetParami(props, param, vals);
    }
}
void EffectHandler::GetParamf(const ConvolutionProps& /*props*/, ALenum param, float* /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid convolution effect float property 0x%04x",
            param};
    }
}
void EffectHandler::GetParamfv(const ConvolutionProps &props, ALenum param, float *values)
{
    switch(param)
    {
    case AL_CONVOLUTION_ORIENTATION_SOFT:
        values[0] = props.OrientAt[0];
        values[1] = props.OrientAt[1];
        values[2] = props.OrientAt[2];
        values[3] = props.OrientUp[0];
        values[4] = props.OrientUp[1];
        values[5] = props.OrientUp[2];
        break;

    default:
        GetParamf(props, param, values);
    }
}
