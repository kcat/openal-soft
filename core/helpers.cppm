module;

#include <string>
#include <vector>

export module core.helpers;

import filesystem;

export extern "C++" {

struct PathNamePair {
    std::string path, fname;
};
auto GetProcBinary() -> const PathNamePair&;

/* Mixing thread priority level */
inline auto RTPrioLevel = 1;

/* Allow reducing the process's RTTime limit for RTKit. */
inline auto AllowRTTimeLimit = true;

auto SetRTPriority() -> void;

auto SearchDataFiles(fs::path const& ext) -> std::vector<fs::path>;
auto SearchDataFiles(fs::path const& ext, fs::path const& subdir) -> std::vector<fs::path>;

}
