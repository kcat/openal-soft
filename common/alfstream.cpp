
#include "config.h"

#include "alfstream.h"

#include "strutils.h"

#ifdef _WIN32

namespace al {

ifstream::ifstream(const char *filename, std::ios_base::openmode mode)
  : std::ifstream{utf8_to_wstr(filename).c_str(), mode}
{ }

void ifstream::open(const char *filename, std::ios_base::openmode mode)
{
    std::wstring wstr{utf8_to_wstr(filename)};
    std::ifstream::open(wstr.c_str(), mode);
}

ifstream::~ifstream() = default;

} // namespace al

#endif
