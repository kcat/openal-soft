#ifndef ALC_FRONT_STABLIZER_H
#define ALC_FRONT_STABLIZER_H

#include "alcmain.h"
#include "almalloc.h"
#include "devformat.h"
#include "filters/splitter.h"


struct FrontStablizer {
    static constexpr size_t DelayLength{256u};

    alignas(16) float DelayBuf[MAX_OUTPUT_CHANNELS][DelayLength];

    BandSplitter LFilter, RFilter;
    alignas(16) float LSplit[2][BUFFERSIZE];
    alignas(16) float RSplit[2][BUFFERSIZE];

    alignas(16) float TempBuf[BUFFERSIZE + DelayLength];

    DEF_NEWDEL(FrontStablizer)
};

#endif /* ALC_FRONT_STABLIZER_H */
