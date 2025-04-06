#ifndef LOADDEF_H
#define LOADDEF_H

#include <istream>
#include <span>
#include <string_view>

#include "makemhr.h"


bool LoadDefInput(std::istream &istream, const std::span<const char> startbytes,
    const std::string_view filename, const uint fftSize, const uint truncSize, const uint outRate,
    const ChannelModeT chanMode, HrirDataT *hData);

#endif /* LOADDEF_H */
