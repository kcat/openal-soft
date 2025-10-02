#ifndef CORE_MASTERING_H
#define CORE_MASTERING_H

#include <array>
#include <bitset>
#include <memory>
#include <span>

#include "alnumeric.h"
#include "bufferline.h"
#include "vector.h"

struct SlidingHold;

using uint = unsigned int;


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

    uint mLookAhead{0};

    float mPreGain{0.0f};
    float mPostGain{0.0f};

    float mThreshold{0.0f};
    float mSlope{0.0f};
    float mKnee{0.0f};

    float mAttack{0.0f};
    float mRelease{0.0f};

    alignas(16) std::array<float,BufferLineSize*2_uz> mSideChain{};
    alignas(16) std::array<float,BufferLineSize> mCrestFactor{};

    std::unique_ptr<SlidingHold> mHold;
    al::vector<FloatBufferLine,16> mDelay;

    float mCrestCoeff{0.0f};
    float mGainEstimate{0.0f};
    float mAdaptCoeff{0.0f};

    float mLastPeakSq{0.0f};
    float mLastRmsSq{0.0f};
    float mLastRelease{0.0f};
    float mLastAttack{0.0f};
    float mLastGainDev{0.0f};

    void gainCompressor(const uint SamplesToDo);

    struct PrivateToken { };
public:
    enum {
        AutoKnee, AutoAttack, AutoRelease, AutoPostGain, AutoDeclip, FlagsCount
    };
    using FlagBits = std::bitset<FlagsCount>;

    Compressor() = delete;
    Compressor(const Compressor&) = delete;
    explicit Compressor(PrivateToken);
    ~Compressor();
    auto operator=(const Compressor&) -> Compressor& = delete;

    void process(const uint SamplesToDo, std::span<FloatBufferLine> InOut);
    [[nodiscard]] auto getLookAhead() const noexcept -> uint { return mLookAhead; }

    /**
     * The compressor is initialized with the following settings:
     *
     * \param NumChans      Number of channels to process.
     * \param SampleRate    Sample rate to process.
     * \param AutoFlags     Flags to automate specific parameters:
     *                      AutoKnee - automate the knee width parameter
     *                      AutoAttack - automate the attack time parameter
     *                      AutoRelease - automate the release time parameter
     *                      AutoPostGain - automate the make-up (post) gain
     *                                     parameter
     *                      AutoDeclip - automate clipping reduction. Ignored
     *                                   when not automating make-up gain
     * \param LookAheadTime Look-ahead time (in seconds).
     * \param HoldTime      Peak hold-time (in seconds).
     * \param PreGainDb     Gain applied before detection (in dB).
     * \param PostGainDb    Make-up gain applied after compression (in dB).
     * \param ThresholdDb   Triggering threshold (in dB).
     * \param Ratio         Compression ratio (x:1). Set to INFINIFTY for true
     *        limiting. Ignored when automating knee width.
     * \param KneeDb        Knee width (in dB). Ignored when automating knee
     *        width.
     * \param AttackTime    Attack time (in seconds). Acts as a maximum when
     *        automating attack time.
     * \param ReleaseTime   Release time (in seconds). Acts as a maximum when
     *        automating release time.
     */
    static auto Create(const size_t NumChans, const float SampleRate, const FlagBits AutoFlags,
        const float LookAheadTime, const float HoldTime, const float PreGainDb,
        const float PostGainDb, const float ThresholdDb, const float Ratio, const float KneeDb,
        const float AttackTime, const float ReleaseTime) -> std::unique_ptr<Compressor>;
};
using CompressorPtr = std::unique_ptr<Compressor>;

#endif /* CORE_MASTERING_H */
