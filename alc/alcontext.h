#ifndef ALCONTEXT_H
#define ALCONTEXT_H

#include <array>
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
#include "core/bufferline.h"
#include "core/context.h"
#include "inprogext.h"
#include "intrusive_ptr.h"
#include "threads.h"
#include "vecmat.h"
#include "vector.h"

struct ALeffectslot;
struct ALsource;
struct DeviceBase;
struct EffectSlot;
struct EffectSlotProps;
struct RingBuffer;
struct Voice;
struct VoiceChange;
struct VoicePropsItem;

using uint = unsigned int;


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

struct ALCcontext : public al::intrusive_ref<ALCcontext>, ContextBase {
    const al::intrusive_ptr<ALCdevice> mALDevice;

    /* Wet buffers used by effect slots. */
    al::vector<WetBufferPtr> mWetBuffers;


    al::atomic_invflag mPropsDirty;
    std::atomic<bool> mDeferUpdates{false};

    std::mutex mPropLock;

    std::atomic<ALenum> mLastError{AL_NO_ERROR};

    DistanceModel mDistanceModel{DistanceModel::Default};
    bool mSourceDistanceModel{false};

    float mDopplerFactor{1.0f};
    float mDopplerVelocity{1.0f};
    float mSpeedOfSound{SpeedOfSoundMetersPerSec};

    std::mutex mEventCbLock;
    ALEVENTPROCSOFT mEventCb{};
    void *mEventParam{nullptr};

    ALlistener mListener{};

    al::vector<SourceSubList> mSourceList;
    ALuint mNumSources{0};
    std::mutex mSourceLock;

    al::vector<EffectSlotSubList> mEffectSlotList;
    ALuint mNumEffectSlots{0u};
    std::mutex mEffectSlotLock;

    /* Default effect slot */
    std::unique_ptr<ALeffectslot> mDefaultSlot;

    const char *mExtensionList{nullptr};


    ALCcontext(al::intrusive_ptr<ALCdevice> device);
    ALCcontext(const ALCcontext&) = delete;
    ALCcontext& operator=(const ALCcontext&) = delete;
    ~ALCcontext();

    void init();
    /**
     * Removes the context from its device and removes it from being current on
     * the running thread or globally. Returns true if other contexts still
     * exist on the device.
     */
    bool deinit();

    /**
     * Defers/suspends updates for the given context's listener and sources.
     * This does *NOT* stop mixing, but rather prevents certain property
     * changes from taking effect.
     */
    void deferUpdates() noexcept { mDeferUpdates.exchange(true, std::memory_order_acq_rel); }

    /** Resumes update processing after being deferred. */
    void processUpdates();

#ifdef __USE_MINGW_ANSI_STDIO
    [[gnu::format(gnu_printf, 3, 4)]]
#else
    [[gnu::format(printf, 3, 4)]]
#endif
    void setError(ALenum errorCode, const char *msg, ...);

    DEF_NEWDEL(ALCcontext)
};

#define SETERR_RETURN(ctx, err, retval, ...) do {                             \
    (ctx)->setError((err), __VA_ARGS__);                                      \
    return retval;                                                            \
} while(0)


using ContextRef = al::intrusive_ptr<ALCcontext>;

ContextRef GetContextRef(void);

void UpdateContextProps(ALCcontext *context);


extern bool TrapALError;

#endif /* ALCONTEXT_H */
