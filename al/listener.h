#ifndef AL_LISTENER_H
#define AL_LISTENER_H

#include <array>
#include <atomic>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/efx.h"

#include "almalloc.h"
#include "vecmat.h"

enum class DistanceModel;


struct ALlistenerProps {
    std::array<ALfloat,3> Position;
    std::array<ALfloat,3> Velocity;
    std::array<ALfloat,3> OrientAt;
    std::array<ALfloat,3> OrientUp;
    ALfloat Gain;
    ALfloat MetersPerUnit;

    std::atomic<ALlistenerProps*> next;

    DEF_NEWDEL(ALlistenerProps)
};

struct ALlistener {
    std::array<ALfloat,3> Position{{0.0f, 0.0f, 0.0f}};
    std::array<ALfloat,3> Velocity{{0.0f, 0.0f, 0.0f}};
    std::array<ALfloat,3> OrientAt{{0.0f, 0.0f, -1.0f}};
    std::array<ALfloat,3> OrientUp{{0.0f, 1.0f, 0.0f}};
    ALfloat Gain{1.0f};
    ALfloat mMetersPerUnit{AL_DEFAULT_METERS_PER_UNIT};

    std::atomic_flag PropsClean;

    struct {
        /* Pointer to the most recent property values that are awaiting an
         * update.
         */
        std::atomic<ALlistenerProps*> Update{nullptr};

        alu::Matrix Matrix;
        alu::Vector Velocity;

        ALfloat Gain;
        ALfloat MetersPerUnit;

        ALfloat DopplerFactor;
        ALfloat SpeedOfSound; /* in units per sec! */

        ALboolean SourceDistanceModel;
        DistanceModel mDistanceModel;
    } Params;

    ALlistener() { PropsClean.test_and_set(std::memory_order_relaxed); }
};

void UpdateListenerProps(ALCcontext *context);

#endif
