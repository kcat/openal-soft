#ifndef _AL_LISTENER_H_
#define _AL_LISTENER_H_

#include "AL/alc.h"
#include "AL/al.h"
#include "AL/alext.h"

#include "atomic.h"
#include "vecmat.h"

enum class DistanceModel;


struct ALlistenerProps {
    ALfloat Position[3];
    ALfloat Velocity[3];
    ALfloat Forward[3];
    ALfloat Up[3];
    ALfloat Gain;

    std::atomic<ALlistenerProps*> next;
};

struct ALlistener {
    ALfloat Position[3]{0.0f, 0.0f, 0.0f};
    ALfloat Velocity[3]{0.0f, 0.0f, 0.0f};
    ALfloat Forward[3]{0.0f, 0.0f, -1.0f};
    ALfloat Up[3]{0.0f, 1.0f, 0.0f};
    ALfloat Gain{1.0f};

    std::atomic_flag PropsClean{true};

    /* Pointer to the most recent property values that are awaiting an update.
     */
    std::atomic<ALlistenerProps*> Update{nullptr};

    struct {
        aluMatrixf Matrix;
        aluVector  Velocity;

        ALfloat Gain;
        ALfloat MetersPerUnit;

        ALfloat DopplerFactor;
        ALfloat SpeedOfSound; /* in units per sec! */
        ALfloat ReverbSpeedOfSound; /* in meters per sec! */

        ALboolean SourceDistanceModel;
        DistanceModel mDistanceModel;
    } Params;
};

void UpdateListenerProps(ALCcontext *context);

#endif
