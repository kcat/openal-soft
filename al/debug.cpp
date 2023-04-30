#include "config.h"

#include "debug.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <stddef.h>
#include <utility>

#include "AL/al.h"

#include "alc/context.h"
#include "alc/inprogext.h"
#include "aloptional.h"
#include "alspan.h"
#include "opthelpers.h"
#include "threads.h"


namespace {

template<typename T, T ...Vals>
constexpr auto make_array(std::integer_sequence<T, Vals...>)
{ return std::array<T,sizeof...(Vals)>{Vals...}; }

template<typename T, size_t N, typename Indices = std::make_integer_sequence<T,N>>
constexpr auto make_array()
{ return make_array(Indices{}); }


constexpr al::optional<DebugSource> GetDebugSource(ALenum source) noexcept
{
    switch(source)
    {
    case AL_DEBUG_SOURCE_API_SOFT: return DebugSource::API;
    case AL_DEBUG_SOURCE_AUDIO_SYSTEM_SOFT: return DebugSource::System;
    case AL_DEBUG_SOURCE_THIRD_PARTY_SOFT: return DebugSource::ThirdParty;
    case AL_DEBUG_SOURCE_APPLICATION_SOFT: return DebugSource::Application;
    case AL_DEBUG_SOURCE_OTHER_SOFT: return DebugSource::Other;
    }
    return al::nullopt;
}

constexpr al::optional<DebugType> GetDebugType(ALenum type) noexcept
{
    switch(type)
    {
    case AL_DEBUG_TYPE_ERROR_SOFT: return DebugType::Error;
    case AL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_SOFT: return DebugType::DeprecatedBehavior;
    case AL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_SOFT: return DebugType::UndefinedBehavior;
    case AL_DEBUG_TYPE_PORTABILITY_SOFT: return DebugType::Portability;
    case AL_DEBUG_TYPE_PERFORMANCE_SOFT: return DebugType::Performance;
    case AL_DEBUG_TYPE_MARKER_SOFT: return DebugType::Marker;
    case AL_DEBUG_TYPE_OTHER_SOFT: return DebugType::Other;
    }
    return al::nullopt;
}

constexpr al::optional<DebugSeverity> GetDebugSeverity(ALenum severity) noexcept
{
    switch(severity)
    {
    case AL_DEBUG_SEVERITY_HIGH_SOFT: return DebugSeverity::High;
    case AL_DEBUG_SEVERITY_MEDIUM_SOFT: return DebugSeverity::Medium;
    case AL_DEBUG_SEVERITY_LOW_SOFT: return DebugSeverity::Low;
    case AL_DEBUG_SEVERITY_NOTIFICATION_SOFT: return DebugSeverity::Notification;
    }
    return al::nullopt;
}

} // namespace


FORCE_ALIGN void AL_APIENTRY alDebugMessageCallbackSOFT(ALDEBUGPROCSOFT callback, void *userParam) noexcept
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    std::lock_guard<std::mutex> _{context->mDebugCbLock};
    context->mDebugCb = callback;
    context->mDebugParam = userParam;
}

FORCE_ALIGN void AL_APIENTRY alDebugMessageInsertSOFT(ALenum source, ALenum type, ALuint id,
    ALenum severity, ALsizei length, const ALchar *message) noexcept
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(!message)
        return context->setError(AL_INVALID_VALUE, "Null message pointer");

    auto dsource = GetDebugSource(source);
    if(!dsource)
        return context->setError(AL_INVALID_ENUM, "Invalid debug source 0x%04x", source);
    if(*dsource != DebugSource::ThirdParty && *dsource != DebugSource::Application)
        return context->setError(AL_INVALID_ENUM, "Debug source 0x%04x not allowed", source);

    auto dtype = GetDebugType(type);
    if(!dtype)
        return context->setError(AL_INVALID_ENUM, "Invalid debug type 0x%04x", type);

    auto dseverity = GetDebugSeverity(severity);
    if(!dseverity)
        return context->setError(AL_INVALID_ENUM, "Invalid debug severity 0x%04x", severity);

    context->debugMessage(*dsource, *dtype, id, *dseverity, length, message);
}


FORCE_ALIGN void AL_APIENTRY alDebugMessageControlSOFT(ALenum source, ALenum type, ALenum severity,
    ALsizei count, const ALuint *ids, ALboolean enable) noexcept
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(count > 0)
    {
        if(!ids)
            return context->setError(AL_INVALID_VALUE, "IDs is null with non-0 count");
        if(source == AL_DONT_CARE_SOFT)
            return context->setError(AL_INVALID_VALUE,
                "Debug source cannot be AL_DONT_CARE_SOFT with IDs");
        if(type == AL_DONT_CARE_SOFT)
            return context->setError(AL_INVALID_VALUE,
                "Debug type cannot be AL_DONT_CARE_SOFT with IDs");
        if(severity != AL_DONT_CARE_SOFT)
            return context->setError(AL_INVALID_VALUE,
                "Debug severity must be AL_DONT_CARE_SOFT with IDs");

        return context->setError(AL_INVALID_VALUE, "Debug ID filtering not supported");
        return;
    }

    if(enable != AL_TRUE && enable != AL_FALSE)
        return context->setError(AL_INVALID_VALUE, "Invalid debug enable %d", enable);

    static constexpr size_t ElemCount{DebugSourceCount + DebugTypeCount + DebugSeverityCount};
    static constexpr auto Values = make_array<uint,ElemCount>();

    al::span<const uint> srcIndices{al::as_span(Values).subspan<DebugSourceBase,DebugSourceCount>()};
    if(source != AL_DONT_CARE_SOFT)
    {
        auto dsource = GetDebugSource(source);
        if(!dsource)
            return context->setError(AL_INVALID_ENUM, "Invalid debug source 0x%04x", source);
        srcIndices = srcIndices.subspan(al::to_underlying(*dsource), 1);
    }

    al::span<const uint> typeIndices{al::as_span(Values).subspan<DebugTypeBase,DebugTypeCount>()};
    if(type != AL_DONT_CARE_SOFT)
    {
        auto dtype = GetDebugType(type);
        if(!dtype)
            return context->setError(AL_INVALID_ENUM, "Invalid debug type 0x%04x", type);
        typeIndices = typeIndices.subspan(al::to_underlying(*dtype), 1);
    }

    al::span<const uint> svrIndices{al::as_span(Values).subspan<DebugSeverityBase,DebugSeverityCount>()};
    if(severity != AL_DONT_CARE_SOFT)
    {
        auto dseverity = GetDebugSeverity(severity);
        if(!dseverity)
            return context->setError(AL_INVALID_ENUM, "Invalid debug severity 0x%04x", severity);
        svrIndices = svrIndices.subspan(al::to_underlying(*dseverity), 1);
    }

    std::lock_guard<std::mutex> _{context->mDebugCbLock};
    auto apply_filter = [enable,&context](const uint filter)
    {
        auto iter = std::lower_bound(context->mDebugFilters.cbegin(),
            context->mDebugFilters.cend(), filter);
        if(enable && (iter == context->mDebugFilters.cend() || *iter != filter))
            context->mDebugFilters.insert(iter, filter);
        else if(!enable && iter != context->mDebugFilters.cend() && *iter == filter)
            context->mDebugFilters.erase(iter);
    };
    auto apply_severity = [apply_filter,svrIndices](const uint filter)
    {
        std::for_each(svrIndices.cbegin(), svrIndices.cend(),
            [apply_filter,filter](const uint idx){ apply_filter(filter | (1<<idx)); });
    };
    auto apply_type = [apply_severity,typeIndices](const uint filter)
    {
        std::for_each(typeIndices.cbegin(), typeIndices.cend(),
            [apply_severity,filter](const uint idx){ apply_severity(filter | (1<<idx)); });
    };
    std::for_each(srcIndices.cbegin(), srcIndices.cend(),
        [apply_type](const uint idx){ apply_type(1<<idx); });
}
