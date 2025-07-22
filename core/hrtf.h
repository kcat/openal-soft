#ifndef CORE_HRTF_H
#define CORE_HRTF_H

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "almalloc.h"
#include "ambidefs.h"
#include "bufferline.h"
#include "flexarray.h"
#include "intrusive_ptr.h"
#include "mixer/hrtfdefs.h"


struct HrtfStore {
    alignas(16) std::atomic<uint> mRef;

    uint mSampleRate : 24;
    uint mIrSize : 8;

    struct Field {
        float distance;
        ubyte evCount;
    };
    /* NOTE: Fields are stored *backwards*. mFields.front() is the farthest
     * field, and mFields.back() is the nearest.
     */
    std::span<const Field> mFields;

    struct Elevation {
        ushort azCount;
        ushort irOffset;
    };
    std::span<Elevation> mElev;
    std::span<const HrirArray> mCoeffs;
    std::span<const ubyte2> mDelays;

    void getCoeffs(float elevation, float azimuth, float distance, float spread,
        const HrirSpan coeffs, const std::span<uint,2> delays) const;

    void inc_ref() noexcept;
    void dec_ref() noexcept;

    auto operator new(size_t) -> void* = delete;
    auto operator new[](size_t) -> void* = delete;
    void operator delete[](void*) noexcept = delete;

    void operator delete(gsl::owner<void*> block, void*) noexcept
    { ::operator delete[](block, std::align_val_t{alignof(HrtfStore)}); }
    void operator delete(gsl::owner<void*> block) noexcept
    { ::operator delete[](block, std::align_val_t{alignof(HrtfStore)}); }
};
using HrtfStorePtr = al::intrusive_ptr<HrtfStore>;

/* Data set limits must be the same as or more flexible than those defined in
 * the makemhr utility.
 */
constexpr inline auto MaxHrirDelay = uint{HrtfHistoryLength} - 1u;

constexpr inline auto HrirDelayFracBits = 2u;
constexpr inline auto HrirDelayFracOne = 1u << HrirDelayFracBits;
constexpr inline auto HrirDelayFracHalf = HrirDelayFracOne >> 1u;

/* The sample rate is stored as a 24-bit integer, so 16MHz is the largest
 * supported.
 */
constexpr inline auto MaxHrtfSampleRate = 0xff'ff'ffu;


struct EvRadians { float value; };
struct AzRadians { float value; };
struct AngularPoint {
    EvRadians Elev;
    AzRadians Azim;
};


class DirectHrtfState {
    explicit DirectHrtfState(size_t numchans) : mChannels{numchans} { }

public:
    std::array<float,BufferLineSize> mTemp{};

    /* HRTF filter state for dry buffer content */
    uint mIrSize{0u};
    al::FlexArray<HrtfChannelState> mChannels;

    /**
     * Produces HRTF filter coefficients for decoding B-Format, given a set of
     * virtual speaker positions, a matching decoding matrix, and per-order
     * high-frequency gains for the decoder. The calculated impulse responses
     * are ordered and scaled according to the matrix input.
     */
    void build(const HrtfStore *Hrtf, const uint irSize, const bool perHrirMin,
        const std::span<const AngularPoint> AmbiPoints,
        const std::span<const std::array<float,MaxAmbiChannels>> AmbiMatrix,
        const float XOverFreq, const std::span<const float,MaxAmbiOrder+1> AmbiOrderHFGain);

    static auto Create(size_t num_chans) -> std::unique_ptr<DirectHrtfState>;

    DEF_FAM_NEWDEL(DirectHrtfState, mChannels)
};


auto EnumerateHrtf(std::optional<std::string> pathopt) -> std::vector<std::string>;
auto GetLoadedHrtf(const std::string_view name, const uint devrate) -> HrtfStorePtr;

#endif /* CORE_HRTF_H */
