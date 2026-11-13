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

#include "alc/inprogext.h"
#include "core/effects/base.h"
#include "disable_alloc.hpp"
#include "effects/effects.h"
#include "gsl/gsl"


enum EffectTypeFlag {
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
inline auto DisabledEffects = std::bitset<MAX_EFFECTS>{};

struct EffectList {
    std::string_view name;
    ALuint type;
    ALenum val;
};
inline auto constexpr gEffectList = std::to_array<EffectList>({
    {.name="eaxreverb",   .type=EAXREVERB_EFFECT,   .val=AL_EFFECT_EAXREVERB },
    {.name="reverb",      .type=REVERB_EFFECT,      .val=AL_EFFECT_REVERB },
    {.name="autowah",     .type=AUTOWAH_EFFECT,     .val=AL_EFFECT_AUTOWAH },
    {.name="chorus",      .type=CHORUS_EFFECT,      .val=AL_EFFECT_CHORUS },
    {.name="compressor",  .type=COMPRESSOR_EFFECT,  .val=AL_EFFECT_COMPRESSOR },
    {.name="distortion",  .type=DISTORTION_EFFECT,  .val=AL_EFFECT_DISTORTION },
    {.name="echo",        .type=ECHO_EFFECT,        .val=AL_EFFECT_ECHO },
    {.name="equalizer",   .type=EQUALIZER_EFFECT,   .val=AL_EFFECT_EQUALIZER },
    {.name="flanger",     .type=FLANGER_EFFECT,     .val=AL_EFFECT_FLANGER },
    {.name="fshifter",    .type=FSHIFTER_EFFECT,    .val=AL_EFFECT_FREQUENCY_SHIFTER },
    {.name="modulator",   .type=MODULATOR_EFFECT,   .val=AL_EFFECT_RING_MODULATOR },
    {.name="pshifter",    .type=PSHIFTER_EFFECT,    .val=AL_EFFECT_PITCH_SHIFTER },
    {.name="vmorpher",    .type=VMORPHER_EFFECT,    .val=AL_EFFECT_VOCAL_MORPHER },
    {.name="dedicated",   .type=DEDICATED_EFFECT,   .val=AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT },
    {.name="dedicated",   .type=DEDICATED_EFFECT,   .val=AL_EFFECT_DEDICATED_DIALOGUE },
    {.name="convolution", .type=CONVOLUTION_EFFECT, .val=AL_EFFECT_CONVOLUTION_SOFT },
});

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
