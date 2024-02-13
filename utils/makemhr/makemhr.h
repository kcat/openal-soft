#ifndef MAKEMHR_H
#define MAKEMHR_H

#include <array>
#include <complex>
#include <vector>

#include "alcomplex.h"
#include "polyphase_resampler.h"


// The maximum path length used when processing filenames.
enum { MAX_PATH_LEN = 256u };

// The limit to the number of 'distances' listed in the data set definition.
// Must be less than 256
enum { MAX_FD_COUNT = 16u };

// The limits to the number of 'elevations' listed in the data set definition.
// Must be less than 256.
enum {
    MIN_EV_COUNT = 5u,
    MAX_EV_COUNT = 181u
};

// The limits for each of the 'azimuths' listed in the data set definition.
// Must be less than 256.
enum {
    MIN_AZ_COUNT = 1u,
    MAX_AZ_COUNT = 255u
};

// The limits for the 'distance' from source to listener for each field in
// the definition file.
inline constexpr double MIN_DISTANCE{0.05};
inline constexpr double MAX_DISTANCE{2.50};

// The limits for the sample 'rate' metric in the data set definition and for
// resampling.
enum {
    MIN_RATE = 32000u,
    MAX_RATE = 96000u
};

// The limits for the HRIR 'points' metric in the data set definition.
enum {
    MIN_POINTS = 16u,
    MAX_POINTS = 8192u
};


using uint = unsigned int;

/* Complex double type. */
using complex_d = std::complex<double>;


enum ChannelModeT : bool {
    CM_AllowStereo = false,
    CM_ForceMono = true
};

// Sample and channel type enum values.
enum SampleTypeT {
    ST_S16 = 0,
    ST_S24 = 1
};

// Certain iterations rely on these integer enum values.
enum ChannelTypeT {
    CT_NONE   = -1,
    CT_MONO   = 0,
    CT_STEREO = 1
};

// Structured HRIR storage for stereo azimuth pairs, elevations, and fields.
struct HrirAzT {
    double mAzimuth{0.0};
    uint mIndex{0u};
    std::array<double,2> mDelays{};
    std::array<double*,2> mIrs{};
};

struct HrirEvT {
    double mElevation{0.0};
    al::span<HrirAzT> mAzs;
};

struct HrirFdT {
    double mDistance{0.0};
    uint mEvStart{0u};
    al::span<HrirEvT> mEvs;
};

// The HRIR metrics and data set used when loading, processing, and storing
// the resulting HRTF.
struct HrirDataT {
    uint mIrRate{0u};
    SampleTypeT mSampleType{ST_S24};
    ChannelTypeT mChannelType{CT_NONE};
    uint mIrPoints{0u};
    uint mFftSize{0u};
    uint mIrSize{0u};
    double mRadius{0.0};
    uint mIrCount{0u};

    std::vector<double> mHrirsBase;
    std::vector<HrirEvT> mEvsBase;
    std::vector<HrirAzT> mAzsBase;

    std::vector<HrirFdT> mFds;

    /* GCC warns when it tries to inline this. */
    ~HrirDataT();
};


bool PrepareHrirData(const al::span<const double> distances,
    const al::span<const uint,MAX_FD_COUNT> evCounts,
    const al::span<const std::array<uint,MAX_EV_COUNT>,MAX_FD_COUNT> azCounts, HrirDataT *hData);
void MagnitudeResponse(const uint n, const complex_d *in, double *out);

// Performs a forward FFT.
inline void FftForward(const uint n, complex_d *inout)
{ forward_fft(al::span{inout, n}); }

// Performs an inverse FFT.
inline void FftInverse(const uint n, complex_d *inout)
{
    inverse_fft(al::span{inout, n});
    double f{1.0 / n};
    for(uint i{0};i < n;i++)
        inout[i] *= f;
}

// Performs linear interpolation.
inline double Lerp(const double a, const double b, const double f)
{ return a + f * (b - a); }

#endif /* MAKEMHR_H */
