#ifndef _AL_AUXEFFECTSLOT_H_
#define _AL_AUXEFFECTSLOT_H_

#include <array>

#include "alMain.h"
#include "alEffect.h"
#include "ambidefs.h"
#include "effects/base.h"

#include "almalloc.h"
#include "atomic.h"


struct ALeffectslot;
union ALeffectProps;


using ALeffectslotArray = al::FlexArray<ALeffectslot*>;


struct ALeffectslotProps {
    ALfloat   Gain;
    ALboolean AuxSendAuto;
    ALeffectslot *Target;

    ALenum Type;
    ALeffectProps Props;

    EffectState *State;

    std::atomic<ALeffectslotProps*> next;
};


struct ALeffectslot {
    ALfloat   Gain{1.0f};
    ALboolean AuxSendAuto{AL_TRUE};
    ALeffectslot *Target{nullptr};

    struct {
        ALenum Type{AL_EFFECT_NULL};
        ALeffectProps Props{};

        EffectState *State{nullptr};
    } Effect;

    std::atomic_flag PropsClean;

    RefCount ref{0u};

    std::atomic<ALeffectslotProps*> Update{nullptr};

    struct {
        ALfloat   Gain{1.0f};
        ALboolean AuxSendAuto{AL_TRUE};
        ALeffectslot *Target{nullptr};

        ALenum EffectType{AL_EFFECT_NULL};
        ALeffectProps EffectProps{};
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

    /* Wet buffer configuration is ACN channel order with N3D scaling.
     * Consequently, effects that only want to work with mono input can use
     * channel 0 by itself. Effects that want multichannel can process the
     * ambisonics signal and make a B-Format source pan.
     */
    al::vector<std::array<ALfloat,BUFFERSIZE>,16> WetBuffer;
    BFChannelConfig ChanMap[MAX_AMBI_CHANNELS];

    ALeffectslot() { PropsClean.test_and_set(std::memory_order_relaxed); }
    ALeffectslot(const ALeffectslot&) = delete;
    ALeffectslot& operator=(const ALeffectslot&) = delete;
    ~ALeffectslot();

    static ALeffectslotArray *CreatePtrArray(size_t count) noexcept;

    DEF_PLACE_NEWDEL()
};

ALenum InitEffectSlot(ALeffectslot *slot);
void UpdateEffectSlotProps(ALeffectslot *slot, ALCcontext *context);
void UpdateAllEffectSlotProps(ALCcontext *context);


ALenum InitializeEffect(ALCcontext *Context, ALeffectslot *EffectSlot, ALeffect *effect);

#endif
