#ifndef CORE_HELPERS_H
#define CORE_HELPERS_H

#include <utility>
#include <string>
#include <vector>


struct PathNamePair {
    std::string path, fname;
};
const PathNamePair &GetProcBinary();

extern int RTPrioLevel;
extern bool AllowRTTimeLimit;
void SetRTPriority();

std::vector<std::string> SearchDataFiles(const char *match, const char *subdir);

#endif /* CORE_HELPERS_H */
