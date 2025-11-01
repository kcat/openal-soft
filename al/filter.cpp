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
#include <bit>
#include <cstdarg>
#include <cstdio>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <span>
#include <unordered_map>
#include <vector>

#include "AL/al.h"
#include "AL/efx.h"

#include "alc/context.h"
#include "alc/device.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "core/except.h"
#include "core/logging.h"
#include "direct_defs.h"
#include "gsl/gsl"
#include "opthelpers.h"

using uint = unsigned int;


/* Null filter parameter handlers */
template<>
void FilterTable<NullFilterTable>::setParami(gsl::not_null<al::Context*> context, gsl::not_null<al::Filter*>, ALenum param, i32)
{ context->throw_error(AL_INVALID_ENUM, "Invalid null filter property {:#04x}", as_unsigned(param)); }
template<>
void FilterTable<NullFilterTable>::setParamiv(gsl::not_null<al::Context*> context, gsl::not_null<al::Filter*>, ALenum param, i32 const*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid null filter property {:#04x}", as_unsigned(param)); }
template<>
void FilterTable<NullFilterTable>::setParamf(gsl::not_null<al::Context*> context, gsl::not_null<al::Filter*>, ALenum param, f32)
{ context->throw_error(AL_INVALID_ENUM, "Invalid null filter property {:#04x}", as_unsigned(param)); }
template<>
void FilterTable<NullFilterTable>::setParamfv(gsl::not_null<al::Context*> context, gsl::not_null<al::Filter*>, ALenum param, f32 const*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid null filter property {:#04x}", as_unsigned(param)); }
template<>
void FilterTable<NullFilterTable>::getParami(gsl::not_null<al::Context*> context, gsl::not_null<const al::Filter*>, ALenum param, i32*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid null filter property {:#04x}", as_unsigned(param)); }
template<>
void FilterTable<NullFilterTable>::getParamiv(gsl::not_null<al::Context*> context, gsl::not_null<const al::Filter*>, ALenum param, i32*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid null filter property {:#04x}", as_unsigned(param)); }
template<>
void FilterTable<NullFilterTable>::getParamf(gsl::not_null<al::Context*> context, gsl::not_null<const al::Filter*>, ALenum param, f32*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid null filter property {:#04x}", as_unsigned(param)); }
template<>
void FilterTable<NullFilterTable>::getParamfv(gsl::not_null<al::Context*> context, gsl::not_null<const al::Filter*>, ALenum param, f32*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid null filter property {:#04x}", as_unsigned(param)); }

/* Lowpass parameter handlers */
template<>
void FilterTable<LowpassFilterTable>::setParami(gsl::not_null<al::Context*> context, gsl::not_null<al::Filter*>, ALenum param, i32)
{ context->throw_error(AL_INVALID_ENUM, "Invalid low-pass integer property {:#04x}", as_unsigned(param)); }
template<>
void FilterTable<LowpassFilterTable>::setParamiv(gsl::not_null<al::Context*> context, gsl::not_null<al::Filter*> filter, ALenum param, i32 const *values)
{ setParami(context, filter, param, *values); }
template<>
void FilterTable<LowpassFilterTable>::setParamf(gsl::not_null<al::Context*> context, gsl::not_null<al::Filter*> filter, ALenum param, f32 val)
{
    switch(param)
    {
    case AL_LOWPASS_GAIN:
        if(!(val >= AL_LOWPASS_MIN_GAIN && val <= AL_LOWPASS_MAX_GAIN))
            context->throw_error(AL_INVALID_VALUE, "Low-pass gain {} out of range", val);
        filter->mGain = val;
        return;

    case AL_LOWPASS_GAINHF:
        if(!(val >= AL_LOWPASS_MIN_GAINHF && val <= AL_LOWPASS_MAX_GAINHF))
            context->throw_error(AL_INVALID_VALUE, "Low-pass gainhf {} out of range", val);
        filter->mGainHF = val;
        return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid low-pass float property {:#04x}",
        as_unsigned(param));
}
template<>
void FilterTable<LowpassFilterTable>::setParamfv(gsl::not_null<al::Context*> context, gsl::not_null<al::Filter*> filter, ALenum param, f32 const *vals)
{ setParamf(context, filter, param, *vals); }
template<>
void FilterTable<LowpassFilterTable>::getParami(gsl::not_null<al::Context*> context, gsl::not_null<const al::Filter*>, ALenum param, i32*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid low-pass integer property {:#04x}", as_unsigned(param)); }
template<>
void FilterTable<LowpassFilterTable>::getParamiv(gsl::not_null<al::Context*> context, gsl::not_null<const al::Filter*> filter, ALenum param, i32 *values)
{ getParami(context, filter, param, values); }
template<>
void FilterTable<LowpassFilterTable>::getParamf(gsl::not_null<al::Context*> context, gsl::not_null<const al::Filter*> filter, ALenum param, f32 *val)
{
    switch(param)
    {
    case AL_LOWPASS_GAIN: *val = filter->mGain; return;
    case AL_LOWPASS_GAINHF: *val = filter->mGainHF; return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid low-pass float property {:#04x}",
        as_unsigned(param));
}
template<>
void FilterTable<LowpassFilterTable>::getParamfv(gsl::not_null<al::Context*> context, gsl::not_null<const al::Filter*> filter, ALenum param, f32 *vals)
{ getParamf(context, filter, param, vals); }

/* Highpass parameter handlers */
template<>
void FilterTable<HighpassFilterTable>::setParami(gsl::not_null<al::Context*> context, gsl::not_null<al::Filter*>, ALenum param, i32)
{ context->throw_error(AL_INVALID_ENUM, "Invalid high-pass integer property {:#04x}", as_unsigned(param)); }
template<>
void FilterTable<HighpassFilterTable>::setParamiv(gsl::not_null<al::Context*> context, gsl::not_null<al::Filter*> filter, ALenum param, i32 const *values)
{ setParami(context, filter, param, *values); }
template<>
void FilterTable<HighpassFilterTable>::setParamf(gsl::not_null<al::Context*> context, gsl::not_null<al::Filter*> filter, ALenum param, f32 val)
{
    switch(param)
    {
    case AL_HIGHPASS_GAIN:
        if(!(val >= AL_HIGHPASS_MIN_GAIN && val <= AL_HIGHPASS_MAX_GAIN))
            context->throw_error(AL_INVALID_VALUE, "High-pass gain {} out of range", val);
        filter->mGain = val;
        return;

    case AL_HIGHPASS_GAINLF:
        if(!(val >= AL_HIGHPASS_MIN_GAINLF && val <= AL_HIGHPASS_MAX_GAINLF))
            context->throw_error(AL_INVALID_VALUE, "High-pass gainlf {} out of range", val);
        filter->mGainLF = val;
        return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid high-pass float property {:#04x}",
        as_unsigned(param));
}
template<>
void FilterTable<HighpassFilterTable>::setParamfv(gsl::not_null<al::Context*> context, gsl::not_null<al::Filter*> filter, ALenum param, f32 const *vals)
{ setParamf(context, filter, param, *vals); }
template<>
void FilterTable<HighpassFilterTable>::getParami(gsl::not_null<al::Context*> context, gsl::not_null<const al::Filter*>, ALenum param, i32*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid high-pass integer property {:#04x}", as_unsigned(param)); }
template<>
void FilterTable<HighpassFilterTable>::getParamiv(gsl::not_null<al::Context*> context, gsl::not_null<const al::Filter*> filter, ALenum param, i32 *values)
{ getParami(context, filter, param, values); }
template<>
void FilterTable<HighpassFilterTable>::getParamf(gsl::not_null<al::Context*> context, gsl::not_null<const al::Filter*> filter, ALenum param, f32 *val)
{
    switch(param)
    {
    case AL_HIGHPASS_GAIN: *val = filter->mGain; return;
    case AL_HIGHPASS_GAINLF: *val = filter->mGainLF; return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid high-pass float property {:#04x}",
        as_unsigned(param));
}
template<>
void FilterTable<HighpassFilterTable>::getParamfv(gsl::not_null<al::Context*> context, gsl::not_null<const al::Filter*> filter, ALenum param, f32 *vals)
{ getParamf(context, filter, param, vals); }

/* Bandpass parameter handlers */
template<>
void FilterTable<BandpassFilterTable>::setParami(gsl::not_null<al::Context*> context, gsl::not_null<al::Filter*>, ALenum param, i32)
{ context->throw_error(AL_INVALID_ENUM, "Invalid band-pass integer property {:#04x}", as_unsigned(param)); }
template<>
void FilterTable<BandpassFilterTable>::setParamiv(gsl::not_null<al::Context*> context, gsl::not_null<al::Filter*> filter, ALenum param, i32 const *values)
{ setParami(context, filter, param, *values); }
template<>
void FilterTable<BandpassFilterTable>::setParamf(gsl::not_null<al::Context*> context, gsl::not_null<al::Filter*> filter, ALenum param, f32 val)
{
    switch(param)
    {
    case AL_BANDPASS_GAIN:
        if(!(val >= AL_BANDPASS_MIN_GAIN && val <= AL_BANDPASS_MAX_GAIN))
            context->throw_error(AL_INVALID_VALUE, "Band-pass gain {} out of range", val);
        filter->mGain = val;
        return;

    case AL_BANDPASS_GAINHF:
        if(!(val >= AL_BANDPASS_MIN_GAINHF && val <= AL_BANDPASS_MAX_GAINHF))
            context->throw_error(AL_INVALID_VALUE, "Band-pass gainhf {} out of range", val);
        filter->mGainHF = val;
        return;

    case AL_BANDPASS_GAINLF:
        if(!(val >= AL_BANDPASS_MIN_GAINLF && val <= AL_BANDPASS_MAX_GAINLF))
            context->throw_error(AL_INVALID_VALUE, "Band-pass gainlf {} out of range", val);
        filter->mGainLF = val;
        return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid band-pass float property {:#04x}",
        as_unsigned(param));
}
template<>
void FilterTable<BandpassFilterTable>::setParamfv(gsl::not_null<al::Context*> context, gsl::not_null<al::Filter*> filter, ALenum param, f32 const *vals)
{ setParamf(context, filter, param, *vals); }
template<>
void FilterTable<BandpassFilterTable>::getParami(gsl::not_null<al::Context*> context, gsl::not_null<const al::Filter*>, ALenum param, i32*)
{ context->throw_error(AL_INVALID_ENUM, "Invalid band-pass integer property {:#04x}", as_unsigned(param)); }
template<>
void FilterTable<BandpassFilterTable>::getParamiv(gsl::not_null<al::Context*> context, gsl::not_null<const al::Filter*> filter, ALenum param, i32 *values)
{ getParami(context, filter, param, values); }
template<>
void FilterTable<BandpassFilterTable>::getParamf(gsl::not_null<al::Context*> context, gsl::not_null<const al::Filter*> filter, ALenum param, f32 *val)
{
    switch(param)
    {
    case AL_BANDPASS_GAIN: *val = filter->mGain; return;
    case AL_BANDPASS_GAINHF: *val = filter->mGainHF; return;
    case AL_BANDPASS_GAINLF: *val = filter->mGainLF; return;
    }
    context->throw_error(AL_INVALID_ENUM, "Invalid band-pass float property {:#04x}",
        as_unsigned(param));
}
template<>
void FilterTable<BandpassFilterTable>::getParamfv(gsl::not_null<al::Context*> context, gsl::not_null<al::Filter const*> filter, ALenum param, f32 *vals)
{ getParamf(context, filter, param, vals); }


namespace {

using SubListAllocator = al::allocator<std::array<al::Filter,64>>;


void InitFilterParams(gsl::not_null<al::Filter*> const filter, ALenum const type)
{
    if(type == AL_FILTER_LOWPASS)
    {
        filter->mGain = AL_LOWPASS_DEFAULT_GAIN;
        filter->mGainHF = AL_LOWPASS_DEFAULT_GAINHF;
        filter->mHFReference = LowPassFreqRef;
        filter->mGainLF = 1.0f;
        filter->mLFReference = HighPassFreqRef;
        filter->mTypeVariant.emplace<LowpassFilterTable>();
    }
    else if(type == AL_FILTER_HIGHPASS)
    {
        filter->mGain = AL_HIGHPASS_DEFAULT_GAIN;
        filter->mGainHF = 1.0f;
        filter->mHFReference = LowPassFreqRef;
        filter->mGainLF = AL_HIGHPASS_DEFAULT_GAINLF;
        filter->mLFReference = HighPassFreqRef;
        filter->mTypeVariant.emplace<HighpassFilterTable>();
    }
    else if(type == AL_FILTER_BANDPASS)
    {
        filter->mGain = AL_BANDPASS_DEFAULT_GAIN;
        filter->mGainHF = AL_BANDPASS_DEFAULT_GAINHF;
        filter->mHFReference = LowPassFreqRef;
        filter->mGainLF = AL_BANDPASS_DEFAULT_GAINLF;
        filter->mLFReference = HighPassFreqRef;
        filter->mTypeVariant.emplace<BandpassFilterTable>();
    }
    else
    {
        filter->mGain = 1.0f;
        filter->mGainHF = 1.0f;
        filter->mHFReference = LowPassFreqRef;
        filter->mGainLF = 1.0f;
        filter->mLFReference = HighPassFreqRef;
        filter->mTypeVariant.emplace<NullFilterTable>();
    }
    filter->mType = type;
}

[[nodiscard]]
auto EnsureFilters(gsl::not_null<al::Device*> device, size_t needed) noexcept -> bool
try {
    auto count = std::accumulate(device->FilterList.cbegin(), device->FilterList.cend(), 0_uz,
        [](size_t cur, const FilterSubList &sublist) noexcept -> size_t
        { return cur + gsl::narrow_cast<uint>(std::popcount(sublist.mFreeMask)); });

    while(needed > count)
    {
        if(device->FilterList.size() >= 1<<25) [[unlikely]]
            return false;

        auto sublist = FilterSubList{};
        sublist.mFreeMask = ~0_u64;
        sublist.mFilters = SubListAllocator{}.allocate(1);
        device->FilterList.emplace_back(std::move(sublist));
        count += std::tuple_size_v<SubListAllocator::value_type>;
    }
    return true;
}
catch(...) {
    return false;
}


[[nodiscard]]
auto AllocFilter(gsl::not_null<al::Device*> const device) noexcept -> gsl::not_null<al::Filter*>
{
    auto const sublist = std::ranges::find_if(device->FilterList, &FilterSubList::mFreeMask);
    auto const lidx = gsl::narrow_cast<u32>(std::distance(device->FilterList.begin(), sublist));
    auto const slidx = gsl::narrow_cast<u32>(std::countr_zero(sublist->mFreeMask));
    ASSUME(slidx < 64);

    auto filter = gsl::make_not_null(std::construct_at(
        std::to_address(std::next(sublist->mFilters->begin(), slidx))));
    InitFilterParams(filter, AL_FILTER_NULL);

    /* Add 1 to avoid filter ID 0. */
    filter->mId = ((lidx<<6) | slidx) + 1;

    sublist->mFreeMask &= ~(1_u64 << slidx);

    return filter;
}

void FreeFilter(gsl::not_null<al::Device*> const device, gsl::not_null<al::Filter*> const filter)
{
    device->mFilterNames.erase(filter->mId);

    const auto id = filter->mId - 1;
    const auto lidx = id >> 6;
    const auto slidx = id & 0x3f;

    std::destroy_at(filter.get());

    device->FilterList[lidx].mFreeMask |= 1_u64 << slidx;
}

[[nodiscard]]
auto LookupFilter(std::nothrow_t, gsl::not_null<al::Device*> const device, u32 const id) noexcept
    -> al::Filter*
{
    const auto lidx = (id-1) >> 6;
    const auto slidx = (id-1) & 0x3f;

    if(lidx >= device->FilterList.size()) [[unlikely]]
        return nullptr;
    auto &sublist = device->FilterList[lidx];
    if(sublist.mFreeMask & (1_u64 << slidx)) [[unlikely]]
        return nullptr;
    return std::to_address(std::next(sublist.mFilters->begin(), slidx));
}

[[nodiscard]]
auto LookupFilter(gsl::not_null<al::Context*> const context, u32 const id)
    -> gsl::not_null<al::Filter*>
{
    if(auto *const filter = LookupFilter(std::nothrow, al::get_not_null(context->mALDevice), id))
        [[likely]] return gsl::make_not_null(filter);
    context->throw_error(AL_INVALID_NAME, "Invalid filter ID {}", id);
}


void alGenFilters(gsl::not_null<al::Context*> context, ALsizei n, ALuint *filters) noexcept
try {
    if(n < 0)
        context->throw_error(AL_INVALID_VALUE, "Generating {} filters", n);
    if(n <= 0) [[unlikely]] return;

    auto const device = al::get_not_null(context->mALDevice);
    auto filterlock = std::lock_guard{device->FilterLock};

    const auto fids = std::views::counted(filters, n);
    if(!EnsureFilters(device, fids.size()))
        context->throw_error(AL_OUT_OF_MEMORY, "Failed to allocate {} filter{}", n,
            (n==1) ? "" : "s");

    std::ranges::generate(fids, [device]{ return AllocFilter(device)->mId; });
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alDeleteFilters(gsl::not_null<al::Context*> context, ALsizei n, const ALuint *filters)
    noexcept
try {
    if(n < 0)
        context->throw_error(AL_INVALID_VALUE, "Deleting {} filters", n);
    if(n <= 0) [[unlikely]] return;

    auto const device = al::get_not_null(context->mALDevice);
    auto filterlock = std::lock_guard{device->FilterLock};

    /* First try to find any filters that are invalid. */
    const auto fids = std::views::counted(filters, n);
    std::ranges::for_each(fids, [context](const ALuint fid)
    { if(fid != 0) std::ignore = LookupFilter(context, fid); });

    /* All good. Delete non-0 filter IDs. */
    std::ranges::for_each(fids, [device](const ALuint fid)
    {
        if(auto *filter = LookupFilter(std::nothrow, device, fid))
            FreeFilter(device, gsl::make_not_null(filter));
    });
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

auto alIsFilter(gsl::not_null<al::Context*> context, ALuint filter) noexcept -> ALboolean
{
    auto const device = al::get_not_null(context->mALDevice);
    auto filterlock = std::lock_guard{device->FilterLock};
    if(filter == 0 || LookupFilter(std::nothrow, device, filter) != nullptr)
        return AL_TRUE;
    return AL_FALSE;
}


void alFilteri(gsl::not_null<al::Context*> context, ALuint filter, ALenum param, ALint value)
    noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto filterlock = std::lock_guard{device->FilterLock};

    auto const alfilt = LookupFilter(context, filter);
    switch(param)
    {
    case AL_FILTER_TYPE:
        if(!(value == AL_FILTER_NULL || value == AL_FILTER_LOWPASS
            || value == AL_FILTER_HIGHPASS || value == AL_FILTER_BANDPASS))
            context->throw_error(AL_INVALID_VALUE, "Invalid filter type {:#04x}",
                as_unsigned(value));
        InitFilterParams(alfilt, value);
        return;
    }

    /* Call the appropriate handler */
    std::visit([context,alfilt,param,value](auto&& thunk)
    { thunk.setParami(context, alfilt, param, value); }, alfilt->mTypeVariant);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alFilteriv(gsl::not_null<al::Context*> context, ALuint filter, ALenum param,
    const ALint *values) noexcept
try {
    switch(param)
    {
    case AL_FILTER_TYPE:
        alFilteri(context, filter, param, *values);
        return;
    }

    auto const device = al::get_not_null(context->mALDevice);
    auto filterlock = std::lock_guard{device->FilterLock};

    auto const alfilt = LookupFilter(context, filter);

    /* Call the appropriate handler */
    std::visit([context,alfilt,param,values](auto&& thunk)
    { thunk.setParamiv(context, alfilt, param, values); }, alfilt->mTypeVariant);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alFilterf(gsl::not_null<al::Context*> context, ALuint filter, ALenum param, ALfloat value)
    noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto filterlock = std::lock_guard{device->FilterLock};

    auto const alfilt = LookupFilter(context, filter);

    /* Call the appropriate handler */
    std::visit([context,alfilt,param,value](auto&& thunk)
    { thunk.setParamf(context, alfilt, param, value); }, alfilt->mTypeVariant);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alFilterfv(gsl::not_null<al::Context*> context, ALuint filter, ALenum param,
    const ALfloat *values) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto filterlock = std::lock_guard{device->FilterLock};

    auto const alfilt = LookupFilter(context, filter);

    /* Call the appropriate handler */
    std::visit([context,alfilt,param,values](auto&& thunk)
    { thunk.setParamfv(context, alfilt, param, values); }, alfilt->mTypeVariant);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetFilteri(gsl::not_null<al::Context*> context, ALuint filter, ALenum param, ALint *value)
    noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto filterlock = std::lock_guard{device->FilterLock};

    auto const alfilt = LookupFilter(context, filter);

    switch(param)
    {
    case AL_FILTER_TYPE: *value = alfilt->mType; return;
    }

    /* Call the appropriate handler */
    std::visit([context,alfilt,param,value](auto&& thunk)
    { thunk.getParami(context, alfilt, param, value); }, alfilt->mTypeVariant);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetFilteriv(gsl::not_null<al::Context*> context, ALuint filter, ALenum param, ALint *values)
    noexcept
try {
    switch(param)
    {
    case AL_FILTER_TYPE:
        alGetFilteri(context, filter, param, values);
        return;
    }

    auto const device = al::get_not_null(context->mALDevice);
    auto filterlock = std::lock_guard{device->FilterLock};

    auto const alfilt = LookupFilter(context, filter);

    /* Call the appropriate handler */
    std::visit([context,alfilt,param,values](auto&& thunk)
    { thunk.getParamiv(context, alfilt, param, values); }, alfilt->mTypeVariant);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetFilterf(gsl::not_null<al::Context*> context, ALuint filter, ALenum param, ALfloat *value)
    noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto filterlock = std::lock_guard{device->FilterLock};

    auto const alfilt = LookupFilter(context, filter);

    /* Call the appropriate handler */
    std::visit([context,alfilt,param,value](auto&& thunk)
    { thunk.getParamf(context, alfilt, param, value); }, alfilt->mTypeVariant);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void alGetFilterfv(gsl::not_null<al::Context*> context, ALuint filter, ALenum param,
    ALfloat *values) noexcept
try {
    auto const device = al::get_not_null(context->mALDevice);
    auto filterlock = std::lock_guard{device->FilterLock};

    auto const alfilt = LookupFilter(context, filter);

    /* Call the appropriate handler */
    std::visit([context,alfilt,param,values](auto&& thunk)
    { thunk.getParamfv(context, alfilt, param, values); }, alfilt->mTypeVariant);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

} // namespace

AL_API DECL_FUNC2(void, alGenFilters, ALsizei,n, ALuint*,filters)
AL_API DECL_FUNC2(void, alDeleteFilters, ALsizei,n, const ALuint*,filters)
AL_API DECL_FUNC1(ALboolean, alIsFilter, ALuint,filter)

AL_API DECL_FUNC3(void, alFilteri, ALuint,filter, ALenum,param, ALint,value)
AL_API DECL_FUNC3(void, alFilteriv, ALuint,filter, ALenum,param, const ALint*,values)
AL_API DECL_FUNC3(void, alFilterf, ALuint,filter, ALenum,param, ALfloat,value)
AL_API DECL_FUNC3(void, alFilterfv, ALuint,filter, ALenum,param, const ALfloat*,values)
AL_API DECL_FUNC3(void, alGetFilteri, ALuint,filter, ALenum,param, ALint*,value)
AL_API DECL_FUNC3(void, alGetFilteriv, ALuint,filter, ALenum,param, ALint*,values)
AL_API DECL_FUNC3(void, alGetFilterf, ALuint,filter, ALenum,param, ALfloat*,value)
AL_API DECL_FUNC3(void, alGetFilterfv, ALuint,filter, ALenum,param, ALfloat*,values)


void al::Filter::SetName(gsl::not_null<al::Context*> const context, u32 const id,
    std::string_view const name)
{
    auto const device = get_not_null(context->mALDevice);
    auto const filterlock = std::lock_guard{device->FilterLock};

    std::ignore = LookupFilter(context, id);
    device->mFilterNames.insert_or_assign(id, name);
}


FilterSubList::~FilterSubList()
{
    if(!mFilters)
        return;

    auto usemask = ~mFreeMask;
    while(usemask)
    {
        auto const idx = std::countr_zero(usemask);
        std::destroy_at(std::to_address(std::next(mFilters->begin(), idx)));
        usemask &= ~(1_u64 << idx);
    }
    mFreeMask = ~usemask;
    SubListAllocator{}.deallocate(mFilters, 1);
    mFilters = nullptr;
}
