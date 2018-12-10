#ifndef AMBDEC_H
#define AMBDEC_H

#include <string>

#include "alMain.h"

/* Helpers to read .ambdec configuration files. */

enum class AmbDecScale {
    N3D,
    SN3D,
    FuMa,
};
struct AmbDecConf {
    std::string Description;
    ALuint Version; /* Must be 3 */

    ALuint ChanMask;
    ALuint FreqBands; /* Must be 1 or 2 */
    ALsizei NumSpeakers;
    AmbDecScale CoeffScale;

    ALfloat XOverFreq;
    ALfloat XOverRatio;

    struct SpeakerConf {
        std::string Name;
        ALfloat Distance;
        ALfloat Azimuth;
        ALfloat Elevation;
        std::string Connection;
    } Speakers[MAX_OUTPUT_CHANNELS];

    /* Unused when FreqBands == 1 */
    ALfloat LFOrderGain[MAX_AMBI_ORDER+1];
    ALfloat LFMatrix[MAX_OUTPUT_CHANNELS][MAX_AMBI_COEFFS];

    ALfloat HFOrderGain[MAX_AMBI_ORDER+1];
    ALfloat HFMatrix[MAX_OUTPUT_CHANNELS][MAX_AMBI_COEFFS];

    int load(const char *fname) noexcept;
};

#endif /* AMBDEC_H */
