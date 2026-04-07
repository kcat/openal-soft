#ifndef CORE_MASTERING_H
#define CORE_MASTERING_H

#include <array>
#include <memory>
#include <span>

#include "altypes.hpp"
#include "bitset.hpp"
#include "bufferline.h"
#include "vector.h"

struct SlidingHold;


/* General topology and basic automation was based on the following paper:
 *
 *   D. Giannoulis, M. Massberg and J. D. Reiss,
 *   "Parameter Automation in a Dynamic Range Compressor,"
 *   Journal of the Audio Engineering Society, v61 (10), Oct. 2013
 *
 * Available (along with supplemental reading) at:
 *
 *   http://c4dm.eecs.qmul.ac.uk/audioengineering/compressors/
 */
class Compressor {
    struct AutoFlags {
        bool Knee : 1;
        bool Attack : 1;
        bool Release : 1;
        bool PostGain : 1;
        bool Declip : 1;
    };
    AutoFlags mAuto{};

    unsigned mLookAhead{0};

    f32 mPreGain{0.0f};
    f32 mPostGain{0.0f};

    f32 mThreshold{0.0f};
    f32 mSlope{0.0f};
    f32 mKnee{0.0f};

    f32 mAttack{0.0f};
    f32 mRelease{0.0f};

    alignas(16) std::array<f32, BufferLineSize*2_uz> mSideChain{};
    alignas(16) std::array<f32, BufferLineSize> mCrestFactor{};

    std::unique_ptr<SlidingHold> mHold;
    al::vector<FloatBufferLine, 16> mDelay;

    f32 mCrestCoeff{0.0f};
    f32 mGainEstimate{0.0f};
    f32 mAdaptCoeff{0.0f};

    f32 mLastPeakSq{0.0f};
    f32 mLastRmsSq{0.0f};
    f32 mLastRelease{0.0f};
    f32 mLastAttack{0.0f};
    f32 mLastGainDev{0.0f};

    void gainCompressor(unsigned SamplesToDo);

    struct PrivateToken { };
public:
    enum class Flags {
        AutoKnee, AutoAttack, AutoRelease, AutoPostGain, AutoDeclip,
        MaxValue = AutoDeclip
    };
    using FlagBits = al::bitset<Flags>;

    Compressor() = delete;
    explicit Compressor(PrivateToken);
    ~Compressor();

    Compressor(const Compressor&) = delete;
    auto operator=(const Compressor&) -> Compressor& = delete;

    void process(unsigned SamplesToDo, std::span<FloatBufferLine> InOut);
    [[nodiscard]] auto getLookAhead() const noexcept -> unsigned { return mLookAhead; }

    /** Parameters for initializing the compressor. */
    struct Params {
        u32 NumChans; /**< Number of channels to process. */
        f32 SampleRate; /**< Sample rate to process. */
        FlagBits AutoFlags; /**< Flags to automate specific parameters:
            * AutoKnee - automate the knee width parameter
            * AutoAttack - automate the attack time parameter
            * AutoRelease - automate the release time parameter
            * AutoPostGain - automate the make-up (post) gain parameter
            * AutoDeclip - automate clipping reduction. Ignored when not
            *              automating make-up gain
            */
        f32 LookAheadTime; /**< Look-ahead time (in seconds). */
        f32 HoldTime; /**< Peak hold-time (in seconds). */
        f32 PreGainDb; /**< Gain applied before detection (in dB). */
        f32 PostGainDb; /**< Make-up gain applied after compression (in dB). */
        f32 ThresholdDb; /**< Triggering threshold (in dB). */
        f32 Ratio; /**< Compression ratio (x:1). Set to INFINITY for true
            * limiting. Ignored when automating knee width.
            */
        f32 KneeDb; /**< Knee width (in dB). Ignored when automating knee
            * width.
            */
        f32 AttackTime; /**< Attack time (in seconds). Acts as a maximum when
            * automating attack time.
            */
        f32 ReleaseTime; /**< Release time (in seconds). Acts as a maximum when
            * automating release time.
            */
    };

    static auto Create(Params params) -> std::unique_ptr<Compressor>;
};
using CompressorPtr = std::unique_ptr<Compressor>;

#endif /* CORE_MASTERING_H */
