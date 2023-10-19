#ifndef AL_SPAN_H
#define AL_SPAN_H

#include <array>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <type_traits>

#include "almalloc.h"
#include "altraits.h"

namespace al {

constexpr size_t dynamic_extent{static_cast<size_t>(-1)};

template<typename T, size_t E=dynamic_extent>
class span;

namespace detail_ {
    template<typename T>
    struct is_span_ : std::false_type { };
    template<typename T, size_t E>
    struct is_span_<span<T,E>> : std::true_type { };
    template<typename T>
    constexpr bool is_span_v = is_span_<std::remove_cv_t<T>>::value;

    template<typename T>
    struct is_std_array_ : std::false_type { };
    template<typename T, size_t N>
    struct is_std_array_<std::array<T,N>> : std::true_type { };
    template<typename T>
    constexpr bool is_std_array_v = is_std_array_<std::remove_cv_t<T>>::value;

    template<typename T, typename = void>
    constexpr bool has_size_and_data = false;
    template<typename T>
    constexpr bool has_size_and_data<T,
        std::void_t<decltype(std::size(std::declval<T>())),decltype(std::data(std::declval<T>()))>>
        = true;

    template<typename C>
    constexpr bool is_valid_container_type = !is_span_v<C> && !is_std_array_v<C>
        && !std::is_array<C>::value && has_size_and_data<C>;

    template<typename T, typename U>
    constexpr bool is_array_compatible = std::is_convertible<T(*)[],U(*)[]>::value;

    template<typename C, typename T>
    constexpr bool is_valid_container = is_valid_container_type<C>
        && is_array_compatible<std::remove_pointer_t<decltype(std::data(std::declval<C&>()))>,T>;
} // namespace detail_

#define REQUIRES(...) std::enable_if_t<(__VA_ARGS__),bool> = true

template<typename T, size_t E>
class span {
public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
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

    static constexpr size_t extent{E};

    template<bool is0=(extent == 0), REQUIRES(is0)>
    constexpr span() noexcept { }
    template<typename U>
    constexpr explicit span(U iter, index_type) : mData{::al::to_address(iter)} { }
    template<typename U, typename V, REQUIRES(!std::is_convertible<V,size_t>::value)>
    constexpr explicit span(U first, V) : mData{::al::to_address(first)}
    {}

    constexpr span(type_identity_t<element_type> (&arr)[E]) noexcept
        : span{std::data(arr), std::size(arr)}
    { }
    constexpr span(std::array<value_type,E> &arr) noexcept
        : span{std::data(arr), std::size(arr)}
    { }
    template<typename U=T, REQUIRES(std::is_const<U>::value)>
    constexpr span(const std::array<value_type,E> &arr) noexcept
      : span{std::data(arr), std::size(arr)}
    { }

    template<typename U, REQUIRES(detail_::is_valid_container<U, element_type>)>
    constexpr explicit span(U&& cont) : span{std::data(cont), std::size(cont)} { }

    template<typename U, index_type N, REQUIRES(!std::is_same<element_type,U>::value
        && detail_::is_array_compatible<U,element_type> && N == dynamic_extent)>
    constexpr explicit span(const span<U,N> &span_) noexcept
        : span{std::data(span_), std::size(span_)}
    { }
    template<typename U, index_type N, REQUIRES(!std::is_same<element_type,U>::value
        && detail_::is_array_compatible<U,element_type> && N == extent)>
    constexpr span(const span<U,N> &span_) noexcept : span{std::data(span_), std::size(span_)} { }
    constexpr span(const span&) noexcept = default;

    constexpr span& operator=(const span &rhs) noexcept = default;

    constexpr reference front() const { return *mData; }
    constexpr reference back() const { return *(mData+E-1); }
    constexpr reference operator[](index_type idx) const { return mData[idx]; }
    constexpr pointer data() const noexcept { return mData; }

    constexpr index_type size() const noexcept { return E; }
    constexpr index_type size_bytes() const noexcept { return E * sizeof(value_type); }
    constexpr bool empty() const noexcept { return E == 0; }

    constexpr iterator begin() const noexcept { return mData; }
    constexpr iterator end() const noexcept { return mData+E; }
    constexpr const_iterator cbegin() const noexcept { return mData; }
    constexpr const_iterator cend() const noexcept { return mData+E; }

    constexpr reverse_iterator rbegin() const noexcept { return reverse_iterator{end()}; }
    constexpr reverse_iterator rend() const noexcept { return reverse_iterator{begin()}; }
    constexpr const_reverse_iterator crbegin() const noexcept
    { return const_reverse_iterator{cend()}; }
    constexpr const_reverse_iterator crend() const noexcept
    { return const_reverse_iterator{cbegin()}; }

    template<size_t C>
    constexpr span<element_type,C> first() const
    {
        static_assert(E >= C, "New size exceeds original capacity");
        return span<element_type,C>{mData, C};
    }

    template<size_t C>
    constexpr span<element_type,C> last() const
    {
        static_assert(E >= C, "New size exceeds original capacity");
        return span<element_type,C>{mData+(E-C), C};
    }

    template<size_t O, size_t C>
    constexpr auto subspan() const -> std::enable_if_t<C!=dynamic_extent,span<element_type,C>>
    {
        static_assert(E >= O, "Offset exceeds extent");
        static_assert(E-O >= C, "New size exceeds original capacity");
        return span<element_type,C>{mData+O, C};
    }

    template<size_t O, size_t C=dynamic_extent>
    constexpr auto subspan() const -> std::enable_if_t<C==dynamic_extent,span<element_type,E-O>>
    {
        static_assert(E >= O, "Offset exceeds extent");
        return span<element_type,E-O>{mData+O, E-O};
    }

    /* NOTE: Can't declare objects of a specialized template class prior to
     * defining the specialization. As a result, these methods need to be
     * defined later.
     */
    constexpr span<element_type,dynamic_extent> first(size_t count) const;
    constexpr span<element_type,dynamic_extent> last(size_t count) const;
    constexpr span<element_type,dynamic_extent> subspan(size_t offset,
        size_t count=dynamic_extent) const;

private:
    pointer mData{nullptr};
};

template<typename T>
class span<T,dynamic_extent> {
public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
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

    static constexpr size_t extent{dynamic_extent};

    constexpr span() noexcept = default;
    template<typename U>
    constexpr span(U iter, index_type count) : mData{::al::to_address(iter)}, mDataEnd{::al::to_address(iter) + count}
    { }
    template<typename U, typename V, REQUIRES(!std::is_convertible<V,size_t>::value)>
    constexpr span(U first, V last) : span{::al::to_address(first), static_cast<size_t>(last - first)}
    { }

    template<size_t N>
    constexpr span(type_identity_t<element_type> (&arr)[N]) noexcept
        : span{std::data(arr), std::size(arr)}
    { }
    template<size_t N>
    constexpr span(std::array<value_type,N> &arr) noexcept
        : span{std::data(arr), std::size(arr)}
    { }
    template<size_t N, typename U=T, REQUIRES(std::is_const<U>::value)>
    constexpr span(const std::array<value_type,N> &arr) noexcept
      : span{std::data(arr), std::size(arr)}
    { }

    template<typename U, REQUIRES(detail_::is_valid_container<U, element_type>)>
    constexpr span(U&& cont) : span{std::data(cont), std::size(cont)} { }

    template<typename U, size_t N, REQUIRES((!std::is_same<element_type,U>::value || extent != N)
        && detail_::is_array_compatible<U,element_type>)>
    constexpr span(const span<U,N> &span_) noexcept : span{std::data(span_), std::size(span_)} { }
    constexpr span(const span&) noexcept = default;

    constexpr span& operator=(const span &rhs) noexcept = default;

    constexpr reference front() const { return *mData; }
    constexpr reference back() const { return *(mDataEnd-1); }
    constexpr reference operator[](index_type idx) const { return mData[idx]; }
    constexpr pointer data() const noexcept { return mData; }

    constexpr index_type size() const noexcept { return static_cast<index_type>(mDataEnd-mData); }
    constexpr index_type size_bytes() const noexcept
    { return static_cast<index_type>(mDataEnd-mData) * sizeof(value_type); }
    constexpr bool empty() const noexcept { return mData == mDataEnd; }

    constexpr iterator begin() const noexcept { return mData; }
    constexpr iterator end() const noexcept { return mDataEnd; }
    constexpr const_iterator cbegin() const noexcept { return mData; }
    constexpr const_iterator cend() const noexcept { return mDataEnd; }

    constexpr reverse_iterator rbegin() const noexcept { return reverse_iterator{end()}; }
    constexpr reverse_iterator rend() const noexcept { return reverse_iterator{begin()}; }
    constexpr const_reverse_iterator crbegin() const noexcept
    { return const_reverse_iterator{cend()}; }
    constexpr const_reverse_iterator crend() const noexcept
    { return const_reverse_iterator{cbegin()}; }

    template<size_t C>
    constexpr span<element_type,C> first() const
    { return span<element_type,C>{mData, C}; }

    constexpr span first(size_t count) const
    { return (count >= size()) ? *this : span{mData, mData+count}; }

    template<size_t C>
    constexpr span<element_type,C> last() const
    { return span<element_type,C>{mDataEnd-C, C}; }

    constexpr span last(size_t count) const
    { return (count >= size()) ? *this : span{mDataEnd-count, mDataEnd}; }

    template<size_t O, size_t C>
    constexpr auto subspan() const -> std::enable_if_t<C!=dynamic_extent,span<element_type,C>>
    { return span<element_type,C>{mData+O, C}; }

    template<size_t O, size_t C=dynamic_extent>
    constexpr auto subspan() const -> std::enable_if_t<C==dynamic_extent,span<element_type,C>>
    { return span<element_type,C>{mData+O, mDataEnd}; }

    constexpr span subspan(size_t offset, size_t count=dynamic_extent) const
    {
        return (offset > size()) ? span{} :
            (count >= size()-offset) ? span{mData+offset, mDataEnd} :
            span{mData+offset, mData+offset+count};
    }

private:
    pointer mData{nullptr};
    pointer mDataEnd{nullptr};
};

template<typename T, size_t E>
constexpr inline auto span<T,E>::first(size_t count) const -> span<element_type,dynamic_extent>
{
    return (count >= size()) ? span<element_type>{mData, extent} :
        span<element_type>{mData, count};
}

template<typename T, size_t E>
constexpr inline auto span<T,E>::last(size_t count) const -> span<element_type,dynamic_extent>
{
    return (count >= size()) ? span<element_type>{mData, extent} :
        span<element_type>{mData+extent-count, count};
}

template<typename T, size_t E>
constexpr inline auto span<T,E>::subspan(size_t offset, size_t count) const
    -> span<element_type,dynamic_extent>
{
    return (offset > size()) ? span<element_type>{} :
        (count >= size()-offset) ? span<element_type>{mData+offset, mData+extent} :
        span<element_type>{mData+offset, mData+offset+count};
}


template<typename T, typename EndOrSize>
span(T, EndOrSize) -> span<std::remove_reference_t<decltype(*std::declval<T&>())>>;

template<typename T, std::size_t N>
span(T (&)[N]) -> span<T, N>;

template<typename T, std::size_t N>
span(std::array<T, N>&) -> span<T, N>;

template<typename T, std::size_t N>
span(const std::array<T, N>&) -> span<const T, N>;

template<typename C, REQUIRES(detail_::is_valid_container_type<C>)>
span(C&&) -> span<std::remove_pointer_t<decltype(std::data(std::declval<C&>()))>>;

#undef REQUIRES

} // namespace al

#endif /* AL_SPAN_H */
