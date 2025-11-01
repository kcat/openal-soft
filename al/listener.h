#ifndef AL_LISTENER_H
#define AL_LISTENER_H

#include <array>

#include "AL/efx.h"

#include "almalloc.h"
#include "altypes.hpp"

namespace al {

struct Listener {
    std::array<f32, 3> mPosition{{0.0f, 0.0f, 0.0f}};
    std::array<f32, 3> mVelocity{{0.0f, 0.0f, 0.0f}};
    std::array<f32, 3> mOrientAt{{0.0f, 0.0f, -1.0f}};
    std::array<f32, 3> mOrientUp{{0.0f, 1.0f, 0.0f}};
    f32 mGain{1.0f};
    f32 mMetersPerUnit{AL_DEFAULT_METERS_PER_UNIT};

    DISABLE_ALLOC
};

} /* namespace al */
#endif
