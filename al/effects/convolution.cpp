
#include "config.h"

#include <cmath>

#include "AL/al.h"
#include "alc/inprogext.h"

#include "alc/effects/base.h"
#include "effects.h"


namespace {

void Convolution_setParami(EffectProps* /*props*/, ALenum param, int /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid null effect integer property 0x%04x",
            param};
    }
}
void Convolution_setParamiv(EffectProps *props, ALenum param, const int *vals)
{
    switch(param)
    {
    default:
        Convolution_setParami(props, param, vals[0]);
    }
}
void Convolution_setParamf(EffectProps* /*props*/, ALenum param, float /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid null effect float property 0x%04x",
            param};
    }
}
void Convolution_setParamfv(EffectProps *props, ALenum param, const float *values)
{
    switch(param)
    {
    case AL_CONVOLUTION_ORIENTATION_SOFT:
        if(!(std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2])
            && std::isfinite(values[3]) && std::isfinite(values[4]) && std::isfinite(values[5])))
            throw effect_exception{AL_INVALID_VALUE, "Property 0x%04x value out of range", param};

        props->Convolution.OrientAt[0] = values[0];
        props->Convolution.OrientAt[1] = values[1];
        props->Convolution.OrientAt[2] = values[2];
        props->Convolution.OrientUp[0] = values[3];
        props->Convolution.OrientUp[1] = values[4];
        props->Convolution.OrientUp[2] = values[5];
        break;

    default:
        Convolution_setParamf(props, param, values[0]);
    }
}

void Convolution_getParami(const EffectProps* /*props*/, ALenum param, int* /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid null effect integer property 0x%04x",
            param};
    }
}
void Convolution_getParamiv(const EffectProps *props, ALenum param, int *vals)
{
    switch(param)
    {
    default:
        Convolution_getParami(props, param, vals);
    }
}
void Convolution_getParamf(const EffectProps* /*props*/, ALenum param, float* /*val*/)
{
    switch(param)
    {
    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid null effect float property 0x%04x",
            param};
    }
}
void Convolution_getParamfv(const EffectProps *props, ALenum param, float *values)
{
    switch(param)
    {
    case AL_CONVOLUTION_ORIENTATION_SOFT:
        values[0] = props->Convolution.OrientAt[0];
        values[1] = props->Convolution.OrientAt[1];
        values[2] = props->Convolution.OrientAt[2];
        values[3] = props->Convolution.OrientUp[0];
        values[4] = props->Convolution.OrientUp[1];
        values[5] = props->Convolution.OrientUp[2];
        break;

    default:
        Convolution_getParamf(props, param, values);
    }
}

EffectProps genDefaultProps() noexcept
{
    EffectProps props{};
    props.Convolution.OrientAt = {0.0f,  0.0f, -1.0f};
    props.Convolution.OrientUp = {0.0f,  1.0f,  0.0f};
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Convolution);

const EffectProps ConvolutionEffectProps{genDefaultProps()};
