
#include "config.h"

#include <array>
#include <span>

#include "hrtf_resource.hpp"


#ifndef ALSOFT_EMBED_HRTF_DATA

auto GetResource(int /*name*/) noexcept -> std::span<const char>
{ return {}; }

#else

constexpr auto hrtf_default = std::to_array<char>({
#include "default_hrtf.txt"
});

auto GetHrtfResource(int name) noexcept -> std::span<const char>
{
    if(name == DefaultHrtfResourceID)
        return hrtf_default;
    return {};
}
#endif
