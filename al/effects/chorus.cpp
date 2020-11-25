
#include "config.h"

#include "AL/al.h"
#include "AL/efx.h"

#include "effects.h"
#include "effects/base.h"


namespace {

void Chorus_setParami(EffectProps *props, ALenum param, int val)
{
    switch(param)
    {
    case AL_CHORUS_WAVEFORM:
        if(!(val >= AL_CHORUS_MIN_WAVEFORM && val <= AL_CHORUS_MAX_WAVEFORM))
            throw effect_exception{AL_INVALID_VALUE, "Invalid chorus waveform"};
        props->Chorus.Waveform = val;
        break;

    case AL_CHORUS_PHASE:
        if(!(val >= AL_CHORUS_MIN_PHASE && val <= AL_CHORUS_MAX_PHASE))
            throw effect_exception{AL_INVALID_VALUE, "Chorus phase out of range"};
        props->Chorus.Phase = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid chorus integer property 0x%04x", param};
    }
}
void Chorus_setParamiv(EffectProps *props, ALenum param, const int *vals)
{ Chorus_setParami(props, param, vals[0]); }
void Chorus_setParamf(EffectProps *props, ALenum param, float val)
{
    switch(param)
    {
    case AL_CHORUS_RATE:
        if(!(val >= AL_CHORUS_MIN_RATE && val <= AL_CHORUS_MAX_RATE))
            throw effect_exception{AL_INVALID_VALUE, "Chorus rate out of range"};
        props->Chorus.Rate = val;
        break;

    case AL_CHORUS_DEPTH:
        if(!(val >= AL_CHORUS_MIN_DEPTH && val <= AL_CHORUS_MAX_DEPTH))
            throw effect_exception{AL_INVALID_VALUE, "Chorus depth out of range"};
        props->Chorus.Depth = val;
        break;

    case AL_CHORUS_FEEDBACK:
        if(!(val >= AL_CHORUS_MIN_FEEDBACK && val <= AL_CHORUS_MAX_FEEDBACK))
            throw effect_exception{AL_INVALID_VALUE, "Chorus feedback out of range"};
        props->Chorus.Feedback = val;
        break;

    case AL_CHORUS_DELAY:
        if(!(val >= AL_CHORUS_MIN_DELAY && val <= AL_CHORUS_MAX_DELAY))
            throw effect_exception{AL_INVALID_VALUE, "Chorus delay out of range"};
        props->Chorus.Delay = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid chorus float property 0x%04x", param};
    }
}
void Chorus_setParamfv(EffectProps *props, ALenum param, const float *vals)
{ Chorus_setParamf(props, param, vals[0]); }

void Chorus_getParami(const EffectProps *props, ALenum param, int *val)
{
    switch(param)
    {
    case AL_CHORUS_WAVEFORM:
        *val = props->Chorus.Waveform;
        break;

    case AL_CHORUS_PHASE:
        *val = props->Chorus.Phase;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid chorus integer property 0x%04x", param};
    }
}
void Chorus_getParamiv(const EffectProps *props, ALenum param, int *vals)
{ Chorus_getParami(props, param, vals); }
void Chorus_getParamf(const EffectProps *props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_CHORUS_RATE:
        *val = props->Chorus.Rate;
        break;

    case AL_CHORUS_DEPTH:
        *val = props->Chorus.Depth;
        break;

    case AL_CHORUS_FEEDBACK:
        *val = props->Chorus.Feedback;
        break;

    case AL_CHORUS_DELAY:
        *val = props->Chorus.Delay;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid chorus float property 0x%04x", param};
    }
}
void Chorus_getParamfv(const EffectProps *props, ALenum param, float *vals)
{ Chorus_getParamf(props, param, vals); }

const EffectProps genDefaultChorusProps() noexcept
{
    EffectProps props{};
    props.Chorus.Waveform = AL_CHORUS_DEFAULT_WAVEFORM;
    props.Chorus.Phase = AL_CHORUS_DEFAULT_PHASE;
    props.Chorus.Rate = AL_CHORUS_DEFAULT_RATE;
    props.Chorus.Depth = AL_CHORUS_DEFAULT_DEPTH;
    props.Chorus.Feedback = AL_CHORUS_DEFAULT_FEEDBACK;
    props.Chorus.Delay = AL_CHORUS_DEFAULT_DELAY;
    return props;
}


void Flanger_setParami(EffectProps *props, ALenum param, int val)
{
    switch(param)
    {
    case AL_FLANGER_WAVEFORM:
        if(!(val >= AL_FLANGER_MIN_WAVEFORM && val <= AL_FLANGER_MAX_WAVEFORM))
            throw effect_exception{AL_INVALID_VALUE, "Invalid flanger waveform"};
        props->Chorus.Waveform = val;
        break;

    case AL_FLANGER_PHASE:
        if(!(val >= AL_FLANGER_MIN_PHASE && val <= AL_FLANGER_MAX_PHASE))
            throw effect_exception{AL_INVALID_VALUE, "Flanger phase out of range"};
        props->Chorus.Phase = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid flanger integer property 0x%04x", param};
    }
}
void Flanger_setParamiv(EffectProps *props, ALenum param, const int *vals)
{ Flanger_setParami(props, param, vals[0]); }
void Flanger_setParamf(EffectProps *props, ALenum param, float val)
{
    switch(param)
    {
    case AL_FLANGER_RATE:
        if(!(val >= AL_FLANGER_MIN_RATE && val <= AL_FLANGER_MAX_RATE))
            throw effect_exception{AL_INVALID_VALUE, "Flanger rate out of range"};
        props->Chorus.Rate = val;
        break;

    case AL_FLANGER_DEPTH:
        if(!(val >= AL_FLANGER_MIN_DEPTH && val <= AL_FLANGER_MAX_DEPTH))
            throw effect_exception{AL_INVALID_VALUE, "Flanger depth out of range"};
        props->Chorus.Depth = val;
        break;

    case AL_FLANGER_FEEDBACK:
        if(!(val >= AL_FLANGER_MIN_FEEDBACK && val <= AL_FLANGER_MAX_FEEDBACK))
            throw effect_exception{AL_INVALID_VALUE, "Flanger feedback out of range"};
        props->Chorus.Feedback = val;
        break;

    case AL_FLANGER_DELAY:
        if(!(val >= AL_FLANGER_MIN_DELAY && val <= AL_FLANGER_MAX_DELAY))
            throw effect_exception{AL_INVALID_VALUE, "Flanger delay out of range"};
        props->Chorus.Delay = val;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid flanger float property 0x%04x", param};
    }
}
void Flanger_setParamfv(EffectProps *props, ALenum param, const float *vals)
{ Flanger_setParamf(props, param, vals[0]); }

void Flanger_getParami(const EffectProps *props, ALenum param, int *val)
{
    switch(param)
    {
    case AL_FLANGER_WAVEFORM:
        *val = props->Chorus.Waveform;
        break;

    case AL_FLANGER_PHASE:
        *val = props->Chorus.Phase;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid flanger integer property 0x%04x", param};
    }
}
void Flanger_getParamiv(const EffectProps *props, ALenum param, int *vals)
{ Flanger_getParami(props, param, vals); }
void Flanger_getParamf(const EffectProps *props, ALenum param, float *val)
{
    switch(param)
    {
    case AL_FLANGER_RATE:
        *val = props->Chorus.Rate;
        break;

    case AL_FLANGER_DEPTH:
        *val = props->Chorus.Depth;
        break;

    case AL_FLANGER_FEEDBACK:
        *val = props->Chorus.Feedback;
        break;

    case AL_FLANGER_DELAY:
        *val = props->Chorus.Delay;
        break;

    default:
        throw effect_exception{AL_INVALID_ENUM, "Invalid flanger float property 0x%04x", param};
    }
}
void Flanger_getParamfv(const EffectProps *props, ALenum param, float *vals)
{ Flanger_getParamf(props, param, vals); }

EffectProps genDefaultFlangerProps() noexcept
{
    EffectProps props{};
    props.Chorus.Waveform = AL_FLANGER_DEFAULT_WAVEFORM;
    props.Chorus.Phase = AL_FLANGER_DEFAULT_PHASE;
    props.Chorus.Rate = AL_FLANGER_DEFAULT_RATE;
    props.Chorus.Depth = AL_FLANGER_DEFAULT_DEPTH;
    props.Chorus.Feedback = AL_FLANGER_DEFAULT_FEEDBACK;
    props.Chorus.Delay = AL_FLANGER_DEFAULT_DELAY;
    return props;
}

} // namespace

DEFINE_ALEFFECT_VTABLE(Chorus);

const EffectProps ChorusEffectProps{genDefaultChorusProps()};

DEFINE_ALEFFECT_VTABLE(Flanger);

const EffectProps FlangerEffectProps{genDefaultFlangerProps()};
