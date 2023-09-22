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

#include "filter.h"

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <vector>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/efx.h"

#include "albit.h"
#include "alc/context.h"
#include "alc/device.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "core/except.h"
#include "direct_defs.h"
#include "opthelpers.h"


namespace {

class filter_exception final : public al::base_exception {
    ALenum mErrorCode;

public:
#ifdef __USE_MINGW_ANSI_STDIO
    [[gnu::format(gnu_printf, 3, 4)]]
#else
    [[gnu::format(printf, 3, 4)]]
#endif
    filter_exception(ALenum code, const char *msg, ...);
    ~filter_exception() override;

    ALenum errorCode() const noexcept { return mErrorCode; }
};

filter_exception::filter_exception(ALenum code, const char* msg, ...) : mErrorCode{code}
{
    std::va_list args;
    va_start(args, msg);
    setMessage(msg, args);
    va_end(args);
}
filter_exception::~filter_exception() = default;


void InitFilterParams(ALfilter *filter, ALenum type)
{
    if(type == AL_FILTER_LOWPASS)
    {
        filter->Gain = AL_LOWPASS_DEFAULT_GAIN;
        filter->GainHF = AL_LOWPASS_DEFAULT_GAINHF;
        filter->HFReference = LOWPASSFREQREF;
        filter->GainLF = 1.0f;
        filter->LFReference = HIGHPASSFREQREF;
        filter->mTypeVariant.emplace<LowpassFilterTable>();
    }
    else if(type == AL_FILTER_HIGHPASS)
    {
        filter->Gain = AL_HIGHPASS_DEFAULT_GAIN;
        filter->GainHF = 1.0f;
        filter->HFReference = LOWPASSFREQREF;
        filter->GainLF = AL_HIGHPASS_DEFAULT_GAINLF;
        filter->LFReference = HIGHPASSFREQREF;
        filter->mTypeVariant.emplace<HighpassFilterTable>();
    }
    else if(type == AL_FILTER_BANDPASS)
    {
        filter->Gain = AL_BANDPASS_DEFAULT_GAIN;
        filter->GainHF = AL_BANDPASS_DEFAULT_GAINHF;
        filter->HFReference = LOWPASSFREQREF;
        filter->GainLF = AL_BANDPASS_DEFAULT_GAINLF;
        filter->LFReference = HIGHPASSFREQREF;
        filter->mTypeVariant.emplace<BandpassFilterTable>();
    }
    else
    {
        filter->Gain = 1.0f;
        filter->GainHF = 1.0f;
        filter->HFReference = LOWPASSFREQREF;
        filter->GainLF = 1.0f;
        filter->LFReference = HIGHPASSFREQREF;
        filter->mTypeVariant.emplace<NullFilterTable>();
    }
    filter->type = type;
}

bool EnsureFilters(ALCdevice *device, size_t needed)
{
    size_t count{std::accumulate(device->FilterList.cbegin(), device->FilterList.cend(), 0_uz,
        [](size_t cur, const FilterSubList &sublist) noexcept -> size_t
        { return cur + static_cast<ALuint>(al::popcount(sublist.FreeMask)); })};

    while(needed > count)
    {
        if(device->FilterList.size() >= 1<<25) UNLIKELY
            return false;

        device->FilterList.emplace_back();
        auto sublist = device->FilterList.end() - 1;
        sublist->FreeMask = ~0_u64;
        sublist->Filters = static_cast<ALfilter*>(al_calloc(alignof(ALfilter), sizeof(ALfilter)*64));
        if(!sublist->Filters) UNLIKELY
        {
            device->FilterList.pop_back();
            return false;
        }
        count += 64;
    }
    return true;
}


ALfilter *AllocFilter(ALCdevice *device)
{
    auto sublist = std::find_if(device->FilterList.begin(), device->FilterList.end(),
        [](const FilterSubList &entry) noexcept -> bool
        { return entry.FreeMask != 0; });
    auto lidx = static_cast<ALuint>(std::distance(device->FilterList.begin(), sublist));
    auto slidx = static_cast<ALuint>(al::countr_zero(sublist->FreeMask));
    ASSUME(slidx < 64);

    ALfilter *filter{al::construct_at(sublist->Filters + slidx)};
    InitFilterParams(filter, AL_FILTER_NULL);

    /* Add 1 to avoid filter ID 0. */
    filter->id = ((lidx<<6) | slidx) + 1;

    sublist->FreeMask &= ~(1_u64 << slidx);

    return filter;
}

void FreeFilter(ALCdevice *device, ALfilter *filter)
{
    device->mFilterNames.erase(filter->id);

    const ALuint id{filter->id - 1};
    const size_t lidx{id >> 6};
    const ALuint slidx{id & 0x3f};

    std::destroy_at(filter);

    device->FilterList[lidx].FreeMask |= 1_u64 << slidx;
}


inline ALfilter *LookupFilter(ALCdevice *device, ALuint id)
{
    const size_t lidx{(id-1) >> 6};
    const ALuint slidx{(id-1) & 0x3f};

    if(lidx >= device->FilterList.size()) UNLIKELY
        return nullptr;
    FilterSubList &sublist = device->FilterList[lidx];
    if(sublist.FreeMask & (1_u64 << slidx)) UNLIKELY
        return nullptr;
    return sublist.Filters + slidx;
}

} // namespace

/* Null filter parameter handlers */
template<>
void FilterTable<NullFilterTable>::setParami(ALfilter*, ALenum param, int)
{ throw filter_exception{AL_INVALID_ENUM, "Invalid null filter property 0x%04x", param}; }
template<>
void FilterTable<NullFilterTable>::setParamiv(ALfilter*, ALenum param, const int*)
{ throw filter_exception{AL_INVALID_ENUM, "Invalid null filter property 0x%04x", param}; }
template<>
void FilterTable<NullFilterTable>::setParamf(ALfilter*, ALenum param, float)
{ throw filter_exception{AL_INVALID_ENUM, "Invalid null filter property 0x%04x", param}; }
template<>
void FilterTable<NullFilterTable>::setParamfv(ALfilter*, ALenum param, const float*)
{ throw filter_exception{AL_INVALID_ENUM, "Invalid null filter property 0x%04x", param}; }
template<>
void FilterTable<NullFilterTable>::getParami(const ALfilter*, ALenum param, int*)
{ throw filter_exception{AL_INVALID_ENUM, "Invalid null filter property 0x%04x", param}; }
template<>
void FilterTable<NullFilterTable>::getParamiv(const ALfilter*, ALenum param, int*)
{ throw filter_exception{AL_INVALID_ENUM, "Invalid null filter property 0x%04x", param}; }
template<>
void FilterTable<NullFilterTable>::getParamf(const ALfilter*, ALenum param, float*)
{ throw filter_exception{AL_INVALID_ENUM, "Invalid null filter property 0x%04x", param}; }
template<>
void FilterTable<NullFilterTable>::getParamfv(const ALfilter*, ALenum param, float*)
{ throw filter_exception{AL_INVALID_ENUM, "Invalid null filter property 0x%04x", param}; }

/* Lowpass parameter handlers */
template<>
void FilterTable<LowpassFilterTable>::setParami(ALfilter*, ALenum param, int)
{ throw filter_exception{AL_INVALID_ENUM, "Invalid low-pass integer property 0x%04x", param}; }
template<>
void FilterTable<LowpassFilterTable>::setParamiv(ALfilter *filter, ALenum param, const int *values)
{ setParami(filter, param, values[0]); }
template<>
void FilterTable<LowpassFilterTable>::setParamf(ALfilter *filter, ALenum param, float val)
{
    switch(param)
    {
    case AL_LOWPASS_GAIN:
        if(!(val >= AL_LOWPASS_MIN_GAIN && val <= AL_LOWPASS_MAX_GAIN))
            throw filter_exception{AL_INVALID_VALUE, "Low-pass gain %f out of range", val};
        filter->Gain = val;
        break;

    case AL_LOWPASS_GAINHF:
        if(!(val >= AL_LOWPASS_MIN_GAINHF && val <= AL_LOWPASS_MAX_GAINHF))
            throw filter_exception{AL_INVALID_VALUE, "Low-pass gainhf %f out of range", val};
        filter->GainHF = val;
        break;

    default:
        throw filter_exception{AL_INVALID_ENUM, "Invalid low-pass float property 0x%04x", param};
    }
}
template<>
void FilterTable<LowpassFilterTable>::setParamfv(ALfilter *filter, ALenum param, const float *vals)
{ setParamf(filter, param, vals[0]); }
template<>
void FilterTable<LowpassFilterTable>::getParami(const ALfilter*, ALenum param, int*)
{ throw filter_exception{AL_INVALID_ENUM, "Invalid low-pass integer property 0x%04x", param}; }
template<>
void FilterTable<LowpassFilterTable>::getParamiv(const ALfilter *filter, ALenum param, int *values)
{ getParami(filter, param, values); }
template<>
void FilterTable<LowpassFilterTable>::getParamf(const ALfilter *filter, ALenum param, float *val)
{
    switch(param)
    {
    case AL_LOWPASS_GAIN:
        *val = filter->Gain;
        break;

    case AL_LOWPASS_GAINHF:
        *val = filter->GainHF;
        break;

    default:
        throw filter_exception{AL_INVALID_ENUM, "Invalid low-pass float property 0x%04x", param};
    }
}
template<>
void FilterTable<LowpassFilterTable>::getParamfv(const ALfilter *filter, ALenum param, float *vals)
{ getParamf(filter, param, vals); }

/* Highpass parameter handlers */
template<>
void FilterTable<HighpassFilterTable>::setParami(ALfilter*, ALenum param, int)
{ throw filter_exception{AL_INVALID_ENUM, "Invalid high-pass integer property 0x%04x", param}; }
template<>
void FilterTable<HighpassFilterTable>::setParamiv(ALfilter *filter, ALenum param, const int *values)
{ setParami(filter, param, values[0]); }
template<>
void FilterTable<HighpassFilterTable>::setParamf(ALfilter *filter, ALenum param, float val)
{
    switch(param)
    {
    case AL_HIGHPASS_GAIN:
        if(!(val >= AL_HIGHPASS_MIN_GAIN && val <= AL_HIGHPASS_MAX_GAIN))
            throw filter_exception{AL_INVALID_VALUE, "High-pass gain %f out of range", val};
        filter->Gain = val;
        break;

    case AL_HIGHPASS_GAINLF:
        if(!(val >= AL_HIGHPASS_MIN_GAINLF && val <= AL_HIGHPASS_MAX_GAINLF))
            throw filter_exception{AL_INVALID_VALUE, "High-pass gainlf %f out of range", val};
        filter->GainLF = val;
        break;

    default:
        throw filter_exception{AL_INVALID_ENUM, "Invalid high-pass float property 0x%04x", param};
    }
}
template<>
void FilterTable<HighpassFilterTable>::setParamfv(ALfilter *filter, ALenum param, const float *vals)
{ setParamf(filter, param, vals[0]); }
template<>
void FilterTable<HighpassFilterTable>::getParami(const ALfilter*, ALenum param, int*)
{ throw filter_exception{AL_INVALID_ENUM, "Invalid high-pass integer property 0x%04x", param}; }
template<>
void FilterTable<HighpassFilterTable>::getParamiv(const ALfilter *filter, ALenum param, int *values)
{ getParami(filter, param, values); }
template<>
void FilterTable<HighpassFilterTable>::getParamf(const ALfilter *filter, ALenum param, float *val)
{
    switch(param)
    {
    case AL_HIGHPASS_GAIN:
        *val = filter->Gain;
        break;

    case AL_HIGHPASS_GAINLF:
        *val = filter->GainLF;
        break;

    default:
        throw filter_exception{AL_INVALID_ENUM, "Invalid high-pass float property 0x%04x", param};
    }
}
template<>
void FilterTable<HighpassFilterTable>::getParamfv(const ALfilter *filter, ALenum param, float *vals)
{ getParamf(filter, param, vals); }

/* Bandpass parameter handlers */
template<>
void FilterTable<BandpassFilterTable>::setParami(ALfilter*, ALenum param, int)
{ throw filter_exception{AL_INVALID_ENUM, "Invalid band-pass integer property 0x%04x", param}; }
template<>
void FilterTable<BandpassFilterTable>::setParamiv(ALfilter *filter, ALenum param, const int *values)
{ setParami(filter, param, values[0]); }
template<>
void FilterTable<BandpassFilterTable>::setParamf(ALfilter *filter, ALenum param, float val)
{
    switch(param)
    {
    case AL_BANDPASS_GAIN:
        if(!(val >= AL_BANDPASS_MIN_GAIN && val <= AL_BANDPASS_MAX_GAIN))
            throw filter_exception{AL_INVALID_VALUE, "Band-pass gain %f out of range", val};
        filter->Gain = val;
        break;

    case AL_BANDPASS_GAINHF:
        if(!(val >= AL_BANDPASS_MIN_GAINHF && val <= AL_BANDPASS_MAX_GAINHF))
            throw filter_exception{AL_INVALID_VALUE, "Band-pass gainhf %f out of range", val};
        filter->GainHF = val;
        break;

    case AL_BANDPASS_GAINLF:
        if(!(val >= AL_BANDPASS_MIN_GAINLF && val <= AL_BANDPASS_MAX_GAINLF))
            throw filter_exception{AL_INVALID_VALUE, "Band-pass gainlf %f out of range", val};
        filter->GainLF = val;
        break;

    default:
        throw filter_exception{AL_INVALID_ENUM, "Invalid band-pass float property 0x%04x", param};
    }
}
template<>
void FilterTable<BandpassFilterTable>::setParamfv(ALfilter *filter, ALenum param, const float *vals)
{ setParamf(filter, param, vals[0]); }
template<>
void FilterTable<BandpassFilterTable>::getParami(const ALfilter*, ALenum param, int*)
{ throw filter_exception{AL_INVALID_ENUM, "Invalid band-pass integer property 0x%04x", param}; }
template<>
void FilterTable<BandpassFilterTable>::getParamiv(const ALfilter *filter, ALenum param, int *values)
{ getParami(filter, param, values); }
template<>
void FilterTable<BandpassFilterTable>::getParamf(const ALfilter *filter, ALenum param, float *val)
{
    switch(param)
    {
    case AL_BANDPASS_GAIN:
        *val = filter->Gain;
        break;

    case AL_BANDPASS_GAINHF:
        *val = filter->GainHF;
        break;

    case AL_BANDPASS_GAINLF:
        *val = filter->GainLF;
        break;

    default:
        throw filter_exception{AL_INVALID_ENUM, "Invalid band-pass float property 0x%04x", param};
    }
}
template<>
void FilterTable<BandpassFilterTable>::getParamfv(const ALfilter *filter, ALenum param, float *vals)
{ getParamf(filter, param, vals); }


AL_API DECL_FUNC2(void, alGenFilters, ALsizei, ALuint*)
FORCE_ALIGN void AL_APIENTRY alGenFiltersDirect(ALCcontext *context, ALsizei n, ALuint *filters) noexcept
{
    if(n < 0) UNLIKELY
        context->setError(AL_INVALID_VALUE, "Generating %d filters", n);
    if(n <= 0) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->FilterLock};
    if(!EnsureFilters(device, static_cast<ALuint>(n)))
    {
        context->setError(AL_OUT_OF_MEMORY, "Failed to allocate %d filter%s", n, (n==1)?"":"s");
        return;
    }

    if(n == 1) LIKELY
    {
        /* Special handling for the easy and normal case. */
        ALfilter *filter{AllocFilter(device)};
        if(filter) filters[0] = filter->id;
    }
    else
    {
        /* Store the allocated buffer IDs in a separate local list, to avoid
         * modifying the user storage in case of failure.
         */
        std::vector<ALuint> ids;
        ids.reserve(static_cast<ALuint>(n));
        do {
            ALfilter *filter{AllocFilter(device)};
            ids.emplace_back(filter->id);
        } while(--n);
        std::copy(ids.begin(), ids.end(), filters);
    }
}

AL_API DECL_FUNC2(void, alDeleteFilters, ALsizei, const ALuint*)
FORCE_ALIGN void AL_APIENTRY alDeleteFiltersDirect(ALCcontext *context, ALsizei n,
    const ALuint *filters) noexcept
{
    if(n < 0) UNLIKELY
        context->setError(AL_INVALID_VALUE, "Deleting %d filters", n);
    if(n <= 0) UNLIKELY return;

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->FilterLock};

    /* First try to find any filters that are invalid. */
    auto validate_filter = [device](const ALuint fid) -> bool
    { return !fid || LookupFilter(device, fid) != nullptr; };

    const ALuint *filters_end = filters + n;
    auto invflt = std::find_if_not(filters, filters_end, validate_filter);
    if(invflt != filters_end) UNLIKELY
    {
        context->setError(AL_INVALID_NAME, "Invalid filter ID %u", *invflt);
        return;
    }

    /* All good. Delete non-0 filter IDs. */
    auto delete_filter = [device](const ALuint fid) -> void
    {
        ALfilter *filter{fid ? LookupFilter(device, fid) : nullptr};
        if(filter) FreeFilter(device, filter);
    };
    std::for_each(filters, filters_end, delete_filter);
}

AL_API DECL_FUNC1(ALboolean, alIsFilter, ALuint)
FORCE_ALIGN ALboolean AL_APIENTRY alIsFilterDirect(ALCcontext *context, ALuint filter) noexcept
{
    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->FilterLock};
    if(!filter || LookupFilter(device, filter))
        return AL_TRUE;
    return AL_FALSE;
}


AL_API DECL_FUNC3(void, alFilteri, ALuint, ALenum, ALint)
FORCE_ALIGN void AL_APIENTRY alFilteriDirect(ALCcontext *context, ALuint filter, ALenum param,
    ALint value) noexcept
{
    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->FilterLock};

    ALfilter *alfilt{LookupFilter(device, filter)};
    if(!alfilt) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid filter ID %u", filter);
    else if(param == AL_FILTER_TYPE)
    {
        if(value == AL_FILTER_NULL || value == AL_FILTER_LOWPASS
            || value == AL_FILTER_HIGHPASS || value == AL_FILTER_BANDPASS)
            InitFilterParams(alfilt, value);
        else
            context->setError(AL_INVALID_VALUE, "Invalid filter type 0x%04x", value);
    }
    else try
    {
        /* Call the appropriate handler */
        std::visit([alfilt,param,value](auto&& thunk){thunk.setParami(alfilt, param, value);},
            alfilt->mTypeVariant);
    }
    catch(filter_exception &e) {
        context->setError(e.errorCode(), "%s", e.what());
    }
}

AL_API DECL_FUNC3(void, alFilteriv, ALuint, ALenum, const ALint*)
FORCE_ALIGN void AL_APIENTRY alFilterivDirect(ALCcontext *context, ALuint filter, ALenum param,
    const ALint *values) noexcept
{
    switch(param)
    {
    case AL_FILTER_TYPE:
        alFilteriDirect(context, filter, param, values[0]);
        return;
    }

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->FilterLock};

    ALfilter *alfilt{LookupFilter(device, filter)};
    if(!alfilt) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid filter ID %u", filter);
    else try
    {
        /* Call the appropriate handler */
        std::visit([alfilt,param,values](auto&& thunk){thunk.setParamiv(alfilt, param, values);},
            alfilt->mTypeVariant);
    }
    catch(filter_exception &e) {
        context->setError(e.errorCode(), "%s", e.what());
    }
}

AL_API DECL_FUNC3(void, alFilterf, ALuint, ALenum, ALfloat)
FORCE_ALIGN void AL_APIENTRY alFilterfDirect(ALCcontext *context, ALuint filter, ALenum param,
    ALfloat value) noexcept
{
    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->FilterLock};

    ALfilter *alfilt{LookupFilter(device, filter)};
    if(!alfilt) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid filter ID %u", filter);
    else try
    {
        /* Call the appropriate handler */
        std::visit([alfilt,param,value](auto&& thunk){thunk.setParamf(alfilt, param, value);},
            alfilt->mTypeVariant);
    }
    catch(filter_exception &e) {
        context->setError(e.errorCode(), "%s", e.what());
    }
}

AL_API DECL_FUNC3(void, alFilterfv, ALuint, ALenum, const ALfloat*)
FORCE_ALIGN void AL_APIENTRY alFilterfvDirect(ALCcontext *context, ALuint filter, ALenum param,
    const ALfloat *values) noexcept
{
    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->FilterLock};

    ALfilter *alfilt{LookupFilter(device, filter)};
    if(!alfilt) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid filter ID %u", filter);
    else try
    {
        /* Call the appropriate handler */
        std::visit([alfilt,param,values](auto&& thunk){thunk.setParamfv(alfilt, param, values);},
            alfilt->mTypeVariant);
    }
    catch(filter_exception &e) {
        context->setError(e.errorCode(), "%s", e.what());
    }
}

AL_API DECL_FUNC3(void, alGetFilteri, ALuint, ALenum, ALint*)
FORCE_ALIGN void AL_APIENTRY alGetFilteriDirect(ALCcontext *context, ALuint filter, ALenum param,
    ALint *value) noexcept
{
    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->FilterLock};

    const ALfilter *alfilt{LookupFilter(device, filter)};
    if(!alfilt) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid filter ID %u", filter);
    else if(param == AL_FILTER_TYPE)
        *value = alfilt->type;
    else try
    {
        /* Call the appropriate handler */
        std::visit([alfilt,param,value](auto&& thunk){thunk.getParami(alfilt, param, value);},
            alfilt->mTypeVariant);
    }
    catch(filter_exception &e) {
        context->setError(e.errorCode(), "%s", e.what());
    }
}

AL_API DECL_FUNC3(void, alGetFilteriv, ALuint, ALenum, ALint*)
FORCE_ALIGN void AL_APIENTRY alGetFilterivDirect(ALCcontext *context, ALuint filter, ALenum param,
    ALint *values) noexcept
{
    switch(param)
    {
    case AL_FILTER_TYPE:
        alGetFilteriDirect(context, filter, param, values);
        return;
    }

    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->FilterLock};

    const ALfilter *alfilt{LookupFilter(device, filter)};
    if(!alfilt) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid filter ID %u", filter);
    else try
    {
        /* Call the appropriate handler */
        std::visit([alfilt,param,values](auto&& thunk){thunk.getParamiv(alfilt, param, values);},
            alfilt->mTypeVariant);
    }
    catch(filter_exception &e) {
        context->setError(e.errorCode(), "%s", e.what());
    }
}

AL_API DECL_FUNC3(void, alGetFilterf, ALuint, ALenum, ALfloat*)
FORCE_ALIGN void AL_APIENTRY alGetFilterfDirect(ALCcontext *context, ALuint filter, ALenum param,
    ALfloat *value) noexcept
{
    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->FilterLock};

    const ALfilter *alfilt{LookupFilter(device, filter)};
    if(!alfilt) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid filter ID %u", filter);
    else try
    {
        /* Call the appropriate handler */
        std::visit([alfilt,param,value](auto&& thunk){thunk.getParamf(alfilt, param, value);},
            alfilt->mTypeVariant);
    }
    catch(filter_exception &e) {
        context->setError(e.errorCode(), "%s", e.what());
    }
}

AL_API DECL_FUNC3(void, alGetFilterfv, ALuint, ALenum, ALfloat*)
FORCE_ALIGN void AL_APIENTRY alGetFilterfvDirect(ALCcontext *context, ALuint filter, ALenum param,
    ALfloat *values) noexcept
{
    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->FilterLock};

    const ALfilter *alfilt{LookupFilter(device, filter)};
    if(!alfilt) UNLIKELY
        context->setError(AL_INVALID_NAME, "Invalid filter ID %u", filter);
    else try
    {
        /* Call the appropriate handler */
        std::visit([alfilt,param,values](auto&& thunk){thunk.getParamfv(alfilt, param, values);},
            alfilt->mTypeVariant);
    }
    catch(filter_exception &e) {
        context->setError(e.errorCode(), "%s", e.what());
    }
}


void ALfilter::SetName(ALCcontext *context, ALuint id, std::string_view name)
{
    ALCdevice *device{context->mALDevice.get()};
    std::lock_guard<std::mutex> _{device->FilterLock};

    auto filter = LookupFilter(device, id);
    if(!filter) UNLIKELY
        return context->setError(AL_INVALID_NAME, "Invalid filter ID %u", id);

    device->mFilterNames.insert_or_assign(id, name);
}


FilterSubList::~FilterSubList()
{
    if(!Filters)
        return;

    uint64_t usemask{~FreeMask};
    while(usemask)
    {
        const int idx{al::countr_zero(usemask)};
        std::destroy_at(Filters+idx);
        usemask &= ~(1_u64 << idx);
    }
    FreeMask = ~usemask;
    al_free(Filters);
    Filters = nullptr;
}
