#ifndef LOADSOFA_H
#define LOADSOFA_H

#include <string_view>

#include "makemhr.h"


auto LoadSofaFile(std::string_view filename, unsigned numThreads, unsigned fftSize,
    unsigned truncSize, unsigned outRate, ChannelModeT chanMode, HrirDataT *hData) -> bool;

#endif /* LOADSOFA_H */
