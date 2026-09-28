#ifndef CORE_HELPERS_H
#define CORE_HELPERS_H

#include <string>
#include <string_view>
#include <vector>


struct PathNamePair {
    std::string path, fname;
};
auto GetProcBinary() -> const PathNamePair&;

/* Mixing thread priority level */
inline auto RTPrioLevel = 1;

/* Allow reducing the process's RTTime limit for RTKit. */
inline auto AllowRTTimeLimit = true;

auto SetRTPriority() -> void;

auto SearchDataFiles(std::string_view ext) -> std::vector<std::string>;
auto SearchDataFiles(std::string_view ext, std::string_view subdir) -> std::vector<std::string>;

#endif /* CORE_HELPERS_H */
