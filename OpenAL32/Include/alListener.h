#ifndef _AL_LISTENER_H_
#define _AL_LISTENER_H_

#include "alMain.h"
#include "alu.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ALlistener {
    aluVector Position;
    aluVector Velocity;
    volatile ALfloat Forward[3];
    volatile ALfloat Up[3];
    volatile ALfloat Gain;
    volatile ALfloat MetersPerUnit;

    struct {
        aluMatrixd Matrix;
        aluVector  Velocity;
    } Params;
} ALlistener;

#ifdef __cplusplus
}
#endif

#endif
