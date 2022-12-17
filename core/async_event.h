#ifndef CORE_EVENT_H
#define CORE_EVENT_H

#include "almalloc.h"

struct EffectState;

using uint = unsigned int;


struct AsyncEvent {
    enum : uint {
        /* User event types. */
        SourceStateChange,
        BufferCompleted,
        Disconnected,
        UserEventCount,

        /* Internal events, always processed. */
        ReleaseEffectState = 128,

        /* End event thread processing. */
        KillThread,
    };

    enum class SrcState {
        Reset,
        Stop,
        Play,
        Pause
    };

    const uint EnumType;
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

    constexpr AsyncEvent(uint type) noexcept : EnumType{type} { }

    DISABLE_ALLOC()
};

#endif
