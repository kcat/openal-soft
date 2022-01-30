#ifndef AL_EFFECT_H
#define AL_EFFECT_H

#include "AL/al.h"
#include "AL/efx.h"

#include "al/effects/effects.h"
#include "alc/effects/base.h"

#ifdef ALSOFT_EAX
#include <memory>

#include "eax_effect.h"
#endif // ALSOFT_EAX


enum {
    EAXREVERB_EFFECT = 0,
    REVERB_EFFECT,
    AUTOWAH_EFFECT,
    CHORUS_EFFECT,
    COMPRESSOR_EFFECT,
    DISTORTION_EFFECT,
    ECHO_EFFECT,
    EQUALIZER_EFFECT,
    FLANGER_EFFECT,
    FSHIFTER_EFFECT,
    MODULATOR_EFFECT,
    PSHIFTER_EFFECT,
    VMORPHER_EFFECT,
    DEDICATED_EFFECT,
    CONVOLUTION_EFFECT,

    MAX_EFFECTS
};
extern bool DisabledEffects[MAX_EFFECTS];

extern float ReverbBoost;

struct EffectList {
    const char name[16];
    int type;
    ALenum val;
};
extern const EffectList gEffectList[16];


struct ALeffect {
    // Effect type (AL_EFFECT_NULL, ...)
    ALenum type{AL_EFFECT_NULL};

    EffectProps Props{};

    const EffectVtable *vtab{nullptr};

    /* Self ID */
    ALuint id{0u};

    DISABLE_ALLOC()


#ifdef ALSOFT_EAX
public:
    EaxEffectUPtr eax_effect{};


    void eax_initialize();

    void eax_al_set_effect(
        ALenum al_effect_type);


private:
    [[noreturn]]
    static void eax_fail(
        const char* message);
#endif // ALSOFT_EAX
};

void InitEffect(ALeffect *effect);

void LoadReverbPreset(const char *name, ALeffect *effect);

#ifdef ALSOFT_EAX
class EaxAlEffectDeleter {
public:
    EaxAlEffectDeleter() noexcept = default;

    EaxAlEffectDeleter(
        ALCcontext& context) noexcept;

    void operator()(
        ALeffect* effect) const;


private:
    ALCcontext* context_{};
}; // EaxAlEffectDeleter

using EaxAlEffectUPtr = std::unique_ptr<ALeffect, EaxAlEffectDeleter>;


EaxAlEffectUPtr eax_create_al_effect(
    ALCcontext& context,
    ALenum effect_type);

void eax_al_delete_effect(
    ALCcontext& context,
    ALeffect& effect);
#endif // ALSOFT_EAX

#endif
