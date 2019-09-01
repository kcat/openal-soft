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
#include <utility>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "albyte.h"
#include "alcmain.h"
#include "alcontext.h"
#include "alexcpt.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "aloptional.h"
#include "atomic.h"
#include "inprogext.h"
#include "opthelpers.h"


namespace {

/* IMA ADPCM Stepsize table */
constexpr int IMAStep_size[89] = {
       7,    8,    9,   10,   11,   12,   13,   14,   16,   17,   19,
      21,   23,   25,   28,   31,   34,   37,   41,   45,   50,   55,
      60,   66,   73,   80,   88,   97,  107,  118,  130,  143,  157,
     173,  190,  209,  230,  253,  279,  307,  337,  371,  408,  449,
     494,  544,  598,  658,  724,  796,  876,  963, 1060, 1166, 1282,
    1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
    4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493,10442,
   11487,12635,13899,15289,16818,18500,20350,22358,24633,27086,29794,
   32767
};

/* IMA4 ADPCM Codeword decode table */
constexpr int IMA4Codeword[16] = {
    1, 3, 5, 7, 9, 11, 13, 15,
   -1,-3,-5,-7,-9,-11,-13,-15,
};

/* IMA4 ADPCM Step index adjust decode table */
constexpr int IMA4Index_adjust[16] = {
   -1,-1,-1,-1, 2, 4, 6, 8,
   -1,-1,-1,-1, 2, 4, 6, 8
};


/* MSADPCM Adaption table */
constexpr int MSADPCMAdaption[16] = {
    230, 230, 230, 230, 307, 409, 512, 614,
    768, 614, 512, 409, 307, 230, 230, 230
};

/* MSADPCM Adaption Coefficient tables */
constexpr int MSADPCMAdaptionCoeff[7][2] = {
    { 256,    0 },
    { 512, -256 },
    {   0,    0 },
    { 192,   64 },
    { 240,    0 },
    { 460, -208 },
    { 392, -232 }
};


void DecodeIMA4Block(ALshort *dst, const al::byte *src, ALint numchans, ALsizei align)
{
    ALint sample[MAX_INPUT_CHANNELS]{};
    ALint index[MAX_INPUT_CHANNELS]{};
    ALuint code[MAX_INPUT_CHANNELS]{};

    for(int c{0};c < numchans;c++)
    {
        sample[c] = al::to_integer<int>(src[0]) | (al::to_integer<int>(src[1])<<8);
        sample[c] = (sample[c]^0x8000) - 32768;
        src += 2;
        index[c] = al::to_integer<int>(src[0]) | (al::to_integer<int>(src[1])<<8);
        index[c] = clampi((index[c]^0x8000) - 32768, 0, 88);
        src += 2;

        *(dst++) = sample[c];
    }

    for(int i{1};i < align;i++)
    {
        if((i&7) == 1)
        {
            for(int c{0};c < numchans;c++)
            {
                code[c] = al::to_integer<ALuint>(src[0]) | (al::to_integer<ALuint>(src[1])<< 8) |
                    (al::to_integer<ALuint>(src[2])<<16) | (al::to_integer<ALuint>(src[3])<<24);
                src += 4;
            }
        }

        for(int c{0};c < numchans;c++)
        {
            const ALuint nibble{code[c]&0xf};
            code[c] >>= 4;

            sample[c] += IMA4Codeword[nibble] * IMAStep_size[index[c]] / 8;
            sample[c] = clampi(sample[c], -32768, 32767);

            index[c] += IMA4Index_adjust[nibble];
            index[c] = clampi(index[c], 0, 88);

            *(dst++) = sample[c];
        }
    }
}

void DecodeMSADPCMBlock(ALshort *dst, const al::byte *src, ALint numchans, ALsizei align)
{
    ALubyte blockpred[MAX_INPUT_CHANNELS]{};
    ALint delta[MAX_INPUT_CHANNELS]{};
    ALshort samples[MAX_INPUT_CHANNELS][2]{};

    for(int c{0};c < numchans;c++)
    {
        blockpred[c] = minu(al::to_integer<ALubyte>(src[0]), 6);
        ++src;
    }
    for(int c{0};c < numchans;c++)
    {
        delta[c] = al::to_integer<int>(src[0]) | (al::to_integer<int>(src[1])<<8);
        delta[c] = (delta[c]^0x8000) - 32768;
        src += 2;
    }
    for(int c{0};c < numchans;c++)
    {
        samples[c][0] = al::to_integer<short>(src[0]) | (al::to_integer<short>(src[1])<<8);
        samples[c][0] = (samples[c][0]^0x8000) - 32768;
        src += 2;
    }
    for(int c{0};c < numchans;c++)
    {
        samples[c][1] = al::to_integer<short>(src[0]) | (al::to_integer<short>(src[1])<<8);
        samples[c][1] = (samples[c][1]^0x8000) - 32768;
        src += 2;
    }

    /* Second sample is written first. */
    for(int c{0};c < numchans;c++)
        *(dst++) = samples[c][1];
    for(int c{0};c < numchans;c++)
        *(dst++) = samples[c][0];

    int num{0};
    for(int i{2};i < align;i++)
    {
        for(int c{0};c < numchans;c++)
        {
            /* Read the nibble (first is in the upper bits). */
            al::byte nibble;
            if(!(num++ & 1))
                nibble = *src >> 4;
            else
                nibble = *(src++) & 0x0f;

            ALint pred{(samples[c][0]*MSADPCMAdaptionCoeff[blockpred[c]][0] +
                samples[c][1]*MSADPCMAdaptionCoeff[blockpred[c]][1]) / 256};
            pred += (al::to_integer<int>(nibble^0x08) - 0x08) * delta[c];
            pred  = clampi(pred, -32768, 32767);

            samples[c][1] = samples[c][0];
            samples[c][0] = pred;

            delta[c] = (MSADPCMAdaption[al::to_integer<ALubyte>(nibble)] * delta[c]) / 256;
            delta[c] = maxi(16, delta[c]);

            *(dst++) = pred;
        }
    }
}

void Convert_ALshort_ALima4(ALshort *dst, const al::byte *src, ALsizei numchans, ALsizei len,
    ALsizei align)
{
    const ALsizei byte_align{((align-1)/2 + 4) * numchans};

    len /= align;
    while(len--)
    {
        DecodeIMA4Block(dst, src, numchans, align);
        src += byte_align;
        dst += align*numchans;
    }
}

void Convert_ALshort_ALmsadpcm(ALshort *dst, const al::byte *src, ALsizei numchans, ALsizei len,
    ALsizei align)
{
    const ALsizei byte_align{((align-2)/2 + 7) * numchans};

    len /= align;
    while(len--)
    {
        DecodeMSADPCMBlock(dst, src, numchans, align);
        src += byte_align;
        dst += align*numchans;
    }
}


constexpr ALbitfieldSOFT INVALID_STORAGE_MASK{~unsigned(AL_MAP_READ_BIT_SOFT |
    AL_MAP_WRITE_BIT_SOFT | AL_MAP_PERSISTENT_BIT_SOFT | AL_PRESERVE_DATA_BIT_SOFT)};
constexpr ALbitfieldSOFT MAP_READ_WRITE_FLAGS{AL_MAP_READ_BIT_SOFT | AL_MAP_WRITE_BIT_SOFT};
constexpr ALbitfieldSOFT INVALID_MAP_FLAGS{~unsigned(AL_MAP_READ_BIT_SOFT | AL_MAP_WRITE_BIT_SOFT |
    AL_MAP_PERSISTENT_BIT_SOFT)};


ALbuffer *AllocBuffer(ALCcontext *context)
{
    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};
    auto sublist = std::find_if(device->BufferList.begin(), device->BufferList.end(),
        [](const BufferSubList &entry) noexcept -> bool
        { return entry.FreeMask != 0; }
    );

    auto lidx = static_cast<ALsizei>(std::distance(device->BufferList.begin(), sublist));
    ALbuffer *buffer{nullptr};
    ALsizei slidx{0};
    if LIKELY(sublist != device->BufferList.end())
    {
        slidx = CTZ64(sublist->FreeMask);
        buffer = sublist->Buffers + slidx;
    }
    else
    {
        /* Don't allocate so many list entries that the 32-bit ID could
         * overflow...
         */
        if UNLIKELY(device->BufferList.size() >= 1<<25)
        {
            context->setError(AL_OUT_OF_MEMORY, "Too many buffers allocated");
            return nullptr;
        }
        device->BufferList.emplace_back();
        sublist = device->BufferList.end() - 1;
        sublist->FreeMask = ~0_u64;
        sublist->Buffers = reinterpret_cast<ALbuffer*>(al_calloc(16, sizeof(ALbuffer)*64));
        if UNLIKELY(!sublist->Buffers)
        {
            device->BufferList.pop_back();
            context->setError(AL_OUT_OF_MEMORY, "Failed to allocate buffer batch");
            return nullptr;
        }

        slidx = 0;
        buffer = sublist->Buffers + slidx;
    }

    buffer = new (buffer) ALbuffer{};
    /* Add 1 to avoid buffer ID 0. */
    buffer->id = ((lidx<<6) | slidx) + 1;

    sublist->FreeMask &= ~(1_u64 << slidx);

    return buffer;
}

void FreeBuffer(ALCdevice *device, ALbuffer *buffer)
{
    ALuint id{buffer->id - 1};
    ALsizei lidx = id >> 6;
    ALsizei slidx = id & 0x3f;

    al::destroy_at(buffer);

    device->BufferList[lidx].FreeMask |= 1_u64 << slidx;
}

inline ALbuffer *LookupBuffer(ALCdevice *device, ALuint id)
{
    ALuint lidx = (id-1) >> 6;
    ALsizei slidx = (id-1) & 0x3f;

    if UNLIKELY(lidx >= device->BufferList.size())
        return nullptr;
    BufferSubList &sublist = device->BufferList[lidx];
    if UNLIKELY(sublist.FreeMask & (1_u64 << slidx))
        return nullptr;
    return sublist.Buffers + slidx;
}


ALsizei SanitizeAlignment(UserFmtType type, ALsizei align)
{
    if(align < 0)
        return 0;

    if(align == 0)
    {
        if(type == UserFmtIMA4)
        {
            /* Here is where things vary:
             * nVidia and Apple use 64+1 sample frames per block -> block_size=36 bytes per channel
             * Most PC sound software uses 2040+1 sample frames per block -> block_size=1024 bytes per channel
             */
            return 65;
        }
        if(type == UserFmtMSADPCM)
            return 64;
        return 1;
    }

    if(type == UserFmtIMA4)
    {
        /* IMA4 block alignment must be a multiple of 8, plus 1. */
        if((align&7) == 1) return align;
        return 0;
    }
    if(type == UserFmtMSADPCM)
    {
        /* MSADPCM block alignment must be a multiple of 2. */
        if((align&1) == 0) return align;
        return 0;
    }

    return align;
}


const ALchar *NameFromUserFmtType(UserFmtType type)
{
    switch(type)
    {
    case UserFmtUByte: return "Unsigned Byte";
    case UserFmtShort: return "Signed Short";
    case UserFmtFloat: return "Float32";
    case UserFmtDouble: return "Float64";
    case UserFmtMulaw: return "muLaw";
    case UserFmtAlaw: return "aLaw";
    case UserFmtIMA4: return "IMA4 ADPCM";
    case UserFmtMSADPCM: return "MSADPCM";
    }
    return "<internal type error>";
}

/*
 * LoadData
 *
 * Loads the specified data into the buffer, using the specified format.
 */
void LoadData(ALCcontext *context, ALbuffer *ALBuf, ALuint freq, ALsizei size, UserFmtChannels SrcChannels, UserFmtType SrcType, const al::byte *SrcData, ALbitfieldSOFT access)
{
    if UNLIKELY(ReadRef(ALBuf->ref) != 0 || ALBuf->MappedAccess != 0)
        SETERR_RETURN(context, AL_INVALID_OPERATION,, "Modifying storage for in-use buffer %u",
                      ALBuf->id);

    /* Currently no channel configurations need to be converted. */
    FmtChannels DstChannels{FmtMono};
    switch(SrcChannels)
    {
    case UserFmtMono: DstChannels = FmtMono; break;
    case UserFmtStereo: DstChannels = FmtStereo; break;
    case UserFmtRear: DstChannels = FmtRear; break;
    case UserFmtQuad: DstChannels = FmtQuad; break;
    case UserFmtX51: DstChannels = FmtX51; break;
    case UserFmtX61: DstChannels = FmtX61; break;
    case UserFmtX71: DstChannels = FmtX71; break;
    case UserFmtBFormat2D: DstChannels = FmtBFormat2D; break;
    case UserFmtBFormat3D: DstChannels = FmtBFormat3D; break;
    }
    if UNLIKELY(static_cast<long>(SrcChannels) != static_cast<long>(DstChannels))
        SETERR_RETURN(context, AL_INVALID_ENUM, , "Invalid format");

    /* IMA4 and MSADPCM convert to 16-bit short. */
    FmtType DstType{FmtUByte};
    switch(SrcType)
    {
    case UserFmtUByte: DstType = FmtUByte; break;
    case UserFmtShort: DstType = FmtShort; break;
    case UserFmtFloat: DstType = FmtFloat; break;
    case UserFmtDouble: DstType = FmtDouble; break;
    case UserFmtAlaw: DstType = FmtAlaw; break;
    case UserFmtMulaw: DstType = FmtMulaw; break;
    case UserFmtIMA4: DstType = FmtShort; break;
    case UserFmtMSADPCM: DstType = FmtShort; break;
    }

    /* TODO: Currently we can only map samples when they're not converted. To
     * allow it would need some kind of double-buffering to hold onto a copy of
     * the original data.
     */
    if((access&MAP_READ_WRITE_FLAGS))
    {
        if UNLIKELY(static_cast<long>(SrcType) != static_cast<long>(DstType))
            SETERR_RETURN(context, AL_INVALID_VALUE,, "%s samples cannot be mapped",
                NameFromUserFmtType(SrcType));
    }

    const ALsizei unpackalign{ALBuf->UnpackAlign.load()};
    const ALsizei align{SanitizeAlignment(SrcType, unpackalign)};
    if UNLIKELY(align < 1)
        SETERR_RETURN(context, AL_INVALID_VALUE,, "Invalid unpack alignment %d for %s samples",
            unpackalign, NameFromUserFmtType(SrcType));

    if((access&AL_PRESERVE_DATA_BIT_SOFT))
    {
        /* Can only preserve data with the same format and alignment. */
        if UNLIKELY(ALBuf->mFmtChannels != DstChannels || ALBuf->OriginalType != SrcType)
            SETERR_RETURN(context, AL_INVALID_VALUE,, "Preserving data of mismatched format");
        if UNLIKELY(ALBuf->OriginalAlign != align)
            SETERR_RETURN(context, AL_INVALID_VALUE,, "Preserving data of mismatched alignment");
    }

    /* Convert the input/source size in bytes to sample frames using the unpack
     * block alignment.
     */
    const ALsizei SrcByteAlign{
        (SrcType == UserFmtIMA4) ? ((align-1)/2 + 4) * ChannelsFromUserFmt(SrcChannels) :
        (SrcType == UserFmtMSADPCM) ? ((align-2)/2 + 7) * ChannelsFromUserFmt(SrcChannels) :
        (align * FrameSizeFromUserFmt(SrcChannels, SrcType))
    };
    if UNLIKELY((size%SrcByteAlign) != 0)
        SETERR_RETURN(context, AL_INVALID_VALUE,,
            "Data size %d is not a multiple of frame size %d (%d unpack alignment)",
            size, SrcByteAlign, align);

    if UNLIKELY(size/SrcByteAlign > std::numeric_limits<ALsizei>::max()/align)
        SETERR_RETURN(context, AL_OUT_OF_MEMORY,,
            "Buffer size overflow, %d blocks x %d samples per block", size/SrcByteAlign, align);
    const auto frames = static_cast<ALuint>(size / SrcByteAlign * align);

    /* Convert the sample frames to the number of bytes needed for internal
     * storage.
     */
    ALsizei NumChannels{ChannelsFromFmt(DstChannels)};
    ALsizei FrameSize{NumChannels * BytesFromFmt(DstType)};
    if UNLIKELY(frames > std::numeric_limits<size_t>::max()/FrameSize)
        SETERR_RETURN(context, AL_OUT_OF_MEMORY,,
            "Buffer size overflow, %d frames x %d bytes per frame", frames, FrameSize);
    size_t newsize{static_cast<size_t>(frames) * FrameSize};

    /* Round up to the next 16-byte multiple. This could reallocate only when
     * increasing or the new size is less than half the current, but then the
     * buffer's AL_SIZE would not be very reliable for accounting buffer memory
     * usage, and reporting the real size could cause problems for apps that
     * use AL_SIZE to try to get the buffer's play length.
     */
    newsize = RoundUp(newsize, 16);
    if(newsize != ALBuf->mData.size())
    {
        auto newdata = al::vector<al::byte,16>(newsize, al::byte{});
        if((access&AL_PRESERVE_DATA_BIT_SOFT))
        {
            const size_t tocopy{minz(newdata.size(), ALBuf->mData.size())};
            std::copy_n(ALBuf->mData.begin(), tocopy, newdata.begin());
        }
        ALBuf->mData = std::move(newdata);
    }

    if(SrcType == UserFmtIMA4)
    {
        assert(DstType == FmtShort);
        if(SrcData != nullptr && !ALBuf->mData.empty())
            Convert_ALshort_ALima4(reinterpret_cast<ALshort*>(ALBuf->mData.data()),
                SrcData, NumChannels, frames, align);
        ALBuf->OriginalAlign = align;
    }
    else if(SrcType == UserFmtMSADPCM)
    {
        assert(DstType == FmtShort);
        if(SrcData != nullptr && !ALBuf->mData.empty())
            Convert_ALshort_ALmsadpcm(reinterpret_cast<ALshort*>(ALBuf->mData.data()),
                SrcData, NumChannels, frames, align);
        ALBuf->OriginalAlign = align;
    }
    else
    {
        assert(static_cast<long>(SrcType) == static_cast<long>(DstType));
        if(SrcData != nullptr && !ALBuf->mData.empty())
            std::copy_n(SrcData, frames*FrameSize, ALBuf->mData.begin());
        ALBuf->OriginalAlign = 1;
    }
    ALBuf->OriginalSize = size;
    ALBuf->OriginalType = SrcType;

    ALBuf->Frequency = freq;
    ALBuf->mFmtChannels = DstChannels;
    ALBuf->mFmtType = DstType;
    ALBuf->Access = access;

    ALBuf->SampleLen = frames;
    ALBuf->LoopStart = 0;
    ALBuf->LoopEnd = ALBuf->SampleLen;
}

void SetUpCallback(ALCcontext *context, ALbuffer *ALBuf,
	ALuint freq, UserFmtChannels SrcChannels, UserFmtType SrcType,
	ALbitfieldSOFT access, alSourceFunc_t callback, ALvoid* usr_ptr) {
	if UNLIKELY(ReadRef(ALBuf->ref) != 0 || ALBuf->MappedAccess != 0)
		SETERR_RETURN(context, AL_INVALID_OPERATION, , "Setting callback for in-use buffer!%u",
			ALBuf->id);

	/* Currently no channel configurations need to be converted. */
	/* TODO: Copied from LoadData, reusing may be better */
	FmtChannels DstChannels{ FmtMono };
	switch (SrcChannels)
	{
	case UserFmtMono: DstChannels = FmtMono; break;
	case UserFmtStereo: DstChannels = FmtStereo; break;
	case UserFmtRear: DstChannels = FmtRear; break;
	case UserFmtQuad: DstChannels = FmtQuad; break;
	case UserFmtX51: DstChannels = FmtX51; break;
	case UserFmtX61: DstChannels = FmtX61; break;
	case UserFmtX71: DstChannels = FmtX71; break;
	case UserFmtBFormat2D: DstChannels = FmtBFormat2D; break;
	case UserFmtBFormat3D: DstChannels = FmtBFormat3D; break;
	}
	if UNLIKELY(static_cast<long>(SrcChannels) != static_cast<long>(DstChannels))
		SETERR_RETURN(context, AL_INVALID_ENUM, , "Invalid format");

	/* IMA4 and MSADPCM needs to be managed via another buffer, which is not currently implemented. */
	if UNLIKELY(SrcType == UserFmtMSADPCM)
		SETERR_RETURN(context, AL_INVALID_ENUM, , "MSADPCM currently cannot be managed via callback.");
	else if UNLIKELY(SrcType == UserFmtIMA4)
		SETERR_RETURN(context, AL_INVALID_ENUM, , "IMA4 currently cannot be managed via callback.");
	FmtType DstType{ FmtUByte };
	switch (SrcType)
	{
	case UserFmtUByte: DstType = FmtUByte; break;
	case UserFmtShort: DstType = FmtShort; break;
	case UserFmtFloat: DstType = FmtFloat; break;
	case UserFmtDouble: DstType = FmtDouble; break;
	case UserFmtAlaw: DstType = FmtAlaw; break;
	case UserFmtMulaw: DstType = FmtMulaw; break;
	}

	if UNLIKELY(static_cast<long>(SrcType) != static_cast<long>(DstType))
		SETERR_RETURN(context, AL_INVALID_VALUE, , "%s samples cannot be mapped",
			NameFromUserFmtType(SrcType));

	/* TODO: Alignment is copied from LoadData here. May break the alBufferi functions, etc. */
	const ALsizei unpackalign{ ALBuf->UnpackAlign.load() };
	const ALsizei align{ SanitizeAlignment(SrcType, unpackalign) };
	if UNLIKELY(align < 1)
		SETERR_RETURN(context, AL_INVALID_VALUE, , "Invalid unpack alignment %d for %s samples",
			unpackalign, NameFromUserFmtType(SrcType));

	/* Cannot preserve a callback source */
	if UNLIKELY(access&AL_PRESERVE_DATA_BIT_SOFT)
		SETERR_RETURN(context, AL_INVALID_ENUM, , "Cannot preserve data for callback source.");

	const ALsizei SrcByteAlign{ align * FrameSizeFromUserFmt(SrcChannels, SrcType) };
	/* Convert the sample frames to the number of bytes needed for internal
	* storage.
	*/
	ALsizei NumChannels{ ChannelsFromFmt(DstChannels) };
	ALsizei FrameSize{ NumChannels * BytesFromFmt(DstType) };
	
	/* Buffer for a callback can be used for on the fly format conversion,
	* but currently this will be a empty one.
	*/
	ALBuf->mData = al::vector<al::byte, 16>{};
	ALBuf->callback = callback;
	ALBuf->usr_ptr = usr_ptr;

	//trivial set up of other values.
	ALBuf->OriginalSize = 0;
	ALBuf->OriginalType = SrcType;

	ALBuf->Frequency = freq;
	ALBuf->mFmtChannels = DstChannels;
	ALBuf->mFmtType = DstType;
	ALBuf->Access = access;

	ALBuf->SampleLen = 0;
	ALBuf->LoopStart = 0;
	ALBuf->LoopEnd = 0;
}

struct DecompResult { UserFmtChannels channels; UserFmtType type; };
al::optional<DecompResult> DecomposeUserFormat(ALenum format)
{
    struct FormatMap {
        ALenum format;
        UserFmtChannels channels;
        UserFmtType type;
    };
    static constexpr std::array<FormatMap,46> UserFmtList{{
        { AL_FORMAT_MONO8,             UserFmtMono, UserFmtUByte   },
        { AL_FORMAT_MONO16,            UserFmtMono, UserFmtShort   },
        { AL_FORMAT_MONO_FLOAT32,      UserFmtMono, UserFmtFloat   },
        { AL_FORMAT_MONO_DOUBLE_EXT,   UserFmtMono, UserFmtDouble  },
        { AL_FORMAT_MONO_IMA4,         UserFmtMono, UserFmtIMA4    },
        { AL_FORMAT_MONO_MSADPCM_SOFT, UserFmtMono, UserFmtMSADPCM },
        { AL_FORMAT_MONO_MULAW,        UserFmtMono, UserFmtMulaw   },
        { AL_FORMAT_MONO_ALAW_EXT,     UserFmtMono, UserFmtAlaw    },

        { AL_FORMAT_STEREO8,             UserFmtStereo, UserFmtUByte   },
        { AL_FORMAT_STEREO16,            UserFmtStereo, UserFmtShort   },
        { AL_FORMAT_STEREO_FLOAT32,      UserFmtStereo, UserFmtFloat   },
        { AL_FORMAT_STEREO_DOUBLE_EXT,   UserFmtStereo, UserFmtDouble  },
        { AL_FORMAT_STEREO_IMA4,         UserFmtStereo, UserFmtIMA4    },
        { AL_FORMAT_STEREO_MSADPCM_SOFT, UserFmtStereo, UserFmtMSADPCM },
        { AL_FORMAT_STEREO_MULAW,        UserFmtStereo, UserFmtMulaw   },
        { AL_FORMAT_STEREO_ALAW_EXT,     UserFmtStereo, UserFmtAlaw    },

        { AL_FORMAT_REAR8,      UserFmtRear, UserFmtUByte },
        { AL_FORMAT_REAR16,     UserFmtRear, UserFmtShort },
        { AL_FORMAT_REAR32,     UserFmtRear, UserFmtFloat },
        { AL_FORMAT_REAR_MULAW, UserFmtRear, UserFmtMulaw },

        { AL_FORMAT_QUAD8_LOKI,  UserFmtQuad, UserFmtUByte },
        { AL_FORMAT_QUAD16_LOKI, UserFmtQuad, UserFmtShort },

        { AL_FORMAT_QUAD8,      UserFmtQuad, UserFmtUByte },
        { AL_FORMAT_QUAD16,     UserFmtQuad, UserFmtShort },
        { AL_FORMAT_QUAD32,     UserFmtQuad, UserFmtFloat },
        { AL_FORMAT_QUAD_MULAW, UserFmtQuad, UserFmtMulaw },

        { AL_FORMAT_51CHN8,      UserFmtX51, UserFmtUByte },
        { AL_FORMAT_51CHN16,     UserFmtX51, UserFmtShort },
        { AL_FORMAT_51CHN32,     UserFmtX51, UserFmtFloat },
        { AL_FORMAT_51CHN_MULAW, UserFmtX51, UserFmtMulaw },

        { AL_FORMAT_61CHN8,      UserFmtX61, UserFmtUByte },
        { AL_FORMAT_61CHN16,     UserFmtX61, UserFmtShort },
        { AL_FORMAT_61CHN32,     UserFmtX61, UserFmtFloat },
        { AL_FORMAT_61CHN_MULAW, UserFmtX61, UserFmtMulaw },

        { AL_FORMAT_71CHN8,      UserFmtX71, UserFmtUByte },
        { AL_FORMAT_71CHN16,     UserFmtX71, UserFmtShort },
        { AL_FORMAT_71CHN32,     UserFmtX71, UserFmtFloat },
        { AL_FORMAT_71CHN_MULAW, UserFmtX71, UserFmtMulaw },

        { AL_FORMAT_BFORMAT2D_8,       UserFmtBFormat2D, UserFmtUByte },
        { AL_FORMAT_BFORMAT2D_16,      UserFmtBFormat2D, UserFmtShort },
        { AL_FORMAT_BFORMAT2D_FLOAT32, UserFmtBFormat2D, UserFmtFloat },
        { AL_FORMAT_BFORMAT2D_MULAW,   UserFmtBFormat2D, UserFmtMulaw },

        { AL_FORMAT_BFORMAT3D_8,       UserFmtBFormat3D, UserFmtUByte },
        { AL_FORMAT_BFORMAT3D_16,      UserFmtBFormat3D, UserFmtShort },
        { AL_FORMAT_BFORMAT3D_FLOAT32, UserFmtBFormat3D, UserFmtFloat },
        { AL_FORMAT_BFORMAT3D_MULAW,   UserFmtBFormat3D, UserFmtMulaw },
    }};

    for(const auto &fmt : UserFmtList)
    {
        if(fmt.format == format)
            return al::make_optional(DecompResult{fmt.channels, fmt.type});
    }
    return al::nullopt;
}

} // namespace


AL_API ALvoid AL_APIENTRY alGenBuffers(ALsizei n, ALuint *buffers)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    if UNLIKELY(n < 0)
        context->setError(AL_INVALID_VALUE, "Generating %d buffers", n);
    if UNLIKELY(n <= 0) return;

    if LIKELY(n == 1)
    {
        /* Special handling for the easy and normal case. */
        ALbuffer *buffer = AllocBuffer(context.get());
        if(buffer) buffers[0] = buffer->id;
    }
    else
    {
        /* Store the allocated buffer IDs in a separate local list, to avoid
         * modifying the user storage in case of failure.
         */
        al::vector<ALuint> ids;
        ids.reserve(n);
        do {
            ALbuffer *buffer = AllocBuffer(context.get());
            if(!buffer)
            {
                alDeleteBuffers(static_cast<ALsizei>(ids.size()), ids.data());
                return;
            }

            ids.emplace_back(buffer->id);
        } while(--n);
        std::copy(ids.begin(), ids.end(), buffers);
    }
}
END_API_FUNC

AL_API ALvoid AL_APIENTRY alDeleteBuffers(ALsizei n, const ALuint *buffers)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    if UNLIKELY(n < 0)
        context->setError(AL_INVALID_VALUE, "Deleting %d buffers", n);
    if UNLIKELY(n <= 0) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    /* First try to find any buffers that are invalid or in-use. */
    const ALuint *buffers_end = buffers + n;
    auto invbuf = std::find_if(buffers, buffers_end,
        [device, &context](ALuint bid) -> bool
        {
            if(!bid) return false;
            ALbuffer *ALBuf = LookupBuffer(device, bid);
            if UNLIKELY(!ALBuf)
            {
                context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", bid);
                return true;
            }
            if UNLIKELY(ReadRef(ALBuf->ref) != 0)
            {
                context->setError(AL_INVALID_OPERATION, "Deleting in-use buffer %u", bid);
                return true;
            }
            return false;
        }
    );
    if LIKELY(invbuf == buffers_end)
    {
        /* All good. Delete non-0 buffer IDs. */
        std::for_each(buffers, buffers_end,
            [device](ALuint bid) -> void
            {
                ALbuffer *buffer{bid ? LookupBuffer(device, bid) : nullptr};
                if(buffer) FreeBuffer(device, buffer);
            }
        );
    }
}
END_API_FUNC

AL_API ALboolean AL_APIENTRY alIsBuffer(ALuint buffer)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if LIKELY(context)
    {
        ALCdevice *device{context->mDevice.get()};
        std::lock_guard<std::mutex> _{device->BufferLock};
        if(!buffer || LookupBuffer(device, buffer))
            return AL_TRUE;
    }
    return AL_FALSE;
}
END_API_FUNC


AL_API ALvoid AL_APIENTRY alBufferData(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq)
START_API_FUNC
{ alBufferStorageSOFT(buffer, format, data, size, freq, 0); }
END_API_FUNC

AL_API void AL_APIENTRY alBufferStorageSOFT(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq, ALbitfieldSOFT flags)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    ALbuffer *albuf = LookupBuffer(device, buffer);
    if UNLIKELY(!albuf)
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if UNLIKELY(size < 0)
        context->setError(AL_INVALID_VALUE, "Negative storage size %d", size);
    else if UNLIKELY(freq < 1)
        context->setError(AL_INVALID_VALUE, "Invalid sample rate %d", freq);
    else if UNLIKELY((flags&INVALID_STORAGE_MASK) != 0)
        context->setError(AL_INVALID_VALUE, "Invalid storage flags 0x%x",
            flags&INVALID_STORAGE_MASK);
    else if UNLIKELY((flags&AL_MAP_PERSISTENT_BIT_SOFT) && !(flags&MAP_READ_WRITE_FLAGS))
        context->setError(AL_INVALID_VALUE,
            "Declaring persistently mapped storage without read or write access");
	else if UNLIKELY(albuf->callback)
		context->setError(AL_INVALID_OPERATION,
			"Declaring storage for callback buffer");
    else
    {
        auto usrfmt = DecomposeUserFormat(format);
        if UNLIKELY(!usrfmt)
            context->setError(AL_INVALID_ENUM, "Invalid format 0x%04x", format);
        else
            LoadData(context.get(), albuf, freq, size, usrfmt->channels, usrfmt->type,
                static_cast<const al::byte*>(data), flags);
    }
}
END_API_FUNC

AL_API ALvoid AL_APIENTRY alBufferCallbackSOFT(ALuint buffer, ALenum format, ALsizei freq, ALbitfieldSOFT flags, alSourceFunc_t callback, ALvoid* usr_ptr)
START_API_FUNC
{
	ContextRef context{ GetContextRef() };
	if UNLIKELY(!context) return;

	ALCdevice *device{ context->mDevice.get() };
	std::lock_guard<std::mutex> _{ device->BufferLock };

	ALbuffer *albuf = LookupBuffer(device, buffer);
	if UNLIKELY(!albuf)
		context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
	else if UNLIKELY(freq < 1)
		context->setError(AL_INVALID_VALUE, "Invalid sample rate %d", freq);
	else if UNLIKELY(flags)
		context->setError(AL_INVALID_OPERATION, "Seting flags on callback currently not supported.");
	else if UNLIKELY(!callback)
		context->setError(AL_INVALID_VALUE, "Callback function cannot currently default to filling in stable wave data.");
	else {
		auto usrfmt = DecomposeUserFormat(format);
		if UNLIKELY(!usrfmt)
			context->setError(AL_INVALID_ENUM, "Invalid format 0x%04x", format);
		else if UNLIKELY(usrfmt->type != UserFmtFloat)
			context->setError(AL_INVALID_OPERATION, "alBufferCallbackSOFT currently only supports floating point format.");
		else if UNLIKELY(usrfmt->channels != UserFmtMono)
			context->setError(AL_INVALID_OPERATION, "alBufferCallbackSOFT currently only supports mono format.");
		else {
			SetUpCallback(context.get(), albuf, freq, usrfmt->channels, usrfmt->type, flags, callback, usr_ptr);
		}
	}
}
END_API_FUNC

AL_API void* AL_APIENTRY alMapBufferSOFT(ALuint buffer, ALsizei offset, ALsizei length, ALbitfieldSOFT access)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return nullptr;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    ALbuffer *albuf = LookupBuffer(device, buffer);
    if UNLIKELY(!albuf)
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if UNLIKELY((access&INVALID_MAP_FLAGS) != 0)
        context->setError(AL_INVALID_VALUE, "Invalid map flags 0x%x", access&INVALID_MAP_FLAGS);
    else if UNLIKELY(!(access&MAP_READ_WRITE_FLAGS))
        context->setError(AL_INVALID_VALUE, "Mapping buffer %u without read or write access",
            buffer);
	else if UNLIKELY(albuf->callback)
		context->setError(AL_INVALID_OPERATION,
			"Mapping storage for callback buffer");
    else
    {
        ALbitfieldSOFT unavailable = (albuf->Access^access) & access;
        if UNLIKELY(ReadRef(albuf->ref) != 0 && !(access&AL_MAP_PERSISTENT_BIT_SOFT))
            context->setError(AL_INVALID_OPERATION,
                "Mapping in-use buffer %u without persistent mapping", buffer);
        else if UNLIKELY(albuf->MappedAccess != 0)
            context->setError(AL_INVALID_OPERATION, "Mapping already-mapped buffer %u", buffer);
        else if UNLIKELY((unavailable&AL_MAP_READ_BIT_SOFT))
            context->setError(AL_INVALID_VALUE,
                "Mapping buffer %u for reading without read access", buffer);
        else if UNLIKELY((unavailable&AL_MAP_WRITE_BIT_SOFT))
            context->setError(AL_INVALID_VALUE,
                "Mapping buffer %u for writing without write access", buffer);
        else if UNLIKELY((unavailable&AL_MAP_PERSISTENT_BIT_SOFT))
            context->setError(AL_INVALID_VALUE,
                "Mapping buffer %u persistently without persistent access", buffer);
        else if UNLIKELY(offset < 0 || offset >= albuf->OriginalSize ||
                         length <= 0 || length > albuf->OriginalSize - offset)
            context->setError(AL_INVALID_VALUE, "Mapping invalid range %d+%d for buffer %u",
                offset, length, buffer);
        else
        {
            void *retval = albuf->mData.data() + offset;
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
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    ALbuffer *albuf = LookupBuffer(device, buffer);
    if UNLIKELY(!albuf)
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if UNLIKELY(albuf->MappedAccess == 0)
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
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    ALbuffer *albuf = LookupBuffer(device, buffer);
    if UNLIKELY(!albuf)
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if UNLIKELY(!(albuf->MappedAccess&AL_MAP_WRITE_BIT_SOFT))
        context->setError(AL_INVALID_OPERATION, "Flushing buffer %u while not mapped for writing",
            buffer);
    else if UNLIKELY(offset < albuf->MappedOffset ||
                     offset >= albuf->MappedOffset+albuf->MappedSize ||
                     length <= 0 || length > albuf->MappedOffset+albuf->MappedSize-offset)
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

AL_API ALvoid AL_APIENTRY alBufferSubDataSOFT(ALuint buffer, ALenum format, const ALvoid *data, ALsizei offset, ALsizei length)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    ALbuffer *albuf = LookupBuffer(device, buffer);
    if UNLIKELY(!albuf)
    {
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
        return;
    }
	else if UNLIKELY(albuf->callback)
		context->setError(AL_INVALID_OPERATION,
			"Callingg SubData on callback buffer");

    auto usrfmt = DecomposeUserFormat(format);
    if UNLIKELY(!usrfmt)
    {
        context->setError(AL_INVALID_ENUM, "Invalid format 0x%04x", format);
        return;
    }

    ALsizei unpack_align{albuf->UnpackAlign.load()};
    ALsizei align{SanitizeAlignment(usrfmt->type, unpack_align)};
    if UNLIKELY(align < 1)
        context->setError(AL_INVALID_VALUE, "Invalid unpack alignment %d", unpack_align);
    else if UNLIKELY(long{usrfmt->channels} != long{albuf->mFmtChannels} ||
                     usrfmt->type != albuf->OriginalType)
        context->setError(AL_INVALID_ENUM, "Unpacking data with mismatched format");
    else if UNLIKELY(align != albuf->OriginalAlign)
        context->setError(AL_INVALID_VALUE,
            "Unpacking data with alignment %u does not match original alignment %u", align,
            albuf->OriginalAlign);
    else if UNLIKELY(albuf->MappedAccess != 0)
        context->setError(AL_INVALID_OPERATION, "Unpacking data into mapped buffer %u", buffer);
    else
    {
        ALsizei num_chans{ChannelsFromFmt(albuf->mFmtChannels)};
        ALsizei frame_size{num_chans * BytesFromFmt(albuf->mFmtType)};
        ALsizei byte_align{
            (albuf->OriginalType == UserFmtIMA4) ? ((align-1)/2 + 4) * num_chans :
            (albuf->OriginalType == UserFmtMSADPCM) ? ((align-2)/2 + 7) * num_chans :
            (align * frame_size)
        };

        if UNLIKELY(offset < 0 || length < 0 || offset > albuf->OriginalSize ||
                    length > albuf->OriginalSize-offset)
            context->setError(AL_INVALID_VALUE, "Invalid data sub-range %d+%d on buffer %u",
                offset, length, buffer);
        else if UNLIKELY((offset%byte_align) != 0)
            context->setError(AL_INVALID_VALUE,
                "Sub-range offset %d is not a multiple of frame size %d (%d unpack alignment)",
                offset, byte_align, align);
        else if UNLIKELY((length%byte_align) != 0)
            context->setError(AL_INVALID_VALUE,
                "Sub-range length %d is not a multiple of frame size %d (%d unpack alignment)",
                length, byte_align, align);
        else
        {
            /* offset -> byte offset, length -> sample count */
            offset = offset/byte_align * align * frame_size;
            length = length/byte_align * align;

            void *dst = albuf->mData.data() + offset;
            if(usrfmt->type == UserFmtIMA4 && albuf->mFmtType == FmtShort)
                Convert_ALshort_ALima4(static_cast<ALshort*>(dst),
                    static_cast<const al::byte*>(data), num_chans, length, align);
            else if(usrfmt->type == UserFmtMSADPCM && albuf->mFmtType == FmtShort)
                Convert_ALshort_ALmsadpcm(static_cast<ALshort*>(dst),
                    static_cast<const al::byte*>(data), num_chans, length, align);
            else
            {
                assert(long{usrfmt->type} == static_cast<long>(albuf->mFmtType));
                memcpy(dst, data, length * frame_size);
            }
        }
    }
}
END_API_FUNC


AL_API void AL_APIENTRY alBufferSamplesSOFT(ALuint /*buffer*/, ALuint /*samplerate*/,
    ALenum /*internalformat*/, ALsizei /*samples*/, ALenum /*channels*/, ALenum /*type*/,
    const ALvoid* /*data*/)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    context->setError(AL_INVALID_OPERATION, "alBufferSamplesSOFT not supported");
}
END_API_FUNC

AL_API void AL_APIENTRY alBufferSubSamplesSOFT(ALuint /*buffer*/, ALsizei /*offset*/,
    ALsizei /*samples*/, ALenum /*channels*/, ALenum /*type*/, const ALvoid* /*data*/)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    context->setError(AL_INVALID_OPERATION, "alBufferSubSamplesSOFT not supported");
}
END_API_FUNC

AL_API void AL_APIENTRY alGetBufferSamplesSOFT(ALuint /*buffer*/, ALsizei /*offset*/,
    ALsizei /*samples*/, ALenum /*channels*/, ALenum /*type*/, ALvoid* /*data*/)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    context->setError(AL_INVALID_OPERATION, "alGetBufferSamplesSOFT not supported");
}
END_API_FUNC

AL_API ALboolean AL_APIENTRY alIsBufferFormatSupportedSOFT(ALenum /*format*/)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return AL_FALSE;

    context->setError(AL_INVALID_OPERATION, "alIsBufferFormatSupportedSOFT not supported");
    return AL_FALSE;
}
END_API_FUNC


AL_API void AL_APIENTRY alBufferf(ALuint buffer, ALenum param, ALfloat /*value*/)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};
	ALbuffer* albuf = LookupBuffer(device, buffer);
    if UNLIKELY(albuf == nullptr)
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
	else if UNLIKELY(albuf->callback)
		context->setError(AL_INVALID_OPERATION,
			"Changing parameter for callback buffer not supported");
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
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

	ALbuffer* albuf = LookupBuffer(device, buffer);
	if UNLIKELY(albuf == nullptr)
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
	else if UNLIKELY(albuf->callback)
		context->setError(AL_INVALID_OPERATION,
			"Changing parameter for callback buffer not supported");
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
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

	ALbuffer* albuf = LookupBuffer(device, buffer);
	if UNLIKELY(albuf == nullptr)
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if UNLIKELY(!values)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
	else if UNLIKELY(albuf->callback)
		context->setError(AL_INVALID_OPERATION,
			"Changing parameter for callback buffer not supported");
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
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    ALbuffer *albuf = LookupBuffer(device, buffer);
    if UNLIKELY(!albuf)
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
	else if UNLIKELY(albuf->callback)
		context->setError(AL_INVALID_OPERATION,
			"Changing parameter for callback buffer not supported");
    else switch(param)
    {
    case AL_UNPACK_BLOCK_ALIGNMENT_SOFT:
        if UNLIKELY(value < 0)
            context->setError(AL_INVALID_VALUE, "Invalid unpack block alignment %d", value);
        else
            albuf->UnpackAlign.store(value);
        break;

    case AL_PACK_BLOCK_ALIGNMENT_SOFT:
        if UNLIKELY(value < 0)
            context->setError(AL_INVALID_VALUE, "Invalid pack block alignment %d", value);
        else
            albuf->PackAlign.store(value);
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
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    if UNLIKELY(LookupBuffer(device, buffer) == nullptr)
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
	else if UNLIKELY(albuf->callback)
		context->setError(AL_INVALID_OPERATION,
			"Changing parameter for callback buffer not supported");
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
            alBufferi(buffer, param, values[0]);
            return;
        }
    }

    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    ALbuffer *albuf = LookupBuffer(device, buffer);
    if UNLIKELY(!albuf)
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if UNLIKELY(!values)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
	else if UNLIKELY(albuf->callback)
		context->setError(AL_INVALID_OPERATION,
			"Changing parameter for callback buffer not supported");
    else switch(param)
    {
    case AL_LOOP_POINTS_SOFT:
        if UNLIKELY(ReadRef(albuf->ref) != 0)
            context->setError(AL_INVALID_OPERATION, "Modifying in-use buffer %u's loop points",
                buffer);
        else if UNLIKELY(values[0] < 0 || values[0] >= values[1] ||
            static_cast<ALuint>(values[1]) > albuf->SampleLen)
            context->setError(AL_INVALID_VALUE, "Invalid loop point range %d -> %d on buffer %u",
                values[0], values[1], buffer);
        else
        {
            albuf->LoopStart = values[0];
            albuf->LoopEnd = values[1];
        }
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid buffer integer-vector property 0x%04x", param);
    }
}
END_API_FUNC


AL_API ALvoid AL_APIENTRY alGetBufferf(ALuint buffer, ALenum param, ALfloat *value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    ALbuffer *albuf = LookupBuffer(device, buffer);
    if UNLIKELY(!albuf)
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if UNLIKELY(!value)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    default:
        context->setError(AL_INVALID_ENUM, "Invalid buffer float property 0x%04x", param);
    }
}
END_API_FUNC

AL_API void AL_APIENTRY alGetBuffer3f(ALuint buffer, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    if UNLIKELY(LookupBuffer(device, buffer) == nullptr)
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if UNLIKELY(!value1 || !value2 || !value3)
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
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};

    if UNLIKELY(LookupBuffer(device, buffer) == nullptr)
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if UNLIKELY(!values)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    default:
        context->setError(AL_INVALID_ENUM, "Invalid buffer float-vector property 0x%04x", param);
    }
}
END_API_FUNC


AL_API ALvoid AL_APIENTRY alGetBufferi(ALuint buffer, ALenum param, ALint *value)
START_API_FUNC
{
    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};
    ALbuffer *albuf = LookupBuffer(device, buffer);
    if UNLIKELY(!albuf)
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if UNLIKELY(!value)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    case AL_FREQUENCY:
        *value = albuf->Frequency;
        break;

    case AL_BITS:
        *value = BytesFromFmt(albuf->mFmtType) * 8;
        break;

    case AL_CHANNELS:
        *value = ChannelsFromFmt(albuf->mFmtChannels);
        break;

    case AL_SIZE:
        *value = albuf->SampleLen * FrameSizeFromFmt(albuf->mFmtChannels, albuf->mFmtType);
        break;

    case AL_UNPACK_BLOCK_ALIGNMENT_SOFT:
        *value = albuf->UnpackAlign.load();
        break;

    case AL_PACK_BLOCK_ALIGNMENT_SOFT:
        *value = albuf->PackAlign.load();
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
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};
    if UNLIKELY(LookupBuffer(device, buffer) == nullptr)
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if UNLIKELY(!value1 || !value2 || !value3)
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
        alGetBufferi(buffer, param, values);
        return;
    }

    ContextRef context{GetContextRef()};
    if UNLIKELY(!context) return;

    ALCdevice *device{context->mDevice.get()};
    std::lock_guard<std::mutex> _{device->BufferLock};
    ALbuffer *albuf = LookupBuffer(device, buffer);
    if UNLIKELY(!albuf)
        context->setError(AL_INVALID_NAME, "Invalid buffer ID %u", buffer);
    else if UNLIKELY(!values)
        context->setError(AL_INVALID_VALUE, "NULL pointer");
    else switch(param)
    {
    case AL_LOOP_POINTS_SOFT:
        values[0] = albuf->LoopStart;
        values[1] = albuf->LoopEnd;
        break;

    default:
        context->setError(AL_INVALID_ENUM, "Invalid buffer integer-vector property 0x%04x", param);
    }
}
END_API_FUNC


ALsizei BytesFromUserFmt(UserFmtType type)
{
    switch(type)
    {
    case UserFmtUByte: return sizeof(ALubyte);
    case UserFmtShort: return sizeof(ALshort);
    case UserFmtFloat: return sizeof(ALfloat);
    case UserFmtDouble: return sizeof(ALdouble);
    case UserFmtMulaw: return sizeof(ALubyte);
    case UserFmtAlaw: return sizeof(ALubyte);
    case UserFmtIMA4: break; /* not handled here */
    case UserFmtMSADPCM: break; /* not handled here */
    }
    return 0;
}
ALsizei ChannelsFromUserFmt(UserFmtChannels chans)
{
    switch(chans)
    {
    case UserFmtMono: return 1;
    case UserFmtStereo: return 2;
    case UserFmtRear: return 2;
    case UserFmtQuad: return 4;
    case UserFmtX51: return 6;
    case UserFmtX61: return 7;
    case UserFmtX71: return 8;
    case UserFmtBFormat2D: return 3;
    case UserFmtBFormat3D: return 4;
    }
    return 0;
}

ALsizei BytesFromFmt(FmtType type)
{
    switch(type)
    {
    case FmtUByte: return sizeof(ALubyte);
    case FmtShort: return sizeof(ALshort);
    case FmtFloat: return sizeof(ALfloat);
    case FmtDouble: return sizeof(ALdouble);
    case FmtMulaw: return sizeof(ALubyte);
    case FmtAlaw: return sizeof(ALubyte);
    }
    return 0;
}
ALsizei ChannelsFromFmt(FmtChannels chans)
{
    switch(chans)
    {
    case FmtMono: return 1;
    case FmtStereo: return 2;
    case FmtRear: return 2;
    case FmtQuad: return 4;
    case FmtX51: return 6;
    case FmtX61: return 7;
    case FmtX71: return 8;
    case FmtBFormat2D: return 3;
    case FmtBFormat3D: return 4;
    }
    return 0;
}


BufferSubList::~BufferSubList()
{
    uint64_t usemask{~FreeMask};
    while(usemask)
    {
        ALsizei idx{CTZ64(usemask)};
        al::destroy_at(Buffers+idx);
        usemask &= ~(1_u64 << idx);
    }
    FreeMask = ~usemask;
    al_free(Buffers);
    Buffers = nullptr;
}
