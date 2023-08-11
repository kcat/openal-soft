#ifndef AL_FILTER_H
#define AL_FILTER_H

#include <string_view>
#include <variant>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "almalloc.h"

#define LOWPASSFREQREF  5000.0f
#define HIGHPASSFREQREF  250.0f


template<typename T>
struct FilterTable {
    static void setParami(struct ALfilter*, ALenum, int);
    static void setParamiv(struct ALfilter*, ALenum, const int*);
    static void setParamf(struct ALfilter*, ALenum, float);
    static void setParamfv(struct ALfilter*, ALenum, const float*);

    static void getParami(const struct ALfilter*, ALenum, int*);
    static void getParamiv(const struct ALfilter*, ALenum, int*);
    static void getParamf(const struct ALfilter*, ALenum, float*);
    static void getParamfv(const struct ALfilter*, ALenum, float*);
};

struct NullFilterTable : public FilterTable<NullFilterTable> { };
struct LowpassFilterTable : public FilterTable<LowpassFilterTable> { };
struct HighpassFilterTable : public FilterTable<HighpassFilterTable> { };
struct BandpassFilterTable : public FilterTable<BandpassFilterTable> { };


struct ALfilter {
    ALenum type{AL_FILTER_NULL};

    float Gain{1.0f};
    float GainHF{1.0f};
    float HFReference{LOWPASSFREQREF};
    float GainLF{1.0f};
    float LFReference{HIGHPASSFREQREF};

    using TableTypes = std::variant<NullFilterTable,LowpassFilterTable,HighpassFilterTable,
        BandpassFilterTable>;
    TableTypes mTypeVariant;

    /* Self ID */
    ALuint id{0};

    static void SetName(ALCcontext *context, ALuint id, std::string_view name);

    DISABLE_ALLOC()
};

#endif
