#ifndef AL_MALLOC_H
#define AL_MALLOC_H

#include <stddef.h>

#include <memory>
#include <limits>

/* Minimum alignment required by posix_memalign. */
#define DEF_ALIGN sizeof(void*)

void *al_malloc(size_t alignment, size_t size);
void *al_calloc(size_t alignment, size_t size);
void al_free(void *ptr) noexcept;

size_t al_get_page_size(void) noexcept;

/**
 * Returns non-0 if the allocation function has direct alignment handling.
 * Otherwise, the standard malloc is used with an over-allocation and pointer
 * offset strategy.
 */
int al_is_sane_alignment_allocator(void) noexcept;

#define DEF_NEWDEL(T)                                                         \
    void *operator new(size_t size)                                           \
    {                                                                         \
        void *ret = al_malloc(alignof(T), size);                              \
        if(!ret) throw std::bad_alloc();                                      \
        return ret;                                                           \
    }                                                                         \
    void operator delete(void *block) noexcept { al_free(block); }

#define DEF_PLACE_NEWDEL()                                                    \
    void *operator new(size_t /*size*/, void *ptr) noexcept { return ptr; }   \
    void operator delete(void *block) noexcept { al_free(block); }

namespace al {

template<typename T, size_t alignment=DEF_ALIGN>
struct allocator : public std::allocator<T> {
    using size_type = size_t;
    using pointer = T*;
    using const_pointer = const T*;

    template<typename U>
    struct rebind {
        using other = allocator<U, alignment>;
    };

    pointer allocate(size_type n, const void* = nullptr)
    {
        if(n > std::numeric_limits<size_t>::max() / sizeof(T))
            throw std::bad_alloc();

        void *ret{al_malloc(alignment, n*sizeof(T))};
        if(!ret) throw std::bad_alloc();
        return static_cast<pointer>(ret);
    }

    void deallocate(pointer p, size_type)
    { al_free(p); }

    allocator() : std::allocator<T>() { }
    allocator(const allocator &a) : std::allocator<T>(a) { }
    template<class U>
    allocator(const allocator<U,alignment> &a) : std::allocator<T>(a)
    { }
};

template<size_t alignment, typename T>
inline T* assume_aligned(T *ptr) noexcept
{
    static_assert((alignment & (alignment-1)) == 0, "alignment must be a power of 2");
#ifdef __GNUC__
    return reinterpret_cast<T*>(__builtin_assume_aligned(ptr, alignment));
#elif defined(_MSC_VER)
    auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    if((ptrval&(alignment-1)) != 0) __assume(0);
    return reinterpret_cast<T*>(ptrval);
#else
    return ptr;
#endif
}

} // namespace al

#endif /* AL_MALLOC_H */
