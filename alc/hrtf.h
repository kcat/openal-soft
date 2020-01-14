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

#define HRIR_BITS   (7)
#define HRIR_LENGTH (1<<HRIR_BITS)
#define HRIR_MASK   (HRIR_LENGTH-1)

#define MIN_IR_LENGTH (8)

using float2 = std::array<float,2>;
using HrirArray = std::array<float2,HRIR_LENGTH>;
using ubyte2 = std::array<ALubyte,2>;


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
    const HrirArray *coeffs;
    const ubyte2 *delays;

    void IncRef();
    void DecRef();

    DEF_PLACE_NEWDEL()
};


struct HrtfFilter {
    alignas(16) HrirArray Coeffs;
    ALuint Delay[2];
    float Gain;
};

struct DirectHrtfState {
    /* HRTF filter state for dry buffer content */
    ALuint IrSize{0};
    al::FlexArray<HrirArray,16> Coeffs;

    DirectHrtfState(size_t numchans) : Coeffs{numchans} { }

    static std::unique_ptr<DirectHrtfState> Create(size_t num_chans);

    DEF_FAM_NEWDEL(DirectHrtfState, Coeffs)
};

struct EvRadians { float value; };
struct AzRadians { float value; };
struct AngularPoint {
    EvRadians Elev;
    AzRadians Azim;
};


al::vector<std::string> EnumerateHrtf(const char *devname);
HrtfStore *GetLoadedHrtf(const std::string &name, const char *devname, const ALuint devrate);

void GetHrtfCoeffs(const HrtfStore *Hrtf, float elevation, float azimuth, float distance,
    float spread, HrirArray &coeffs, ALuint (&delays)[2]);

/**
 * Produces HRTF filter coefficients for decoding B-Format, given a set of
 * virtual speaker positions, a matching decoding matrix, and per-order high-
 * frequency gains for the decoder. The calculated impulse responses are
 * ordered and scaled according to the matrix input.
 */
void BuildBFormatHrtf(const HrtfStore *Hrtf, DirectHrtfState *state,
    const al::span<const AngularPoint> AmbiPoints, const float (*AmbiMatrix)[MAX_AMBI_CHANNELS],
    const al::span<const float,MAX_AMBI_ORDER+1> AmbiOrderHFGain);

#endif /* ALC_HRTF_H */
