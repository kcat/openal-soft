#ifndef CORE_HRTFSTORE_HPP
#define CORE_HRTFSTORE_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>

#include "filesystem.h"
#include "gsl/pointers"
#include "intrusive_ptr.h"
#include "mixer/hrtfdefs.h"
#include "opthelpers.h"


constexpr inline auto MaxHrirDelay = unsigned{HrtfHistoryLength - 1};

constexpr inline auto HrirDelayFracBits = 2u;
constexpr inline auto HrirDelayFracOne = 1u << HrirDelayFracBits;
constexpr inline auto HrirDelayFracHalf = HrirDelayFracOne >> 1u;

constexpr inline auto MaxHrtfSampleRate = 0xff'ff'ffu;


struct HrtfStore {
    alignas(16) std::atomic<unsigned> mRef;
    fs::path mFilename;

    unsigned mSampleRate : 24;
    unsigned mIrSize : 8;

    struct Field {
        float distance;
        std::uint8_t evCount;
    };
    /* NOTE: Fields are stored *backwards*. mFields.front() is the farthest
     * field, and mFields.back() is the nearest.
     */
    std::span<Field const> mFields;

    struct Elevation {
        std::uint16_t azCount;
        std::uint16_t irOffset;
    };
    std::span<Elevation> mElev;
    std::span<HrirArray const> mCoeffs;
    std::span<u8x2 const> mDelays;

    void getCoeffs(float elevation, float azimuth, float distance, float spread,
        HrirSpan coeffs, std::span<unsigned, 2> delays) const noexcept NONBLOCKING;

    void inc_ref() noexcept;
    void dec_ref() noexcept;

    auto operator new(std::size_t) -> void* = delete;
    auto operator new[](std::size_t) -> void* = delete;
    void operator delete[](void*) noexcept = delete;

    void operator delete(gsl::owner<void*> const block, void*) noexcept
    { ::operator delete[](block, std::align_val_t{alignof(HrtfStore)}); }
    void operator delete(gsl::owner<void*> const block) noexcept
    { ::operator delete[](block, std::align_val_t{alignof(HrtfStore)}); }
};
using HrtfStorePtr = al::intrusive_ptr<HrtfStore>;

#endif /* CORE_HRTFSTORE_HPP */
