
#include "config.h"

#include "AL/alc.h"
#include "AL/al.h"
#include "AL/alext.h"
#include "alMain.h"
#include "alError.h"
#include "alAuxEffectSlot.h"
#include "ringbuffer.h"


int EventThread(void *arg)
{
    ALCcontext *context = arg;
    bool quitnow = false;

    while(!quitnow)
    {
        ALbitfieldSOFT enabledevts;
        AsyncEvent evt;

        if(ll_ringbuffer_read(context->AsyncEvents, (char*)&evt, 1) == 0)
        {
            alsem_wait(&context->EventSem);
            continue;
        }

        almtx_lock(&context->EventCbLock);
        do {
            quitnow = evt.EnumType == EventType_KillThread;
            if(quitnow) break;

            if(evt.EnumType == EventType_ReleaseEffectState)
            {
                ALeffectState_DecRef(evt.u.EffectState);
                continue;
            }

            enabledevts = ATOMIC_LOAD(&context->EnabledEvts, almemory_order_acquire);
            if(context->EventCb && (enabledevts&evt.EnumType) == evt.EnumType)
                context->EventCb(evt.u.user.type, evt.u.user.id, evt.u.user.param,
                    (ALsizei)strlen(evt.u.user.msg), evt.u.user.msg, context->EventParam
                );
        } while(ll_ringbuffer_read(context->AsyncEvents, (char*)&evt, 1) != 0);
        almtx_unlock(&context->EventCbLock);
    }
    return 0;
}

AL_API void AL_APIENTRY alEventControlSOFT(ALsizei count, const ALenum *types, ALboolean enable)
{
    ALCcontext *context;
    ALbitfieldSOFT enabledevts;
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
        else if(types[i] == AL_EVENT_TYPE_DISCONNECTED_SOFT)
            flags |= EventType_Disconnected;
        else
            SETERR_GOTO(context, AL_INVALID_ENUM, done, "Invalid event type 0x%04x", types[i]);
    }

    if(enable)
    {
        enabledevts = ATOMIC_LOAD(&context->EnabledEvts, almemory_order_relaxed);
        while(ATOMIC_COMPARE_EXCHANGE_WEAK(&context->EnabledEvts, &enabledevts, enabledevts|flags,
                                           almemory_order_acq_rel, almemory_order_acquire) == 0)
        {
            /* enabledevts is (re-)filled with the current value on failure, so
             * just try again.
             */
        }
    }
    else
    {
        enabledevts = ATOMIC_LOAD(&context->EnabledEvts, almemory_order_relaxed);
        while(ATOMIC_COMPARE_EXCHANGE_WEAK(&context->EnabledEvts, &enabledevts, enabledevts&~flags,
                                           almemory_order_acq_rel, almemory_order_acquire) == 0)
        {
        }
        /* Wait to ensure the event handler sees the changed flags before
         * returning.
         */
        almtx_lock(&context->EventCbLock);
        almtx_unlock(&context->EventCbLock);
    }

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alEventCallbackSOFT(ALEVENTPROCSOFT callback, void *userParam)
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    almtx_lock(&context->PropLock);
    almtx_lock(&context->EventCbLock);
    context->EventCb = callback;
    context->EventParam = userParam;
    almtx_unlock(&context->EventCbLock);
    almtx_unlock(&context->PropLock);

    ALCcontext_DecRef(context);
}
