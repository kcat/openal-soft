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
#include "effectslot.h"
#include "effects/base.h"
#include "intrusive_ptr.h"
#include "vector.h"

struct ALbuffer;
struct ALeffect;
struct WetBuffer;


enum class SlotState : ALenum {
    Initial = AL_INITIAL,
    Playing = AL_PLAYING,
    Stopped = AL_STOPPED,
};

struct ALeffectslot {
    float Gain{1.0f};
    bool  AuxSendAuto{true};
    ALeffectslot *Target{nullptr};
    ALbuffer *Buffer{nullptr};

    struct {
        EffectSlotType Type{EffectSlotType::None};
        EffectProps Props{};

        al::intrusive_ptr<EffectState> State;
    } Effect;

    std::atomic_flag PropsClean;

    SlotState mState{SlotState::Initial};

    RefCount ref{0u};

    EffectSlot mSlot;

    /* Self ID */
    ALuint id{};

    ALeffectslot();
    ALeffectslot(const ALeffectslot&) = delete;
    ALeffectslot& operator=(const ALeffectslot&) = delete;
    ~ALeffectslot();

    ALenum initEffect(ALeffect *effect, ALCcontext *context);
    void updateProps(ALCcontext *context);

    /* This can be new'd for the context's default effect slot. */
    DEF_NEWDEL(ALeffectslot)
};

void UpdateAllEffectSlotProps(ALCcontext *context);

#endif
