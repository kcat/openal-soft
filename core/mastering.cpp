
#include "config.h"

#include "mastering.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <ranges>
#include <span>

#include "alnumeric.h"
#include "gsl/gsl"
#include "opthelpers.h"


/* These structures assume BufferLineSize is a power of 2. */
static_assert((BufferLineSize & (BufferLineSize-1)) == 0, "BufferLineSize is not a power of 2");

struct SlidingHold {
    alignas(16) FloatBufferLine mValues;
    std::array<uint,BufferLineSize> mExpiries;
    uint mLowerIndex;
    uint mUpperIndex;
    uint mLength;
};


namespace {

template<std::size_t A, typename T, std::size_t N>
constexpr auto assume_aligned_span(const std::span<T,N> s) noexcept -> std::span<T,N>
{ return std::span<T,N>{std::assume_aligned<A>(s.data()), s.size()}; }

/* This sliding hold follows the input level with an instant attack and a
 * fixed duration hold before an instant release to the next highest level.
 * It is a sliding window maximum (descending maxima) implementation based on
 * Richard Harter's ascending minima algorithm available at:
 *
 *   http://www.richardhartersworld.com/cri/2001/slidingmin.html
 */
float UpdateSlidingHold(SlidingHold *Hold, const uint i, const float in)
{
    static constexpr auto mask = uint{BufferLineSize - 1};
    const auto length = Hold->mLength;
    const auto values = std::span{Hold->mValues};
    const auto expiries = std::span{Hold->mExpiries};
    auto lowerIndex = Hold->mLowerIndex;
    auto upperIndex = Hold->mUpperIndex;

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
        auto findLowerIndex = [&lowerIndex,in,values]() noexcept -> bool
        {
            do {
                if(!(in >= values[lowerIndex]))
                    return true;
            } while(lowerIndex--);
            return false;
        };
        while(!findLowerIndex())
            lowerIndex = mask;

        lowerIndex = (lowerIndex + 1) & mask;
        values[lowerIndex] = in;
        expiries[lowerIndex] = i + length;
    }

    Hold->mLowerIndex = lowerIndex;
    Hold->mUpperIndex = upperIndex;

    return values[upperIndex];
}

void ShiftSlidingHold(SlidingHold *Hold, const uint n)
{
    if(Hold->mLowerIndex < Hold->mUpperIndex)
    {
        auto expiries = std::span{Hold->mExpiries}.first(Hold->mLowerIndex+1);
        std::ranges::transform(expiries, expiries.begin(), [n](const uint e) { return e - n; });
        expiries = std::span{Hold->mExpiries}.subspan(Hold->mUpperIndex);
        std::ranges::transform(expiries, expiries.begin(), [n](const uint e) { return e - n; });
    }
    else
    {
        const auto expiries = std::span{Hold->mExpiries}.first(Hold->mLowerIndex+1)
            .subspan(Hold->mUpperIndex);
        std::ranges::transform(expiries, expiries.begin(), [n](const uint e) { return e - n; });
    }
}

} // namespace

auto Compressor::Create(const size_t NumChans, const float SampleRate, const FlagBits AutoFlags,
    const float LookAheadTime, const float HoldTime, const float PreGainDb, const float PostGainDb,
    const float ThresholdDb, const float Ratio, const float KneeDb, const float AttackTime,
    const float ReleaseTime) -> std::unique_ptr<Compressor>
{
    const auto lookAhead = gsl::narrow_cast<uint>(std::clamp(std::round(LookAheadTime*SampleRate),
        0.0f, BufferLineSize-1.0f));
    const auto hold = gsl::narrow_cast<uint>(std::clamp(std::round(HoldTime*SampleRate), 0.0f,
        BufferLineSize-1.0f));

    auto Comp = std::make_unique<Compressor>(PrivateToken{});
    Comp->mAuto.Knee = AutoFlags.test(AutoKnee);
    Comp->mAuto.Attack = AutoFlags.test(AutoAttack);
    Comp->mAuto.Release = AutoFlags.test(AutoRelease);
    Comp->mAuto.PostGain = AutoFlags.test(AutoPostGain);
    Comp->mAuto.Declip = AutoFlags.test(AutoPostGain) && AutoFlags.test(AutoDeclip);
    Comp->mLookAhead = lookAhead;
    Comp->mPreGain = std::pow(10.0f, PreGainDb / 20.0f);
    Comp->mPostGain = std::log(10.0f)/20.0f * PostGainDb;
    Comp->mThreshold = std::log(10.0f)/20.0f * ThresholdDb;
    Comp->mSlope = 1.0f / std::max(1.0f, Ratio) - 1.0f;
    Comp->mKnee = std::max(0.0f, std::log(10.0f)/20.0f * KneeDb);
    Comp->mAttack = std::max(1.0f, AttackTime * SampleRate);
    Comp->mRelease = std::max(1.0f, ReleaseTime * SampleRate);

    /* Knee width automation actually treats the compressor as a limiter. By
     * varying the knee width, it can effectively be seen as applying
     * compression over a wide range of ratios.
     */
    if(AutoFlags.test(AutoKnee))
        Comp->mSlope = -1.0f;

    if(lookAhead > 0)
    {
        /* The sliding hold implementation doesn't handle a length of 1. A 1-
         * sample hold is useless anyway, it would only ever give back what was
         * just given to it.
         */
        if(hold > 1)
        {
            Comp->mHold = std::make_unique<SlidingHold>();
            Comp->mHold->mValues[0] = -std::numeric_limits<float>::infinity();
            Comp->mHold->mExpiries[0] = hold;
            Comp->mHold->mLength = hold;
        }
        Comp->mDelay.resize(NumChans, FloatBufferLine{});
    }

    Comp->mCrestCoeff = std::exp(-1.0f / (0.200f * SampleRate)); // 200ms
    Comp->mGainEstimate = Comp->mThreshold * -0.5f * Comp->mSlope;
    Comp->mAdaptCoeff = std::exp(-1.0f / (2.0f * SampleRate)); // 2s

    return Comp;
}

Compressor::Compressor(PrivateToken) { }
Compressor::~Compressor() = default;


/* This is the heart of the feed-forward compressor.  It operates in the log
 * domain (to better match human hearing) and can apply some basic automation
 * to knee width, attack/release times, make-up/post gain, and clipping
 * reduction.
 */
void Compressor::gainCompressor(const uint SamplesToDo)
{
    const auto autoKnee = mAuto.Knee;
    const auto autoAttack = mAuto.Attack;
    const auto autoRelease = mAuto.Release;
    const auto autoPostGain = mAuto.PostGain;
    const auto autoDeclip = mAuto.Declip;
    const auto threshold = mThreshold;
    const auto slope = mSlope;
    const auto attack = mAttack;
    const auto release = mRelease;
    const auto c_est = mGainEstimate;
    const auto a_adp = mAdaptCoeff;
    auto crestFactor = mCrestFactor.cbegin();
    auto postGain = mPostGain;
    auto knee = mKnee;
    auto t_att = attack;
    auto t_rel = release - attack;
    auto a_att = std::exp(-1.0f / t_att);
    auto a_rel = std::exp(-1.0f / t_rel);
    auto y_1 = mLastRelease;
    auto y_L = mLastAttack;
    auto c_dev = mLastGainDev;

    ASSUME(SamplesToDo > 0);
    ASSUME(SamplesToDo <= BufferLineSize);

    std::ranges::transform(mSideChain | std::views::take(SamplesToDo),
        mSideChain | std::views::drop(mLookAhead), mSideChain.begin(),
        [&](const float input, const float lookAhead) -> float
    {
        if(autoKnee)
            knee = std::max(0.0f, 2.5f*(c_dev + c_est));
        const auto knee_h = 0.5f * knee;

        /* This is the gain computer. It applies a static compression curve to
         * the control signal.
         */
        const auto x_over = lookAhead - threshold;
        const auto y_G = (x_over <= -knee_h) ? 0.0f
            : (std::fabs(x_over) < knee_h) ? (x_over+knee_h) * (x_over+knee_h) / (2.0f * knee)
            : x_over;

        const auto y2_crest = *(crestFactor++);
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
         * detector. The attack time is subtracted from the release time
         * above to compensate for the chained operating mode.
         */
        const auto x_L = -slope * y_G;
        y_1 = std::max(x_L, lerpf(x_L, y_1, a_rel));
        y_L = lerpf(y_1, y_L, a_att);

        /* Knee width and make-up gain automation make use of a smoothed
         * measurement of deviation between the control signal and estimate.
         * The estimate is also used to bias the measurement to hot-start its
         * average.
         */
        c_dev = lerpf(-(y_L+c_est), c_dev, a_adp);

        if(autoPostGain)
        {
            /* Clipping reduction is only viable when make-up gain is being
             * automated. It modifies the deviation to further attenuate the
             * control signal when clipping is detected. The adaptation time
             * is sufficiently long enough to suppress further clipping at the
             * same output level.
             */
            if(autoDeclip)
                c_dev = std::max(c_dev, input - y_L - threshold - c_est);

            postGain = -(c_dev + c_est);
        }

        return std::exp(postGain - y_L);
    });

    mLastRelease = y_1;
    mLastAttack = y_L;
    mLastGainDev = c_dev;
}

void Compressor::process(const uint SamplesToDo, const std::span<FloatBufferLine> InOut)
{
    ASSUME(SamplesToDo > 0);
    ASSUME(SamplesToDo <= BufferLineSize);

    if(const auto preGain = mPreGain; preGain != 1.0f)
    {
        std::ranges::for_each(InOut, [SamplesToDo,preGain](const FloatBufferSpan input) -> void
        {
            std::ranges::transform(input | std::views::take(SamplesToDo), input.begin(),
                [preGain](const float s) noexcept { return s * preGain; });
        });
    }

    /* Multichannel compression is linked via the absolute maximum of all
     * channels.
     */
    const auto sideChain = std::span{mSideChain}.subspan(mLookAhead, SamplesToDo);
    std::ranges::fill(sideChain, 0.0f);
    std::ranges::for_each(InOut, [sideChain](const FloatBufferSpan input) -> void
    {
        std::ranges::transform(sideChain, input, sideChain.begin(),
            [](const float s0, const float s1) noexcept -> float
        { return std::max(s0, std::fabs(s1)); });
    });

    if(mAuto.Attack || mAuto.Release)
    {
        /* This calculates the squared crest factor of the control signal for
         * the basic automation of the attack/release times. As suggested by
         * the paper, it uses an instantaneous squared peak detector and a
         * squared RMS detector both with 200ms release times.
         */
        const auto a_crest = mCrestCoeff;
        auto y2_peak = mLastPeakSq;
        auto y2_rms = mLastRmsSq;

        std::ranges::transform(sideChain, mCrestFactor.begin(),
            [&y2_rms,&y2_peak,a_crest](const float x_abs) noexcept -> float
        {
            const auto x2 = std::clamp(x_abs*x_abs, 0.000001f, 1000000.0f);

            y2_peak = std::max(x2, lerpf(x2, y2_peak, a_crest));
            y2_rms = lerpf(x2, y2_rms, a_crest);
            return y2_peak / y2_rms;
        });

        mLastPeakSq = y2_peak;
        mLastRmsSq = y2_rms;
    }

    if(auto *hold = mHold.get())
    {
        /* An optional hold can be used to extend the peak detector so it can
         * more solidly detect fast transients. This is best used when
         * operating as a limiter.
         */
        auto i = 0u;
        std::ranges::transform(sideChain, sideChain.begin(), [&i,hold](const float x_abs) -> float
        {
            const auto x_G = std::log(std::max(0.000001f, x_abs));
            return UpdateSlidingHold(hold, i++, x_G);
        });
        ShiftSlidingHold(hold, SamplesToDo);
    }
    else
    {
        /* The side-chain starts with a simple peak detector (based on the
         * absolute value of the incoming signal) and performs most of its
         * operations in the log domain.
         */
        std::ranges::transform(sideChain, sideChain.begin(), [](const float s) -> float
        { return std::log(std::max(0.000001f, s)); });
    }

    gainCompressor(SamplesToDo);

    if(!mDelay.empty())
    {
        /* Combined with the hold time, a look-ahead delay can improve handling
         * of fast transients by allowing the envelope time to converge prior
         * to reaching the offending impulse. This is best used when operating
         * as a limiter.
         */
        const auto lookAhead = mLookAhead;
        ASSUME(lookAhead > 0);
        ASSUME(lookAhead < BufferLineSize);

        auto delays = mDelay.begin();
        std::ranges::for_each(InOut, [SamplesToDo,lookAhead,&delays](const FloatBufferSpan buffer)
        {
            const auto inout = buffer.first(SamplesToDo);
            const auto delaybuf = std::span{*(delays++)}.first(lookAhead);

            if(SamplesToDo >= delaybuf.size()) [[likely]]
            {
                const auto inout_start = std::prev(inout.end(), std::ssize(delaybuf));
                const auto delay_end = std::ranges::rotate(inout, inout_start).begin();
                std::ranges::swap_ranges(std::span{inout.begin(), delay_end}, delaybuf);
            }
            else
            {
                const auto delay_start = std::ranges::swap_ranges(inout, delaybuf).in2;
                std::ranges::rotate(delaybuf, delay_start);
            }
        });
    }

    const auto gains = std::span{mSideChain}.first(SamplesToDo);
    std::ranges::for_each(InOut, [gains](const FloatBufferSpan inout) -> void
    {
        const auto buffer = assume_aligned_span<16>(std::span{inout});
        std::ranges::transform(gains, buffer, buffer.begin(), std::multiplies{});
    });

    std::ranges::copy(mSideChain | std::views::drop(SamplesToDo) | std::views::take(mLookAhead),
        mSideChain.begin());
}
