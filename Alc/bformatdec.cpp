
#include "config.h"

#include <cmath>
#include <array>
#include <vector>
#include <numeric>
#include <algorithm>
#include <functional>

#include "bformatdec.h"
#include "ambdec.h"
#include "filters/splitter.h"
#include "alu.h"

#include "threads.h"
#include "almalloc.h"


namespace {

using namespace std::placeholders;

#define HF_BAND 0
#define LF_BAND 1
static_assert(BFormatDec::sNumBands == 2, "Unexpected BFormatDec::sNumBands");

constexpr ALfloat Ambi3DDecoderHFScale[MAX_AMBI_ORDER+1] = {
    2.00000000f, 1.15470054f
};
constexpr ALfloat Ambi3DDecoderHFScale2O[MAX_AMBI_ORDER+1] = {
    1.49071198f, 1.15470054f
};
constexpr ALfloat Ambi3DDecoderHFScale3O[MAX_AMBI_ORDER+1] = {
    1.17958441f, 1.01578297f
};

inline auto GetDecoderHFScales(ALsizei order) noexcept -> const ALfloat(&)[MAX_AMBI_ORDER+1]
{
    if(order >= 3) return Ambi3DDecoderHFScale3O;
    if(order == 2) return Ambi3DDecoderHFScale2O;
    return Ambi3DDecoderHFScale;
}

inline auto GetAmbiScales(AmbDecScale scaletype) noexcept -> const std::array<float,MAX_AMBI_COEFFS>&
{
    if(scaletype == AmbDecScale::FuMa) return AmbiScale::FromFuMa;
    if(scaletype == AmbDecScale::SN3D) return AmbiScale::FromSN3D;
    return AmbiScale::FromN3D;
}

} // namespace


void BFormatDec::reset(const AmbDecConf *conf, bool allow_2band, ALsizei inchans, ALuint srate, const ALsizei (&chanmap)[MAX_OUTPUT_CHANNELS])
{
    mSamples.clear();
    mSamplesHF = nullptr;
    mSamplesLF = nullptr;

    mMatrix = MatrixU{};
    mDualBand = allow_2band && (conf->FreqBands == 2);
    if(!mDualBand)
        mSamples.resize(1);
    else
    {
        mSamples.resize(inchans * 2);
        mSamplesHF = mSamples.data();
        mSamplesLF = mSamplesHF + inchans;
    }
    mNumChannels = inchans;

    mEnabled = std::accumulate(std::begin(chanmap), std::begin(chanmap)+conf->Speakers.size(), 0u,
        [](ALuint mask, const ALsizei &chan) noexcept -> ALuint
        { return mask | (1 << chan); }
    );

    const ALfloat xover_norm{conf->XOverFreq / (float)srate};

    const ALsizei out_order{
        (conf->ChanMask > AMBI_3ORDER_MASK) ? 4 :
        (conf->ChanMask > AMBI_2ORDER_MASK) ? 3 :
        (conf->ChanMask > AMBI_1ORDER_MASK) ? 2 : 1};
    {
        const ALfloat (&hfscales)[MAX_AMBI_ORDER+1] = GetDecoderHFScales(out_order);
        /* The specified filter gain is for the mid-point/reference gain. The
         * gain at the shelf itself will be the square of that, so specify the
         * square-root of the desired shelf gain.
         */
        const ALfloat gain0{std::sqrt(Ambi3DDecoderHFScale[0] / hfscales[0])};
        const ALfloat gain1{std::sqrt(Ambi3DDecoderHFScale[1] / hfscales[1])};

        mShelf[0].setParams(BiquadType::HighShelf, gain0, xover_norm,
            calc_rcpQ_from_slope(gain0, 1.0f));
        mShelf[1].setParams(BiquadType::HighShelf, gain1, xover_norm,
            calc_rcpQ_from_slope(gain1, 1.0f));
        std::for_each(std::begin(mShelf)+2, std::end(mShelf),
            std::bind(std::mem_fn(&BiquadFilter::copyParamsFrom), _1, mShelf[1]));
    }

    const bool periphonic{(conf->ChanMask&AMBI_PERIPHONIC_MASK) != 0};
    const std::array<float,MAX_AMBI_COEFFS> &coeff_scale = GetAmbiScales(conf->CoeffScale);
    const ALsizei coeff_count{periphonic ? MAX_AMBI_COEFFS : MAX_AMBI2D_COEFFS};

    if(!mDualBand)
    {
        for(size_t i{0u};i < conf->Speakers.size();i++)
        {
            ALfloat (&mtx)[MAX_AMBI_COEFFS] = mMatrix.Single[chanmap[i]];
            for(ALsizei j{0},k{0};j < coeff_count;j++)
            {
                const ALsizei l{periphonic ? j : AmbiIndex::From2D[j]};
                if(!(conf->ChanMask&(1<<l))) continue;
                mtx[j] = conf->HFMatrix[i][k] / coeff_scale[l] *
                    ((l>=9) ? conf->HFOrderGain[3] :
                    (l>=4) ? conf->HFOrderGain[2] :
                    (l>=1) ? conf->HFOrderGain[1] : conf->HFOrderGain[0]);
                ++k;
            }
        }
    }
    else
    {
        mXOver[0].init(xover_norm);
        std::fill(std::begin(mXOver)+1, std::end(mXOver), mXOver[0]);

        const float ratio{std::pow(10.0f, conf->XOverRatio / 40.0f)};
        for(size_t i{0u};i < conf->Speakers.size();i++)
        {
            ALfloat (&mtx)[sNumBands][MAX_AMBI_COEFFS] = mMatrix.Dual[chanmap[i]];
            for(ALsizei j{0},k{0};j < coeff_count;j++)
            {
                const ALsizei l{periphonic ? j : AmbiIndex::From2D[j]};
                if(!(conf->ChanMask&(1<<l))) continue;
                mtx[HF_BAND][j] = conf->HFMatrix[i][k] / coeff_scale[l] *
                    ((l>=9) ? conf->HFOrderGain[3] :
                    (l>=4) ? conf->HFOrderGain[2] :
                    (l>=1) ? conf->HFOrderGain[1] : conf->HFOrderGain[0]) * ratio;
                mtx[LF_BAND][j] = conf->LFMatrix[i][k] / coeff_scale[l] *
                    ((l>=9) ? conf->LFOrderGain[3] :
                    (l>=4) ? conf->LFOrderGain[2] :
                    (l>=1) ? conf->LFOrderGain[1] : conf->LFOrderGain[0]) / ratio;
                ++k;
            }
        }
    }
}

void BFormatDec::reset(const ALsizei inchans, const ALfloat xover_norm, const ALsizei chancount, const ChannelDec (&chancoeffs)[MAX_OUTPUT_CHANNELS], const ALsizei (&chanmap)[MAX_OUTPUT_CHANNELS])
{
    mSamples.clear();
    mSamplesHF = nullptr;
    mSamplesLF = nullptr;

    mMatrix = MatrixU{};
    mDualBand = false;
    mSamples.resize(1);
    mNumChannels = inchans;

    mEnabled = std::accumulate(std::begin(chanmap), std::begin(chanmap)+chancount, 0u,
        [](ALuint mask, const ALsizei &chan) noexcept -> ALuint
        { return mask | (1 << chan); }
    );

    const ALsizei out_order{
        (inchans > 7) ? 4 :
        (inchans > 5) ? 3 :
        (inchans > 3) ? 2 : 1};
    {
        const ALfloat (&hfscales)[MAX_AMBI_ORDER+1] = GetDecoderHFScales(out_order);
        /* The specified filter gain is for the mid-point/reference gain. The
         * gain at the shelf itself will be the square of that, so specify the
         * square-root of the desired shelf gain.
         */
        const ALfloat gain0{std::sqrt(Ambi3DDecoderHFScale[0] / hfscales[0])};
        const ALfloat gain1{std::sqrt(Ambi3DDecoderHFScale[1] / hfscales[1])};

        mShelf[0].setParams(BiquadType::HighShelf, gain0, xover_norm,
            calc_rcpQ_from_slope(gain0, 1.0f));
        mShelf[1].setParams(BiquadType::HighShelf, gain1, xover_norm,
            calc_rcpQ_from_slope(gain1, 1.0f));
        std::for_each(std::begin(mShelf)+2, std::end(mShelf),
            std::bind(std::mem_fn(&BiquadFilter::copyParamsFrom), _1, mShelf[1]));
    }

    for(ALsizei i{0};i < chancount;i++)
    {
        const ALfloat (&coeffs)[MAX_AMBI_COEFFS] = chancoeffs[chanmap[i]];
        ALfloat (&mtx)[MAX_AMBI_COEFFS] = mMatrix.Single[chanmap[i]];

        std::copy_n(std::begin(coeffs), inchans, std::begin(mtx));
    }
}


void BFormatDec::process(ALfloat (*OutBuffer)[BUFFERSIZE], const ALsizei OutChannels, const ALfloat (*InSamples)[BUFFERSIZE], const ALsizei SamplesToDo)
{
    ASSUME(OutChannels > 0);
    ASSUME(mNumChannels > 0);

    if(mDualBand)
    {
        for(ALsizei i{0};i < mNumChannels;i++)
            mXOver[i].process(mSamplesHF[i].data(), mSamplesLF[i].data(), InSamples[i],
                              SamplesToDo);

        for(ALsizei chan{0};chan < OutChannels;chan++)
        {
            if(UNLIKELY(!(mEnabled&(1<<chan))))
                continue;

            MixRowSamples(OutBuffer[chan], mMatrix.Dual[chan][HF_BAND],
                &reinterpret_cast<ALfloat(&)[BUFFERSIZE]>(mSamplesHF[0]),
                mNumChannels, 0, SamplesToDo);
            MixRowSamples(OutBuffer[chan], mMatrix.Dual[chan][LF_BAND],
                &reinterpret_cast<ALfloat(&)[BUFFERSIZE]>(mSamplesLF[0]),
                mNumChannels, 0, SamplesToDo);
        }
    }
    else
    {
        for(ALsizei chan{0};chan < OutChannels;chan++)
        {
            if(UNLIKELY(!(mEnabled&(1<<chan))))
                continue;

            MixRowSamples(OutBuffer[chan], mMatrix.Single[chan], InSamples,
                          mNumChannels, 0, SamplesToDo);
        }
    }
}

void BFormatDec::upSample(ALfloat (*OutBuffer)[BUFFERSIZE], const ALfloat (*InSamples)[BUFFERSIZE], const ALsizei InChannels, const ALsizei SamplesToDo)
{
    ASSUME(InChannels > 0);
    ASSUME(SamplesToDo > 0);

    /* This up-sampler leverages the differences observed in dual-band higher-
     * order decoder matrices compared to first-order. For the same output
     * channel configuration, the low-frequency matrix has identical
     * coefficients in the shared input channels, while the high-frequency
     * matrix has extra scalars applied to the W channel and X/Y/Z channels.
     * Using a high-shelf filter to mix the first-order content into the
     * higher-order stream, with the appropriate counter-scales applied to the
     * HF response, results in the subsequent higher-order decode generating
     * the same response as a first-order decode.
     */
    for(ALsizei i{0};i < InChannels;i++)
    {
        mShelf[i].process(mSamples[0].data(), InSamples[i], SamplesToDo);

        const ALfloat *RESTRICT src{al::assume_aligned<16>(mSamples[0].data())};
        ALfloat *dst{al::assume_aligned<16>(OutBuffer[i])};
        std::transform(src, src+SamplesToDo, dst, dst, std::plus<float>{});
    }
}


void AmbiUpsampler::reset(const ALsizei out_order, const ALfloat xover_norm)
{
    const ALfloat (&hfscales)[MAX_AMBI_ORDER+1] = GetDecoderHFScales(out_order);
    const ALfloat gain0{std::sqrt(Ambi3DDecoderHFScale[0] / hfscales[0])};
    const ALfloat gain1{std::sqrt(Ambi3DDecoderHFScale[1] / hfscales[1])};

    mShelf[0].setParams(BiquadType::HighShelf, gain0, xover_norm,
        calc_rcpQ_from_slope(gain0, 1.0f));
    mShelf[1].setParams(BiquadType::HighShelf, gain1, xover_norm,
        calc_rcpQ_from_slope(gain1, 1.0f));
    std::for_each(std::begin(mShelf)+2, std::end(mShelf),
        std::bind(std::mem_fn(&BiquadFilter::copyParamsFrom), _1, mShelf[1]));
}

void AmbiUpsampler::process(ALfloat (*OutBuffer)[BUFFERSIZE], const ALfloat (*InSamples)[BUFFERSIZE], const ALsizei InChannels, const ALsizei SamplesToDo)
{
    ASSUME(SamplesToDo > 0);
    ASSUME(InChannels > 0);
    ASSUME(InChannels <= 4);

    for(ALsizei i{0};i < InChannels;i++)
    {
        mShelf[i].process(mSamples, InSamples[i], SamplesToDo);

        const ALfloat *RESTRICT src{al::assume_aligned<16>(mSamples)};
        ALfloat *dst{al::assume_aligned<16>(OutBuffer[i])};
        std::transform(src, src+SamplesToDo, dst, dst, std::plus<float>{});
    }
}
