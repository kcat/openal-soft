#if ALSOFT_EAX


#include <cassert>

#include "AL/efx.h"

#include "effects.h"


EaxEffectUPtr eax_create_eax_null_effect();

EaxEffectUPtr eax_create_eax_chorus_effect(
    EffectProps& al_effect_props);

EaxEffectUPtr eax_create_eax_distortion_effect(
    EffectProps& al_effect_props);

EaxEffectUPtr eax_create_eax_echo_effect(
    EffectProps& al_effect_props);

EaxEffectUPtr eax_create_eax_flanger_effect(
    EffectProps& al_effect_props);

EaxEffectUPtr eax_create_eax_frequency_shifter_effect(
    EffectProps& al_effect_props);

EaxEffectUPtr eax_create_eax_vocal_morpher_effect(
    EffectProps& al_effect_props);

EaxEffectUPtr eax_create_eax_pitch_shifter_effect(
    EffectProps& al_effect_props);

EaxEffectUPtr eax_create_eax_ring_modulator_effect(
    EffectProps& al_effect_props);

EaxEffectUPtr eax_create_eax_auto_wah_effect(
    EffectProps& al_effect_props);

EaxEffectUPtr eax_create_eax_compressor_effect(
    EffectProps& al_effect_props);

EaxEffectUPtr eax_create_eax_equalizer_effect(
    EffectProps& al_effect_props);

EaxEffectUPtr eax_create_eax_reverb_effect(
    EffectProps& al_effect_props);


EaxEffectUPtr eax_create_eax_effect(
    ALenum al_effect_type,
    EffectProps& al_effect_props)
{
#define EAX_PREFIX "[EAX_MAKE_EAX_EFFECT] "

    switch (al_effect_type)
    {
        case AL_EFFECT_NULL:
            return eax_create_eax_null_effect();

        case AL_EFFECT_CHORUS:
            return eax_create_eax_chorus_effect(al_effect_props);

        case AL_EFFECT_DISTORTION:
            return eax_create_eax_distortion_effect(al_effect_props);

        case AL_EFFECT_ECHO:
            return eax_create_eax_echo_effect(al_effect_props);

        case AL_EFFECT_FLANGER:
            return eax_create_eax_flanger_effect(al_effect_props);

        case AL_EFFECT_FREQUENCY_SHIFTER:
            return eax_create_eax_frequency_shifter_effect(al_effect_props);

        case AL_EFFECT_VOCAL_MORPHER:
            return eax_create_eax_vocal_morpher_effect(al_effect_props);

        case AL_EFFECT_PITCH_SHIFTER:
            return eax_create_eax_pitch_shifter_effect(al_effect_props);

        case AL_EFFECT_RING_MODULATOR:
            return eax_create_eax_ring_modulator_effect(al_effect_props);

        case AL_EFFECT_AUTOWAH:
            return eax_create_eax_auto_wah_effect(al_effect_props);

        case AL_EFFECT_COMPRESSOR:
            return eax_create_eax_compressor_effect(al_effect_props);

        case AL_EFFECT_EQUALIZER:
            return eax_create_eax_equalizer_effect(al_effect_props);

        case AL_EFFECT_EAXREVERB:
            return eax_create_eax_reverb_effect(al_effect_props);

        default:
            assert(false && "Unsupported AL effect type.");
            return nullptr;
    }

#undef EAX_PREFIX
}


#endif // ALSOFT_EAX
