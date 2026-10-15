#ifndef CORE_HELPERS_HPP
#define CORE_HELPERS_HPP

#include <vector>

#include "filesystem.h"


auto GetProcBinary() -> fs::path const&;

/* Mixing thread priority level */
inline auto RTPrioLevel = 1;

/* Allow reducing the process's RTTime limit for RTKit. */
inline auto AllowRTTimeLimit = true;

auto SetRTPriority() -> void;

auto SearchDataFiles(fs::path const& ext) -> std::vector<fs::path>;
auto SearchDataFiles(fs::path const& ext, fs::path const& subdir) -> std::vector<fs::path>;

#endif /* CORE_HELPERS_HPP */
