#ifndef AL_SPAN_H
#define AL_SPAN_H

#include <cassert>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include "almalloc.h"
#include "altraits.h"

namespace al {

inline constexpr std::size_t dynamic_extent{static_cast<std::size_t>(-1)};

template<typename T, std::size_t E=dynamic_extent>
class span;

namespace detail_ {
    template<typename T>
    struct is_span_ : std::false_type { };
    template<typename T, std::size_t E>
    struct is_span_<span<T,E>> : std::true_type { };
    template<typename T>
    inline constexpr bool is_span_v = is_span_<std::remove_cv_t<T>>::value;

    template<typename T>
    struct is_std_array_ : std::false_type { };
    template<typename T, std::size_t N>
    struct is_std_array_<std::array<T,N>> : std::true_type { };
    template<typename T>
    inline constexpr bool is_std_array_v = is_std_array_<std::remove_cv_t<T>>::value;

    template<typename T, typename = void>
    inline constexpr bool has_size_and_data = false;
    template<typename T>
    inline constexpr bool has_size_and_data<T,
        std::void_t<decltype(std::size(std::declval<T>())),decltype(std::data(std::declval<T>()))>>
        = true;

    template<typename C>
    inline constexpr bool is_valid_container_type = !is_span_v<C> && !is_std_array_v<C>
        && !std::is_array<C>::value && has_size_and_data<C>;

    template<typename T, typename U>
    inline constexpr bool is_array_compatible = std::is_convertible<T(*)[],U(*)[]>::value; /* NOLINT(*-avoid-c-arrays) */

    template<typename C, typename T>
    inline constexpr bool is_valid_container = is_valid_container_type<C>
        && is_array_compatible<std::remove_pointer_t<decltype(std::data(std::declval<C&>()))>,T>;
} // namespace detail_

#define REQUIRES(...) std::enable_if_t<(__VA_ARGS__),bool> = true

template<typename T, std::size_t E>
class span {
public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
    using size_type = std::size_t;
    using difference_type = ptrdiff_t;

    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;

    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    static constexpr std::size_t extent{E};

    template<bool is0=(extent == 0), REQUIRES(is0)>
    constexpr span() noexcept { }
    template<typename U>
    constexpr explicit span(U iter, size_type size_ [[maybe_unused]])
        : mData{::al::to_address(iter)}
    { assert(size_ == extent); }
    template<typename U, typename V, REQUIRES(!std::is_convertible<V,std::size_t>::value)>
    constexpr explicit span(U first, V last [[maybe_unused]]) : mData{::al::to_address(first)}
    { assert(static_cast<std::size_t>(last-first) == extent); }

    template<std::size_t N>
    constexpr span(type_identity_t<element_type> (&arr)[N]) noexcept /* NOLINT(*-avoid-c-arrays) */
        : mData{std::data(arr)}
    { static_assert(N == extent); }
    template<std::size_t N>
    constexpr span(std::array<value_type,N> &arr) noexcept : mData{std::data(arr)}
    { static_assert(N == extent); }
    template<typename U=T, std::size_t N, REQUIRES(std::is_const<U>::value)>
    constexpr span(const std::array<value_type,N> &arr) noexcept : mData{std::data(arr)}
    { static_assert(N == extent); }

    template<typename U, REQUIRES(detail_::is_valid_container<U, element_type>)>
    constexpr explicit span(U&& cont) : span{std::data(cont), std::size(cont)} { }

    template<typename U, std::size_t N, REQUIRES(!std::is_same<element_type,U>::value
        && detail_::is_array_compatible<U,element_type> && N == dynamic_extent)>
    constexpr explicit span(const span<U,N> &span_) noexcept : mData{std::data(span_)}
    { assert(std::size(span_) == extent); }
    template<typename U, std::size_t N, REQUIRES(!std::is_same<element_type,U>::value
        && detail_::is_array_compatible<U,element_type> && N == extent)>
    constexpr span(const span<U,N> &span_) noexcept : mData{std::data(span_)} { }
    constexpr span(const span&) noexcept = default;

    constexpr span& operator=(const span &rhs) noexcept = default;

    [[nodiscard]] constexpr auto front() const -> reference { return mData[0]; }
    [[nodiscard]] constexpr auto back() const -> reference { return mData[E-1]; }
    [[nodiscard]] constexpr auto operator[](size_type idx) const -> reference { return mData[idx]; }
    [[nodiscard]] constexpr auto data() const noexcept -> pointer { return mData; }

    [[nodiscard]] constexpr auto size() const noexcept -> size_type { return E; }
    [[nodiscard]] constexpr auto size_bytes() const noexcept -> size_type { return E * sizeof(value_type); }
    [[nodiscard]] constexpr auto empty() const noexcept -> bool { return E == 0; }

    [[nodiscard]] constexpr auto begin() const noexcept -> iterator { return mData; }
    [[nodiscard]] constexpr auto end() const noexcept -> iterator { return mData+E; }
    [[nodiscard]] constexpr auto cbegin() const noexcept -> const_iterator { return mData; }
    [[nodiscard]] constexpr auto cend() const noexcept -> const_iterator { return mData+E; }

    [[nodiscard]] constexpr auto rbegin() const noexcept -> reverse_iterator { return reverse_iterator{end()}; }
    [[nodiscard]] constexpr auto rend() const noexcept -> reverse_iterator { return reverse_iterator{begin()}; }
    [[nodiscard]] constexpr auto crbegin() const noexcept -> const_reverse_iterator
    { return const_reverse_iterator{cend()}; }
    [[nodiscard]] constexpr auto crend() const noexcept -> const_reverse_iterator
    { return const_reverse_iterator{cbegin()}; }

    template<std::size_t C>
    [[nodiscard]] constexpr auto first() const -> span<element_type,C>
    {
        static_assert(E >= C, "New size exceeds original capacity");
        return span<element_type,C>{mData, C};
    }

    template<std::size_t C>
    [[nodiscard]] constexpr auto last() const -> span<element_type,C>
    {
        static_assert(E >= C, "New size exceeds original capacity");
        return span<element_type,C>{mData+(E-C), C};
    }

    template<std::size_t O, std::size_t C>
    [[nodiscard]] constexpr
    auto subspan() const -> std::enable_if_t<C!=dynamic_extent,span<element_type,C>>
    {
        static_assert(E >= O, "Offset exceeds extent");
        static_assert(E-O >= C, "New size exceeds original capacity");
        return span<element_type,C>{mData+O, C};
    }

    template<std::size_t O, std::size_t C=dynamic_extent>
    [[nodiscard]] constexpr
    auto subspan() const -> std::enable_if_t<C==dynamic_extent,span<element_type,E-O>>
    {
        static_assert(E >= O, "Offset exceeds extent");
        return span<element_type,E-O>{mData+O, E-O};
    }

    /* NOTE: Can't declare objects of a specialized template class prior to
     * defining the specialization. As a result, these methods need to be
     * defined later.
     */
    [[nodiscard]] constexpr auto first(std::size_t count) const
        -> span<element_type,dynamic_extent>;
    [[nodiscard]] constexpr auto last(std::size_t count) const
        -> span<element_type,dynamic_extent>;
    [[nodiscard]] constexpr auto subspan(std::size_t offset,
        std::size_t count=dynamic_extent) const -> span<element_type,dynamic_extent>;

private:
    pointer mData{nullptr};
};

template<typename T>
class span<T,dynamic_extent> {
public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
    using size_type = std::size_t;
    using difference_type = ptrdiff_t;

    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;

    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    static constexpr std::size_t extent{dynamic_extent};

    constexpr span() noexcept = default;
    template<typename U>
    constexpr span(U iter, size_type count) : mData{::al::to_address(iter)}, mDataLength{count}
    { }
    template<typename U, typename V, REQUIRES(!std::is_convertible<V,std::size_t>::value)>
    constexpr span(U first, V last)
        : span{::al::to_address(first), static_cast<std::size_t>(last-first)}
    { }

    template<std::size_t N>
    constexpr span(type_identity_t<element_type> (&arr)[N]) noexcept /* NOLINT(*-avoid-c-arrays) */
        : mData{std::data(arr)}, mDataLength{std::size(arr)}
    { }
    template<std::size_t N>
    constexpr span(std::array<value_type,N> &arr) noexcept
        : mData{std::data(arr)}, mDataLength{std::size(arr)}
    { }
    template<std::size_t N, typename U=T, REQUIRES(std::is_const<U>::value)>
    constexpr span(const std::array<value_type,N> &arr) noexcept
        : mData{std::data(arr)}, mDataLength{std::size(arr)}
    { }

    template<typename U, REQUIRES(detail_::is_valid_container<U, element_type>)>
    constexpr span(U&& cont) : span{std::data(cont), std::size(cont)} { }

    template<typename U, std::size_t N, REQUIRES(detail_::is_array_compatible<U,element_type>
        && (!std::is_same<element_type,U>::value || extent != N))>
    constexpr span(const span<U,N> &span_) noexcept : span{std::data(span_), std::size(span_)} { }
    constexpr span(const span&) noexcept = default;

    constexpr span& operator=(const span &rhs) noexcept = default;

    [[nodiscard]] constexpr auto front() const -> reference { return mData[0]; }
    [[nodiscard]] constexpr auto back() const -> reference { return mData[mDataLength-1]; }
    [[nodiscard]] constexpr auto operator[](size_type idx) const -> reference { return mData[idx]; }
    [[nodiscard]] constexpr auto data() const noexcept -> pointer { return mData; }

    [[nodiscard]] constexpr auto size() const noexcept -> size_type { return mDataLength; }
    [[nodiscard]] constexpr auto size_bytes() const noexcept -> size_type { return mDataLength * sizeof(value_type); }
    [[nodiscard]] constexpr auto empty() const noexcept -> bool { return mDataLength == 0; }

    [[nodiscard]] constexpr auto begin() const noexcept -> iterator { return mData; }
    [[nodiscard]] constexpr auto end() const noexcept -> iterator { return mData+mDataLength; }
    [[nodiscard]] constexpr auto cbegin() const noexcept -> const_iterator { return mData; }
    [[nodiscard]] constexpr auto cend() const noexcept -> const_iterator { return mData+mDataLength; }

    [[nodiscard]] constexpr auto rbegin() const noexcept -> reverse_iterator { return reverse_iterator{end()}; }
    [[nodiscard]] constexpr auto rend() const noexcept -> reverse_iterator { return reverse_iterator{begin()}; }
    [[nodiscard]] constexpr auto crbegin() const noexcept -> const_reverse_iterator
    { return const_reverse_iterator{cend()}; }
    [[nodiscard]] constexpr auto crend() const noexcept -> const_reverse_iterator
    { return const_reverse_iterator{cbegin()}; }

    template<std::size_t C>
    [[nodiscard]] constexpr auto first() const -> span<element_type,C>
    {
        if(C > mDataLength)
            throw std::out_of_range{"Subspan count out of range"};
        return span<element_type,C>{mData, C};
    }

    [[nodiscard]] constexpr auto first(std::size_t count) const -> span
    {
        if(count > mDataLength)
            throw std::out_of_range{"Subspan count out of range"};
        return span{mData, count};
    }

    template<std::size_t C>
    [[nodiscard]] constexpr auto last() const -> span<element_type,C>
    {
        if(C > mDataLength)
            throw std::out_of_range{"Subspan count out of range"};
        return span<element_type,C>{mData+mDataLength-C, C};
    }

    [[nodiscard]] constexpr auto last(std::size_t count) const -> span
    {
        if(count > mDataLength)
            throw std::out_of_range{"Subspan count out of range"};
        return span{mData+mDataLength-count, count};
    }

    template<std::size_t O, std::size_t C>
    [[nodiscard]] constexpr
    auto subspan() const -> std::enable_if_t<C!=dynamic_extent,span<element_type,C>>
    {
        if(O > mDataLength)
            throw std::out_of_range{"Subspan offset out of range"};
        if(C > mDataLength-O)
            throw std::out_of_range{"Subspan length out of range"};
        return span<element_type,C>{mData+O, C};
    }

    template<std::size_t O, std::size_t C=dynamic_extent>
    [[nodiscard]] constexpr
    auto subspan() const -> std::enable_if_t<C==dynamic_extent,span<element_type,C>>
    {
        if(O > mDataLength)
            throw std::out_of_range{"Subspan offset out of range"};
        return span<element_type,C>{mData+O, mDataLength-O};
    }

    [[nodiscard]] constexpr
    auto subspan(std::size_t offset, std::size_t count=dynamic_extent) const -> span
    {
        if(offset > mDataLength)
            throw std::out_of_range{"Subspan offset out of range"};
        if(count != dynamic_extent)
        {
            if(count > mDataLength-offset)
                throw std::out_of_range{"Subspan length out of range"};
            return span{mData+offset, count};
        }
        return span{mData+offset, mDataLength-offset};
    }

private:
    pointer mData{nullptr};
    size_type mDataLength{0};
};

template<typename T, std::size_t E>
[[nodiscard]] constexpr
auto span<T,E>::first(std::size_t count) const -> span<element_type,dynamic_extent>
{
    if(count > size())
        throw std::out_of_range{"Subspan count out of range"};
    return span<element_type>{mData, count};
}

template<typename T, std::size_t E>
[[nodiscard]] constexpr
auto span<T,E>::last(std::size_t count) const -> span<element_type,dynamic_extent>
{
    if(count > size())
        throw std::out_of_range{"Subspan count out of range"};
    return span<element_type>{mData+size()-count, count};
}

template<typename T, std::size_t E>
[[nodiscard]] constexpr
auto span<T,E>::subspan(std::size_t offset, std::size_t count) const
    -> span<element_type,dynamic_extent>
{
    if(offset > size())
        throw std::out_of_range{"Subspan offset out of range"};
    if(count != dynamic_extent)
    {
        if(count > size()-offset)
            throw std::out_of_range{"Subspan length out of range"};
        return span<element_type>{mData+offset, count};
    }
    return span<element_type>{mData+offset, size()-offset};
}


template<typename T, typename EndOrSize>
span(T, EndOrSize) -> span<std::remove_reference_t<decltype(*std::declval<T&>())>>;

template<typename T, std::size_t N>
span(T (&)[N]) -> span<T, N>; /* NOLINT(*-avoid-c-arrays) */

template<typename T, std::size_t N>
span(std::array<T, N>&) -> span<T, N>;

template<typename T, std::size_t N>
span(const std::array<T, N>&) -> span<const T, N>;

template<typename C, REQUIRES(detail_::is_valid_container_type<C>)>
span(C&&) -> span<std::remove_pointer_t<decltype(std::data(std::declval<C&>()))>>;

#undef REQUIRES

} // namespace al

#endif /* AL_SPAN_H */
