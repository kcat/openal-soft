
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
    const ALuint srate, const ALuint (&chanmap)[MAX_OUTPUT_CHANNELS]) : mChannelDec{inchans}
{
    mDualBand = allow_2band && (conf->FreqBands == 2);

    const bool periphonic{(conf->ChanMask&AMBI_PERIPHONIC_MASK) != 0};
    const std::array<float,MAX_AMBI_CHANNELS> &coeff_scale = GetAmbiScales(conf->CoeffScale);

    if(!mDualBand)
    {
        for(size_t j{0},k{0};j < mChannelDec.size();++j)
        {
            const size_t acn{periphonic ? j : AmbiIndex::From2D[j]};
            if(!(conf->ChanMask&(1u<<acn))) continue;
            const size_t order{AmbiIndex::OrderFromChannel[acn]};
            const float gain{conf->HFOrderGain[order] / coeff_scale[acn]};
            for(size_t i{0u};i < conf->Speakers.size();++i)
            {
                const size_t chanidx{chanmap[i]};
                mChannelDec[j].mGains.Single[chanidx] = conf->HFMatrix[i][k] * gain;
            }
            ++k;
        }
    }
    else
    {
        mChannelDec[0].mXOver.init(conf->XOverFreq / static_cast<float>(srate));
        for(size_t j{1};j < mChannelDec.size();++j)
            mChannelDec[j].mXOver = mChannelDec[0].mXOver;

        const float ratio{std::pow(10.0f, conf->XOverRatio / 40.0f)};
        for(size_t j{0},k{0};j < mChannelDec.size();++j)
        {
            const size_t acn{periphonic ? j : AmbiIndex::From2D[j]};
            if(!(conf->ChanMask&(1u<<acn))) continue;
            const size_t order{AmbiIndex::OrderFromChannel[acn]};
            const float hfGain{conf->HFOrderGain[order] * ratio / coeff_scale[acn]};
            const float lfGain{conf->LFOrderGain[order] / ratio / coeff_scale[acn]};
            for(size_t i{0u};i < conf->Speakers.size();++i)
            {
                const size_t chanidx{chanmap[i]};
                mChannelDec[j].mGains.Dual[sHFBand][chanidx] = conf->HFMatrix[i][k] * hfGain;
                mChannelDec[j].mGains.Dual[sLFBand][chanidx] = conf->LFMatrix[i][k] * lfGain;
            }
            ++k;
        }
    }
}

BFormatDec::BFormatDec(const ALuint inchans, const al::span<const ChannelDec> chancoeffs)
    : mChannelDec{inchans}
{
    for(size_t j{0};j < mChannelDec.size();++j)
    {
        float *outcoeffs{mChannelDec[j].mGains.Single};
        for(const ChannelDec &incoeffs : chancoeffs)
            *(outcoeffs++) = incoeffs[j];
    }
}


void BFormatDec::process(const al::span<FloatBufferLine> OutBuffer,
    const FloatBufferLine *InSamples, const size_t SamplesToDo)
{
    ASSUME(SamplesToDo > 0);

    if(mDualBand)
    {
        const al::span<const float> hfSamples{mSamples[sHFBand].data(), SamplesToDo};
        const al::span<const float> lfSamples{mSamples[sLFBand].data(), SamplesToDo};
        for(auto &chandec : mChannelDec)
        {
            chandec.mXOver.process({InSamples->data(), SamplesToDo}, mSamples[sHFBand].data(),
                mSamples[sLFBand].data());
            MixSamples(hfSamples, OutBuffer, chandec.mGains.Dual[sHFBand],
                chandec.mGains.Dual[sHFBand], 0, 0);
            MixSamples(hfSamples, OutBuffer, chandec.mGains.Dual[sLFBand],
                chandec.mGains.Dual[sLFBand], 0, 0);
            ++InSamples;
        }
    }
    else
    {
        for(auto &chandec : mChannelDec)
        {
            MixSamples({InSamples->data(), SamplesToDo}, OutBuffer, chandec.mGains.Single,
                chandec.mGains.Single, 0, 0);
            ++InSamples;
        }
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
