#ifndef ALCONTEXT_H
#define ALCONTEXT_H

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
    uint64_t FreeMask;
    ALsource *Sources; /* 64 */
};
TYPEDEF_VECTOR(SourceSubList, vector_SourceSubList)

/* Effect slots are rather large, and apps aren't likely to have more than one
 * or two (let alone 64), so hold them individually.
 */
using ALeffectslotPtr = struct ALeffectslot*;
TYPEDEF_VECTOR(ALeffectslotPtr, vector_ALeffectslotPtr)

struct ALCcontext_struct {
    RefCount ref;

    vector_SourceSubList SourceList;
    ALuint NumSources;
    almtx_t SourceLock;

    vector_ALeffectslotPtr EffectSlotList;
    almtx_t EffectSlotLock;

    ATOMIC(ALenum) LastError;

    enum DistanceModel DistanceModel;
    ALboolean SourceDistanceModel;

    ALfloat DopplerFactor;
    ALfloat DopplerVelocity;
    ALfloat SpeedOfSound;
    ALfloat MetersPerUnit;

    ATOMIC(ALenum) PropsClean;
    ATOMIC(ALenum) DeferUpdates;

    almtx_t PropLock;

    /* Counter for the pre-mixing updates, in 31.1 fixed point (lowest bit
     * indicates if updates are currently happening).
     */
    RefCount UpdateCount;
    ATOMIC(ALenum) HoldUpdates;

    ALfloat GainBoost;

    ATOMIC(ALcontextProps*) Update;

    /* Linked lists of unused property containers, free to use for future
     * updates.
     */
    ATOMIC(ALcontextProps*) FreeContextProps;
    ATOMIC(ALlistenerProps*) FreeListenerProps;
    ATOMIC(ALvoiceProps*) FreeVoiceProps;
    ATOMIC(ALeffectslotProps*) FreeEffectslotProps;

    ALvoice **Voices;
    ALsizei VoiceCount;
    ALsizei MaxVoices;

    ATOMIC(ALeffectslotArray*) ActiveAuxSlots;

    althrd_t EventThread;
    alsem_t EventSem;
    ll_ringbuffer *AsyncEvents;
    ATOMIC(ALbitfieldSOFT) EnabledEvts;
    almtx_t EventCbLock;
    ALEVENTPROCSOFT EventCb;
    void *EventParam;

    /* Default effect slot */
    ALeffectslot *DefaultSlot;

    ALCdevice  *Device;
    const ALCchar *ExtensionList;

    ATOMIC(ALCcontext*) next;

    ALlistener Listener;

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
    enum DistanceModel DistanceModel;
    ALfloat MetersPerUnit;

    ATOMIC(struct ALcontextProps*) next;
};

#endif /* ALCONTEXT_H */
