#ifndef ALC_EVENTS_H
#define ALC_EVENTS_H

#include "inprogext.h"
#include "opthelpers.h"

#include <mutex>
#include <optional>
#include <string_view>

#include "altypes.hpp"
#include "bitset.hpp"

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

void Event(EventType eventType, DeviceType deviceType, ALCdevice *device, std::string_view message) noexcept;

inline void Event(EventType eventType, DeviceType deviceType, std::string_view message) noexcept
{ Event(eventType, deviceType, nullptr, message); }

} // namespace alc

#endif /* ALC_EVENTS_H */
