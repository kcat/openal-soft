#ifndef ALC_EVENTS_H
#define ALC_EVENTS_H

#include "inprogext.h"
#include "opthelpers.h"

#include <bitset>
#include <mutex>
#include <string_view>


namespace alc {

enum class EventType : uint8_t {
    DefaultDeviceChanged,
    DeviceAdded,
    DeviceRemoved,

    Count
};

inline std::bitset<al::to_underlying(EventType::Count)> EventsEnabled{0};

inline std::mutex EventMutex;

inline ALCEVENTPROCTYPESOFT EventCallback{};
inline void *EventUserPtr{};

void Event(alc::EventType eventType, ALCdevice *device, std::string_view message) noexcept;

inline void Event(alc::EventType eventType, std::string_view message) noexcept
{ Event(eventType, nullptr, message); }

} // namespace alc

#endif /* ALC_EVENTS_H */
