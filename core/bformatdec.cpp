
#include "config.h"

#include "bformatdec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>

#include "alnumeric.h"
#include "bufferline.h"
#include "filters/splitter.h"
#include "mixer.h"
#include "opthelpers.h"


namespace {

template<typename... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

} // namespace

BFormatDec::BFormatDec(const size_t inchans, const std::span<const ChannelDec> coeffs,
    const std::span<const ChannelDec> coeffslf, const float xover_f0norm)
{
    if(coeffslf.empty())
    {
        auto &decoder = mChannelDec.emplace<SBandDecoderVector>(inchans);
        for(const auto j : std::views::iota(0_uz, decoder.size()))
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

        for(const auto j : std::views::iota(0_uz, decoder.size()))
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
