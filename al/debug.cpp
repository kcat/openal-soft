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

    DebugSource dsource{};
    switch(source)
    {
    case AL_DEBUG_SOURCE_THIRD_PARTY_SOFT: dsource = DebugSource::ThirdParty; break;
    case AL_DEBUG_SOURCE_APPLICATION_SOFT: dsource = DebugSource::Application; break;
    case AL_DEBUG_SOURCE_API_SOFT:
    case AL_DEBUG_SOURCE_AUDIO_SYSTEM_SOFT:
    case AL_DEBUG_SOURCE_OTHER_SOFT:
        return context->setError(AL_INVALID_ENUM, "Debug source enum 0x%04x not allowed", source);
    default:
        return context->setError(AL_INVALID_ENUM, "Invalid debug source enum 0x%04x", source);
    }

    DebugType dtype{};
    switch(type)
    {
    case AL_DEBUG_TYPE_ERROR_SOFT: dtype = DebugType::Error; break;
    case AL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_SOFT: dtype = DebugType::DeprecatedBehavior; break;
    case AL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_SOFT: dtype = DebugType::UndefinedBehavior; break;
    case AL_DEBUG_TYPE_PORTABILITY_SOFT: dtype = DebugType::Portability; break;
    case AL_DEBUG_TYPE_PERFORMANCE_SOFT: dtype = DebugType::Performance; break;
    case AL_DEBUG_TYPE_MARKER_SOFT: dtype = DebugType::Marker; break;
    case AL_DEBUG_TYPE_OTHER_SOFT: dtype = DebugType::Other; break;
    default:
        return context->setError(AL_INVALID_ENUM, "Invalid debug type 0x%04x", type);
    }

    DebugSeverity dseverity{};
    switch(severity)
    {
    case AL_DEBUG_SEVERITY_HIGH_SOFT: dseverity = DebugSeverity::High; break;
    case AL_DEBUG_SEVERITY_MEDIUM_SOFT: dseverity = DebugSeverity::Medium; break;
    case AL_DEBUG_SEVERITY_LOW_SOFT: dseverity = DebugSeverity::Low; break;
    case AL_DEBUG_SEVERITY_NOTIFICATION_SOFT: dseverity = DebugSeverity::Notification; break;
    default:
        return context->setError(AL_INVALID_ENUM, "Invalid debug severity 0x%04x", severity);
    }

    context->debugMessage(dsource, dtype, id, dseverity, length, message);
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
    switch(source)
    {
    case AL_DEBUG_SOURCE_API_SOFT: srcIndices = srcIndices.subspan(0, 1); break;
    case AL_DEBUG_SOURCE_AUDIO_SYSTEM_SOFT: srcIndices = srcIndices.subspan(1, 1); break;
    case AL_DEBUG_SOURCE_THIRD_PARTY_SOFT: srcIndices = srcIndices.subspan(2, 1); break;
    case AL_DEBUG_SOURCE_APPLICATION_SOFT: srcIndices = srcIndices.subspan(3, 1); break;
    case AL_DEBUG_SOURCE_OTHER_SOFT: srcIndices = srcIndices.subspan(4, 1); break;
    case AL_DONT_CARE_SOFT: break;
    default:
        return context->setError(AL_INVALID_VALUE, "Invalid debug source 0x%04x", source);
    }

    al::span<const uint> typeIndices{al::as_span(Values).subspan<DebugTypeBase,DebugTypeCount>()};
    switch(type)
    {
    case AL_DEBUG_TYPE_ERROR_SOFT: typeIndices = typeIndices.subspan(0, 1); break;
    case AL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_SOFT: typeIndices = typeIndices.subspan(1, 1); break;
    case AL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_SOFT: typeIndices = typeIndices.subspan(2, 1); break;
    case AL_DEBUG_TYPE_PORTABILITY_SOFT: typeIndices = typeIndices.subspan(3, 1); break;
    case AL_DEBUG_TYPE_PERFORMANCE_SOFT: typeIndices = typeIndices.subspan(4, 1); break;
    case AL_DEBUG_TYPE_MARKER_SOFT: typeIndices = typeIndices.subspan(5, 1); break;
    case AL_DEBUG_TYPE_OTHER_SOFT: typeIndices = typeIndices.subspan(6, 1); break;
    case AL_DONT_CARE_SOFT: break;
    default:
        return context->setError(AL_INVALID_VALUE, "Invalid debug type 0x%04x", type);
    }

    al::span<const uint> svrIndices{al::as_span(Values).subspan<DebugSeverityBase,DebugSeverityCount>()};
    switch(severity)
    {
    case AL_DEBUG_SEVERITY_HIGH_SOFT: svrIndices = svrIndices.subspan(0, 1); break;
    case AL_DEBUG_SEVERITY_MEDIUM_SOFT: svrIndices = svrIndices.subspan(1, 1); break;
    case AL_DEBUG_SEVERITY_LOW_SOFT: svrIndices = svrIndices.subspan(2, 1); break;
    case AL_DEBUG_SEVERITY_NOTIFICATION_SOFT: svrIndices = svrIndices.subspan(3, 1); break;
    case AL_DONT_CARE_SOFT: break;
    default:
        return context->setError(AL_INVALID_VALUE, "Invalid debug severity 0x%04x", severity);
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
