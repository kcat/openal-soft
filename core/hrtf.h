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
    alignas(16) std::atomic<u32> mRef;

    u32 mSampleRate : 24;
    u32 mIrSize : 8;

    struct Field {
        f32 distance;
        u8 evCount;
    };
    /* NOTE: Fields are stored *backwards*. mFields.front() is the farthest
     * field, and mFields.back() is the nearest.
     */
    std::span<Field const> mFields;

    struct Elevation {
        u16 azCount;
        u16 irOffset;
    };
    std::span<Elevation> mElev;
    std::span<HrirArray const> mCoeffs;
    std::span<u8x2 const> mDelays;

    void getCoeffs(f32 elevation, f32 azimuth, f32 distance, f32 spread,
        HrirSpan coeffs, std::span<u32, 2> delays) const;

    void inc_ref() noexcept;
    void dec_ref() noexcept;

    auto operator new(usize) -> void* = delete;
    auto operator new[](usize) -> void* = delete;
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
constexpr inline auto MaxHrirDelay = u32{HrtfHistoryLength - 1};

constexpr inline auto HrirDelayFracBits = 2_u32;
constexpr inline auto HrirDelayFracOne = 1_u32 << HrirDelayFracBits;
constexpr inline auto HrirDelayFracHalf = HrirDelayFracOne >> 1_u32;

/* The sample rate is stored as a 24-bit integer, so 16MHz is the largest
 * supported.
 */
constexpr inline auto MaxHrtfSampleRate = 0xff'ff'ff_u32;


struct EvRadians { f32 value; };
struct AzRadians { f32 value; };
struct AngularPoint {
    EvRadians Elev;
    AzRadians Azim;
};


class DirectHrtfState {
    explicit DirectHrtfState(usize const numchans) : mChannels{numchans} { }

public:
    std::array<f32, BufferLineSize> mTemp{};

    /* HRTF filter state for dry buffer content */
    u32 mIrSize{0};
    al::FlexArray<HrtfChannelState> mChannels;

    /**
     * Produces HRTF filter coefficients for decoding B-Format, given a set of
     * virtual speaker positions, a matching decoding matrix, and per-order
     * high-frequency gains for the decoder. The calculated impulse responses
     * are ordered and scaled according to the matrix input.
     */
    void build(HrtfStore const *Hrtf, u32 irSize, bool perHrirMin,
        std::span<AngularPoint const> AmbiPoints,
        std::span<std::array<f32, MaxAmbiChannels> const> AmbiMatrix, f32 XOverFreq,
        std::span<f32 const, MaxAmbiOrder+1> AmbiOrderHFGain);

    static auto Create(usize num_chans) -> std::unique_ptr<DirectHrtfState>;

    DEF_FAM_NEWDEL(DirectHrtfState, mChannels)
};


auto EnumerateHrtf(std::optional<std::string> pathopt) -> std::vector<std::string>;
auto GetLoadedHrtf(std::string_view name, u32 devrate) -> HrtfStorePtr;

#endif /* CORE_HRTF_H */
