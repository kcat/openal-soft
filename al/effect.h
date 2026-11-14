#ifndef AL_EFFECT_H
#define AL_EFFECT_H

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>
#include <variant>

#include "AL/al.h"
#include "AL/efx.h"

#include "alc/inprogext.h"
#include "bitset.hpp"
#include "core/effects/base.h"
#include "disable_alloc.hpp"
#include "effects/effects.h"
#include "gsl/gsl"


enum class EffectTypeFlag : std::uint8_t {
    EaxReverb,
    Reverb,
    Autowah,
    Chorus,
    Compressor,
    Distortion,
    Echo,
    Equalizer,
    Flanger,
    FShifter,
    Modulator,
    PShifter,
    VMorpher,
    Dedicated,
    Convolution,

    MaxValue = Convolution
};
inline auto DisabledEffects = al::bitset<EffectTypeFlag>{};

struct EffectList {
    std::string_view name;
    EffectTypeFlag type;
    ALenum val;
};
inline auto constexpr gEffectList = std::to_array<EffectList>({
    {.name="eaxreverb",   .type=EffectTypeFlag::EaxReverb,   .val=AL_EFFECT_EAXREVERB },
    {.name="reverb",      .type=EffectTypeFlag::Reverb,      .val=AL_EFFECT_REVERB },
    {.name="autowah",     .type=EffectTypeFlag::Autowah,     .val=AL_EFFECT_AUTOWAH },
    {.name="chorus",      .type=EffectTypeFlag::Chorus,      .val=AL_EFFECT_CHORUS },
    {.name="compressor",  .type=EffectTypeFlag::Compressor,  .val=AL_EFFECT_COMPRESSOR },
    {.name="distortion",  .type=EffectTypeFlag::Distortion,  .val=AL_EFFECT_DISTORTION },
    {.name="echo",        .type=EffectTypeFlag::Echo,        .val=AL_EFFECT_ECHO },
    {.name="equalizer",   .type=EffectTypeFlag::Equalizer,   .val=AL_EFFECT_EQUALIZER },
    {.name="flanger",     .type=EffectTypeFlag::Flanger,     .val=AL_EFFECT_FLANGER },
    {.name="fshifter",    .type=EffectTypeFlag::FShifter,    .val=AL_EFFECT_FREQUENCY_SHIFTER },
    {.name="modulator",   .type=EffectTypeFlag::Modulator,   .val=AL_EFFECT_RING_MODULATOR },
    {.name="pshifter",    .type=EffectTypeFlag::PShifter,    .val=AL_EFFECT_PITCH_SHIFTER },
    {.name="vmorpher",    .type=EffectTypeFlag::VMorpher,    .val=AL_EFFECT_VOCAL_MORPHER },
    {.name="dedicated",   .type=EffectTypeFlag::Dedicated,
        .val=AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT },
    {.name="dedicated",   .type=EffectTypeFlag::Dedicated,   .val=AL_EFFECT_DEDICATED_DIALOGUE },
    {.name="convolution", .type=EffectTypeFlag::Convolution, .val=AL_EFFECT_CONVOLUTION_SOFT },
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
