#ifndef AL_EFFECT_H
#define AL_EFFECT_H

#include <array>
#include <bitset>
#include <cstdint>
#include <string_view>
#include <utility>
#include <variant>

#include "AL/al.h"
#include "AL/efx.h"

#include "core/effects/base.h"
#include "disable_alloc.hpp"
#include "effects/effects.h"
#include "gsl/gsl"


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
inline std::bitset<MAX_EFFECTS> DisabledEffects;

struct EffectList {
    std::string_view name;
    ALuint type;
    ALenum val;
};
DECL_HIDDEN constinit extern const std::array<EffectList,16> gEffectList;

using EffectHandlerVariant = std::variant<NullEffectHandler,ReverbEffectHandler,
    StdReverbEffectHandler,AutowahEffectHandler,ChorusEffectHandler,CompressorEffectHandler,
    DistortionEffectHandler,EchoEffectHandler,EqualizerEffectHandler,FlangerEffectHandler,
    FshifterEffectHandler,ModulatorEffectHandler,PshifterEffectHandler,VmorpherEffectHandler,
    DedicatedDialogEffectHandler,DedicatedLfeEffectHandler,ConvolutionEffectHandler>;

namespace al {

struct Effect {
    // Effect type (AL_EFFECT_NULL, ...)
    ALenum mType{AL_EFFECT_NULL};

    EffectHandlerVariant mPropsVariant;
    EffectProps mProps;

    /* Self ID */
    ALuint mId{0u};

    Effect() = default;
    Effect(const Effect&) = delete;
    Effect(Effect&&) = delete;
    ~Effect() = default;
    auto operator=(const Effect&) -> Effect& = delete;
    auto operator=(Effect&&) -> Effect& = delete;

    static void SetName(al::Context& context, ALuint id, std::string_view name);

    DISABLE_ALLOC
};

} /* namespace al */

void InitEffect(al::Effect& effect);

void LoadReverbPreset(std::string_view name, al::Effect& effect);

bool IsValidEffectType(ALenum type) noexcept;

struct EffectSubList {
    std::uint64_t mFreeMask{~std::uint64_t{0}};
    gsl::owner<std::array<al::Effect,64>*> mEffects{nullptr}; /* 64 */

    EffectSubList() noexcept = default;
    EffectSubList(const EffectSubList&) = delete;
    EffectSubList(EffectSubList&& rhs) noexcept : mFreeMask{rhs.mFreeMask}, mEffects{rhs.mEffects}
    { rhs.mFreeMask = ~std::uint64_t{0}; rhs.mEffects = nullptr; }
    ~EffectSubList();

    EffectSubList& operator=(const EffectSubList&) = delete;
    EffectSubList& operator=(EffectSubList&& rhs) noexcept
    { std::swap(mFreeMask, rhs.mFreeMask); std::swap(mEffects, rhs.mEffects); return *this; }
};

#endif
