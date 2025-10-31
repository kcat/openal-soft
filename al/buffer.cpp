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

#include "buffer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AL/al.h"
#include "AL/alext.h"

#include "alc/context.h"
#include "alc/device.h"
#include "alc/inprogext.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "core/device.h"
#include "core/except.h"
#include "core/logging.h"
#include "core/resampler_limits.h"
#include "core/voice.h"
#include "direct_defs.h"
#include "gsl/gsl"
#include "intrusive_ptr.h"
#include "opthelpers.h"

#if ALSOFT_EAX
#include <unordered_set>

#include "eax/globals.h"
#include "eax/x_ram.h"
#endif // ALSOFT_EAX

using uint = unsigned int;


namespace {

using SubListAllocator = al::allocator<std::array<ALbuffer,64>>;

constexpr auto AmbiLayoutFromEnum(ALenum const layout) noexcept -> std::optional<AmbiLayout>
{
    switch(layout)
    {
    case AL_FUMA_SOFT: return AmbiLayout::FuMa;
    case AL_ACN_SOFT: return AmbiLayout::ACN;
    }
    return std::nullopt;
}
constexpr auto EnumFromAmbiLayout(AmbiLayout const layout) -> ALenum
{
    switch(layout)
    {
    case AmbiLayout::FuMa: return AL_FUMA_SOFT;
    case AmbiLayout::ACN: return AL_ACN_SOFT;
    }
    throw std::runtime_error{std::format("Invalid AmbiLayout: {}",
        int{al::to_underlying(layout)})};
}

constexpr auto AmbiScalingFromEnum(ALenum const scale) noexcept -> std::optional<AmbiScaling>
{
    switch(scale)
    {
    case AL_FUMA_SOFT: return AmbiScaling::FuMa;
    case AL_SN3D_SOFT: return AmbiScaling::SN3D;
    case AL_N3D_SOFT: return AmbiScaling::N3D;
    }
    return std::nullopt;
}
constexpr auto EnumFromAmbiScaling(AmbiScaling const scale) -> ALenum
{
    switch(scale)
    {
    case AmbiScaling::FuMa: return AL_FUMA_SOFT;
    case AmbiScaling::SN3D: return AL_SN3D_SOFT;
    case AmbiScaling::N3D: return AL_N3D_SOFT;
    case AmbiScaling::UHJ: break;
    }
    throw std::runtime_error{std::format("Invalid AmbiScaling: {}",
        int{al::to_underlying(scale)})};
}

#if ALSOFT_EAX
constexpr auto EaxStorageFromEnum(ALenum const scale) noexcept -> std::optional<EaxStorage>
{
    switch(scale)
    {
    case AL_STORAGE_AUTOMATIC: return EaxStorage::Automatic;
    case AL_STORAGE_ACCESSIBLE: return EaxStorage::Accessible;
    case AL_STORAGE_HARDWARE: return EaxStorage::Hardware;
    }
    return std::nullopt;
}
constexpr auto EnumFromEaxStorage(EaxStorage const storage) -> ALenum
{
    switch(storage)
    {
    case EaxStorage::Automatic: return AL_STORAGE_AUTOMATIC;
    case EaxStorage::Accessible: return AL_STORAGE_ACCESSIBLE;
    case EaxStorage::Hardware: return AL_STORAGE_HARDWARE;
    }
    throw std::runtime_error{std::format("Invalid EaxStorage: {}",
        int{al::to_underlying(storage)})};
}


auto eax_x_ram_check_availability(const al::Device &device, const ALbuffer &buffer,
    u32 const newsize) noexcept -> bool
{
    auto freemem = device.eax_x_ram_free_size;
    /* If the buffer is currently in "hardware", add its memory to the free
     * pool since it'll be "replaced".
     */
    if(buffer.mEaxXRamIsHardware)
        freemem += buffer.mOriginalSize;
    return freemem >= newsize;
}

void eax_x_ram_apply(al::Device &device, ALbuffer &buffer) noexcept
{
    if(buffer.mEaxXRamIsHardware)
        return;

    if(device.eax_x_ram_free_size >= buffer.mOriginalSize)
    {
        device.eax_x_ram_free_size -= buffer.mOriginalSize;
        buffer.mEaxXRamIsHardware = true;
    }
}

void eax_x_ram_clear(al::Device &al_device, ALbuffer &al_buffer) noexcept
{
    if(al_buffer.mEaxXRamIsHardware)
        al_device.eax_x_ram_free_size += al_buffer.mOriginalSize;
    al_buffer.mEaxXRamIsHardware = false;
}
#endif // ALSOFT_EAX


constexpr auto INVALID_STORAGE_MASK = ~gsl::narrow<ALbitfieldSOFT>(AL_MAP_READ_BIT_SOFT
    | AL_MAP_WRITE_BIT_SOFT | AL_MAP_PERSISTENT_BIT_SOFT | AL_PRESERVE_DATA_BIT_SOFT);
constexpr auto MAP_READ_WRITE_FLAGS = ALbitfieldSOFT{AL_MAP_READ_BIT_SOFT | AL_MAP_WRITE_BIT_SOFT};
constexpr auto INVALID_MAP_FLAGS = ~gsl::narrow<ALbitfieldSOFT>(AL_MAP_READ_BIT_SOFT
    | AL_MAP_WRITE_BIT_SOFT | AL_MAP_PERSISTENT_BIT_SOFT);


[[nodiscard]]
auto EnsureBuffers(gsl::not_null<al::Device*> const device, usize const needed) noexcept -> bool
try {
    auto count = std::accumulate(device->BufferList.cbegin(), device->BufferList.cend(), 0_uz,
        [](usize const cur, const BufferSubList &sublist) noexcept -> usize
        { return cur + gsl::narrow_cast<u32>(std::popcount(sublist.mFreeMask)); });

    while(needed > count)
    {
        if(device->BufferList.size() >= 1_uz<<25) [[unlikely]]
            return false;

        auto sublist = BufferSubList{};
        sublist.mFreeMask = ~0_u64;
        sublist.mBuffers = SubListAllocator{}.allocate(1);
        device->BufferList.emplace_back(std::move(sublist));
        count += std::tuple_size_v<SubListAllocator::value_type>;
    }
    return true;
}
catch(...) {
    return false;
}

[[nodiscard]]
auto AllocBuffer(gsl::not_null<al::Device*> const device) noexcept -> gsl::not_null<ALbuffer*>
{
    auto sublist = std::ranges::find_if(device->BufferList, &BufferSubList::mFreeMask);
    auto lidx = std::distance(device->BufferList.begin(), sublist);
    auto slidx = std::countr_zero(sublist->mFreeMask);
    ASSUME(slidx < 64);

    auto const buffer = gsl::make_not_null(std::construct_at(
        std::to_address(sublist->mBuffers->begin() + slidx)));

    /* Add 1 to avoid buffer ID 0. */
    buffer->mId = gsl::narrow_cast<u32>((lidx<<6) | slidx) + 1;

    sublist->mFreeMask &= ~(1_u64 << slidx);

    return buffer;
}

void FreeBuffer(gsl::not_null<al::Device*> const device, gsl::not_null<ALbuffer*> const buffer)
{
#if ALSOFT_EAX
    eax_x_ram_clear(*device, *buffer);
#endif // ALSOFT_EAX

    device->mBufferNames.erase(buffer->mId);

    const auto id = buffer->mId - 1;
    const auto lidx = id >> 6;
    const auto slidx = id & 0x3f;

    std::destroy_at(buffer.get());

    device->BufferList[lidx].mFreeMask |= 1_u64 << slidx;
}

[[nodiscard]]
inline auto LookupBuffer(std::nothrow_t, gsl::not_null<al::Device*> const device, u32 const id)
    noexcept -> ALbuffer*
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
    -> gsl::not_null<ALbuffer*>
{
    if(auto *const buffer = LookupBuffer(std::nothrow, al::get_not_null(context->mALDevice), id))
        [[likely]] return gsl::make_not_null(buffer);
    context->throw_error(AL_INVALID_NAME, "Invalid buffer ID {}", id);
}

[[nodiscard]]
constexpr auto SanitizeAlignment(FmtType const type, u32 const align) noexcept -> u32
{
    if(align == 0)
    {
        if(type == FmtIMA4)
        {
            /* Here is where things vary:
             * nVidia and Apple use 64+1 sample frames per block -> block_size=36 bytes per channel
             * Most PC sound software uses 2040+1 sample frames per block -> block_size=1024 bytes per channel
             */
            return 65;
        }
        if(type == FmtMSADPCM)
            return 64;
        return 1;
    }

    if(type == FmtIMA4)
    {
        /* IMA4 block alignment must be a multiple of 8, plus 1. */
        if((align&7) == 1) return align;
        return 0;
    }
    if(type == FmtMSADPCM)
    {
        /* MSADPCM block alignment must be a multiple of 2. */
        if((align&1) == 0) return align;
        return 0;
    }

    return align;
}


/** Loads the specified data into the buffer, using the specified format. */
void LoadData(gsl::not_null<al::Context*> const context, gsl::not_null<ALbuffer*> const ALBuf,
    i32 const freq, u32 const size, FmtChannels const DstChannels, FmtType const DstType,
    std::span<std::byte const> const SrcData, ALbitfieldSOFT const access)
{
    if(ALBuf->mRef.load(std::memory_order_relaxed) != 0 || ALBuf->mMappedAccess != 0)
        context->throw_error(AL_INVALID_OPERATION, "Modifying storage for in-use buffer {}",
            ALBuf->mId);

    auto const samplesPerBlock = SanitizeAlignment(DstType, ALBuf->mUnpackAlign);
    if(samplesPerBlock < 1)
        context->throw_error(AL_INVALID_VALUE, "Invalid unpack alignment {} for {} samples",
            ALBuf->mUnpackAlign, NameFromFormat(DstType));

    auto const ambiorder = IsBFormat(DstChannels) ? ALBuf->mUnpackAmbiOrder :
        (IsUHJ(DstChannels) ? 1_u32 : 0_u32);
    if(ambiorder > 3)
    {
        if(ALBuf->mAmbiLayout == AmbiLayout::FuMa)
            context->throw_error(AL_INVALID_OPERATION,
                "Cannot load {}{} order B-Format data with FuMa layout", ALBuf->mAmbiOrder,
                GetCounterSuffix(ALBuf->mAmbiOrder));
        if(ALBuf->mAmbiScaling == AmbiScaling::FuMa)
            context->throw_error(AL_INVALID_OPERATION,
                "Cannot load {}{} order B-Format data with FuMa scaling", ALBuf->mAmbiOrder,
                GetCounterSuffix(ALBuf->mAmbiOrder));
    }

    if((access&AL_PRESERVE_DATA_BIT_SOFT))
    {
        /* Can only preserve data with the same format and alignment. */
        if(ALBuf->mChannels != DstChannels || ALBuf->mType != DstType)
            context->throw_error(AL_INVALID_VALUE, "Preserving data of mismatched format");
        if(ALBuf->mBlockAlign != samplesPerBlock)
            context->throw_error(AL_INVALID_VALUE, "Preserving data of mismatched alignment");
        if(ALBuf->mAmbiOrder != ambiorder)
            context->throw_error(AL_INVALID_VALUE, "Preserving data of mismatched order");
    }

    /* Convert the size in bytes to blocks using the unpack block alignment. */
    auto const NumChannels = ChannelsFromFmt(DstChannels, ambiorder);
    auto const bytesPerBlock = NumChannels *
        ((DstType == FmtIMA4) ? (samplesPerBlock-1_u32)/2_u32 + 4_u32 :
        (DstType == FmtMSADPCM) ? (samplesPerBlock-2_u32)/2_u32 + 7_u32 :
        (samplesPerBlock * BytesFromFmt(DstType)));
    if((size%bytesPerBlock) != 0)
        context->throw_error(AL_INVALID_VALUE,
            "Data size {} is not a multiple of frame size {} ({} unpack alignment)",
            size, bytesPerBlock, samplesPerBlock);
    auto const blocks = size / bytesPerBlock;

    if(blocks > std::numeric_limits<i32>::max()/samplesPerBlock)
        context->throw_error(AL_OUT_OF_MEMORY,
            "Buffer size overflow, {} blocks x {} samples per block", blocks, samplesPerBlock);
    if(blocks > std::numeric_limits<usize>::max()/bytesPerBlock)
        context->throw_error(AL_OUT_OF_MEMORY,
            "Buffer size overflow, {} frames x {} bytes per frame", blocks, bytesPerBlock);

#if ALSOFT_EAX
    if(ALBuf->mEaxXRamMode == EaxStorage::Hardware)
    {
        auto &device = *context->mALDevice;
        if(!eax_x_ram_check_availability(device, *ALBuf, size))
            context->throw_error(AL_OUT_OF_MEMORY, "Out of X-RAM memory (avail: {}, needed: {})",
                device.eax_x_ram_free_size, size);
    }
#endif

    auto const newsize = usize{blocks} * bytesPerBlock;
    auto const needRealloc = std::visit([ALBuf,DstType,newsize,access]<typename T>(T &datavec)
        -> bool
    {
        using vector_t = std::remove_cvref_t<T>;
        using sample_t = vector_t::value_type;

        /* A new sample type must reallocate. */
        if(DstType != SampleInfo<sample_t>::format())
            return true;

        if(datavec.size() != newsize/sizeof(sample_t))
        {
            if(!(access&AL_PRESERVE_DATA_BIT_SOFT))
                return true;

            /* Reallocate in situ, to preserve existing samples as needed. */
            datavec.resize(newsize, SampleInfo<sample_t>::silence());
            ALBuf->mData = datavec;
        }
        return false;
    }, ALBuf->mDataStorage);
    if(needRealloc)
    {
        auto do_realloc = [ALBuf,newsize]<typename T>(T value)
        {
            using vector_t = al::vector<T, 16>;
            ALBuf->mData = ALBuf->mDataStorage.emplace<vector_t>(newsize/sizeof(T), value);
        };
        switch(DstType)
        {
        case FmtUByte: do_realloc(SampleInfo<u8>::silence()); break;
        case FmtShort: do_realloc(SampleInfo<i16>::silence()); break;
        case FmtInt: do_realloc(SampleInfo<i32>::silence()); break;
        case FmtFloat: do_realloc(SampleInfo<f32>::silence()); break;
        case FmtDouble: do_realloc(SampleInfo<f64>::silence()); break;
        case FmtMulaw: do_realloc(SampleInfo<MulawSample>::silence()); break;
        case FmtAlaw: do_realloc(SampleInfo<AlawSample>::silence()); break;
        case FmtIMA4: do_realloc(SampleInfo<IMA4Data>::silence()); break;
        case FmtMSADPCM: do_realloc(SampleInfo<MSADPCMData>::silence()); break;
        }
    }

    auto const bufferbytes = std::visit([](auto&& dataspan) -> std::span<std::byte>
    { return std::as_writable_bytes(dataspan); }, ALBuf->mData);
    std::ranges::copy(SrcData | std::views::take(newsize), bufferbytes.begin());

#if ALSOFT_EAX
    eax_x_ram_clear(*context->mALDevice, *ALBuf);
#endif

    ALBuf->mBlockAlign = (DstType == FmtIMA4 || DstType == FmtMSADPCM) ? samplesPerBlock : 1_u32;

    ALBuf->mOriginalSize = size;

    ALBuf->mAccess = access;

    ALBuf->mSampleRate = gsl::narrow_cast<u32>(freq);
    ALBuf->mChannels = DstChannels;
    ALBuf->mType = DstType;
    ALBuf->mAmbiOrder = ambiorder;

    ALBuf->mCallback = nullptr;
    ALBuf->mUserData = nullptr;

    ALBuf->mSampleLen = blocks * samplesPerBlock;
    ALBuf->mLoopStart = 0;
    ALBuf->mLoopEnd = ALBuf->mSampleLen;

#if ALSOFT_EAX
    if(eax_g_is_enabled && ALBuf->mEaxXRamMode == EaxStorage::Hardware)
        eax_x_ram_apply(*context->mALDevice, *ALBuf);
#endif
}

/** Prepares the buffer to use the specified callback, using the specified format. */
void PrepareCallback(gsl::not_null<al::Context*> const context,
    gsl::not_null<ALbuffer*> const ALBuf, i32 const freq, FmtChannels const DstChannels,
    FmtType const DstType, ALBUFFERCALLBACKTYPESOFT const callback, void *const userptr)
{
    if(ALBuf->mRef.load(std::memory_order_relaxed) != 0 || ALBuf->mMappedAccess != 0)
        context->throw_error(AL_INVALID_OPERATION, "Modifying callback for in-use buffer {}",
            ALBuf->mId);

    const auto ambiorder = IsBFormat(DstChannels) ? ALBuf->mUnpackAmbiOrder :
        (IsUHJ(DstChannels) ? 1_u32 : 0_u32);

    const auto samplesPerBlock = SanitizeAlignment(DstType, ALBuf->mUnpackAlign);
    if(samplesPerBlock < 1)
        context->throw_error(AL_INVALID_VALUE, "Invalid unpack alignment {} for {} samples",
            ALBuf->mUnpackAlign, NameFromFormat(DstType));

    const auto bytesPerBlock = ChannelsFromFmt(DstChannels, ambiorder) *
        ((DstType == FmtIMA4) ? (samplesPerBlock-1_u32)/2_u32 + 4_u32 :
        (DstType == FmtMSADPCM) ? (samplesPerBlock-2_u32)/2_u32 + 7_u32 :
        (samplesPerBlock * BytesFromFmt(DstType)));

    /* The maximum number of samples a callback buffer may need to store is a
     * full mixing line * max pitch * channel count, since it may need to hold
     * a full line's worth of sample frames before downsampling. An additional
     * MaxResamplerEdge is needed for "future" samples during resampling (the
     * voice will hold a history for the past samples).
     */
    static constexpr auto line_size = DeviceBase::MixerLineSize*MaxPitch + MaxResamplerEdge;
    const auto line_blocks = (line_size + samplesPerBlock-1_u32) / samplesPerBlock;

    const auto newsize = line_blocks * bytesPerBlock;
    auto do_realloc = [ALBuf,newsize]<typename T>(T value)
    {
        using vector_t = al::vector<T,16>;
        ALBuf->mData = ALBuf->mDataStorage.emplace<vector_t>(newsize/sizeof(T), value);
    };
    switch(DstType)
    {
    case FmtUByte: do_realloc(SampleInfo<u8>::silence()); break;
    case FmtShort: do_realloc(SampleInfo<i16>::silence()); break;
    case FmtInt: do_realloc(SampleInfo<i32>::silence()); break;
    case FmtFloat: do_realloc(SampleInfo<f32>::silence()); break;
    case FmtDouble: do_realloc(SampleInfo<f64>::silence()); break;
    case FmtMulaw: do_realloc(SampleInfo<MulawSample>::silence()); break;
    case FmtAlaw: do_realloc(SampleInfo<AlawSample>::silence()); break;
    case FmtIMA4: do_realloc(SampleInfo<IMA4Data>::silence()); break;
    case FmtMSADPCM: do_realloc(SampleInfo<MSADPCMData>::silence()); break;
    }

#if ALSOFT_EAX
    eax_x_ram_clear(*context->mALDevice, *ALBuf);
#endif

    ALBuf->mCallback = callback;
    ALBuf->mUserData = userptr;

    ALBuf->mOriginalSize = 0;
    ALBuf->mAccess = 0;

    ALBuf->mBlockAlign = (DstType == FmtIMA4 || DstType == FmtMSADPCM) ? samplesPerBlock : 1_u32;
    ALBuf->mSampleRate = gsl::narrow_cast<u32>(freq);
    ALBuf->mChannels = DstChannels;
    ALBuf->mType = DstType;
    ALBuf->mAmbiOrder = ambiorder;

    ALBuf->mSampleLen = 0;
    ALBuf->mLoopStart = 0;
    ALBuf->mLoopEnd = ALBuf->mSampleLen;
}

/** Prepares the buffer to use caller-specified storage. */
void PrepareUserPtr(gsl::not_null<al::Context*> const context [[maybe_unused]],
    gsl::not_null<ALbuffer*> const ALBuf, i32 const freq, FmtChannels const DstChannels,
    FmtType const DstType, void *const usrdata, u32 const usrdatalen)
{
    if(ALBuf->mRef.load(std::memory_order_relaxed) != 0 || ALBuf->mMappedAccess != 0)
        context->throw_error(AL_INVALID_OPERATION, "Modifying storage for in-use buffer {}",
            ALBuf->mId);

    const auto samplesPerBlock = SanitizeAlignment(DstType, ALBuf->mUnpackAlign);
    if(samplesPerBlock < 1)
        context->throw_error(AL_INVALID_VALUE, "Invalid unpack alignment {} for {} samples",
            ALBuf->mUnpackAlign, NameFromFormat(DstType));

    const auto typealign = std::invoke([DstType]() noexcept -> u32
    {
        /* NOTE: This only needs to be the required alignment for the CPU to
         * read/write the given sample type in the mixer.
         */
        switch(DstType)
        {
        case FmtUByte: return alignof(ALubyte);
        case FmtShort: return alignof(ALshort);
        case FmtInt: return alignof(ALint);
        case FmtFloat: return alignof(ALfloat);
        case FmtDouble: return alignof(ALdouble);
        case FmtMulaw: return alignof(MulawSample);
        case FmtAlaw: return alignof(AlawSample);
        case FmtIMA4: break;
        case FmtMSADPCM: break;
        }
        return 1;
    });
    /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
    if((reinterpret_cast<uintptr_t>(usrdata) & (typealign-1)) != 0)
        context->throw_error(AL_INVALID_VALUE, "Pointer {} is misaligned for {} samples ({})",
            usrdata, NameFromFormat(DstType), typealign);

    const auto ambiorder = IsBFormat(DstChannels) ? ALBuf->mUnpackAmbiOrder :
        (IsUHJ(DstChannels) ? 1_u32 : 0_u32);

    /* Convert the size in bytes to blocks using the unpack block alignment. */
    const auto NumChannels = ChannelsFromFmt(DstChannels, ambiorder);
    const auto bytesPerBlock = NumChannels *
        ((DstType == FmtIMA4) ? (samplesPerBlock-1_u32)/2_u32 + 4_u32 :
        (DstType == FmtMSADPCM) ? (samplesPerBlock-2_u32)/2_u32 + 7_u32 :
        (samplesPerBlock * BytesFromFmt(DstType)));
    if((usrdatalen%bytesPerBlock) != 0)
        context->throw_error(AL_INVALID_VALUE,
            "Data size {} is not a multiple of frame size {} ({} unpack alignment)",
            usrdatalen, bytesPerBlock, samplesPerBlock);
    const auto blocks = usrdatalen / bytesPerBlock;

    if(blocks > std::numeric_limits<i32>::max()/samplesPerBlock)
        context->throw_error(AL_OUT_OF_MEMORY,
            "Buffer size overflow, {} blocks x {} samples per block", blocks, samplesPerBlock);
    if(blocks > std::numeric_limits<usize>::max()/bytesPerBlock)
        context->throw_error(AL_OUT_OF_MEMORY,
            "Buffer size overflow, {} frames x {} bytes per frame", blocks, bytesPerBlock);

#if ALSOFT_EAX
    if(ALBuf->mEaxXRamMode == EaxStorage::Hardware)
    {
        auto &device = *context->mALDevice;
        if(!eax_x_ram_check_availability(device, *ALBuf, usrdatalen))
            context->throw_error(AL_OUT_OF_MEMORY, "Out of X-RAM memory (avail: {}, needed: {})",
                device.eax_x_ram_free_size, usrdatalen);
    }
#endif

    auto do_realloc = [ALBuf,usrdata,usrdatalen]<typename T>(T value [[maybe_unused]])
    {
        ALBuf->mDataStorage.emplace<al::vector<T, 16>>();
        ALBuf->mData = std::span{static_cast<T*>(usrdata), usrdatalen/sizeof(T)};
    };
    switch(DstType)
    {
    case FmtUByte: do_realloc(SampleInfo<u8>::silence()); break;
    case FmtShort: do_realloc(SampleInfo<i16>::silence()); break;
    case FmtInt: do_realloc(SampleInfo<i32>::silence()); break;
    case FmtFloat: do_realloc(SampleInfo<f32>::silence()); break;
    case FmtDouble: do_realloc(SampleInfo<f64>::silence()); break;
    case FmtMulaw: do_realloc(SampleInfo<MulawSample>::silence()); break;
    case FmtAlaw: do_realloc(SampleInfo<AlawSample>::silence()); break;
    case FmtIMA4: do_realloc(SampleInfo<IMA4Data>::silence()); break;
    case FmtMSADPCM: do_realloc(SampleInfo<MSADPCMData>::silence()); break;
    }

#if ALSOFT_EAX
    eax_x_ram_clear(*context->mALDevice, *ALBuf);
#endif

    ALBuf->mCallback = nullptr;
    ALBuf->mUserData = nullptr;

    ALBuf->mOriginalSize = usrdatalen;
    ALBuf->mAccess = 0;

    ALBuf->mBlockAlign = (DstType == FmtIMA4 || DstType == FmtMSADPCM) ? samplesPerBlock : 1_u32;
    ALBuf->mSampleRate = gsl::narrow_cast<u32>(freq);
    ALBuf->mChannels = DstChannels;
    ALBuf->mType = DstType;
    ALBuf->mAmbiOrder = ambiorder;

    ALBuf->mSampleLen = blocks * samplesPerBlock;
    ALBuf->mLoopStart = 0;
    ALBuf->mLoopEnd = ALBuf->mSampleLen;

#if ALSOFT_EAX
    if(ALBuf->mEaxXRamMode == EaxStorage::Hardware)
        eax_x_ram_apply(*context->mALDevice, *ALBuf);
#endif
}


struct DecompResult { FmtChannels channels; FmtType type; };
auto DecomposeUserFormat(ALenum const format) noexcept -> std::optional<DecompResult>
{
    struct FormatMap {
        ALenum format;
        DecompResult result;
    };
    static constexpr std::array UserFmtList{
        FormatMap{AL_FORMAT_MONO8,             {FmtMono, FmtUByte}  },
        FormatMap{AL_FORMAT_MONO16,            {FmtMono, FmtShort}  },
        FormatMap{AL_FORMAT_MONO_I32,          {FmtMono, FmtInt}    },
        FormatMap{AL_FORMAT_MONO_FLOAT32,      {FmtMono, FmtFloat}  },
        FormatMap{AL_FORMAT_MONO_DOUBLE_EXT,   {FmtMono, FmtDouble} },
        FormatMap{AL_FORMAT_MONO_IMA4,         {FmtMono, FmtIMA4}   },
        FormatMap{AL_FORMAT_MONO_MSADPCM_SOFT, {FmtMono, FmtMSADPCM}},
        FormatMap{AL_FORMAT_MONO_MULAW,        {FmtMono, FmtMulaw}  },
        FormatMap{AL_FORMAT_MONO_ALAW_EXT,     {FmtMono, FmtAlaw}   },

        FormatMap{AL_FORMAT_STEREO8,             {FmtStereo, FmtUByte}  },
        FormatMap{AL_FORMAT_STEREO16,            {FmtStereo, FmtShort}  },
        FormatMap{AL_FORMAT_STEREO_I32,          {FmtStereo, FmtInt}    },
        FormatMap{AL_FORMAT_STEREO_FLOAT32,      {FmtStereo, FmtFloat}  },
        FormatMap{AL_FORMAT_STEREO_DOUBLE_EXT,   {FmtStereo, FmtDouble} },
        FormatMap{AL_FORMAT_STEREO_IMA4,         {FmtStereo, FmtIMA4}   },
        FormatMap{AL_FORMAT_STEREO_MSADPCM_SOFT, {FmtStereo, FmtMSADPCM}},
        FormatMap{AL_FORMAT_STEREO_MULAW,        {FmtStereo, FmtMulaw}  },
        FormatMap{AL_FORMAT_STEREO_ALAW_EXT,     {FmtStereo, FmtAlaw}   },

        FormatMap{AL_FORMAT_REAR8,        {FmtRear, FmtUByte}},
        FormatMap{AL_FORMAT_REAR16,       {FmtRear, FmtShort}},
        FormatMap{AL_FORMAT_REAR32,       {FmtRear, FmtFloat}},
        FormatMap{AL_FORMAT_REAR_I32,     {FmtRear, FmtInt}  },
        FormatMap{AL_FORMAT_REAR_FLOAT32, {FmtRear, FmtFloat}},
        FormatMap{AL_FORMAT_REAR_MULAW,   {FmtRear, FmtMulaw}},

        FormatMap{AL_FORMAT_QUAD8_LOKI,  {FmtQuad, FmtUByte}},
        FormatMap{AL_FORMAT_QUAD16_LOKI, {FmtQuad, FmtShort}},

        FormatMap{AL_FORMAT_QUAD8,        {FmtQuad, FmtUByte}},
        FormatMap{AL_FORMAT_QUAD16,       {FmtQuad, FmtShort}},
        FormatMap{AL_FORMAT_QUAD32,       {FmtQuad, FmtFloat}},
        FormatMap{AL_FORMAT_QUAD_I32,     {FmtQuad, FmtInt}  },
        FormatMap{AL_FORMAT_QUAD_FLOAT32, {FmtQuad, FmtFloat}},
        FormatMap{AL_FORMAT_QUAD_MULAW,   {FmtQuad, FmtMulaw}},

        FormatMap{AL_FORMAT_51CHN8,        {FmtX51, FmtUByte}},
        FormatMap{AL_FORMAT_51CHN16,       {FmtX51, FmtShort}},
        FormatMap{AL_FORMAT_51CHN32,       {FmtX51, FmtFloat}},
        FormatMap{AL_FORMAT_51CHN_I32,     {FmtX51, FmtInt}  },
        FormatMap{AL_FORMAT_51CHN_FLOAT32, {FmtX51, FmtFloat}},
        FormatMap{AL_FORMAT_51CHN_MULAW,   {FmtX51, FmtMulaw}},

        FormatMap{AL_FORMAT_61CHN8,        {FmtX61, FmtUByte}},
        FormatMap{AL_FORMAT_61CHN16,       {FmtX61, FmtShort}},
        FormatMap{AL_FORMAT_61CHN32,       {FmtX61, FmtFloat}},
        FormatMap{AL_FORMAT_61CHN_I32,     {FmtX61, FmtInt}  },
        FormatMap{AL_FORMAT_61CHN_FLOAT32, {FmtX61, FmtFloat}},
        FormatMap{AL_FORMAT_61CHN_MULAW,   {FmtX61, FmtMulaw}},

        FormatMap{AL_FORMAT_71CHN8,        {FmtX71, FmtUByte}},
        FormatMap{AL_FORMAT_71CHN16,       {FmtX71, FmtShort}},
        FormatMap{AL_FORMAT_71CHN32,       {FmtX71, FmtFloat}},
        FormatMap{AL_FORMAT_71CHN_I32,     {FmtX71, FmtInt}  },
        FormatMap{AL_FORMAT_71CHN_FLOAT32, {FmtX71, FmtFloat}},
        FormatMap{AL_FORMAT_71CHN_MULAW,   {FmtX71, FmtMulaw}},

        FormatMap{AL_FORMAT_BFORMAT2D_8,       {FmtBFormat2D, FmtUByte}},
        FormatMap{AL_FORMAT_BFORMAT2D_16,      {FmtBFormat2D, FmtShort}},
        FormatMap{AL_FORMAT_BFORMAT2D_I32,     {FmtBFormat2D, FmtInt}  },
        FormatMap{AL_FORMAT_BFORMAT2D_FLOAT32, {FmtBFormat2D, FmtFloat}},
        FormatMap{AL_FORMAT_BFORMAT2D_MULAW,   {FmtBFormat2D, FmtMulaw}},

        FormatMap{AL_FORMAT_BFORMAT3D_8,       {FmtBFormat3D, FmtUByte}},
        FormatMap{AL_FORMAT_BFORMAT3D_16,      {FmtBFormat3D, FmtShort}},
        FormatMap{AL_FORMAT_BFORMAT3D_I32,     {FmtBFormat3D, FmtInt}  },
        FormatMap{AL_FORMAT_BFORMAT3D_FLOAT32, {FmtBFormat3D, FmtFloat}},
        FormatMap{AL_FORMAT_BFORMAT3D_MULAW,   {FmtBFormat3D, FmtMulaw}},

        FormatMap{AL_FORMAT_UHJ2CHN8_SOFT,        {FmtUHJ2, FmtUByte}  },
        FormatMap{AL_FORMAT_UHJ2CHN16_SOFT,       {FmtUHJ2, FmtShort}  },
        FormatMap{AL_FORMAT_UHJ2CHN_I32_SOFT,     {FmtUHJ2, FmtInt}    },
        FormatMap{AL_FORMAT_UHJ2CHN_FLOAT32_SOFT, {FmtUHJ2, FmtFloat}  },
        FormatMap{AL_FORMAT_UHJ2CHN_MULAW_SOFT,   {FmtUHJ2, FmtMulaw}  },
        FormatMap{AL_FORMAT_UHJ2CHN_ALAW_SOFT,    {FmtUHJ2, FmtAlaw}   },
        FormatMap{AL_FORMAT_UHJ2CHN_IMA4_SOFT,    {FmtUHJ2, FmtIMA4}   },
        FormatMap{AL_FORMAT_UHJ2CHN_MSADPCM_SOFT, {FmtUHJ2, FmtMSADPCM}},

        FormatMap{AL_FORMAT_UHJ3CHN8_SOFT,        {FmtUHJ3, FmtUByte}},
        FormatMap{AL_FORMAT_UHJ3CHN16_SOFT,       {FmtUHJ3, FmtShort}},
        FormatMap{AL_FORMAT_UHJ3CHN_I32_SOFT,     {FmtUHJ3, FmtInt}  },
        FormatMap{AL_FORMAT_UHJ3CHN_FLOAT32_SOFT, {FmtUHJ3, FmtFloat}},
        FormatMap{AL_FORMAT_UHJ3CHN_MULAW_SOFT,   {FmtUHJ3, FmtMulaw}},
        FormatMap{AL_FORMAT_UHJ3CHN_ALAW_SOFT,    {FmtUHJ3, FmtAlaw} },

        FormatMap{AL_FORMAT_UHJ4CHN8_SOFT,        {FmtUHJ4, FmtUByte}},
        FormatMap{AL_FORMAT_UHJ4CHN16_SOFT,       {FmtUHJ4, FmtShort}},
        FormatMap{AL_FORMAT_UHJ4CHN_I32_SOFT,     {FmtUHJ4, FmtInt}  },
        FormatMap{AL_FORMAT_UHJ4CHN_FLOAT32_SOFT, {FmtUHJ4, FmtFloat}},
        FormatMap{AL_FORMAT_UHJ4CHN_MULAW_SOFT,   {FmtUHJ4, FmtMulaw}},
        FormatMap{AL_FORMAT_UHJ4CHN_ALAW_SOFT,    {FmtUHJ4, FmtAlaw} },
    };

    if(const auto iter = std::ranges::find(UserFmtList, format, &FormatMap::format);
        iter != UserFmtList.end())
        return iter->result;
    return std::nullopt;
}


void alGenBuffers(gsl::not_null<al::Context*> const context, ALsizei const n, u32 *const buffers)
    noexcept
try {
    if(n < 0)
        context->throw_error(AL_INVALID_VALUE, "Generating {} buffers", n);
    if(n <= 0) [[unlikely]] return;

    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock = std::lock_guard{device->BufferLock};

    const auto bids = std::views::counted(buffers, n);
    if(!EnsureBuffers(device, bids.size()))
        context->throw_error(AL_OUT_OF_MEMORY, "Failed to allocate {} buffer{}", n,
            (n==1) ? "" : "s");

    std::ranges::generate(bids, [device]{ return AllocBuffer(device)->mId; });
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alDeleteBuffers(gsl::not_null<al::Context*> const context, ALsizei const n,
    const u32 *const buffers) noexcept
try {
    if(n < 0)
        context->throw_error(AL_INVALID_VALUE, "Deleting {} buffers", n);
    if(n <= 0) [[unlikely]] return;

    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock = std::lock_guard{device->BufferLock};

    /* First try to find any buffers that are invalid or in-use. */
    auto const bids = std::views::counted(buffers, n);
    std::ranges::for_each(bids, [context](u32 const bid)
    {
        if(!bid) return;
        auto const albuf = LookupBuffer(context, bid);
        if(albuf->mRef.load(std::memory_order_relaxed) != 0)
            context->throw_error(AL_INVALID_OPERATION, "Deleting in-use buffer {}", bid);
    });

    /* All good. Delete non-0 buffer IDs. */
    std::ranges::for_each(bids, [device](u32 const bid) -> void
    {
        if(auto *const buffer = LookupBuffer(std::nothrow, device, bid))
            FreeBuffer(device, gsl::make_not_null(buffer));
    });
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

auto alIsBuffer(gsl::not_null<al::Context*> const context, u32 const buffer) noexcept -> ALboolean
{
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock = std::lock_guard{device->BufferLock};
    if(buffer == 0 || LookupBuffer(std::nothrow, device, buffer) != nullptr)
        return AL_TRUE;
    return AL_FALSE;
}


void alBufferStorageSOFT(gsl::not_null<al::Context*> const context, u32 const buffer,
    ALenum const format, void const *const data, ALsizei const size, i32 const freq,
    ALbitfieldSOFT const flags) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock = std::lock_guard{device->BufferLock};

    auto const albuf = LookupBuffer(context, buffer);
    if(size < 0)
        context->throw_error(AL_INVALID_VALUE, "Negative storage size {}", size);
    if(freq < 1)
        context->throw_error(AL_INVALID_VALUE, "Invalid sample rate {}", freq);
    if((flags&INVALID_STORAGE_MASK) != 0)
        context->throw_error(AL_INVALID_VALUE, "Invalid storage flags {:#x}",
            flags&INVALID_STORAGE_MASK);
    if((flags&AL_MAP_PERSISTENT_BIT_SOFT) && !(flags&MAP_READ_WRITE_FLAGS))
        context->throw_error(AL_INVALID_VALUE,
            "Declaring persistently mapped storage without read or write access");

    auto const usrfmt = DecomposeUserFormat(format);
    if(!usrfmt)
        context->throw_error(AL_INVALID_ENUM, "Invalid format {:#04x}", as_unsigned(format));

    auto *const bdata = static_cast<const std::byte*>(data);
    auto const usize = gsl::narrow<u32>(size);
    LoadData(context, albuf, freq, usize, usrfmt->channels, usrfmt->type,
        std::span{bdata, bdata ? usize : 0_u32}, flags);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alBufferData(gsl::not_null<al::Context*> const context, u32 const buffer, ALenum const format,
    void const *const data, ALsizei const size, i32 const freq) noexcept
{
    alBufferStorageSOFT(context, buffer, format, data, size, freq, 0);
}

void alBufferDataStatic(gsl::not_null<al::Context*> const context, u32 const buffer,
    ALenum const format, void *const data, ALsizei const size, i32 const freq) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock = std::lock_guard{device->BufferLock};

    auto const albuf = LookupBuffer(context, buffer);
    if(size < 0)
        context->throw_error(AL_INVALID_VALUE, "Negative storage size {}", size);
    if(freq < 1)
        context->throw_error(AL_INVALID_VALUE, "Invalid sample rate {}", freq);

    auto usrfmt = DecomposeUserFormat(format);
    if(!usrfmt)
        context->throw_error(AL_INVALID_ENUM, "Invalid format {:#04x}", as_unsigned(format));

    PrepareUserPtr(context, albuf, freq, usrfmt->channels, usrfmt->type, data,
        gsl::narrow<u32>(size));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void alBufferCallbackSOFT(gsl::not_null<al::Context*> const context, u32 const buffer,
    ALenum const format, i32 const freq, ALBUFFERCALLBACKTYPESOFT const callback,
    void *const userptr) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock = std::lock_guard{device->BufferLock};

    auto const albuf = LookupBuffer(context, buffer);
    if(freq < 1)
        context->throw_error(AL_INVALID_VALUE, "Invalid sample rate {}", freq);
    if(callback == nullptr)
        context->throw_error(AL_INVALID_VALUE, "NULL callback");

    auto usrfmt = DecomposeUserFormat(format);
    if(!usrfmt)
        context->throw_error(AL_INVALID_ENUM, "Invalid format {:#04x}", as_unsigned(format));

    PrepareCallback(context, albuf, freq, usrfmt->channels, usrfmt->type, callback, userptr);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void alBufferSubDataSOFT(gsl::not_null<al::Context*> const context, u32 const buffer,
    ALenum const format, void const *const data, ALsizei const offset, ALsizei const length)
    noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock = std::lock_guard{device->BufferLock};

    auto const albuf = LookupBuffer(context, buffer);

    auto const usrfmt = DecomposeUserFormat(format);
    if(!usrfmt)
        context->throw_error(AL_INVALID_ENUM, "Invalid format {:#04x}", as_unsigned(format));

    const auto unpack_align = albuf->mUnpackAlign;
    const auto align = SanitizeAlignment(usrfmt->type, unpack_align);
    if(align < 1)
        context->throw_error(AL_INVALID_VALUE, "Invalid unpack alignment {}", unpack_align);
    if(usrfmt->channels != albuf->mChannels || usrfmt->type != albuf->mType)
        context->throw_error(AL_INVALID_ENUM, "Unpacking data with mismatched format");
    if(align != albuf->mBlockAlign)
        context->throw_error(AL_INVALID_VALUE,
            "Unpacking data with alignment {} does not match original alignment {}", align,
            albuf->mBlockAlign);
    if(albuf->isBFormat() && albuf->mUnpackAmbiOrder != albuf->mAmbiOrder)
        context->throw_error(AL_INVALID_VALUE, "Unpacking data with mismatched ambisonic order");
    if(albuf->mMappedAccess != 0)
        context->throw_error(AL_INVALID_OPERATION, "Unpacking data into mapped buffer {}", buffer);

    const auto num_chans = albuf->channelsFromFmt();
    const auto byte_align = (albuf->mType == FmtIMA4) ? ((align-1u)/2u + 4u) * num_chans :
        (albuf->mType == FmtMSADPCM) ? ((align-2u)/2u + 7u) * num_chans :
        (align * albuf->bytesFromFmt() * num_chans);

    if(offset < 0 || length < 0 || gsl::narrow_cast<usize>(offset) > albuf->mOriginalSize
        || gsl::narrow_cast<usize>(length) > albuf->mOriginalSize - gsl::narrow_cast<usize>(offset))
        context->throw_error(AL_INVALID_VALUE, "Invalid data sub-range {}+{} on buffer {}", offset,
            length, buffer);
    if((gsl::narrow_cast<usize>(offset)%byte_align) != 0)
        context->throw_error(AL_INVALID_VALUE,
            "Sub-range offset {} is not a multiple of frame size {} ({} unpack alignment)",
            offset, byte_align, align);
    if((gsl::narrow_cast<usize>(length)%byte_align) != 0)
        context->throw_error(AL_INVALID_VALUE,
            "Sub-range length {} is not a multiple of frame size {} ({} unpack alignment)",
            length, byte_align, align);

    auto bufferbytes = std::visit([](auto &datavec)
    { return std::as_writable_bytes(datavec); }, albuf->mData);
    std::ranges::copy(std::views::counted(static_cast<std::byte const*>(data), length),
        (bufferbytes | std::views::drop(offset)).begin());
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


auto alMapBufferSOFT(gsl::not_null<al::Context*> const context, u32 const buffer,
    ALsizei const offset, ALsizei const length, ALbitfieldSOFT const access) noexcept -> void*
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock = std::lock_guard{device->BufferLock};

    auto const albuf = LookupBuffer(context, buffer);
    if((access&INVALID_MAP_FLAGS) != 0)
        context->throw_error(AL_INVALID_VALUE, "Invalid map flags {:#x}",
            access&INVALID_MAP_FLAGS);
    if(!(access&MAP_READ_WRITE_FLAGS))
        context->throw_error(AL_INVALID_VALUE, "Mapping buffer {} without read or write access",
            buffer);

    auto const unavailable = (albuf->mAccess^access) & access;
    if(albuf->mRef.load(std::memory_order_relaxed) != 0 && !(access&AL_MAP_PERSISTENT_BIT_SOFT))
        context->throw_error(AL_INVALID_OPERATION,
            "Mapping in-use buffer {} without persistent mapping", buffer);
    if(albuf->mMappedAccess != 0)
        context->throw_error(AL_INVALID_OPERATION, "Mapping already-mapped buffer {}", buffer);
    if((unavailable&AL_MAP_READ_BIT_SOFT))
        context->throw_error(AL_INVALID_VALUE, "Mapping buffer {} for reading without read access",
            buffer);
    if((unavailable&AL_MAP_WRITE_BIT_SOFT))
        context->throw_error(AL_INVALID_VALUE,
            "Mapping buffer {} for writing without write access", buffer);
    if((unavailable&AL_MAP_PERSISTENT_BIT_SOFT))
        context->throw_error(AL_INVALID_VALUE,
            "Mapping buffer {} persistently without persistent access", buffer);
    if(offset < 0 || length <= 0 || gsl::narrow_cast<usize>(offset) >= albuf->mOriginalSize
        || gsl::narrow_cast<usize>(length) > albuf->mOriginalSize - gsl::narrow_cast<usize>(offset))
        context->throw_error(AL_INVALID_VALUE, "Mapping invalid range {}+{} for buffer {}", offset,
            length, buffer);

    auto *const retval = std::visit([ptroff=gsl::narrow_cast<usize>(offset)](auto &datavec)
    { return &std::as_writable_bytes(datavec)[ptroff]; }, albuf->mData);
    albuf->mMappedAccess = access;
    albuf->mMappedOffset = offset;
    albuf->mMappedSize = length;
    return retval;
}
catch(al::base_exception&) {
    return nullptr;
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
    return nullptr;
}

void alUnmapBufferSOFT(gsl::not_null<al::Context*> const context, u32 const buffer) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock = std::lock_guard{device->BufferLock};

    auto const albuf = LookupBuffer(context, buffer);
    if(albuf->mMappedAccess == 0)
        context->throw_error(AL_INVALID_OPERATION, "Unmapping unmapped buffer {}", buffer);

    albuf->mMappedAccess = 0;
    albuf->mMappedOffset = 0;
    albuf->mMappedSize = 0;
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alFlushMappedBufferSOFT(gsl::not_null<al::Context*> const context, u32 const buffer,
    ALsizei const offset, ALsizei const length) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock = std::lock_guard{device->BufferLock};

    auto const albuf = LookupBuffer(context, buffer);
    if(!(albuf->mMappedAccess&AL_MAP_WRITE_BIT_SOFT))
        context->throw_error(AL_INVALID_OPERATION,
            "Flushing buffer {} while not mapped for writing", buffer);
    if(offset < albuf->mMappedOffset || length <= 0
        || offset >= albuf->mMappedOffset+albuf->mMappedSize
        || length > albuf->mMappedOffset+albuf->mMappedSize-offset)
        context->throw_error(AL_INVALID_VALUE, "Flushing invalid range {}+{} on buffer {}", offset,
            length, buffer);

    /* FIXME: Need to use some method of double-buffering for the mixer and app
     * to hold separate memory, which can be safely transferred asynchronously.
     * Currently we just say the app shouldn't write where OpenAL's reading,
     * and hope for the best...
     */
    std::atomic_thread_fence(std::memory_order_seq_cst);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void alBufferf(gsl::not_null<al::Context*> const context, u32 const buffer, ALenum const param,
    f32 const value [[maybe_unused]]) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock [[maybe_unused]] = std::lock_guard{device->BufferLock};

    std::ignore = LookupBuffer(context, buffer);

    context->throw_error(AL_INVALID_ENUM, "Invalid buffer float property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alBuffer3f(gsl::not_null<al::Context*> const context, u32 const buffer, ALenum const param,
    f32 const value1 [[maybe_unused]], f32 const value2 [[maybe_unused]],
    f32 const value3 [[maybe_unused]]) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock [[maybe_unused]] = std::lock_guard{device->BufferLock};

    std::ignore = LookupBuffer(context, buffer);

    context->throw_error(AL_INVALID_ENUM, "Invalid buffer 3-float property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alBufferfv(gsl::not_null<al::Context*> const context, u32 const buffer, ALenum const param,
    f32 const *const values) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock [[maybe_unused]] = std::lock_guard{device->BufferLock};

    std::ignore = LookupBuffer(context, buffer);
    if(!values)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    context->throw_error(AL_INVALID_ENUM, "Invalid buffer float-vector property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void alBufferi(gsl::not_null<al::Context*> const context, u32 const buffer, ALenum const param,
    i32 const value) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock = std::lock_guard{device->BufferLock};

    auto const albuf = LookupBuffer(context, buffer);
    switch(param)
    {
    case AL_UNPACK_BLOCK_ALIGNMENT_SOFT:
        if(value < 0)
            context->throw_error(AL_INVALID_VALUE, "Invalid unpack block alignment {}", value);
        albuf->mUnpackAlign = gsl::narrow_cast<u32>(value);
        return;

    case AL_PACK_BLOCK_ALIGNMENT_SOFT:
        if(value < 0)
            context->throw_error(AL_INVALID_VALUE, "Invalid pack block alignment {}", value);
        albuf->mPackAlign = gsl::narrow_cast<u32>(value);
        return;

    case AL_AMBISONIC_LAYOUT_SOFT:
        if(albuf->mRef.load(std::memory_order_relaxed) != 0)
            context->throw_error(AL_INVALID_OPERATION,
                "Modifying in-use buffer {}'s ambisonic layout", buffer);
        if(const auto layout = AmbiLayoutFromEnum(value))
        {
            if(layout.value() == AmbiLayout::FuMa && albuf->mAmbiOrder > 3)
                context->throw_error(AL_INVALID_OPERATION,
                    "Cannot set FuMa layout for {}{} order B-Format data", albuf->mAmbiOrder,
                    GetCounterSuffix(albuf->mAmbiOrder));
            albuf->mAmbiLayout = layout.value();
            return;
        }
        context->throw_error(AL_INVALID_VALUE, "Invalid unpack ambisonic layout {:#04x}",
            as_unsigned(value));

    case AL_AMBISONIC_SCALING_SOFT:
        if(albuf->mRef.load(std::memory_order_relaxed) != 0)
            context->throw_error(AL_INVALID_OPERATION,
                "Modifying in-use buffer {}'s ambisonic scaling", buffer);
        if(const auto scaling = AmbiScalingFromEnum(value))
        {
            if(scaling.value() == AmbiScaling::FuMa && albuf->mAmbiOrder > 3)
                context->throw_error(AL_INVALID_OPERATION,
                    "Cannot set FuMa scaling for {}{} order B-Format data", albuf->mAmbiOrder,
                    GetCounterSuffix(albuf->mAmbiOrder));
            albuf->mAmbiScaling = scaling.value();
            return;
        }
        context->throw_error(AL_INVALID_VALUE, "Invalid unpack ambisonic scaling {:#04x}",
            as_unsigned(value));

    case AL_UNPACK_AMBISONIC_ORDER_SOFT:
        if(value < 1 || value > 14)
            context->throw_error(AL_INVALID_VALUE, "Invalid unpack ambisonic order {}", value);
        albuf->mUnpackAmbiOrder = gsl::narrow_cast<u32>(value);
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid buffer integer property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alBuffer3i(gsl::not_null<al::Context*> const context, u32 const buffer, ALenum const param,
    i32 const value1 [[maybe_unused]], i32 const value2 [[maybe_unused]],
    i32 const value3 [[maybe_unused]]) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock [[maybe_unused]] = std::lock_guard{device->BufferLock};

    std::ignore = LookupBuffer(context, buffer);

    context->throw_error(AL_INVALID_ENUM, "Invalid buffer 3-integer property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alBufferiv(gsl::not_null<al::Context*> const context, u32 const buffer, ALenum const param,
    i32 const *const values) noexcept
try {
    if(!values)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    switch(param)
    {
    case AL_UNPACK_BLOCK_ALIGNMENT_SOFT:
    case AL_PACK_BLOCK_ALIGNMENT_SOFT:
    case AL_AMBISONIC_LAYOUT_SOFT:
    case AL_AMBISONIC_SCALING_SOFT:
    case AL_UNPACK_AMBISONIC_ORDER_SOFT:
        alBufferi(context, buffer, param, *values);
        return;
    }

    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock = std::lock_guard{device->BufferLock};

    auto const albuf = LookupBuffer(context, buffer);
    switch(param)
    {
    case AL_LOOP_POINTS_SOFT:
        const auto vals = std::span{values, 2_uz};
        if(albuf->mRef.load(std::memory_order_relaxed) != 0)
            context->throw_error(AL_INVALID_OPERATION, "Modifying in-use buffer {}'s loop points",
                buffer);
        if(vals[0] < 0 || vals[0] >= vals[1]
            || gsl::narrow_cast<u32>(vals[1]) > albuf->mSampleLen)
            context->throw_error(AL_INVALID_VALUE,
                "Invalid loop point range {} -> {} on buffer {}", vals[0], vals[1], buffer);

        albuf->mLoopStart = gsl::narrow_cast<u32>(vals[0]);
        albuf->mLoopEnd = gsl::narrow_cast<u32>(vals[1]);
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid buffer integer-vector property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void alGetBufferf(gsl::not_null<al::Context*> const context, u32 const buffer, ALenum const param,
    f32 *const value) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock = std::lock_guard{device->BufferLock};

    auto const albuf = LookupBuffer(context, buffer);
    if(!value)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    switch(param)
    {
    case AL_SEC_LENGTH_SOFT:
        *value = (albuf->mSampleRate < 1) ? 0.0_f32 :
            (gsl::narrow_cast<f32>(albuf->mSampleLen)/gsl::narrow_cast<f32>(albuf->mSampleRate));
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid buffer float property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetBuffer3f(gsl::not_null<al::Context*> const context, u32 const buffer, ALenum const param,
    f32 *const value1, f32 *const value2, f32 *const value3) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock [[maybe_unused]] = std::lock_guard{device->BufferLock};

    std::ignore = LookupBuffer(context, buffer);
    if(!value1 || !value2 || !value3)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    context->throw_error(AL_INVALID_ENUM, "Invalid buffer 3-float property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetBufferfv(gsl::not_null<al::Context*> const context, u32 const buffer, ALenum const param,
    f32 *const values) noexcept
try {
    switch(param)
    {
    case AL_SEC_LENGTH_SOFT:
        alGetBufferf(context, buffer, param, values);
        return;
    }

    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock [[maybe_unused]] = std::lock_guard{device->BufferLock};

    std::ignore = LookupBuffer(context, buffer);
    if(!values)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    context->throw_error(AL_INVALID_ENUM, "Invalid buffer float-vector property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void alGetBufferi(gsl::not_null<al::Context*> const context, u32 const buffer, ALenum const param,
    i32 *const value) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock = std::lock_guard{device->BufferLock};

    auto const albuf = LookupBuffer(context, buffer);
    if(!value)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    switch(param)
    {
    case AL_FREQUENCY:
        *value = gsl::narrow_cast<i32>(albuf->mSampleRate);
        return;

    case AL_BITS:
        *value = (albuf->mType == FmtIMA4 || albuf->mType == FmtMSADPCM) ? 4_i32
            : gsl::narrow_cast<i32>(albuf->bytesFromFmt() * 8_u32);
        return;

    case AL_CHANNELS:
        *value = gsl::narrow_cast<i32>(albuf->channelsFromFmt());
        return;

    case AL_SIZE:
        if(albuf->mCallback)
            *value = 0;
        else
            *value = std::visit([](auto &dataspan) -> i32
                { return gsl::narrow_cast<i32>(dataspan.size_bytes()); },
                albuf->mData);
        return;

    case AL_BYTE_LENGTH_SOFT:
        *value = gsl::narrow_cast<i32>(albuf->mSampleLen / albuf->mBlockAlign
            * albuf->blockSizeFromFmt());
        return;

    case AL_SAMPLE_LENGTH_SOFT:
        *value = gsl::narrow_cast<i32>(albuf->mSampleLen);
        return;

    case AL_UNPACK_BLOCK_ALIGNMENT_SOFT:
        *value = gsl::narrow_cast<i32>(albuf->mUnpackAlign);
        return;

    case AL_PACK_BLOCK_ALIGNMENT_SOFT:
        *value = gsl::narrow_cast<i32>(albuf->mPackAlign);
        return;

    case AL_AMBISONIC_LAYOUT_SOFT:
        *value = EnumFromAmbiLayout(albuf->mAmbiLayout);
        return;

    case AL_AMBISONIC_SCALING_SOFT:
        *value = EnumFromAmbiScaling(albuf->mAmbiScaling);
        return;

    case AL_UNPACK_AMBISONIC_ORDER_SOFT:
        *value = gsl::narrow_cast<i32>(albuf->mUnpackAmbiOrder);
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid buffer integer property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetBuffer3i(gsl::not_null<al::Context*> const context, u32 const buffer, ALenum const param,
    i32 *const value1, i32 *const value2, i32 *const value3) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock [[maybe_unused]] = std::lock_guard{device->BufferLock};

    std::ignore = LookupBuffer(context, buffer);
    if(!value1 || !value2 || !value3)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    context->throw_error(AL_INVALID_ENUM, "Invalid buffer 3-integer property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetBufferiv(gsl::not_null<al::Context*> const context, u32 const buffer, ALenum const param,
    i32 *const values) noexcept
try {
    switch(param)
    {
    case AL_FREQUENCY:
    case AL_BITS:
    case AL_CHANNELS:
    case AL_SIZE:
    case AL_INTERNAL_FORMAT_SOFT:
    case AL_BYTE_LENGTH_SOFT:
    case AL_SAMPLE_LENGTH_SOFT:
    case AL_UNPACK_BLOCK_ALIGNMENT_SOFT:
    case AL_PACK_BLOCK_ALIGNMENT_SOFT:
    case AL_AMBISONIC_LAYOUT_SOFT:
    case AL_AMBISONIC_SCALING_SOFT:
    case AL_UNPACK_AMBISONIC_ORDER_SOFT:
        alGetBufferi(context, buffer, param, values);
        return;
    }

    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock = std::lock_guard{device->BufferLock};

    auto const albuf = LookupBuffer(context, buffer);
    if(!values)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    switch(param)
    {
    case AL_LOOP_POINTS_SOFT:
        const auto vals = std::span{values, 2_uz};
        vals[0] = gsl::narrow_cast<i32>(albuf->mLoopStart);
        vals[1] = gsl::narrow_cast<i32>(albuf->mLoopEnd);
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid buffer integer-vector property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void alGetBufferPtrSOFT(gsl::not_null<al::Context*> const context, u32 const buffer,
    ALenum const param, void **const value) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock = std::lock_guard{device->BufferLock};

    auto const albuf = LookupBuffer(context, buffer);
    if(!value)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    switch(param)
    {
    case AL_BUFFER_CALLBACK_FUNCTION_SOFT:
        /* NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) */
        *value = reinterpret_cast<void*>(albuf->mCallback);
        return;
    case AL_BUFFER_CALLBACK_USER_PARAM_SOFT:
        *value = albuf->mUserData;
        return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid buffer pointer property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetBuffer3PtrSOFT(gsl::not_null<al::Context*> const context, u32 const buffer,
    ALenum const param, void **const value1, void **const value2, void **const value3) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock [[maybe_unused]] = std::lock_guard{device->BufferLock};

    std::ignore = LookupBuffer(context, buffer);
    if(!value1 || !value2 || !value3)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    context->throw_error(AL_INVALID_ENUM, "Invalid buffer 3-pointer property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetBufferPtrvSOFT(gsl::not_null<al::Context*> const context, u32 const buffer,
    ALenum const param, void **const values) noexcept
try {
    switch(param)
    {
    case AL_BUFFER_CALLBACK_FUNCTION_SOFT:
    case AL_BUFFER_CALLBACK_USER_PARAM_SOFT:
        alGetBufferPtrSOFT(context, buffer, param, values);
        return;
    }

    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock [[maybe_unused]] = std::lock_guard{device->BufferLock};

    std::ignore = LookupBuffer(context, buffer);
    if(!values)
        context->throw_error(AL_INVALID_VALUE, "NULL pointer");

    context->throw_error(AL_INVALID_ENUM, "Invalid buffer pointer-vector property {:#04x}",
        as_unsigned(param));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


#if ALSOFT_EAX
auto EAXSetBufferMode(gsl::not_null<al::Context*> const context, ALsizei const n,
    u32 const *const buffers, i32 const value) noexcept -> ALboolean
try {
    if(!eax_g_is_enabled)
        context->throw_error(AL_INVALID_OPERATION, "EAX not enabled");

    const auto storage = EaxStorageFromEnum(value);
    if(!storage)
        context->throw_error(AL_INVALID_ENUM, "Unsupported X-RAM mode {:#x}", as_unsigned(value));

    if(n == 0)
        return AL_TRUE;

    if(n < 0)
        context->throw_error(AL_INVALID_VALUE, "Buffer count {} out of range", n);
    if(!buffers)
        context->throw_error(AL_INVALID_VALUE, "Null AL buffers");

    auto const device = al::get_not_null(context->mALDevice);
    auto const devlock = std::lock_guard{device->BufferLock};

    /* Special-case setting a single buffer, to avoid extraneous allocations. */
    if(n == 1)
    {
        auto const bufid = *buffers;
        if(bufid == AL_NONE)
            return AL_TRUE;

        auto const buffer = LookupBuffer(context, bufid);

        /* TODO: Is the store location allowed to change for in-use buffers, or
         * only when not set/queued on a source?
         */

        if(*storage == EaxStorage::Hardware)
        {
            if(!buffer->mEaxXRamIsHardware
                && buffer->mOriginalSize > device->eax_x_ram_free_size)
                context->throw_error(AL_OUT_OF_MEMORY,
                    "Out of X-RAM memory (need: {}, avail: {})", buffer->mOriginalSize,
                    device->eax_x_ram_free_size);

            eax_x_ram_apply(*device, *buffer);
        }
        else
            eax_x_ram_clear(*device, *buffer);
        buffer->mEaxXRamMode = *storage;
        return AL_TRUE;
    }

    /* Validate the buffers. */
    auto buflist = std::unordered_set<gsl::not_null<ALbuffer*>>{};
    for(u32 const bufid : std::views::counted(buffers, n))
    {
        if(bufid == AL_NONE)
            continue;

        auto const buffer = LookupBuffer(context, bufid);

        /* TODO: Is the store location allowed to change for in-use buffers, or
         * only when not set/queued on a source?
         */

        buflist.emplace(buffer);
    }

    if(*storage == EaxStorage::Hardware)
    {
        auto total_needed = 0_uz;
        for(auto const &buffer : buflist)
        {
            if(!buffer->mEaxXRamIsHardware)
            {
                if(std::numeric_limits<usize>::max() - buffer->mOriginalSize < total_needed)
                    context->throw_error(AL_OUT_OF_MEMORY, "Size overflow ({} + {})",
                        buffer->mOriginalSize, total_needed);

                total_needed += buffer->mOriginalSize;
            }
        }
        if(total_needed > device->eax_x_ram_free_size)
            context->throw_error(AL_OUT_OF_MEMORY, "Out of X-RAM memory (need: {}, avail: {})",
                total_needed, device->eax_x_ram_free_size);
    }

    /* Update the mode. */
    for(auto const buffer : buflist)
    {
        if(*storage == EaxStorage::Hardware)
            eax_x_ram_apply(*device, *buffer);
        else
            eax_x_ram_clear(*device, *buffer);
        buffer->mEaxXRamMode = *storage;
    }

    return AL_TRUE;
}
catch(al::base_exception&) {
    return AL_FALSE;
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
    return AL_FALSE;
}

auto EAXGetBufferMode(gsl::not_null<al::Context*> const context, u32 const buffer,
    i32 *const pReserved) noexcept -> ALenum
try {
    if(!eax_g_is_enabled)
        context->throw_error(AL_INVALID_OPERATION, "EAX not enabled.");

    if(pReserved)
        context->throw_error(AL_INVALID_VALUE, "Non-null reserved parameter");

    auto const device = al::get_not_null(context->mALDevice);
    auto const devlock = std::lock_guard{device->BufferLock};

    auto const al_buffer = LookupBuffer(context, buffer);
    return EnumFromEaxStorage(al_buffer->mEaxXRamMode);
}
catch(al::base_exception&) {
    return AL_NONE;
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
    return AL_NONE;
}
#endif /* ALSOFT_EAX */

} // namespace

AL_API DECL_FUNC2(void, alGenBuffers, ALsizei,n, ALuint*,buffers)
AL_API DECL_FUNC2(void, alDeleteBuffers, ALsizei,n, const ALuint*,buffers)
AL_API DECL_FUNC1(ALboolean, alIsBuffer, ALuint,buffer)

AL_API DECL_FUNC5(void, alBufferData, ALuint,buffer, ALenum,format, const ALvoid*,data, ALsizei,size, ALsizei,freq)
AL_API DECL_FUNCEXT6(void, alBufferStorage,SOFT, ALuint,buffer, ALenum,format, const ALvoid*,data, ALsizei,size, ALsizei,freq, ALbitfieldSOFT,flags)
FORCE_ALIGN DECL_FUNC5(void, alBufferDataStatic, ALuint,buffer, ALenum,format, ALvoid*,data, ALsizei,size, ALsizei,freq)
AL_API DECL_FUNCEXT5(void, alBufferCallback,SOFT, ALuint,buffer, ALenum,format, ALsizei,freq, ALBUFFERCALLBACKTYPESOFT,callback, ALvoid*,userptr)
AL_API DECL_FUNCEXT5(void, alBufferSubData,SOFT, ALuint,buffer, ALenum,format, const ALvoid*,data, ALsizei,offset, ALsizei,length)

AL_API DECL_FUNCEXT4(void*, alMapBuffer,SOFT, ALuint,buffer, ALsizei,offset, ALsizei,length, ALbitfieldSOFT,access)
AL_API DECL_FUNCEXT1(void, alUnmapBuffer,SOFT, ALuint,buffer)
AL_API DECL_FUNCEXT3(void, alFlushMappedBuffer,SOFT, ALuint,buffer, ALsizei,offset, ALsizei,length)

AL_API DECL_FUNC3(void, alBufferf, ALuint,buffer, ALenum,param, ALfloat,value)
AL_API DECL_FUNC5(void, alBuffer3f, ALuint,buffer, ALenum,param, ALfloat,value1, ALfloat,value2, ALfloat,value3)
AL_API DECL_FUNC3(void, alBufferfv, ALuint,buffer, ALenum,param, const ALfloat*,values)

AL_API DECL_FUNC3(void, alBufferi, ALuint,buffer, ALenum,param, ALint,value)
AL_API DECL_FUNC5(void, alBuffer3i, ALuint,buffer, ALenum,param, ALint,value1, ALint,value2, ALint,value3)
AL_API DECL_FUNC3(void, alBufferiv, ALuint,buffer, ALenum,param, const ALint*,values)

AL_API DECL_FUNC3(void, alGetBufferf, ALuint,buffer, ALenum,param, ALfloat*,value)
AL_API DECL_FUNC5(void, alGetBuffer3f, ALuint,buffer, ALenum,param, ALfloat*,value1, ALfloat*,value2, ALfloat*,value3)
AL_API DECL_FUNC3(void, alGetBufferfv, ALuint,buffer, ALenum,param, ALfloat*,values)

AL_API DECL_FUNC3(void, alGetBufferi, ALuint,buffer, ALenum,param, ALint*,value)
AL_API DECL_FUNC5(void, alGetBuffer3i, ALuint,buffer, ALenum,param, ALint*,value1, ALint*,value2, ALint*,value3)
AL_API DECL_FUNC3(void, alGetBufferiv, ALuint,buffer, ALenum,param, ALint*,values)

AL_API DECL_FUNCEXT3(void, alGetBufferPtr,SOFT, ALuint,buffer, ALenum,param, ALvoid**,value)
AL_API DECL_FUNCEXT5(void, alGetBuffer3Ptr,SOFT, ALuint,buffer, ALenum,param, ALvoid**,value1, ALvoid**,value2, ALvoid**,value3)
AL_API DECL_FUNCEXT3(void, alGetBufferPtrv,SOFT, ALuint,buffer, ALenum,param, ALvoid**,values)

#if ALSOFT_EAX
FORCE_ALIGN DECL_FUNC3(ALboolean, EAXSetBufferMode, ALsizei,n, const ALuint*,buffers, ALint,value)
FORCE_ALIGN DECL_FUNC2(ALenum, EAXGetBufferMode, ALuint,buffer, ALint*,pReserved)
#endif // ALSOFT_EAX


AL_API void AL_APIENTRY alBufferSamplesSOFT(ALuint /*buffer*/, ALuint /*samplerate*/,
    ALenum /*internalformat*/, ALsizei /*samples*/, ALenum /*channels*/, ALenum /*type*/,
    const ALvoid* /*data*/) noexcept
{
    const auto context = GetContextRef();
    if(!context) [[unlikely]] return;

    context->setError(AL_INVALID_OPERATION, "alBufferSamplesSOFT not supported");
}

AL_API void AL_APIENTRY alBufferSubSamplesSOFT(ALuint /*buffer*/, ALsizei /*offset*/,
    ALsizei /*samples*/, ALenum /*channels*/, ALenum /*type*/, const ALvoid* /*data*/) noexcept
{
    const auto context = GetContextRef();
    if(!context) [[unlikely]] return;

    context->setError(AL_INVALID_OPERATION, "alBufferSubSamplesSOFT not supported");
}

AL_API void AL_APIENTRY alGetBufferSamplesSOFT(ALuint /*buffer*/, ALsizei /*offset*/,
    ALsizei /*samples*/, ALenum /*channels*/, ALenum /*type*/, ALvoid* /*data*/) noexcept
{
    const auto context = GetContextRef();
    if(!context) [[unlikely]] return;

    context->setError(AL_INVALID_OPERATION, "alGetBufferSamplesSOFT not supported");
}

AL_API auto AL_APIENTRY alIsBufferFormatSupportedSOFT(ALenum /*format*/) noexcept -> ALboolean
{
    const auto context = GetContextRef();
    if(!context) [[unlikely]] return AL_FALSE;

    context->setError(AL_INVALID_OPERATION, "alIsBufferFormatSupportedSOFT not supported");
    return AL_FALSE;
}


void ALbuffer::SetName(gsl::not_null<al::Context*> context, u32 id, std::string_view name)
{
    auto const device = al::get_not_null(context->mALDevice);
    auto const buflock = std::lock_guard{device->BufferLock};

    std::ignore = LookupBuffer(context, id);
    device->mBufferNames.insert_or_assign(id, name);
}


BufferSubList::~BufferSubList()
{
    if(!mBuffers)
        return;

    auto usemask = ~mFreeMask;
    while(usemask)
    {
        auto const idx = std::countr_zero(usemask);
        std::destroy_at(std::to_address(mBuffers->begin() + idx));
        usemask &= ~(1_u64 << idx);
    }
    mFreeMask = ~usemask;
    SubListAllocator{}.deallocate(mBuffers, 1);
    mBuffers = nullptr;
}
