/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <stdlib.h>
#include <math.h>

#include <thread>
#include <algorithm>

#include "AL/al.h"
#include "AL/alc.h"

#include "alMain.h"
#include "alcontext.h"
#include "alAuxEffectSlot.h"
#include "alError.h"
#include "alListener.h"
#include "alSource.h"

#include "fpu_modes.h"
#include "almalloc.h"


namespace {

inline ALeffectslot *LookupEffectSlot(ALCcontext *context, ALuint id) noexcept
{
    --id;
    if(UNLIKELY(id >= context->EffectSlotList.size()))
        return nullptr;
    return context->EffectSlotList[id].get();
}

inline ALeffect *LookupEffect(ALCdevice *device, ALuint id) noexcept
{
    ALuint lidx = (id-1) >> 6;
    ALsizei slidx = (id-1) & 0x3f;

    if(UNLIKELY(lidx >= device->EffectList.size()))
        return nullptr;
    EffectSubList &sublist = device->EffectList[lidx];
    if(UNLIKELY(sublist.FreeMask & (U64(1)<<slidx)))
        return nullptr;
    return sublist.Effects + slidx;
}


void AddActiveEffectSlots(const ALuint *slotids, ALsizei count, ALCcontext *context)
{
    if(count < 1) return;
    ALeffectslotArray *curarray{context->ActiveAuxSlots.load(std::memory_order_acquire)};
    ALsizei newcount{curarray->count + count};

    /* Insert the new effect slots into the head of the array, followed by the
     * existing ones. Allocate twice as much space for effect slots so the
     * mixer has a place to sort them.
     */
    auto newarray = static_cast<ALeffectslotArray*>(al_calloc(DEF_ALIGN,
        FAM_SIZE(ALeffectslotArray, slot, newcount*2)));
    newarray->count = newcount;
    auto slotiter = std::transform(slotids, slotids+count, newarray->slot,
        [context](ALuint id) noexcept -> ALeffectslot*
        { return LookupEffectSlot(context, id); }
    );
    std::copy_n(curarray->slot, curarray->count, slotiter);

    /* Remove any duplicates (first instance of each will be kept). */
    ALeffectslot **last = newarray->slot + newarray->count;
    for(ALeffectslot **start=newarray->slot+1;;)
    {
        last = std::remove(start, last, *(start-1));
        if(start == last) break;
        ++start;
    }
    newcount = static_cast<ALsizei>(std::distance(newarray->slot, last));

    /* Reallocate newarray if the new size ended up smaller from duplicate
     * removal.
     */
    if(UNLIKELY(newcount < newarray->count))
    {
        curarray = newarray;
        newarray = static_cast<ALeffectslotArray*>(al_calloc(DEF_ALIGN,
            FAM_SIZE(ALeffectslotArray, slot, newcount*2)));
        newarray->count = newcount;
        std::copy_n(curarray->slot, newcount, newarray->slot);
        al_free(curarray);
        curarray = nullptr;
    }

    curarray = context->ActiveAuxSlots.exchange(newarray, std::memory_order_acq_rel);
    ALCdevice *device{context->Device};
    while((device->MixCount.load(std::memory_order_acquire)&1))
        std::this_thread::yield();
    al_free(curarray);
}

void RemoveActiveEffectSlots(const ALuint *slotids, ALsizei count, ALCcontext *context)
{
    if(count < 1) return;
    ALeffectslotArray *curarray{context->ActiveAuxSlots.load(std::memory_order_acquire)};

    /* Don't shrink the allocated array size since we don't know how many (if
     * any) of the effect slots to remove are in the array.
     */
    auto newarray = static_cast<ALeffectslotArray*>(al_calloc(DEF_ALIGN,
        FAM_SIZE(ALeffectslotArray, slot, curarray->count*2)));

    /* Copy each element in curarray to newarray whose ID is not in slotids. */
    const ALuint *slotids_end{slotids + count};
    auto slotiter = std::copy_if(curarray->slot, curarray->slot+curarray->count, newarray->slot,
        [slotids, slotids_end](const ALeffectslot *slot) -> bool
        { return std::find(slotids, slotids_end, slot->id) == slotids_end; }
    );
    newarray->count = static_cast<ALsizei>(std::distance(newarray->slot, slotiter));

    /* TODO: Could reallocate newarray now that we know it's needed size. */

    curarray = context->ActiveAuxSlots.exchange(newarray, std::memory_order_acq_rel);
    ALCdevice *device{context->Device};
    while((device->MixCount.load(std::memory_order_acquire)&1))
        std::this_thread::yield();
    al_free(curarray);
}

constexpr struct FactoryItem {
    ALenum Type;
    EffectStateFactory* (&GetFactory)(void);
} FactoryList[] = {
    { AL_EFFECT_NULL, NullStateFactory_getFactory },
    { AL_EFFECT_EAXREVERB, ReverbStateFactory_getFactory },
    { AL_EFFECT_REVERB, ReverbStateFactory_getFactory },
    { AL_EFFECT_AUTOWAH, AutowahStateFactory_getFactory },
    { AL_EFFECT_CHORUS, ChorusStateFactory_getFactory },
    { AL_EFFECT_COMPRESSOR, CompressorStateFactory_getFactory },
    { AL_EFFECT_DISTORTION, DistortionStateFactory_getFactory },
    { AL_EFFECT_ECHO, EchoStateFactory_getFactory },
    { AL_EFFECT_EQUALIZER, EqualizerStateFactory_getFactory },
    { AL_EFFECT_FLANGER, FlangerStateFactory_getFactory },
    { AL_EFFECT_FREQUENCY_SHIFTER, FshifterStateFactory_getFactory },
    { AL_EFFECT_RING_MODULATOR, ModulatorStateFactory_getFactory },
    { AL_EFFECT_PITCH_SHIFTER, PshifterStateFactory_getFactory},
    { AL_EFFECT_DEDICATED_DIALOGUE, DedicatedStateFactory_getFactory },
    { AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT, DedicatedStateFactory_getFactory }
};

inline EffectStateFactory *getFactoryByType(ALenum type)
{
    auto iter = std::find_if(std::begin(FactoryList), std::end(FactoryList),
        [type](const FactoryItem &item) noexcept -> bool
        { return item.Type == type; }
    );
    return (iter != std::end(FactoryList)) ? iter->GetFactory() : nullptr;
}


#define DO_UPDATEPROPS() do {                                                 \
    if(!context->DeferUpdates.load(std::memory_order_acquire))                \
        UpdateEffectSlotProps(slot, context.get());                           \
    else                                                                      \
        slot->PropsClean.clear(std::memory_order_release);                    \
} while(0)

} // namespace

AL_API ALvoid AL_APIENTRY alGenAuxiliaryEffectSlots(ALsizei n, ALuint *effectslots)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(n < 0)
        SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Generating %d effect slots", n);
    if(n == 0) return;

    std::unique_lock<std::mutex> slotlock{context->EffectSlotLock};
    ALCdevice *device{context->Device};
    for(ALsizei cur{0};cur < n;cur++)
    {
        auto iter = std::find_if(context->EffectSlotList.begin(), context->EffectSlotList.end(),
            [](const ALeffectslotPtr &entry) noexcept -> bool
            { return !entry; }
        );
        if(iter == context->EffectSlotList.end())
        {
            if(UNLIKELY(device->AuxiliaryEffectSlotMax == context->EffectSlotList.size()))
            {
                slotlock.unlock();
                alDeleteAuxiliaryEffectSlots(cur, effectslots);
                alSetError(context.get(), AL_OUT_OF_MEMORY,
                    "Exceeding %u auxiliary effect slot limit", device->AuxiliaryEffectSlotMax);
                return;
            }
            context->EffectSlotList.emplace_back(nullptr);
            iter = context->EffectSlotList.end() - 1;
        }

        *iter = std::unique_ptr<ALeffectslot>(new ALeffectslot{});
        ALenum err{InitEffectSlot(iter->get())};
        if(err != AL_NO_ERROR)
        {
            *iter = nullptr;
            slotlock.unlock();

            alDeleteAuxiliaryEffectSlots(cur, effectslots);
            alSetError(context.get(), err, "Effect slot object allocation failed");
            return;
        }
        aluInitEffectPanning(iter->get());

        auto id = static_cast<ALuint>(std::distance(context->EffectSlotList.begin(), iter) + 1);
        (*iter)->id = id;
        effectslots[cur] = id;
    }
    AddActiveEffectSlots(effectslots, n, context.get());
}

AL_API ALvoid AL_APIENTRY alDeleteAuxiliaryEffectSlots(ALsizei n, const ALuint *effectslots)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    if(n < 0)
        SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Deleting %d effect slots", n);
    if(n == 0) return;

    std::lock_guard<std::mutex> _{context->EffectSlotLock};
    auto effectslots_end = effectslots + n;
    auto bad_slot = std::find_if(effectslots, effectslots_end,
        [&context](ALuint id) -> bool
        {
            ALeffectslot *slot{LookupEffectSlot(context.get(), id)};
            if(!slot)
            {
                alSetError(context.get(), AL_INVALID_NAME, "Invalid effect slot ID %u", id);
                return true;
            }
            if(ReadRef(&slot->ref) != 0)
            {
                alSetError(context.get(), AL_INVALID_NAME, "Deleting in-use effect slot %u", id);
                return true;
            }
            return false;
        }
    );
    if(bad_slot != effectslots_end)
        return;

    // All effectslots are valid, remove and delete them
    RemoveActiveEffectSlots(effectslots, n, context.get());
    std::for_each(effectslots, effectslots_end,
        [&context](ALuint id) noexcept -> void
        { context->EffectSlotList[id-1] = nullptr; }
    );
}

AL_API ALboolean AL_APIENTRY alIsAuxiliaryEffectSlot(ALuint effectslot)
{
    ContextRef context{GetContextRef()};
    if(LIKELY(context))
    {
        std::lock_guard<std::mutex> _{context->EffectSlotLock};
        if(LookupEffectSlot(context.get(), effectslot) != nullptr)
            return AL_TRUE;
    }
    return AL_FALSE;
}

AL_API ALvoid AL_APIENTRY alAuxiliaryEffectSloti(ALuint effectslot, ALenum param, ALint value)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->PropLock};
    std::lock_guard<std::mutex> __{context->EffectSlotLock};
    ALeffectslot *slot = LookupEffectSlot(context.get(), effectslot);
    if(UNLIKELY(!slot))
        SETERR_RETURN(context.get(), AL_INVALID_NAME,, "Invalid effect slot ID %u", effectslot);

    ALeffectslot *target{};
    ALCdevice *device{};
    ALenum err{};
    switch(param)
    {
    case AL_EFFECTSLOT_EFFECT:
        device = context->Device;

        { std::lock_guard<std::mutex> ___{device->EffectLock};
            ALeffect *effect{value ? LookupEffect(device, value) : nullptr};
            if(!(value == 0 || effect != nullptr))
                SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Invalid effect ID %u", value);
            err = InitializeEffect(context.get(), slot, effect);
        }
        if(err != AL_NO_ERROR)
        {
            alSetError(context.get(), err, "Effect initialization failed");
            return;
        }
        break;

    case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
        if(!(value == AL_TRUE || value == AL_FALSE))
            SETERR_RETURN(context.get(), AL_INVALID_VALUE,,
                          "Effect slot auxiliary send auto out of range");
        slot->AuxSendAuto = value;
        break;

    case AL_EFFECTSLOT_TARGET_SOFT:
        target = (value ? LookupEffectSlot(context.get(), value) : nullptr);
        if(value && !target)
            SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Invalid effect slot target ID");
        if(target)
        {
            ALeffectslot *checker{target};
            while(checker && checker != slot)
                checker = checker->Target;
            if(checker)
                SETERR_RETURN(context.get(), AL_INVALID_OPERATION,,
                    "Setting target of effect slot ID %u to %u creates circular chain", slot->id,
                    target->id);
        }

        if(ALeffectslot *oldtarget{slot->Target})
        {
            /* We must force an update if there was an existing effect slot
             * target, in case it's about to be deleted.
             */
            if(target) IncrementRef(&target->ref);
            DecrementRef(&oldtarget->ref);
            slot->Target = target;
            UpdateEffectSlotProps(slot, context.get());
            return;
        }

        if(target) IncrementRef(&target->ref);
        slot->Target = target;
        break;

    default:
        SETERR_RETURN(context.get(), AL_INVALID_ENUM,,
                      "Invalid effect slot integer property 0x%04x", param);
    }
    DO_UPDATEPROPS();
}

AL_API ALvoid AL_APIENTRY alAuxiliaryEffectSlotiv(ALuint effectslot, ALenum param, const ALint *values)
{
    switch(param)
    {
    case AL_EFFECTSLOT_EFFECT:
    case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
    case AL_EFFECTSLOT_TARGET_SOFT:
        alAuxiliaryEffectSloti(effectslot, param, values[0]);
        return;
    }

    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->EffectSlotLock};
    ALeffectslot *slot = LookupEffectSlot(context.get(), effectslot);
    if(UNLIKELY(!slot))
        SETERR_RETURN(context.get(), AL_INVALID_NAME,, "Invalid effect slot ID %u", effectslot);

    switch(param)
    {
    default:
        SETERR_RETURN(context.get(), AL_INVALID_ENUM,,
                      "Invalid effect slot integer-vector property 0x%04x", param);
    }
}

AL_API ALvoid AL_APIENTRY alAuxiliaryEffectSlotf(ALuint effectslot, ALenum param, ALfloat value)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->PropLock};
    std::lock_guard<std::mutex> __{context->EffectSlotLock};
    ALeffectslot *slot = LookupEffectSlot(context.get(), effectslot);
    if(UNLIKELY(!slot))
        SETERR_RETURN(context.get(), AL_INVALID_NAME,, "Invalid effect slot ID %u", effectslot);

    switch(param)
    {
    case AL_EFFECTSLOT_GAIN:
        if(!(value >= 0.0f && value <= 1.0f))
            SETERR_RETURN(context.get(), AL_INVALID_VALUE,, "Effect slot gain out of range");
        slot->Gain = value;
        break;

    default:
        SETERR_RETURN(context.get(), AL_INVALID_ENUM,, "Invalid effect slot float property 0x%04x",
                      param);
    }
    DO_UPDATEPROPS();
}

AL_API ALvoid AL_APIENTRY alAuxiliaryEffectSlotfv(ALuint effectslot, ALenum param, const ALfloat *values)
{
    switch(param)
    {
    case AL_EFFECTSLOT_GAIN:
        alAuxiliaryEffectSlotf(effectslot, param, values[0]);
        return;
    }

    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->EffectSlotLock};
    ALeffectslot *slot = LookupEffectSlot(context.get(), effectslot);
    if(UNLIKELY(!slot))
        SETERR_RETURN(context.get(), AL_INVALID_NAME,, "Invalid effect slot ID %u", effectslot);

    switch(param)
    {
    default:
        SETERR_RETURN(context.get(), AL_INVALID_ENUM,,
                      "Invalid effect slot float-vector property 0x%04x", param);
    }
}

AL_API ALvoid AL_APIENTRY alGetAuxiliaryEffectSloti(ALuint effectslot, ALenum param, ALint *value)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->EffectSlotLock};
    ALeffectslot *slot = LookupEffectSlot(context.get(), effectslot);
    if(UNLIKELY(!slot))
        SETERR_RETURN(context.get(), AL_INVALID_NAME,, "Invalid effect slot ID %u", effectslot);

    switch(param)
    {
    case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
        *value = slot->AuxSendAuto;
        break;

    case AL_EFFECTSLOT_TARGET_SOFT:
        *value = slot->Target ? slot->Target->id : 0;
        break;

    default:
        SETERR_RETURN(context.get(), AL_INVALID_ENUM,,
                      "Invalid effect slot integer property 0x%04x", param);
    }
}

AL_API ALvoid AL_APIENTRY alGetAuxiliaryEffectSlotiv(ALuint effectslot, ALenum param, ALint *values)
{
    switch(param)
    {
    case AL_EFFECTSLOT_EFFECT:
    case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
    case AL_EFFECTSLOT_TARGET_SOFT:
        alGetAuxiliaryEffectSloti(effectslot, param, values);
        return;
    }

    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->EffectSlotLock};
    ALeffectslot *slot = LookupEffectSlot(context.get(), effectslot);
    if(UNLIKELY(!slot))
        SETERR_RETURN(context.get(), AL_INVALID_NAME,, "Invalid effect slot ID %u", effectslot);

    switch(param)
    {
    default:
        SETERR_RETURN(context.get(), AL_INVALID_ENUM,,
                      "Invalid effect slot integer-vector property 0x%04x", param);
    }
}

AL_API ALvoid AL_APIENTRY alGetAuxiliaryEffectSlotf(ALuint effectslot, ALenum param, ALfloat *value)
{
    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->EffectSlotLock};
    ALeffectslot *slot = LookupEffectSlot(context.get(), effectslot);
    if(UNLIKELY(!slot))
        SETERR_RETURN(context.get(), AL_INVALID_NAME,, "Invalid effect slot ID %u", effectslot);

    switch(param)
    {
    case AL_EFFECTSLOT_GAIN:
        *value = slot->Gain;
        break;

    default:
        SETERR_RETURN(context.get(), AL_INVALID_ENUM,,
                      "Invalid effect slot float property 0x%04x", param);
    }
}

AL_API ALvoid AL_APIENTRY alGetAuxiliaryEffectSlotfv(ALuint effectslot, ALenum param, ALfloat *values)
{
    switch(param)
    {
    case AL_EFFECTSLOT_GAIN:
        alGetAuxiliaryEffectSlotf(effectslot, param, values);
        return;
    }

    ContextRef context{GetContextRef()};
    if(UNLIKELY(!context)) return;

    std::lock_guard<std::mutex> _{context->EffectSlotLock};
    ALeffectslot *slot = LookupEffectSlot(context.get(), effectslot);
    if(UNLIKELY(!slot))
        SETERR_RETURN(context.get(), AL_INVALID_NAME,, "Invalid effect slot ID %u", effectslot);

    switch(param)
    {
    default:
        SETERR_RETURN(context.get(), AL_INVALID_ENUM,,
                      "Invalid effect slot float-vector property 0x%04x", param);
    }
}


ALenum InitializeEffect(ALCcontext *Context, ALeffectslot *EffectSlot, ALeffect *effect)
{
    ALenum newtype{effect ? effect->type : AL_EFFECT_NULL};
    if(newtype != EffectSlot->Effect.Type)
    {
        EffectStateFactory *factory{getFactoryByType(newtype)};
        if(!factory)
        {
            ERR("Failed to find factory for effect type 0x%04x\n", newtype);
            return AL_INVALID_ENUM;
        }
        EffectState *State{factory->create()};
        if(!State) return AL_OUT_OF_MEMORY;

        FPUCtl mixer_mode{};
        ALCdevice *Device{Context->Device};
        std::unique_lock<std::mutex> backlock{Device->BackendLock};
        State->mOutBuffer = Device->Dry.Buffer;
        State->mOutChannels = Device->Dry.NumChannels;
        if(State->deviceUpdate(Device) == AL_FALSE)
        {
            backlock.unlock();
            mixer_mode.leave();
            State->DecRef();
            return AL_OUT_OF_MEMORY;
        }
        mixer_mode.leave();

        if(!effect)
        {
            EffectSlot->Effect.Type = AL_EFFECT_NULL;
            EffectSlot->Effect.Props = ALeffectProps{};
        }
        else
        {
            EffectSlot->Effect.Type = effect->type;
            EffectSlot->Effect.Props = effect->Props;
        }

        EffectSlot->Effect.State->DecRef();
        EffectSlot->Effect.State = State;
    }
    else if(effect)
        EffectSlot->Effect.Props = effect->Props;

    /* Remove state references from old effect slot property updates. */
    ALeffectslotProps *props{Context->FreeEffectslotProps.load()};
    while(props)
    {
        if(props->State)
            props->State->DecRef();
        props->State = nullptr;
        props = props->next.load(std::memory_order_relaxed);
    }

    return AL_NO_ERROR;
}


void EffectState::IncRef() noexcept
{
    auto ref = IncrementRef(&mRef);
    TRACEREF("%p increasing refcount to %u\n", this, ref);
}

void EffectState::DecRef() noexcept
{
    auto ref = DecrementRef(&mRef);
    TRACEREF("%p decreasing refcount to %u\n", this, ref);
    if(ref == 0) delete this;
}


ALenum InitEffectSlot(ALeffectslot *slot)
{
    EffectStateFactory *factory{getFactoryByType(slot->Effect.Type)};
    if(!factory) return AL_INVALID_VALUE;
    slot->Effect.State = factory->create();
    if(!slot->Effect.State) return AL_OUT_OF_MEMORY;

    slot->Effect.State->IncRef();
    slot->Params.mEffectState = slot->Effect.State;
    return AL_NO_ERROR;
}

ALeffectslot::~ALeffectslot()
{
    if(Target)
        DecrementRef(&Target->ref);
    Target = nullptr;

    ALeffectslotProps *props{Update.load()};
    if(props)
    {
        if(props->State) props->State->DecRef();
        TRACE("Freed unapplied AuxiliaryEffectSlot update %p\n", props);
        al_free(props);
    }

    if(Effect.State)
        Effect.State->DecRef();
    if(Params.mEffectState)
        Params.mEffectState->DecRef();
}

void UpdateEffectSlotProps(ALeffectslot *slot, ALCcontext *context)
{
    /* Get an unused property container, or allocate a new one as needed. */
    ALeffectslotProps *props{context->FreeEffectslotProps.load(std::memory_order_relaxed)};
    if(!props)
        props = static_cast<ALeffectslotProps*>(al_calloc(16, sizeof(*props)));
    else
    {
        ALeffectslotProps *next;
        do {
            next = props->next.load(std::memory_order_relaxed);
        } while(context->FreeEffectslotProps.compare_exchange_weak(props, next,
                std::memory_order_seq_cst, std::memory_order_acquire) == 0);
    }

    /* Copy in current property values. */
    props->Gain = slot->Gain;
    props->AuxSendAuto = slot->AuxSendAuto;
    props->Target = slot->Target;

    props->Type = slot->Effect.Type;
    props->Props = slot->Effect.Props;
    /* Swap out any stale effect state object there may be in the container, to
     * delete it.
     */
    EffectState *oldstate{props->State};
    slot->Effect.State->IncRef();
    props->State = slot->Effect.State;

    /* Set the new container for updating internal parameters. */
    props = slot->Update.exchange(props, std::memory_order_acq_rel);
    if(props)
    {
        /* If there was an unused update container, put it back in the
         * freelist.
         */
        if(props->State)
            props->State->DecRef();
        props->State = nullptr;
        AtomicReplaceHead(context->FreeEffectslotProps, props);
    }

    if(oldstate)
        oldstate->DecRef();
}

void UpdateAllEffectSlotProps(ALCcontext *context)
{
    std::lock_guard<std::mutex> _{context->EffectSlotLock};
    ALeffectslotArray *auxslots{context->ActiveAuxSlots.load(std::memory_order_acquire)};
    for(ALsizei i{0};i < auxslots->count;i++)
    {
        ALeffectslot *slot = auxslots->slot[i];
        if(!slot->PropsClean.test_and_set(std::memory_order_acq_rel))
            UpdateEffectSlotProps(slot, context);
    }
}
