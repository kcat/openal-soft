#ifndef AL_FILTER_H
#define AL_FILTER_H

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>
#include <variant>

#include "AL/al.h"
#include "AL/efx.h"

#include "almalloc.h"
#include "alnumeric.h"
#include "gsl/gsl"

namespace al {
struct Context;
struct Filter;
} // namespace al


inline constexpr auto LowPassFreqRef = 5000.0f;
inline constexpr auto HighPassFreqRef = 250.0f;

template<typename T>
struct FilterTable {
    static void setParami(gsl::not_null<al::Context*>, gsl::not_null<al::Filter*>, ALenum, ALint);
    static void setParamiv(gsl::not_null<al::Context*>, gsl::not_null<al::Filter*>, ALenum, ALint const*);
    static void setParamf(gsl::not_null<al::Context*>, gsl::not_null<al::Filter*>, ALenum, ALfloat);
    static void setParamfv(gsl::not_null<al::Context*>, gsl::not_null<al::Filter*>, ALenum, ALfloat const*);

    static void getParami(gsl::not_null<al::Context*>, gsl::not_null<al::Filter const*>, ALenum, ALint*);
    static void getParamiv(gsl::not_null<al::Context*>, gsl::not_null<al::Filter const*>, ALenum, ALint*);
    static void getParamf(gsl::not_null<al::Context*>, gsl::not_null<al::Filter const*>, ALenum, ALfloat*);
    static void getParamfv(gsl::not_null<al::Context*>, gsl::not_null<al::Filter const*>, ALenum, ALfloat*);

private:
    FilterTable() = default;
    friend T;
};

struct NullFilterTable : FilterTable<NullFilterTable> { };
struct LowpassFilterTable : FilterTable<LowpassFilterTable> { };
struct HighpassFilterTable : FilterTable<HighpassFilterTable> { };
struct BandpassFilterTable : FilterTable<BandpassFilterTable> { };

namespace al {

struct Filter {
    ALenum mType{AL_FILTER_NULL};

    float mGain{1.0f};
    float mGainHF{1.0f};
    float mHFReference{LowPassFreqRef};
    float mGainLF{1.0f};
    float mLFReference{HighPassFreqRef};

    using TableTypes = std::variant<NullFilterTable,LowpassFilterTable,HighpassFilterTable,
        BandpassFilterTable>;
    TableTypes mTypeVariant;

    /* Self ID */
    ALuint mId{0};

    static void SetName(gsl::not_null<Context*> context, ALuint id, std::string_view name);

    DISABLE_ALLOC
};

} /* namespace al */

struct FilterSubList {
    u64 mFreeMask{~0_u64};
    gsl::owner<std::array<al::Filter,64>*> mFilters{nullptr};

    FilterSubList() noexcept = default;
    FilterSubList(const FilterSubList&) = delete;
    FilterSubList(FilterSubList&& rhs) noexcept : mFreeMask{rhs.mFreeMask}, mFilters{rhs.mFilters}
    { rhs.mFreeMask = ~0_u64; rhs.mFilters = nullptr; }
    ~FilterSubList();

    FilterSubList& operator=(const FilterSubList&) = delete;
    FilterSubList& operator=(FilterSubList&& rhs) noexcept
    { std::swap(mFreeMask, rhs.mFreeMask); std::swap(mFilters, rhs.mFilters); return *this; }
};

#endif
