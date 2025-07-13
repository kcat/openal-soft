#include "config.h"

#include "debug.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
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
#include "alnumeric.h"
#include "auxeffectslot.h"
#include "buffer.h"
#include "core/except.h"
#include "core/logging.h"
#include "core/voice.h"
#include "direct_defs.h"
#include "effect.h"
#include "filter.h"
#include "fmt/core.h"
#include "gsl/gsl"
#include "intrusive_ptr.h"
#include "opthelpers.h"
#include "source.h"


/* Declared here to prevent compilers from thinking it should be inlined, which
 * GCC warns about increasing code size.
 */
DebugGroup::~DebugGroup() = default;

namespace {

using namespace std::string_view_literals;

static_assert(DebugSeverityBase+DebugSeverityCount <= 32, "Too many debug bits");

template<typename T, T ...Vals>
constexpr auto make_array_sequence(std::integer_sequence<T, Vals...>)
{ return std::array<T,sizeof...(Vals)>{Vals...}; }

template<typename T, size_t N>
constexpr auto make_array_sequence()
{ return make_array_sequence(std::make_integer_sequence<T,N>{}); }


constexpr auto GetDebugSource(ALenum source) noexcept -> std::optional<DebugSource>
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

constexpr auto GetDebugType(ALenum type) noexcept -> std::optional<DebugType>
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

constexpr auto GetDebugSeverity(ALenum severity) noexcept -> std::optional<DebugSeverity>
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


constexpr auto GetDebugSourceEnum(DebugSource source) -> ALenum
{
    switch(source)
    {
    case DebugSource::API: return AL_DEBUG_SOURCE_API_EXT;
    case DebugSource::System: return AL_DEBUG_SOURCE_AUDIO_SYSTEM_EXT;
    case DebugSource::ThirdParty: return AL_DEBUG_SOURCE_THIRD_PARTY_EXT;
    case DebugSource::Application: return AL_DEBUG_SOURCE_APPLICATION_EXT;
    case DebugSource::Other: return AL_DEBUG_SOURCE_OTHER_EXT;
    }
    throw std::runtime_error{fmt::format("Unexpected debug source value: {}",
        int{al::to_underlying(source)})};
}

constexpr auto GetDebugTypeEnum(DebugType type) -> ALenum
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
    throw std::runtime_error{fmt::format("Unexpected debug type value: {}",
        int{al::to_underlying(type)})};
}

constexpr auto GetDebugSeverityEnum(DebugSeverity severity) -> ALenum
{
    switch(severity)
    {
    case DebugSeverity::High: return AL_DEBUG_SEVERITY_HIGH_EXT;
    case DebugSeverity::Medium: return AL_DEBUG_SEVERITY_MEDIUM_EXT;
    case DebugSeverity::Low: return AL_DEBUG_SEVERITY_LOW_EXT;
    case DebugSeverity::Notification: return AL_DEBUG_SEVERITY_NOTIFICATION_EXT;
    }
    throw std::runtime_error{fmt::format("Unexpected debug severity value: {}",
        int{al::to_underlying(severity)})};
}


constexpr auto GetDebugSourceName(DebugSource source) noexcept -> std::string_view
{
    switch(source)
    {
    case DebugSource::API: return "API"sv;
    case DebugSource::System: return "Audio System"sv;
    case DebugSource::ThirdParty: return "Third Party"sv;
    case DebugSource::Application: return "Application"sv;
    case DebugSource::Other: return "Other"sv;
    }
    return "<invalid source>"sv;
}

constexpr auto GetDebugTypeName(DebugType type) noexcept -> std::string_view
{
    switch(type)
    {
    case DebugType::Error: return "Error"sv;
    case DebugType::DeprecatedBehavior: return "Deprecated Behavior"sv;
    case DebugType::UndefinedBehavior: return "Undefined Behavior"sv;
    case DebugType::Portability: return "Portability"sv;
    case DebugType::Performance: return "Performance"sv;
    case DebugType::Marker: return "Marker"sv;
    case DebugType::PushGroup: return "Push Group"sv;
    case DebugType::PopGroup: return "Pop Group"sv;
    case DebugType::Other: return "Other"sv;
    }
    return "<invalid type>"sv;
}

constexpr auto GetDebugSeverityName(DebugSeverity severity) noexcept -> std::string_view
{
    switch(severity)
    {
    case DebugSeverity::High: return "High"sv;
    case DebugSeverity::Medium: return "Medium"sv;
    case DebugSeverity::Low: return "Low"sv;
    case DebugSeverity::Notification: return "Notification"sv;
    }
    return "<invalid severity>"sv;
}


void AL_APIENTRY alDebugMessageCallbackImplEXT(gsl::not_null<ALCcontext*> context,
    ALDEBUGPROCEXT callback, void *userParam) noexcept
{
    auto debuglock = std::lock_guard{context->mDebugCbLock};
    context->mDebugCb = callback;
    context->mDebugParam = userParam;
}


void AL_APIENTRY alPushDebugGroupImplEXT(gsl::not_null<ALCcontext*> context, ALenum source,
    ALuint id, ALsizei length, const ALchar *message) noexcept
try {
    if(length < 0)
    {
        const auto newlen = std::strlen(message);
        if(newlen >= MaxDebugMessageLength)
            context->throw_error(AL_INVALID_VALUE, "Debug message too long ({} >= {})", newlen,
                MaxDebugMessageLength);
        length = gsl::narrow_cast<ALsizei>(newlen);
    }
    else if(length >= MaxDebugMessageLength)
        context->throw_error(AL_INVALID_VALUE, "Debug message too long ({} >= {})", length,
            MaxDebugMessageLength);

    const auto dsource = GetDebugSource(source);
    if(!dsource)
        context->throw_error(AL_INVALID_ENUM, "Invalid debug source {:#04x}", as_unsigned(source));
    if(*dsource != DebugSource::ThirdParty && *dsource != DebugSource::Application)
        context->throw_error(AL_INVALID_ENUM, "Debug source {:#04x} not allowed",
            as_unsigned(source));

    auto debuglock = std::unique_lock{context->mDebugCbLock};
    if(context->mDebugGroups.size() >= MaxDebugGroupDepth)
        context->throw_error(AL_STACK_OVERFLOW_EXT, "Pushing too many debug groups");

    context->mDebugGroups.emplace_back(*dsource, id,
        std::string_view{message, gsl::narrow_cast<uint>(length)});
    auto &oldback = *(context->mDebugGroups.end()-2);
    auto &newback = context->mDebugGroups.back();

    newback.mFilters = oldback.mFilters;
    newback.mIdFilters = oldback.mIdFilters;

    if(context->mContextFlags.test(ContextFlags::DebugBit))
        context->sendDebugMessage(debuglock, newback.mSource, DebugType::PushGroup, newback.mId,
            DebugSeverity::Notification, newback.mMessage);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alPopDebugGroupImplEXT(gsl::not_null<ALCcontext*> context) noexcept
try {
    auto debuglock = std::unique_lock{context->mDebugCbLock};
    if(context->mDebugGroups.size() <= 1)
        context->throw_error(AL_STACK_UNDERFLOW_EXT, "Attempting to pop the default debug group");

    auto &debug = context->mDebugGroups.back();
    const auto source = debug.mSource;
    const auto id = debug.mId;
    auto message = std::move(debug.mMessage);

    context->mDebugGroups.pop_back();
    if(context->mContextFlags.test(ContextFlags::DebugBit))
        context->sendDebugMessage(debuglock, source, DebugType::PopGroup, id,
                                  DebugSeverity::Notification, message);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


void AL_APIENTRY alObjectLabelImplEXT(gsl::not_null<ALCcontext*> context, ALenum identifier,
    ALuint name, ALsizei length, const ALchar *label) noexcept
try {
    if(!label && length != 0)
        context->throw_error(AL_INVALID_VALUE, "Null label pointer");

    auto objname = (length < 0) ? std::string_view{label}
        : std::string_view{label, gsl::narrow_cast<uint>(length)};
    if(objname.size() >= MaxObjectLabelLength)
        context->throw_error(AL_INVALID_VALUE, "Object label length too long ({} >= {})",
            objname.size(), MaxObjectLabelLength);

    switch(identifier)
    {
    case AL_SOURCE_EXT: ALsource::SetName(context, name, objname); return;
    case AL_BUFFER: ALbuffer::SetName(context, name, objname); return;
    case AL_FILTER_EXT: ALfilter::SetName(context, name, objname); return;
    case AL_EFFECT_EXT: ALeffect::SetName(context, name, objname); return;
    case AL_AUXILIARY_EFFECT_SLOT_EXT: ALeffectslot::SetName(context, name, objname); return;
    }

    context->throw_error(AL_INVALID_ENUM, "Invalid name identifier {:#04x}",
        as_unsigned(identifier));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

void AL_APIENTRY alGetObjectLabelImplEXT(gsl::not_null<ALCcontext*> context, ALenum identifier,
    ALuint name, ALsizei bufSize, ALsizei *length, ALchar *label) noexcept
try {
    if(bufSize < 0)
        context->throw_error(AL_INVALID_VALUE, "Negative label bufSize");

    if(!label && !length)
        context->throw_error(AL_INVALID_VALUE, "Null length and label");
    if(label && bufSize == 0)
        context->throw_error(AL_INVALID_VALUE, "Zero label bufSize");

    const auto labelOut = std::span{label, label ? gsl::narrow_cast<ALuint>(bufSize) : 0u};
    auto copy_name = [name,length,labelOut](std::unordered_map<ALuint,std::string> &names)
    {
        const auto objname = std::invoke([name,&names]
        {
            if(auto iter = names.find(name); iter != names.end())
                return std::string_view{iter->second};
            return std::string_view{};
        });

        if(labelOut.empty())
            *length = gsl::narrow_cast<ALsizei>(objname.size());
        else
        {
            const auto namerange = objname | std::views::take(labelOut.size()-1);
            auto oiter = std::ranges::copy(namerange, labelOut.begin()).out;
            *oiter = '\0';
            if(length)
                *length = gsl::narrow_cast<ALsizei>(namerange.size());
        }
    };

    if(identifier == AL_SOURCE_EXT)
    {
        auto srclock = std::lock_guard{context->mSourceLock};
        copy_name(context->mSourceNames);
    }
    else if(identifier == AL_BUFFER)
    {
        auto *device = context->mALDevice.get();
        auto buflock = std::lock_guard{device->BufferLock};
        copy_name(device->mBufferNames);
    }
    else if(identifier == AL_FILTER_EXT)
    {
        auto *device = context->mALDevice.get();
        auto buflock = std::lock_guard{device->FilterLock};
        copy_name(device->mFilterNames);
    }
    else if(identifier == AL_EFFECT_EXT)
    {
        auto *device = context->mALDevice.get();
        auto buflock = std::lock_guard{device->EffectLock};
        copy_name(device->mEffectNames);
    }
    else if(identifier == AL_AUXILIARY_EFFECT_SLOT_EXT)
    {
        auto slotlock = std::lock_guard{context->mEffectSlotLock};
        copy_name(context->mEffectSlotNames);
    }
    else
        context->throw_error(AL_INVALID_ENUM, "Invalid name identifier {:#04x}",
                             as_unsigned(identifier));
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}

} // namespace


void ALCcontext::sendDebugMessage(std::unique_lock<std::mutex> &debuglock, DebugSource source,
    DebugType type, ALuint id, DebugSeverity severity, std::string_view message)
{
    if(!mDebugEnabled.load(std::memory_order_relaxed)) [[unlikely]]
        return;

    if(message.length() >= MaxDebugMessageLength) [[unlikely]]
    {
        ERR("Debug message too long ({} >= {}):\n-> {}", message.length(),
            MaxDebugMessageLength, message);
        return;
    }

    auto &debug = mDebugGroups.back();

    const auto idfilter = (1_u64 << (DebugSourceBase+al::to_underlying(source)))
        | (1_u64 << (DebugTypeBase+al::to_underlying(type)))
        | (uint64_t{id} << 32);
    const auto iditer = std::ranges::lower_bound(debug.mIdFilters, idfilter);
    if(iditer != debug.mIdFilters.cend() && *iditer == idfilter)
        return;

    const auto filter = (1u << (DebugSourceBase+al::to_underlying(source)))
        | (1u << (DebugTypeBase+al::to_underlying(type)))
        | (1u << (DebugSeverityBase+al::to_underlying(severity)));
    const auto iter = std::ranges::lower_bound(debug.mFilters, filter);
    if(iter != debug.mFilters.cend() && *iter == filter)
        return;

    if(mDebugCb)
    {
        auto callback = mDebugCb;
        auto param = mDebugParam;
        debuglock.unlock();
        callback(GetDebugSourceEnum(source), GetDebugTypeEnum(type), id,
            GetDebugSeverityEnum(severity), gsl::narrow_cast<ALsizei>(message.size()),
            message.data(), param); /* NOLINT(bugprone-suspicious-stringview-data-usage) */
    }
    else
    {
        if(mDebugLog.size() < MaxDebugLoggedMessages)
            mDebugLog.emplace_back(source, type, id, severity, message);
        else [[unlikely]]
            ERR("Debug message log overflow. Lost message:\n"
                "  Source: {}\n"
                "  Type: {}\n"
                "  ID: {}\n"
                "  Severity: {}\n"
                "  Message: \"{}\"",
                GetDebugSourceName(source), GetDebugTypeName(type), id,
                GetDebugSeverityName(severity), message);
    }
}


FORCE_ALIGN DECL_FUNCEXT2(void, alDebugMessageCallback,EXT, ALDEBUGPROCEXT,callback, void*,userParam)


FORCE_ALIGN DECL_FUNCEXT6(void, alDebugMessageInsert,EXT, ALenum,source, ALenum,type, ALuint,id, ALenum,severity, ALsizei,length, const ALchar*,message)
FORCE_ALIGN void AL_APIENTRY alDebugMessageInsertDirectEXT(ALCcontext *context, ALenum source,
    ALenum type, ALuint id, ALenum severity, ALsizei length, const ALchar *message) noexcept
try {
    if(!context->mContextFlags.test(ContextFlags::DebugBit))
        return;

    if(!message)
        context->throw_error(AL_INVALID_VALUE, "Null message pointer");

    const auto msgview = (length < 0) ? std::string_view{message}
        : std::string_view{message, gsl::narrow_cast<uint>(length)};
    if(msgview.size() >= MaxDebugMessageLength)
        context->throw_error(AL_INVALID_VALUE, "Debug message too long ({} >= {})", msgview.size(),
            MaxDebugMessageLength);

    const auto dsource = GetDebugSource(source);
    if(!dsource)
        context->throw_error(AL_INVALID_ENUM, "Invalid debug source {:#04x}", as_unsigned(source));
    if(*dsource != DebugSource::ThirdParty && *dsource != DebugSource::Application)
        context->throw_error(AL_INVALID_ENUM, "Debug source {:#04x} not allowed",
            as_unsigned(source));

    const auto dtype = GetDebugType(type);
    if(!dtype)
        context->throw_error(AL_INVALID_ENUM, "Invalid debug type {:#04x}", as_unsigned(type));

    const auto dseverity = GetDebugSeverity(severity);
    if(!dseverity)
        context->throw_error(AL_INVALID_ENUM, "Invalid debug severity {:#04x}",
            as_unsigned(severity));

    context->debugMessage(*dsource, *dtype, id, *dseverity, msgview);
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


FORCE_ALIGN DECL_FUNCEXT6(void, alDebugMessageControl,EXT, ALenum,source, ALenum,type, ALenum,severity, ALsizei,count, const ALuint*,ids, ALboolean,enable)
FORCE_ALIGN void AL_APIENTRY alDebugMessageControlDirectEXT(ALCcontext *context, ALenum source,
    ALenum type, ALenum severity, ALsizei count, const ALuint *ids, ALboolean enable) noexcept
try {
    if(count > 0)
    {
        if(!ids)
            context->throw_error(AL_INVALID_VALUE, "IDs is null with non-0 count");
        if(source == AL_DONT_CARE_EXT)
            context->throw_error(AL_INVALID_OPERATION,
                "Debug source cannot be AL_DONT_CARE_EXT with IDs");
        if(type == AL_DONT_CARE_EXT)
            context->throw_error(AL_INVALID_OPERATION,
                "Debug type cannot be AL_DONT_CARE_EXT with IDs");
        if(severity != AL_DONT_CARE_EXT)
            context->throw_error(AL_INVALID_OPERATION,
                "Debug severity must be AL_DONT_CARE_EXT with IDs");
    }

    if(enable != AL_TRUE && enable != AL_FALSE)
        context->throw_error(AL_INVALID_ENUM, "Invalid debug enable {}", enable);

    static constexpr auto ElemCount = DebugSourceCount + DebugTypeCount + DebugSeverityCount;
    static constexpr auto Values = make_array_sequence<uint8_t,ElemCount>();

    auto srcIdxs = std::span{Values}.subspan(DebugSourceBase,DebugSourceCount);
    if(source != AL_DONT_CARE_EXT)
    {
        auto dsource = GetDebugSource(source);
        if(!dsource)
            context->throw_error(AL_INVALID_ENUM, "Invalid debug source {:#04x}",
                as_unsigned(source));
        srcIdxs = srcIdxs.subspan(al::to_underlying(*dsource), 1);
    }

    auto typeIdxs = std::span{Values}.subspan(DebugTypeBase,DebugTypeCount);
    if(type != AL_DONT_CARE_EXT)
    {
        auto dtype = GetDebugType(type);
        if(!dtype)
            context->throw_error(AL_INVALID_ENUM, "Invalid debug type {:#04x}", as_unsigned(type));
        typeIdxs = typeIdxs.subspan(al::to_underlying(*dtype), 1);
    }

    auto svrIdxs = std::span{Values}.subspan(DebugSeverityBase,DebugSeverityCount);
    if(severity != AL_DONT_CARE_EXT)
    {
        auto dseverity = GetDebugSeverity(severity);
        if(!dseverity)
            context->throw_error(AL_INVALID_ENUM, "Invalid debug severity {:#04x}",
                as_unsigned(severity));
        svrIdxs = svrIdxs.subspan(al::to_underlying(*dseverity), 1);
    }

    auto debuglock = std::lock_guard{context->mDebugCbLock};
    auto &debug = context->mDebugGroups.back();
    if(count > 0)
    {
        const auto filterbase = (1u<<srcIdxs[0]) | (1u<<typeIdxs[0]);

        std::ranges::for_each(std::views::counted(ids, count),
            [enable,filterbase,&debug](const uint id)
        {
            const auto filter = uint64_t{filterbase} | (uint64_t{id} << 32);

            const auto iter = std::ranges::lower_bound(debug.mIdFilters, filter);
            if(!enable && (iter == debug.mIdFilters.cend() || *iter != filter))
                debug.mIdFilters.insert(iter, filter);
            else if(enable && iter != debug.mIdFilters.cend() && *iter == filter)
                debug.mIdFilters.erase(iter);
        });
    }
    else
    {
        /* C++23 has std::views::cartesian(srcIdxs, typeIdxs, svrIdxs) for a
         * range that gives all value combinations of the given ranges.
         */
        std::ranges::for_each(srcIdxs, [enable,typeIdxs,svrIdxs,&debug](const uint srcidx)
        {
            const auto srcfilt = 1u<<srcidx;
            std::ranges::for_each(typeIdxs, [enable,srcfilt,svrIdxs,&debug](const uint typeidx)
            {
                const auto srctype = srcfilt | (1u<<typeidx);
                std::ranges::for_each(svrIdxs, [enable,srctype,&debug](const uint svridx)
                {
                    const auto filter = srctype | (1u<<svridx);
                    auto iter = std::ranges::lower_bound(debug.mFilters, filter);
                    if(!enable && (iter == debug.mFilters.cend() || *iter != filter))
                        debug.mFilters.insert(iter, filter);
                    else if(enable && iter != debug.mFilters.cend() && *iter == filter)
                        debug.mFilters.erase(iter);
                });
            });
        });
    }
}
catch(al::base_exception&) {
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
}


FORCE_ALIGN DECL_FUNCEXT4(void, alPushDebugGroup,EXT, ALenum,source, ALuint,id, ALsizei,length, const ALchar*,message)
FORCE_ALIGN DECL_FUNCEXT(void, alPopDebugGroup,EXT)


FORCE_ALIGN DECL_FUNCEXT8(ALuint, alGetDebugMessageLog,EXT, ALuint,count, ALsizei,logBufSize, ALenum*,sources, ALenum*,types, ALuint*,ids, ALenum*,severities, ALsizei*,lengths, ALchar*,logBuf)
FORCE_ALIGN ALuint AL_APIENTRY alGetDebugMessageLogDirectEXT(ALCcontext *context, ALuint count,
    ALsizei logBufSize, ALenum *sources, ALenum *types, ALuint *ids, ALenum *severities,
    ALsizei *lengths, ALchar *logBuf) noexcept
try {
    if(logBuf && logBufSize < 0)
        context->throw_error(AL_INVALID_VALUE, "Negative debug log buffer size");

    auto debuglock = std::lock_guard{context->mDebugCbLock};
    /* Calculate the number of log entries to get, depending on the log buffer
     * size (if applicable), the number of logged messages, and the requested
     * count.
     */
    const auto toget = std::invoke([context,count,logBuf,logBufSize]() -> ALuint
    {
        /* NOTE: The log buffer size is relevant when the log buffer is
         * non-NULL, including when the size is 0.
         */
        if(logBuf)
        {
            const auto logSpan = std::span{logBuf, gsl::narrow_cast<ALuint>(logBufSize)};
            auto counter = 0_uz;
            auto todo = 0u;
            std::ignore = std::ranges::find_if(context->mDebugLog | std::views::take(count),
                [logSpan,&counter,&todo](const DebugLogEntry &entry) noexcept -> bool
            {
                const auto tocopy = size_t{entry.mMessage.size() + 1};
                if(tocopy > logSpan.size()-counter)
                    return true;
                counter += tocopy;
                ++todo;
                return false;
            });
            return todo;
        }
        return gsl::narrow_cast<ALuint>(std::min(context->mDebugLog.size(), size_t{count}));
    });
    if(toget < 1)
        return 0;

    auto logrange = context->mDebugLog | std::views::take(toget);
    if(sources)
        std::ranges::transform(logrange | std::views::transform(&DebugLogEntry::mSource),
            std::span{sources, toget}.begin(), GetDebugSourceEnum);
    if(types)
        std::ranges::transform(logrange | std::views::transform(&DebugLogEntry::mType),
            std::span{types, toget}.begin(), GetDebugTypeEnum);
    if(ids)
        std::ranges::transform(logrange, std::span{ids, toget}.begin(), &DebugLogEntry::mId);
    if(severities)
        std::ranges::transform(logrange | std::views::transform(&DebugLogEntry::mSeverity),
            std::span{severities, toget}.begin(), GetDebugSeverityEnum);
    if(lengths)
    {
        std::ranges::transform(logrange, std::span{lengths, toget}.begin(),
            [](const DebugLogEntry &entry)
        { return gsl::narrow_cast<ALsizei>(entry.mMessage.size()+1); });
    }

    if(logBuf)
    {
        const auto logSpan = std::span{logBuf, gsl::narrow_cast<ALuint>(logBufSize)};
        /* C++23...
        std::ranges::copy(logrange | std::views::transform(&DebugLogEntry::mMessage)
            | std::views::join_with('\0'), logSpan.begin());
        */
        auto logiter = logSpan.begin();
        std::ranges::for_each(logrange, [&logiter](const std::string_view msg)
        {
            logiter = std::ranges::copy(msg, logiter).out;
            *(logiter++) = '\0';
        }, &DebugLogEntry::mMessage);
    }

    /* FIXME: Ugh. Calling erase(begin(), begin()+toget) causes an error since
     * DebugLogEntry can't be moved/copied. Not sure how else to pop a number
     * of elements from the front of a deque without it trying to instantiate a
     * move/copy.
     */
    std::ranges::for_each(std::views::iota(0u, toget),
        [context](auto&&){ context->mDebugLog.pop_front(); });

    return toget;
}
catch(al::base_exception&) {
    return 0;
}
catch(std::exception &e) {
    ERR("Caught exception: {}", e.what());
    return 0;
}

FORCE_ALIGN DECL_FUNCEXT4(void, alObjectLabel,EXT, ALenum,identifier, ALuint,name, ALsizei,length, const ALchar*,label)
FORCE_ALIGN DECL_FUNCEXT5(void, alGetObjectLabel,EXT, ALenum,identifier, ALuint,name, ALsizei,bufSize, ALsizei*,length, ALchar*,label)
