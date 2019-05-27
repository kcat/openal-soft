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
    constexpr span(pointer ptr, index_type count) : mData{ptr}, mDataEnd{ptr+count} { }
    constexpr span(pointer first, pointer last) : mData{first}, mDataEnd{last} { }
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

    constexpr reference front() const { return *mData; }
    constexpr reference back() const { return *(mDataEnd-1); }
    constexpr reference operator[](index_type idx) const { return mData[idx]; }
    constexpr pointer data() const noexcept { return mData; }

    constexpr index_type size() const noexcept { return mDataEnd-mData; }
    constexpr index_type size_bytes() const noexcept { return (mDataEnd-mData) * sizeof(value_type); }
    constexpr bool empty() const noexcept { return mData == mDataEnd; }

    constexpr iterator begin() const noexcept { return mData; }
    constexpr iterator end() const noexcept { return mDataEnd; }
    constexpr const_iterator cbegin() const noexcept { return mData; }
    constexpr const_iterator cend() const noexcept { return mDataEnd; }

    constexpr reverse_iterator rbegin() const noexcept { return end(); }
    constexpr reverse_iterator rend() const noexcept { return begin(); }
    constexpr const_reverse_iterator crbegin() const noexcept { return cend(); }
    constexpr const_reverse_iterator crend() const noexcept { return cbegin(); }

    constexpr span first(size_t count) const
    { return (count >= size()) ? *this : span{mData, mData+count}; }
    constexpr span last(size_t count) const
    { return (count >= size()) ? *this : span{mDataEnd-count, mDataEnd}; }
    constexpr span subspan(size_t offset, size_t count=static_cast<size_t>(-1)) const
    {
        return (offset >= size()) ? span{} :
            (count >= size()-offset) ? span{mData+offset, mDataEnd} :
            span{mData+offset, mData+offset+count};
    }

private:
    pointer mData{nullptr};
    pointer mDataEnd{nullptr};
};

} // namespace al

#endif /* AL_SPAN_H */
