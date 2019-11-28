#ifndef ALC_HRTF_H
#define ALC_HRTF_H

#include <array>
#include <cstddef>
#include <memory>
#include <string>

#include "AL/al.h"

#include "almalloc.h"
#include "alspan.h"
#include "ambidefs.h"
#include "atomic.h"
#include "vector.h"


#define HRTF_HISTORY_BITS   (6)
#define HRTF_HISTORY_LENGTH (1<<HRTF_HISTORY_BITS)
#define HRTF_HISTORY_MASK   (HRTF_HISTORY_LENGTH-1)

#define HRIR_BITS        (7)
#define HRIR_LENGTH      (1<<HRIR_BITS)
#define HRIR_MASK        (HRIR_LENGTH-1)


struct HrtfStore {
    RefCount mRef;

    ALuint sampleRate;
    ALuint irSize;

    struct Field {
        ALfloat distance;
        ALubyte evCount;
    };
    /* NOTE: Fields are stored *backwards*. field[0] is the farthest field, and
     * field[fdCount-1] is the nearest.
     */
    ALuint fdCount;
    const Field *field;

    struct Elevation {
        ALushort azCount;
        ALushort irOffset;
    };
    Elevation *elev;
    const ALfloat (*coeffs)[2];
    const ALubyte (*delays)[2];

    void IncRef();
    void DecRef();

    DEF_PLACE_NEWDEL()
};


using float2 = std::array<float,2>;
using HrirArray = std::array<float2,HRIR_LENGTH>;

struct HrtfState {
    alignas(16) std::array<ALfloat,HRTF_HISTORY_LENGTH> History;
};

struct HrtfFilter {
    alignas(16) HrirArray Coeffs;
    ALsizei Delay[2];
    ALfloat Gain;
};

struct DirectHrtfState {
    /* HRTF filter state for dry buffer content */
    ALuint IrSize{0};
    al::FlexArray<HrirArray,16> Coeffs;

    DirectHrtfState(size_t numchans) : Coeffs{numchans} { }

    static std::unique_ptr<DirectHrtfState> Create(size_t num_chans);

    DEF_FAM_NEWDEL(DirectHrtfState, Coeffs)
};

struct ElevRadius { float value; };
struct AzimRadius { float value; };
struct AngularPoint {
    ElevRadius Elev;
    AzimRadius Azim;
};


al::vector<std::string> EnumerateHrtf(const char *devname);
HrtfStore *GetLoadedHrtf(const std::string &name);

void GetHrtfCoeffs(const HrtfStore *Hrtf, ALfloat elevation, ALfloat azimuth, ALfloat distance,
    ALfloat spread, HrirArray &coeffs, ALsizei (&delays)[2]);

/**
 * Produces HRTF filter coefficients for decoding B-Format, given a set of
 * virtual speaker positions, a matching decoding matrix, and per-order high-
 * frequency gains for the decoder. The calculated impulse responses are
 * ordered and scaled according to the matrix input.
 */
void BuildBFormatHrtf(const HrtfStore *Hrtf, DirectHrtfState *state,
    const al::span<const AngularPoint> AmbiPoints, const ALfloat (*AmbiMatrix)[MAX_AMBI_CHANNELS],
    const ALfloat *AmbiOrderHFGain);

#endif /* ALC_HRTF_H */
