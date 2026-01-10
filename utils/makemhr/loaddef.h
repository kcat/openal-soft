#ifndef LOADDEF_H
#define LOADDEF_H

#include <istream>
#include <span>
#include <string_view>

#include "makemhr.h"


auto LoadDefInput(std::istream &istream, std::span<char const> startbytes,
    std::string_view filename, unsigned fftSize, unsigned truncSize, unsigned outRate,
    ChannelModeT chanMode, HrirDataT *hData) -> bool;

#endif /* LOADDEF_H */
