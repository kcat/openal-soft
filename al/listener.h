#ifndef AL_LISTENER_H
#define AL_LISTENER_H

#include <array>
#include <atomic>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/efx.h"

#include "almalloc.h"


struct ALlistener {
    std::array<float,3> Position{{0.0f, 0.0f, 0.0f}};
    std::array<float,3> Velocity{{0.0f, 0.0f, 0.0f}};
    std::array<float,3> OrientAt{{0.0f, 0.0f, -1.0f}};
    std::array<float,3> OrientUp{{0.0f, 1.0f, 0.0f}};
    float Gain{1.0f};
    float mMetersPerUnit{AL_DEFAULT_METERS_PER_UNIT};

    std::atomic_flag PropsClean;

    ALlistener() { PropsClean.test_and_set(std::memory_order_relaxed); }

    DISABLE_ALLOC()
};

void UpdateListenerProps(ALCcontext *context);

#endif
