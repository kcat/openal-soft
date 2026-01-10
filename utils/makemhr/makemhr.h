#ifndef MAKEMHR_H
#define MAKEMHR_H

#include <algorithm>
#include <array>
#include <complex>
#include <ranges>
#include <span>
#include <vector>

#include "alcomplex.h"
#include "opthelpers.h"
#include "polyphase_resampler.h"


// The limit to the number of 'distances' listed in the data set definition.
// Must be less than 256
inline constexpr auto MAX_FD_COUNT = 16u;

// The limits to the number of 'elevations' listed in the data set definition.
// Must be less than 256.
inline constexpr auto MIN_EV_COUNT = 5u;
inline constexpr auto MAX_EV_COUNT = 181u;

// The limits for each of the 'azimuths' listed in the data set definition.
// Must be less than 256.
inline constexpr auto MIN_AZ_COUNT = 1u;
inline constexpr auto MAX_AZ_COUNT = 255u;

// The limits for the 'distance' from source to listener for each field in
// the definition file.
inline constexpr auto MIN_DISTANCE = 0.05;
inline constexpr auto MAX_DISTANCE = 2.50;

// The limits for the sample 'rate' metric in the data set definition and for
// resampling.
inline constexpr auto MIN_RATE = 32000u;
inline constexpr auto MAX_RATE = 96000u;

// The limits for the HRIR 'points' metric in the data set definition.
inline constexpr auto MIN_POINTS = 16u;
inline constexpr auto MAX_POINTS = 8192u;


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
    unsigned mIndex{0u};
    std::array<double,2> mDelays{};
    std::array<std::span<double>,2> mIrs{};
};

struct HrirEvT {
    double mElevation{0.0};
    std::span<HrirAzT> mAzs;
};

struct HrirFdT {
    double mDistance{0.0};
    unsigned mEvStart{0u};
    std::span<HrirEvT> mEvs;
};

// The HRIR metrics and data set used when loading, processing, and storing
// the resulting HRTF.
struct HrirDataT {
    unsigned mIrRate{0u};
    SampleTypeT mSampleType{ST_S24};
    ChannelTypeT mChannelType{CT_NONE};
    unsigned mIrPoints{0u};
    unsigned mFftSize{0u};
    unsigned mIrSize{0u};
    double mRadius{0.0};
    unsigned mIrCount{0u};

    std::vector<double> mHrirsBase;
    std::vector<HrirEvT> mEvsBase;
    std::vector<HrirAzT> mAzsBase;

    std::vector<HrirFdT> mFds;

    /* GCC warns when it tries to inline this. */
    NOINLINE ~HrirDataT() = default;
};


bool PrepareHrirData(std::span<const double> distances,
    std::span<const unsigned,MAX_FD_COUNT> evCounts,
    std::span<const std::array<unsigned,MAX_EV_COUNT>,MAX_FD_COUNT> azCounts, HrirDataT *hData);

/* Calculate the magnitude response of the given input.  This is used in
 * place of phase decomposition, since the phase residuals are discarded for
 * minimum phase reconstruction.  The mirrored half of the response is also
 * discarded.
 */
inline void MagnitudeResponse(const std::span<const complex_d> in, const std::span<double> out)
{
    static constexpr auto Epsilon = 1e-9;
    std::ranges::transform(in | std::views::take(out.size()), out.begin(),
        [](const complex_d &c) -> double { return std::max(std::abs(c), Epsilon); });
}

// Performs a forward FFT.
inline void FftForward(unsigned const n, complex_d *inout)
{ forward_fft(std::span{inout, n}); }

// Performs an inverse FFT, scaling the result by the number of elements.
inline void FftInverse(unsigned const n, complex_d *inout)
{
    const auto values = std::span{inout, n};
    inverse_fft(values);

    const auto f = 1.0 / n;
    std::ranges::for_each(values, [f](complex_d &value) { value *= f; });
}

#endif /* MAKEMHR_H */
