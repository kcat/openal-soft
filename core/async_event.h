#ifndef CORE_EVENT_H
#define CORE_EVENT_H

#include <memory>
#include <string>
#include <variant>

#include "altypes.hpp"
#include "opthelpers.h"

struct EffectState;


enum class AsyncEnableBits : u8::value_t {
    SourceState,
    BufferCompleted,
    Disconnected,

    MaxValue = Disconnected
};


enum class AsyncSrcState : u8::value_t {
    Reset,
    Stop,
    Play,
    Pause
};

using AsyncKillThread = std::monostate;

struct AsyncSourceStateEvent {
    unsigned mId;
    AsyncSrcState mState;
};

struct AsyncBufferCompleteEvent {
    unsigned mId;
    unsigned mCount;
};

struct AsyncDisconnectEvent {
    std::string msg;
};

struct AsyncEffectReleaseEvent {
    EffectState *mEffectState;
};

using AsyncEvent = std::variant<AsyncKillThread,
        AsyncSourceStateEvent,
        AsyncBufferCompleteEvent,
        AsyncEffectReleaseEvent,
        AsyncDisconnectEvent>;

template<typename T, typename ...Args>
auto &InitAsyncEvent(AsyncEvent &event, Args&& ...args) noexcept NONBLOCKING
{
    IGNORE_FUNCTION_EFFECTS(
        auto *evt = std::construct_at(&event, std::in_place_type<T>, std::forward<Args>(args)...);
        return std::get<T>(*evt);
    )
}

#endif
