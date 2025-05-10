
#include "config.h"

#include "bformatdec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <numbers>
#include <ranges>
#include <utility>

#include "alnumeric.h"
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
        auto &decoder = mChannelDec.emplace<SBandDecoderVector>(inchans);
        for(auto j = 0_uz;j < decoder.size();++j)
        {
            std::ranges::transform(coeffs, decoder[j].mGains.begin(),
                [j](const ChannelDec &incoeffs) { return incoeffs[j]; });
        }
    }
    else
    {
        using decoder_t = DBandDecoderVector::value_type;
        auto &decoder = mChannelDec.emplace<DBandDecoderVector>(inchans);
        decoder[0].mXOver.init(xover_f0norm);
        std::ranges::fill(decoder|std::views::drop(1) | std::views::transform(&decoder_t::mXOver),
            decoder[0].mXOver);

        for(auto j = 0_uz;j < decoder.size();++j)
        {
            std::ranges::transform(coeffs, decoder[j].mGains[sHFBand].begin(),
                [j](const ChannelDec &incoeffs) { return incoeffs[j]; });

            std::ranges::transform(coeffslf, decoder[j].mGains[sLFBand].begin(),
                [j](const ChannelDec &incoeffs) { return incoeffs[j]; });
        }
    }
}


void BFormatDec::process(const std::span<FloatBufferLine> OutBuffer,
    const std::span<const FloatBufferLine> InSamples, const size_t SamplesToDo)
{
    ASSUME(SamplesToDo > 0);

    std::visit(overloaded {
        [=,this](DBandDecoderVector &decoder)
        {
            using decoder_t = DBandDecoderVector::value_type;
            const auto hfSamples = std::span<float>{mSamples[sHFBand]}.first(SamplesToDo);
            const auto lfSamples = std::span<float>{mSamples[sLFBand]}.first(SamplesToDo);
            std::ignore = std::ranges::mismatch(decoder, InSamples,
                [OutBuffer,SamplesToDo,hfSamples,lfSamples](decoder_t &chandec,
                    FloatConstBufferSpan input)
            {
                chandec.mXOver.process(input.first(SamplesToDo), hfSamples, lfSamples);
                MixSamples(hfSamples, OutBuffer, chandec.mGains[sHFBand], chandec.mGains[sHFBand],
                    0_uz, 0_uz);
                MixSamples(lfSamples, OutBuffer, chandec.mGains[sLFBand], chandec.mGains[sLFBand],
                    0_uz, 0_uz);
                return true;
            });
        },
        [=](SBandDecoderVector &decoder)
        {
            using decoder_t = SBandDecoderVector::value_type;
            std::ignore = std::ranges::mismatch(decoder, InSamples,
                [OutBuffer,SamplesToDo](decoder_t &chandec, FloatConstBufferSpan input)
            {
                MixSamples(input.first(SamplesToDo), OutBuffer, chandec.mGains, chandec.mGains,
                    0_uz, 0_uz);
                return true;
            });
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
    std::ranges::transform(leftout, rightout, mid.begin(), std::plus{});
    std::ranges::transform(leftout, rightout, side.begin(), std::minus{});
    std::ranges::fill(leftout, 0.0f);
    std::ranges::fill(rightout, 0.0f);

    /* Decode the B-Format mix to OutBuffer. */
    process(OutBuffer, InSamples, SamplesToDo);

    /* Include the decoded side signal with the direct side signal. */
    for(auto i = 0_uz;i < SamplesToDo;++i)
        side[i] += leftout[i] - rightout[i];

    /* Get the decoded mid signal and band-split it. */
    const auto tmpsamples = std::span{mStablizer->Temp}.first(SamplesToDo);
    std::ranges::transform(leftout, rightout, tmpsamples.begin(), std::plus{});

    mStablizer->MidFilter.process(tmpsamples, mStablizer->MidHF, mStablizer->MidLF);

    /* Apply an all-pass to all channels to match the band-splitter's phase
     * shift. This is to keep the phase synchronized between the existing
     * signal and the split mid signal.
     */
    const auto NumChannels = OutBuffer.size();
    for(auto i = 0_uz;i < NumChannels;++i)
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
    const auto mid_lf = std::cos(1.0f/3.0f * (std::numbers::pi_v<float>*0.5f));
    const auto mid_hf = std::cos(1.0f/4.0f * (std::numbers::pi_v<float>*0.5f));
    const auto center_lf = std::sin(1.0f/3.0f * (std::numbers::pi_v<float>*0.5f));
    const auto center_hf = std::sin(1.0f/4.0f * (std::numbers::pi_v<float>*0.5f));
    const auto centerout = std::span{OutBuffer[cidx]}.first(SamplesToDo);
    for(auto i = 0_uz;i < SamplesToDo;++i)
    {
        /* Add the direct mid signal to the processed mid signal so it can be
         * properly combined with the direct+decoded side signal.
         */
        const auto m = mStablizer->MidLF[i]*mid_lf + mStablizer->MidHF[i]*mid_hf + mid[i];
        const auto c = mStablizer->MidLF[i]*center_lf + mStablizer->MidHF[i]*center_hf;
        const auto s = side[i];

        /* The generated center channel signal adds to the existing signal,
         * while the modified left and right channels replace.
         */
        leftout[i] = (m + s) * 0.5f;
        rightout[i] = (m - s) * 0.5f;
        centerout[i] += c * 0.5f;
    }
}
