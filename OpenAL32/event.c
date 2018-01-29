
#include "config.h"

#include "AL/alc.h"
#include "AL/al.h"
#include "AL/alext.h"
#include "alMain.h"
#include "alError.h"


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

    almtx_lock(&context->EventLock);
    if(enable)
    {
        ALbitfieldSOFT enabledevts = ATOMIC_LOAD(&context->EnabledEvts, almemory_order_relaxed);
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
        ALbitfieldSOFT enabledevts = ATOMIC_LOAD(&context->EnabledEvts, almemory_order_relaxed);
        while(ATOMIC_COMPARE_EXCHANGE_WEAK(&context->EnabledEvts, &enabledevts, enabledevts&~flags,
                                           almemory_order_acq_rel, almemory_order_acquire) == 0)
        {
        }
    }
    almtx_unlock(&context->EventLock);

done:
    ALCcontext_DecRef(context);
}

AL_API void AL_APIENTRY alEventCallbackSOFT(ALEVENTPROCSOFT callback, void *userParam)
{
    ALCcontext *context;

    context = GetContextRef();
    if(!context) return;

    almtx_lock(&context->EventLock);
    context->EventCb = callback;
    context->EventParam = userParam;
    almtx_unlock(&context->EventLock);

    ALCcontext_DecRef(context);
}
