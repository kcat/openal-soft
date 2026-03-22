
#include "config.h"

#include "events.h"

#include <ranges>
#include <span>

#include "alnumeric.h"
#include "device.h"
#include "opthelpers.h"

#if HAVE_CXXMODULES
import gsl;
import logging;
#else
#include "core/logging.h"
#include "gsl/gsl"
#endif


namespace {

#if defined(__linux__) && !defined(AL_LIBTYPE_STATIC) && HAS_ATTRIBUTE(gnu::alias)
#define DefineAlcAlias(X) extern "C" DECL_HIDDEN [[gnu::alias(#X)]] decltype(X) X##_;
#else
#define DefineAlcAlias(X)
#endif

using EventBitSet = al::bitset<alc::EventType>;
auto gEventsEnabled = EventBitSet{0};

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

} // namespace

namespace alc {

auto GetEventType(ALCenum const type) -> std::optional<EventType>
{
    switch(type)
    {
    case ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT: return EventType::DefaultDeviceChanged;
    case ALC_EVENT_TYPE_DEVICE_ADDED_SOFT: return EventType::DeviceAdded;
    case ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT: return EventType::DeviceRemoved;
    }
    return std::nullopt;
}

void Event(EventType const eventType, DeviceType const deviceType, ALCdevice *const device,
    std::string_view const message) noexcept
{
    auto eventlock = std::unique_lock{EventMutex};
    if(EventCallback && gEventsEnabled.test(eventType))
        EventCallback(EnumFromEventType(eventType), al::to_underlying(deviceType), device,
            /* NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage) */
            al::saturate_cast<ALCsizei>(message.size()), message.data(), EventUserPtr);
}

} /* namespace alc */

FORCE_ALIGN auto ALC_APIENTRY alcEventControlSOFT(ALCsizei count, const ALCenum *events,
    ALCboolean enable) noexcept -> ALCboolean
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
        const auto etype = alc::GetEventType(type);
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

FORCE_ALIGN void ALC_APIENTRY alcEventCallbackSOFT(ALCEVENTPROCTYPESOFT callback, void *userParam) noexcept
{
    auto eventlock = std::unique_lock{alc::EventMutex};
    alc::EventCallback = callback;
    alc::EventUserPtr = userParam;
}
DefineAlcAlias(alcEventCallbackSOFT)
