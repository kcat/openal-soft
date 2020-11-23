#ifndef ALC_HRTF_H
#define ALC_HRTF_H

#include <array>
#include <cstddef>
#include <memory>
#include <string>

#include "almalloc.h"
#include "alspan.h"
#include "ambidefs.h"
#include "atomic.h"
#include "bufferline.h"
#include "filters/splitter.h"
#include "intrusive_ptr.h"
#include "vector.h"


#define HRTF_HISTORY_BITS   6
#define HRTF_HISTORY_LENGTH (1<<HRTF_HISTORY_BITS)
#define HRTF_HISTORY_MASK   (HRTF_HISTORY_LENGTH-1)

#define HRIR_BITS   7
#define HRIR_LENGTH (1<<HRIR_BITS)
#define HRIR_MASK   (HRIR_LENGTH-1)

#define MIN_IR_LENGTH 8

using float2 = std::array<float,2>;
using HrirArray = std::array<float2,HRIR_LENGTH>;
using ubyte = unsigned char;
using ubyte2 = std::array<ubyte,2>;
using ushort = unsigned short;
using uint = unsigned int;

struct HrtfStore {
    RefCount mRef;

    uint sampleRate;
    uint irSize;

    struct Field {
        float distance;
        ubyte evCount;
    };
    /* NOTE: Fields are stored *backwards*. field[0] is the farthest field, and
     * field[fdCount-1] is the nearest.
     */
    uint fdCount;
    const Field *field;

    struct Elevation {
        ushort azCount;
        ushort irOffset;
    };
    Elevation *elev;
    const HrirArray *coeffs;
    const ubyte2 *delays;

    void add_ref();
    void release();

    DEF_PLACE_NEWDEL()
};
using HrtfStorePtr = al::intrusive_ptr<HrtfStore>;


struct HrtfFilter {
    alignas(16) HrirArray Coeffs;
    std::array<uint,2> Delay;
    float Gain;
};


struct EvRadians { float value; };
struct AzRadians { float value; };
struct AngularPoint {
    EvRadians Elev;
    AzRadians Azim;
};

#define HRTF_DIRECT_DELAY 192
struct DirectHrtfState {
    struct ChannelData {
        std::array<float,HRTF_DIRECT_DELAY> mDelay{};
        BandSplitter mSplitter;
        float mHfScale{};
        alignas(16) HrirArray mCoeffs{};
    };

    std::array<float,HRTF_DIRECT_DELAY+BUFFERSIZE> mTemp;

    /* HRTF filter state for dry buffer content */
    uint mIrSize{0};
    al::FlexArray<ChannelData> mChannels;

    DirectHrtfState(size_t numchans) : mChannels{numchans} { }
    /**
     * Produces HRTF filter coefficients for decoding B-Format, given a set of
     * virtual speaker positions, a matching decoding matrix, and per-order
     * high-frequency gains for the decoder. The calculated impulse responses
     * are ordered and scaled according to the matrix input.
     */
    void build(const HrtfStore *Hrtf, const al::span<const AngularPoint> AmbiPoints,
        const float (*AmbiMatrix)[MAX_AMBI_CHANNELS],
        const al::span<const float,MAX_AMBI_ORDER+1> AmbiOrderHFGain);

    static std::unique_ptr<DirectHrtfState> Create(size_t num_chans);

    DEF_FAM_NEWDEL(DirectHrtfState, mChannels)
};


al::vector<std::string> EnumerateHrtf(const char *devname);
HrtfStorePtr GetLoadedHrtf(const std::string &name, const char *devname, const uint devrate);

void GetHrtfCoeffs(const HrtfStore *Hrtf, float elevation, float azimuth, float distance,
    float spread, HrirArray &coeffs, const al::span<uint,2> delays);

#endif /* ALC_HRTF_H */
