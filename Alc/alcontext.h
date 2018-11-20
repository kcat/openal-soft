#ifndef ALCONTEXT_H
#define ALCONTEXT_H

#include <mutex>
#include <atomic>
#include <memory>
#include <thread>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"
#include "inprogext.h"

#include "atomic.h"
#include "vector.h"
#include "threads.h"
#include "almalloc.h"

#include "alListener.h"


struct ALsource;
struct ALeffectslot;
struct ALcontextProps;
struct ALlistenerProps;
struct ALvoiceProps;
struct ALeffectslotProps;
struct ALvoice;
struct ALeffectslotArray;
struct ll_ringbuffer;

enum class DistanceModel {
    InverseClamped  = AL_INVERSE_DISTANCE_CLAMPED,
    LinearClamped   = AL_LINEAR_DISTANCE_CLAMPED,
    ExponentClamped = AL_EXPONENT_DISTANCE_CLAMPED,
    Inverse  = AL_INVERSE_DISTANCE,
    Linear   = AL_LINEAR_DISTANCE,
    Exponent = AL_EXPONENT_DISTANCE,
    Disable  = AL_NONE,

    Default = InverseClamped
};

struct SourceSubList {
    uint64_t FreeMask{0u};
    ALsource *Sources{nullptr}; /* 64 */
};

/* Effect slots are rather large, and apps aren't likely to have more than one
 * or two (let alone 64), so hold them individually.
 */
using ALeffectslotPtr = std::unique_ptr<ALeffectslot>;

struct ALCcontext_struct {
    RefCount ref{1u};

    al::vector<SourceSubList> SourceList;
    ALuint NumSources{0};
    almtx_t SourceLock;

    al::vector<ALeffectslotPtr> EffectSlotList;
    almtx_t EffectSlotLock;

    ATOMIC(ALenum) LastError{AL_NO_ERROR};

    DistanceModel mDistanceModel{DistanceModel::Default};
    ALboolean SourceDistanceModel{AL_FALSE};

    ALfloat DopplerFactor{1.0f};
    ALfloat DopplerVelocity{1.0f};
    ALfloat SpeedOfSound{};
    ALfloat MetersPerUnit{1.0f};

    std::atomic_flag PropsClean{true};
    std::atomic<bool> DeferUpdates{false};

    almtx_t PropLock;

    /* Counter for the pre-mixing updates, in 31.1 fixed point (lowest bit
     * indicates if updates are currently happening).
     */
    RefCount UpdateCount{0u};
    ATOMIC(ALenum) HoldUpdates{AL_FALSE};

    ALfloat GainBoost{1.0f};

    ATOMIC(ALcontextProps*) Update{nullptr};

    /* Linked lists of unused property containers, free to use for future
     * updates.
     */
    ATOMIC(ALcontextProps*) FreeContextProps{nullptr};
    ATOMIC(ALlistenerProps*) FreeListenerProps{nullptr};
    ATOMIC(ALvoiceProps*) FreeVoiceProps{nullptr};
    ATOMIC(ALeffectslotProps*) FreeEffectslotProps{nullptr};

    ALvoice **Voices{nullptr};
    ALsizei VoiceCount{0};
    ALsizei MaxVoices{0};

    ATOMIC(ALeffectslotArray*) ActiveAuxSlots{nullptr};

    std::thread EventThread;
    alsem_t EventSem;
    ll_ringbuffer *AsyncEvents{nullptr};
    ATOMIC(ALbitfieldSOFT) EnabledEvts{0u};
    std::mutex EventCbLock;
    ALEVENTPROCSOFT EventCb{};
    void *EventParam{nullptr};

    /* Default effect slot */
    std::unique_ptr<ALeffectslot> DefaultSlot;

    ALCdevice *const Device;
    const ALCchar *ExtensionList{nullptr};

    ATOMIC(ALCcontext*) next{nullptr};

    ALlistener Listener{};


    ALCcontext_struct(ALCdevice *device);
    ALCcontext_struct(const ALCcontext_struct&) = delete;
    ALCcontext_struct& operator=(const ALCcontext_struct&) = delete;
    ~ALCcontext_struct();

    DEF_NEWDEL(ALCcontext)
};

ALCcontext *GetContextRef(void);
void ALCcontext_DecRef(ALCcontext *context);

void UpdateContextProps(ALCcontext *context);

void ALCcontext_DeferUpdates(ALCcontext *context);
void ALCcontext_ProcessUpdates(ALCcontext *context);

inline void LockEffectSlotList(ALCcontext *context)
{ almtx_lock(&context->EffectSlotLock); }
inline void UnlockEffectSlotList(ALCcontext *context)
{ almtx_unlock(&context->EffectSlotLock); }


/* Simple RAII context reference. Takes the reference of the provided
 * ALCcontext, and decrements it when leaving scope. Movable (transfer
 * reference) but not copyable (no new references).
 */
class ContextRef {
    ALCcontext *mCtx{nullptr};

    void release() noexcept
    {
        if(mCtx)
            ALCcontext_DecRef(mCtx);
        mCtx = nullptr;
    }

public:
    ContextRef() noexcept = default;
    explicit ContextRef(ALCcontext *ctx) noexcept : mCtx(ctx) { }
    ~ContextRef() { release(); }

    ContextRef& operator=(const ContextRef&) = delete;
    ContextRef& operator=(ContextRef&& rhs) noexcept
    {
        release();
        mCtx = rhs.mCtx;
        rhs.mCtx = nullptr;
        return *this;
    }

    operator bool() const noexcept { return mCtx != nullptr; }

    ALCcontext* operator->() noexcept { return mCtx; }
    ALCcontext* get() noexcept { return mCtx; }
};


struct ALcontextProps {
    ALfloat DopplerFactor;
    ALfloat DopplerVelocity;
    ALfloat SpeedOfSound;
    ALboolean SourceDistanceModel;
    DistanceModel mDistanceModel;
    ALfloat MetersPerUnit;

    ATOMIC(struct ALcontextProps*) next;
};

#endif /* ALCONTEXT_H */
