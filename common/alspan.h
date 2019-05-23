#ifndef AL_SPAN_H
#define AL_SPAN_H

#include <cstddef>

#include <array>
#include <type_traits>
#include <initializer_list>

namespace al {

template<typename T>
constexpr auto size(T &cont) -> decltype(cont.size())
{ return cont.size(); }

template<typename T>
constexpr auto size(const T &cont) -> decltype(cont.size())
{ return cont.size(); }

template<typename T, size_t N>
constexpr size_t size(T (&)[N]) noexcept
{ return N; }

template<typename T>
constexpr size_t size(std::initializer_list<T> list) noexcept
{ return list.size(); }


template<typename T>
constexpr auto data(T &cont) -> decltype(cont.data())
{ return cont.data(); }

template<typename T>
constexpr auto data(const T &cont) -> decltype(cont.data())
{ return cont.data(); }

template<typename T, size_t N>
constexpr T* data(T (&arr)[N]) noexcept
{ return arr; }

template<typename T>
constexpr const T* data(std::initializer_list<T> list) noexcept
{ return list.begin(); }


template<typename T>
class span {
public:
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

    constexpr span() noexcept = default;
    constexpr span(pointer ptr, index_type count) : mData{ptr}, mCount{count} { }
    constexpr span(pointer first, pointer last) : mData{first}, mCount{std::distance(first, last)} { }
    template<size_t N>
    constexpr span(element_type (&arr)[N]) noexcept : span{al::data(arr), al::size(arr)} { }
    template<size_t N>
    constexpr span(std::array<value_type,N> &arr) noexcept : span{al::data(arr), al::size(arr)} { }
    template<size_t N>
    constexpr span(const std::array<value_type,N> &arr) noexcept : span{al::data(arr), al::size(arr)} { }
    template<typename U>
    constexpr span(U &cont) : span{al::data(cont), al::size(cont)} { }
    template<typename U>
    constexpr span(const U &cont) : span{al::data(cont), al::size(cont)} { }
    constexpr span(const span&) noexcept = default;

    span& operator=(const span &rhs) noexcept = default;

    constexpr reference front() const { return mData[0]; }
    constexpr reference back() const { return mData[mCount-1]; }
    constexpr reference operator[](index_type idx) const { return mData[idx]; }
    constexpr pointer data() const noexcept { return mData; }

    constexpr index_type size() const noexcept { return mCount; }
    constexpr index_type size_bytes() const noexcept { return mCount * sizeof(value_type); }
    constexpr bool empty() const noexcept { return mCount == 0; }

    constexpr iterator begin() const noexcept { return mData; }
    constexpr iterator end() const noexcept { return mData + mCount; }
    constexpr const_iterator cbegin() const noexcept { return mData; }
    constexpr const_iterator cend() const noexcept { return mData + mCount; }

    constexpr reverse_iterator rbegin() const noexcept { return end(); }
    constexpr reverse_iterator rend() const noexcept { return begin(); }
    constexpr const_reverse_iterator crbegin() const noexcept { return cend(); }
    constexpr const_reverse_iterator crend() const noexcept { return cbegin(); }

private:
    pointer mData{nullptr};
    index_type mCount{0u};
};

} // namespace al

#endif /* AL_SPAN_H */
