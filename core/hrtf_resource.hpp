#ifndef CORE_HRTF_RESOURCE_HPP
#define CORE_HRTF_RESOURCE_HPP

#include <span>


constexpr inline auto DefaultHrtfResourceID = 1;

auto GetHrtfResource(int name) noexcept -> std::span<const char>;

#endif /* CORE_HRTF_RESOURCE_HPP */
