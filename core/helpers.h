#ifndef CORE_HELPERS_H
#define CORE_HELPERS_H

#include <utility>
#include <string>
#include <vector>


struct PathNamePair {
    std::string path, fname;

    PathNamePair() = default;
    template<typename T, typename U>
    PathNamePair(T&& path_, U&& fname_)
        : path{std::forward<T>(path_)}, fname{std::forward<U>(fname_)}
    { }
};
const PathNamePair &GetProcBinary(void);

extern int RTPrioLevel;
extern bool AllowRTTimeLimit;
void SetRTPriority(void);

std::vector<std::string> SearchDataFiles(const char *match, const char *subdir);

#endif /* CORE_HELPERS_H */
