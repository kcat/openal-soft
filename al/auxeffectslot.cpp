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
#include <bit>
#include <cstddef>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <ranges>
#include <span>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "AL/al.h"
#include "AL/alext.h"
#include "AL/efx.h"

#include "alc/alu.h"
#include "alc/context.h"
#include "alc/device.h"
#include "alc/effects/base.h"
#include "alc/inprogext.h"
#include "alformat.hpp"
#include "almalloc.h"
#include "alnumeric.h"
#include "atomic.h"
#include "buffer.h"
#include "core/device.h"
#include "core/except.h"
#include "core/fpu_ctrl.h"
#include "core/logging.h"
#include "direct_defs.h"
#include "effect.h"
#include "flexarray.h"
#include "gsl/gsl"
#include "opthelpers.h"

#if ALSOFT_EAX
#include "eax/api.h"
#include "eax/call.h"
#include "eax/effect.h"
#include "eax/fx_slot_index.h"
#endif

namespace {

using SubListAllocator = al::allocator<std::array<al::EffectSlot,64>>;

[[nodiscard]]
auto getFactoryByType(EffectSlotType const type) -> gsl::not_null<EffectStateFactory*>
{
    switch(type)
    {
    case EffectSlotType::None: return NullStateFactory_getFactory();
    case EffectSlotType::Reverb: return ReverbStateFactory_getFactory();
    case EffectSlotType::Chorus: return ChorusStateFactory_getFactory();
    case EffectSlotType::Autowah: return AutowahStateFactory_getFactory();
    case EffectSlotType::Compressor: return CompressorStateFactory_getFactory();
    case EffectSlotType::Convolution: return ConvolutionStateFactory_getFactory();
    case EffectSlotType::Dedicated: return DedicatedStateFactory_getFactory();
    case EffectSlotType::Distortion: return DistortionStateFactory_getFactory();
    case EffectSlotType::Echo: return EchoStateFactory_getFactory();
    case EffectSlotType::Equalizer: return EqualizerStateFactory_getFactory();
    case EffectSlotType::Flanger: return ChorusStateFactory_getFactory();
    case EffectSlotType::FrequencyShifter: return FshifterStateFactory_getFactory();
    case EffectSlotType::RingModulator: return ModulatorStateFactory_getFactory();
    case EffectSlotType::PitchShifter: return PshifterStateFactory_getFactory();
    case EffectSlotType::VocalMorpher: return VmorpherStateFactory_getFactory();
    }
    throw std::runtime_error{al::format("Unexpected effect slot type: {:#x}",
        al::to_underlying(type))};
}


[[nodiscard]]
auto LookupEffectSlot(std::nothrow_t, gsl::not_null<al::Context*> const context, u32 const id)
    noexcept -> al::EffectSlot*
{
    const auto lidx = (id-1) >> 6;
    const auto slidx = (id-1) & 0x3f;

    if(lidx >= context->mEffectSlotList.size()) [[unlikely]]
        return nullptr;
    auto &sublist = context->mEffectSlotList[lidx];
    if(sublist.mFreeMask & (1_u64 << slidx)) [[unlikely]]
        return nullptr;
    return std::to_address(std::next(sublist.mEffectSlots->begin(), slidx));
}

[[nodiscard]]
auto LookupEffectSlot(gsl::not_null<al::Context*> const context, u32 const id)
    -> gsl::not_null<al::EffectSlot*>
{
    if(auto *const slot = LookupEffectSlot(std::nothrow, context, id)) [[likely]]
        return gsl::make_not_null(slot);
    context->throw_error(AL_INVALID_NAME, "Invalid effect slot ID {}", id);
}

[[nodiscard]]
auto LookupEffect(std::nothrow_t, gsl::not_null<al::Device*> const device, u32 const id) noexcept
    -> al::Effect*
{
    const auto lidx = (id-1) >> 6;
    const auto slidx = (id-1) & 0x3f;

    if(lidx >= device->EffectList.size()) [[unlikely]]
        return nullptr;
    auto &sublist = device->EffectList[lidx];
    if(sublist.mFreeMask & (1_u64 << slidx)) [[unlikely]]
        return nullptr;
    return std::to_address(std::next(sublist.mEffects->begin(), slidx));
}

[[nodiscard]]
auto LookupEffect(gsl::not_null<al::Context*> const context, u32 const id)
    -> gsl::not_null<al::Effect*>
{
    if(auto *const effect = LookupEffect(std::nothrow, al::get_not_null(context->mALDevice), id))
        [[likely]] return gsl::make_not_null(effect);
    context->throw_error(AL_INVALID_NAME, "Invalid effect ID {}", id);
}

[[nodiscard]]
auto LookupBuffer(std::nothrow_t, gsl::not_null<al::Device*> const device, u32 const id) noexcept
    -> al::Buffer*
{
    const auto lidx = (id-1) >> 6;
    const auto slidx = (id-1) & 0x3f;

    if(lidx >= device->BufferList.size()) [[unlikely]]
        return nullptr;
    auto &sublist = device->BufferList[lidx];
    if(sublist.mFreeMask & (1_u64 << slidx)) [[unlikely]]
        return nullptr;
    return std::to_address(std::next(sublist.mBuffers->begin(), slidx));
}

[[nodiscard]]
auto LookupBuffer(gsl::not_null<al::Context*> const context, u32 const id)
    -> gsl::not_null<al::Buffer*>
{
    if(auto *const buffer = LookupBuffer(std::nothrow, al::get_not_null(context->mALDevice), id))
        [[likely]] return gsl::make_not_null(buffer);
    context->throw_error(AL_INVALID_NAME, "Invalid buffer ID {}", id);
}


void AddActiveEffectSlots(std::span<gsl::not_null<al::EffectSlot*> const> const auxslots,
    gsl::not_null<al::Context*> const context)
{
    if(auxslots.empty())
        return;

    auto *curarray = context->mActiveAuxSlots.load(std::memory_order_acquire);
    if((curarray->size()>>1) > (std::numeric_limits<size_t>::max()>>1)-auxslots.size())
        throw std::runtime_error{"Too many active effect slots"};

    auto newcount = (curarray->size()>>1) + auxslots.size();

    /* Insert the new effect slots into the head of the new array, followed by
     * the existing ones.
     */
    auto newarray = EffectSlotBase::CreatePtrArray(newcount<<1);
    {
        const auto new_end = std::ranges::transform(auxslots, newarray->begin(),
            &al::EffectSlot::mSlot).out;
        std::ranges::copy(*curarray | std::views::take(curarray->size()>>1), new_end);
    }

    /* Sort and remove duplicates. Reallocate newarray if any duplicates were
     * removed.
     */
    std::ranges::sort(*newarray | std::views::take(newcount));
    if(const auto removed = std::ranges::unique(*newarray | std::views::take(newcount)).size())
        [[unlikely]]
    {
        newcount -= removed;
        auto const oldarray = std::move(newarray);
        newarray = EffectSlotBase::CreatePtrArray(newcount<<1);
        std::ranges::copy(*oldarray | std::views::take(newcount), newarray->begin());
    }
    std::ranges::fill(*newarray | std::views::drop(newcount), nullptr);

    auto oldarray = context->mActiveAuxSlots.exchange(std::move(newarray),
        std::memory_order_acq_rel);
    std::ignore = context->mDevice->waitForMix();
}

void RemoveActiveEffectSlots(std::span<gsl::not_null<al::EffectSlot*> const> const auxslots,
    gsl::not_null<al::Context*> const context)
{
    if(auxslots.empty())
        return;

    /* Copy the existing slots, excluding those specified in auxslots. */
    auto *curarray = context->mActiveAuxSlots.load(std::memory_order_acquire);
    auto tmparray = std::vector<EffectSlotBase*>{};
    tmparray.reserve(curarray->size()>>1);
    std::ranges::copy_if(*curarray | std::views::take(curarray->size()>>1),
        std::back_inserter(tmparray), [auxslots](const EffectSlotBase *slot) -> bool
        { return std::ranges::find(auxslots, slot, &al::EffectSlot::mSlot) == auxslots.end(); });

    /* Reallocate with the new size. */
    auto newarray = EffectSlotBase::CreatePtrArray(tmparray.size()<<1);
    auto new_end = std::ranges::copy(tmparray, newarray->begin()).out;
    std::ranges::fill(new_end, newarray->end(), nullptr);

    auto oldarray = context->mActiveAuxSlots.exchange(std::move(newarray),
        std::memory_order_acq_rel);
    std::ignore = context->mDevice->waitForMix();
}


[[nodiscard]]
constexpr auto EffectSlotTypeFromEnum(ALenum const type) noexcept -> EffectSlotType
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
    case AL_EFFECT_EAXREVERB: return EffectSlotType::Reverb;
    case AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT: return EffectSlotType::Dedicated;
    case AL_EFFECT_DEDICATED_DIALOGUE: return EffectSlotType::Dedicated;
    case AL_EFFECT_CONVOLUTION_SOFT: return EffectSlotType::Convolution;
    }
    ERR("Unhandled effect enum: {:#04x}", as_unsigned(type));
    return EffectSlotType::None;
}

[[nodiscard]]
auto EnsureEffectSlots(gsl::not_null<al::Context*> const context, usize const needed) noexcept
    -> bool
try {
    auto count = std::accumulate(context->mEffectSlotList.cbegin(),
        context->mEffectSlotList.cend(), 0_uz,
        [](usize const cur, const EffectSlotSubList &sublist) noexcept -> usize
        { return cur + gsl::narrow_cast<ALuint>(std::popcount(sublist.mFreeMask)); });

    while(needed > count)
    {
        if(context->mEffectSlotList.size() >= 1<<25) [[unlikely]]
            return false;

        auto sublist = EffectSlotSubList{};
        sublist.mFreeMask = ~0_u64;
        sublist.mEffectSlots = SubListAllocator{}.allocate(1);
        context->mEffectSlotList.emplace_back(std::move(sublist));
        count += std::tuple_size_v<SubListAllocator::value_type>;
    }
    return true;
}
catch(...) {
    return false;
}

[[nodiscard]]
auto AllocEffectSlot(gsl::not_null<al::Context*> const context) -> gsl::not_null<al::EffectSlot*>
{
    auto const sublist = std::ranges::find_if(context->mEffectSlotList,
        &EffectSlotSubList::mFreeMask);
    auto const lidx = gsl::narrow_cast<u32>(std::distance(context->mEffectSlotList.begin(),
        sublist));
    auto const slidx = gsl::narrow_cast<u32>(std::countr_zero(sublist->mFreeMask));
    ASSUME(slidx < 64);

    auto const slot = gsl::make_not_null(std::construct_at(
        std::to_address(std::next(sublist->mEffectSlots->begin(), slidx)), context));
    aluInitEffectPanning(slot->mSlot, context);

    /* Add 1 to avoid ID 0. */
    slot->mId = ((lidx<<6) | slidx) + 1;

    context->mNumEffectSlots += 1;
    sublist->mFreeMask &= ~(1_u64 << slidx);

    return slot;
}

void FreeEffectSlot(gsl::not_null<al::Context*> const context,
    gsl::not_null<al::EffectSlot*> const slot)
{
    context->mEffectSlotNames.erase(slot->mId);

    const auto id = slot->mId - 1;
    const auto lidx = id >> 6;
    const auto slidx = id & 0x3f;

    std::destroy_at(std::to_address(slot));

    context->mEffectSlotList[lidx].mFreeMask |= 1_u64 << slidx;
    context->mNumEffectSlots--;
}


void UpdateProps(gsl::not_null<al::EffectSlot*> const slot,
    gsl::not_null<al::Context*> const context)
{
    if(!context->mDeferUpdates && slot->mState == SlotState::Playing)
    {
        slot->updateProps(context);
        return;
    }
    slot->mPropsDirty = true;
}


void alGenAuxiliaryEffectSlots(gsl::not_null<al::Context*> const context, ALsizei const n,
    ALuint *const effectslots) noexcept
try {
    if(n < 0)
        context->throw_error(AL_INVALID_VALUE, "Generating {} effect slots", n);
    if(n <= 0) [[unlikely]] return;

    auto slotlock = std::lock_guard{context->mEffectSlotLock};
    auto const device = al::get_not_null(context->mALDevice);

    const auto eids = std::span{effectslots, gsl::narrow_cast<ALuint>(n)};
    if(context->mNumEffectSlots > device->AuxiliaryEffectSlotMax
        || eids.size() > device->AuxiliaryEffectSlotMax-context->mNumEffectSlots)
        context->throw_error(AL_OUT_OF_MEMORY, "Exceeding {} effect slot limit ({} + {})",
            device->AuxiliaryEffectSlotMax, context->mNumEffectSlots, n);

    if(!EnsureEffectSlots(context, eids.size()))
        context->throw_error(AL_OUT_OF_MEMORY, "Failed to allocate {} effectslot{}", n,
            (n==1) ? "" : "s");

    auto slots = std::vector<gsl::not_null<al::EffectSlot*>>{};
    try {
        if(eids.size() == 1)
        {
            /* Special handling for the easy and normal case. */
            eids[0] = AllocEffectSlot(context)->mId;
        }
        else
        {
            slots.reserve(eids.size());
            std::generate_n(std::back_inserter(slots), eids.size(),
                [context]{ return AllocEffectSlot(context); });

            std::ranges::transform(slots, eids.begin(), &al::EffectSlot::mId);
        }
    }
    catch(std::exception& e) {
        ERR("Exception allocating effectslot {} of {}: {}", slots.size()+1, n, e.what());
        std::ranges::for_each(slots, [context](gsl::not_null<al::EffectSlot*> const slot) -> void
        { FreeEffectSlot(context, slot); });
        context->throw_error(AL_INVALID_OPERATION, "Exception allocating {} effectslots: {}", n,
            e.what());
    }
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alDeleteAuxiliaryEffectSlots(gsl::not_null<al::Context*> const context, ALsizei const n,
    ALuint const *const effectslots) noexcept
try {
    if(n < 0) [[unlikely]]
        context->throw_error(AL_INVALID_VALUE, "Deleting {} effect slots", n);
    if(n <= 0) [[unlikely]] return;

    auto slotlock = std::lock_guard{context->mEffectSlotLock};
    if(n == 1)
    {
        auto slot = LookupEffectSlot(context, *effectslots);
        if(slot->mRef.load(std::memory_order_relaxed) != 0)
            context->throw_error(AL_INVALID_OPERATION, "Deleting in-use effect slot {}",
                *effectslots);

        RemoveActiveEffectSlots({&slot, 1u}, context);
        FreeEffectSlot(context, slot);
    }
    else
    {
        const auto eids = std::span{effectslots, gsl::narrow_cast<ALuint>(n)};
        auto slots = std::vector<gsl::not_null<al::EffectSlot*>>{};
        slots.reserve(eids.size());

        std::ranges::transform(eids, std::back_inserter(slots),
            [context](u32 const eid) -> gsl::not_null<al::EffectSlot*>
        {
            auto const slot = LookupEffectSlot(context, eid);
            if(slot->mRef.load(std::memory_order_relaxed) != 0)
                context->throw_error(AL_INVALID_OPERATION, "Deleting in-use effect slot {}", eid);
            return slot;
        });

        /* All effectslots are valid, remove and delete them */
        RemoveActiveEffectSlots(slots, context);

        std::ranges::for_each(eids, [context](const ALuint eid) -> void
        {
            if(auto *slot = LookupEffectSlot(std::nothrow, context, eid))
                FreeEffectSlot(context, gsl::make_not_null(slot));
        });
    }
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

auto alIsAuxiliaryEffectSlot(gsl::not_null<al::Context*> const context, ALuint const effectslot)
    noexcept -> ALboolean
{
    const auto slotlock = std::lock_guard{context->mEffectSlotLock};
    if(LookupEffectSlot(std::nothrow, context, effectslot) != nullptr)
        return AL_TRUE;
    return AL_FALSE;
}


void alAuxiliaryEffectSloti(gsl::not_null<al::Context*> const context, ALuint const effectslot,
    ALenum const param, ALint const value) noexcept
try {
    const auto proplock = std::lock_guard{context->mPropLock};
    const auto slotlock = std::lock_guard{context->mEffectSlotLock};

    auto slot = LookupEffectSlot(context, effectslot);
    auto targetref = al::intrusive_ptr<al::EffectSlot>{};
    switch(param)
    {
    case AL_EFFECTSLOT_EFFECT:
        {
            auto const device = al::get_not_null(context->mALDevice);
            const auto effectlock = std::lock_guard{device->EffectLock};
            if(value == 0)
                slot->initEffect(0, AL_EFFECT_NULL, EffectProps{}, context);
            else
            {
                auto const effect = LookupEffect(context, as_unsigned(value));
                slot->initEffect(effect->mId, effect->mType, effect->mProps, context);
            }
        }

        if(slot->mState == SlotState::Initial) [[unlikely]]
        {
            slot->mPropsDirty = false;
            slot->updateProps(context);

            AddActiveEffectSlots({&slot, 1}, context);
            slot->mState = SlotState::Playing;
            return;
        }
        UpdateProps(slot, context);
        return;

    case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
        if(!(value == AL_TRUE || value == AL_FALSE))
            context->throw_error(AL_INVALID_VALUE, "Effect slot auxiliary send auto out of range");
        if(!(slot->mAuxSendAuto == !!value)) [[likely]]
        {
            slot->mAuxSendAuto = !!value;
            UpdateProps(slot, context);
        }
        return;

    case AL_EFFECTSLOT_TARGET_SOFT:
        if(value != 0)
        {
            auto const target = LookupEffectSlot(context, as_unsigned(value));
            if(slot->mTarget.get() == target)
                return;

            auto const *checker = target.get();
            while(checker && checker != slot)
                checker = checker->mTarget.get();
            if(checker)
                context->throw_error(AL_INVALID_OPERATION,
                    "Setting target of effect slot ID {} to {} creates circular chain", slot->mId,
                    target->mId);

            targetref = target->newReference();
        }
        else if(!slot->mTarget)
            return;

        if(slot->mTarget)
        {
            /* We must force an update if there was an existing effect slot
             * target, in case it's about to be deleted.
             */
            slot->mTarget = std::move(targetref);
            slot->updateProps(context);
        }
        else
        {
            slot->mTarget = std::move(targetref);
            UpdateProps(slot, context);
        }
        return;

    case AL_BUFFER:
        if(auto const *const buffer = slot->mBuffer.get())
        {
            if(buffer->mId == as_unsigned(value))
                return;
        }
        else if(value == 0)
            return;

        if(slot->mState == SlotState::Playing)
        {
            auto state = getFactoryByType(slot->mEffect.Type)->create();

            auto const device = al::get_not_null(context->mALDevice);
            auto bufferlock = std::unique_lock{device->BufferLock};
            auto buffer = al::intrusive_ptr<al::Buffer>{};
            if(value)
            {
                auto const buf = LookupBuffer(context, as_unsigned(value));
                if(buf->mCallback)
                    context->throw_error(AL_INVALID_OPERATION,
                        "Callback buffer not valid for effects");

                buffer = buf->newReference();
            }

            /* Stop the effect slot from processing while we switch buffers. */
            RemoveActiveEffectSlots({&slot, 1}, context);

            slot->mBuffer = std::move(buffer);
            bufferlock.unlock();

            state->mOutTarget = device->Dry.Buffer;
            {
                const auto mixer_mode = FPUCtl{};
                state->deviceUpdate(device, slot->mBuffer.get());
            }
            slot->mEffect.State = std::move(state);

            slot->mPropsDirty = false;
            slot->updateProps(context);
            AddActiveEffectSlots({&slot, 1}, context);
        }
        else
        {
            auto const device = al::get_not_null(context->mALDevice);
            auto bufferlock = std::unique_lock{device->BufferLock};
            if(value)
            {
                auto const buffer = LookupBuffer(context, as_unsigned(value));
                if(buffer->mCallback)
                    context->throw_error(AL_INVALID_OPERATION,
                        "Callback buffer not valid for effects");

                slot->mBuffer = buffer->newReference();
            }
            else
                slot->mBuffer.reset();
            bufferlock.unlock();

            const auto mixer_mode = FPUCtl{};
            auto *state = slot->mEffect.State.get();
            state->deviceUpdate(device, slot->mBuffer.get());
            slot->mPropsDirty = true;
        }
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid effect slot integer property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alAuxiliaryEffectSlotiv(gsl::not_null<al::Context*> context, ALuint effectslot, ALenum param,
    const ALint *values) noexcept
try {
    switch(param)
    {
    case AL_EFFECTSLOT_EFFECT:
    case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
    case AL_EFFECTSLOT_TARGET_SOFT:
    case AL_BUFFER:
        alAuxiliaryEffectSloti(context, effectslot, param, *values);
        return;
    }

    const auto slotlock [[maybe_unused]] = std::lock_guard{context->mEffectSlotLock};
    std::ignore = LookupEffectSlot(context, effectslot);

    context->throw_error(AL_INVALID_ENUM, "Invalid effect slot integer-vector property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alAuxiliaryEffectSlotf(gsl::not_null<al::Context*> context, ALuint effectslot, ALenum param,
    ALfloat value) noexcept
try {
    const auto proplock = std::lock_guard{context->mPropLock};
    const auto slotlock = std::lock_guard{context->mEffectSlotLock};

    auto slot = LookupEffectSlot(context, effectslot);
    switch(param)
    {
    case AL_EFFECTSLOT_GAIN:
        if(!(value >= 0.0f && value <= 1.0f))
            context->throw_error(AL_INVALID_VALUE, "Effect slot gain {} out of range", value);
        if(!(slot->mGain == value)) [[likely]]
        {
            slot->mGain = value;
            UpdateProps(slot, context);
        }
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid effect slot float property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alAuxiliaryEffectSlotfv(gsl::not_null<al::Context*> context, ALuint effectslot, ALenum param,
    const ALfloat *values) noexcept
try {
    switch(param)
    {
    case AL_EFFECTSLOT_GAIN:
        alAuxiliaryEffectSlotf(context, effectslot, param, *values);
        return;
    }

    const auto slotlock [[maybe_unused]] = std::lock_guard{context->mEffectSlotLock};
    std::ignore = LookupEffectSlot(context, effectslot);

    context->throw_error(AL_INVALID_ENUM, "Invalid effect slot float-vector property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void alGetAuxiliaryEffectSloti(gsl::not_null<al::Context*> context, ALuint effectslot,
    ALenum param, ALint *value) noexcept
try {
    const auto slotlock = std::lock_guard{context->mEffectSlotLock};

    auto slot = LookupEffectSlot(context, effectslot);
    switch(param)
    {
    case AL_EFFECTSLOT_EFFECT:
        *value = as_signed(slot->mEffectId);
        return;

    case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
        *value = slot->mAuxSendAuto ? AL_TRUE : AL_FALSE;
        return;

    case AL_EFFECTSLOT_TARGET_SOFT:
        if(auto *target = slot->mTarget.get())
            *value = as_signed(target->mId);
        else
            *value = 0;
        return;

    case AL_BUFFER:
        if(auto *buffer = slot->mBuffer.get())
            *value = as_signed(buffer->mId);
        else
            *value = 0;
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid effect slot integer property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetAuxiliaryEffectSlotiv(gsl::not_null<al::Context*> context, ALuint effectslot,
    ALenum param, ALint *values) noexcept
try {
    switch(param)
    {
    case AL_EFFECTSLOT_EFFECT:
    case AL_EFFECTSLOT_AUXILIARY_SEND_AUTO:
    case AL_EFFECTSLOT_TARGET_SOFT:
    case AL_BUFFER:
        alGetAuxiliaryEffectSloti(context, effectslot, param, values);
        return;
    }

    const auto slotlock [[maybe_unused]] = std::lock_guard{context->mEffectSlotLock};
    std::ignore = LookupEffectSlot(context, effectslot);

    context->throw_error(AL_INVALID_ENUM, "Invalid effect slot integer-vector property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetAuxiliaryEffectSlotf(gsl::not_null<al::Context*> context, ALuint effectslot,
    ALenum param, ALfloat *value) noexcept
try {
    const auto slotlock = std::lock_guard{context->mEffectSlotLock};

    auto slot = LookupEffectSlot(context, effectslot);
    switch(param)
    {
    case AL_EFFECTSLOT_GAIN: *value = slot->mGain; return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid effect slot float property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetAuxiliaryEffectSlotfv(gsl::not_null<al::Context*> context, ALuint effectslot,
    ALenum param, ALfloat *values) noexcept
try {
    switch(param)
    {
    case AL_EFFECTSLOT_GAIN:
        alGetAuxiliaryEffectSlotf(context, effectslot, param, values);
        return;
    }

    const auto slotlock [[maybe_unused]] = std::lock_guard{context->mEffectSlotLock};
    std::ignore = LookupEffectSlot(context, effectslot);

    context->throw_error(AL_INVALID_ENUM, "Invalid effect slot float-vector property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

} // namespace


AL_API DECL_FUNC2(void, alGenAuxiliaryEffectSlots, ALsizei,n, ALuint*,effectslots)
AL_API DECL_FUNC2(void, alDeleteAuxiliaryEffectSlots, ALsizei,n, const ALuint*,effectslots)
AL_API DECL_FUNC1(ALboolean, alIsAuxiliaryEffectSlot, ALuint,effectslot)

AL_API DECL_FUNC3(void, alAuxiliaryEffectSloti, ALuint,effectslot, ALenum,param, ALint,value)
AL_API DECL_FUNC3(void, alAuxiliaryEffectSlotiv, ALuint,effectslot, ALenum,param, const ALint*,values)
AL_API DECL_FUNC3(void, alAuxiliaryEffectSlotf, ALuint,effectslot, ALenum,param, ALfloat,value)
AL_API DECL_FUNC3(void, alAuxiliaryEffectSlotfv, ALuint,effectslot, ALenum,param, const ALfloat*,values)
AL_API DECL_FUNC3(void, alGetAuxiliaryEffectSloti, ALuint,effectslot, ALenum,param, ALint*,value)
AL_API DECL_FUNC3(void, alGetAuxiliaryEffectSlotiv, ALuint,effectslot, ALenum,param, ALint*,values)
AL_API DECL_FUNC3(void, alGetAuxiliaryEffectSlotf, ALuint,effectslot, ALenum,param, ALfloat*,value)
AL_API DECL_FUNC3(void, alGetAuxiliaryEffectSlotfv, ALuint,effectslot, ALenum,param, ALfloat*,values)


al::EffectSlot::EffectSlot(gsl::not_null<al::Context*> context) : mSlot{context->getEffectSlot()}
#if ALSOFT_EAX
    , mEaxALContext{context}
#endif
{
    mSlot->InUse = true;
    try {
        auto state = getFactoryByType(EffectSlotType::None)->create();
        mEffect.State = state;
        mSlot->mEffectState = std::move(state);
    }
    catch(...) {
        mSlot->InUse = false;
        throw;
    }
}

al::EffectSlot::~EffectSlot()
{
    if(auto *slot = mSlot->Update.exchange(nullptr, std::memory_order_relaxed))
        slot->State = nullptr;

    mSlot->mEffectState = nullptr;
    mSlot->InUse = false;
}

auto al::EffectSlot::initEffect(u32 const effectId, ALenum const effectType,
    EffectProps const &effectProps, gsl::not_null<Context*> const context) -> void
{
    const auto newtype = EffectSlotTypeFromEnum(effectType);
    if(newtype != mEffect.Type)
    {
        auto state = getFactoryByType(newtype)->create();

        auto const device = al::get_not_null(context->mALDevice);
        state->mOutTarget = device->Dry.Buffer;
        {
            const auto mixer_mode = FPUCtl{};
            state->deviceUpdate(device, mBuffer.get());
        }

        mEffect.Type = newtype;
        mEffect.Props = effectProps;

        mEffect.State = std::move(state);
    }
    else if(newtype != EffectSlotType::None)
        mEffect.Props = effectProps;
    mEffectId = effectId;

    /* Remove state references from old effect slot property updates. */
    auto *props = context->mFreeEffectSlotProps.load();
    while(props)
    {
        props->State = nullptr;
        props = props->next.load(std::memory_order_relaxed);
    }
}

void al::EffectSlot::updateProps(gsl::not_null<Context*> const context) const
{
    /* Get an unused property container, or allocate a new one as needed. */
    auto *props = context->mFreeEffectSlotProps.load(std::memory_order_acquire);
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
    props->Gain = mGain;
    props->AuxSendAuto = mAuxSendAuto;
    props->Target = mTarget ? mTarget->mSlot.get() : nullptr;

    props->Type = mEffect.Type;
    props->Props = mEffect.Props;
    props->State = mEffect.State;

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

void al::EffectSlot::SetName(gsl::not_null<Context*> const context, u32 const id,
    std::string_view const name)
{
    const auto slotlock = std::lock_guard{context->mEffectSlotLock};

    std::ignore = LookupEffectSlot(context, id);

    context->mEffectSlotNames.insert_or_assign(id, name);
}

void UpdateAllEffectSlotProps(gsl::not_null<al::Context*> context)
{
    const auto slotlock = std::lock_guard{context->mEffectSlotLock};
    for(auto &sublist : context->mEffectSlotList)
    {
        auto usemask = ~sublist.mFreeMask;
        while(usemask)
        {
            const auto idx = as_unsigned(std::countr_zero(usemask));
            usemask ^= 1_u64 << idx;

            auto &slot = (*sublist.mEffectSlots)[idx];
            if(std::exchange(slot.mPropsDirty, false))
                slot.updateProps(context);
        }
    }
}

EffectSlotSubList::~EffectSlotSubList()
{
    if(!mEffectSlots)
        return;

    uint64_t usemask{~mFreeMask};
    while(usemask)
    {
        const int idx{std::countr_zero(usemask)};
        std::destroy_at(std::to_address(mEffectSlots->begin() + idx));
        usemask &= ~(1_u64 << idx);
    }
    mFreeMask = ~usemask;
    SubListAllocator{}.deallocate(mEffectSlots, 1);
    mEffectSlots = nullptr;
}


AL_API void AL_APIENTRY alAuxiliaryEffectSlotPlaySOFT(ALuint) noexcept
{
    const auto context = GetContextRef();
    if(!context) [[unlikely]] return;
    context->setError(AL_INVALID_OPERATION, "alAuxiliaryEffectSlotPlaySOFT not supported");
}

AL_API void AL_APIENTRY alAuxiliaryEffectSlotPlayvSOFT(ALsizei, const ALuint*) noexcept
{
    const auto context = GetContextRef();
    if(!context) [[unlikely]] return;
    context->setError(AL_INVALID_OPERATION, "alAuxiliaryEffectSlotPlayvSOFT not supported");
}

AL_API void AL_APIENTRY alAuxiliaryEffectSlotStopSOFT(ALuint) noexcept
{
    const auto context = GetContextRef();
    if(!context) [[unlikely]] return;
    context->setError(AL_INVALID_OPERATION, "alAuxiliaryEffectSlotStopSOFT not supported");
}

AL_API void AL_APIENTRY alAuxiliaryEffectSlotStopvSOFT(ALsizei, const ALuint*) noexcept
{
    const auto context = GetContextRef();
    if(!context) [[unlikely]] return;
    context->setError(AL_INVALID_OPERATION, "alAuxiliaryEffectSlotStopvSOFT not supported");
}


#if ALSOFT_EAX
void al::EffectSlot::eax_initialize(EaxFxSlotIndexValue const index)
{
    if(index >= EAX_MAX_FXSLOTS)
        eax_fail("Index out of range.");

    mEaxFXSlotIndex = index;
    eax_fx_slot_set_defaults();

    mEaxEffect = std::make_unique<EaxEffect>();
    if(index == 0) mEaxEffect->init<EaxReverbCommitter>();
    else if(index == 1) mEaxEffect->init<EaxChorusCommitter>();
    else mEaxEffect->init<EaxNullCommitter>();
}

void al::EffectSlot::eax_commit()
{
    if(mEaxDf.any())
    {
        auto df = std::bitset<eax_dirty_bit_count>{};
        switch(mEaxVersion)
        {
        case 1:
        case 2:
        case 3:
            eax5_fx_slot_commit(mEax123, df);
            break;
        case 4:
            eax4_fx_slot_commit(df);
            break;
        case 5:
            eax5_fx_slot_commit(mEax5, df);
            break;
        }
        mEaxDf.reset();

        if(df.test(eax_volume_dirty_bit))
            eax_fx_slot_set_volume();
        if(df.test(eax_flags_dirty_bit))
            eax_fx_slot_set_flags();
    }

    if(mEaxEffect->commit(mEaxVersion))
        eax_set_efx_slot_effect(*mEaxEffect);
}

[[noreturn]]
void al::EffectSlot::eax_fail(std::string_view const message)
{ throw Exception{message}; }

[[noreturn]]
void al::EffectSlot::eax_fail_unknown_effect_id()
{ eax_fail("Unknown effect ID."); }

[[noreturn]]
void al::EffectSlot::eax_fail_unknown_property_id()
{ eax_fail("Unknown property ID."); }

[[noreturn]]
void al::EffectSlot::eax_fail_unknown_version()
{ eax_fail("Unknown version."); }

void al::EffectSlot::eax4_fx_slot_ensure_unlocked() const
{
    if(eax4_fx_slot_is_legacy())
        eax_fail("Locked legacy slot.");
}

ALenum al::EffectSlot::eax_get_efx_effect_type(const GUID& guid)
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

const GUID& al::EffectSlot::eax_get_eax_default_effect_guid() const noexcept
{
    switch(mEaxFXSlotIndex)
    {
    case 0: return EAX_REVERB_EFFECT;
    case 1: return EAX_CHORUS_EFFECT;
    default: return EAX_NULL_GUID;
    }
}

auto al::EffectSlot::eax_get_eax_default_lock() const noexcept -> eax_long
{
    return eax4_fx_slot_is_legacy() ? EAXFXSLOT_LOCKED : EAXFXSLOT_UNLOCKED;
}

void al::EffectSlot::eax4_fx_slot_set_defaults(EAX40FXSLOTPROPERTIES& props) const noexcept
{
    props.guidLoadEffect = eax_get_eax_default_effect_guid();
    props.lVolume = EAXFXSLOT_DEFAULTVOLUME;
    props.lLock = eax_get_eax_default_lock();
    props.ulFlags = EAX40FXSLOT_DEFAULTFLAGS;
}

void al::EffectSlot::eax5_fx_slot_set_defaults(EAX50FXSLOTPROPERTIES& props) const noexcept
{
    props.guidLoadEffect = eax_get_eax_default_effect_guid();
    props.lVolume = EAXFXSLOT_DEFAULTVOLUME;
    props.lLock = EAXFXSLOT_UNLOCKED;
    props.ulFlags = EAX50FXSLOT_DEFAULTFLAGS;
    props.lOcclusion = EAXFXSLOT_DEFAULTOCCLUSION;
    props.flOcclusionLFRatio = EAXFXSLOT_DEFAULTOCCLUSIONLFRATIO;
}

void al::EffectSlot::eax_fx_slot_set_defaults()
{
    eax5_fx_slot_set_defaults(mEax123.i);
    eax4_fx_slot_set_defaults(mEax4.i);
    eax5_fx_slot_set_defaults(mEax5.i);
    mEax = mEax5.i;
    mEaxDf.reset();
}

void al::EffectSlot::eax4_fx_slot_get(const EaxCall& call, const EAX40FXSLOTPROPERTIES& props)
{
    switch(call.get_property_id())
    {
    case EAXFXSLOT_ALLPARAMETERS: call.store(props); break;
    case EAXFXSLOT_LOADEFFECT: call.store(props.guidLoadEffect); break;
    case EAXFXSLOT_VOLUME: call.store(props.lVolume); break;
    case EAXFXSLOT_LOCK: call.store(props.lLock); break;
    case EAXFXSLOT_FLAGS: call.store(props.ulFlags); break;
    default: eax_fail_unknown_property_id();
    }
}

void al::EffectSlot::eax5_fx_slot_get(const EaxCall& call, const EAX50FXSLOTPROPERTIES& props)
{
    switch(call.get_property_id())
    {
    case EAXFXSLOT_ALLPARAMETERS: call.store(props); break;
    case EAXFXSLOT_LOADEFFECT: call.store(props.guidLoadEffect); break;
    case EAXFXSLOT_VOLUME: call.store(props.lVolume); break;
    case EAXFXSLOT_LOCK: call.store(props.lLock); break;
    case EAXFXSLOT_FLAGS: call.store(props.ulFlags); break;
    case EAXFXSLOT_OCCLUSION: call.store(props.lOcclusion); break;
    case EAXFXSLOT_OCCLUSIONLFRATIO: call.store(props.flOcclusionLFRatio); break;
    default: eax_fail_unknown_property_id();
    }
}

void al::EffectSlot::eax_fx_slot_get(const EaxCall& call) const
{
    switch(call.get_version())
    {
    case 4: eax4_fx_slot_get(call, mEax4.i); break;
    case 5: eax5_fx_slot_get(call, mEax5.i); break;
    default: eax_fail_unknown_version();
    }
}

auto al::EffectSlot::eax_get(const EaxCall& call) const -> bool
{
    switch(call.get_property_set_id())
    {
    case EaxCallPropertySetId::fx_slot:
        eax_fx_slot_get(call);
        break;
    case EaxCallPropertySetId::fx_slot_effect:
        mEaxEffect->get(call);
        break;
    default:
        eax_fail_unknown_property_id();
    }

    return false;
}

void al::EffectSlot::eax_fx_slot_load_effect(int version, ALenum altype) const
{
    if(!IsValidEffectType(altype))
        altype = AL_EFFECT_NULL;
    mEaxEffect->set_defaults(version, altype);
}

void al::EffectSlot::eax_fx_slot_set_volume()
{
    const auto volume = std::clamp(mEax.lVolume, EAXFXSLOT_MINVOLUME, EAXFXSLOT_MAXVOLUME);
    const auto gain = level_mb_to_gain(gsl::narrow_cast<float>(volume));
    eax_set_efx_slot_gain(gain);
}

void al::EffectSlot::eax_fx_slot_set_environment_flag()
{
    eax_set_efx_slot_send_auto((mEax.ulFlags & EAXFXSLOTFLAGS_ENVIRONMENT) != 0u);
}

void al::EffectSlot::eax_fx_slot_set_flags()
{
    eax_fx_slot_set_environment_flag();
}

void al::EffectSlot::eax4_fx_slot_set_all(const EaxCall& call)
{
    eax4_fx_slot_ensure_unlocked();
    const auto &src = call.load<const EAX40FXSLOTPROPERTIES>();
    Eax4AllValidator{}(src);
    auto &dst = mEax4.i;
    mEaxDf.set(eax_load_effect_dirty_bit); // Always reset the effect.
    if(dst.lVolume != src.lVolume) mEaxDf.set(eax_volume_dirty_bit);
    if(dst.lLock != src.lLock) mEaxDf.set(eax_lock_dirty_bit);
    if(dst.ulFlags != src.ulFlags) mEaxDf.set(eax_flags_dirty_bit);
    dst = src;
}

void al::EffectSlot::eax5_fx_slot_set_all(const EaxCall& call)
{
    const auto &src = call.load<const EAX50FXSLOTPROPERTIES>();
    Eax5AllValidator{}(src);
    auto &dst = mEax5.i;
    mEaxDf.set(eax_load_effect_dirty_bit); // Always reset the effect.
    if(dst.lVolume != src.lVolume) mEaxDf.set(eax_volume_dirty_bit);
    if(dst.lLock != src.lLock) mEaxDf.set(eax_lock_dirty_bit);
    if(dst.ulFlags != src.ulFlags) mEaxDf.set(eax_flags_dirty_bit);
    if(dst.lOcclusion != src.lOcclusion) mEaxDf.set(eax_flags_dirty_bit);
    if(dst.flOcclusionLFRatio != src.flOcclusionLFRatio) mEaxDf.set(eax_flags_dirty_bit);
    dst = src;
}

auto al::EffectSlot::eax_fx_slot_should_update_sources() const noexcept -> bool
{
    static constexpr auto dirty_bits = std::bitset<eax_dirty_bit_count>{
        (1u << eax_occlusion_dirty_bit)
        | (1u << eax_occlusion_lf_ratio_dirty_bit)
        | (1u << eax_flags_dirty_bit)
    };
    return (mEaxDf & dirty_bits).any();
}

// Returns `true` if all sources should be updated, or `false` otherwise.
auto al::EffectSlot::eax4_fx_slot_set(const EaxCall& call) -> bool
{
    auto& dst = mEax4.i;

    switch(call.get_property_id())
    {
    case EAXFXSLOT_NONE:
        break;
    case EAXFXSLOT_ALLPARAMETERS:
        eax4_fx_slot_set_all(call);
        if(mEaxDf.test(eax_load_effect_dirty_bit))
            eax_fx_slot_load_effect(4, eax_get_efx_effect_type(dst.guidLoadEffect));
        break;
    case EAXFXSLOT_LOADEFFECT:
        eax4_fx_slot_ensure_unlocked();
        eax_fx_slot_set_dirty<Eax4GuidLoadEffectValidator>(call, dst.guidLoadEffect,
            eax_load_effect_dirty_bit);
        if(mEaxDf.test(eax_load_effect_dirty_bit))
            eax_fx_slot_load_effect(4, eax_get_efx_effect_type(dst.guidLoadEffect));
        break;
    case EAXFXSLOT_VOLUME:
        eax_fx_slot_set<Eax4VolumeValidator>(call, dst.lVolume, eax_volume_dirty_bit);
        break;
    case EAXFXSLOT_LOCK:
        eax4_fx_slot_ensure_unlocked();
        eax_fx_slot_set<Eax4LockValidator>(call, dst.lLock, eax_lock_dirty_bit);
        break;
    case EAXFXSLOT_FLAGS:
        eax_fx_slot_set<Eax4FlagsValidator>(call, dst.ulFlags, eax_flags_dirty_bit);
        break;
    default:
        eax_fail_unknown_property_id();
    }

    return eax_fx_slot_should_update_sources();
}

// Returns `true` if all sources should be updated, or `false` otherwise.
auto al::EffectSlot::eax5_fx_slot_set(const EaxCall& call) -> bool
{
    auto& dst = mEax5.i;

    switch(call.get_property_id())
    {
    case EAXFXSLOT_NONE:
        break;
    case EAXFXSLOT_ALLPARAMETERS:
        eax5_fx_slot_set_all(call);
        if(mEaxDf.test(eax_load_effect_dirty_bit))
            eax_fx_slot_load_effect(5, eax_get_efx_effect_type(dst.guidLoadEffect));
        break;
    case EAXFXSLOT_LOADEFFECT:
        eax_fx_slot_set_dirty<Eax4GuidLoadEffectValidator>(call, dst.guidLoadEffect,
            eax_load_effect_dirty_bit);
        if(mEaxDf.test(eax_load_effect_dirty_bit))
            eax_fx_slot_load_effect(5, eax_get_efx_effect_type(dst.guidLoadEffect));
        break;
    case EAXFXSLOT_VOLUME:
        eax_fx_slot_set<Eax4VolumeValidator>(call, dst.lVolume, eax_volume_dirty_bit);
        break;
    case EAXFXSLOT_LOCK:
        eax_fx_slot_set<Eax4LockValidator>(call, dst.lLock, eax_lock_dirty_bit);
        break;
    case EAXFXSLOT_FLAGS:
        eax_fx_slot_set<Eax5FlagsValidator>(call, dst.ulFlags, eax_flags_dirty_bit);
        break;
    case EAXFXSLOT_OCCLUSION:
        eax_fx_slot_set<Eax5OcclusionValidator>(call, dst.lOcclusion, eax_occlusion_dirty_bit);
        break;
    case EAXFXSLOT_OCCLUSIONLFRATIO:
        eax_fx_slot_set<Eax5OcclusionLfRatioValidator>(call, dst.flOcclusionLFRatio,
            eax_occlusion_lf_ratio_dirty_bit);
        break;
    default:
        eax_fail_unknown_property_id();
    }

    return eax_fx_slot_should_update_sources();
}

// Returns `true` if all sources should be updated, or `false` otherwise.
auto al::EffectSlot::eax_fx_slot_set(const EaxCall& call) -> bool
{
    switch(call.get_version())
    {
    case 4: return eax4_fx_slot_set(call);
    case 5: return eax5_fx_slot_set(call);
    default: eax_fail_unknown_version();
    }
}

// Returns `true` if all sources should be updated, or `false` otherwise.
auto al::EffectSlot::eax_set(const EaxCall& call) -> bool
{
    auto ret = false;

    switch(call.get_property_set_id())
    {
    case EaxCallPropertySetId::fx_slot: ret = eax_fx_slot_set(call); break;
    case EaxCallPropertySetId::fx_slot_effect: mEaxEffect->set(call); break;
    default: eax_fail_unknown_property_id();
    }

    const auto version = call.get_version();
    if(mEaxVersion != version)
        mEaxDf.set();
    mEaxVersion = version;

    return ret;
}

void al::EffectSlot::eax4_fx_slot_commit(std::bitset<eax_dirty_bit_count>& dst_df)
{
    eax_fx_slot_commit_property(mEax4, dst_df, eax_load_effect_dirty_bit,
        &EAX40FXSLOTPROPERTIES::guidLoadEffect);
    eax_fx_slot_commit_property(mEax4, dst_df, eax_volume_dirty_bit,
        &EAX40FXSLOTPROPERTIES::lVolume);
    eax_fx_slot_commit_property(mEax4, dst_df, eax_lock_dirty_bit, &EAX40FXSLOTPROPERTIES::lLock);
    eax_fx_slot_commit_property(mEax4, dst_df, eax_flags_dirty_bit,
        &EAX40FXSLOTPROPERTIES::ulFlags);

    auto& dst_i = mEax;

    if(dst_i.lOcclusion != EAXFXSLOT_DEFAULTOCCLUSION)
    {
        dst_df.set(eax_occlusion_dirty_bit);
        dst_i.lOcclusion = EAXFXSLOT_DEFAULTOCCLUSION;
    }

    if(dst_i.flOcclusionLFRatio != EAXFXSLOT_DEFAULTOCCLUSIONLFRATIO)
    {
        dst_df.set(eax_occlusion_lf_ratio_dirty_bit);
        dst_i.flOcclusionLFRatio = EAXFXSLOT_DEFAULTOCCLUSIONLFRATIO;
    }
}

void al::EffectSlot::eax5_fx_slot_commit(Eax5State &state,
    std::bitset<eax_dirty_bit_count> &dst_df)
{
    eax_fx_slot_commit_property(state, dst_df, eax_load_effect_dirty_bit,
        &EAX50FXSLOTPROPERTIES::guidLoadEffect);
    eax_fx_slot_commit_property(state, dst_df, eax_volume_dirty_bit,
        &EAX50FXSLOTPROPERTIES::lVolume);
    eax_fx_slot_commit_property(state, dst_df, eax_lock_dirty_bit, &EAX50FXSLOTPROPERTIES::lLock);
    eax_fx_slot_commit_property(state, dst_df, eax_flags_dirty_bit,
        &EAX50FXSLOTPROPERTIES::ulFlags);
    eax_fx_slot_commit_property(state, dst_df, eax_occlusion_dirty_bit,
        &EAX50FXSLOTPROPERTIES::lOcclusion);
    eax_fx_slot_commit_property(state, dst_df, eax_occlusion_lf_ratio_dirty_bit,
        &EAX50FXSLOTPROPERTIES::flOcclusionLFRatio);
}

void al::EffectSlot::eax_set_efx_slot_effect(EaxEffect const &effect)
{
#define EAX_PREFIX "[EAX_SET_EFFECT_SLOT_EFFECT] "

    initEffect(0, effect.al_effect_type_, effect.al_effect_props_, mEaxALContext);
    if(mState == SlotState::Initial)
    {
        mPropsDirty = false;
        updateProps(mEaxALContext);
        auto effect_slot_ptr = gsl::make_not_null(this);
        AddActiveEffectSlots({&effect_slot_ptr, 1}, mEaxALContext);
        mState = SlotState::Playing;
        return;
    }

    mPropsDirty = true;

#undef EAX_PREFIX
}

void al::EffectSlot::eax_set_efx_slot_send_auto(bool const is_send_auto)
{
    if(mAuxSendAuto == is_send_auto)
        return;

    mAuxSendAuto = is_send_auto;
    mPropsDirty = true;
}

void al::EffectSlot::eax_set_efx_slot_gain(f32 const gain)
{
#define EAX_PREFIX "[EAX_SET_EFFECT_SLOT_GAIN] "

    if(gain == mGain)
        return;
    if(gain < 0.0f || gain > 1.0f)
        ERR(EAX_PREFIX "Slot gain out of range: {}", gain);

    mGain = std::clamp(gain, 0.0f, 1.0f);
    mPropsDirty = true;

#undef EAX_PREFIX
}

void al::EffectSlot::EaxDeleter::operator()(gsl::not_null<EffectSlot*> const effect_slot) const
{
#define EAX_PREFIX "[EAX_DELETE_EFFECT_SLOT] "

    auto const context = al::get_not_null(effect_slot->mEaxALContext);
    auto slotlock = std::lock_guard{context->mEffectSlotLock};
    if(effect_slot->mRef.load(std::memory_order_relaxed) != 0)
    {
        ERR(EAX_PREFIX "Deleting in-use effect slot {}.", effect_slot->mId);
        return;
    }

    RemoveActiveEffectSlots({&effect_slot, 1}, context);
    FreeEffectSlot(context, effect_slot);

#undef EAX_PREFIX
}

auto eax_create_al_effect_slot(gsl::not_null<al::Context*> const context) -> EaxAlEffectSlotUPtr
{
#define EAX_PREFIX "[EAX_MAKE_EFFECT_SLOT] "

    auto slotlock = std::lock_guard{context->mEffectSlotLock};

    if(auto const& device = *context->mALDevice;
        context->mNumEffectSlots == device.AuxiliaryEffectSlotMax)
    {
        ERR(EAX_PREFIX "Out of memory.");
        return nullptr;
    }
    if(!EnsureEffectSlots(context, 1))
    {
        ERR(EAX_PREFIX "Failed to ensure.");
        return nullptr;
    }

    return EaxAlEffectSlotUPtr{AllocEffectSlot(context)};

#undef EAX_PREFIX
}
#endif // ALSOFT_EAX
