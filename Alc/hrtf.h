#ifndef ALC_HRTF_H
#define ALC_HRTF_H

#include <array>
#include <memory>
#include <string>

#include "AL/al.h"
#include "AL/alc.h"

#include "vector.h"
#include "almalloc.h"


#define HRTF_HISTORY_BITS   (6)
#define HRTF_HISTORY_LENGTH (1<<HRTF_HISTORY_BITS)
#define HRTF_HISTORY_MASK   (HRTF_HISTORY_LENGTH-1)

#define HRIR_BITS        (7)
#define HRIR_LENGTH      (1<<HRIR_BITS)
#define HRIR_MASK        (HRIR_LENGTH-1)


struct HrtfHandle;

struct HrtfEntry {
    RefCount ref;

    ALuint sampleRate;
    ALsizei irSize;

    struct Field {
        ALfloat distance;
        ALubyte evCount;
    };
    /* NOTE: Fields are stored *backwards*. field[0] is the farthest field, and
     * field[fdCount-1] is the nearest.
     */
    ALsizei fdCount;
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

struct EnumeratedHrtf {
    std::string name;

    HrtfHandle *hrtf;
};


using float2 = std::array<float,2>;

template<typename T>
using HrirArray = std::array<std::array<T,2>,HRIR_LENGTH>;

struct HrtfState {
    alignas(16) std::array<ALfloat,HRTF_HISTORY_LENGTH> History;
    alignas(16) HrirArray<ALfloat> Values;
};

struct HrtfFilter {
    alignas(16) HrirArray<ALfloat> Coeffs;
    ALsizei Delay[2];
    ALfloat Gain;
};

struct DirectHrtfState {
    /* HRTF filter state for dry buffer content */
    ALsizei IrSize{0};
    struct ChanData {
        alignas(16) HrirArray<ALfloat> Values;
        alignas(16) HrirArray<ALfloat> Coeffs;
    };
    al::FlexArray<ChanData> Chan;

    DirectHrtfState(size_t numchans) : Chan{numchans} { }
    DirectHrtfState(const DirectHrtfState&) = delete;
    DirectHrtfState& operator=(const DirectHrtfState&) = delete;

    static std::unique_ptr<DirectHrtfState> Create(size_t num_chans);
    static constexpr size_t Sizeof(size_t numchans) noexcept
    { return al::FlexArray<ChanData>::Sizeof(numchans, offsetof(DirectHrtfState, Chan)); }

    DEF_PLACE_NEWDEL()
};

struct AngularPoint {
    ALfloat Elev;
    ALfloat Azim;
};


al::vector<EnumeratedHrtf> EnumerateHrtf(const char *devname);
HrtfEntry *GetLoadedHrtf(HrtfHandle *handle);

void GetHrtfCoeffs(const HrtfEntry *Hrtf, ALfloat elevation, ALfloat azimuth, ALfloat distance,
    ALfloat spread, HrirArray<ALfloat> &coeffs, ALsizei (&delays)[2]);

/**
 * Produces HRTF filter coefficients for decoding B-Format, given a set of
 * virtual speaker positions, a matching decoding matrix, and per-order high-
 * frequency gains for the decoder. The calculated impulse responses are
 * ordered and scaled according to the matrix input. Note the specified virtual
 * positions should be in degrees, not radians!
 */
void BuildBFormatHrtf(const HrtfEntry *Hrtf, DirectHrtfState *state, const ALuint NumChannels,
    const AngularPoint *AmbiPoints, const ALfloat (*RESTRICT AmbiMatrix)[MAX_AMBI_CHANNELS],
    const size_t AmbiCount, const ALfloat *RESTRICT AmbiOrderHFGain);

#endif /* ALC_HRTF_H */
