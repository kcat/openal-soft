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

#include "auxeffectslot.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"
#include "AL/efx.h"

#include "albit.h"
#include "alc/alu.h"
#include "alc/context.h"
#include "alc/device.h"
#include "alc/effects/base.h"
#include "alc/inprogext.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "alspan.h"
#include "atomic.h"
#include "buffer.h"
#include "core/buffer_storage.h"
#include "core/device.h"
#include "core/fpu_ctrl.h"
#include "core/logging.h"
#include "direct_defs.h"
#include "effect.h"
#include "flexarray.h"
#include "opthelpers.h"

#ifdef ALSOFT_EAX
#include "eax/api.h"
#include "eax/call.h"
#include "eax/effect.h"
#include "eax/fx_slot_index.h"
#include "eax/utils.h"
#endif

namespace {

using SubListAllocator = al::allocator<std::array<ALeffectslot,64>>;

EffectStateFactory *getFactoryByType(EffectSlotType type)
{
    switch(type)
    {
    case EffectSlotType::None: return NullStateFactory_getFactory();
    case EffectSlotType::EAXReverb: return ReverbStateFactory_getFactory();
    case EffectSlotType::Reverb: return StdReverbStateFactory_getFactory();
    case EffectSlotType::Autowah: return AutowahStateFactory_getFactory();
    case EffectSlotType::Chorus: return ChorusStateFactory_getFactory();
    case EffectSlotType::Compressor: return CompressorStateFactory_getFactory();
    case EffectSlotType::Distortion: return DistortionStateFactory_getFactory();
    case EffectSlotType::Echo: return EchoStateFactory_getFactory();
    case EffectSlotType::Equalizer: return EqualizerStateFactory_getFactory();
    case EffectSlotType::Flanger: return FlangerStateFactory_getFactory();
    case EffectSlotType::FrequencyShifter: return FshifterStateFactory_getFactory();
    case EffectSlotType::RingModulator: return ModulatorStateFactory_getFactory();
    case EffectSlotType::PitchShifter: return PshifterStateFactory_getFactory();
    case EffectSlotType::VocalMorpher: return VmorpherStateFactory_getFactory();
    case EffectSlotType::DedicatedDialog: return DedicatedDialogStateFactory_getFactory();
    case EffectSlotType::DedicatedLFE: return DedicatedLfeStateFactory_getFactory();
    case EffectSlotType::Convolution: return ConvolutionStateFactory_getFactory();
    }
    return nullptr;
}


inline ALeffectslot *LookupEffectSlot(ALCcontext *context, ALuint id) noexcept
{
    const size_t lidx{(id-1) >> 6};
    const ALuint slidx{(id-1) & 0x3f};

    if(lidx >= context->mEffectSlotList.size()) UNLIKELY
        return nullptr;
    EffectSlotSubList &sublist{context->mEffectSlotList[lidx]};
    if(sublist.FreeMask & (1_u64 << slidx)) UNLIKELY
        return nullptr;
    return al::to_address(sublist.EffectSlots->begin() + slidx);
}

inline ALeffect *LookupEffect(ALCdevice *device, ALuint id) noexcept
{
    const size_t lidx{(id-1) >> 6};
    const ALuint slidx{(id-1) & 0x3f};

    if(lidx >= device->EffectList.size()) UNLIKELY
        return nullptr;
    EffectSubList &sublist = device->EffectList[lidx];
    if(sublist.FreeMask & (1_u64 << slidx)) UNLIKELY
        return nullptr;
    return al::to_address(sublist.Effects->begin() + slidx);
}

inline ALbuffer *LookupBuffer(ALCdevice *device, ALuint id) noexcept
{
    const size_t lidx{(id-1) >> 6};
    const ALuint slidx{(id-1) & 0x3f};

    if(lidx >= device->BufferList.size()) UNLIKELY
        return nullptr;
    BufferSubList &sublist = device->BufferList[lidx];
    if(sublist.FreeMask & (1_u64 << slidx)) UNLIKELY
        return nullptr;
    return al::to_address(sublist.Buffers->begin() + slidx);
}


void AddActiveEffectSlots(const al::span<ALeffectslot*> auxslots, ALCcontext *context)
{
    if(auxslots.empty()) return;
    EffectSlotArray *curarray{context->mActiveAuxSlots.load(std::memory_order_acquire)};
    size_t newcount{curarray->size() + auxslots.size()};

    /* Insert the new effect slots into the head of the array, followed by the
     * existing ones.
     */
    auto newarray = EffectSlot::CreatePtrArray(newcount);
    auto slotiter = std::transform(auxslots.begin(), auxslots.end(), newarray->begin(),
        [](ALeffectslot *auxslot) noexcept { return auxslot->mSlot; });
    std::copy(curarray->begin(), curarray->end(), slotiter);

    /* Remove any duplicates (first instance of each will be kept). */
    auto last = newarray->end();
    for(auto start=newarray->begin()+1;;)
    {
        last = std::remove(start, last, *(start-1));
        if(start == last) break;
        ++start;
    }
    newcount = static_cast<size_t>(std::distance(newarray->begin(), last));

    /* Reallocate newarray if the new size ended up smaller from duplicate
     * removal.
     */
    if(newcount < newarray->size()) UNLIKELY
    {
        auto oldarray = std::move(newarray);
        newarray = EffectSlot::CreatePtrArray(newcount);
        std::copy_n(oldarray->begin(), newcount, newarray->begin());
    }
    std::uninitialized_fill_n(newarray->end(), newcount, nullptr);

    auto oldarray = context->mActiveAuxSlots.exchange(std::move(newarray),
        std::memory_order_acq_rel);
    std::ignore = context->mDevice->waitForMix();

    std::destroy_n(oldarray->end(), oldarray->size());
}

void RemoveActiveEffectSlots(const al::span<ALeffectslot*> auxslots, ALCcontext *context)
{
    if(auxslots.empty()) return;
    EffectSlotArray *curarray{context->mActiveAuxSlots.load(std::memory_order_acquire)};

    /* Don't shrink the allocated array size since we don't know how many (if
     * any) of the effect slots to remove are in the array.
     */
    auto newarray = EffectSlot::CreatePtrArray(curarray->size());

    auto new_end = std::copy(curarray->begin(), curarray->end(), newarray->begin());
    /* Remove elements from newarray that match any ID in slotids. */
    for(const ALeffectslot *auxslot : auxslots)
    {
        auto slot_match = [auxslot](EffectSlot *slot) noexcept -> bool
        { return (slot == auxslot->mSlot); };
        new_end = std::remove_if(newarray->begin(), new_end, slot_match);
    }

    /* Reallocate with the new size. */
    auto newsize = static_cast<size_t>(std::distance(newarray->begin(), new_end));
    if(newsize != newarray->size()) LIKELY
    {
        auto oldarray = std::move(newarray);
        newarray = EffectSlot::CreatePtrArray(newsize);
        std::copy_n(oldarray->begin(), newsize, newarray->begin());
    }
    std::uninitialized_fill_n(newarray->end(), newsize, nullptr);

    auto oldarray = context->mActiveAuxSlots.exchange(std::move(newarray),
        std::memory_order_acq_rel);
    std::ignore = context->mDevice->waitForMix();

    std::destroy_n(curarray->end(), curarray->size());
}


EffectSlotType EffectSlotTypeFromEnum(ALenum type)
{
    switch(type)
    {
    case AL_EFFECT_NULL: return EffectSlotType::None;
    case AL_EFFECT_REVERB: return EffectSlotType::Reverb;
    case AL_EFFECT_CHORUS: return EffectSlotType::Chorus;
    case AL_EFFECT_DISTORTION: return EffectSlotType::Distortion;
    case AL_EFFECT_ECHO: return EffectSlotType::Echo;
    case AL_EFFECT_FLANGER: return EffectSlotType::Flanger;
    case AL_EFFECT_FREQUENCY_SHIFTER: return EffectSlotType::FrequencyShifter;
    case AL_EFFECT_VOCAL_MORPHER: return EffectSlotType::VocalMorpher;
    case AL_EFFECT_PITCH_SHIFTER: return EffectSlotType::PitchShifter;
    case AL_EFFECT_RING_MODULATOR: return EffectSlotType::RingModulator;
    case AL_EFFECT_AUTOWAH: return EffectSlotType::Autowah;
    case AL_EFFECT_COMPRESSOR: return EffectSlotType::Compressor;
    case AL_EFFECT_EQUALIZER: return EffectSlotType::Equalizer;
    case AL_EFFECT_EAXREVERB: return EffectSlotType::EAXReverb;
    case AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT: return EffectSlotType::DedicatedLFE;
    case AL_EFFECT_DEDICATED_DIALOGUE: return EffectSlotType::DedicatedDialog;
    case AL_EFFECT_CONVOLUTION_SOFT: return EffectSlotType::Convolution;
    }
    ERR("Unhandled effect enum: 0x%04x\n", type);
    return EffectSlotType::None;
}

bool EnsureEffectSlots(ALCcontext *context, size_t needed)
{
    size_t count{std::accumulate(context->mEffectSlotList.cbegin(),
        context->mEffectSlotList.cend(), 0_uz,
        [](size_t cur, const EffectSlotSubList &sublist) noexcept -> size_t
        { return cur + static_cast<ALuint>(al::popcount(sublist.FreeMask)); })};

    try {
        while(needed > count)
        {
            if(context->mEffectSlotList.size() >= 1<<25) UNLIKELY
                return false;

            EffectSlotSubList sublist{};
            sublist.FreeMask = ~0_u64;
            sublist.EffectSlots = SubListAllocator{}.allocate(1);
            context->mEffectSlotList.emplace_back(std::move(sublist));
            count += std::tuple_size_v<SubListAllocator::value_type>;
        }
    }
    catch(...) {
        return false;
    }
    return true;
}

ALeffectslot *AllocEffectSlot(ALCcontext *context)
{
    auto sublist = std::find_if(context->mEffectSlotList.begin(), context->mEffectSlotList.end(),
        [](const EffectSlotSubList &entry) noexcept -> bool
        { return entry.FreeMask != 0; });
    auto lidx = static_cast<ALuint>(std::distance(context->mEffectSlotList.begin(), sublist));
    auto slidx = static_cast<ALuint>(al::countr_zero(sublist->FreeMask));
    ASSUME(slidx < 64);

    ALeffectslot *slot{al::construct_at(al::to_address(sublist->EffectSlots->begin() + slidx),
        context)};
    aluInitEffectPanning(slot->mSlot, context);

    /* Add 1 to avoid ID 0. */
    slot->id = ((lidx<<6) | slidx) + 1;

    context->mNumEffectSlots += 1;
    sublist->FreeMask &= ~(1_u64 << slidx);

    return slot;
}

void FreeEffectSlot(ALCcontext *context, ALeffectslot *slot)
{
    context->mEffectSlotNames.erase(slot->id);

    const ALuint id{slot->id - 1};
    const size_t lidx{id >> 6};
    const ALuint slidx{id & 0x3f};

    std::destroy_at(slot);

    context->mEffectSlotList[lidx].FreeMask |= 1_u64 << slidx;
    context->mNumEffectSlots--;
}


inline void UpdateProps(ALeffectslot *slot, ALCcontext *context)
{
    if(!context->mDeferUpdates && slot->mState == SlotState::Playing)
    {
        slot->updateProps(context);
        return;
    }
    slot->mPropsDirty = true;
}

} // namespace


AL_API DECL_FUNC2(void, alGenAuxiliaryEffectSlots, ALsizei,n, ALuint*,effectslots)
FORCE_ALIGN void AL_APIENTRY alGenAuxiliaryEffectSlotsDirect(ALCcontext *context, ALsizei n,
    ALuint *effectslots) noexcept
{
    if(n < 0) UNLIKELY
        context->setError(AL_INVALID_VALUE, "Generating %d effect slots", n);
    if(n <= 0) UNLIKELY return;

    std::lock_guard<std::mutex> slotlock{context->mEffectSlotLock};
    ALCdevice *device{context->mALDevice.get()};
    if(static_cast<ALuint>(n) > device->AuxiliaryEffectSlotMax-context->mNumEffectSlots)
    {
        context->setError(AL_OUT_OF_MEMORY, "Exceeding %u effect slot limit (%u + %d)",
            device->AuxiliaryEffectSlotMax, context->mNumEffectSlots, n);
        return;
    }
    if(!EnsureEffectSlots(context, static_cast<ALuint>(n)))
    {
        context->setError(AL_OUT_OF_MEMORY, "Failed to allocate %d effectslot%s", n,
            (n==1) ? "" : "s");
        return;
    }

    if(n == 1)
    {
        /* Special handling for the easy and normal case. */
        *effectslots = AllocEffectSlot(context)->id;
    }
    else
    {
        std::vector<ALuint> ids;
        ALsizei count{n};
        ids.reserve(static_cast<ALuint>(count));
        do {
            ALeffectslot *slot{AllocEffectSlot(context)};
            ids.emplace_back(slot->id);
        } while(--count);
        const al::span eids{effectslots, static_cast<ALuint>(n)};
        std::copy(ids.cbegin(), ids.cend(), eids.begin());
    }
}

AL_API DECL_FUNC2(void, alDeleteAuxiliaryEffectSlots, ALsizei,n, const ALuint*,effectslots)
FORCE_ALIGN void AL_APIENTRY alDeleteAuxiliaryEffectSlotsDirect(ALCcontext *context, ALsizei n,
    const ALuint *effectslots) noexcept
{
    if(n < 0) UNLIKELY
        context->setError(AL_INVALID_VALUE, "Deleting %d effect slots", n);
    if(n <= 0) UNLIKELY return;

    std::lock_guard<std::mutex> slotlock{context->mEffectSlotLock};
    if(n == 1)
    {
        ALeffectslot *slot{LookupEffectSlot(context, effectslots[0])};
        if(!slot) UNLIKELY
        {
            context->setError(AL_INVALID_NAME, "Invalid effect slot ID %u", effectslots[0]);
            return;
        }
        if(slot->ref.load(std::memory_order_relaxed) != 0) UNLIKELY
        {
            context->setError(AL_INVALID_OPERATION, "Deleting in-use effect slot %u",
                effectslots[0]);
            return;
        }
        RemoveActiveEffectSlots({&slot, 1u}, context);
        FreeEffectSlot(context, slot);
    }
    else
    {
        const al::span eids{effectslots, static_cast<ALuint>(n)};
        auto slots = std::vector<ALeffectslot*>(static_cast<ALuint>(n));
        for(size_t i{0};i < slots.size();++i)
        {
            ALeffectslot *slot{LookupEffectSlot(context, eids[i])};
            if(!slot) UNLIKELY
            {
                context->setError(AL_INVALID_NAME, "Invalid effect slot ID %u", eids[i]);
                return;
            }
            if(slot->ref.load(std::memory_order_relaxed) != 0) UNLIKELY
            {
                context->setError(AL_INVALID_OPERATION, "Deleting in-use effect slot %u", eids[i]);
                return;
            }
            slots[i] = slot;
        }
        /* All effectslots are valid, remove and delete them */
        RemoveActiveEffectSlots(slots, context);

        auto delete_effectslot = [context](const ALuint eid) -> void
        {
            if(ALeffectslot *slot{LookupEffectSlot(context, eid)})
                FreeEffectSlot(context, slot);
        };
        std::for_each(eids.begin(), eids.end(), delete_effectslot);
    }
}

AL_API DECL_FUNC1(ALboolean, alIsAuxiliaryEffectSlot, ALuint,effectslot)
FORCE_ALIGN ALboolean AL_APIENTRY alIsAuxiliaryEffectSlotDirect(ALCcontext *context,
    ALuint effectslot) noexcept
{
    std::lock_guard<std::mutex> slotlock{context->mEffectSlotLock};
    if(LookupEffectSlot(context, effectslot) != nullptr)
        return AL_TRUE;
    return AL_FALSE;
}


AL_API void AL_APIENTRY alAuxiliaryEffectSlotPlaySOFT(ALuint slotid) noexcept
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    std::lock_guard<std::mutex> slotlock{context->mEffectSlotLock};
    ALeffectslot *slot{LookupEffectSlot(context.get(), slotid)};
    if(!slot) UNLIKELY
    {
        context->setError(AL_INVALID_NAME, "Invalid effect slot ID %u", slotid);
        return;
    }
    if(slot->mState == SlotState::Playing)
        return;

    slot->mPropsDirty = false;
    slot->updateProps(context.get());

    AddActiveEffectSlots({&slot, 1}, context.get());
    slot->mState = SlotState::Playing;
}

AL_API void AL_APIENTRY alAuxiliaryEffectSlotPlayvSOFT(ALsizei n, const ALuint *slotids) noexcept
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(n < 0) UNLIKELY
        context->setError(AL_INVALID_VALUE, "Playing %d effect slots", n);
    if(n <= 0) UNLIKELY return;

    auto slots = std::vector<ALeffectslot*>(static_cast<ALuint>(n));
    std::lock_guard<std::mutex> slotlock{context->mEffectSlotLock};
    for(size_t i{0};i < slots.size();++i)
    {
        ALeffectslot *slot{LookupEffectSlot(context.get(), slotids[i])};
        if(!slot) UNLIKELY
        {
            context->setError(AL_INVALID_NAME, "Invalid effect slot ID %u", slotids[i]);
            return;
        }

        if(slot->mState != SlotState::Playing)
        {
            slot->mPropsDirty = false;
            slot->updateProps(context.get());
        }
        slots[i] = slot;
    };

    AddActiveEffectSlots(slots, context.get());
    for(auto slot : slots)
        slot->mState = SlotState::Playing;
}

AL_API void AL_APIENTRY alAuxiliaryEffectSlotStopSOFT(ALuint slotid) noexcept
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    std::lock_guard<std::mutex> slotlock{context->mEffectSlotLock};
    ALeffectslot *slot{LookupEffectSlot(context.get(), slotid)};
    if(!slot) UNLIKELY
    {
        context->setError(AL_INVALID_NAME, "Invalid effect slot ID %u", slotid);
        return;
    }

    RemoveActiveEffectSlots({&slot, 1}, context.get());
    slot->mState = SlotState::Stopped;
}

AL_API void AL_APIENTRY alAuxiliaryEffectSlotStopvSOFT(ALsizei n, const ALuint *slotids) noexcept
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(n < 0) UNLIKELY
        context->setError(AL_INVALID_VALUE, "Stopping %d effect slots", n);
    if(n <= 0) UNLIKELY return;

    auto slots = std::vector<ALeffectslot*>(static_cast<ALuint>(n));
    std::lock_guard<std::mutex> slotlock{context->mEffectSlotLock};
    for(size_t i{0};i < slots.size();++i)
    {
        ALeffectslot *slot{LookupEffectSlot(context.get(), slotids[i])};
        if(!slot) UNLIKELY
        {
            context->setError(AL_INVALID_NAME, "Invalid effect slot ID %u", slotids[i]);
            return;
        }

        slots[i] = slot;
    };

    RemoveActiveEffectSlots(slots, context.get());
    for(auto slot : slots)
        slot->mState = SlotState::Stopped;
}


AL_API DECL_FUNC3(void, alAuxiliaryEffectSloti, ALuint,effectslot, ALenum,param, ALint,value)
FORCE_ALIGN void AL_APIENTRY alAuxiliaryEffectSlotiDirect(ALCcontext *context, ALuint effectslot,
    ALenum param, ALint value) noexcept
{
    std::lock_guard<std::mutex> proplock{context->mPropLock};
    std::lock_guard<std::mutex> slotlock{context->mEffectSlotLock};
    ALeffectslot *slot = LookupEffectSlot(context, effectslot);
    if(!slot) UNLIKELY
        return context->setError(AL_INVALID_NAME, "Invalid effect slot ID %u", effectslot);

    ALeffectslot *target{};
    ALCdevice *device{};
    ALenum err{};
    switch(param)
    {
    case AL_EFFECTSLOT_EFFECT:
        device = context->mALDevice.get();

        {
            std::lock_guard<std::mutex> effectlock{device->EffectLock};
            ALeffect *effect{value ? LookupEffect(device, static_cast<ALuint>(value)) : nullptr};
            if(effect)
                err = slot->initEffect(effect->id, effect->type, effect->Props, context);
            else
            {
                if(value != 0)
                    return context->setError(AL_INVALID_VALUE, "Invalid effect ID %u", value);
                err = slot->initEffect(0, AL_EFFECT_NULL, EffectProps{}, context);
            }
        }
        if(err != AL_NO_ERROR) UNLIKELY
        {
            context->setError(err, "Effect initialization failed");
            return;
        }
        if(slot->mState == SlotState::Initial) UNLIKELY
        {
            slot->mPropsDirty = false;
            slot->updateProps(context);

            AddActiveEffectSlots({&slot, 1}, context);
            slot->mState = SlotState::Playing;
            return;
        }
        break;

    case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
        if(!(value == AL_TRUE || value == AL_FALSE))
            return context->setError(AL_INVALID_VALUE,
                "Effect slot auxiliary send auto out of range");
        if(slot->AuxSendAuto == !!value) UNLIKELY
            return;
        slot->AuxSendAuto = !!value;
        break;

    case AL_EFFECTSLOT_TARGET_SOFT:
        target = LookupEffectSlot(context, static_cast<ALuint>(value));
        if(value && !target)
            return context->setError(AL_INVALID_VALUE, "Invalid effect slot target ID");
        if(slot->Target == target) UNLIKELY
            return;
        if(target)
        {
            ALeffectslot *checker{target};
            while(checker && checker != slot)
                checker = checker->Target;
            if(checker)
                return context->setError(AL_INVALID_OPERATION,
                    "Setting target of effect slot ID %u to %u creates circular chain", slot->id,
                    target->id);
        }

        if(ALeffectslot *oldtarget{slot->Target})
        {
            /* We must force an update if there was an existing effect slot
             * target, in case it's about to be deleted.
             */
            if(target) IncrementRef(target->ref);
            DecrementRef(oldtarget->ref);
            slot->Target = target;
            slot->updateProps(context);
            return;
        }

        if(target) IncrementRef(target->ref);
        slot->Target = target;
        break;

    case AL_BUFFER:
        device = context->mALDevice.get();

        if(slot->mState == SlotState::Playing)
            return context->setError(AL_INVALID_OPERATION,
                "Setting buffer on playing effect slot %u", slot->id);

        if(ALbuffer *buffer{slot->Buffer})
        {
            if(buffer->id == static_cast<ALuint>(value)) UNLIKELY
                return;
        }
        else if(value == 0) UNLIKELY
            return;

        {
            std::lock_guard<std::mutex> bufferlock{device->BufferLock};
            ALbuffer *buffer{};
            if(value)
            {
                buffer = LookupBuffer(device, static_cast<ALuint>(value));
                if(!buffer) return context->setError(AL_INVALID_VALUE, "Invalid buffer ID");
                if(buffer->mCallback)
                    return context->setError(AL_INVALID_OPERATION,
                        "Callback buffer not valid for effects");

                IncrementRef(buffer->ref);
            }

            if(ALbuffer *oldbuffer{slot->Buffer})
                DecrementRef(oldbuffer->ref);
            slot->Buffer = buffer;

            FPUCtl mixer_mode{};
            auto *state = slot->Effect.State.get();
            state->deviceUpdate(device, buffer);
        }
        break;

    case AL_EFFECTSLOT_STATE_SOFT:
        return context->setError(AL_INVALID_OPERATION, "AL_EFFECTSLOT_STATE_SOFT is read-only");

    default:
        return context->setError(AL_INVALID_ENUM, "Invalid effect slot integer property 0x%04x",
            param);
    }
    UpdateProps(slot, context);
}

AL_API DECL_FUNC3(void, alAuxiliaryEffectSlotiv, ALuint,effectslot, ALenum,param, const ALint*,values)
FORCE_ALIGN void AL_APIENTRY alAuxiliaryEffectSlotivDirect(ALCcontext *context, ALuint effectslot,
    ALenum param, const ALint *values) noexcept
{
    switch(param)
    {
    case AL_EFFECTSLOT_EFFECT:
    case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
    case AL_EFFECTSLOT_TARGET_SOFT:
    case AL_EFFECTSLOT_STATE_SOFT:
    case AL_BUFFER:
        alAuxiliaryEffectSlotiDirect(context, effectslot, param, values[0]);
        return;
    }

    std::lock_guard<std::mutex> slotlock{context->mEffectSlotLock};
    ALeffectslot *slot = LookupEffectSlot(context, effectslot);
    if(!slot) UNLIKELY
        return context->setError(AL_INVALID_NAME, "Invalid effect slot ID %u", effectslot);

    switch(param)
    {
    default:
        return context->setError(AL_INVALID_ENUM,
            "Invalid effect slot integer-vector property 0x%04x", param);
    }
}

AL_API DECL_FUNC3(void, alAuxiliaryEffectSlotf, ALuint,effectslot, ALenum,param, ALfloat,value)
FORCE_ALIGN void AL_APIENTRY alAuxiliaryEffectSlotfDirect(ALCcontext *context, ALuint effectslot,
    ALenum param, ALfloat value) noexcept
{
    std::lock_guard<std::mutex> proplock{context->mPropLock};
    std::lock_guard<std::mutex> slotlock{context->mEffectSlotLock};
    ALeffectslot *slot = LookupEffectSlot(context, effectslot);
    if(!slot) UNLIKELY
        return context->setError(AL_INVALID_NAME, "Invalid effect slot ID %u", effectslot);

    switch(param)
    {
    case AL_EFFECTSLOT_GAIN:
        if(!(value >= 0.0f && value <= 1.0f))
            return context->setError(AL_INVALID_VALUE, "Effect slot gain out of range");
        if(slot->Gain == value) UNLIKELY
            return;
        slot->Gain = value;
        break;

    default:
        return context->setError(AL_INVALID_ENUM, "Invalid effect slot float property 0x%04x",
            param);
    }
    UpdateProps(slot, context);
}

AL_API DECL_FUNC3(void, alAuxiliaryEffectSlotfv, ALuint,effectslot, ALenum,param, const ALfloat*,values)
FORCE_ALIGN void AL_APIENTRY alAuxiliaryEffectSlotfvDirect(ALCcontext *context, ALuint effectslot,
    ALenum param, const ALfloat *values) noexcept
{
    switch(param)
    {
    case AL_EFFECTSLOT_GAIN:
        alAuxiliaryEffectSlotfDirect(context, effectslot, param, values[0]);
        return;
    }

    std::lock_guard<std::mutex> slotlock{context->mEffectSlotLock};
    ALeffectslot *slot = LookupEffectSlot(context, effectslot);
    if(!slot) UNLIKELY
        return context->setError(AL_INVALID_NAME, "Invalid effect slot ID %u", effectslot);

    switch(param)
    {
    default:
        return context->setError(AL_INVALID_ENUM,
            "Invalid effect slot float-vector property 0x%04x", param);
    }
}


AL_API DECL_FUNC3(void, alGetAuxiliaryEffectSloti, ALuint,effectslot, ALenum,param, ALint*,value)
FORCE_ALIGN void AL_APIENTRY alGetAuxiliaryEffectSlotiDirect(ALCcontext *context,
    ALuint effectslot, ALenum param, ALint *value) noexcept
{
    std::lock_guard<std::mutex> slotlock{context->mEffectSlotLock};
    ALeffectslot *slot = LookupEffectSlot(context, effectslot);
    if(!slot) UNLIKELY
        return context->setError(AL_INVALID_NAME, "Invalid effect slot ID %u", effectslot);

    switch(param)
    {
    case AL_EFFECTSLOT_EFFECT:
        *value = static_cast<ALint>(slot->EffectId);
        break;

    case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
        *value = slot->AuxSendAuto ? AL_TRUE : AL_FALSE;
        break;

    case AL_EFFECTSLOT_TARGET_SOFT:
        if(auto *target = slot->Target)
            *value = static_cast<ALint>(target->id);
        else
            *value = 0;
        break;

    case AL_EFFECTSLOT_STATE_SOFT:
        *value = static_cast<int>(slot->mState);
        break;

    case AL_BUFFER:
        if(auto *buffer = slot->Buffer)
            *value = static_cast<ALint>(buffer->id);
        else
            *value = 0;
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid effect slot integer property 0x%04x", param);
    }
}

AL_API DECL_FUNC3(void, alGetAuxiliaryEffectSlotiv, ALuint,effectslot, ALenum,param, ALint*,values)
FORCE_ALIGN void AL_APIENTRY alGetAuxiliaryEffectSlotivDirect(ALCcontext *context,
    ALuint effectslot, ALenum param, ALint *values) noexcept
{
    switch(param)
    {
    case AL_EFFECTSLOT_EFFECT:
    case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
    case AL_EFFECTSLOT_TARGET_SOFT:
    case AL_EFFECTSLOT_STATE_SOFT:
    case AL_BUFFER:
        alGetAuxiliaryEffectSlotiDirect(context, effectslot, param, values);
        return;
    }

    std::lock_guard<std::mutex> slotlock{context->mEffectSlotLock};
    ALeffectslot *slot = LookupEffectSlot(context, effectslot);
    if(!slot) UNLIKELY
        return context->setError(AL_INVALID_NAME, "Invalid effect slot ID %u", effectslot);

    switch(param)
    {
    default:
        context->setError(AL_INVALID_ENUM, "Invalid effect slot integer-vector property 0x%04x",
            param);
    }
}

AL_API DECL_FUNC3(void, alGetAuxiliaryEffectSlotf, ALuint,effectslot, ALenum,param, ALfloat*,value)
FORCE_ALIGN void AL_APIENTRY alGetAuxiliaryEffectSlotfDirect(ALCcontext *context,
    ALuint effectslot, ALenum param, ALfloat *value) noexcept
{
    std::lock_guard<std::mutex> slotlock{context->mEffectSlotLock};
    ALeffectslot *slot = LookupEffectSlot(context, effectslot);
    if(!slot) UNLIKELY
        return context->setError(AL_INVALID_NAME, "Invalid effect slot ID %u", effectslot);

    switch(param)
    {
    case AL_EFFECTSLOT_GAIN:
        *value = slot->Gain;
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid effect slot float property 0x%04x", param);
    }
}

AL_API DECL_FUNC3(void, alGetAuxiliaryEffectSlotfv, ALuint,effectslot, ALenum,param, ALfloat*,values)
FORCE_ALIGN void AL_APIENTRY alGetAuxiliaryEffectSlotfvDirect(ALCcontext *context,
    ALuint effectslot, ALenum param, ALfloat *values) noexcept
{
    switch(param)
    {
    case AL_EFFECTSLOT_GAIN:
        alGetAuxiliaryEffectSlotfDirect(context, effectslot, param, values);
        return;
    }

    std::lock_guard<std::mutex> slotlock{context->mEffectSlotLock};
    ALeffectslot *slot = LookupEffectSlot(context, effectslot);
    if(!slot) UNLIKELY
        return context->setError(AL_INVALID_NAME, "Invalid effect slot ID %u", effectslot);

    switch(param)
    {
    default:
        context->setError(AL_INVALID_ENUM, "Invalid effect slot float-vector property 0x%04x",
            param);
    }
}


ALeffectslot::ALeffectslot(ALCcontext *context)
{
    EffectStateFactory *factory{getFactoryByType(EffectSlotType::None)};
    if(!factory) throw std::runtime_error{"Failed to get null effect factory"};

    al::intrusive_ptr<EffectState> state{factory->create()};
    Effect.State = state;

    mSlot = context->getEffectSlot();
    mSlot->InUse = true;
    mSlot->mEffectState = std::move(state);
}

ALeffectslot::~ALeffectslot()
{
    if(Target)
        DecrementRef(Target->ref);
    Target = nullptr;
    if(Buffer)
        DecrementRef(Buffer->ref);
    Buffer = nullptr;

    if(auto *slot = mSlot->Update.exchange(nullptr, std::memory_order_relaxed))
        slot->State = nullptr;

    mSlot->mEffectState = nullptr;
    mSlot->InUse = false;
}

ALenum ALeffectslot::initEffect(ALuint effectId, ALenum effectType, const EffectProps &effectProps,
    ALCcontext *context)
{
    EffectSlotType newtype{EffectSlotTypeFromEnum(effectType)};
    if(newtype != Effect.Type)
    {
        EffectStateFactory *factory{getFactoryByType(newtype)};
        if(!factory)
        {
            ERR("Failed to find factory for effect slot type %d\n", static_cast<int>(newtype));
            return AL_INVALID_ENUM;
        }
        al::intrusive_ptr<EffectState> state{factory->create()};

        ALCdevice *device{context->mALDevice.get()};
        std::unique_lock<std::mutex> statelock{device->StateLock};
        state->mOutTarget = device->Dry.Buffer;
        {
            FPUCtl mixer_mode{};
            state->deviceUpdate(device, Buffer);
        }

        Effect.Type = newtype;
        Effect.Props = effectProps;

        Effect.State = std::move(state);
    }
    else if(newtype != EffectSlotType::None)
        Effect.Props = effectProps;
    EffectId = effectId;

    /* Remove state references from old effect slot property updates. */
    EffectSlotProps *props{context->mFreeEffectSlotProps.load()};
    while(props)
    {
        props->State = nullptr;
        props = props->next.load(std::memory_order_relaxed);
    }

    return AL_NO_ERROR;
}

void ALeffectslot::updateProps(ALCcontext *context) const
{
    /* Get an unused property container, or allocate a new one as needed. */
    EffectSlotProps *props{context->mFreeEffectSlotProps.load(std::memory_order_acquire)};
    if(!props)
    {
        context->allocEffectSlotProps();
        props = context->mFreeEffectSlotProps.load(std::memory_order_acquire);
    }
    EffectSlotProps *next;
    do {
        next = props->next.load(std::memory_order_relaxed);
    } while(!context->mFreeEffectSlotProps.compare_exchange_weak(props, next,
        std::memory_order_acq_rel, std::memory_order_acquire));

    /* Copy in current property values. */
    props->Gain = Gain;
    props->AuxSendAuto = AuxSendAuto;
    props->Target = Target ? Target->mSlot : nullptr;

    props->Type = Effect.Type;
    props->Props = Effect.Props;
    props->State = Effect.State;

    /* Set the new container for updating internal parameters. */
    props = mSlot->Update.exchange(props, std::memory_order_acq_rel);
    if(props)
    {
        /* If there was an unused update container, put it back in the
         * freelist.
         */
        props->State = nullptr;
        AtomicReplaceHead(context->mFreeEffectSlotProps, props);
    }
}

void ALeffectslot::SetName(ALCcontext* context, ALuint id, std::string_view name)
{
    std::lock_guard<std::mutex> slotlock{context->mEffectSlotLock};

    auto slot = LookupEffectSlot(context, id);
    if(!slot) UNLIKELY
        return context->setError(AL_INVALID_NAME, "Invalid effect slot ID %u", id);

    context->mEffectSlotNames.insert_or_assign(id, name);
}

void UpdateAllEffectSlotProps(ALCcontext *context)
{
    std::lock_guard<std::mutex> slotlock{context->mEffectSlotLock};
    for(auto &sublist : context->mEffectSlotList)
    {
        uint64_t usemask{~sublist.FreeMask};
        while(usemask)
        {
            const auto idx = static_cast<uint>(al::countr_zero(usemask));
            usemask &= ~(1_u64 << idx);
            auto &slot = (*sublist.EffectSlots)[idx];

            if(slot.mState != SlotState::Stopped && std::exchange(slot.mPropsDirty, false))
                slot.updateProps(context);
        }
    }
}

EffectSlotSubList::~EffectSlotSubList()
{
    if(!EffectSlots)
        return;

    uint64_t usemask{~FreeMask};
    while(usemask)
    {
        const int idx{al::countr_zero(usemask)};
        std::destroy_at(al::to_address(EffectSlots->begin() + idx));
        usemask &= ~(1_u64 << idx);
    }
    FreeMask = ~usemask;
    SubListAllocator{}.deallocate(EffectSlots, 1);
    EffectSlots = nullptr;
}

#ifdef ALSOFT_EAX
void ALeffectslot::eax_initialize(ALCcontext& al_context, EaxFxSlotIndexValue index)
{
    if(index >= EAX_MAX_FXSLOTS)
        eax_fail("Index out of range.");

    eax_al_context_ = &al_context;
    eax_fx_slot_index_ = index;
    eax_fx_slot_set_defaults();

    eax_effect_ = std::make_unique<EaxEffect>();
    if(index == 0) eax_effect_->init<EaxReverbCommitter>();
    else if(index == 1) eax_effect_->init<EaxChorusCommitter>();
    else eax_effect_->init<EaxNullCommitter>();
}

void ALeffectslot::eax_commit()
{
    if(eax_df_ != EaxDirtyFlags{})
    {
        auto df = EaxDirtyFlags{};
        switch(eax_version_)
        {
        case 1:
        case 2:
        case 3:
            eax5_fx_slot_commit(eax123_, df);
            break;
        case 4:
            eax4_fx_slot_commit(df);
            break;
        case 5:
            eax5_fx_slot_commit(eax5_, df);
            break;
        }
        eax_df_ = EaxDirtyFlags{};

        if((df & eax_volume_dirty_bit) != EaxDirtyFlags{})
            eax_fx_slot_set_volume();
        if((df & eax_flags_dirty_bit) != EaxDirtyFlags{})
            eax_fx_slot_set_flags();
    }

    if(eax_effect_->commit(eax_version_))
        eax_set_efx_slot_effect(*eax_effect_);
}

[[noreturn]] void ALeffectslot::eax_fail(const char* message)
{
    throw Exception{message};
}

[[noreturn]] void ALeffectslot::eax_fail_unknown_effect_id()
{
    eax_fail("Unknown effect ID.");
}

[[noreturn]] void ALeffectslot::eax_fail_unknown_property_id()
{
    eax_fail("Unknown property ID.");
}

[[noreturn]] void ALeffectslot::eax_fail_unknown_version()
{
    eax_fail("Unknown version.");
}

void ALeffectslot::eax4_fx_slot_ensure_unlocked() const
{
    if(eax4_fx_slot_is_legacy())
        eax_fail("Locked legacy slot.");
}

ALenum ALeffectslot::eax_get_efx_effect_type(const GUID& guid)
{
    if(guid == EAX_NULL_GUID)
        return AL_EFFECT_NULL;
    if(guid == EAX_AUTOWAH_EFFECT)
        return AL_EFFECT_AUTOWAH;
    if(guid == EAX_CHORUS_EFFECT)
        return AL_EFFECT_CHORUS;
    if(guid == EAX_AGCCOMPRESSOR_EFFECT)
        return AL_EFFECT_COMPRESSOR;
    if(guid == EAX_DISTORTION_EFFECT)
        return AL_EFFECT_DISTORTION;
    if(guid == EAX_REVERB_EFFECT)
        return AL_EFFECT_EAXREVERB;
    if(guid == EAX_ECHO_EFFECT)
        return AL_EFFECT_ECHO;
    if(guid == EAX_EQUALIZER_EFFECT)
        return AL_EFFECT_EQUALIZER;
    if(guid == EAX_FLANGER_EFFECT)
        return AL_EFFECT_FLANGER;
    if(guid == EAX_FREQUENCYSHIFTER_EFFECT)
        return AL_EFFECT_FREQUENCY_SHIFTER;
    if(guid == EAX_PITCHSHIFTER_EFFECT)
        return AL_EFFECT_PITCH_SHIFTER;
    if(guid == EAX_RINGMODULATOR_EFFECT)
        return AL_EFFECT_RING_MODULATOR;
    if(guid == EAX_VOCALMORPHER_EFFECT)
        return AL_EFFECT_VOCAL_MORPHER;

    eax_fail_unknown_effect_id();
}

const GUID& ALeffectslot::eax_get_eax_default_effect_guid() const noexcept
{
    switch(eax_fx_slot_index_)
    {
    case 0: return EAX_REVERB_EFFECT;
    case 1: return EAX_CHORUS_EFFECT;
    default: return EAX_NULL_GUID;
    }
}

long ALeffectslot::eax_get_eax_default_lock() const noexcept
{
    return eax4_fx_slot_is_legacy() ? EAXFXSLOT_LOCKED : EAXFXSLOT_UNLOCKED;
}

void ALeffectslot::eax4_fx_slot_set_defaults(Eax4Props& props) noexcept
{
    props.guidLoadEffect = eax_get_eax_default_effect_guid();
    props.lVolume = EAXFXSLOT_DEFAULTVOLUME;
    props.lLock = eax_get_eax_default_lock();
    props.ulFlags = EAX40FXSLOT_DEFAULTFLAGS;
}

void ALeffectslot::eax5_fx_slot_set_defaults(Eax5Props& props) noexcept
{
    props.guidLoadEffect = eax_get_eax_default_effect_guid();
    props.lVolume = EAXFXSLOT_DEFAULTVOLUME;
    props.lLock = EAXFXSLOT_UNLOCKED;
    props.ulFlags = EAX50FXSLOT_DEFAULTFLAGS;
    props.lOcclusion = EAXFXSLOT_DEFAULTOCCLUSION;
    props.flOcclusionLFRatio = EAXFXSLOT_DEFAULTOCCLUSIONLFRATIO;
}

void ALeffectslot::eax_fx_slot_set_defaults()
{
    eax5_fx_slot_set_defaults(eax123_.i);
    eax4_fx_slot_set_defaults(eax4_.i);
    eax5_fx_slot_set_defaults(eax5_.i);
    eax_ = eax5_.i;
    eax_df_ = EaxDirtyFlags{};
}

void ALeffectslot::eax4_fx_slot_get(const EaxCall& call, const Eax4Props& props)
{
    switch(call.get_property_id())
    {
    case EAXFXSLOT_ALLPARAMETERS:
        call.set_value<Exception>(props);
        break;
    case EAXFXSLOT_LOADEFFECT:
        call.set_value<Exception>(props.guidLoadEffect);
        break;
    case EAXFXSLOT_VOLUME:
        call.set_value<Exception>(props.lVolume);
        break;
    case EAXFXSLOT_LOCK:
        call.set_value<Exception>(props.lLock);
        break;
    case EAXFXSLOT_FLAGS:
        call.set_value<Exception>(props.ulFlags);
        break;
    default:
        eax_fail_unknown_property_id();
    }
}

void ALeffectslot::eax5_fx_slot_get(const EaxCall& call, const Eax5Props& props)
{
    switch(call.get_property_id())
    {
    case EAXFXSLOT_ALLPARAMETERS:
        call.set_value<Exception>(props);
        break;
    case EAXFXSLOT_LOADEFFECT:
        call.set_value<Exception>(props.guidLoadEffect);
        break;
    case EAXFXSLOT_VOLUME:
        call.set_value<Exception>(props.lVolume);
        break;
    case EAXFXSLOT_LOCK:
        call.set_value<Exception>(props.lLock);
        break;
    case EAXFXSLOT_FLAGS:
        call.set_value<Exception>(props.ulFlags);
        break;
    case EAXFXSLOT_OCCLUSION:
        call.set_value<Exception>(props.lOcclusion);
        break;
    case EAXFXSLOT_OCCLUSIONLFRATIO:
        call.set_value<Exception>(props.flOcclusionLFRatio);
        break;
    default:
        eax_fail_unknown_property_id();
    }
}

void ALeffectslot::eax_fx_slot_get(const EaxCall& call) const
{
    switch(call.get_version())
    {
    case 4: eax4_fx_slot_get(call, eax4_.i); break;
    case 5: eax5_fx_slot_get(call, eax5_.i); break;
    default: eax_fail_unknown_version();
    }
}

bool ALeffectslot::eax_get(const EaxCall& call)
{
    switch(call.get_property_set_id())
    {
    case EaxCallPropertySetId::fx_slot:
        eax_fx_slot_get(call);
        break;
    case EaxCallPropertySetId::fx_slot_effect:
        eax_effect_->get(call);
        break;
    default:
        eax_fail_unknown_property_id();
    }

    return false;
}

void ALeffectslot::eax_fx_slot_load_effect(int version, ALenum altype)
{
    if(!IsValidEffectType(altype))
        altype = AL_EFFECT_NULL;
    eax_effect_->set_defaults(version, altype);
}

void ALeffectslot::eax_fx_slot_set_volume()
{
    const auto volume = std::clamp(eax_.lVolume, EAXFXSLOT_MINVOLUME, EAXFXSLOT_MAXVOLUME);
    const auto gain = level_mb_to_gain(static_cast<float>(volume));
    eax_set_efx_slot_gain(gain);
}

void ALeffectslot::eax_fx_slot_set_environment_flag()
{
    eax_set_efx_slot_send_auto((eax_.ulFlags & EAXFXSLOTFLAGS_ENVIRONMENT) != 0u);
}

void ALeffectslot::eax_fx_slot_set_flags()
{
    eax_fx_slot_set_environment_flag();
}

void ALeffectslot::eax4_fx_slot_set_all(const EaxCall& call)
{
    eax4_fx_slot_ensure_unlocked();
    const auto& src = call.get_value<Exception, const EAX40FXSLOTPROPERTIES>();
    Eax4AllValidator{}(src);
    auto& dst = eax4_.i;
    eax_df_ |= eax_load_effect_dirty_bit; // Always reset the effect.
    eax_df_ |= (dst.lVolume != src.lVolume ? eax_volume_dirty_bit : EaxDirtyFlags{});
    eax_df_ |= (dst.lLock != src.lLock ? eax_lock_dirty_bit : EaxDirtyFlags{});
    eax_df_ |= (dst.ulFlags != src.ulFlags ? eax_flags_dirty_bit : EaxDirtyFlags{});
    dst = src;
}

void ALeffectslot::eax5_fx_slot_set_all(const EaxCall& call)
{
    const auto& src = call.get_value<Exception, const EAX50FXSLOTPROPERTIES>();
    Eax5AllValidator{}(src);
    auto& dst = eax5_.i;
    eax_df_ |= eax_load_effect_dirty_bit; // Always reset the effect.
    eax_df_ |= (dst.lVolume != src.lVolume ? eax_volume_dirty_bit : EaxDirtyFlags{});
    eax_df_ |= (dst.lLock != src.lLock ? eax_lock_dirty_bit : EaxDirtyFlags{});
    eax_df_ |= (dst.ulFlags != src.ulFlags ? eax_flags_dirty_bit : EaxDirtyFlags{});
    eax_df_ |= (dst.lOcclusion != src.lOcclusion ? eax_flags_dirty_bit : EaxDirtyFlags{});
    eax_df_ |= (dst.flOcclusionLFRatio != src.flOcclusionLFRatio ? eax_flags_dirty_bit : EaxDirtyFlags{});
    dst = src;
}

bool ALeffectslot::eax_fx_slot_should_update_sources() const noexcept
{
    static constexpr auto dirty_bits =
        eax_occlusion_dirty_bit |
        eax_occlusion_lf_ratio_dirty_bit |
        eax_flags_dirty_bit;

    return (eax_df_ & dirty_bits) != EaxDirtyFlags{};
}

// Returns `true` if all sources should be updated, or `false` otherwise.
bool ALeffectslot::eax4_fx_slot_set(const EaxCall& call)
{
    auto& dst = eax4_.i;

    switch(call.get_property_id())
    {
    case EAXFXSLOT_NONE:
        break;
    case EAXFXSLOT_ALLPARAMETERS:
        eax4_fx_slot_set_all(call);
        if((eax_df_ & eax_load_effect_dirty_bit))
            eax_fx_slot_load_effect(4, eax_get_efx_effect_type(dst.guidLoadEffect));
        break;
    case EAXFXSLOT_LOADEFFECT:
        eax4_fx_slot_ensure_unlocked();
        eax_fx_slot_set_dirty<Eax4GuidLoadEffectValidator, eax_load_effect_dirty_bit>(call, dst.guidLoadEffect, eax_df_);
        if((eax_df_ & eax_load_effect_dirty_bit))
            eax_fx_slot_load_effect(4, eax_get_efx_effect_type(dst.guidLoadEffect));
        break;
    case EAXFXSLOT_VOLUME:
        eax_fx_slot_set<Eax4VolumeValidator, eax_volume_dirty_bit>(call, dst.lVolume, eax_df_);
        break;
    case EAXFXSLOT_LOCK:
        eax4_fx_slot_ensure_unlocked();
        eax_fx_slot_set<Eax4LockValidator, eax_lock_dirty_bit>(call, dst.lLock, eax_df_);
        break;
    case EAXFXSLOT_FLAGS:
        eax_fx_slot_set<Eax4FlagsValidator, eax_flags_dirty_bit>(call, dst.ulFlags, eax_df_);
        break;
    default:
        eax_fail_unknown_property_id();
    }

    return eax_fx_slot_should_update_sources();
}

// Returns `true` if all sources should be updated, or `false` otherwise.
bool ALeffectslot::eax5_fx_slot_set(const EaxCall& call)
{
    auto& dst = eax5_.i;

    switch(call.get_property_id())
    {
    case EAXFXSLOT_NONE:
        break;
    case EAXFXSLOT_ALLPARAMETERS:
        eax5_fx_slot_set_all(call);
        if((eax_df_ & eax_load_effect_dirty_bit))
            eax_fx_slot_load_effect(5, eax_get_efx_effect_type(dst.guidLoadEffect));
        break;
    case EAXFXSLOT_LOADEFFECT:
        eax_fx_slot_set_dirty<Eax4GuidLoadEffectValidator, eax_load_effect_dirty_bit>(call, dst.guidLoadEffect, eax_df_);
        if((eax_df_ & eax_load_effect_dirty_bit))
            eax_fx_slot_load_effect(5, eax_get_efx_effect_type(dst.guidLoadEffect));
        break;
    case EAXFXSLOT_VOLUME:
        eax_fx_slot_set<Eax4VolumeValidator, eax_volume_dirty_bit>(call, dst.lVolume, eax_df_);
        break;
    case EAXFXSLOT_LOCK:
        eax_fx_slot_set<Eax4LockValidator, eax_lock_dirty_bit>(call, dst.lLock, eax_df_);
        break;
    case EAXFXSLOT_FLAGS:
        eax_fx_slot_set<Eax5FlagsValidator, eax_flags_dirty_bit>(call, dst.ulFlags, eax_df_);
        break;
    case EAXFXSLOT_OCCLUSION:
        eax_fx_slot_set<Eax5OcclusionValidator, eax_occlusion_dirty_bit>(call, dst.lOcclusion, eax_df_);
        break;
    case EAXFXSLOT_OCCLUSIONLFRATIO:
        eax_fx_slot_set<Eax5OcclusionLfRatioValidator, eax_occlusion_lf_ratio_dirty_bit>(call, dst.flOcclusionLFRatio, eax_df_);
        break;
    default:
        eax_fail_unknown_property_id();
    }

    return eax_fx_slot_should_update_sources();
}

// Returns `true` if all sources should be updated, or `false` otherwise.
bool ALeffectslot::eax_fx_slot_set(const EaxCall& call)
{
    switch (call.get_version())
    {
    case 4: return eax4_fx_slot_set(call);
    case 5: return eax5_fx_slot_set(call);
    default: eax_fail_unknown_version();
    }
}

// Returns `true` if all sources should be updated, or `false` otherwise.
bool ALeffectslot::eax_set(const EaxCall& call)
{
    bool ret{false};

    switch(call.get_property_set_id())
    {
    case EaxCallPropertySetId::fx_slot: ret = eax_fx_slot_set(call); break;
    case EaxCallPropertySetId::fx_slot_effect: eax_effect_->set(call); break;
    default: eax_fail_unknown_property_id();
    }

    const auto version = call.get_version();
    if(eax_version_ != version)
        eax_df_ = ~EaxDirtyFlags{};
    eax_version_ = version;

    return ret;
}

void ALeffectslot::eax4_fx_slot_commit(EaxDirtyFlags& dst_df)
{
    eax_fx_slot_commit_property<eax_load_effect_dirty_bit>(eax4_, dst_df, &EAX40FXSLOTPROPERTIES::guidLoadEffect);
    eax_fx_slot_commit_property<eax_volume_dirty_bit>(eax4_, dst_df, &EAX40FXSLOTPROPERTIES::lVolume);
    eax_fx_slot_commit_property<eax_lock_dirty_bit>(eax4_, dst_df, &EAX40FXSLOTPROPERTIES::lLock);
    eax_fx_slot_commit_property<eax_flags_dirty_bit>(eax4_, dst_df, &EAX40FXSLOTPROPERTIES::ulFlags);

    auto& dst_i = eax_;

    if(dst_i.lOcclusion != EAXFXSLOT_DEFAULTOCCLUSION) {
        dst_df |= eax_occlusion_dirty_bit;
        dst_i.lOcclusion = EAXFXSLOT_DEFAULTOCCLUSION;
    }

    if(dst_i.flOcclusionLFRatio != EAXFXSLOT_DEFAULTOCCLUSIONLFRATIO) {
        dst_df |= eax_occlusion_lf_ratio_dirty_bit;
        dst_i.flOcclusionLFRatio = EAXFXSLOT_DEFAULTOCCLUSIONLFRATIO;
    }
}

void ALeffectslot::eax5_fx_slot_commit(Eax5State& state, EaxDirtyFlags& dst_df)
{
    eax_fx_slot_commit_property<eax_load_effect_dirty_bit>(state, dst_df, &EAX50FXSLOTPROPERTIES::guidLoadEffect);
    eax_fx_slot_commit_property<eax_volume_dirty_bit>(state, dst_df, &EAX50FXSLOTPROPERTIES::lVolume);
    eax_fx_slot_commit_property<eax_lock_dirty_bit>(state, dst_df, &EAX50FXSLOTPROPERTIES::lLock);
    eax_fx_slot_commit_property<eax_flags_dirty_bit>(state, dst_df, &EAX50FXSLOTPROPERTIES::ulFlags);
    eax_fx_slot_commit_property<eax_occlusion_dirty_bit>(state, dst_df, &EAX50FXSLOTPROPERTIES::lOcclusion);
    eax_fx_slot_commit_property<eax_occlusion_lf_ratio_dirty_bit>(state, dst_df, &EAX50FXSLOTPROPERTIES::flOcclusionLFRatio);
}

void ALeffectslot::eax_set_efx_slot_effect(EaxEffect &effect)
{
#define EAX_PREFIX "[EAX_SET_EFFECT_SLOT_EFFECT] "

    const auto error = initEffect(0, effect.al_effect_type_, effect.al_effect_props_,
        eax_al_context_);

    if(error != AL_NO_ERROR) {
        ERR(EAX_PREFIX "%s\n", "Failed to initialize an effect.");
        return;
    }

    if(mState == SlotState::Initial) {
        mPropsDirty = false;
        updateProps(eax_al_context_);
        auto effect_slot_ptr = this;
        AddActiveEffectSlots({&effect_slot_ptr, 1}, eax_al_context_);
        mState = SlotState::Playing;
        return;
    }

    mPropsDirty = true;

#undef EAX_PREFIX
}

void ALeffectslot::eax_set_efx_slot_send_auto(bool is_send_auto)
{
    if(AuxSendAuto == is_send_auto)
        return;

    AuxSendAuto = is_send_auto;
    mPropsDirty = true;
}

void ALeffectslot::eax_set_efx_slot_gain(ALfloat gain)
{
#define EAX_PREFIX "[EAX_SET_EFFECT_SLOT_GAIN] "

    if(gain == Gain)
        return;
    if(gain < 0.0f || gain > 1.0f)
        ERR(EAX_PREFIX "Gain out of range (%f)\n", gain);

    Gain = std::clamp(gain, 0.0f, 1.0f);
    mPropsDirty = true;

#undef EAX_PREFIX
}

void ALeffectslot::EaxDeleter::operator()(ALeffectslot* effect_slot)
{
    assert(effect_slot);
    eax_delete_al_effect_slot(*effect_slot->eax_al_context_, *effect_slot);
}

EaxAlEffectSlotUPtr eax_create_al_effect_slot(ALCcontext& context)
{
#define EAX_PREFIX "[EAX_MAKE_EFFECT_SLOT] "

    std::lock_guard<std::mutex> slotlock{context.mEffectSlotLock};
    auto& device = *context.mALDevice;

    if(context.mNumEffectSlots == device.AuxiliaryEffectSlotMax) {
        ERR(EAX_PREFIX "%s\n", "Out of memory.");
        return nullptr;
    }

    if(!EnsureEffectSlots(&context, 1)) {
        ERR(EAX_PREFIX "%s\n", "Failed to ensure.");
        return nullptr;
    }

    return EaxAlEffectSlotUPtr{AllocEffectSlot(&context)};

#undef EAX_PREFIX
}

void eax_delete_al_effect_slot(ALCcontext& context, ALeffectslot& effect_slot)
{
#define EAX_PREFIX "[EAX_DELETE_EFFECT_SLOT] "

    std::lock_guard<std::mutex> slotlock{context.mEffectSlotLock};

    if(effect_slot.ref.load(std::memory_order_relaxed) != 0)
    {
        ERR(EAX_PREFIX "Deleting in-use effect slot %u.\n", effect_slot.id);
        return;
    }

    auto effect_slot_ptr = &effect_slot;
    RemoveActiveEffectSlots({&effect_slot_ptr, 1}, &context);
    FreeEffectSlot(&context, &effect_slot);

#undef EAX_PREFIX
}
#endif // ALSOFT_EAX
