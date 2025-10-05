#ifndef AL_FLEXARRAY_H
#define AL_FLEXARRAY_H

#include <algorithm>
#include <cstddef>
#include <memory>
#include <new>
#include <span>
#include <type_traits>

#include "almalloc.h"


namespace al {

/* Storage for flexible array data. This is trivially destructible if type T is
 * trivially destructible.
 */
template<typename T, size_t alignment, bool = std::is_trivially_destructible_v<T>>
struct alignas(alignment) FlexArrayStorage : std::span<T> {
    /* NOLINTBEGIN(bugprone-sizeof-expression) clang-tidy warns about the
     * sizeof(T) being suspicious when T is a pointer type, which it will be
     * for flexible arrays of pointers.
     */
    static constexpr size_t Sizeof(size_t count, size_t base=0u) noexcept
    { return sizeof(FlexArrayStorage) + sizeof(T)*count + base; }
    /* NOLINTEND(bugprone-sizeof-expression) */

    /* NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic) Flexible
     * arrays store their payloads after the end of the object, which must be
     * the last in the whole parent chain.
     */
    explicit FlexArrayStorage(size_t size) noexcept(std::is_nothrow_constructible_v<T>)
        : std::span<T>{::new(static_cast<void*>(this+1)) T[size], size}
    { }
    /* NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic) */
    ~FlexArrayStorage() = default;

    FlexArrayStorage(const FlexArrayStorage&) = delete;
    FlexArrayStorage& operator=(const FlexArrayStorage&) = delete;
};

template<typename T, size_t alignment>
struct alignas(alignment) FlexArrayStorage<T,alignment,false> : std::span<T> {
    static constexpr size_t Sizeof(size_t count, size_t base=0u) noexcept
    { return sizeof(FlexArrayStorage) + sizeof(T)*count + base; }

    /* NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic) */
    explicit FlexArrayStorage(size_t size) noexcept(std::is_nothrow_constructible_v<T>)
        : std::span<T>{::new(static_cast<void*>(this+1)) T[size], size}
    { }
    /* NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic) */
    ~FlexArrayStorage() { std::destroy(this->begin(), this->end()); }

    FlexArrayStorage(const FlexArrayStorage&) = delete;
    FlexArrayStorage& operator=(const FlexArrayStorage&) = delete;
};

/* A flexible array type. Used either standalone or at the end of a parent
 * struct, to have a run-time-sized array that's embedded with its size. Should
 * be used delicately, ensuring there's no additional data after the FlexArray
 * member.
 */
template<typename T, size_t Align=alignof(T)>
struct FlexArray {
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
    using index_type = size_t;
    using difference_type = ptrdiff_t;

    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;

    static constexpr std::size_t StorageAlign{std::max(alignof(T), Align)};
    using Storage_t_ = FlexArrayStorage<element_type,std::max(alignof(std::span<T>),StorageAlign)>;

    using iterator = Storage_t_::iterator;
    using reverse_iterator = Storage_t_::reverse_iterator;

    const Storage_t_ mStore;

    static constexpr index_type Sizeof(index_type count, index_type base=0u) noexcept
    { return Storage_t_::Sizeof(count, base); }
    static std::unique_ptr<FlexArray> Create(index_type count)
    { return std::unique_ptr<FlexArray>{new(FamCount{count}) FlexArray{count}}; }

    explicit FlexArray(index_type size)
        noexcept(std::is_nothrow_constructible_v<Storage_t_,index_type>)
        : mStore{size}
    { }
    ~FlexArray() = default;

    [[nodiscard]] auto size() const noexcept -> index_type { return mStore.size(); }
    [[nodiscard]] auto empty() const noexcept -> bool { return mStore.empty(); }

    [[nodiscard]] auto data() noexcept -> pointer { return mStore.data(); }
    [[nodiscard]] auto data() const noexcept -> const_pointer { return mStore.data(); }

    [[nodiscard]] auto operator[](index_type i) noexcept -> reference { return mStore[i]; }
    [[nodiscard]] auto operator[](index_type i) const noexcept -> const_reference { return mStore[i]; }

    [[nodiscard]] auto front() noexcept -> reference { return mStore.front(); }
    [[nodiscard]] auto front() const noexcept -> const_reference { return mStore.front(); }

    [[nodiscard]] auto back() noexcept -> reference { return mStore.back(); }
    [[nodiscard]] auto back() const noexcept -> const_reference { return mStore.back(); }

    /* FIXME: We want to act like a container here, rather than a container
     * view. A const FlexArray should return const iterators, but std::span
     * doesn't have those until C++23. So for now, don't bother with them.
     */
#if 0
    [[nodiscard]] auto begin() noexcept -> iterator { return mStore.begin(); }
    [[nodiscard]] auto begin() const noexcept -> const_iterator { return mStore.cbegin(); }
    [[nodiscard]] auto cbegin() const noexcept -> const_iterator { return mStore.cbegin(); }
    [[nodiscard]] auto end() noexcept -> iterator { return mStore.end(); }
    [[nodiscard]] auto end() const noexcept -> const_iterator { return mStore.cend(); }
    [[nodiscard]] auto cend() const noexcept -> const_iterator { return mStore.cend(); }

    [[nodiscard]] auto rbegin() noexcept -> reverse_iterator { return mStore.rbegin(); }
    [[nodiscard]] auto rbegin() const noexcept -> const_reverse_iterator { return mStore.crbegin(); }
    [[nodiscard]] auto crbegin() const noexcept -> const_reverse_iterator { return mStore.crbegin(); }
    [[nodiscard]] auto rend() noexcept -> reverse_iterator { return mStore.rend(); }
    [[nodiscard]] auto rend() const noexcept -> const_reverse_iterator { return mStore.crend(); }
    [[nodiscard]] auto crend() const noexcept -> const_reverse_iterator { return mStore.crend(); }
#else
    [[nodiscard]] auto begin() const noexcept -> iterator { return mStore.begin(); }
    [[nodiscard]] auto end() const noexcept -> iterator { return mStore.end(); }

    [[nodiscard]] auto rbegin() const noexcept -> reverse_iterator { return mStore.rbegin(); }
    [[nodiscard]] auto rend() const noexcept -> reverse_iterator { return mStore.rend(); }
#endif

    gsl::owner<void*> operator new(size_t, FamCount count)
    { return ::operator new[](Sizeof(count), std::align_val_t{alignof(FlexArray)}); }
    void operator delete(gsl::owner<void*> block, FamCount) noexcept
    { ::operator delete[](block, std::align_val_t{alignof(FlexArray)}); }
    void operator delete(gsl::owner<void*> block) noexcept
    { ::operator delete[](block, std::align_val_t{alignof(FlexArray)}); }

    void *operator new(size_t size) = delete;
    void *operator new[](size_t size) = delete;
    void operator delete[](void *block) = delete;
};

} // namespace al

#endif /* AL_FLEXARRAY_H */
