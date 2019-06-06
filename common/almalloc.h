#ifndef AL_MALLOC_H
#define AL_MALLOC_H

#include <stddef.h>

#include <memory>
#include <limits>
#include <algorithm>


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
    void operator delete(void *block) noexcept { al_free(block); }            \
    void operator delete(void* /*block*/, void* /*ptr*/) noexcept { }

namespace al {

#define REQUIRES(...) typename std::enable_if<(__VA_ARGS__),int>::type = 0

template<typename T, size_t alignment=alignof(T)>
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
    return static_cast<T*>(__builtin_assume_aligned(ptr, alignment));
#elif defined(_MSC_VER)
    auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    if((ptrval&(alignment-1)) != 0) __assume(0);
    return reinterpret_cast<T*>(ptrval);
#else
    return ptr;
#endif
}

template<typename T>
inline void destroy_at(T *ptr) { ptr->~T(); }

template<typename T>
inline void destroy(T first, const T end)
{
    while(first != end)
    {
        destroy_at(std::addressof(*first));
        ++first;
    }
}

template<typename T, typename N, REQUIRES(std::is_integral<N>::value)>
inline T destroy_n(T first, N count)
{
    if(count != 0)
    {
        do {
            destroy_at(std::addressof(*first));
            ++first;
        } while(--count);
    }
    return first;
}


template<typename T>
inline void uninitialized_default_construct(T first, const T last)
{
    using ValueT = typename std::iterator_traits<T>::value_type;
    T current{first};
    try {
        while(current != last)
        {
            ::new (static_cast<void*>(std::addressof(*current))) ValueT;
            ++current;
        }
    }
    catch(...) {
        destroy(first, current);
        throw;
    }
}

template<typename T, typename N, REQUIRES(std::is_integral<N>::value)>
inline T uninitialized_default_construct_n(T first, N count)
{
    using ValueT = typename std::iterator_traits<T>::value_type;
    T current{first};
    if(count != 0)
    {
        try {
            do {
                ::new (static_cast<void*>(std::addressof(*current))) ValueT;
                ++current;
            } while(--count);
        }
        catch(...) {
            destroy(first, current);
            throw;
        }
    }
    return current;
}


/* std::make_unique was added with C++14, so until we rely on that, make our
 * own version.
 */
template<typename T, typename ...ArgsT>
std::unique_ptr<T> make_unique(ArgsT&&...args)
{ return std::unique_ptr<T>{new T{std::forward<ArgsT>(args)...}}; }


/* A flexible array type. Used either standalone or at the end of a parent
 * struct, with placement new, to have a run-time-sized array that's embedded
 * with its size.
 */
template<typename T, size_t alignment=alignof(T)>
struct FlexArray {
    const size_t mSize;
    alignas(alignment) T mArray[];

    static constexpr size_t Sizeof(size_t count, size_t base=0u) noexcept
    {
        return base +
            std::max<size_t>(offsetof(FlexArray, mArray) + sizeof(T)*count, sizeof(FlexArray));
    }

    FlexArray(size_t size) : mSize{size}
    { uninitialized_default_construct_n(mArray, mSize); }
    ~FlexArray()
    { destroy_n(mArray, mSize); }

    FlexArray(const FlexArray&) = delete;
    FlexArray& operator=(const FlexArray&) = delete;

    size_t size() const noexcept { return mSize; }

    T *data() noexcept { return mArray; }
    const T *data() const noexcept { return mArray; }

    T& operator[](size_t i) noexcept { return mArray[i]; }
    const T& operator[](size_t i) const noexcept { return mArray[i]; }

    T& front() noexcept { return mArray[0]; }
    const T& front() const noexcept { return mArray[0]; }

    T& back() noexcept { return mArray[mSize-1]; }
    const T& back() const noexcept { return mArray[mSize-1]; }

    T *begin() noexcept { return mArray; }
    const T *begin() const noexcept { return mArray; }
    const T *cbegin() const noexcept { return mArray; }
    T *end() noexcept { return mArray + mSize; }
    const T *end() const noexcept { return mArray + mSize; }
    const T *cend() const noexcept { return mArray + mSize; }

    DEF_PLACE_NEWDEL()
};

#undef REQUIRES

} // namespace al

#endif /* AL_MALLOC_H */
