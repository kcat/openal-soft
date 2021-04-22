#ifndef AL_COMPAT_H
#define AL_COMPAT_H

#include <string>

#include "vector.h"


struct PathNamePair { std::string path, fname; };
const PathNamePair &GetProcBinary(void);

extern int RTPrioLevel;
extern bool AllowRTTimeLimit;
void SetRTPriority(void);

al::vector<std::string> SearchDataFiles(const char *match, const char *subdir);

#endif /* AL_COMPAT_H */
