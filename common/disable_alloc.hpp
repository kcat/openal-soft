#ifndef DISABLE_ALLOC_HPP
#define DISABLE_ALLOC_HPP

#include <cstddef>

#define DISABLE_ALLOC                                                         \
    auto operator new(std::size_t) -> void* = delete;                         \
    auto operator new[](std::size_t) -> void* = delete;                       \
    auto operator delete(void*) noexcept -> void = delete;                    \
    auto operator delete[](void*) noexcept -> void = delete;

#endif /* DISABLE_ALLOC_HPP */
