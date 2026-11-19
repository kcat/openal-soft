#ifndef COMMON_UZDL_HPP
#define COMMON_UZDL_HPP

/* Provides user-defined literals for size_t and signed size_t, until we can
 * use C++23's native suffixes.
 */

#include <cstddef>
#include <type_traits>
#include <utility>

#include "opthelper_macros.hpp"

[[nodiscard]] consteval
auto operator ""_z(unsigned long long const n) noexcept
{
    using signed_size_t = std::make_signed_t<std::size_t>;
    if(not std::in_range<signed_size_t>(n)) ASSUME(false);
    return static_cast<signed_size_t>(n);
}

[[nodiscard]] consteval
auto operator ""_uz(unsigned long long const n) noexcept
{
    if(not std::in_range<std::size_t>(n)) ASSUME(false);
    return static_cast<std::size_t>(n);
}

[[nodiscard]] consteval
auto operator ""_zu(unsigned long long const n) noexcept
{
    if(not std::in_range<std::size_t>(n)) ASSUME(false);
    return static_cast<std::size_t>(n);
}

#endif /* COMMON_UZDL_HPP */
