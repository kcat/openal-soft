#ifndef _AL_FILTER_H_
#define _AL_FILTER_H_

#include "AL/al.h"
#include "alu.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ALfloat coeff;
#ifndef _MSC_VER
    ALfloat history[0];
#else
    ALfloat history[1];
#endif
} FILTER;
/*
static __inline ALfloat lpFilter4P(FILTER *iir, ALuint offset, ALfloat input)
{
    ALfloat *history = &iir->history[offset];
    ALfloat a = iir->coeff;
    ALfloat output = input;

    output = output + (history[0]-output)*a;
    history[0] = output;
    output = output + (history[1]-output)*a;
    history[1] = output;
    output = output + (history[2]-output)*a;
    history[2] = output;
    output = output + (history[3]-output)*a;
    history[3] = output;

    return output;
}
*/
static __inline ALfloat lpFilter2P(FILTER *iir, ALuint offset, ALfloat input)
{
    ALfloat *history = &iir->history[offset];
    ALfloat a = iir->coeff;
    ALfloat output = input;

    output = output + (history[0]-output)*a;
    history[0] = output;
    output = output + (history[1]-output)*a;
    history[1] = output;

    return output;
}

static __inline ALfloat lpFilter1P(FILTER *iir, ALuint offset, ALfloat input)
{
    ALfloat *history = &iir->history[offset];
    ALfloat a = iir->coeff;
    ALfloat output = input;

    output = output + (history[0]-output)*a;
    history[0] = output;

    return output;
}
/*
static __inline ALfloat lpFilter4PC(const FILTER *iir, ALuint offset, ALfloat input)
{
    const ALfloat *history = &iir->history[offset];
    ALfloat a = iir->coeff;
    ALfloat output = input;

    output = output + (history[0]-output)*a;
    output = output + (history[1]-output)*a;
    output = output + (history[2]-output)*a;
    output = output + (history[3]-output)*a;

    return output;
}
*/
static __inline ALfloat lpFilter2PC(const FILTER *iir, ALuint offset, ALfloat input)
{
    const ALfloat *history = &iir->history[offset];
    ALfloat a = iir->coeff;
    ALfloat output = input;

    output = output + (history[0]-output)*a;
    output = output + (history[1]-output)*a;

    return output;
}

static __inline ALfloat lpFilter1PC(FILTER *iir, ALuint offset, ALfloat input)
{
    const ALfloat *history = &iir->history[offset];
    ALfloat a = iir->coeff;
    ALfloat output = input;

    output = output + (history[0]-output)*a;

    return output;
}

/* Calculates the low-pass filter coefficient given the pre-scaled gain and
 * cos(w) value. Note that g should be pre-scaled (sqr(gain) for one-pole,
 * sqrt(gain) for four-pole, etc) */
ALfloat lpCoeffCalc(ALfloat g, ALfloat cw);


typedef struct ALfilter
{
    // Filter type (AL_FILTER_NULL, ...)
    ALenum type;

    ALfloat Gain;
    ALfloat GainHF;

    // Index to itself
    ALuint filter;
} ALfilter;


ALvoid ReleaseALFilters(ALCdevice *device);

#ifdef __cplusplus
}
#endif

#endif
