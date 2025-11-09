
#include "almalloc.h"

#if defined(__APPLE__)
#include <AvailabilityMacros.h>

#if defined(MAC_OS_X_VERSION_MIN_REQUIRED) && MAC_OS_X_VERSION_MIN_REQUIRED < 101300

#include <stdlib.h>

auto operator new(std::size_t const len, std::align_val_t const align, std::nothrow_t const&)
    noexcept -> gsl::owner<void*>
{
    gsl::owner<void*> mem;
    if(posix_memalign(&mem, static_cast<std::size_t>(align), len))
        return nullptr;
    return mem;
}
auto operator new[](std::size_t const len, std::align_val_t const align, std::nothrow_t const &tag)
    noexcept -> gsl::owner<void*>
{ return operator new(len, align, tag); }

auto operator new(std::size_t const len, std::align_val_t const align) -> gsl::owner<void*>
{
    gsl::owner<void*> mem;
    if(posix_memalign(&mem, static_cast<std::size_t>(align), len))
        throw std::bad_alloc();
    return mem;
}
auto operator new[](std::size_t const len, std::align_val_t const align) -> gsl::owner<void*>
{ return operator new(len, align); }

auto operator delete(void *const ptr, std::align_val_t) noexcept -> void
{ free(ptr); }
auto operator delete[](void *const ptr, std::align_val_t) noexcept -> void
{ free(ptr); }

auto operator delete(void *const ptr, std::size_t, std::align_val_t) noexcept -> void
{ free(ptr); }
auto operator delete[](void *const ptr, std::size_t, std::align_val_t) noexcept -> void
{ free(ptr); }

auto operator delete(void *const ptr, std::align_val_t, std::nothrow_t const&) noexcept -> void
{ free(ptr); }
auto operator delete[](void *const ptr, std::align_val_t, std::nothrow_t const&) noexcept -> void
{ free(ptr); }

#endif
#endif
