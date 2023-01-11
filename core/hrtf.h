#ifndef CORE_HRTF_H
#define CORE_HRTF_H

#include <array>
#include <cstddef>
#include <memory>
#include <string>

#include "almalloc.h"
#include "aloptional.h"
#include "alspan.h"
#include "atomic.h"
#include "ambidefs.h"
#include "bufferline.h"
#include "mixer/hrtfdefs.h"
#include "intrusive_ptr.h"
#include "vector.h"


struct HrtfStore {
    RefCount mRef;

    uint mSampleRate : 24;
    uint mIrSize : 8;

    struct Field {
        float distance;
        ubyte evCount;
    };
    /* NOTE: Fields are stored *backwards*. field[0] is the farthest field, and
     * field[fdCount-1] is the nearest.
     */
    al::span<const Field> mFields;

    struct Elevation {
        ushort azCount;
        ushort irOffset;
    };
    Elevation *mElev;
    const HrirArray *mCoeffs;
    const ubyte2 *mDelays;

    void getCoeffs(float elevation, float azimuth, float distance, float spread, HrirArray &coeffs,
        const al::span<uint,2> delays);

    void add_ref();
    void dec_ref();

    DEF_PLACE_NEWDEL()
};
using HrtfStorePtr = al::intrusive_ptr<HrtfStore>;


struct EvRadians { float value; };
struct AzRadians { float value; };
struct AngularPoint {
    EvRadians Elev;
    AzRadians Azim;
};


struct DirectHrtfState {
    std::array<float,BufferLineSize> mTemp;

    /* HRTF filter state for dry buffer content */
    uint mIrSize{0};
    al::FlexArray<HrtfChannelState> mChannels;

    DirectHrtfState(size_t numchans) : mChannels{numchans} { }
    /**
     * Produces HRTF filter coefficients for decoding B-Format, given a set of
     * virtual speaker positions, a matching decoding matrix, and per-order
     * high-frequency gains for the decoder. The calculated impulse responses
     * are ordered and scaled according to the matrix input.
     */
    void build(const HrtfStore *Hrtf, const uint irSize, const bool perHrirMin,
        const al::span<const AngularPoint> AmbiPoints, const float (*AmbiMatrix)[MaxAmbiChannels],
        const float XOverFreq, const al::span<const float,MaxAmbiOrder+1> AmbiOrderHFGain);

    static std::unique_ptr<DirectHrtfState> Create(size_t num_chans);

    DEF_FAM_NEWDEL(DirectHrtfState, mChannels)
};


al::vector<std::string> EnumerateHrtf(al::optional<std::string> pathopt);
HrtfStorePtr GetLoadedHrtf(const std::string &name, const uint devrate);

#endif /* CORE_HRTF_H */
