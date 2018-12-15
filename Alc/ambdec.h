#ifndef AMBDEC_H
#define AMBDEC_H

#include <array>
#include <string>

#include "alMain.h"
#include "vector.h"

/* Helpers to read .ambdec configuration files. */

enum class AmbDecScale {
    N3D,
    SN3D,
    FuMa,
};
struct AmbDecConf {
    std::string Description;
    int Version; /* Must be 3 */

    unsigned int ChanMask;
    unsigned int FreqBands; /* Must be 1 or 2 */
    AmbDecScale CoeffScale;

    float XOverFreq;
    float XOverRatio;

    struct SpeakerConf {
        std::string Name;
        float Distance{0.0f};
        float Azimuth{0.0f};
        float Elevation{0.0f};
        std::string Connection;
    };
    al::vector<SpeakerConf> Speakers;

    using CoeffArray = std::array<float,MAX_AMBI_COEFFS>;
    /* Unused when FreqBands == 1 */
    float LFOrderGain[MAX_AMBI_ORDER+1];
    al::vector<CoeffArray> LFMatrix;

    float HFOrderGain[MAX_AMBI_ORDER+1];
    al::vector<CoeffArray> HFMatrix;

    int load(const char *fname) noexcept;
};

#endif /* AMBDEC_H */
