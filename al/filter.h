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
} // namespace al
struct ALfilter;


inline constexpr float LowPassFreqRef{5000.0f};
inline constexpr float HighPassFreqRef{250.0f};

template<typename T>
struct FilterTable {
    static void setParami(gsl::not_null<al::Context*>, gsl::not_null<ALfilter*>, ALenum, int);
    static void setParamiv(gsl::not_null<al::Context*>, gsl::not_null<ALfilter*>, ALenum, const int*);
    static void setParamf(gsl::not_null<al::Context*>, gsl::not_null<ALfilter*>, ALenum, float);
    static void setParamfv(gsl::not_null<al::Context*>, gsl::not_null<ALfilter*>, ALenum, const float*);

    static void getParami(gsl::not_null<al::Context*>, gsl::not_null<const ALfilter*>, ALenum, int*);
    static void getParamiv(gsl::not_null<al::Context*>, gsl::not_null<const ALfilter*>, ALenum, int*);
    static void getParamf(gsl::not_null<al::Context*>, gsl::not_null<const ALfilter*>, ALenum, float*);
    static void getParamfv(gsl::not_null<al::Context*>, gsl::not_null<const ALfilter*>, ALenum, float*);

private:
    FilterTable() = default;
    friend T;
};

struct NullFilterTable : public FilterTable<NullFilterTable> { };
struct LowpassFilterTable : public FilterTable<LowpassFilterTable> { };
struct HighpassFilterTable : public FilterTable<HighpassFilterTable> { };
struct BandpassFilterTable : public FilterTable<BandpassFilterTable> { };


struct ALfilter {
    ALenum type{AL_FILTER_NULL};

    float Gain{1.0f};
    float GainHF{1.0f};
    float HFReference{LowPassFreqRef};
    float GainLF{1.0f};
    float LFReference{HighPassFreqRef};

    using TableTypes = std::variant<NullFilterTable,LowpassFilterTable,HighpassFilterTable,
        BandpassFilterTable>;
    TableTypes mTypeVariant;

    /* Self ID */
    ALuint id{0};

    static void SetName(gsl::not_null<al::Context*> context, ALuint id, std::string_view name);

    DISABLE_ALLOC
};

struct FilterSubList {
    uint64_t FreeMask{~0_u64};
    gsl::owner<std::array<ALfilter,64>*> Filters{nullptr};

    FilterSubList() noexcept = default;
    FilterSubList(const FilterSubList&) = delete;
    FilterSubList(FilterSubList&& rhs) noexcept : FreeMask{rhs.FreeMask}, Filters{rhs.Filters}
    { rhs.FreeMask = ~0_u64; rhs.Filters = nullptr; }
    ~FilterSubList();

    FilterSubList& operator=(const FilterSubList&) = delete;
    FilterSubList& operator=(FilterSubList&& rhs) noexcept
    { std::swap(FreeMask, rhs.FreeMask); std::swap(Filters, rhs.Filters); return *this; }
};

#endif
