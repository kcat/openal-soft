
#include "config.h"

#include "event.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>

#include "AL/al.h"
#include "AL/alc.h"

#include "albyte.h"
#include "alcontext.h"
#include "almalloc.h"
#include "async_event.h"
#include "core/except.h"
#include "core/logging.h"
#include "effects/base.h"
#include "inprogext.h"
#include "opthelpers.h"
#include "ringbuffer.h"
#include "threads.h"
#include "voice_change.h"


static int EventThread(ALCcontext *context)
{
    RingBuffer *ring{context->mAsyncEvents.get()};
    bool quitnow{false};
    while LIKELY(!quitnow)
    {
        auto evt_data = ring->getReadVector().first;
        if(evt_data.len == 0)
        {
            context->mEventSem.wait();
            continue;
        }

        std::lock_guard<std::mutex> _{context->mEventCbLock};
        do {
            auto *evt_ptr = reinterpret_cast<AsyncEvent*>(evt_data.buf);
            evt_data.buf += sizeof(AsyncEvent);
            evt_data.len -= 1;

            AsyncEvent evt{*evt_ptr};
            al::destroy_at(evt_ptr);
            ring->readAdvance(1);

            quitnow = evt.EnumType == EventType_KillThread;
            if UNLIKELY(quitnow) break;

            if(evt.EnumType == EventType_ReleaseEffectState)
            {
                evt.u.mEffectState->release();
                continue;
            }

            uint enabledevts{context->mEnabledEvts.load(std::memory_order_acquire)};
            if(!context->mEventCb) continue;

            if(evt.EnumType == EventType_SourceStateChange)
            {
                if(!(enabledevts&EventType_SourceStateChange))
                    continue;
                ALuint state{};
                std::string msg{"Source ID " + std::to_string(evt.u.srcstate.id)};
                msg += " state has changed to ";
                switch(evt.u.srcstate.state)
                {
                case VChangeState::Reset:
                    msg += "AL_INITIAL";
                    state = AL_INITIAL;
                    break;
                case VChangeState::Stop:
                    msg += "AL_STOPPED";
                    state = AL_STOPPED;
                    break;
                case VChangeState::Play:
                    msg += "AL_PLAYING";
                    state = AL_PLAYING;
                    break;
                case VChangeState::Pause:
                    msg += "AL_PAUSED";
                    state = AL_PAUSED;
                    break;
                /* Shouldn't happen */
                case VChangeState::Restart:
                    break;
                }
                context->mEventCb(AL_EVENT_TYPE_SOURCE_STATE_CHANGED_SOFT, evt.u.srcstate.id,
                    state, static_cast<ALsizei>(msg.length()), msg.c_str(), context->mEventParam);
            }
            else if(evt.EnumType == EventType_BufferCompleted)
            {
                if(!(enabledevts&EventType_BufferCompleted))
                    continue;
                std::string msg{std::to_string(evt.u.bufcomp.count)};
                if(evt.u.bufcomp.count == 1) msg += " buffer completed";
                else msg += " buffers completed";
                context->mEventCb(AL_EVENT_TYPE_BUFFER_COMPLETED_SOFT, evt.u.bufcomp.id,
                    evt.u.bufcomp.count, static_cast<ALsizei>(msg.length()), msg.c_str(),
                    context->mEventParam);
            }
            else if(evt.EnumType == EventType_Disconnected)
            {
                if(!(enabledevts&EventType_Disconnected))
                    continue;
                context->mEventCb(AL_EVENT_TYPE_DISCONNECTED_SOFT, 0, 0,
                    static_cast<ALsizei>(strlen(evt.u.disconnect.msg)), evt.u.disconnect.msg,
                    context->mEventParam);
            }
        } while(evt_data.len != 0);
    }
    return 0;
}

void StartEventThrd(ALCcontext *ctx)
{
    try {
        ctx->mEventThread = std::thread{EventThread, ctx};
    }
    catch(std::exception& e) {
        ERR("Failed to start event thread: %s\n", e.what());
    }
    catch(...) {
        ERR("Failed to start event thread! Expect problems.\n");
    }
}

void StopEventThrd(ALCcontext *ctx)
{
    RingBuffer *ring{ctx->mAsyncEvents.get()};
    auto evt_data = ring->getWriteVector().first;
    if(evt_data.len == 0)
    {
        do {
            std::this_thread::yield();
            evt_data = ring->getWriteVector().first;
        } while(evt_data.len == 0);
    }
    ::new(evt_data.buf) AsyncEvent{EventType_KillThread};
    ring->writeAdvance(1);

    ctx->mEventSem.post();
    if(ctx->mEventThread.joinable())
        ctx->mEventThread.join();
}

AL_API void AL_APIENTRY alEventControlSOFT(ALsizei count, const ALenum *types, ALboolean enable)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    if(count < 0) context->setError(AL_INVALID_VALUE, "Controlling %d events", count);
    if(count <= 0) return;
    if(!types) SETERR_RETURN(context, AL_INVALID_VALUE,, "NULL pointer");

    uint flags{0};
    const ALenum *types_end = types+count;
    auto bad_type = std::find_if_not(types, types_end,
        [&flags](ALenum type) noexcept -> bool
        {
            if(type == AL_EVENT_TYPE_BUFFER_COMPLETED_SOFT)
                flags |= EventType_BufferCompleted;
            else if(type == AL_EVENT_TYPE_SOURCE_STATE_CHANGED_SOFT)
                flags |= EventType_SourceStateChange;
            else if(type == AL_EVENT_TYPE_DISCONNECTED_SOFT)
                flags |= EventType_Disconnected;
            else
                return false;
            return true;
        }
    );
    if(bad_type != types_end)
        SETERR_RETURN(context, AL_INVALID_ENUM,, "Invalid event type 0x%04x", *bad_type);

    if(enable)
    {
        uint enabledevts{context->mEnabledEvts.load(std::memory_order_relaxed)};
        while(context->mEnabledEvts.compare_exchange_weak(enabledevts, enabledevts|flags,
            std::memory_order_acq_rel, std::memory_order_acquire) == 0)
        {
            /* enabledevts is (re-)filled with the current value on failure, so
             * just try again.
             */
        }
    }
    else
    {
        uint enabledevts{context->mEnabledEvts.load(std::memory_order_relaxed)};
        while(context->mEnabledEvts.compare_exchange_weak(enabledevts, enabledevts&~flags,
            std::memory_order_acq_rel, std::memory_order_acquire) == 0)
        {
        }
        /* Wait to ensure the event handler sees the changed flags before
         * returning.
         */
        std::lock_guard<std::mutex>{context->mEventCbLock};
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alEventCallbackSOFT(ALEVENTPROCSOFT callback, void *userParam)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    std::lock_guard<std::mutex> _{context->mPropLock};
    std::lock_guard<std::mutex> __{context->mEventCbLock};
    context->mEventCb = callback;
    context->mEventParam = userParam;
}
END_API_FUNC
