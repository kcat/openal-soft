#ifndef CORE_HRTF_LOADER_HPP
#define CORE_HRTF_LOADER_HPP

#include <istream>
#include <memory>

struct HrtfStore;


auto LoadHrtf(std::istream &stream) -> std::unique_ptr<HrtfStore>;

#endif /* CORE_HRTF_LOADER_HPP */
