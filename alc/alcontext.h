#ifndef ALCONTEXT_H
#define ALCONTEXT_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include "AL/al.h"
#include "AL/alc.h"

#include "al/listener.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "alu.h"
#include "atomic.h"
#include "inprogext.h"
#include "logging.h"
#include "threads.h"
#include "vector.h"

struct ALeffectslot;
struct ALeffectslotProps;
struct ALsource;
struct RingBuffer;


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


struct ALcontextProps {
    ALfloat DopplerFactor;
    ALfloat DopplerVelocity;
    ALfloat SpeedOfSound;
    ALboolean SourceDistanceModel;
    DistanceModel mDistanceModel;
    ALfloat MetersPerUnit;

    std::atomic<ALcontextProps*> next;
};


struct SourceSubList {
    uint64_t FreeMask{~0_u64};
    ALsource *Sources{nullptr}; /* 64 */

    SourceSubList() noexcept = default;
    SourceSubList(const SourceSubList&) = delete;
    SourceSubList(SourceSubList&& rhs) noexcept : FreeMask{rhs.FreeMask}, Sources{rhs.Sources}
    { rhs.FreeMask = ~0_u64; rhs.Sources = nullptr; }
    ~SourceSubList();

    SourceSubList& operator=(const SourceSubList&) = delete;
    SourceSubList& operator=(SourceSubList&& rhs) noexcept
    { std::swap(FreeMask, rhs.FreeMask); std::swap(Sources, rhs.Sources); return *this; }
};

struct EffectSlotSubList {
    uint64_t FreeMask{~0_u64};
    ALeffectslot *EffectSlots{nullptr}; /* 64 */

    EffectSlotSubList() noexcept = default;
    EffectSlotSubList(const EffectSlotSubList&) = delete;
    EffectSlotSubList(EffectSlotSubList&& rhs) noexcept
      : FreeMask{rhs.FreeMask}, EffectSlots{rhs.EffectSlots}
    { rhs.FreeMask = ~0_u64; rhs.EffectSlots = nullptr; }
    ~EffectSlotSubList();

    EffectSlotSubList& operator=(const EffectSlotSubList&) = delete;
    EffectSlotSubList& operator=(EffectSlotSubList&& rhs) noexcept
    { std::swap(FreeMask, rhs.FreeMask); std::swap(EffectSlots, rhs.EffectSlots); return *this; }
};

struct ALCcontext {
    RefCount mRef{1u};

    al::vector<SourceSubList> mSourceList;
    ALuint mNumSources{0};
    std::mutex mSourceLock;

    al::vector<EffectSlotSubList> mEffectSlotList;
    ALuint mNumEffectSlots{0u};
    std::mutex mEffectSlotLock;

    std::atomic<ALenum> mLastError{AL_NO_ERROR};

    DistanceModel mDistanceModel{DistanceModel::Default};
    ALboolean mSourceDistanceModel{AL_FALSE};

    ALfloat mDopplerFactor{1.0f};
    ALfloat mDopplerVelocity{1.0f};
    ALfloat mSpeedOfSound{};
    ALfloat mMetersPerUnit{1.0f};

    std::atomic_flag mPropsClean;
    std::atomic<bool> mDeferUpdates{false};

    std::mutex mPropLock;

    /* Counter for the pre-mixing updates, in 31.1 fixed point (lowest bit
     * indicates if updates are currently happening).
     */
    RefCount mUpdateCount{0u};
    std::atomic<bool> mHoldUpdates{false};

    ALfloat mGainBoost{1.0f};

    std::atomic<ALcontextProps*> mUpdate{nullptr};

    /* Linked lists of unused property containers, free to use for future
     * updates.
     */
    std::atomic<ALcontextProps*> mFreeContextProps{nullptr};
    std::atomic<ALlistenerProps*> mFreeListenerProps{nullptr};
    std::atomic<ALvoiceProps*> mFreeVoiceProps{nullptr};
    std::atomic<ALeffectslotProps*> mFreeEffectslotProps{nullptr};

    std::unique_ptr<al::FlexArray<ALvoice>> mVoices{nullptr};
    std::atomic<ALuint> mVoiceCount{0u};

    using ALeffectslotArray = al::FlexArray<ALeffectslot*>;
    std::atomic<ALeffectslotArray*> mActiveAuxSlots{nullptr};

    std::thread mEventThread;
    al::semaphore mEventSem;
    std::unique_ptr<RingBuffer> mAsyncEvents;
    std::atomic<ALbitfieldSOFT> mEnabledEvts{0u};
    std::mutex mEventCbLock;
    ALEVENTPROCSOFT mEventCb{};
    void *mEventParam{nullptr};

    /* Default effect slot */
    std::unique_ptr<ALeffectslot> mDefaultSlot;

    ALCdevice *const mDevice;
    const ALCchar *mExtensionList{nullptr};

    ALlistener mListener{};


    ALCcontext(ALCdevice *device);
    ALCcontext(const ALCcontext&) = delete;
    ALCcontext& operator=(const ALCcontext&) = delete;
    ~ALCcontext();

    void decRef() noexcept;

    void allocVoices(size_t num_voices);

    /**
     * Defers/suspends updates for the given context's listener and sources.
     * This does *NOT* stop mixing, but rather prevents certain property
     * changes from taking effect.
     */
    void deferUpdates() noexcept { mDeferUpdates.store(true); }

    /** Resumes update processing after being deferred. */
    void processUpdates();

    void setError(ALenum errorCode, const char *msg, ...) DECL_FORMAT(printf, 3, 4);

    DEF_NEWDEL(ALCcontext)
};

#define SETERR_RETURN(ctx, err, retval, ...) do {                             \
    (ctx)->setError((err), __VA_ARGS__);                                      \
    return retval;                                                            \
} while(0)


void UpdateContextProps(ALCcontext *context);


/* Simple RAII context reference. Takes the reference of the provided
 * ALCcontext, and decrements it when leaving scope. Movable (transfer
 * reference) but not copyable (no new references).
 */
class ContextRef {
    ALCcontext *mCtx{nullptr};

    void reset() noexcept
    {
        if(mCtx)
            mCtx->decRef();
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
    { std::swap(mCtx, rhs.mCtx); return *this; }

    operator bool() const noexcept { return mCtx != nullptr; }

    ALCcontext* operator->() const noexcept { return mCtx; }
    ALCcontext* get() const noexcept { return mCtx; }

    ALCcontext* release() noexcept
    {
        ALCcontext *ret{mCtx};
        mCtx = nullptr;
        return ret;
    }
};

inline bool operator==(const ContextRef &lhs, const ALCcontext *rhs) noexcept
{ return lhs.get() == rhs; }
inline bool operator!=(const ContextRef &lhs, const ALCcontext *rhs) noexcept
{ return !(lhs == rhs); }
inline bool operator<(const ContextRef &lhs, const ALCcontext *rhs) noexcept
{ return lhs.get() < rhs; }

ContextRef GetContextRef(void);


extern bool TrapALError;

#endif /* ALCONTEXT_H */
