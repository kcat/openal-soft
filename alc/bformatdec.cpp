
#include "config.h"

#include "bformatdec.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iterator>
#include <numeric>

#include "AL/al.h"

#include "almalloc.h"
#include "alu.h"
#include "ambdec.h"
#include "filters/splitter.h"
#include "opthelpers.h"


namespace {

constexpr std::array<float,MAX_AMBI_ORDER+1> Ambi3DDecoderHFScale{{
    1.00000000e+00f, 1.00000000e+00f
}};
constexpr std::array<float,MAX_AMBI_ORDER+1> Ambi3DDecoderHFScale2O{{
    7.45355990e-01f, 1.00000000e+00f, 1.00000000e+00f
}};
constexpr std::array<float,MAX_AMBI_ORDER+1> Ambi3DDecoderHFScale3O{{
    5.89792205e-01f, 8.79693856e-01f, 1.00000000e+00f, 1.00000000e+00f
}};

inline auto GetDecoderHFScales(ALuint order) noexcept -> const std::array<float,MAX_AMBI_ORDER+1>&
{
    if(order >= 3) return Ambi3DDecoderHFScale3O;
    if(order == 2) return Ambi3DDecoderHFScale2O;
    return Ambi3DDecoderHFScale;
}

inline auto GetAmbiScales(AmbDecScale scaletype) noexcept
    -> const std::array<float,MAX_AMBI_CHANNELS>&
{
    if(scaletype == AmbDecScale::FuMa) return AmbiScale::FromFuMa;
    if(scaletype == AmbDecScale::SN3D) return AmbiScale::FromSN3D;
    return AmbiScale::FromN3D;
}

} // namespace


BFormatDec::BFormatDec(const AmbDecConf *conf, const bool allow_2band, const ALuint inchans,
    const ALuint srate, const ALuint (&chanmap)[MAX_OUTPUT_CHANNELS])
{
    mDualBand = allow_2band && (conf->FreqBands == 2);
    mNumChannels = inchans;

    const bool periphonic{(conf->ChanMask&AMBI_PERIPHONIC_MASK) != 0};
    const std::array<float,MAX_AMBI_CHANNELS> &coeff_scale = GetAmbiScales(conf->CoeffScale);

    if(!mDualBand)
    {
        for(size_t i{0u};i < conf->Speakers.size();i++)
        {
            const size_t chanidx{chanmap[i]};
            for(size_t j{0},k{0};j < mNumChannels;j++)
            {
                const size_t acn{periphonic ? j : AmbiIndex::From2D[j]};
                if(!(conf->ChanMask&(1u<<acn))) continue;
                mMatrix.Single[j][chanidx] = conf->HFMatrix[i][k] / coeff_scale[acn] *
                    conf->HFOrderGain[AmbiIndex::OrderFromChannel[acn]];
                ++k;
            }
        }
    }
    else
    {
        mXOver[0].init(conf->XOverFreq / static_cast<float>(srate));
        std::fill(std::begin(mXOver)+1, std::end(mXOver), mXOver[0]);

        const float ratio{std::pow(10.0f, conf->XOverRatio / 40.0f)};
        for(size_t i{0u};i < conf->Speakers.size();i++)
        {
            const size_t chanidx{chanmap[i]};
            for(size_t j{0},k{0};j < mNumChannels;j++)
            {
                const size_t acn{periphonic ? j : AmbiIndex::From2D[j]};
                if(!(conf->ChanMask&(1u<<acn))) continue;
                mMatrix.Dual[j][sHFBand][chanidx] = conf->HFMatrix[i][k] / coeff_scale[acn] *
                    conf->HFOrderGain[AmbiIndex::OrderFromChannel[acn]] * ratio;
                mMatrix.Dual[j][sLFBand][chanidx] = conf->LFMatrix[i][k] / coeff_scale[acn] *
                    conf->LFOrderGain[AmbiIndex::OrderFromChannel[acn]] / ratio;
                ++k;
            }
        }
    }
}

BFormatDec::BFormatDec(const ALuint inchans, const ChannelDec (&chancoeffs)[MAX_OUTPUT_CHANNELS],
    const al::span<const ALuint> chanmap)
{
    mNumChannels = inchans;

    const ChannelDec *incoeffs{chancoeffs};
    auto set_coeffs = [this,inchans,&incoeffs](const ALuint chanidx) noexcept -> void
    {
        const float (&coeffs)[MAX_AMBI_CHANNELS] = *(incoeffs++);

        ASSUME(inchans > 0);
        for(size_t j{0};j < inchans;++j)
            mMatrix.Single[j][chanidx] = coeffs[j];
    };
    std::for_each(chanmap.begin(), chanmap.end(), set_coeffs);
}


void BFormatDec::process(const al::span<FloatBufferLine> OutBuffer,
    const FloatBufferLine *InSamples, const size_t SamplesToDo)
{
    ASSUME(SamplesToDo > 0);

    if(mDualBand)
    {
        const al::span<const float> hfSamples{mSamples[sHFBand].data(), SamplesToDo};
        const al::span<const float> lfSamples{mSamples[sLFBand].data(), SamplesToDo};
        const size_t numchans{mNumChannels};
        for(size_t i{0};i < numchans;i++)
        {
            mXOver[i].process({InSamples[i].data(), SamplesToDo}, mSamples[sHFBand].data(),
                mSamples[sLFBand].data());
            MixSamples(hfSamples, OutBuffer, mMatrix.Dual[i][sHFBand], mMatrix.Dual[i][sHFBand],
                0, 0);
            MixSamples(lfSamples, OutBuffer, mMatrix.Dual[i][sLFBand], mMatrix.Dual[i][sLFBand],
                0, 0);
        }
    }
    else
    {
        const size_t numchans{mNumChannels};
        for(size_t i{0};i < numchans;i++)
            MixSamples({InSamples[i].data(), SamplesToDo}, OutBuffer, mMatrix.Single[i],
                mMatrix.Single[i], 0, 0);
    }
}


auto BFormatDec::GetHFOrderScales(const ALuint in_order, const ALuint out_order) noexcept
    -> std::array<float,MAX_AMBI_ORDER+1>
{
    std::array<float,MAX_AMBI_ORDER+1> ret{};

    assert(out_order >= in_order);

    const auto &target = GetDecoderHFScales(out_order);
    const auto &input = GetDecoderHFScales(in_order);

    for(size_t i{0};i < in_order+1;++i)
        ret[i] = input[i] / target[i];

    return ret;
}
