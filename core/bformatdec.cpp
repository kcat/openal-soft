
#include "config.h"

#include "bformatdec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <numbers>
#include <utility>

#include "bufferline.h"
#include "filters/splitter.h"
#include "flexarray.h"
#include "front_stablizer.h"
#include "mixer.h"
#include "opthelpers.h"


namespace {

template<typename... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

template<typename... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

} // namespace

BFormatDec::BFormatDec(const size_t inchans, const std::span<const ChannelDec> coeffs,
    const std::span<const ChannelDec> coeffslf, const float xover_f0norm,
    std::unique_ptr<FrontStablizer> stablizer)
    : mStablizer{std::move(stablizer)}
{
    if(coeffslf.empty())
    {
        auto &decoder = mChannelDec.emplace<std::vector<ChannelDecoderSingle>>(inchans);
        for(size_t j{0};j < decoder.size();++j)
        {
            std::transform(coeffs.begin(), coeffs.end(), decoder[j].mGains.begin(),
                [j](const ChannelDec &incoeffs) { return incoeffs[j]; });
        }
    }
    else
    {
        auto &decoder = mChannelDec.emplace<std::vector<ChannelDecoderDual>>(inchans);
        decoder[0].mXOver.init(xover_f0norm);
        for(size_t j{1};j < decoder.size();++j)
            decoder[j].mXOver = decoder[0].mXOver;

        for(size_t j{0};j < decoder.size();++j)
        {
            std::transform(coeffs.begin(), coeffs.end(), decoder[j].mGains[sHFBand].begin(),
                [j](const ChannelDec &incoeffs) { return incoeffs[j]; });

            std::transform(coeffslf.begin(), coeffslf.end(), decoder[j].mGains[sLFBand].begin(),
                [j](const ChannelDec &incoeffs) { return incoeffs[j]; });
        }
    }
}


void BFormatDec::process(const std::span<FloatBufferLine> OutBuffer,
    const std::span<const FloatBufferLine> InSamples, const size_t SamplesToDo)
{
    ASSUME(SamplesToDo > 0);

    std::visit(overloaded {
        [=,this](std::vector<ChannelDecoderDual> &decoder)
        {
            auto input = InSamples.begin();
            const auto hfSamples = std::span<float>{mSamples[sHFBand]}.first(SamplesToDo);
            const auto lfSamples = std::span<float>{mSamples[sLFBand]}.first(SamplesToDo);
            for(auto &chandec : decoder)
            {
                chandec.mXOver.process(std::span{*input++}.first(SamplesToDo), hfSamples, lfSamples);
                MixSamples(hfSamples, OutBuffer, chandec.mGains[sHFBand], chandec.mGains[sHFBand],0,0);
                MixSamples(lfSamples, OutBuffer, chandec.mGains[sLFBand], chandec.mGains[sLFBand],0,0);
            }
        },
        [=](std::vector<ChannelDecoderSingle> &decoder)
        {
            auto input = InSamples.begin();
            for(auto &chandec : decoder)
            {
                MixSamples(std::span{*input++}.first(SamplesToDo), OutBuffer, chandec.mGains,
                    chandec.mGains, 0, 0);
            }
        },
    }, mChannelDec);
}

void BFormatDec::processStablize(const std::span<FloatBufferLine> OutBuffer,
    const std::span<const FloatBufferLine> InSamples, const size_t lidx, const size_t ridx,
    const size_t cidx, const size_t SamplesToDo)
{
    ASSUME(SamplesToDo > 0);

    /* Move the existing direct L/R signal out so it doesn't get processed by
     * the stablizer.
     */
    const auto leftout = std::span{OutBuffer[lidx]}.first(SamplesToDo);
    const auto rightout = std::span{OutBuffer[ridx]}.first(SamplesToDo);
    const auto mid = std::span{mStablizer->MidDirect}.first(SamplesToDo);
    const auto side = std::span{mStablizer->Side}.first(SamplesToDo);
    std::transform(leftout.begin(), leftout.end(), rightout.begin(), mid.begin(),std::plus{});
    std::transform(leftout.begin(), leftout.end(), rightout.begin(), side.begin(),std::minus{});
    std::fill_n(leftout.begin(), leftout.size(), 0.0f);
    std::fill_n(rightout.begin(), rightout.size(), 0.0f);

    /* Decode the B-Format input to OutBuffer. */
    process(OutBuffer, InSamples, SamplesToDo);

    /* Include the decoded side signal with the direct side signal. */
    for(size_t i{0};i < SamplesToDo;++i)
        side[i] += leftout[i] - rightout[i];

    /* Get the decoded mid signal and band-split it. */
    const auto tmpsamples = std::span{mStablizer->Temp}.first(SamplesToDo);
    std::transform(leftout.begin(), leftout.end(), rightout.begin(), tmpsamples.begin(),
        std::plus{});

    mStablizer->MidFilter.process(tmpsamples, mStablizer->MidHF, mStablizer->MidLF);

    /* Apply an all-pass to all channels to match the band-splitter's phase
     * shift. This is to keep the phase synchronized between the existing
     * signal and the split mid signal.
     */
    const size_t NumChannels{OutBuffer.size()};
    for(size_t i{0u};i < NumChannels;i++)
    {
        /* Skip the left and right channels, which are going to get overwritten,
         * and substitute the direct mid signal and direct+decoded side signal.
         */
        if(i == lidx)
            mStablizer->ChannelFilters[i].processAllPass(mid);
        else if(i == ridx)
            mStablizer->ChannelFilters[i].processAllPass(side);
        else
            mStablizer->ChannelFilters[i].processAllPass({OutBuffer[i].data(), SamplesToDo});
    }

    /* This pans the separate low- and high-frequency signals between being on
     * the center channel and the left+right channels. The low-frequency signal
     * is panned 1/3rd toward center and the high-frequency signal is panned
     * 1/4th toward center. These values can be tweaked.
     */
    const auto cos_lf = std::cos(1.0f/3.0f * (std::numbers::pi_v<float>*0.5f));
    const auto cos_hf = std::cos(1.0f/4.0f * (std::numbers::pi_v<float>*0.5f));
    const auto sin_lf = std::sin(1.0f/3.0f * (std::numbers::pi_v<float>*0.5f));
    const auto sin_hf = std::sin(1.0f/4.0f * (std::numbers::pi_v<float>*0.5f));
    const auto centerout = std::span{OutBuffer[cidx]}.first(SamplesToDo);
    for(size_t i{0};i < SamplesToDo;i++)
    {
        /* Add the direct mid signal to the processed mid signal so it can be
         * properly combined with the direct+decoded side signal.
         */
        const float m{mStablizer->MidLF[i]*cos_lf + mStablizer->MidHF[i]*cos_hf + mid[i]};
        const float c{mStablizer->MidLF[i]*sin_lf + mStablizer->MidHF[i]*sin_hf};
        const float s{side[i]};

        /* The generated center channel signal adds to the existing signal,
         * while the modified left and right channels replace.
         */
        leftout[i] = (m + s) * 0.5f;
        rightout[i] = (m - s) * 0.5f;
        centerout[i] += c * 0.5f;
    }
}


std::unique_ptr<BFormatDec> BFormatDec::Create(const size_t inchans,
    const std::span<const ChannelDec> coeffs, const std::span<const ChannelDec> coeffslf,
    const float xover_f0norm, std::unique_ptr<FrontStablizer> stablizer)
{
    return std::make_unique<BFormatDec>(inchans, coeffs, coeffslf, xover_f0norm,
        std::move(stablizer));
}
