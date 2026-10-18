#ifndef CORE_HRTF_H
#define CORE_HRTF_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ambidefs.h"
#include "bufferline.h"
#include "flexarray.h"
#include "intrusive_ptr.h"
#include "mixer/hrtfdefs.h"

struct HrtfStore;


struct EvRadians { float value; };
struct AzRadians { float value; };
struct AngularPoint {
    EvRadians Elev;
    AzRadians Azim;
};


class DirectHrtfState {
    explicit DirectHrtfState(std::size_t const numchans) : mChannels{numchans} { }

public:
    std::array<float, BufferLineSize> mTemp{};

    /* HRTF filter state for dry buffer content */
    unsigned mIrSize{0};
    al::FlexArray<HrtfChannelState> mChannels;

    /**
     * Produces HRTF filter coefficients for decoding B-Format, given a set of
     * virtual speaker positions, a matching decoding matrix, and per-order
     * high-frequency gains for the decoder. The calculated impulse responses
     * are ordered and scaled according to the matrix input.
     */
    void build(HrtfStore const *Hrtf, unsigned irSize, bool perHrirMin,
        std::span<AngularPoint const> AmbiPoints,
        std::span<std::array<float, MaxAmbiChannels> const> AmbiMatrix, float XOverFreq,
        std::span<float const, MaxAmbiOrder+1> AmbiOrderHFGain);

    static auto Create(std::size_t num_chans) -> std::unique_ptr<DirectHrtfState>;

    DEF_FAM_NEWDEL(DirectHrtfState, mChannels)
};


auto EnumerateHrtf(std::optional<std::string> const &pathopt) -> std::vector<std::string>;
auto GetLoadedHrtf(std::string_view name, unsigned devrate) -> al::intrusive_ptr<HrtfStore>;

#endif /* CORE_HRTF_H */
