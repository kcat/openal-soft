#ifndef ALC_FRONT_STABLIZER_H
#define ALC_FRONT_STABLIZER_H

#include "alcmain.h"
#include "almalloc.h"
#include "devformat.h"
#include "filters/splitter.h"


struct FrontStablizer {
    static constexpr size_t DelayLength{256u};

    BandSplitter MidFilter;
    alignas(16) float MidLF[BUFFERSIZE];
    alignas(16) float MidHF[BUFFERSIZE];
    alignas(16) float Side[BUFFERSIZE];

    alignas(16) float TempBuf[BUFFERSIZE + DelayLength];

    alignas(16) float DelayBuf[MAX_OUTPUT_CHANNELS][DelayLength];

    DEF_NEWDEL(FrontStablizer)
};

#endif /* ALC_FRONT_STABLIZER_H */
