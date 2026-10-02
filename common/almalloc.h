#ifndef AL_MALLOC_H
#define AL_MALLOC_H

#include <algorithm>
#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>

#include "gsl/gsl"


#define DISABLE_ALLOC                                                         \
    auto operator new(std::size_t) -> void* = delete;                         \
    auto operator new[](std::size_t) -> void* = delete;                       \
    auto operator delete(void*) noexcept -> void = delete;                    \
    auto operator delete[](void*) noexcept -> void = delete;

namespace al {

template<typename T, std::size_t AlignV=alignof(T)>
struct allocator {
    static constexpr auto Alignment = std::max(AlignV, alignof(T));
    static constexpr auto AlignVal = std::align_val_t{Alignment};

    using value_type = std::remove_cvref_t<T>;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using is_always_equal = std::true_type;

    template<typename U> requires(alignof(U) <= Alignment)
    struct rebind {
        using other = allocator<U, Alignment>;
    };

    constexpr explicit allocator() noexcept = default;
    template<typename U, std::size_t N>
    constexpr explicit allocator(const allocator<U,N>&) noexcept
    { static_assert(Alignment == allocator<U,N>::Alignment); }

    static constexpr auto allocate(std::size_t const n) -> gsl::owner<T*>
    {
        if(n > std::numeric_limits<std::size_t>::max()/sizeof(T)) throw std::bad_alloc();
        return static_cast<gsl::owner<T*>>(::operator new[](n*sizeof(T), AlignVal));
    }
    static constexpr void deallocate(gsl::owner<T*> const p, std::size_t) noexcept
    { ::operator delete[](gsl::owner<void*>{p}, AlignVal); }

    template<typename U, std::size_t M> [[nodiscard]] friend constexpr
    auto operator==(const allocator&, const allocator<U,M>&) noexcept -> bool
    { return Alignment == allocator<U,M>::Alignment; }
};

} // namespace al

#endif /* AL_MALLOC_H */
