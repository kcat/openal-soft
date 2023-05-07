#include "config.h"

#include "debug.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <optional>
#include <stddef.h>
#include <stdexcept>
#include <string>
#include <utility>

#include "AL/al.h"

#include "alc/context.h"
#include "alc/inprogext.h"
#include "alspan.h"
#include "core/logging.h"
#include "opthelpers.h"
#include "threads.h"


namespace {

static_assert(DebugSeverityBase+DebugSeverityCount <= 32, "Too many debug bits");

template<typename T, T ...Vals>
constexpr auto make_array_sequence(std::integer_sequence<T, Vals...>)
{ return std::array<T,sizeof...(Vals)>{Vals...}; }

template<typename T, size_t N>
constexpr auto make_array_sequence()
{ return make_array_sequence(std::make_integer_sequence<T,N>{}); }


constexpr std::optional<DebugSource> GetDebugSource(ALenum source) noexcept
{
    switch(source)
    {
    case AL_DEBUG_SOURCE_API_EXT: return DebugSource::API;
    case AL_DEBUG_SOURCE_AUDIO_SYSTEM_EXT: return DebugSource::System;
    case AL_DEBUG_SOURCE_THIRD_PARTY_EXT: return DebugSource::ThirdParty;
    case AL_DEBUG_SOURCE_APPLICATION_EXT: return DebugSource::Application;
    case AL_DEBUG_SOURCE_OTHER_EXT: return DebugSource::Other;
    }
    return std::nullopt;
}

constexpr std::optional<DebugType> GetDebugType(ALenum type) noexcept
{
    switch(type)
    {
    case AL_DEBUG_TYPE_ERROR_EXT: return DebugType::Error;
    case AL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_EXT: return DebugType::DeprecatedBehavior;
    case AL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_EXT: return DebugType::UndefinedBehavior;
    case AL_DEBUG_TYPE_PORTABILITY_EXT: return DebugType::Portability;
    case AL_DEBUG_TYPE_PERFORMANCE_EXT: return DebugType::Performance;
    case AL_DEBUG_TYPE_MARKER_EXT: return DebugType::Marker;
    case AL_DEBUG_TYPE_PUSH_GROUP_EXT: return DebugType::PushGroup;
    case AL_DEBUG_TYPE_POP_GROUP_EXT: return DebugType::PopGroup;
    case AL_DEBUG_TYPE_OTHER_EXT: return DebugType::Other;
    }
    return std::nullopt;
}

constexpr std::optional<DebugSeverity> GetDebugSeverity(ALenum severity) noexcept
{
    switch(severity)
    {
    case AL_DEBUG_SEVERITY_HIGH_EXT: return DebugSeverity::High;
    case AL_DEBUG_SEVERITY_MEDIUM_EXT: return DebugSeverity::Medium;
    case AL_DEBUG_SEVERITY_LOW_EXT: return DebugSeverity::Low;
    case AL_DEBUG_SEVERITY_NOTIFICATION_EXT: return DebugSeverity::Notification;
    }
    return std::nullopt;
}


ALenum GetDebugSourceEnum(DebugSource source)
{
    switch(source)
    {
    case DebugSource::API: return AL_DEBUG_SOURCE_API_EXT;
    case DebugSource::System: return AL_DEBUG_SOURCE_AUDIO_SYSTEM_EXT;
    case DebugSource::ThirdParty: return AL_DEBUG_SOURCE_THIRD_PARTY_EXT;
    case DebugSource::Application: return AL_DEBUG_SOURCE_APPLICATION_EXT;
    case DebugSource::Other: return AL_DEBUG_SOURCE_OTHER_EXT;
    }
    throw std::runtime_error{"Unexpected debug source value "+std::to_string(al::to_underlying(source))};
}

ALenum GetDebugTypeEnum(DebugType type)
{
    switch(type)
    {
    case DebugType::Error: return AL_DEBUG_TYPE_ERROR_EXT;
    case DebugType::DeprecatedBehavior: return AL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_EXT;
    case DebugType::UndefinedBehavior: return AL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_EXT;
    case DebugType::Portability: return AL_DEBUG_TYPE_PORTABILITY_EXT;
    case DebugType::Performance: return AL_DEBUG_TYPE_PERFORMANCE_EXT;
    case DebugType::Marker: return AL_DEBUG_TYPE_MARKER_EXT;
    case DebugType::PushGroup: return AL_DEBUG_TYPE_PUSH_GROUP_EXT;
    case DebugType::PopGroup: return AL_DEBUG_TYPE_POP_GROUP_EXT;
    case DebugType::Other: return AL_DEBUG_TYPE_OTHER_EXT;
    }
    throw std::runtime_error{"Unexpected debug type value "+std::to_string(al::to_underlying(type))};
}

ALenum GetDebugSeverityEnum(DebugSeverity severity)
{
    switch(severity)
    {
    case DebugSeverity::High: return AL_DEBUG_SEVERITY_HIGH_EXT;
    case DebugSeverity::Medium: return AL_DEBUG_SEVERITY_MEDIUM_EXT;
    case DebugSeverity::Low: return AL_DEBUG_SEVERITY_LOW_EXT;
    case DebugSeverity::Notification: return AL_DEBUG_SEVERITY_NOTIFICATION_EXT;
    }
    throw std::runtime_error{"Unexpected debug severity value "+std::to_string(al::to_underlying(severity))};
}


const char *GetDebugSourceName(DebugSource source)
{
    switch(source)
    {
    case DebugSource::API: return "API";
    case DebugSource::System: return "Audio System";
    case DebugSource::ThirdParty: return "Third Party";
    case DebugSource::Application: return "Application";
    case DebugSource::Other: return "Other";
    }
    return "<invalid source>";
}

const char *GetDebugTypeName(DebugType type)
{
    switch(type)
    {
    case DebugType::Error: return "Error";
    case DebugType::DeprecatedBehavior: return "Deprecated Behavior";
    case DebugType::UndefinedBehavior: return "Undefined Behavior";
    case DebugType::Portability: return "Portability";
    case DebugType::Performance: return "Performance";
    case DebugType::Marker: return "Marker";
    case DebugType::PushGroup: return "Push Group";
    case DebugType::PopGroup: return "Pop Group";
    case DebugType::Other: return "Other";
    }
    return "<invalid type>";
}

const char *GetDebugSeverityName(DebugSeverity severity)
{
    switch(severity)
    {
    case DebugSeverity::High: return "High";
    case DebugSeverity::Medium: return "Medium";
    case DebugSeverity::Low: return "Low";
    case DebugSeverity::Notification: return "Notification";
    }
    return "<invalid severity>";
}

} // namespace


void ALCcontext::sendDebugMessage(std::unique_lock<std::mutex> &debuglock, DebugSource source,
    DebugType type, ALuint id, DebugSeverity severity, ALsizei length, const char *message)
{
    if(!mDebugEnabled.load()) UNLIKELY
        return;

    /* MaxDebugMessageLength is the size including the null terminator,
     * <length> does not include the null terminator.
     */
    if(length < 0)
    {
        size_t newlen{std::strlen(message)};
        if(newlen >= MaxDebugMessageLength) UNLIKELY
        {
            ERR("Debug message too long (%zu >= %d):\n-> %s\n", newlen, MaxDebugMessageLength,
                message);
            return;
        }
        length = static_cast<ALsizei>(newlen);
    }
    else if(length >= MaxDebugMessageLength) UNLIKELY
    {
        ERR("Debug message too long (%d >= %d):\n-> %s\n", length, MaxDebugMessageLength, message);
        return;
    }

    DebugGroup &debug = mDebugGroups.back();

    const uint64_t idfilter{(1_u64 << (DebugSourceBase+al::to_underlying(source)))
        | (1_u64 << (DebugTypeBase+al::to_underlying(type)))
        | (uint64_t{id} << 32)};
    auto iditer = std::lower_bound(debug.mIdFilters.cbegin(), debug.mIdFilters.cend(), idfilter);
    if(iditer != debug.mIdFilters.cend() && *iditer == idfilter)
        return;

    const uint filter{(1u << (DebugSourceBase+al::to_underlying(source)))
        | (1u << (DebugTypeBase+al::to_underlying(type)))
        | (1u << (DebugSeverityBase+al::to_underlying(severity)))};
    auto iter = std::lower_bound(debug.mFilters.cbegin(), debug.mFilters.cend(), filter);
    if(iter != debug.mFilters.cend() && *iter == filter)
        return;

    if(mDebugCb)
    {
        auto callback = mDebugCb;
        auto param = mDebugParam;
        debuglock.unlock();
        callback(GetDebugSourceEnum(source), GetDebugTypeEnum(type), id,
            GetDebugSeverityEnum(severity), length, message, param);
    }
    else
    {
        if(mDebugLog.size() < MaxDebugLoggedMessages)
            mDebugLog.emplace_back(source, type, id, severity, message);
        else UNLIKELY
            ERR("Debug message log overflow. Lost message:\n"
                "  Source: %s\n"
                "  Type: %s\n"
                "  ID: %u\n"
                "  Severity: %s\n"
                "  Message: \"%s\"\n",
                GetDebugSourceName(source), GetDebugTypeName(type), id,
                GetDebugSeverityName(severity), message);
    }
}


FORCE_ALIGN void AL_APIENTRY alDebugMessageCallbackEXT(ALDEBUGPROCEXT callback, void *userParam) noexcept
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    std::lock_guard<std::mutex> _{context->mDebugCbLock};
    context->mDebugCb = callback;
    context->mDebugParam = userParam;
}

FORCE_ALIGN void AL_APIENTRY alDebugMessageInsertEXT(ALenum source, ALenum type, ALuint id,
    ALenum severity, ALsizei length, const ALchar *message) noexcept
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(!context->mContextFlags.test(ContextFlags::DebugBit))
        return;

    if(!message)
        return context->setError(AL_INVALID_VALUE, "Null message pointer");

    if(length < 0)
    {
        size_t newlen{std::strlen(message)};
        if(newlen >= MaxDebugMessageLength) UNLIKELY
            return context->setError(AL_INVALID_VALUE, "Debug message too long (%zu >= %d)",
                newlen, MaxDebugMessageLength);
        length = static_cast<ALsizei>(newlen);
    }
    else if(length >= MaxDebugMessageLength) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "Debug message too long (%d > %d)", length,
            MaxDebugMessageLength);

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


FORCE_ALIGN void AL_APIENTRY alDebugMessageControlEXT(ALenum source, ALenum type, ALenum severity,
    ALsizei count, const ALuint *ids, ALboolean enable) noexcept
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(count > 0)
    {
        if(!ids)
            return context->setError(AL_INVALID_VALUE, "IDs is null with non-0 count");
        if(source == AL_DONT_CARE_EXT)
            return context->setError(AL_INVALID_OPERATION,
                "Debug source cannot be AL_DONT_CARE_EXT with IDs");
        if(type == AL_DONT_CARE_EXT)
            return context->setError(AL_INVALID_OPERATION,
                "Debug type cannot be AL_DONT_CARE_EXT with IDs");
        if(severity != AL_DONT_CARE_EXT)
            return context->setError(AL_INVALID_OPERATION,
                "Debug severity must be AL_DONT_CARE_EXT with IDs");
    }

    if(enable != AL_TRUE && enable != AL_FALSE)
        return context->setError(AL_INVALID_ENUM, "Invalid debug enable %d", enable);

    static constexpr size_t ElemCount{DebugSourceCount + DebugTypeCount + DebugSeverityCount};
    static constexpr auto Values = make_array_sequence<uint,ElemCount>();

    al::span<const uint> srcIndices{al::span{Values}.subspan<DebugSourceBase,DebugSourceCount>()};
    if(source != AL_DONT_CARE_EXT)
    {
        auto dsource = GetDebugSource(source);
        if(!dsource)
            return context->setError(AL_INVALID_ENUM, "Invalid debug source 0x%04x", source);
        srcIndices = srcIndices.subspan(al::to_underlying(*dsource), 1);
    }

    al::span<const uint> typeIndices{al::span{Values}.subspan<DebugTypeBase,DebugTypeCount>()};
    if(type != AL_DONT_CARE_EXT)
    {
        auto dtype = GetDebugType(type);
        if(!dtype)
            return context->setError(AL_INVALID_ENUM, "Invalid debug type 0x%04x", type);
        typeIndices = typeIndices.subspan(al::to_underlying(*dtype), 1);
    }

    al::span<const uint> svrIndices{al::span{Values}.subspan<DebugSeverityBase,DebugSeverityCount>()};
    if(severity != AL_DONT_CARE_EXT)
    {
        auto dseverity = GetDebugSeverity(severity);
        if(!dseverity)
            return context->setError(AL_INVALID_ENUM, "Invalid debug severity 0x%04x", severity);
        svrIndices = svrIndices.subspan(al::to_underlying(*dseverity), 1);
    }

    std::lock_guard<std::mutex> _{context->mDebugCbLock};
    DebugGroup &debug = context->mDebugGroups.back();
    if(count > 0)
    {
        const uint filterbase{(1u<<srcIndices[0]) | (1u<<typeIndices[0])};

        for(const uint id : al::span{ids, static_cast<uint>(count)})
        {
            const uint64_t filter{filterbase | (uint64_t{id} << 32)};

            auto iter = std::lower_bound(debug.mIdFilters.cbegin(), debug.mIdFilters.cend(),
                filter);
            if(!enable && (iter == debug.mIdFilters.cend() || *iter != filter))
                debug.mIdFilters.insert(iter, filter);
            else if(enable && iter != debug.mIdFilters.cend() && *iter == filter)
                debug.mIdFilters.erase(iter);
        }
    }
    else
    {
        auto apply_filter = [enable,&debug](const uint filter)
        {
            auto iter = std::lower_bound(debug.mFilters.cbegin(), debug.mFilters.cend(), filter);
            if(!enable && (iter == debug.mFilters.cend() || *iter != filter))
                debug.mFilters.insert(iter, filter);
            else if(enable && iter != debug.mFilters.cend() && *iter == filter)
                debug.mFilters.erase(iter);
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
}


FORCE_ALIGN void AL_APIENTRY alPushDebugGroupEXT(ALenum source, ALuint id, ALsizei length, const ALchar *message) noexcept
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    if(length < 0)
    {
        size_t newlen{std::strlen(message)};
        if(newlen >= MaxDebugMessageLength) UNLIKELY
            return context->setError(AL_INVALID_VALUE, "Debug message too long (%zu >= %d)",
                newlen, MaxDebugMessageLength);
        length = static_cast<ALsizei>(newlen);
    }
    else if(length >= MaxDebugMessageLength) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "Debug message too long (%d > %d)", length,
            MaxDebugMessageLength);

    auto dsource = GetDebugSource(source);
    if(!dsource)
        return context->setError(AL_INVALID_ENUM, "Invalid debug source 0x%04x", source);
    if(*dsource != DebugSource::ThirdParty && *dsource != DebugSource::Application)
        return context->setError(AL_INVALID_ENUM, "Debug source 0x%04x not allowed", source);

    std::unique_lock<std::mutex> debuglock{context->mDebugCbLock};
    if(context->mDebugGroups.size() >= MaxDebugGroupDepth)
    {
        debuglock.unlock();
        return context->setError(AL_STACK_OVERFLOW_EXT, "Pushing too many debug groups");
    }

    context->mDebugGroups.emplace_back(*dsource, id, message);
    auto &oldback = *(context->mDebugGroups.end()-2);
    auto &newback = context->mDebugGroups.back();

    newback.mFilters = oldback.mFilters;
    newback.mIdFilters = oldback.mIdFilters;

    if(context->mContextFlags.test(ContextFlags::DebugBit))
        context->sendDebugMessage(debuglock, newback.mSource, DebugType::PushGroup, newback.mId,
            DebugSeverity::Notification, static_cast<ALsizei>(newback.mMessage.size()),
            newback.mMessage.data());
}

FORCE_ALIGN void AL_APIENTRY alPopDebugGroupEXT(void) noexcept
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return;

    std::unique_lock<std::mutex> debuglock{context->mDebugCbLock};
    if(context->mDebugGroups.size() <= 1)
    {
        debuglock.unlock();
        return context->setError(AL_STACK_UNDERFLOW_EXT,
            "Attempting to pop the default debug group");
    }

    DebugGroup &debug = context->mDebugGroups.back();
    const auto source = debug.mSource;
    const auto id = debug.mId;
    std::string message{std::move(debug.mMessage)};

    context->mDebugGroups.pop_back();
    if(context->mContextFlags.test(ContextFlags::DebugBit))
        context->sendDebugMessage(debuglock, source, DebugType::PopGroup, id,
            DebugSeverity::Notification, static_cast<ALsizei>(message.size()), message.data());
}


FORCE_ALIGN ALuint AL_APIENTRY alGetDebugMessageLogEXT(ALuint count, ALsizei logBufSize,
    ALenum *sources, ALenum *types, ALuint *ids, ALenum *severities, ALsizei *lengths,
    ALchar *logBuf) noexcept
{
    ContextRef context{GetContextRef()};
    if(!context) UNLIKELY return 0;

    if(logBufSize < 0)
    {
        context->setError(AL_INVALID_VALUE, "Negative debug log buffer size");
        return 0;
    }

    std::lock_guard<std::mutex> _{context->mDebugCbLock};
    ALsizei logBufWritten{0};
    for(ALuint i{0};i < count;++i)
    {
        if(context->mDebugLog.empty())
            return i;

        auto &entry = context->mDebugLog.front();
        const size_t tocopy{entry.mMessage.size() + 1};
        if(logBuf)
        {
            const size_t avail{static_cast<ALuint>(logBufSize - logBufWritten)};
            if(avail < tocopy)
                return i;
            std::copy_n(entry.mMessage.data(), tocopy, logBuf+logBufWritten);
        }

        if(sources) sources[i] = GetDebugSourceEnum(entry.mSource);
        if(types) types[i] = GetDebugTypeEnum(entry.mType);
        if(ids) ids[i] = entry.mId;
        if(severities) severities[i] = GetDebugSeverityEnum(entry.mSeverity);
        if(lengths) lengths[i] = static_cast<ALsizei>(tocopy);

        logBufWritten += static_cast<ALsizei>(tocopy);
        context->mDebugLog.pop_front();
    }

    return count;
}
