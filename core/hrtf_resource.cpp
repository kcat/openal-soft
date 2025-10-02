
#include "config.h"

#include <span>

#include "hrtf_resource.hpp"


#ifndef ALSOFT_EMBED_HRTF_DATA

auto GetResource(int name [[maybe_unused]]) noexcept -> std::span<const char>
{ return {}; }

#else

namespace {

/* NOLINTNEXTLINE(*-avoid-c-arrays) */
constexpr char hrtf_default[] = {
#include "default_hrtf.txt"
};

} // namespace

auto GetHrtfResource(int name) noexcept -> std::span<const char>
{
    if(name == DefaultHrtfResourceID)
        return hrtf_default;
    return {};
}
#endif
