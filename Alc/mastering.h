#ifndef MASTERING_H
#define MASTERING_H

#include <memory>

#include "AL/al.h"

#include "almalloc.h"
/* For FloatBufferLine/BUFFERSIZE. */
#include "alMain.h"


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
struct Compressor {
    ALuint mNumChans{0u};
    ALuint mSampleRate{0u};

    struct {
        bool Knee : 1;
        bool Attack : 1;
        bool Release : 1;
        bool PostGain : 1;
        bool Declip : 1;
    } mAuto{};

    ALsizei mLookAhead{0};

    ALfloat mPreGain{0.0f};
    ALfloat mPostGain{0.0f};

    ALfloat mThreshold{0.0f};
    ALfloat mSlope{0.0f};
    ALfloat mKnee{0.0f};

    ALfloat mAttack{0.0f};
    ALfloat mRelease{0.0f};

    alignas(16) ALfloat mSideChain[2*BUFFERSIZE]{};
    alignas(16) ALfloat mCrestFactor[BUFFERSIZE]{};

    SlidingHold *mHold{nullptr};
    FloatBufferLine *mDelay{nullptr};

    ALfloat mCrestCoeff{0.0f};
    ALfloat mGainEstimate{0.0f};
    ALfloat mAdaptCoeff{0.0f};

    ALfloat mLastPeakSq{0.0f};
    ALfloat mLastRmsSq{0.0f};
    ALfloat mLastRelease{0.0f};
    ALfloat mLastAttack{0.0f};
    ALfloat mLastGainDev{0.0f};


    ~Compressor();
    void process(const ALsizei SamplesToDo, FloatBufferLine *OutBuffer);
    ALsizei getLookAhead() const noexcept { return mLookAhead; }

    DEF_PLACE_NEWDEL()
};

/* The compressor is initialized with the following settings:
 *
 *   NumChans       - Number of channels to process.
 *   SampleRate     - Sample rate to process.
 *   AutoKnee       - Whether to automate the knee width parameter.
 *   AutoAttack     - Whether to automate the attack time parameter.
 *   AutoRelease    - Whether to automate the release time parameter.
 *   AutoPostGain   - Whether to automate the make-up (post) gain parameter.
 *   AutoDeclip     - Whether to automate clipping reduction.  Ignored when
 *                    not automating make-up gain.
 *   LookAheadTime  - Look-ahead time (in seconds).
 *   HoldTime       - Peak hold-time (in seconds).
 *   PreGainDb      - Gain applied before detection (in dB).
 *   PostGainDb     - Make-up gain applied after compression (in dB).
 *   ThresholdDb    - Triggering threshold (in dB).
 *   Ratio          - Compression ratio (x:1).  Set to INFINIFTY for true
 *                    limiting.  Ignored when automating knee width.
 *   KneeDb         - Knee width (in dB).  Ignored when automating knee
 *                    width.
 *   AttackTimeMin  - Attack time (in seconds).  Acts as a maximum when
 *                    automating attack time.
 *   ReleaseTimeMin - Release time (in seconds).  Acts as a maximum when
 *                    automating release time.
 */
std::unique_ptr<Compressor> CompressorInit(const ALuint NumChans, const ALuint SampleRate,
    const ALboolean AutoKnee, const ALboolean AutoAttack, const ALboolean AutoRelease,
    const ALboolean AutoPostGain, const ALboolean AutoDeclip, const ALfloat LookAheadTime,
    const ALfloat HoldTime, const ALfloat PreGainDb, const ALfloat PostGainDb,
    const ALfloat ThresholdDb, const ALfloat Ratio, const ALfloat KneeDb, const ALfloat AttackTime,
    const ALfloat ReleaseTime);

#endif /* MASTERING_H */
