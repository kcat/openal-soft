#ifndef MASTERING_H
#define MASTERING_H

#include "AL/al.h"

/* For BUFFERSIZE. */
#include "alMain.h"

struct Compressor;

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
struct Compressor* CompressorInit(const ALsizei NumChans, const ALuint SampleRate,
    const ALboolean AutoKnee, const ALboolean AutoAttack,
    const ALboolean AutoRelease, const ALboolean AutoPostGain,
    const ALboolean AutoDeclip, const ALfloat LookAheadTime,
    const ALfloat HoldTime, const ALfloat PreGainDb,
    const ALfloat PostGainDb, const ALfloat ThresholdDb,
    const ALfloat Ratio, const ALfloat KneeDb,
    const ALfloat AttackTime, const ALfloat ReleaseTime);

void ApplyCompression(struct Compressor *Comp, const ALsizei SamplesToDo,
                      ALfloat (*restrict OutBuffer)[BUFFERSIZE]);

ALsizei GetCompressorLookAhead(const struct Compressor *Comp);

#endif /* MASTERING_H */
