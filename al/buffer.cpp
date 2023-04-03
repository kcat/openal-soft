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
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <stdexcept>
#include <utility>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "albit.h"
#include "albyte.h"
#include "alc/context.h"
#include "alc/device.h"
#include "alc/inprogext.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "aloptional.h"
#include "atomic.h"
#include "core/except.h"
#include "core/logging.h"
#include "core/voice.h"
#include "opthelpers.h"

#ifdef ALSOFT_EAX
#include "eax/globals.h"
#include "eax/x_ram.h"
#endif // ALSOFT_EAX


namespace {

al::optional<AmbiLayout> AmbiLayoutFromEnum(ALenum layout)
{
    switch(layout)
    {
    case AL_FUMA_SOFT: return AmbiLayout::FuMa;
    case AL_ACN_SOFT: return AmbiLayout::ACN;
    }
    return al::nullopt;
}
ALenum EnumFromAmbiLayout(AmbiLayout layout)
{
    switch(layout)
    {
    case AmbiLayout::FuMa: return AL_FUMA_SOFT;
    case AmbiLayout::ACN: return AL_ACN_SOFT;
    }
    throw std::runtime_error{"Invalid AmbiLayout: "+std::to_string(int(layout))};
}

al::optional<AmbiScaling> AmbiScalingFromEnum(ALenum scale)
{
    switch(scale)
    {
    case AL_FUMA_SOFT: return AmbiScaling::FuMa;
    case AL_SN3D_SOFT: return AmbiScaling::SN3D;
    case AL_N3D_SOFT: return AmbiScaling::N3D;
    }
    return al::nullopt;
}
ALenum EnumFromAmbiScaling(AmbiScaling scale)
{
    switch(scale)
    {
    case AmbiScaling::FuMa: return AL_FUMA_SOFT;
    case AmbiScaling::SN3D: return AL_SN3D_SOFT;
    case AmbiScaling::N3D: return AL_N3D_SOFT;
    case AmbiScaling::UHJ: break;
    }
    throw std::runtime_error{"Invalid AmbiScaling: "+std::to_string(int(scale))};
}

#ifdef ALSOFT_EAX
al::optional<EaxStorage> EaxStorageFromEnum(ALenum scale)
{
    switch(scale)
    {
    case AL_STORAGE_AUTOMATIC: return EaxStorage::Automatic;
    case AL_STORAGE_ACCESSIBLE: return EaxStorage::Accessible;
    case AL_STORAGE_HARDWARE: return EaxStorage::Hardware;
    }
    return al::nullopt;
}
ALenum EnumFromEaxStorage(EaxStorage storage)
{
    switch(storage)
    {
    case EaxStorage::Automatic: return AL_STORAGE_AUTOMATIC;
    case EaxStorage::Accessible: return AL_STORAGE_ACCESSIBLE;
    case EaxStorage::Hardware: return AL_STORAGE_HARDWARE;
    }
    throw std::runtime_error{"Invalid EaxStorage: "+std::to_string(int(storage))};
}


bool eax_x_ram_check_availability(const ALCdevice &device, const ALbuffer &buffer,
    const ALuint newsize) noexcept
{
    ALuint freemem{device.eax_x_ram_free_size};
    /* If the buffer is currently in "hardware", add its memory to the free
     * pool since it'll be "replaced".
     */
    if(buffer.eax_x_ram_is_hardware)
        freemem += buffer.OriginalSize;
    return freemem >= newsize;
}

void eax_x_ram_apply(ALCdevice &device, ALbuffer &buffer) noexcept
{
    if(buffer.eax_x_ram_is_hardware)
        return;

    if(device.eax_x_ram_free_size >= buffer.OriginalSize)
    {
        device.eax_x_ram_free_size -= buffer.OriginalSize;
        buffer.eax_x_ram_is_hardware = true;
    }
}

void eax_x_ram_clear(ALCdevice& al_device, ALbuffer& al_buffer)
{
    if(al_buffer.eax_x_ram_is_hardware)
        al_device.eax_x_ram_free_size += al_buffer.OriginalSize;
    al_buffer.eax_x_ram_is_hardware = false;
}
#endif // ALSOFT_EAX


constexpr ALbitfieldSOFT INVALID_STORAGE_MASK{~unsigned(AL_MAP_READ_BIT_SOFT |
    AL_MAP_WRITE_BIT_SOFT | AL_MAP_PERSISTENT_BIT_SOFT | AL_PRESERVE_DATA_BIT_SOFT)};
constexpr ALbitfieldSOFT MAP_READ_WRITE_FLAGS{AL_MAP_READ_BIT_SOFT | AL_MAP_WRITE_BIT_SOFT};
constexpr ALbitfieldSOFT INVALID_MAP_FLAGS{~unsigned(AL_MAP_READ_BIT_SOFT | AL_MAP_WRITE_BIT_SOFT |
    AL_MAP_PERSISTENT_BIT_SOFT)};


bool EnsureBuffers(ALCdevice *device, size_t needed)
{
    size_t count{std::accumulate(device->BufferList.cbegin(), device->BufferList.cend(), size_t{0},
        [](size_t cur, const BufferSubList &sublist) noexcept -> size_t
        { return cur + static_cast<ALuint>(al::popcount(sublist.FreeMask)); })};

    while(needed > count)
    {
        if(device->BufferList.size() >= 1<<25) UNLIKELY
            return false;

        device->BufferList.emplace_back();
        auto sublist = device->BufferList.end() - 1;
        sublist->FreeMask = ~0_u64;
        sublist->Buffers = static_cast<ALbuffer*>(al_calloc(alignof(ALbuffer), sizeof(ALbuffer)*64));
        if(!sublist->Buffers) UNLIKELY
        {
            device->BufferList.pop_back();
            return false;
        }
        count += 64;
    }
    return true;
}

ALbuffer *AllocBuffer(ALCdevice *device)
{
    auto sublist = std::find_if(device->BufferList.begin(), device->BufferList.end(),
        [](const BufferSubList &entry) noexcept -> bool
        { return entry.FreeMask != 0; });
    auto lidx = static_cast<ALuint>(std::distance(device->BufferList.begin(), sublist));
    auto slidx = static_cast<ALuint>(al::countr_zero(sublist->FreeMask));
    ASSUME(slidx < 64);

    ALbuffer *buffer{al::construct_at(sublist->Buffers + slidx)};

    /* Add 1 to avoid buffer ID 0. */
    buffer->id = ((lidx<<6) | slidx) + 1;

    sublist->FreeMask &= ~(1_u64 << slidx);

    return buffer;
}

void FreeBuffer(ALCdevice *device, ALbuffer *buffer)
{
#ifdef ALSOFT_EAX
    eax_x_ram_clear(*device, *buffer);
#endif // ALSOFT_EAX

    const ALuint id{buffer->id - 1};
    const size_t lidx{id >> 6};
    const ALuint slidx{id & 0x3f};

    al::destroy_at(buffer);

    device->BufferList[lidx].FreeMask |= 1_u64 << slidx;
}

inline ALbuffer *LookupBuffer(ALCdevice *device, ALuint id)
{
    const size_t lidx{(id-1) >> 6};
    const ALuint slidx{(id-1) & 0x3f};

    if(lidx >= device->BufferList.size()) UNLIKELY
        return nullptr;
    BufferSubList &sublist = device->BufferList[lidx];
    if(sublist.FreeMask & (1_u64 << slidx)) UNLIKELY
        return nullptr;
    return sublist.Buffers + slidx;
}


ALuint SanitizeAlignment(FmtType type, ALuint align)
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
        if((align&7) == 1) return static_cast<ALuint>(align);
        return 0;
    }
    if(type == FmtMSADPCM)
    {
        /* MSADPCM block alignment must be a multiple of 2. */
        if((align&1) == 0) return static_cast<ALuint>(align);
        return 0;
    }

    return static_cast<ALuint>(align);
}


/** Loads the specified data into the buffer, using the specified format. */
void LoadData(ALCcontext *context, ALbuffer *ALBuf, ALsizei freq, ALuint size,
    const FmtChannels DstChannels, const FmtType DstType, const al::byte *SrcData,
    ALbitfieldSOFT access)
{
    if(ReadRef(ALBuf->ref) != 0 || ALBuf->MappedAccess != 0) UNLIKELY
        return context->setError(AL_INVALID_OPERATION, "Modifying storage for in-use buffer %u",
            ALBuf->id);

    const ALuint unpackalign{ALBuf->UnpackAlign};
    const ALuint align{SanitizeAlignment(DstType, unpackalign)};
    if(align < 1) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "Invalid unpack alignment %u for %s samples",
            unpackalign, NameFromFormat(DstType));

    const ALuint ambiorder{IsBFormat(DstChannels) ? ALBuf->UnpackAmbiOrder :
        (IsUHJ(DstChannels) ? 1 : 0)};

    if((access&AL_PRESERVE_DATA_BIT_SOFT))
    {
        /* Can only preserve data with the same format and alignment. */
        if(ALBuf->mChannels != DstChannels || ALBuf->mType != DstType) UNLIKELY
            return context->setError(AL_INVALID_VALUE, "Preserving data of mismatched format");
        if(ALBuf->mBlockAlign != align) UNLIKELY
            return context->setError(AL_INVALID_VALUE, "Preserving data of mismatched alignment");
        if(ALBuf->mAmbiOrder != ambiorder) UNLIKELY
            return context->setError(AL_INVALID_VALUE, "Preserving data of mismatched order");
    }

    /* Convert the size in bytes to blocks using the unpack block alignment. */
    const ALuint NumChannels{ChannelsFromFmt(DstChannels, ambiorder)};
    const ALuint BlockSize{NumChannels *
        ((DstType == FmtIMA4) ? (align-1)/2 + 4 :
        (DstType == FmtMSADPCM) ? (align-2)/2 + 7 :
        (align * BytesFromFmt(DstType)))};
    if((size%BlockSize) != 0) UNLIKELY
        return context->setError(AL_INVALID_VALUE,
            "Data size %d is not a multiple of frame size %d (%d unpack alignment)",
            size, BlockSize, align);
    const ALuint blocks{size / BlockSize};

    if(blocks > std::numeric_limits<ALsizei>::max()/align) UNLIKELY
        return context->setError(AL_OUT_OF_MEMORY,
            "Buffer size overflow, %d blocks x %d samples per block", blocks, align);
    if(blocks > std::numeric_limits<size_t>::max()/BlockSize) UNLIKELY
        return context->setError(AL_OUT_OF_MEMORY,
            "Buffer size overflow, %d frames x %d bytes per frame", blocks, BlockSize);

    const size_t newsize{static_cast<size_t>(blocks) * BlockSize};

#ifdef ALSOFT_EAX
    if(ALBuf->eax_x_ram_mode == EaxStorage::Hardware)
    {
        ALCdevice &device = *context->mALDevice;
        if(!eax_x_ram_check_availability(device, *ALBuf, size))
            return context->setError(AL_OUT_OF_MEMORY,
                "Out of X-RAM memory (avail: %u, needed: %u)", device.eax_x_ram_free_size, size);
    }
#endif

    /* This could reallocate only when increasing the size or the new size is
     * less than half the current, but then the buffer's AL_SIZE would not be
     * very reliable for accounting buffer memory usage, and reporting the real
     * size could cause problems for apps that use AL_SIZE to try to get the
     * buffer's play length.
     */
    if(newsize != ALBuf->mDataStorage.size())
    {
        auto newdata = al::vector<al::byte,16>(newsize, al::byte{});
        if((access&AL_PRESERVE_DATA_BIT_SOFT))
        {
            const size_t tocopy{minz(newdata.size(), ALBuf->mDataStorage.size())};
            std::copy_n(ALBuf->mDataStorage.begin(), tocopy, newdata.begin());
        }
        newdata.swap(ALBuf->mDataStorage);
    }
    ALBuf->mData = ALBuf->mDataStorage;
#ifdef ALSOFT_EAX
    eax_x_ram_clear(*context->mALDevice, *ALBuf);
#endif

    if(SrcData != nullptr && !ALBuf->mData.empty())
        std::copy_n(SrcData, blocks*BlockSize, ALBuf->mData.begin());
    ALBuf->mBlockAlign = (DstType == FmtIMA4 || DstType == FmtMSADPCM) ? align : 1;

    ALBuf->OriginalSize = size;

    ALBuf->Access = access;

    ALBuf->mSampleRate = static_cast<ALuint>(freq);
    ALBuf->mChannels = DstChannels;
    ALBuf->mType = DstType;
    ALBuf->mAmbiOrder = ambiorder;

    ALBuf->mCallback = nullptr;
    ALBuf->mUserData = nullptr;

    ALBuf->mSampleLen = blocks * align;
    ALBuf->mLoopStart = 0;
    ALBuf->mLoopEnd = ALBuf->mSampleLen;

#ifdef ALSOFT_EAX
    if(eax_g_is_enabled && ALBuf->eax_x_ram_mode == EaxStorage::Hardware)
        eax_x_ram_apply(*context->mALDevice, *ALBuf);
#endif
}

/** Prepares the buffer to use the specified callback, using the specified format. */
void PrepareCallback(ALCcontext *context, ALbuffer *ALBuf, ALsizei freq,
    const FmtChannels DstChannels, const FmtType DstType, ALBUFFERCALLBACKTYPESOFT callback,
    void *userptr)
{
    if(ReadRef(ALBuf->ref) != 0 || ALBuf->MappedAccess != 0) UNLIKELY
        return context->setError(AL_INVALID_OPERATION, "Modifying callback for in-use buffer %u",
            ALBuf->id);

    const ALuint ambiorder{IsBFormat(DstChannels) ? ALBuf->UnpackAmbiOrder :
        (IsUHJ(DstChannels) ? 1 : 0)};

    const ALuint unpackalign{ALBuf->UnpackAlign};
    const ALuint align{SanitizeAlignment(DstType, unpackalign)};
    const ALuint BlockSize{ChannelsFromFmt(DstChannels, ambiorder) *
        ((DstType == FmtIMA4) ? (align-1)/2 + 4 :
        (DstType == FmtMSADPCM) ? (align-2)/2 + 7 :
        (align * BytesFromFmt(DstType)))};

    /* The maximum number of samples a callback buffer may need to store is a
     * full mixing line * max pitch * channel count, since it may need to hold
     * a full line's worth of sample frames before downsampling. An additional
     * MaxResamplerEdge is needed for "future" samples during resampling (the
     * voice will hold a history for the past samples).
     */
    static constexpr size_t line_size{DeviceBase::MixerLineSize*MaxPitch + MaxResamplerEdge};
    const size_t line_blocks{(line_size + align-1) / align};

    using BufferVectorType = decltype(ALBuf->mDataStorage);
    BufferVectorType(line_blocks*BlockSize).swap(ALBuf->mDataStorage);
    ALBuf->mData = ALBuf->mDataStorage;

#ifdef ALSOFT_EAX
    eax_x_ram_clear(*context->mALDevice, *ALBuf);
#endif

    ALBuf->mCallback = callback;
    ALBuf->mUserData = userptr;

    ALBuf->OriginalSize = 0;
    ALBuf->Access = 0;

    ALBuf->mBlockAlign = (DstType == FmtIMA4 || DstType == FmtMSADPCM) ? align : 1;
    ALBuf->mSampleRate = static_cast<ALuint>(freq);
    ALBuf->mChannels = DstChannels;
    ALBuf->mType = DstType;
    ALBuf->mAmbiOrder = ambiorder;

    ALBuf->mSampleLen = 0;
    ALBuf->mLoopStart = 0;
    ALBuf->mLoopEnd = ALBuf->mSampleLen;
}

/** Prepares the buffer to use caller-specified storage. */
void PrepareUserPtr(ALCcontext *context, ALbuffer *ALBuf, ALsizei freq,
    const FmtChannels DstChannels, const FmtType DstType, al::byte *sdata, const ALuint sdatalen)
{
    if(ReadRef(ALBuf->ref) != 0 || ALBuf->MappedAccess != 0) UNLIKELY
        return context->setError(AL_INVALID_OPERATION, "Modifying storage for in-use buffer %u",
            ALBuf->id);

    const ALuint unpackalign{ALBuf->UnpackAlign};
    const ALuint align{SanitizeAlignment(DstType, unpackalign)};
    if(align < 1) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "Invalid unpack alignment %u for %s samples",
            unpackalign, NameFromFormat(DstType));

    auto get_type_alignment = [](const FmtType type) noexcept -> ALuint
    {
        /* NOTE: This only needs to be the required alignment for the CPU to
         * read/write the given sample type in the mixer.
         */
        switch(type)
        {
        case FmtUByte: return alignof(ALubyte);
        case FmtShort: return alignof(ALshort);
        case FmtFloat: return alignof(ALfloat);
        case FmtDouble: return alignof(ALdouble);
        case FmtMulaw: return alignof(ALubyte);
        case FmtAlaw: return alignof(ALubyte);
        case FmtIMA4: break;
        case FmtMSADPCM: break;
        }
        return 1;
    };
    const auto typealign = get_type_alignment(DstType);
    if((reinterpret_cast<uintptr_t>(sdata) & (typealign-1)) != 0)
        return context->setError(AL_INVALID_VALUE, "Pointer %p is misaligned for %s samples (%u)",
            static_cast<void*>(sdata), NameFromFormat(DstType), typealign);

    const ALuint ambiorder{IsBFormat(DstChannels) ? ALBuf->UnpackAmbiOrder :
        (IsUHJ(DstChannels) ? 1 : 0)};

    /* Convert the size in bytes to blocks using the unpack block alignment. */
    const ALuint NumChannels{ChannelsFromFmt(DstChannels, ambiorder)};
    const ALuint BlockSize{NumChannels *
        ((DstType == FmtIMA4) ? (align-1)/2 + 4 :
        (DstType == FmtMSADPCM) ? (align-2)/2 + 7 :
        (align * BytesFromFmt(DstType)))};
    if((sdatalen%BlockSize) != 0) UNLIKELY
        return context->setError(AL_INVALID_VALUE,
            "Data size %u is not a multiple of frame size %u (%u unpack alignment)",
            sdatalen, BlockSize, align);
    const ALuint blocks{sdatalen / BlockSize};

    if(blocks > std::numeric_limits<ALsizei>::max()/align) UNLIKELY
        return context->setError(AL_OUT_OF_MEMORY,
            "Buffer size overflow, %d blocks x %d samples per block", blocks, align);
    if(blocks > std::numeric_limits<size_t>::max()/BlockSize) UNLIKELY
        return context->setError(AL_OUT_OF_MEMORY,
            "Buffer size overflow, %d frames x %d bytes per frame", blocks, BlockSize);

#ifdef ALSOFT_EAX
    if(ALBuf->eax_x_ram_mode == EaxStorage::Hardware)
    {
        ALCdevice &device = *context->mALDevice;
        if(!eax_x_ram_check_availability(device, *ALBuf, sdatalen))
            return context->setError(AL_OUT_OF_MEMORY,
                "Out of X-RAM memory (avail: %u, needed: %u)", device.eax_x_ram_free_size,
                sdatalen);
    }
#endif

    decltype(ALBuf->mDataStorage){}.swap(ALBuf->mDataStorage);
    ALBuf->mData = {static_cast<al::byte*>(sdata), sdatalen};

#ifdef ALSOFT_EAX
    eax_x_ram_clear(*context->mALDevice, *ALBuf);
#endif

    ALBuf->mCallback = nullptr;
    ALBuf->mUserData = nullptr;

    ALBuf->OriginalSize = sdatalen;
    ALBuf->Access = 0;

    ALBuf->mBlockAlign = (DstType == FmtIMA4 || DstType == FmtMSADPCM) ? align : 1;
    ALBuf->mSampleRate = static_cast<ALuint>(freq);
    ALBuf->mChannels = DstChannels;
    ALBuf->mType = DstType;
    ALBuf->mAmbiOrder = ambiorder;

    ALBuf->mSampleLen = blocks * align;
    ALBuf->mLoopStart = 0;
    ALBuf->mLoopEnd = ALBuf->mSampleLen;

#ifdef ALSOFT_EAX
    if(ALBuf->eax_x_ram_mode == EaxStorage::Hardware)
        eax_x_ram_apply(*context->mALDevice, *ALBuf);
#endif
}


struct DecompResult { FmtChannels channels; FmtType type; };
al::optional<DecompResult> DecomposeUserFormat(ALenum format)
{
    struct FormatMap {
        ALenum format;
        FmtChannels channels;
        FmtType type;
    };
    static const std::array<FormatMap,63> UserFmtList{{
        { AL_FORMAT_MONO8,             FmtMono, FmtUByte   },
        { AL_FORMAT_MONO16,            FmtMono, FmtShort   },
        { AL_FORMAT_MONO_FLOAT32,      FmtMono, FmtFloat   },
        { AL_FORMAT_MONO_DOUBLE_EXT,   FmtMono, FmtDouble  },
        { AL_FORMAT_MONO_IMA4,         FmtMono, FmtIMA4    },
        { AL_FORMAT_MONO_MSADPCM_SOFT, FmtMono, FmtMSADPCM },
        { AL_FORMAT_MONO_MULAW,        FmtMono, FmtMulaw   },
        { AL_FORMAT_MONO_ALAW_EXT,     FmtMono, FmtAlaw    },

        { AL_FORMAT_STEREO8,             FmtStereo, FmtUByte   },
        { AL_FORMAT_STEREO16,            FmtStereo, FmtShort   },
        { AL_FORMAT_STEREO_FLOAT32,      FmtStereo, FmtFloat   },
        { AL_FORMAT_STEREO_DOUBLE_EXT,   FmtStereo, FmtDouble  },
        { AL_FORMAT_STEREO_IMA4,         FmtStereo, FmtIMA4    },
        { AL_FORMAT_STEREO_MSADPCM_SOFT, FmtStereo, FmtMSADPCM },
        { AL_FORMAT_STEREO_MULAW,        FmtStereo, FmtMulaw   },
        { AL_FORMAT_STEREO_ALAW_EXT,     FmtStereo, FmtAlaw    },

        { AL_FORMAT_REAR8,      FmtRear, FmtUByte },
        { AL_FORMAT_REAR16,     FmtRear, FmtShort },
        { AL_FORMAT_REAR32,     FmtRear, FmtFloat },
        { AL_FORMAT_REAR_MULAW, FmtRear, FmtMulaw },

        { AL_FORMAT_QUAD8_LOKI,  FmtQuad, FmtUByte },
        { AL_FORMAT_QUAD16_LOKI, FmtQuad, FmtShort },

        { AL_FORMAT_QUAD8,      FmtQuad, FmtUByte },
        { AL_FORMAT_QUAD16,     FmtQuad, FmtShort },
        { AL_FORMAT_QUAD32,     FmtQuad, FmtFloat },
        { AL_FORMAT_QUAD_MULAW, FmtQuad, FmtMulaw },

        { AL_FORMAT_51CHN8,      FmtX51, FmtUByte },
        { AL_FORMAT_51CHN16,     FmtX51, FmtShort },
        { AL_FORMAT_51CHN32,     FmtX51, FmtFloat },
        { AL_FORMAT_51CHN_MULAW, FmtX51, FmtMulaw },

        { AL_FORMAT_61CHN8,      FmtX61, FmtUByte },
        { AL_FORMAT_61CHN16,     FmtX61, FmtShort },
        { AL_FORMAT_61CHN32,     FmtX61, FmtFloat },
        { AL_FORMAT_61CHN_MULAW, FmtX61, FmtMulaw },

        { AL_FORMAT_71CHN8,      FmtX71, FmtUByte },
        { AL_FORMAT_71CHN16,     FmtX71, FmtShort },
        { AL_FORMAT_71CHN32,     FmtX71, FmtFloat },
        { AL_FORMAT_71CHN_MULAW, FmtX71, FmtMulaw },

        { AL_FORMAT_BFORMAT2D_8,       FmtBFormat2D, FmtUByte },
        { AL_FORMAT_BFORMAT2D_16,      FmtBFormat2D, FmtShort },
        { AL_FORMAT_BFORMAT2D_FLOAT32, FmtBFormat2D, FmtFloat },
        { AL_FORMAT_BFORMAT2D_MULAW,   FmtBFormat2D, FmtMulaw },

        { AL_FORMAT_BFORMAT3D_8,       FmtBFormat3D, FmtUByte },
        { AL_FORMAT_BFORMAT3D_16,      FmtBFormat3D, FmtShort },
        { AL_FORMAT_BFORMAT3D_FLOAT32, FmtBFormat3D, FmtFloat },
        { AL_FORMAT_BFORMAT3D_MULAW,   FmtBFormat3D, FmtMulaw },

        { AL_FORMAT_UHJ2CHN8_SOFT,        FmtUHJ2, FmtUByte   },
        { AL_FORMAT_UHJ2CHN16_SOFT,       FmtUHJ2, FmtShort   },
        { AL_FORMAT_UHJ2CHN_FLOAT32_SOFT, FmtUHJ2, FmtFloat   },
        { AL_FORMAT_UHJ2CHN_MULAW_SOFT,   FmtUHJ2, FmtMulaw   },
        { AL_FORMAT_UHJ2CHN_ALAW_SOFT,    FmtUHJ2, FmtAlaw    },
        { AL_FORMAT_UHJ2CHN_IMA4_SOFT,    FmtUHJ2, FmtIMA4    },
        { AL_FORMAT_UHJ2CHN_MSADPCM_SOFT, FmtUHJ2, FmtMSADPCM },

        { AL_FORMAT_UHJ3CHN8_SOFT,        FmtUHJ3, FmtUByte },
        { AL_FORMAT_UHJ3CHN16_SOFT,       FmtUHJ3, FmtShort },
        { AL_FORMAT_UHJ3CHN_FLOAT32_SOFT, FmtUHJ3, FmtFloat },
        { AL_FORMAT_UHJ3CHN_MULAW_SOFT,   FmtUHJ3, FmtMulaw },
        { AL_FORMAT_UHJ3CHN_ALAW_SOFT,    FmtUHJ3, FmtAlaw  },

        { AL_FORMAT_UHJ4CHN8_SOFT,        FmtUHJ4, FmtUByte },
        { AL_FORMAT_UHJ4CHN16_SOFT,       FmtUHJ4, FmtShort },
        { AL_FORMAT_UHJ4CHN_FLOAT32_SOFT, FmtUHJ4, FmtFloat },
        { AL_FORMAT_UHJ4CHN_MULAW_SOFT,   FmtUHJ4, FmtMulaw },
        { AL_FORMAT_UHJ4CHN_ALAW_SOFT,    FmtUHJ4, FmtAlaw  },
    }};

    for(const auto &fmt : UserFmtList)
    {
        if(fmt.format == format)
            return al::make_optional<DecompResult>({fmt.channels, fmt.type});
    }
    return al::nullopt;
}

} // namespace


AL_API void AL_APIENTRY alGenBuffers(ALsizei n, ALuint *buffers)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(n < 0) UNLIKELY
        context->setError(AL_INVALID_VALUE, "Generating %d buffers", n);
    if(n <= 0) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};
    if(!EnsureBuffers(device, static_cast<ALuint>(n)))
    {
        context->setError(AL_OUT_OF_MEMORY, "Failed to allocate %d buffer%s", n, (n==1)?"":"s");
        return;
    }

    if(n == 1) LIKELY
    {
        /* Special handling for the easy and normal case. */
        ALbuffer *buffer{AllocBuffer(device)};
        buffers[0] = buffer->id;
    }
    else
    {
        /* Store the allocated buffer IDs in a separate local list, to avoid
         * modifying the user storage in case of failure.
         */
        al::vector<ALuint> ids;
        ids.reserve(static_cast<ALuint>(n));
        do {
            ALbuffer *buffer{AllocBuffer(device)};
            ids.emplace_back(buffer->id);
        } while(--n);
        std::copy(ids.begin(), ids.end(), buffers);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alDeleteBuffers(ALsizei n, const ALuint *buffers)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(n < 0) UNLIKELY
        context->setError(AL_INVALID_VALUE, "Deleting %d buffers", n);
    if(n <= 0) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    /* First try to find any buffers that are invalid or in-use. */
    auto validate_buffer = [device, &context](const ALuint bid) -> bool
    {
        if(!bid) return true;
        ALbuffer *ALBuf{LookupBuffer(device, bid)};
        if(!ALBuf) UNLIKELY
        {
            context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", bid);
            return false;
        }
        if(ReadRef(ALBuf->ref) != 0) UNLIKELY
        {
            context->setError(AL_INVALID_OPERATION, "Deleting in-use buffer %u", bid);
            return false;
        }
        return true;
    };
    const ALuint *buffers_end = buffers + n;
    auto invbuf = std::find_if_not(buffers, buffers_end, validate_buffer);
    if(invbuf != buffers_end) UNLIKELY return;

    /* All good. Delete non-0 buffer IDs. */
    auto delete_buffer = [device](const ALuint bid) -> void
    {
        ALbuffer *buffer{bid ? LookupBuffer(device, bid) : nullptr};
        if(buffer) FreeBuffer(device, buffer);
    };
    std::for_each(buffers, buffers_end, delete_buffer);
}
END_API_FUNC

AL_API ALboolean AL_APIENTRY alIsBuffer(ALuint buffer)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(context) LIKELY
    {
        ALCdevice *device{context->mALDevice.get()};
        std::lock_guard<std::mutex> _{device->BufferLock};
        if(!buffer || LookupBuffer(device, buffer))
            return AL_TRUE;
    }
    return AL_FALSE;
}
END_API_FUNC


AL_API void AL_APIENTRY alBufferData(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq)
START_API_FUNC
{ alBufferStorageSOFT(buffer, format, data, size, freq, 0); }
END_API_FUNC

AL_API void AL_APIENTRY alBufferStorageSOFT(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq, ALbitfieldSOFT flags)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    ALbuffer *albuf = LookupBuffer(device, buffer);
    if(!albuf) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if(size < 0) UNLIKELY
        context->setError(AL_INVALID_VALUE, "Negative storage size %d", size);
    else if(freq < 1) UNLIKELY
        context->setError(AL_INVALID_VALUE, "Invalid sample rate %d", freq);
    else if((flags&INVALID_STORAGE_MASK) != 0) UNLIKELY
        context->setError(AL_INVALID_VALUE, "Invalid storage flags 0x%x",
            flags&INVALID_STORAGE_MASK);
    else if((flags&AL_MAP_PERSISTENT_BIT_SOFT) && !(flags&MAP_READ_WRITE_FLAGS)) UNLIKELY
        context->setError(AL_INVALID_VALUE,
            "Declaring persistently mapped storage without read or write access");
    else
    {
        auto usrfmt = DecomposeUserFormat(format);
        if(!usrfmt) UNLIKELY
            context->setError(AL_INVALID_ENUM, "Invalid format 0x%04x", format);
        else
        {
            LoadData(context.get(), albuf, freq, static_cast<ALuint>(size), usrfmt->channels,
                usrfmt->type, static_cast<const al::byte*>(data), flags);
        }
    }
}
END_API_FUNC

void AL_APIENTRY alBufferDataStatic(const ALuint buffer, ALenum format, ALvoid *data, ALsizei size,
    ALsizei freq)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    ALbuffer *albuf = LookupBuffer(device, buffer);
    if(!albuf) UNLIKELY
        return context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    if(size < 0) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "Negative storage size %d", size);
    if(freq < 1) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "Invalid sample rate %d", freq);

    auto usrfmt = DecomposeUserFormat(format);
    if(!usrfmt) UNLIKELY
        return context->setError(AL_INVALID_ENUM, "Invalid format 0x%04x", format);

    PrepareUserPtr(context.get(), albuf, freq, usrfmt->channels, usrfmt->type,
        static_cast<al::byte*>(data), static_cast<ALuint>(size));
}
END_API_FUNC

AL_API void* AL_APIENTRY alMapBufferSOFT(ALuint buffer, ALsizei offset, ALsizei length, ALbitfieldSOFT access)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return nullptr;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    ALbuffer *albuf = LookupBuffer(device, buffer);
    if(!albuf) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if((access&INVALID_MAP_FLAGS) != 0) UNLIKELY
        context->setError(AL_INVALID_VALUE, "Invalid map flags 0x%x", access&INVALID_MAP_FLAGS);
    else if(!(access&MAP_READ_WRITE_FLAGS)) UNLIKELY
        context->setError(AL_INVALID_VALUE, "Mapping buffer %u without read or write access",
            buffer);
    else
    {
        ALbitfieldSOFT unavailable = (albuf->Access^access) & access;
        if(ReadRef(albuf->ref) != 0 && !(access&AL_MAP_PERSISTENT_BIT_SOFT)) UNLIKELY
            context->setError(AL_INVALID_OPERATION,
                "Mapping in-use buffer %u without persistent mapping", buffer);
        else if(albuf->MappedAccess != 0) UNLIKELY
            context->setError(AL_INVALID_OPERATION, "Mapping already-mapped buffer %u", buffer);
        else if((unavailable&AL_MAP_READ_BIT_SOFT)) UNLIKELY
            context->setError(AL_INVALID_VALUE,
                "Mapping buffer %u for reading without read access", buffer);
        else if((unavailable&AL_MAP_WRITE_BIT_SOFT)) UNLIKELY
            context->setError(AL_INVALID_VALUE,
                "Mapping buffer %u for writing without write access", buffer);
        else if((unavailable&AL_MAP_PERSISTENT_BIT_SOFT)) UNLIKELY
            context->setError(AL_INVALID_VALUE,
                "Mapping buffer %u persistently without persistent access", buffer);
        else if(offset < 0 || length <= 0
            || static_cast<ALuint>(offset) >= albuf->OriginalSize
            || static_cast<ALuint>(length) > albuf->OriginalSize - static_cast<ALuint>(offset))
            UNLIKELY
            context->setError(AL_INVALID_VALUE, "Mapping invalid range %d+%d for buffer %u",
                offset, length, buffer);
        else
        {
            void *retval{albuf->mData.data() + offset};
            albuf->MappedAccess = access;
            albuf->MappedOffset = offset;
            albuf->MappedSize = length;
            return retval;
        }
    }

    return nullptr;
}
END_API_FUNC

AL_API void AL_APIENTRY alUnmapBufferSOFT(ALuint buffer)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    ALbuffer *albuf = LookupBuffer(device, buffer);
    if(!albuf) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if(albuf->MappedAccess == 0) UNLIKELY
        context->setError(AL_INVALID_OPERATION, "Unmapping unmapped buffer %u", buffer);
    else
    {
        albuf->MappedAccess = 0;
        albuf->MappedOffset = 0;
        albuf->MappedSize = 0;
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alFlushMappedBufferSOFT(ALuint buffer, ALsizei offset, ALsizei length)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    ALbuffer *albuf = LookupBuffer(device, buffer);
    if(!albuf) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if(!(albuf->MappedAccess&AL_MAP_WRITE_BIT_SOFT)) UNLIKELY
        context->setError(AL_INVALID_OPERATION, "Flushing buffer %u while not mapped for writing",
            buffer);
    else if(offset < albuf->MappedOffset || length <= 0
        || offset >= albuf->MappedOffset+albuf->MappedSize
        || length > albuf->MappedOffset+albuf->MappedSize-offset) UNLIKELY
        context->setError(AL_INVALID_VALUE, "Flushing invalid range %d+%d on buffer %u", offset,
            length, buffer);
    else
    {
        /* FIXME: Need to use some method of double-buffering for the mixer and
         * app to hold separate memory, which can be safely transfered
         * asynchronously. Currently we just say the app shouldn't write where
         * OpenAL's reading, and hope for the best...
         */
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alBufferSubDataSOFT(ALuint buffer, ALenum format, const ALvoid *data, ALsizei offset, ALsizei length)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    ALbuffer *albuf = LookupBuffer(device, buffer);
    if(!albuf) UNLIKELY
        return context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);

    auto usrfmt = DecomposeUserFormat(format);
    if(!usrfmt) UNLIKELY
        return context->setError(AL_INVALID_ENUM, "Invalid format 0x%04x", format);

    const ALuint unpack_align{albuf->UnpackAlign};
    const ALuint align{SanitizeAlignment(usrfmt->type, unpack_align)};
    if(align < 1) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "Invalid unpack alignment %u", unpack_align);
    if(usrfmt->channels != albuf->mChannels || usrfmt->type != albuf->mType) UNLIKELY
        return context->setError(AL_INVALID_ENUM, "Unpacking data with mismatched format");
    if(align != albuf->mBlockAlign) UNLIKELY
        return context->setError(AL_INVALID_VALUE,
            "Unpacking data with alignment %u does not match original alignment %u", align,
            albuf->mBlockAlign);
    if(albuf->isBFormat() && albuf->UnpackAmbiOrder != albuf->mAmbiOrder) UNLIKELY
        return context->setError(AL_INVALID_VALUE,
            "Unpacking data with mismatched ambisonic order");
    if(albuf->MappedAccess != 0) UNLIKELY
        return context->setError(AL_INVALID_OPERATION, "Unpacking data into mapped buffer %u",
            buffer);

    const ALuint num_chans{albuf->channelsFromFmt()};
    const ALuint byte_align{
        (albuf->mType == FmtIMA4) ? ((align-1)/2 + 4) * num_chans :
        (albuf->mType == FmtMSADPCM) ? ((align-2)/2 + 7) * num_chans :
        (align * albuf->bytesFromFmt() * num_chans)};

    if(offset < 0 || length < 0 || static_cast<ALuint>(offset) > albuf->OriginalSize
        || static_cast<ALuint>(length) > albuf->OriginalSize-static_cast<ALuint>(offset))
        UNLIKELY
        return context->setError(AL_INVALID_VALUE, "Invalid data sub-range %d+%d on buffer %u",
            offset, length, buffer);
    if((static_cast<ALuint>(offset)%byte_align) != 0) UNLIKELY
        return context->setError(AL_INVALID_VALUE,
            "Sub-range offset %d is not a multiple of frame size %d (%d unpack alignment)",
            offset, byte_align, align);
    if((static_cast<ALuint>(length)%byte_align) != 0) UNLIKELY
        return context->setError(AL_INVALID_VALUE,
            "Sub-range length %d is not a multiple of frame size %d (%d unpack alignment)",
            length, byte_align, align);

    assert(al::to_underlying(usrfmt->type) == al::to_underlying(albuf->mType));
    memcpy(albuf->mData.data()+offset, data, static_cast<ALuint>(length));
}
END_API_FUNC


AL_API void AL_APIENTRY alBufferSamplesSOFT(ALuint /*buffer*/, ALuint /*samplerate*/,
    ALenum /*internalformat*/, ALsizei /*samples*/, ALenum /*channels*/, ALenum /*type*/,
    const ALvoid* /*data*/)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    context->setError(AL_INVALID_OPERATION, "alBufferSamplesSOFT not supported");
}
END_API_FUNC

AL_API void AL_APIENTRY alBufferSubSamplesSOFT(ALuint /*buffer*/, ALsizei /*offset*/,
    ALsizei /*samples*/, ALenum /*channels*/, ALenum /*type*/, const ALvoid* /*data*/)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    context->setError(AL_INVALID_OPERATION, "alBufferSubSamplesSOFT not supported");
}
END_API_FUNC

AL_API void AL_APIENTRY alGetBufferSamplesSOFT(ALuint /*buffer*/, ALsizei /*offset*/,
    ALsizei /*samples*/, ALenum /*channels*/, ALenum /*type*/, ALvoid* /*data*/)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    context->setError(AL_INVALID_OPERATION, "alGetBufferSamplesSOFT not supported");
}
END_API_FUNC

AL_API ALboolean AL_APIENTRY alIsBufferFormatSupportedSOFT(ALenum /*format*/)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return AL_FALSE;

    context->setError(AL_INVALID_OPERATION, "alIsBufferFormatSupportedSOFT not supported");
    return AL_FALSE;
}
END_API_FUNC


AL_API void AL_APIENTRY alBufferf(ALuint buffer, ALenum param, ALfloat /*value*/)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    if(LookupBuffer(device, buffer) == nullptr) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else switch(param)
    {
    default:
        context->setError(AL_INVALID_ENUM, "Invalid buffer float property 0x%04x", param);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alBuffer3f(ALuint buffer, ALenum param,
    ALfloat /*value1*/, ALfloat /*value2*/, ALfloat /*value3*/)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    if(LookupBuffer(device, buffer) == nullptr) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else switch(param)
    {
    default:
        context->setError(AL_INVALID_ENUM, "Invalid buffer 3-float property 0x%04x", param);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alBufferfv(ALuint buffer, ALenum param, const ALfloat *values)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    if(LookupBuffer(device, buffer) == nullptr) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if(!values) UNLIKELY
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    default:
        context->setError(AL_INVALID_ENUM, "Invalid buffer float-vector property 0x%04x", param);
    }
}
END_API_FUNC


AL_API void AL_APIENTRY alBufferi(ALuint buffer, ALenum param, ALint value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    ALbuffer *albuf = LookupBuffer(device, buffer);
    if(!albuf) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else switch(param)
    {
    case AL_UNPACK_BLOCK_ALIGNMENT_SOFT:
        if(value < 0) UNLIKELY
            context->setError(AL_INVALID_VALUE, "Invalid unpack block alignment %d", value);
        else
            albuf->UnpackAlign = static_cast<ALuint>(value);
        break;

    case AL_PACK_BLOCK_ALIGNMENT_SOFT:
        if(value < 0) UNLIKELY
            context->setError(AL_INVALID_VALUE, "Invalid pack block alignment %d", value);
        else
            albuf->PackAlign = static_cast<ALuint>(value);
        break;

    case AL_AMBISONIC_LAYOUT_SOFT:
        if(ReadRef(albuf->ref) != 0) UNLIKELY
            context->setError(AL_INVALID_OPERATION, "Modifying in-use buffer %u's ambisonic layout",
                buffer);
        else if(const auto layout = AmbiLayoutFromEnum(value))
            albuf->mAmbiLayout = layout.value();
        else UNLIKELY
            context->setError(AL_INVALID_VALUE, "Invalid unpack ambisonic layout %04x", value);
        break;

    case AL_AMBISONIC_SCALING_SOFT:
        if(ReadRef(albuf->ref) != 0) UNLIKELY
            context->setError(AL_INVALID_OPERATION, "Modifying in-use buffer %u's ambisonic scaling",
                buffer);
        else if(const auto scaling = AmbiScalingFromEnum(value))
            albuf->mAmbiScaling = scaling.value();
        else UNLIKELY
            context->setError(AL_INVALID_VALUE, "Invalid unpack ambisonic scaling %04x", value);
        break;

    case AL_UNPACK_AMBISONIC_ORDER_SOFT:
        if(value < 1 || value > 14) UNLIKELY
            context->setError(AL_INVALID_VALUE, "Invalid unpack ambisonic order %d", value);
        else
            albuf->UnpackAmbiOrder = static_cast<ALuint>(value);
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid buffer integer property 0x%04x", param);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alBuffer3i(ALuint buffer, ALenum param,
    ALint /*value1*/, ALint /*value2*/, ALint /*value3*/)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    if(LookupBuffer(device, buffer) == nullptr) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else switch(param)
    {
    default:
        context->setError(AL_INVALID_ENUM, "Invalid buffer 3-integer property 0x%04x", param);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alBufferiv(ALuint buffer, ALenum param, const ALint *values)
START_API_FUNC
{
    if(values)
    {
        switch(param)
        {
        case AL_UNPACK_BLOCK_ALIGNMENT_SOFT:
        case AL_PACK_BLOCK_ALIGNMENT_SOFT:
        case AL_AMBISONIC_LAYOUT_SOFT:
        case AL_AMBISONIC_SCALING_SOFT:
        case AL_UNPACK_AMBISONIC_ORDER_SOFT:
            alBufferi(buffer, param, values[0]);
            return;
        }
    }

    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    ALbuffer *albuf = LookupBuffer(device, buffer);
    if(!albuf) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if(!values) UNLIKELY
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    case AL_LOOP_POINTS_SOFT:
        if(ReadRef(albuf->ref) != 0) UNLIKELY
            context->setError(AL_INVALID_OPERATION, "Modifying in-use buffer %u's loop points",
                buffer);
        else if(values[0] < 0 || values[0] >= values[1]
            || static_cast<ALuint>(values[1]) > albuf->mSampleLen) UNLIKELY
            context->setError(AL_INVALID_VALUE, "Invalid loop point range %d -> %d on buffer %u",
                values[0], values[1], buffer);
        else
        {
            albuf->mLoopStart = static_cast<ALuint>(values[0]);
            albuf->mLoopEnd = static_cast<ALuint>(values[1]);
        }
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid buffer integer-vector property 0x%04x", param);
    }
}
END_API_FUNC


AL_API void AL_APIENTRY alGetBufferf(ALuint buffer, ALenum param, ALfloat *value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    ALbuffer *albuf = LookupBuffer(device, buffer);
    if(!albuf) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if(!value) UNLIKELY
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    case AL_SEC_LENGTH_SOFT:
        *value = (albuf->mSampleRate < 1) ? 0.0f :
            (static_cast<float>(albuf->mSampleLen) / static_cast<float>(albuf->mSampleRate));
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid buffer float property 0x%04x", param);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alGetBuffer3f(ALuint buffer, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    if(LookupBuffer(device, buffer) == nullptr) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if(!value1 || !value2 || !value3) UNLIKELY
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    default:
        context->setError(AL_INVALID_ENUM, "Invalid buffer 3-float property 0x%04x", param);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alGetBufferfv(ALuint buffer, ALenum param, ALfloat *values)
START_API_FUNC
{
    switch(param)
    {
    case AL_SEC_LENGTH_SOFT:
        alGetBufferf(buffer, param, values);
        return;
    }

    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    if(LookupBuffer(device, buffer) == nullptr) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if(!values) UNLIKELY
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    default:
        context->setError(AL_INVALID_ENUM, "Invalid buffer float-vector property 0x%04x", param);
    }
}
END_API_FUNC


AL_API void AL_APIENTRY alGetBufferi(ALuint buffer, ALenum param, ALint *value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};
    ALbuffer *albuf = LookupBuffer(device, buffer);
    if(!albuf) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if(!value) UNLIKELY
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    case AL_FREQUENCY:
        *value = static_cast<ALint>(albuf->mSampleRate);
        break;

    case AL_BITS:
        *value = (albuf->mType == FmtIMA4 || albuf->mType == FmtMSADPCM) ? 4
            : static_cast<ALint>(albuf->bytesFromFmt() * 8);
        break;

    case AL_CHANNELS:
        *value = static_cast<ALint>(albuf->channelsFromFmt());
        break;

    case AL_SIZE:
        *value = albuf->mCallback ? 0 : static_cast<ALint>(albuf->mData.size());
        break;

    case AL_BYTE_LENGTH_SOFT:
        *value = static_cast<ALint>(albuf->mSampleLen / albuf->mBlockAlign
            * albuf->blockSizeFromFmt());
        break;

    case AL_SAMPLE_LENGTH_SOFT:
        *value = static_cast<ALint>(albuf->mSampleLen);
        break;

    case AL_UNPACK_BLOCK_ALIGNMENT_SOFT:
        *value = static_cast<ALint>(albuf->UnpackAlign);
        break;

    case AL_PACK_BLOCK_ALIGNMENT_SOFT:
        *value = static_cast<ALint>(albuf->PackAlign);
        break;

    case AL_AMBISONIC_LAYOUT_SOFT:
        *value = EnumFromAmbiLayout(albuf->mAmbiLayout);
        break;

    case AL_AMBISONIC_SCALING_SOFT:
        *value = EnumFromAmbiScaling(albuf->mAmbiScaling);
        break;

    case AL_UNPACK_AMBISONIC_ORDER_SOFT:
        *value = static_cast<int>(albuf->UnpackAmbiOrder);
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid buffer integer property 0x%04x", param);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alGetBuffer3i(ALuint buffer, ALenum param, ALint *value1, ALint *value2, ALint *value3)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};
    if(LookupBuffer(device, buffer) == nullptr) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if(!value1 || !value2 || !value3) UNLIKELY
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    default:
        context->setError(AL_INVALID_ENUM, "Invalid buffer 3-integer property 0x%04x", param);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alGetBufferiv(ALuint buffer, ALenum param, ALint *values)
START_API_FUNC
{
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
        alGetBufferi(buffer, param, values);
        return;
    }

    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};
    ALbuffer *albuf = LookupBuffer(device, buffer);
    if(!albuf) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if(!values) UNLIKELY
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    case AL_LOOP_POINTS_SOFT:
        values[0] = static_cast<ALint>(albuf->mLoopStart);
        values[1] = static_cast<ALint>(albuf->mLoopEnd);
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid buffer integer-vector property 0x%04x", param);
    }
}
END_API_FUNC


AL_API void AL_APIENTRY alBufferCallbackSOFT(ALuint buffer, ALenum format, ALsizei freq,
    ALBUFFERCALLBACKTYPESOFT callback, ALvoid *userptr)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    ALbuffer *albuf = LookupBuffer(device, buffer);
    if(!albuf) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if(freq < 1) UNLIKELY
        context->setError(AL_INVALID_VALUE, "Invalid sample rate %d", freq);
    else if(callback == nullptr) UNLIKELY
        context->setError(AL_INVALID_VALUE, "NULL callback");
    else
    {
        auto usrfmt = DecomposeUserFormat(format);
        if(!usrfmt) UNLIKELY
            context->setError(AL_INVALID_ENUM, "Invalid format 0x%04x", format);
        else
            PrepareCallback(context.get(), albuf, freq, usrfmt->channels, usrfmt->type, callback,
                userptr);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alGetBufferPtrSOFT(ALuint buffer, ALenum param, ALvoid **value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};
    ALbuffer *albuf = LookupBuffer(device, buffer);
    if(!albuf) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if(!value) UNLIKELY
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    case AL_BUFFER_CALLBACK_FUNCTION_SOFT:
        *value = reinterpret_cast<void*>(albuf->mCallback);
        break;
    case AL_BUFFER_CALLBACK_USER_PARAM_SOFT:
        *value = albuf->mUserData;
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid buffer pointer property 0x%04x", param);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alGetBuffer3PtrSOFT(ALuint buffer, ALenum param, ALvoid **value1, ALvoid **value2, ALvoid **value3)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};
    if(LookupBuffer(device, buffer) == nullptr) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if(!value1 || !value2 || !value3) UNLIKELY
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    default:
        context->setError(AL_INVALID_ENUM, "Invalid buffer 3-pointer property 0x%04x", param);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alGetBufferPtrvSOFT(ALuint buffer, ALenum param, ALvoid **values)
START_API_FUNC
{
    switch(param)
    {
    case AL_BUFFER_CALLBACK_FUNCTION_SOFT:
    case AL_BUFFER_CALLBACK_USER_PARAM_SOFT:
        alGetBufferPtrSOFT(buffer, param, values);
        return;
    }

    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};
    if(LookupBuffer(device, buffer) == nullptr) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if(!values) UNLIKELY
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    default:
        context->setError(AL_INVALID_ENUM, "Invalid buffer pointer-vector property 0x%04x", param);
    }
}
END_API_FUNC


BufferSubList::~BufferSubList()
{
    uint64_t usemask{~FreeMask};
    while(usemask)
    {
        const int idx{al::countr_zero(usemask)};
        al::destroy_at(Buffers+idx);
        usemask &= ~(1_u64 << idx);
    }
    FreeMask = ~usemask;
    al_free(Buffers);
    Buffers = nullptr;
}


#ifdef ALSOFT_EAX
FORCE_ALIGN ALboolean AL_APIENTRY EAXSetBufferMode(ALsizei n, const ALuint* buffers, ALint value)
START_API_FUNC
{
#define EAX_PREFIX "[EAXSetBufferMode] "

    const auto context = ContextRef{GetContextRef()};
    if(!context)
    {
        ERR(EAX_PREFIX "%s\n", "No current context.");
        return ALC_FALSE;
    }

    if(!eax_g_is_enabled)
    {
        context->setError(AL_INVALID_OPERATION, EAX_PREFIX "%s", "EAX not enabled.");
        return ALC_FALSE;
    }

    const auto storage = EaxStorageFromEnum(value);
    if(!storage)
    {
        context->setError(AL_INVALID_ENUM, EAX_PREFIX "Unsupported X-RAM mode 0x%x", value);
        return ALC_FALSE;
    }

    if(n == 0)
        return ALC_TRUE;

    if(n < 0)
    {
        context->setError(AL_INVALID_VALUE, EAX_PREFIX "Buffer count %d out of range", n);
        return ALC_FALSE;
    }

    if(!buffers)
    {
        context->setError(AL_INVALID_VALUE, EAX_PREFIX "%s", "Null AL buffers");
        return ALC_FALSE;
    }

    auto device = context->mALDevice.get();
    std::lock_guard<std::mutex> device_lock{device->BufferLock};
    size_t total_needed{0};

    // Validate the buffers.
    //
    for(auto i = 0;i < n;++i)
    {
        const auto bufid = buffers[i];
        if(bufid == AL_NONE)
            continue;

        const auto buffer = LookupBuffer(device, bufid);
        if(!buffer) UNLIKELY
        {
            ERR(EAX_PREFIX "Invalid buffer ID %u.\n", bufid);
            return ALC_FALSE;
        }

        /* TODO: Is the store location allowed to change for in-use buffers, or
         * only when not set/queued on a source?
         */

        if(*storage == EaxStorage::Hardware && !buffer->eax_x_ram_is_hardware)
        {
            /* FIXME: This doesn't account for duplicate buffers. When the same
             * buffer ID is specified multiple times in the provided list, it
             * counts each instance as more memory that needs to fit in X-RAM.
             */
            if(std::numeric_limits<size_t>::max()-buffer->OriginalSize < total_needed) UNLIKELY
            {
                context->setError(AL_OUT_OF_MEMORY, EAX_PREFIX "Size overflow (%u + %zu)\n",
                    buffer->OriginalSize, total_needed);
                return ALC_FALSE;
            }
            total_needed += buffer->OriginalSize;
        }
    }
    if(total_needed > device->eax_x_ram_free_size)
    {
        context->setError(AL_OUT_OF_MEMORY,EAX_PREFIX "Out of X-RAM memory (need: %zu, avail: %u)",
            total_needed, device->eax_x_ram_free_size);
        return ALC_FALSE;
    }

    // Update the mode.
    //
    for(auto i = 0;i < n;++i)
    {
        const auto bufid = buffers[i];
        if(bufid == AL_NONE)
            continue;

        const auto buffer = LookupBuffer(device, bufid);
        assert(buffer);

        if(*storage == EaxStorage::Hardware)
            eax_x_ram_apply(*device, *buffer);
        else
            eax_x_ram_clear(*device, *buffer);
        buffer->eax_x_ram_mode = *storage;
    }

    return AL_TRUE;

#undef EAX_PREFIX
}
END_API_FUNC

FORCE_ALIGN ALenum AL_APIENTRY EAXGetBufferMode(ALuint buffer, ALint* pReserved)
START_API_FUNC
{
#define EAX_PREFIX "[EAXGetBufferMode] "

    const auto context = ContextRef{GetContextRef()};
    if(!context)
    {
        ERR(EAX_PREFIX "%s\n", "No current context.");
        return AL_NONE;
    }

    if(!eax_g_is_enabled)
    {
        context->setError(AL_INVALID_OPERATION, EAX_PREFIX "%s", "EAX not enabled.");
        return AL_NONE;
    }

    if(pReserved)
    {
        context->setError(AL_INVALID_VALUE, EAX_PREFIX "%s", "Non-null reserved parameter");
        return AL_NONE;
    }

    auto device = context->mALDevice.get();
    std::lock_guard<std::mutex> device_lock{device->BufferLock};

    const auto al_buffer = LookupBuffer(device, buffer);
    if(!al_buffer)
    {
        context->setError(AL_INVALID_NAME, EAX_PREFIX "Invalid buffer ID %u", buffer);
        return AL_NONE;
    }

    return EnumFromEaxStorage(al_buffer->eax_x_ram_mode);

#undef EAX_PREFIX
}
END_API_FUNC

#endif // ALSOFT_EAX
