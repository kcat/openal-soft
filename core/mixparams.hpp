#ifndef CORE_MIXPARAMS_HPP
#define CORE_MIXPARAMS_HPP

#include <array>
#include <concepts>
#include <cstddef>
#include <ranges>
#include <span>

#include "altypes.hpp"
#include "ambidefs.h"
#include "bufferline.h"
#include "devformat.h"


inline constexpr auto InvalidChannelIndex = ~0_u8;

struct BFChannelConfig {
    float Scale;
    unsigned Index;
};

struct MixParams {
    /* Coefficient channel mapping for mixing to the buffer. */
    std::array<BFChannelConfig, MaxAmbiChannels> AmbiMap{};

    std::span<FloatBufferLine> Buffer;

    /**
     * Helper to set an identity/pass-through panning for ambisonic mixing. The
     * source is expected to be a 3D ACN/N3D ambisonic buffer, and for each
     * channel [0...count), the given functor is called with the source channel
     * index, destination channel index, and the gain for that channel. If the
     * destination channel is InvalidChannelIndex, the given source channel is
     * not used for output.
     */
    template<std::invocable<std::size_t, u8, float> F>
    void setAmbiMixParams(MixParams const &inmix, float const gainbase, F func) const
    {
        auto const numIn = inmix.Buffer.size();
        auto const numOut = Buffer.size();
        for(auto const i : std::views::iota(0_uz, numIn))
        {
            auto idx = InvalidChannelIndex;
            auto gain = 0.0f;

            for(auto const j : std::views::iota(0_uz, numOut))
            {
                if(AmbiMap[j].Index == inmix.AmbiMap[i].Index)
                {
                    idx = u8{static_cast<u8::value_t>(j)};
                    gain = AmbiMap[j].Scale * gainbase;
                    break;
                }
            }
            std::invoke(func, i, idx, gain);
        }
    }
};


struct InputRemixMap {
    struct TargetMix { Channel channel; float mix; };

    Channel channel;
    std::span<TargetMix const> targets;
};


struct RealMixParams {
    std::span<InputRemixMap const> RemixMap;
    std::array<u8, MaxChannels> ChannelIndex{};

    std::span<FloatBufferLine> Buffer;
};

#endif /* CORE_MIXPARAMS_HPP */
