
#include "config.h"

#include <algorithm>

#include "AL/alc.h"
#include "AL/al.h"
#include "AL/alext.h"

#include "alMain.h"
#include "alcontext.h"
#include "alError.h"
#include "alAuxEffectSlot.h"
#include "ringbuffer.h"
#include "threads.h"


static int EventThread(ALCcontext *context)
{
    bool quitnow{false};
    while(LIKELY(!quitnow))
    {
        AsyncEvent evt;
        if(ll_ringbuffer_read(context->AsyncEvents, &evt, 1) == 0)
        {
            context->EventSem.wait();
            continue;
        }

        std::lock_guard<std::mutex> _{context->EventCbLock};
        do {
            quitnow = evt.EnumType == EventType_KillThread;
            if(UNLIKELY(quitnow)) break;

            if(evt.EnumType == EventType_ReleaseEffectState)
            {
                evt.u.mEffectState->DecRef();
                continue;
            }

            ALbitfieldSOFT enabledevts{context->EnabledEvts.load(std::memory_order_acquire)};
            if(!context->EventCb) continue;

            if(evt.EnumType == EventType_SourceStateChange)
            {
                if(!(enabledevts&EventType_SourceStateChange))
                    continue;
                std::string msg{"Source ID " + std::to_string(evt.u.srcstate.id)};
                msg += " state has changed to ";
                msg += (evt.u.srcstate.state==AL_INITIAL) ? "AL_INITIAL" :
                    (evt.u.srcstate.state==AL_PLAYING) ? "AL_PLAYING" :
                    (evt.u.srcstate.state==AL_PAUSED) ? "AL_PAUSED" :
                    (evt.u.srcstate.state==AL_STOPPED) ? "AL_STOPPED" : "<unknown>";
                context->EventCb(AL_EVENT_TYPE_SOURCE_STATE_CHANGED_SOFT, evt.u.srcstate.id,
                    evt.u.srcstate.state, msg.length(), msg.c_str(), context->EventParam
                );
            }
            else if((enabledevts&evt.EnumType) == evt.EnumType)
                context->EventCb(evt.u.user.type, evt.u.user.id, evt.u.user.param,
                    (ALsizei)strlen(evt.u.user.msg), evt.u.user.msg, context->EventParam
                );
        } while(ll_ringbuffer_read(context->AsyncEvents, &evt, 1) != 0);
    }
    return 0;
}

void StartEventThrd(ALCcontext *ctx)
{
    try {
        ctx->EventThread = std::thread(EventThread, ctx);
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
    static constexpr AsyncEvent kill_evt = ASYNC_EVENT(EventType_KillThread);
    while(ll_ringbuffer_write(ctx->AsyncEvents, &kill_evt, 1) == 0)
        std::this_thread::yield();
    ctx->EventSem.post();
    if(ctx->EventThread.joinable())
        ctx->EventThread.join();
}

AL_API void AL_APIENTRY alEventControlSOFT(ALsizei count, const ALenum *types, ALboolean enable)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(count < 0) SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Controlling %d events", count);
    if(count == 0) return;
    if(!types) SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "NULL pointer");

    ALbitfieldSOFT flags{0};
    const ALenum *types_end = types+count;
    auto bad_type = std::find_if_not(types, types_end,
        [&flags](ALenum type) noexcept -> bool
        {
            if(type == AL_EVENT_TYPE_BUFFER_COMPLETED_SOFT)
                flags |= EventType_BufferCompleted;
            else if(type == AL_EVENT_TYPE_SOURCE_STATE_CHANGED_SOFT)
                flags |= EventType_SourceStateChange;
            else if(type == AL_EVENT_TYPE_ERROR_SOFT)
                flags |= EventType_Error;
            else if(type == AL_EVENT_TYPE_PERFORMANCE_SOFT)
                flags |= EventType_Performance;
            else if(type == AL_EVENT_TYPE_DEPRECATED_SOFT)
                flags |= EventType_Deprecated;
            else if(type == AL_EVENT_TYPE_DISCONNECTED_SOFT)
                flags |= EventType_Disconnected;
            else
                return false;
            return true;
        }
    );
    if(bad_type != types_end)
        SETERR_RETURN(context.get(), AL_INVALID_ENUM,, "Invalid event type 0x%04x", *bad_type);

    if(enable)
    {
        ALbitfieldSOFT enabledevts{context->EnabledEvts.load(std::memory_order_relaxed)};
        while(context->EnabledEvts.compare_exchange_weak(enabledevts, enabledevts|flags,
            std::memory_order_acq_rel, std::memory_order_acquire) == 0)
        {
            /* enabledevts is (re-)filled with the current value on failure, so
             * just try again.
             */
        }
    }
    else
    {
        ALbitfieldSOFT enabledevts{context->EnabledEvts.load(std::memory_order_relaxed)};
        while(context->EnabledEvts.compare_exchange_weak(enabledevts, enabledevts&~flags,
            std::memory_order_acq_rel, std::memory_order_acquire) == 0)
        {
        }
        /* Wait to ensure the event handler sees the changed flags before
         * returning.
         */
        std::lock_guard<std::mutex>{context->EventCbLock};
    }
}

AL_API void AL_APIENTRY alEventCallbackSOFT(ALEVENTPROCSOFT callback, void *userParam)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->PropLock};
    std::lock_guard<std::mutex> __{context->EventCbLock};
    context->EventCb = callback;
    context->EventParam = userParam;
}
