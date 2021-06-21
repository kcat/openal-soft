#ifndef CORE_EVENT_H
#define CORE_EVENT_H

#include "almalloc.h"

struct EffectState;

using uint = unsigned int;


enum {
    /* End event thread processing. */
    EventType_KillThread = 0,

    /* User event types. */
    EventType_SourceStateChange = 1<<0,
    EventType_BufferCompleted   = 1<<1,
    EventType_Disconnected      = 1<<2,

    /* Internal events. */
    EventType_ReleaseEffectState = 65536,
};

struct AsyncEvent {
    enum class SrcState {
        Reset,
        Stop,
        Play,
        Pause
    };

    uint EnumType{0u};
    union {
        char dummy;
        struct {
            uint id;
            SrcState state;
        } srcstate;
        struct {
            uint id;
            uint count;
        } bufcomp;
        struct {
            char msg[244];
        } disconnect;
        EffectState *mEffectState;
    } u{};

    AsyncEvent() noexcept = default;
    constexpr AsyncEvent(uint type) noexcept : EnumType{type} { }

    DISABLE_ALLOC()
};

#endif
