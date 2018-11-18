#ifndef _AL_LISTENER_H_
#define _AL_LISTENER_H_

#include "AL/alc.h"
#include "AL/al.h"
#include "AL/alext.h"

#include "atomic.h"
#include "vecmat.h"


struct ALlistenerProps {
    ALfloat Position[3];
    ALfloat Velocity[3];
    ALfloat Forward[3];
    ALfloat Up[3];
    ALfloat Gain;

    ATOMIC(ALlistenerProps*) next;
};

struct ALlistener {
    alignas(16) ALfloat Position[3];
    ALfloat Velocity[3];
    ALfloat Forward[3];
    ALfloat Up[3];
    ALfloat Gain;

    ATOMIC(ALenum) PropsClean;

    /* Pointer to the most recent property values that are awaiting an update.
     */
    ATOMIC(ALlistenerProps*) Update;

    struct {
        aluMatrixf Matrix;
        aluVector  Velocity;

        ALfloat Gain;
        ALfloat MetersPerUnit;

        ALfloat DopplerFactor;
        ALfloat SpeedOfSound; /* in units per sec! */
        ALfloat ReverbSpeedOfSound; /* in meters per sec! */

        ALboolean SourceDistanceModel;
        enum DistanceModel DistanceModel;
    } Params;
};

void UpdateListenerProps(ALCcontext *context);

#endif
