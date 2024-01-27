#ifndef CORE_HELPERS_H
#define CORE_HELPERS_H

#include <string>
#include <string_view>
#include <vector>


struct PathNamePair {
    std::string path, fname;
};
const PathNamePair &GetProcBinary();

/* Mixing thread priority level */
inline int RTPrioLevel{1};

/* Allow reducing the process's RTTime limit for RTKit. */
inline bool AllowRTTimeLimit{true};

void SetRTPriority();

std::vector<std::string> SearchDataFiles(const std::string_view ext, const std::string_view subdir);

#endif /* CORE_HELPERS_H */
