
#include "config.h"

#include "events.h"

#include <ranges>
#include <span>

#include "alnumeric.h"
#include "core/logging.h"
#include "device.h"
#include "fmt/core.h"
#include "gsl/gsl"


namespace {

auto EnumFromEventType(const alc::EventType type) -> ALCenum
{
    switch(type)
    {
    case alc::EventType::DefaultDeviceChanged: return ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT;
    case alc::EventType::DeviceAdded: return ALC_EVENT_TYPE_DEVICE_ADDED_SOFT;
    case alc::EventType::DeviceRemoved: return ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT;
    case alc::EventType::Count: break;
    }
    throw std::runtime_error{fmt::format("Invalid EventType: {}", int{al::to_underlying(type)})};
}

} // namespace

namespace alc {

auto GetEventType(ALCenum type) -> std::optional<alc::EventType>
{
    switch(type)
    {
    case ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT: return alc::EventType::DefaultDeviceChanged;
    case ALC_EVENT_TYPE_DEVICE_ADDED_SOFT: return alc::EventType::DeviceAdded;
    case ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT: return alc::EventType::DeviceRemoved;
    }
    return std::nullopt;
}

void Event(EventType eventType, DeviceType deviceType, ALCdevice *device, std::string_view message)
    noexcept
{
    auto eventlock = std::unique_lock{EventMutex};
    if(EventCallback && EventsEnabled.test(al::to_underlying(eventType)))
        EventCallback(EnumFromEventType(eventType), al::to_underlying(deviceType), device,
            /* NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage) */
            gsl::narrow_cast<ALCsizei>(message.size()), message.data(), EventUserPtr);
}

} // namespace alc

FORCE_ALIGN auto ALC_APIENTRY alcEventControlSOFT(ALCsizei count, const ALCenum *events,
    ALCboolean enable) noexcept -> ALCboolean
{
    if(enable != ALC_FALSE && enable != ALC_TRUE)
    {
        alcSetError(nullptr, ALC_INVALID_ENUM);
        return ALC_FALSE;
    }
    if(count < 0)
    {
        alcSetError(nullptr, ALC_INVALID_VALUE);
        return ALC_FALSE;
    }
    if(count == 0)
        return ALC_TRUE;
    if(!events)
    {
        alcSetError(nullptr, ALC_INVALID_VALUE);
        return ALC_FALSE;
    }

    auto eventSet = alc::EventBitSet{0};
    auto eventrange = std::views::counted(events, count);
    const auto invalidevent = std::ranges::find_if_not(eventrange, [&eventSet](ALCenum type)
    {
        const auto etype = alc::GetEventType(type);
        if(!etype) return false;

        eventSet.set(al::to_underlying(*etype));
        return true;
    });
    if(invalidevent != eventrange.end())
    {
        WARN("Invalid event type: {:#04x}", as_unsigned(*invalidevent));
        alcSetError(nullptr, ALC_INVALID_ENUM);
        return ALC_FALSE;
    }

    auto eventlock = std::unique_lock{alc::EventMutex};
    if(enable) alc::EventsEnabled |= eventSet;
    else alc::EventsEnabled &= ~eventSet;
    return ALC_TRUE;
}

FORCE_ALIGN void ALC_APIENTRY alcEventCallbackSOFT(ALCEVENTPROCTYPESOFT callback, void *userParam) noexcept
{
    auto eventlock = std::unique_lock{alc::EventMutex};
    alc::EventCallback = callback;
    alc::EventUserPtr = userParam;
}
