#ifndef AL_MALLOC_H
#define AL_MALLOC_H

#include <stddef.h>

#include <memory>
#include <limits>
#include <algorithm>


void *al_malloc(size_t alignment, size_t size);
void *al_calloc(size_t alignment, size_t size);
void al_free(void *ptr) noexcept;


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
        al::destroy_at(std::addressof(*first));
        ++first;
    }
}

template<typename T, typename N, REQUIRES(std::is_integral<N>::value)>
inline T destroy_n(T first, N count)
{
    if(count != 0)
    {
        do {
            al::destroy_at(std::addressof(*first));
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
    using element_type = T;
    using value_type = typename std::remove_cv<T>::type;
    using index_type = size_t;
    using difference_type = ptrdiff_t;

    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;

    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;


    const index_type mSize;
    alignas(alignment) element_type mArray[0];

    static constexpr index_type Sizeof(index_type count, index_type base=0u) noexcept
    {
        return base +
            std::max<index_type>(offsetof(FlexArray, mArray) + sizeof(T)*count, sizeof(FlexArray));
    }

    FlexArray(index_type size) : mSize{size}
    { uninitialized_default_construct_n(mArray, mSize); }
    ~FlexArray() { destroy_n(mArray, mSize); }

    FlexArray(const FlexArray&) = delete;
    FlexArray& operator=(const FlexArray&) = delete;

    index_type size() const noexcept { return mSize; }
    bool empty() const noexcept { return mSize == 0; }

    pointer data() noexcept { return mArray; }
    const_pointer data() const noexcept { return mArray; }

    reference operator[](index_type i) noexcept { return mArray[i]; }
    const_reference operator[](index_type i) const noexcept { return mArray[i]; }

    reference front() noexcept { return mArray[0]; }
    const_reference front() const noexcept { return mArray[0]; }

    reference back() noexcept { return mArray[mSize-1]; }
    const_reference back() const noexcept { return mArray[mSize-1]; }

    iterator begin() noexcept { return mArray; }
    const_iterator begin() const noexcept { return mArray; }
    const_iterator cbegin() const noexcept { return mArray; }
    iterator end() noexcept { return mArray + mSize; }
    const_iterator end() const noexcept { return mArray + mSize; }
    const_iterator cend() const noexcept { return mArray + mSize; }

    reverse_iterator rbegin() noexcept { return end(); }
    const_reverse_iterator rbegin() const noexcept { return end(); }
    const_reverse_iterator crbegin() const noexcept { return cend(); }
    reverse_iterator rend() noexcept { return begin(); }
    const_reverse_iterator rend() const noexcept { return begin(); }
    const_reverse_iterator crend() const noexcept { return cbegin(); }

    DEF_PLACE_NEWDEL()
};

#undef REQUIRES

} // namespace al

#endif /* AL_MALLOC_H */
