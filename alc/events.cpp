
#include "config.h"

#include "events.h"

#include <ranges>
#include <span>

#include "alnumeric.h"
#include "backends/base.h"
#include "bitset.hpp"
#include "opthelpers.h"

#if HAVE_CXXMODULES
import alc.device;
import format;
import gsl;
import logging;
#else
#include "alc/device.h"
#include "alformat.hpp"
#include "core/logging.h"
#include "gsl/gsl"
#endif


namespace {

#if defined(__linux__) && !defined(AL_LIBTYPE_STATIC) && __has_cpp_attribute(gnu::alias)
#define DefineAlcAlias(X) extern "C" DECL_HIDDEN [[gnu::alias(#X)]] decltype(X) X##_;
#else
#define DefineAlcAlias(X)
#endif

using EventBitSet = al::bitset<alc::EventType>;
auto gEventsEnabled = EventBitSet{0};

[[nodiscard]] constexpr
auto EnumFromEventType(const alc::EventType type) -> ALCenum
{
    switch(type)
    {
    case alc::EventType::DefaultDeviceChanged: return ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT;
    case alc::EventType::DeviceAdded: return ALC_EVENT_TYPE_DEVICE_ADDED_SOFT;
    case alc::EventType::DeviceRemoved: return ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT;
    }
    throw std::runtime_error{al::format("Invalid EventType: {}", int{al::to_underlying(type)})};
}

[[nodiscard]] constexpr
auto GetEventType(ALCenum const type) -> std::optional<alc::EventType>
{
    switch(type)
    {
        case ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT:
            return alc::EventType::DefaultDeviceChanged;
        case ALC_EVENT_TYPE_DEVICE_ADDED_SOFT: return alc::EventType::DeviceAdded;
        case ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT: return alc::EventType::DeviceRemoved;
    }
    return std::nullopt;
}

} // namespace

namespace alc {

void Event(EventType const eventType, DeviceType const deviceType, ALCdevice *const device,
    al::zstring_view const message) noexcept
{
    auto eventlock = std::unique_lock{EventMutex};
    if(EventCallback && gEventsEnabled.test(eventType))
        EventCallback(EnumFromEventType(eventType), al::to_underlying(deviceType), device,
            al::saturate_cast<ALCsizei>(message.size()), message.c_str(), EventUserPtr);
}

} /* namespace alc */

FORCE_ALIGN auto ALC_APIENTRY alcEventIsSupportedSOFT(ALCenum const eventType,
    ALCenum const deviceType) noexcept -> ALCenum
{
    auto const etype = GetEventType(eventType);
    if(!etype)
    {
        WARN("Invalid event type: {:#04x}", as_unsigned(eventType));
        al::Device::SetGlobalError(ALC_INVALID_ENUM);
        return ALC_FALSE;
    }

    auto supported = alc::EventSupport::NoSupport;
    switch(deviceType)
    {
        case ALC_PLAYBACK_DEVICE_SOFT:
            if(PlaybackFactory)
                supported = PlaybackFactory->queryEventSupport(*etype, BackendType::Playback);
            return al::to_underlying(supported);

        case ALC_CAPTURE_DEVICE_SOFT:
            if(CaptureFactory)
                supported = CaptureFactory->queryEventSupport(*etype, BackendType::Capture);
            return al::to_underlying(supported);
    }
    WARN("Invalid device type: {:#04x}", as_unsigned(deviceType));
    al::Device::SetGlobalError(ALC_INVALID_ENUM);
    return ALC_FALSE;
}
DefineAlcAlias(alcEventIsSupportedSOFT)

FORCE_ALIGN auto ALC_APIENTRY alcEventControlSOFT(ALCsizei const count, ALCenum const*const events,
    ALCboolean const enable) noexcept -> ALCboolean
{
    if(enable != ALC_FALSE && enable != ALC_TRUE)
    {
        al::Device::SetGlobalError(ALC_INVALID_ENUM);
        return ALC_FALSE;
    }
    if(count < 0)
    {
        al::Device::SetGlobalError(ALC_INVALID_VALUE);
        return ALC_FALSE;
    }
    if(count == 0)
        return ALC_TRUE;
    if(!events)
    {
        al::Device::SetGlobalError(ALC_INVALID_VALUE);
        return ALC_FALSE;
    }

    auto eventSet = EventBitSet{};
    auto eventrange = std::views::counted(events, count);
    const auto invalidevent = std::ranges::find_if_not(eventrange, [&eventSet](ALCenum const type)
    {
        const auto etype = GetEventType(type);
        if(!etype) return false;

        eventSet.set(*etype);
        return true;
    });
    if(invalidevent != eventrange.end())
    {
        WARN("Invalid event type: {:#04x}", as_unsigned(*invalidevent));
        al::Device::SetGlobalError(ALC_INVALID_ENUM);
        return ALC_FALSE;
    }

    auto eventlock = std::unique_lock{alc::EventMutex};
    if(enable) gEventsEnabled |= eventSet;
    else gEventsEnabled &= ~eventSet;
    return ALC_TRUE;
}
DefineAlcAlias(alcEventControlSOFT)

FORCE_ALIGN auto ALC_APIENTRY alcEventCallbackSOFT(ALCEVENTPROCTYPESOFT const callback,
    void *const userParam) noexcept -> void
{
    auto eventlock = std::unique_lock{alc::EventMutex};
    alc::EventCallback = callback;
    alc::EventUserPtr = userParam;
}
DefineAlcAlias(alcEventCallbackSOFT)
