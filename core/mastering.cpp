
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
#include "altypes.hpp"
#include "gsl/gsl"
#include "opthelpers.h"


/* These structures assume BufferLineSize is a power of 2. */
static_assert((BufferLineSize & (BufferLineSize-1)) == 0, "BufferLineSize is not a power of 2");

struct SlidingHold {
    alignas(16) std::array<f32, BufferLineSize> mValues;
    std::array<sys_uint, BufferLineSize> mExpiries;
    unsigned mLowerIndex;
    unsigned mUpperIndex;
    sys_uint mLength;
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
auto UpdateSlidingHold(SlidingHold *Hold, unsigned const i, f32 const in) -> f32
{
    static constexpr auto mask = unsigned{BufferLineSize - 1};
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

void ShiftSlidingHold(SlidingHold *Hold, sys_uint const n)
{
    if(Hold->mLowerIndex < Hold->mUpperIndex)
    {
        auto expiries = std::span{Hold->mExpiries}.first(Hold->mLowerIndex+1);
        std::ranges::transform(expiries, expiries.begin(), [n](sys_uint const e) { return e-n; });
        expiries = std::span{Hold->mExpiries}.subspan(Hold->mUpperIndex);
        std::ranges::transform(expiries, expiries.begin(), [n](sys_uint const e) { return e-n; });
    }
    else
    {
        const auto expiries = std::span{Hold->mExpiries}.first(Hold->mLowerIndex+1)
            .subspan(Hold->mUpperIndex);
        std::ranges::transform(expiries, expiries.begin(), [n](sys_uint const e) { return e-n; });
    }
}

} // namespace

auto Compressor::Create(Params const params) -> std::unique_ptr<Compressor>
{
    const auto lookAhead = std::clamp(round(params.LookAheadTime*params.SampleRate), 0.0_f32,
        f32{BufferLineSize-1.0f}).reinterpret_as<sys_uint>();
    const auto hold = std::clamp(round(params.HoldTime*params.SampleRate), 0.0_f32,
        f32{BufferLineSize-1.0f}).reinterpret_as<sys_uint>();

    auto Comp = std::make_unique<Compressor>(PrivateToken{});
    Comp->mAuto.Knee = params.AutoFlags.test(Flags::AutoKnee);
    Comp->mAuto.Attack = params.AutoFlags.test(Flags::AutoAttack);
    Comp->mAuto.Release = params.AutoFlags.test(Flags::AutoRelease);
    Comp->mAuto.PostGain = params.AutoFlags.test(Flags::AutoPostGain);
    Comp->mAuto.Declip = params.AutoFlags.test(Flags::AutoPostGain)
        && params.AutoFlags.test(Flags::AutoDeclip);
    Comp->mLookAhead = lookAhead.c_val;
    Comp->mPreGain = pow(10.0_f32, params.PreGainDb / 20.0_f32);
    Comp->mPostGain = (log(10.0_f64)/20.0_f64 * params.PostGainDb).cast_to<f32>();
    Comp->mThreshold = (log(10.0_f64)/20.0_f64 * params.ThresholdDb).cast_to<f32>();
    Comp->mSlope = 1.0_f32/std::max(1.0_f32, params.Ratio) - 1.0_f32;
    Comp->mKnee = std::max(0.0_f64, log(10.0_f64)/20.0_f64 * params.KneeDb).cast_to<f32>();
    Comp->mAttack = std::max(1.0_f32, params.AttackTime * params.SampleRate);
    Comp->mRelease = std::max(1.0_f32, params.ReleaseTime * params.SampleRate);

    /* Knee width automation actually treats the compressor as a limiter. By
     * varying the knee width, it can effectively be seen as applying
     * compression over a wide range of ratios.
     */
    if(params.AutoFlags.test(Flags::AutoKnee))
        Comp->mSlope = -1.0_f32;

    if(lookAhead > 0)
    {
        /* The sliding hold implementation doesn't handle a length of 1. A 1-
         * sample hold is useless anyway, it would only ever give back what was
         * just given to it.
         */
        if(hold > 1)
        {
            Comp->mHold = std::make_unique<SlidingHold>();
            Comp->mHold->mValues[0] = -f32::infinity();
            Comp->mHold->mExpiries[0] = hold;
            Comp->mHold->mLength = hold;
        }
        Comp->mDelay.resize(params.NumChans.c_val, FloatBufferLine{});
    }

    Comp->mCrestCoeff = exp(-1.0_f32 / (0.200_f32 * params.SampleRate)); // 200ms
    Comp->mGainEstimate = Comp->mThreshold * -0.5_f32 * Comp->mSlope;
    Comp->mAdaptCoeff = exp(-1.0_f32 / (2.0_f32 * params.SampleRate)); // 2s

    return Comp;
}

Compressor::Compressor(PrivateToken) { }
Compressor::~Compressor() = default;


/* This is the heart of the feed-forward compressor.  It operates in the log
 * domain (to better match human hearing) and can apply some basic automation
 * to knee width, attack/release times, make-up/post gain, and clipping
 * reduction.
 */
void Compressor::gainCompressor(unsigned const SamplesToDo)
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
    auto a_att = exp(-1.0_f32 / t_att);
    auto a_rel = exp(-1.0_f32 / t_rel);
    auto y_1 = mLastRelease;
    auto y_L = mLastAttack;
    auto c_dev = mLastGainDev;

    ASSUME(SamplesToDo > 0);
    ASSUME(SamplesToDo <= BufferLineSize);

    std::ranges::transform(mSideChain | std::views::take(SamplesToDo),
        mSideChain | std::views::drop(mLookAhead), mSideChain.begin(),
        [&](f32 const input, f32 const lookAhead) -> f32
    {
        if(autoKnee)
            knee = std::max(0.0_f32, 2.5_f32*(c_dev + c_est));
        const auto knee_h = 0.5_f32 * knee;

        /* This is the gain computer. It applies a static compression curve to
         * the control signal.
         */
        const auto x_over = lookAhead - threshold;
        const auto y_G = (x_over <= -knee_h) ? 0.0_f32
            : (x_over.abs() < knee_h) ? (x_over+knee_h) * (x_over+knee_h) / (2.0_f32 * knee)
            : x_over;

        const auto y2_crest = *(crestFactor++);
        if(autoAttack)
        {
            t_att = 2.0_f32*attack/y2_crest;
            a_att = exp(-1.0_f32 / t_att);
        }
        if(autoRelease)
        {
            t_rel = 2.0_f32*release/y2_crest - t_att;
            a_rel = exp(-1.0_f32 / t_rel);
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

        return exp(postGain - y_L);
    });

    mLastRelease = y_1;
    mLastAttack = y_L;
    mLastGainDev = c_dev;
}

void Compressor::process(unsigned const SamplesToDo, std::span<FloatBufferLine> const InOut)
{
    ASSUME(SamplesToDo > 0);
    ASSUME(SamplesToDo <= BufferLineSize);

    if(const auto preGain = mPreGain; preGain != 1.0f)
    {
        std::ranges::for_each(InOut, [SamplesToDo,preGain](const FloatBufferSpan input) -> void
        {
            std::ranges::transform(input | std::views::take(SamplesToDo), input.begin(),
                [preGain](const float s) noexcept { return s * preGain.c_val; });
        });
    }

    /* Multichannel compression is linked via the absolute maximum of all
     * channels.
     */
    const auto sideChain = std::span{mSideChain}.subspan(mLookAhead, SamplesToDo);
    std::ranges::fill(sideChain, 0.0_f32);
    std::ranges::for_each(InOut, [sideChain](const FloatBufferSpan input) -> void
    {
        std::ranges::transform(sideChain, input, sideChain.begin(),
            [](f32 const s0, f32 const s1) noexcept -> f32
        { return std::max(s0, s1.abs()); });
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
            [&y2_rms,&y2_peak,a_crest](f32 const x_abs) noexcept -> f32
        {
            const auto x2 = f32{std::clamp(x_abs*x_abs, 0.000001_f32, 1000000.0_f32)};

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
        std::ranges::transform(sideChain, sideChain.begin(), [&i,hold](f32 const x_abs) -> f32
        {
            auto const x_G = log(std::max(0.000001_f32, x_abs));
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
        std::ranges::transform(sideChain, sideChain.begin(), [](f32 const s) -> f32
        { return log(std::max(0.000001_f32, s)); });
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
        std::ranges::transform(gains | std::views::transform(&f32::c_val), buffer, buffer.begin(),
            std::multiplies{});
    });

    std::ranges::copy(mSideChain | std::views::drop(SamplesToDo) | std::views::take(mLookAhead),
        mSideChain.begin());
}
