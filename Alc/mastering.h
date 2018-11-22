#ifndef MASTERING_H
#define MASTERING_H

#include "AL/al.h"

#include "almalloc.h"
/* For BUFFERSIZE. */
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
    ALsizei NumChans;
    ALuint SampleRate;

    struct {
        ALuint Knee : 1;
        ALuint Attack : 1;
        ALuint Release : 1;
        ALuint PostGain : 1;
        ALuint Declip : 1;
    } Auto;

    ALsizei LookAhead;

    ALfloat PreGain;
    ALfloat PostGain;

    ALfloat Threshold;
    ALfloat Slope;
    ALfloat Knee;

    ALfloat Attack;
    ALfloat Release;

    alignas(16) ALfloat SideChain[2*BUFFERSIZE];
    alignas(16) ALfloat CrestFactor[BUFFERSIZE];

    SlidingHold *Hold;
    ALfloat (*Delay)[BUFFERSIZE];
    ALsizei DelayIndex;

    ALfloat CrestCoeff;
    ALfloat GainEstimate;
    ALfloat AdaptCoeff;

    ALfloat LastPeakSq;
    ALfloat LastRmsSq;
    ALfloat LastRelease;
    ALfloat LastAttack;
    ALfloat LastGainDev;

    void *operator new(size_t size) = delete;
    void *operator new(size_t /*size*/, void *ptr) noexcept { return ptr; }
    void operator delete(void *block) noexcept { al_free(block); }
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
Compressor* CompressorInit(const ALsizei NumChans, const ALuint SampleRate,
    const ALboolean AutoKnee, const ALboolean AutoAttack,
    const ALboolean AutoRelease, const ALboolean AutoPostGain,
    const ALboolean AutoDeclip, const ALfloat LookAheadTime,
    const ALfloat HoldTime, const ALfloat PreGainDb,
    const ALfloat PostGainDb, const ALfloat ThresholdDb,
    const ALfloat Ratio, const ALfloat KneeDb,
    const ALfloat AttackTime, const ALfloat ReleaseTime);

void ApplyCompression(struct Compressor *Comp, const ALsizei SamplesToDo,
                      ALfloat (*RESTRICT OutBuffer)[BUFFERSIZE]);

ALsizei GetCompressorLookAhead(const struct Compressor *Comp);

#endif /* MASTERING_H */
