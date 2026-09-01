#ifndef AL_LISTENER_H
#define AL_LISTENER_H

#include <array>

#include "AL/efx.h"

#include "almalloc.h"

namespace al {

struct Listener {
    std::array<float, 3> mPosition{{0.0f, 0.0f, 0.0f}};
    std::array<float, 3> mVelocity{{0.0f, 0.0f, 0.0f}};
    std::array<float, 3> mOrientAt{{0.0f, 0.0f, -1.0f}};
    std::array<float, 3> mOrientUp{{0.0f, 1.0f, 0.0f}};
    float mGain{1.0f};
    float mMetersPerUnit{AL_DEFAULT_METERS_PER_UNIT};

    DISABLE_ALLOC
};

} /* namespace al */
#endif
