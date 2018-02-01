
#include "config.h"

#include "AL/alc.h"
#include "AL/al.h"
#include "AL/alext.h"
#include "alMain.h"
#include "alError.h"
#include "ringbuffer.h"


static int EventThread(void *arg)
{
    ALCcontext *context = arg;

    almtx_lock(&context->EventCbLock);
    while(1)
    {
        AsyncEvent evt;
        ALbitfieldSOFT enabledevts;

        if(ll_ringbuffer_read_space(context->AsyncEvents) == 0)
        {
            /* Wait 50ms before checking again. Because events are delivered
             * asynchronously by the mixer, it's possible for one to be written
             * in between checking for a readable element and sleeping. So to
             * ensure events don't get left to go stale in the ringbuffer, we
             * need to keep checking regardless of being signaled.
             */
            struct timespec ts;
            altimespec_get(&ts, AL_TIME_UTC);
            ts.tv_nsec += 50000000;
            ts.tv_sec += ts.tv_nsec/1000000000;
            ts.tv_nsec %= 1000000000;
            alcnd_timedwait(&context->EventCnd, &context->EventCbLock, &ts);
            continue;
        }
        ll_ringbuffer_read(context->AsyncEvents, (char*)&evt, 1);
        if(!evt.EnumType) break;

        /* Should check the actual type is enabled here too. */
        enabledevts = ATOMIC_LOAD(&context->EnabledEvts, almemory_order_acquire);
        if(context->EventCb && (enabledevts&evt.EnumType) == evt.EnumType)
            context->EventCb(evt.Type, evt.ObjectId, evt.Param, (ALsizei)strlen(evt.Message),
                             evt.Message, context->EventParam);
    }
    almtx_unlock(&context->EventCbLock);
    return 0;
}

AL_API void AL_APIENTRY alEventControlSOFT(ALsizei count, const ALenum *types, ALboolean enable)
{
    ALCcontext *context;
    ALbitfieldSOFT flags = 0;
    ALsizei i;

    context = GetContextRef();
    if(!context) return;

    if(count < 0) SETERR_GOTO(context, AL_INVALID_VALUE, done, "Controlling %d events", count);
    if(count == 0) goto done;
    if(!types) SETERR_GOTO(context, AL_INVALID_VALUE, done, "NULL pointer");

    for(i = 0;i < count;i++)
    {
        if(types[i] == AL_EVENT_TYPE_BUFFER_COMPLETED_SOFT)
            flags |= EventType_BufferCompleted;
        else if(types[i] == AL_EVENT_TYPE_SOURCE_STATE_CHANGED_SOFT)
            flags |= EventType_SourceStateChange;
        else if(types[i] == AL_EVENT_TYPE_ERROR_SOFT)
            flags |= EventType_Error;
        else if(types[i] == AL_EVENT_TYPE_PERFORMANCE_SOFT)
            flags |= EventType_Performance;
        else if(types[i] == AL_EVENT_TYPE_DEPRECATED_SOFT)
            flags |= EventType_Deprecated;
        else
            SETERR_GOTO(context, AL_INVALID_ENUM, done, "Invalid event type 0x%04x", types[i]);
    }

    if(enable)
    {
        ALbitfieldSOFT enabledevts;
        bool isrunning;
        almtx_lock(&context->EventThrdLock);
        if(!context->AsyncEvents)
            context->AsyncEvents = ll_ringbuffer_create(64, sizeof(AsyncEvent));
        enabledevts = ATOMIC_LOAD(&context->EnabledEvts, almemory_order_relaxed);
        isrunning = !!enabledevts;
        while(ATOMIC_COMPARE_EXCHANGE_WEAK(&context->EnabledEvts, &enabledevts, enabledevts|flags,
                                           almemory_order_acq_rel, almemory_order_acquire) == 0)
        {
            /* enabledevts is (re-)filled with the current value on failure, so
             * just try again.
             */
        }
        if(!isrunning && flags)
            althrd_create(&context->EventThread, EventThread, context);
        almtx_unlock(&context->EventThrdLock);
    }
    else
    {
        ALbitfieldSOFT enabledevts;
        bool isrunning;
        almtx_lock(&context->EventThrdLock);
        enabledevts = ATOMIC_LOAD(&context->EnabledEvts, almemory_order_relaxed);
        isrunning = !!enabledevts;
        while(ATOMIC_COMPARE_EXCHANGE_WEAK(&context->EnabledEvts, &enabledevts, enabledevts&~flags,
                                           almemory_order_acq_rel, almemory_order_acquire) == 0)
        {
        }
        if(isrunning && !(enabledevts&~flags))
        {
            static const AsyncEvent kill_evt = { 0 };
            while(ll_ringbuffer_write_space(context->AsyncEvents) == 0)
                althrd_yield();
            ll_ringbuffer_write(context->AsyncEvents, (const char*)&kill_evt, 1);
            althrd_join(context->EventThread, NULL);
        }
        else
        {
            /* Wait to ensure the event handler sees the changed flags before
             * returning.
             */
            almtx_lock(&context->EventCbLock);
            almtx_unlock(&context->EventCbLock);
        }
        almtx_unlock(&context->EventThrdLock);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alEventCallbackSOFT(ALEVENTPROCSOFT callback, void *userParam)
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    almtx_lock(&context->EventCbLock);
    context->EventCb = callback;
    context->EventParam = userParam;
    almtx_unlock(&context->EventCbLock);

    ALCcontext_DecRef(context);
}
