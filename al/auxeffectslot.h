#ifndef AL_AUXEFFECTSLOT_H
#define AL_AUXEFFECTSLOT_H

#include <atomic>
#include <cstddef>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/efx.h"

#include "alcmain.h"
#include "almalloc.h"
#include "atomic.h"
#include "effects/base.h"
#include "vector.h"

struct ALeffect;
struct ALeffectslot;


using ALeffectslotArray = al::FlexArray<ALeffectslot*>;


struct ALeffectslotProps {
    ALfloat   Gain;
    ALboolean AuxSendAuto;
    ALeffectslot *Target;

    ALenum Type;
    EffectProps Props;

    EffectState *State;

    std::atomic<ALeffectslotProps*> next;

    DEF_NEWDEL(ALeffectslotProps)
};


struct ALeffectslot {
    ALfloat   Gain{1.0f};
    ALboolean AuxSendAuto{AL_TRUE};
    ALeffectslot *Target{nullptr};

    struct {
        ALenum Type{AL_EFFECT_NULL};
        EffectProps Props{};

        EffectState *State{nullptr};
    } Effect;

    std::atomic_flag PropsClean;

    RefCount ref{0u};

    struct {
        std::atomic<ALeffectslotProps*> Update{nullptr};

        ALfloat   Gain{1.0f};
        ALboolean AuxSendAuto{AL_TRUE};
        ALeffectslot *Target{nullptr};

        ALenum EffectType{AL_EFFECT_NULL};
        EffectProps mEffectProps{};
        EffectState *mEffectState{nullptr};

        ALfloat RoomRolloff{0.0f}; /* Added to the source's room rolloff, not multiplied. */
        ALfloat DecayTime{0.0f};
        ALfloat DecayLFRatio{0.0f};
        ALfloat DecayHFRatio{0.0f};
        ALboolean DecayHFLimit{AL_FALSE};
        ALfloat AirAbsorptionGainHF{1.0f};
    } Params;

    /* Self ID */
    ALuint id{};

    /* Mixing buffer used by the Wet mix. */
    al::vector<FloatBufferLine, 16> MixBuffer;

    /* Wet buffer configuration is ACN channel order with N3D scaling.
     * Consequently, effects that only want to work with mono input can use
     * channel 0 by itself. Effects that want multichannel can process the
     * ambisonics signal and make a B-Format source pan.
     */
    MixParams Wet;

    ALeffectslot() { PropsClean.test_and_set(std::memory_order_relaxed); }
    ALeffectslot(const ALeffectslot&) = delete;
    ALeffectslot& operator=(const ALeffectslot&) = delete;
    ~ALeffectslot();

    static ALeffectslotArray *CreatePtrArray(size_t count) noexcept;

    DEF_NEWDEL(ALeffectslot)
};

ALenum InitEffectSlot(ALeffectslot *slot);
void UpdateEffectSlotProps(ALeffectslot *slot, ALCcontext *context);
void UpdateAllEffectSlotProps(ALCcontext *context);


ALenum InitializeEffect(ALCcontext *Context, ALeffectslot *EffectSlot, ALeffect *effect);

#endif
