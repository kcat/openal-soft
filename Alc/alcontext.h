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
    uint64_t FreeMask{~uint64_t{}};
    ALsource *Sources{nullptr}; /* 64 */

    SourceSubList() noexcept = default;
    SourceSubList(const SourceSubList&) = delete;
    SourceSubList(SourceSubList&& rhs) noexcept : FreeMask{rhs.FreeMask}, Sources{rhs.Sources}
    { rhs.FreeMask = ~uint64_t{}; rhs.Sources = nullptr; }
    ~SourceSubList();

    SourceSubList& operator=(const SourceSubList&) = delete;
    SourceSubList& operator=(SourceSubList&& rhs) noexcept
    { std::swap(FreeMask, rhs.FreeMask); std::swap(Sources, rhs.Sources); return *this; }
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

    std::atomic<ALenum> LastError{AL_NO_ERROR};

    DistanceModel mDistanceModel{DistanceModel::Default};
    ALboolean SourceDistanceModel{AL_FALSE};

    ALfloat DopplerFactor{1.0f};
    ALfloat DopplerVelocity{1.0f};
    ALfloat SpeedOfSound{};
    ALfloat MetersPerUnit{1.0f};

    std::atomic_flag PropsClean{true};
    std::atomic<bool> DeferUpdates{false};

    std::mutex PropLock;

    /* Counter for the pre-mixing updates, in 31.1 fixed point (lowest bit
     * indicates if updates are currently happening).
     */
    RefCount UpdateCount{0u};
    std::atomic<ALenum> HoldUpdates{AL_FALSE};

    ALfloat GainBoost{1.0f};

    std::atomic<ALcontextProps*> Update{nullptr};

    /* Linked lists of unused property containers, free to use for future
     * updates.
     */
    std::atomic<ALcontextProps*> FreeContextProps{nullptr};
    std::atomic<ALlistenerProps*> FreeListenerProps{nullptr};
    std::atomic<ALvoiceProps*> FreeVoiceProps{nullptr};
    std::atomic<ALeffectslotProps*> FreeEffectslotProps{nullptr};

    ALvoice **Voices{nullptr};
    std::atomic<ALsizei> VoiceCount{0};
    ALsizei MaxVoices{0};

    std::atomic<ALeffectslotArray*> ActiveAuxSlots{nullptr};

    std::thread EventThread;
    alsem_t EventSem;
    ll_ringbuffer *AsyncEvents{nullptr};
    std::atomic<ALbitfieldSOFT> EnabledEvts{0u};
    std::mutex EventCbLock;
    ALEVENTPROCSOFT EventCb{};
    void *EventParam{nullptr};

    /* Default effect slot */
    std::unique_ptr<ALeffectslot> DefaultSlot;

    ALCdevice *const Device;
    const ALCchar *ExtensionList{nullptr};

    std::atomic<ALCcontext*> next{nullptr};

    ALlistener Listener{};


    ALCcontext_struct(ALCdevice *device);
    ALCcontext_struct(const ALCcontext_struct&) = delete;
    ALCcontext_struct& operator=(const ALCcontext_struct&) = delete;
    ~ALCcontext_struct();

    DEF_NEWDEL(ALCcontext)
};

void ALCcontext_DecRef(ALCcontext *context);

void UpdateContextProps(ALCcontext *context);

void ALCcontext_DeferUpdates(ALCcontext *context);
void ALCcontext_ProcessUpdates(ALCcontext *context);


/* Simple RAII context reference. Takes the reference of the provided
 * ALCcontext, and decrements it when leaving scope. Movable (transfer
 * reference) but not copyable (no new references).
 */
class ContextRef {
    ALCcontext *mCtx{nullptr};

    void reset() noexcept
    {
        if(mCtx)
            ALCcontext_DecRef(mCtx);
        mCtx = nullptr;
    }

public:
    ContextRef() noexcept = default;
    ContextRef(ContextRef&& rhs) noexcept : mCtx{rhs.mCtx}
    { rhs.mCtx = nullptr; }
    explicit ContextRef(ALCcontext *ctx) noexcept : mCtx(ctx) { }
    ~ContextRef() { reset(); }

    ContextRef& operator=(const ContextRef&) = delete;
    ContextRef& operator=(ContextRef&& rhs) noexcept
    {
        reset();
        mCtx = rhs.mCtx;
        rhs.mCtx = nullptr;
        return *this;
    }

    operator bool() const noexcept { return mCtx != nullptr; }

    ALCcontext* operator->() noexcept { return mCtx; }
    ALCcontext* get() noexcept { return mCtx; }

    ALCcontext* release() noexcept
    {
        ALCcontext *ret{mCtx};
        mCtx = nullptr;
        return ret;
    }
};

ContextRef GetContextRef(void);


struct ALcontextProps {
    ALfloat DopplerFactor;
    ALfloat DopplerVelocity;
    ALfloat SpeedOfSound;
    ALboolean SourceDistanceModel;
    DistanceModel mDistanceModel;
    ALfloat MetersPerUnit;

    std::atomic<ALcontextProps*> next;
};

#endif /* ALCONTEXT_H */
