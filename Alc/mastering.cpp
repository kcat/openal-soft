#include "config.h"

#include <cmath>
#include <algorithm>
#include <functional>

#include "mastering.h"
#include "alu.h"
#include "almalloc.h"
#include "math_defs.h"


/* These structures assume BUFFERSIZE is a power of 2. */
static_assert((BUFFERSIZE & (BUFFERSIZE-1)) == 0, "BUFFERSIZE is not a power of 2");

struct SlidingHold {
    ALfloat Values[BUFFERSIZE];
    ALsizei Expiries[BUFFERSIZE];
    ALsizei LowerIndex;
    ALsizei UpperIndex;
    ALsizei Length;
};


namespace {

using namespace std::placeholders;

/* This sliding hold follows the input level with an instant attack and a
 * fixed duration hold before an instant release to the next highest level.
 * It is a sliding window maximum (descending maxima) implementation based on
 * Richard Harter's ascending minima algorithm available at:
 *
 *   http://www.richardhartersworld.com/cri/2001/slidingmin.html
 */
static ALfloat UpdateSlidingHold(SlidingHold *Hold, const ALsizei i, const ALfloat in)
{
    static constexpr ALsizei mask{BUFFERSIZE - 1};
    const ALsizei length{Hold->Length};
    ALfloat (&values)[BUFFERSIZE] = Hold->Values;
    ALsizei (&expiries)[BUFFERSIZE] = Hold->Expiries;
    ALsizei lowerIndex{Hold->LowerIndex};
    ALsizei upperIndex{Hold->UpperIndex};

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
        do {
            do {
                if(!(in >= values[lowerIndex]))
                    goto found_place;
            } while(lowerIndex--);
            lowerIndex = mask;
        } while(1);
    found_place:

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
    ASSUME(Hold->UpperIndex >= 0);
    ASSUME(Hold->LowerIndex >= 0);

    auto exp_begin = std::begin(Hold->Expiries) + Hold->UpperIndex;
    auto exp_last = std::begin(Hold->Expiries) + Hold->LowerIndex;
    if(exp_last < exp_begin)
    {
        std::transform(exp_begin, std::end(Hold->Expiries), exp_begin,
            std::bind(std::minus<float>{}, _1, n));
        exp_begin = std::begin(Hold->Expiries);
    }
    std::transform(exp_begin, exp_last+1, exp_begin, std::bind(std::minus<float>{}, _1, n));
}

/* Multichannel compression is linked via the absolute maximum of all
 * channels.
 */
void LinkChannels(Compressor *Comp, const ALsizei SamplesToDo, const ALfloat (*RESTRICT OutBuffer)[BUFFERSIZE])
{
    const ALsizei index{Comp->LookAhead};
    const ALsizei numChans{Comp->NumChans};

    ASSUME(SamplesToDo > 0);
    ASSUME(numChans > 0);
    ASSUME(index >= 0);

    auto side_begin = std::begin(Comp->SideChain) + index;
    std::fill(side_begin, side_begin+SamplesToDo, 0.0f);

    auto fill_max = [SamplesToDo,side_begin](const ALfloat *input) -> void
    {
        const ALfloat *RESTRICT buffer{al::assume_aligned<16>(input)};
        auto max_abs = std::bind(maxf, _1, std::bind(static_cast<float(&)(float)>(std::fabs), _2));
        std::transform(side_begin, side_begin+SamplesToDo, buffer, side_begin, max_abs);
    };
    std::for_each(OutBuffer, OutBuffer+numChans, fill_max);
}

/* This calculates the squared crest factor of the control signal for the
 * basic automation of the attack/release times.  As suggested by the paper,
 * it uses an instantaneous squared peak detector and a squared RMS detector
 * both with 200ms release times.
 */
static void CrestDetector(Compressor *Comp, const ALsizei SamplesToDo)
{
    const ALfloat a_crest{Comp->CrestCoeff};
    const ALsizei index{Comp->LookAhead};
    ALfloat y2_peak{Comp->LastPeakSq};
    ALfloat y2_rms{Comp->LastRmsSq};

    ASSUME(SamplesToDo > 0);
    ASSUME(index >= 0);

    auto calc_crest = [&y2_rms,&y2_peak,a_crest](const ALfloat x_abs) noexcept -> ALfloat
    {
        ALfloat x2 = maxf(0.000001f, x_abs * x_abs);

        y2_peak = maxf(x2, lerp(x2, y2_peak, a_crest));
        y2_rms = lerp(x2, y2_rms, a_crest);
        return y2_peak / y2_rms;
    };
    auto side_begin = std::begin(Comp->SideChain) + index;
    std::transform(side_begin, side_begin+SamplesToDo, std::begin(Comp->CrestFactor), calc_crest);

    Comp->LastPeakSq = y2_peak;
    Comp->LastRmsSq = y2_rms;
}

/* The side-chain starts with a simple peak detector (based on the absolute
 * value of the incoming signal) and performs most of its operations in the
 * log domain.
 */
void PeakDetector(Compressor *Comp, const ALsizei SamplesToDo)
{
    const ALsizei index{Comp->LookAhead};

    ASSUME(SamplesToDo > 0);
    ASSUME(index >= 0);

    /* Clamp the minimum amplitude to near-zero and convert to logarithm. */
    auto side_begin = std::begin(Comp->SideChain) + index;
    std::transform(side_begin, side_begin+SamplesToDo, side_begin,
        std::bind(static_cast<float(&)(float)>(std::log), std::bind(maxf, 0.000001f, _1)));
}

/* An optional hold can be used to extend the peak detector so it can more
 * solidly detect fast transients.  This is best used when operating as a
 * limiter.
 */
void PeakHoldDetector(Compressor *Comp, const ALsizei SamplesToDo)
{
    const ALsizei index{Comp->LookAhead};

    ASSUME(SamplesToDo > 0);
    ASSUME(index >= 0);

    SlidingHold *hold{Comp->Hold};
    ALsizei i{0};
    auto detect_peak = [&i,hold](const ALfloat x_abs) -> ALfloat
    {
        const ALfloat x_G{std::log(maxf(0.000001f, x_abs))};
        return UpdateSlidingHold(hold, i++, x_G);
    };
    auto side_begin = std::begin(Comp->SideChain) + index;
    std::transform(side_begin, side_begin+SamplesToDo, side_begin, detect_peak);

    ShiftSlidingHold(hold, SamplesToDo);
}

/* This is the heart of the feed-forward compressor.  It operates in the log
 * domain (to better match human hearing) and can apply some basic automation
 * to knee width, attack/release times, make-up/post gain, and clipping
 * reduction.
 */
void GainCompressor(Compressor *Comp, const ALsizei SamplesToDo)
{
    const bool autoKnee{Comp->Auto.Knee};
    const bool autoAttack{Comp->Auto.Attack};
    const bool autoRelease{Comp->Auto.Release};
    const bool autoPostGain{Comp->Auto.PostGain};
    const bool autoDeclip{Comp->Auto.Declip};
    const ALsizei lookAhead{Comp->LookAhead};
    const ALfloat threshold{Comp->Threshold};
    const ALfloat slope{Comp->Slope};
    const ALfloat attack{Comp->Attack};
    const ALfloat release{Comp->Release};
    const ALfloat c_est{Comp->GainEstimate};
    const ALfloat a_adp{Comp->AdaptCoeff};
    const ALfloat (&crestFactor)[BUFFERSIZE] = Comp->CrestFactor;
    ALfloat (&sideChain)[BUFFERSIZE*2] = Comp->SideChain;
    ALfloat postGain{Comp->PostGain};
    ALfloat knee{Comp->Knee};
    ALfloat t_att{attack};
    ALfloat t_rel{release - attack};
    ALfloat a_att{std::exp(-1.0f / t_att)};
    ALfloat a_rel{std::exp(-1.0f / t_rel)};
    ALfloat y_1{Comp->LastRelease};
    ALfloat y_L{Comp->LastAttack};
    ALfloat c_dev{Comp->LastGainDev};

    ASSUME(SamplesToDo > 0);
    ASSUME(lookAhead >= 0);

    for(ALsizei i{0};i < SamplesToDo;i++)
    {
        if(autoKnee)
            knee = maxf(0.0f, 2.5f * (c_dev + c_est));
        const ALfloat knee_h{0.5f * knee};

        /* This is the gain computer.  It applies a static compression curve
         * to the control signal.
         */
        const ALfloat x_over{sideChain[lookAhead+i] - threshold};
        const ALfloat y_G{
            (x_over <= -knee_h) ? 0.0f :
            (std::fabs(x_over) < knee_h) ? (x_over + knee_h) * (x_over + knee_h) / (2.0f * knee) :
            x_over
        };

        const ALfloat y2_crest{crestFactor[i]};
        if(autoAttack)
        {
            t_att = 2.0f*attack/y2_crest;
            a_att = std::exp(-1.0f / t_att);
        }
        if(autoRelease)
        {
            t_rel = 2.0f*release/y2_crest - t_att;
            a_rel = std::exp(-1.0f / t_rel);
        }

        /* Gain smoothing (ballistics) is done via a smooth decoupled peak
         * detector.  The attack time is subtracted from the release time
         * above to compensate for the chained operating mode.
         */
        const ALfloat x_L{-slope * y_G};
        y_1 = maxf(x_L, lerp(x_L, y_1, a_rel));
        y_L = lerp(y_1, y_L, a_att);

        /* Knee width and make-up gain automation make use of a smoothed
         * measurement of deviation between the control signal and estimate.
         * The estimate is also used to bias the measurement to hot-start its
         * average.
         */
        c_dev = lerp(-(y_L+c_est), c_dev, a_adp);

        if(autoPostGain)
        {
            /* Clipping reduction is only viable when make-up gain is being
             * automated. It modifies the deviation to further attenuate the
             * control signal when clipping is detected. The adaptation time
             * is sufficiently long enough to suppress further clipping at the
             * same output level.
             */
            if(autoDeclip)
                c_dev = maxf(c_dev, sideChain[i] - y_L - threshold - c_est);

            postGain = -(c_dev + c_est);
        }

        sideChain[i] = std::exp(postGain - y_L);
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
void SignalDelay(Compressor *Comp, const ALsizei SamplesToDo, ALfloat (*RESTRICT OutBuffer)[BUFFERSIZE])
{
    static constexpr ALsizei mask{BUFFERSIZE - 1};
    const ALsizei numChans{Comp->NumChans};
    const ALsizei indexIn{Comp->DelayIndex};
    const ALsizei indexOut{Comp->DelayIndex - Comp->LookAhead};

    ASSUME(SamplesToDo > 0);
    ASSUME(numChans > 0);

    for(ALsizei c{0};c < numChans;c++)
    {
        ALfloat *RESTRICT inout{al::assume_aligned<16>(OutBuffer[c])};
        ALfloat *RESTRICT delay{al::assume_aligned<16>(Comp->Delay[c])};
        for(ALsizei i{0};i < SamplesToDo;i++)
        {
            const ALfloat sig{inout[i]};

            inout[i] = delay[(indexOut + i) & mask];
            delay[(indexIn + i) & mask] = sig;
        }
    }

    Comp->DelayIndex = (indexIn + SamplesToDo) & mask;
}

} // namespace

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
Compressor* CompressorInit(const ALsizei NumChans, const ALuint SampleRate,
                           const ALboolean AutoKnee, const ALboolean AutoAttack,
                           const ALboolean AutoRelease, const ALboolean AutoPostGain,
                           const ALboolean AutoDeclip, const ALfloat LookAheadTime,
                           const ALfloat HoldTime, const ALfloat PreGainDb,
                           const ALfloat PostGainDb, const ALfloat ThresholdDb,
                           const ALfloat Ratio, const ALfloat KneeDb,
                           const ALfloat AttackTime, const ALfloat ReleaseTime)
{
    auto lookAhead = static_cast<ALsizei>(
        clampf(std::round(LookAheadTime*SampleRate), 0.0f, BUFFERSIZE-1));
    auto hold = static_cast<ALsizei>(clampf(std::round(HoldTime*SampleRate), 0.0f, BUFFERSIZE-1));

    Compressor *Comp;
    size_t size{sizeof(*Comp)};
    if(lookAhead > 0)
    {
        size += sizeof(*Comp->Delay) * NumChans;
        /* The sliding hold implementation doesn't handle a length of 1. A 1-
         * sample hold is useless anyway, it would only ever give back what was
         * just given to it.
         */
        if(hold > 1)
            size += sizeof(*Comp->Hold);
    }

    Comp = new (al_calloc(16, size)) Compressor{};
    Comp->NumChans = NumChans;
    Comp->SampleRate = SampleRate;
    Comp->Auto.Knee = AutoKnee;
    Comp->Auto.Attack = AutoAttack;
    Comp->Auto.Release = AutoRelease;
    Comp->Auto.PostGain = AutoPostGain;
    Comp->Auto.Declip = AutoPostGain && AutoDeclip;
    Comp->LookAhead = lookAhead;
    Comp->PreGain = std::pow(10.0f, PreGainDb / 20.0f);
    Comp->PostGain = PostGainDb * std::log(10.0f) / 20.0f;
    Comp->Threshold = ThresholdDb * std::log(10.0f) / 20.0f;
    Comp->Slope = 1.0f / maxf(1.0f, Ratio) - 1.0f;
    Comp->Knee = maxf(0.0f, KneeDb * std::log(10.0f) / 20.0f);
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
        if(hold > 1)
        {
            Comp->Hold = new ((void*)(Comp + 1)) SlidingHold{};
            Comp->Hold->Values[0] = -HUGE_VALF;
            Comp->Hold->Expiries[0] = hold;
            Comp->Hold->Length = hold;
            Comp->Delay = (ALfloat(*)[BUFFERSIZE])(Comp->Hold + 1);
        }
        else
        {
            Comp->Delay = (ALfloat(*)[BUFFERSIZE])(Comp + 1);
        }
    }

    Comp->CrestCoeff = std::exp(-1.0f / (0.200f * SampleRate)); // 200ms
    Comp->GainEstimate = Comp->Threshold * -0.5f * Comp->Slope;
    Comp->AdaptCoeff = std::exp(-1.0f / (2.0f * SampleRate)); // 2s

    return Comp;
}

void ApplyCompression(Compressor *Comp, const ALsizei SamplesToDo, ALfloat (*OutBuffer)[BUFFERSIZE])
{
    const ALsizei numChans{Comp->NumChans};

    ASSUME(SamplesToDo > 0);
    ASSUME(numChans > 0);

    const ALfloat preGain{Comp->PreGain};
    if(preGain != 1.0f)
    {
        auto apply_gain = [SamplesToDo,preGain](ALfloat *input) noexcept -> void
        {
            ALfloat *buffer{al::assume_aligned<16>(input)};
            std::transform(buffer, buffer+SamplesToDo, buffer,
                std::bind(std::multiplies<float>{}, _1, preGain));
        };
        std::for_each(OutBuffer, OutBuffer+numChans, apply_gain);
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

    const ALfloat (&sideChain)[BUFFERSIZE*2] = Comp->SideChain;
    auto apply_comp = [SamplesToDo,&sideChain](ALfloat *input) noexcept -> void
    {
        ALfloat *buffer{al::assume_aligned<16>(input)};
        const ALfloat *gains{al::assume_aligned<16>(sideChain)};
        /* Mark the gains "input-1 type" as restrict, so the compiler can
         * vectorize this loop (otherwise it assumes a write to buffer[n] can
         * change gains[n+1]).
         */
        std::transform<const ALfloat*RESTRICT>(gains, gains+SamplesToDo, buffer, buffer,
            std::bind(std::multiplies<float>{}, _1, _2));
    };
    std::for_each(OutBuffer, OutBuffer+numChans, apply_comp);

    ASSUME(Comp->LookAhead >= 0);
    auto side_begin = std::begin(Comp->SideChain) + SamplesToDo;
    std::copy(side_begin, side_begin+Comp->LookAhead, std::begin(Comp->SideChain));
}


ALsizei GetCompressorLookAhead(const Compressor *Comp)
{ return Comp->LookAhead; }
