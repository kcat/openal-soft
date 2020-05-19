#ifndef ALC_FRONT_STABLIZER_H
#define ALC_FRONT_STABLIZER_H

#include <array>
#include <memory>

#include "alcmain.h"
#include "almalloc.h"
#include "filters/splitter.h"


struct FrontStablizer {
    static constexpr size_t DelayLength{256u};

    FrontStablizer(size_t numchans) : DelayBuf{numchans} { }

    BandSplitter MidFilter;
    alignas(16) float MidLF[BUFFERSIZE]{};
    alignas(16) float MidHF[BUFFERSIZE]{};
    alignas(16) float Side[BUFFERSIZE]{};

    alignas(16) float TempBuf[BUFFERSIZE + DelayLength]{};

    using DelayLine = std::array<float,DelayLength>;
    al::FlexArray<DelayLine,16> DelayBuf;

    static std::unique_ptr<FrontStablizer> Create(size_t numchans)
    { return std::unique_ptr<FrontStablizer>{new(FamCount(numchans)) FrontStablizer{numchans}}; }

    DEF_FAM_NEWDEL(FrontStablizer, DelayBuf)
};

#endif /* ALC_FRONT_STABLIZER_H */
