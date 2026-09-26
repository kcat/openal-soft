#ifndef ALC_EVENTS_H
#define ALC_EVENTS_H

#include "inprogext.h"

#include <cstdint>
#include <mutex>
#include <optional>

#include "zstring_view.hpp"

namespace alc {

enum class EventType : std::uint8_t {
    DefaultDeviceChanged,
    DeviceAdded,
    DeviceRemoved,

    MaxValue = DeviceRemoved
};

auto GetEventType(ALCenum type) -> std::optional<EventType>;

enum class EventSupport : ALCenum {
    FullSupport = ALC_EVENT_SUPPORTED_SOFT,
    NoSupport = ALC_EVENT_NOT_SUPPORTED_SOFT,
};

enum class DeviceType : ALCenum {
    Playback = ALC_PLAYBACK_DEVICE_SOFT,
    Capture = ALC_CAPTURE_DEVICE_SOFT,
};

inline auto EventMutex = std::mutex{};

inline auto EventCallback = ALCEVENTPROCTYPESOFT{};
inline void *EventUserPtr{};

auto Event(EventType eventType, DeviceType deviceType, ALCdevice *device, al::zstring_view message)
    noexcept -> void;

inline
auto Event(EventType const eventType, DeviceType const deviceType, al::zstring_view const message)
    noexcept -> void
{ Event(eventType, deviceType, nullptr, message); }

} // namespace alc

#endif /* ALC_EVENTS_H */
