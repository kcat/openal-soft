
#include "config.h"

#include <span>

#include "hrtf_resource.hpp"


#ifndef ALSOFT_EMBED_HRTF_DATA

auto GetHrtfResource(int const name [[maybe_unused]]) noexcept -> std::span<const char>
{ return {}; }

#else

namespace {

#include "default_hrtf.hpp"

} // namespace

auto GetHrtfResource(int const name) noexcept -> std::span<const char>
{
    if(name == DefaultHrtfResourceID)
        return default_hrtf;
    return {};
}
#endif
