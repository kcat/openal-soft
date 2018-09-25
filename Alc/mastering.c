#include "config.h"

#include <math.h>

#include "mastering.h"
#include "alu.h"
#include "almalloc.h"


extern inline ALsizei GetCompressorChannelCount(const Compressor *Comp);
extern inline ALuint GetCompressorSampleRate(const Compressor *Comp);


/* This sliding hold follows the input level with an instant attack and a
 * fixed duration hold before an instant release to the next highest level.
 * It is a sliding window maximum (descending maxima) implementation based on
 * Richard Harter's ascending minima algorithm available at:
 *
 *   http://www.richardhartersworld.com/cri/2001/slidingmin.html
 */
static ALfloat UpdateSlidingHold(SlidingHold *Hold, const ALsizei i, const ALfloat in)
{
    const ALsizei mask = BUFFERSIZE - 1;
    const ALsizei length = Hold->Length;
    ALfloat *restrict values = Hold->Values;
    ALsizei *restrict expiries = Hold->Expiries;
    ALsizei lowerIndex = Hold->LowerIndex;
    ALsizei upperIndex = Hold->UpperIndex;

    if(i >= expiries[upperIndex])
        upperIndex = (upperIndex + 1) & mask;

    if(in >= values[upperIndex])
    {
        values[upperIndex] = in;
        expiries[upperIndex] = i + length;
        lowerIndex = upperIndex;
    }
    else
    {
        while(in >= values[lowerIndex])
            lowerIndex = (lowerIndex - 1) & mask;

        lowerIndex = (lowerIndex + 1) & mask;
        values[lowerIndex] = in;
        expiries[lowerIndex] = i + length;
    }

    Hold->LowerIndex = lowerIndex;
    Hold->UpperIndex = upperIndex;

    return values[upperIndex];
}

static void ShiftSlidingHold(SlidingHold *Hold, const ALsizei n)
{
    const ALsizei mask = BUFFERSIZE - 1;
    const ALsizei lowerIndex = Hold->LowerIndex;
    ALsizei *restrict expiries = Hold->Expiries;
    ALsizei i = Hold->UpperIndex;

    while(i != lowerIndex)
    {
        expiries[i] -= n;
        i = (i + 1) & mask;
    }

    expiries[i] -= n;
}

/* Multichannel compression is linked via the absolute maximum of all
 * channels.
 */
static void LinkChannels(Compressor *Comp, const ALsizei SamplesToDo, ALfloat (*restrict OutBuffer)[BUFFERSIZE])
{
    const ALsizei mask = 2*BUFFERSIZE - 1;
    const ALsizei index = Comp->SideChainIndex + Comp->LookAhead;
    const ALsizei numChans = Comp->NumChans;
    ALfloat *restrict sideChain = Comp->SideChain;
    ALsizei c, i;

    for(i = 0;i < SamplesToDo;i++)
        sideChain[(index + i) & mask] = 0.0f;

    for(c = 0;c < numChans;c++)
    {
        for(i = 0;i < SamplesToDo;i++)
        {
            ALsizei offset = (index + i) & mask;

            sideChain[offset] = maxf(sideChain[offset], fabsf(OutBuffer[c][i]));
        }
    }
}

/* This calculates the squared crest factor of the control signal for the
 * basic automation of the attack/release times.  As suggested by the paper,
 * it uses an instantaneous squared peak detector and a squared RMS detector
 * both with 200ms release times.
 */
static void CrestDetector(Compressor *Comp, const ALsizei SamplesToDo)
{
    const ALsizei mask = 2*BUFFERSIZE - 1;
    const ALfloat a_crest = Comp->CrestCoeff;
    const ALsizei index = Comp->SideChainIndex + Comp->LookAhead;
    const ALfloat *restrict sideChain = Comp->SideChain;
    ALfloat *restrict crestFactor = Comp->CrestFactor;
    ALfloat y2_peak = Comp->LastPeakSq;
    ALfloat y2_rms = Comp->LastRmsSq;
    ALsizei i;

    for(i = 0;i < SamplesToDo;i++)
    {
        ALfloat x_abs = sideChain[(index + i) & mask];
        ALfloat x2 = maxf(0.000001f, x_abs * x_abs);

        y2_peak = maxf(x2, lerp(x2, y2_peak, a_crest));
        y2_rms = lerp(x2, y2_rms, a_crest);
        crestFactor[i] = y2_peak / y2_rms;
    }

    Comp->LastPeakSq = y2_peak;
    Comp->LastRmsSq = y2_rms;
}

/* The side-chain starts with a simple peak detector (based on the absolute
 * value of the incoming signal) and performs most of its operations in the
 * log domain.
 */
static void PeakDetector(Compressor *Comp, const ALsizei SamplesToDo)
{
    const ALsizei mask = 2*BUFFERSIZE - 1;
    const ALsizei index = Comp->SideChainIndex + Comp->LookAhead;
    ALfloat *restrict sideChain = Comp->SideChain;
    ALsizei i;

    for(i = 0;i < SamplesToDo;i++)
    {
        ALuint offset = (index + i) & mask;
        ALfloat x_abs = sideChain[offset];

        sideChain[offset] = logf(maxf(0.000001f, x_abs));
    }
}

/* An optional hold can be used to extend the peak detector so it can more
 * solidly detect fast transients.  This is best used when operating as a
 * limiter.
 */
static void PeakHoldDetector(Compressor *Comp, const ALsizei SamplesToDo)
{
    const ALsizei mask = 2*BUFFERSIZE - 1;
    const ALsizei index = Comp->SideChainIndex + Comp->LookAhead;
    ALfloat *restrict sideChain = Comp->SideChain;
    SlidingHold *hold = Comp->Hold;
    ALsizei i;

    for(i = 0;i < SamplesToDo;i++)
    {
        ALsizei offset = (index + i) & mask;
        ALfloat x_abs = sideChain[offset];
        ALfloat x_G = logf(maxf(0.000001f, x_abs));

        sideChain[offset] = UpdateSlidingHold(hold, i, x_G);
    }

    ShiftSlidingHold(hold, SamplesToDo);
}

/* This is the heart of the feed-forward compressor.  It operates in the log
 * domain (to better match human hearing) and can apply some basic automation
 * to knee width, attack/release times, make-up/post gain, and clipping
 * reduction.
 */
static void GainCompressor(Compressor *Comp, const ALsizei SamplesToDo)
{
    const ALsizei mask = 2*BUFFERSIZE - 1;
    const bool autoKnee = Comp->Auto.Knee;
    const bool autoAttack = Comp->Auto.Attack;
    const bool autoRelease = Comp->Auto.Release;
    const bool autoPostGain = Comp->Auto.PostGain;
    const bool autoDeclip = Comp->Auto.Declip;
    const ALsizei lookAhead = Comp->LookAhead;
    const ALfloat threshold = Comp->Threshold;
    const ALfloat slope = Comp->Slope;
    const ALfloat attack = Comp->Attack;
    const ALfloat release = Comp->Release;
    const ALsizei index = Comp->SideChainIndex;
    const ALfloat *restrict crestFactor = Comp->CrestFactor;
    ALfloat *restrict sideChain = Comp->SideChain;
    ALfloat postGain = Comp->PostGain;
    ALfloat knee = Comp->Knee;
    ALfloat c_est = Comp->GainEstimate;
    ALfloat a_adp = Comp->AdaptCoeff;
    ALfloat t_att = attack;
    ALfloat t_rel = release - attack;
    ALfloat a_att = expf(-1.0f / t_att);
    ALfloat a_rel = expf(-1.0f / t_rel);
    ALfloat y_1 = Comp->LastRelease;
    ALfloat y_L = Comp->LastAttack;
    ALfloat c_dev = Comp->LastGainDev;
    ALsizei i;

    for(i = 0;i < SamplesToDo;i++)
    {
        const ALfloat y2_crest = crestFactor[i];
        ALfloat x_G = sideChain[(index + lookAhead + i) & mask];
        ALfloat x_over = x_G - threshold;
        ALfloat knee_h;
        ALfloat y_G;
        ALfloat x_L;

        if(autoKnee)
            knee = maxf(0.0f, 2.5f * (c_dev + c_est));
        knee_h = 0.5f * knee;

        /* This is the gain computer.  It applies a static compression curve
         * to the control signal.
         */
        if(x_over <= -knee_h)
            y_G = 0.0f;
        else if(fabsf(x_over) < knee_h)
            y_G = (x_over + knee_h) * (x_over + knee_h) / (2.0f * knee);
        else
            y_G = x_over;

        x_L = -slope * y_G;

        if(autoAttack)
        {
            t_att = 2.0f * attack / y2_crest;
            a_att = expf(-1.0f / t_att);
        }

        if(autoRelease)
        {
            t_rel = 2.0f * release / y2_crest - t_att;
            a_rel = expf(-1.0f / t_rel);
        }

        /* Gain smoothing (ballistics) is done via a smooth decoupled peak
         * detector.  The attack time is subtracted from the release time
         * above to compensate for the chained operating mode.
         */
        y_1 = maxf(x_L, lerp(x_L, y_1, a_rel));
        y_L = lerp(y_1, y_L, a_att);

        /* Knee width and make-up gain automation make use of a smoothed
         * measurement of deviation between the control signal and estimate.
         * The estimate is also used to bias the measurement to hot-start its
         * average.
         */
        c_dev = lerp(-y_L - c_est, c_dev, a_adp);

        if(autoPostGain)
        {
            /* Clipping reduction is only viable when make-up gain is being
             * automated.  It modifies the deviation to further attenuate the
             * control signal when clipping is detected.  The adaptation
             * time is sufficiently long enough to suppress further clipping
             * at the same output level.
             */
            if(autoDeclip)
            {
                x_G = sideChain[(index + i) & mask];
                if((x_G - c_dev - c_est - y_L) > threshold)
                    c_dev = x_G - c_est - y_L - threshold;
            }

            postGain = -(c_dev + c_est);
        }

        sideChain[(index + i) & mask] = expf(postGain - y_L);
    }

    Comp->LastRelease = y_1;
    Comp->LastAttack = y_L;
    Comp->LastGainDev = c_dev;
}

/* Combined with the hold time, a look-ahead delay can improve handling of
 * fast transients by allowing the envelope time to converge prior to
 * reaching the offending impulse.  This is best used when operating as a
 * limiter.
 */
static void SignalDelay(Compressor *Comp, const ALsizei SamplesToDo, ALfloat (*restrict OutBuffer)[BUFFERSIZE])
{
    const ALsizei mask = BUFFERSIZE - 1;
    const ALsizei numChans = Comp->NumChans;
    const ALsizei indexIn = Comp->DelayIndex;
    const ALsizei indexOut = Comp->DelayIndex - Comp->LookAhead;
    ALfloat (*restrict delay)[BUFFERSIZE] = Comp->Delay;
    ALsizei c, i;

    for(c = 0;c < numChans;c++)
    {
        for(i = 0;i < SamplesToDo;i++)
        {
            ALfloat sig = OutBuffer[c][i];

            OutBuffer[c][i] = delay[c][(indexOut + i) & mask];
            delay[c][(indexIn + i) & mask] = sig;
        }
    }

    Comp->DelayIndex = (indexIn + SamplesToDo) & mask;
}

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
 *   Ratio          - Compression ratio (x:1).  Set to INFINITY for true
 *                    limiting.  Ignored when automating knee width.
 *   KneeDb         - Knee width (in dB).  Ignored when automating knee
 *                    width.
 *   AttackTimeMin  - Attack time (in seconds).  Acts as a maximum when
 *                    automating attack time.
 *   ReleaseTimeMin - Release time (in seconds).  Acts as a maximum when
 *                    automating release time.
 */
Compressor* CompressorInit(const ALuint NumChans, const ALuint SampleRate,
                           const ALboolean AutoKnee, const ALboolean AutoAttack,
                           const ALboolean AutoRelease, const ALboolean AutoPostGain,
                           const ALboolean AutoDeclip, const ALfloat LookAheadTime,
                           const ALfloat HoldTime, const ALfloat PreGainDb,
                           const ALfloat PostGainDb, const ALfloat ThresholdDb,
                           const ALfloat Ratio, const ALfloat KneeDb,
                           const ALfloat AttackTime, const ALfloat ReleaseTime)
{
    Compressor *Comp;
    ALsizei lookAhead;
    ALsizei hold;
    size_t size;

    lookAhead = (ALsizei)minf(BUFFERSIZE, roundf(maxf(0.0f, LookAheadTime) * SampleRate));
    hold = (ALsizei)minf(BUFFERSIZE, roundf(maxf(0.0f, HoldTime) * SampleRate));
    size = sizeof(*Comp);

    if(lookAhead > 0)
    {
        size += sizeof(*Comp->Delay) * NumChans;
        if(hold > 0)
            size += sizeof(*Comp->Hold);
    }

    Comp = al_calloc(16, size);
    Comp->NumChans = NumChans;
    Comp->SampleRate = SampleRate;
    Comp->Auto.Knee = AutoKnee;
    Comp->Auto.Attack = AutoAttack;
    Comp->Auto.Release = AutoRelease;
    Comp->Auto.PostGain = AutoPostGain;
    Comp->Auto.Declip = AutoPostGain && AutoDeclip;
    Comp->LookAhead = lookAhead;
    Comp->PreGain = powf(10.0f, PreGainDb / 20.0f);
    Comp->PostGain = PostGainDb * logf(10.0f) / 20.0f;
    Comp->Threshold = ThresholdDb * logf(10.0f) / 20.0f;
    Comp->Slope = 1.0f / maxf(1.0f, Ratio) - 1.0f;
    Comp->Knee = maxf(0.0f, KneeDb * logf(10.0f) / 20.0f);
    Comp->Attack = maxf(1.0f, AttackTime * SampleRate);
    Comp->Release = maxf(1.0f, ReleaseTime * SampleRate);

    /* Knee width automation actually treats the compressor as a limiter.  By
     * varying the knee width, it can effectively be seen as applying
     * compression over a wide range of ratios.
     */
    if(AutoKnee)
        Comp->Slope = -1.0f;

    if(lookAhead > 0)
    {
        if(hold > 0)
        {
            Comp->Hold = (SlidingHold*)(Comp + 1);
            Comp->Hold->Values[0] = -INFINITY;
            Comp->Hold->Expiries[0] = hold;
            Comp->Hold->Length = hold;
            Comp->Delay = (ALfloat(*)[])(Comp->Hold + 1);
        }
        else
        {
            Comp->Delay = (ALfloat(*)[])(Comp + 1);
        }
    }

    Comp->CrestCoeff = expf(-1.0f / (0.200f * SampleRate)); // 200ms
    Comp->GainEstimate = Comp->Threshold * -0.5f * Comp->Slope;
    Comp->AdaptCoeff = expf(-1.0f / (2.0f * SampleRate)); // 2s

    return Comp;
}

void ApplyCompression(Compressor *Comp, const ALsizei SamplesToDo, ALfloat (*restrict OutBuffer)[BUFFERSIZE])
{
    const ALsizei mask = 2*BUFFERSIZE - 1;
    const ALsizei numChans = Comp->NumChans;
    const ALfloat preGain = Comp->PreGain;
    const ALsizei index = Comp->SideChainIndex;
    ALfloat *restrict sideChain = Comp->SideChain;
    ALsizei c, i;

    if(preGain != 1.0f)
    {
        for(c = 0;c < numChans;c++)
        {
            for(i = 0;i < SamplesToDo;i++)
                OutBuffer[c][i] *= preGain;
        }
    }

    LinkChannels(Comp, SamplesToDo, OutBuffer);

    if(Comp->Auto.Attack || Comp->Auto.Release)
        CrestDetector(Comp, SamplesToDo);

    if(Comp->Hold)
        PeakHoldDetector(Comp, SamplesToDo);
    else
        PeakDetector(Comp, SamplesToDo);

    GainCompressor(Comp, SamplesToDo);

    if(Comp->Delay)
        SignalDelay(Comp, SamplesToDo, OutBuffer);

    for(c = 0;c < numChans;c++)
    {
        for(i = 0;i < SamplesToDo;i++)
            OutBuffer[c][i] *= sideChain[(index + i) & mask];
    }

    Comp->SideChainIndex = (index + SamplesToDo) & mask;
}
