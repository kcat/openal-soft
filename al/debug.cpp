#include "config.h"

#include "debug.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "alc/context.h"
#include "alc/device.h"
#include "alc/inprogext.h"
#include "alnumeric.h"
#include "alspan.h"
#include "alstring.h"
#include "auxeffectslot.h"
#include "buffer.h"
#include "core/logging.h"
#include "core/voice.h"
#include "direct_defs.h"
#include "effect.h"
#include "filter.h"
#include "intrusive_ptr.h"
#include "opthelpers.h"
#include "source.h"


/* Declared here to prevent compilers from thinking it should be inlined, which
 * GCC warns about increasing code size.
 */
DebugGroup::~DebugGroup() = default;

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
    DebugType type, ALuint id, DebugSeverity severity, std::string_view message)
{
    if(!mDebugEnabled.load(std::memory_order_relaxed)) UNLIKELY
        return;

    if(message.length() >= MaxDebugMessageLength) UNLIKELY
    {
        ERR("Debug message too long (%zu >= %d):\n-> %.*s\n", message.length(),
            MaxDebugMessageLength, al::sizei(message), message.data());
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
            GetDebugSeverityEnum(severity), static_cast<ALsizei>(message.length()), message.data(),
            param);
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
                "  Message: \"%.*s\"\n",
                GetDebugSourceName(source), GetDebugTypeName(type), id,
                GetDebugSeverityName(severity), al::sizei(message), message.data());
    }
}


FORCE_ALIGN DECL_FUNCEXT2(void, alDebugMessageCallback,EXT, ALDEBUGPROCEXT,callback, void*,userParam)
FORCE_ALIGN void AL_APIENTRY alDebugMessageCallbackDirectEXT(ALCcontext *context,
    ALDEBUGPROCEXT callback, void *userParam) noexcept
{
    std::lock_guard<std::mutex> debuglock{context->mDebugCbLock};
    context->mDebugCb = callback;
    context->mDebugParam = userParam;
}


FORCE_ALIGN DECL_FUNCEXT6(void, alDebugMessageInsert,EXT, ALenum,source, ALenum,type, ALuint,id, ALenum,severity, ALsizei,length, const ALchar*,message)
FORCE_ALIGN void AL_APIENTRY alDebugMessageInsertDirectEXT(ALCcontext *context, ALenum source,
    ALenum type, ALuint id, ALenum severity, ALsizei length, const ALchar *message) noexcept
{
    if(!context->mContextFlags.test(ContextFlags::DebugBit))
        return;

    if(!message) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "Null message pointer");

    auto msgview = (length < 0) ? std::string_view{message}
        : std::string_view{message, static_cast<uint>(length)};
    if(msgview.length() >= MaxDebugMessageLength) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "Debug message too long (%zu >= %d)",
            msgview.length(), MaxDebugMessageLength);

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

    context->debugMessage(*dsource, *dtype, id, *dseverity, msgview);
}


FORCE_ALIGN DECL_FUNCEXT6(void, alDebugMessageControl,EXT, ALenum,source, ALenum,type, ALenum,severity, ALsizei,count, const ALuint*,ids, ALboolean,enable)
FORCE_ALIGN void AL_APIENTRY alDebugMessageControlDirectEXT(ALCcontext *context, ALenum source,
    ALenum type, ALenum severity, ALsizei count, const ALuint *ids, ALboolean enable) noexcept
{
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
    static constexpr auto Values = make_array_sequence<uint8_t,ElemCount>();

    auto srcIndices = al::span{Values}.subspan(DebugSourceBase,DebugSourceCount);
    if(source != AL_DONT_CARE_EXT)
    {
        auto dsource = GetDebugSource(source);
        if(!dsource)
            return context->setError(AL_INVALID_ENUM, "Invalid debug source 0x%04x", source);
        srcIndices = srcIndices.subspan(al::to_underlying(*dsource), 1);
    }

    auto typeIndices = al::span{Values}.subspan(DebugTypeBase,DebugTypeCount);
    if(type != AL_DONT_CARE_EXT)
    {
        auto dtype = GetDebugType(type);
        if(!dtype)
            return context->setError(AL_INVALID_ENUM, "Invalid debug type 0x%04x", type);
        typeIndices = typeIndices.subspan(al::to_underlying(*dtype), 1);
    }

    auto svrIndices = al::span{Values}.subspan(DebugSeverityBase,DebugSeverityCount);
    if(severity != AL_DONT_CARE_EXT)
    {
        auto dseverity = GetDebugSeverity(severity);
        if(!dseverity)
            return context->setError(AL_INVALID_ENUM, "Invalid debug severity 0x%04x", severity);
        svrIndices = svrIndices.subspan(al::to_underlying(*dseverity), 1);
    }

    std::lock_guard<std::mutex> debuglock{context->mDebugCbLock};
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


FORCE_ALIGN DECL_FUNCEXT4(void, alPushDebugGroup,EXT, ALenum,source, ALuint,id, ALsizei,length, const ALchar*,message)
FORCE_ALIGN void AL_APIENTRY alPushDebugGroupDirectEXT(ALCcontext *context, ALenum source,
    ALuint id, ALsizei length, const ALchar *message) noexcept
{
    if(length < 0)
    {
        size_t newlen{std::strlen(message)};
        if(newlen >= MaxDebugMessageLength) UNLIKELY
            return context->setError(AL_INVALID_VALUE, "Debug message too long (%zu >= %d)",
                newlen, MaxDebugMessageLength);
        length = static_cast<ALsizei>(newlen);
    }
    else if(length >= MaxDebugMessageLength) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "Debug message too long (%d >= %d)", length,
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

    context->mDebugGroups.emplace_back(*dsource, id,
        std::string_view{message, static_cast<uint>(length)});
    auto &oldback = *(context->mDebugGroups.end()-2);
    auto &newback = context->mDebugGroups.back();

    newback.mFilters = oldback.mFilters;
    newback.mIdFilters = oldback.mIdFilters;

    if(context->mContextFlags.test(ContextFlags::DebugBit))
        context->sendDebugMessage(debuglock, newback.mSource, DebugType::PushGroup, newback.mId,
            DebugSeverity::Notification, newback.mMessage);
}

FORCE_ALIGN DECL_FUNCEXT(void, alPopDebugGroup,EXT)
FORCE_ALIGN void AL_APIENTRY alPopDebugGroupDirectEXT(ALCcontext *context) noexcept
{
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
            DebugSeverity::Notification, message);
}


FORCE_ALIGN DECL_FUNCEXT8(ALuint, alGetDebugMessageLog,EXT, ALuint,count, ALsizei,logBufSize, ALenum*,sources, ALenum*,types, ALuint*,ids, ALenum*,severities, ALsizei*,lengths, ALchar*,logBuf)
FORCE_ALIGN ALuint AL_APIENTRY alGetDebugMessageLogDirectEXT(ALCcontext *context, ALuint count,
    ALsizei logBufSize, ALenum *sources, ALenum *types, ALuint *ids, ALenum *severities,
    ALsizei *lengths, ALchar *logBuf) noexcept
{
    if(logBufSize < 0)
    {
        context->setError(AL_INVALID_VALUE, "Negative debug log buffer size");
        return 0;
    }

    std::lock_guard<std::mutex> debuglock{context->mDebugCbLock};
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
            logBufWritten += static_cast<ALsizei>(tocopy);
        }

        if(sources) sources[i] = GetDebugSourceEnum(entry.mSource);
        if(types) types[i] = GetDebugTypeEnum(entry.mType);
        if(ids) ids[i] = entry.mId;
        if(severities) severities[i] = GetDebugSeverityEnum(entry.mSeverity);
        if(lengths) lengths[i] = static_cast<ALsizei>(tocopy);

        context->mDebugLog.pop_front();
    }

    return count;
}

FORCE_ALIGN DECL_FUNCEXT4(void, alObjectLabel,EXT, ALenum,identifier, ALuint,name, ALsizei,length, const ALchar*,label)
FORCE_ALIGN void AL_APIENTRY alObjectLabelDirectEXT(ALCcontext *context, ALenum identifier,
    ALuint name, ALsizei length, const ALchar *label) noexcept
{
    if(!label && length != 0) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "Null label pointer");

    auto objname = (length < 0) ? std::string_view{label}
        : std::string_view{label, static_cast<uint>(length)};
    if(objname.length() >= MaxObjectLabelLength) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "Object label length too long (%zu >= %d)",
            objname.length(), MaxObjectLabelLength);

    if(identifier == AL_SOURCE_EXT)
        return ALsource::SetName(context, name, objname);
    if(identifier == AL_BUFFER)
        return ALbuffer::SetName(context, name, objname);
    if(identifier == AL_FILTER_EXT)
        return ALfilter::SetName(context, name, objname);
    if(identifier == AL_EFFECT_EXT)
        return ALeffect::SetName(context, name, objname);
    if(identifier == AL_AUXILIARY_EFFECT_SLOT_EXT)
        return ALeffectslot::SetName(context, name, objname);

    return context->setError(AL_INVALID_ENUM, "Invalid name identifier 0x%04x", identifier);
}

FORCE_ALIGN DECL_FUNCEXT5(void, alGetObjectLabel,EXT, ALenum,identifier, ALuint,name, ALsizei,bufSize, ALsizei*,length, ALchar*,label)
FORCE_ALIGN void AL_APIENTRY alGetObjectLabelDirectEXT(ALCcontext *context, ALenum identifier,
    ALuint name, ALsizei bufSize, ALsizei *length, ALchar *label) noexcept
{
    if(bufSize < 0) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "Negative label bufSize");

    if(!label && !length) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "Null length and label");
    if(label && bufSize == 0) UNLIKELY
        return context->setError(AL_INVALID_VALUE, "Zero label bufSize");

    auto copy_name = [name,bufSize,length,label](std::unordered_map<ALuint,std::string> &names)
    {
        std::string_view objname;

        auto iter = names.find(name);
        if(iter != names.end())
            objname = iter->second;

        if(!label)
            *length = static_cast<ALsizei>(objname.length());
        else
        {
            const size_t tocopy{std::min(objname.size(), static_cast<uint>(bufSize)-1_uz)};
            std::memcpy(label, objname.data(), tocopy);
            label[tocopy] = '\0';
            if(length)
                *length = static_cast<ALsizei>(tocopy);
        }
    };

    if(identifier == AL_SOURCE_EXT)
    {
        std::lock_guard srclock{context->mSourceLock};
        copy_name(context->mSourceNames);
    }
    else if(identifier == AL_BUFFER)
    {
        ALCdevice *device{context->mALDevice.get()};
        std::lock_guard buflock{device->BufferLock};
        copy_name(device->mBufferNames);
    }
    else if(identifier == AL_FILTER_EXT)
    {
        ALCdevice *device{context->mALDevice.get()};
        std::lock_guard filterlock{device->FilterLock};
        copy_name(device->mFilterNames);
    }
    else if(identifier == AL_EFFECT_EXT)
    {
        ALCdevice *device{context->mALDevice.get()};
        std::lock_guard effectlock{device->EffectLock};
        copy_name(device->mEffectNames);
    }
    else if(identifier == AL_AUXILIARY_EFFECT_SLOT_EXT)
    {
        std::lock_guard slotlock{context->mEffectSlotLock};
        copy_name(context->mEffectSlotNames);
    }
    else
        context->setError(AL_INVALID_ENUM, "Invalid name identifier 0x%04x", identifier);
}
