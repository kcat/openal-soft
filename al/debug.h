#ifndef AL_DEBUG_H
#define AL_DEBUG_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "AL/al.h"

#include "opthelper_macros.hpp"


/* Somewhat arbitrary. Avoid letting it get out of control if the app enables
 * logging but never reads it.
 */
inline constexpr auto MaxDebugLoggedMessages = 64u;
inline constexpr auto MaxDebugMessageLength = 1024u;
inline constexpr auto MaxDebugGroupDepth = 64u;
inline constexpr auto MaxObjectLabelLength = 1024u;


inline constexpr auto DebugSourceBase = 0u;
enum class DebugSource : std::uint8_t {
    API = 0,
    System,
    ThirdParty,
    Application,
    Other,
};
inline constexpr auto DebugSourceCount = 5u;

inline constexpr auto DebugTypeBase = DebugSourceBase + DebugSourceCount;
enum class DebugType : std::uint8_t {
    Error = 0,
    DeprecatedBehavior,
    UndefinedBehavior,
    Portability,
    Performance,
    Marker,
    PushGroup,
    PopGroup,
    Other,
};
inline constexpr auto DebugTypeCount = 9u;

inline constexpr auto DebugSeverityBase = DebugTypeBase + DebugTypeCount;
enum class DebugSeverity : std::uint8_t {
    High = 0,
    Medium,
    Low,
    Notification,
};
inline constexpr auto DebugSeverityCount = 4u;

struct DebugGroup {
    ALuint const mId;
    DebugSource const mSource;
    std::string mMessage;
    std::vector<std::uint32_t> mFilters;
    std::vector<std::uint64_t> mIdFilters;

    template<typename T>
    DebugGroup(DebugSource const source, ALuint const id, T&& message)
        : mId{id}, mSource{source}, mMessage{std::forward<T>(message)}
    { }
    DebugGroup(const DebugGroup&) = default;
    DebugGroup(DebugGroup&&) = default;
    NOINLINE ~DebugGroup() = default;
};

#endif /* AL_DEBUG_H */
