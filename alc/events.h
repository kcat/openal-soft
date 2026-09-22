#ifndef ALC_EVENTS_H
#define ALC_EVENTS_H

#include "inprogext.h"

#include <mutex>
#include <optional>

#include "altypes.hpp"
#include "zstring_view.hpp"

namespace alc {

enum class EventType : u8::value_t {
    DefaultDeviceChanged,
    DeviceAdded,
    DeviceRemoved,

    MaxValue = DeviceRemoved
};

std::optional<EventType> GetEventType(ALCenum type);

enum class EventSupport : ALCenum {
    FullSupport = ALC_EVENT_SUPPORTED_SOFT,
    NoSupport = ALC_EVENT_NOT_SUPPORTED_SOFT,
};

enum class DeviceType : ALCenum {
    Playback = ALC_PLAYBACK_DEVICE_SOFT,
    Capture = ALC_CAPTURE_DEVICE_SOFT,
};

inline std::mutex EventMutex;

inline ALCEVENTPROCTYPESOFT EventCallback{};
inline void *EventUserPtr{};

void Event(EventType eventType, DeviceType deviceType, ALCdevice *device, al::zstring_view message) noexcept;

inline void Event(EventType eventType, DeviceType deviceType, al::zstring_view message) noexcept
{ Event(eventType, deviceType, nullptr, message); }

} // namespace alc

#endif /* ALC_EVENTS_H */
