#ifndef AL_OPTIONAL_H
#define AL_OPTIONAL_H

#include <initializer_list>
#include <type_traits>
#include <utility>

#include "almalloc.h"

namespace al {

struct nullopt_t { };
struct in_place_t { };

constexpr nullopt_t nullopt{};
constexpr in_place_t in_place{};


template<typename T, bool = std::is_trivially_destructible<T>::value>
struct optional_storage;

template<typename T>
struct optional_storage<T, true> {
    bool mHasValue{false};
    union {
        char mDummy;
        T mValue;
    };

    optional_storage() { }
    template<typename ...Args>
    explicit optional_storage(in_place_t, Args&& ...args)
        : mHasValue{true}, mValue{std::forward<Args>(args)...}
    { }
    ~optional_storage() = default;
};

template<typename T>
struct optional_storage<T, false> {
    bool mHasValue{false};
    union {
        char mDummy;
        T mValue;
    };

    optional_storage() { }
    template<typename ...Args>
    explicit optional_storage(in_place_t, Args&& ...args)
        : mHasValue{true}, mValue{std::forward<Args>(args)...}
    { }
    ~optional_storage() { if(mHasValue) al::destroy_at(std::addressof(mValue)); }
};

template<typename T>
class optional {
    using storage_t = optional_storage<T>;

    storage_t mStore;

    template<typename... Args>
    void doConstruct(Args&& ...args)
    {
        ::new(std::addressof(mStore.mValue)) T{std::forward<Args>(args)...};
        mStore.mHasValue = true;
    }

public:
    using value_type = T;

    optional() = default;
    optional(nullopt_t) noexcept { }
    optional(const optional &rhs) { if(rhs) doConstruct(*rhs); }
    optional(optional&& rhs) { if(rhs) doConstruct(std::move(*rhs)); }
    template<typename ...Args>
    explicit optional(in_place_t, Args&& ...args)
        : mStore{al::in_place, std::forward<Args>(args)...}
    { }
    ~optional() = default;

    optional& operator=(nullopt_t) noexcept { reset(); return *this; }
    std::enable_if_t<std::is_copy_constructible<T>::value && std::is_copy_assignable<T>::value,
    optional&> operator=(const optional &rhs)
    {
        if(!rhs)
            reset();
        else if(*this)
            mStore.mValue = *rhs;
        else
            doConstruct(*rhs);
        return *this;
    }
    std::enable_if_t<std::is_move_constructible<T>::value && std::is_move_assignable<T>::value,
    optional&> operator=(optional&& rhs)
    {
        if(!rhs)
            reset();
        else if(*this)
            mStore.mValue = std::move(*rhs);
        else
            doConstruct(std::move(*rhs));
        return *this;
    }
    template<typename U=T>
    std::enable_if_t<std::is_constructible<T, U>::value
        && std::is_assignable<T&, U>::value
        && !std::is_same<std::decay_t<U>, optional<T>>::value
        && (!std::is_same<std::decay_t<U>, T>::value || !std::is_scalar<U>::value),
    optional&> operator=(U&& rhs)
    {
        if(*this)
            mStore.mValue = std::forward<U>(rhs);
        else
            doConstruct(std::forward<U>(rhs));
        return *this;
    }

    const T* operator->() const { return std::addressof(mStore.mValue); }
    T* operator->() { return std::addressof(mStore.mValue); }
    const T& operator*() const& { return this->mValue; }
    T& operator*() & { return mStore.mValue; }
    const T&& operator*() const&& { return std::move(mStore.mValue); }
    T&& operator*() && { return std::move(mStore.mValue); }

    operator bool() const noexcept { return mStore.mHasValue; }
    bool has_value() const noexcept { return mStore.mHasValue; }

    T& value() & { return mStore.mValue; }
    const T& value() const& { return mStore.mValue; }
    T&& value() && { return std::move(mStore.mValue); }
    const T&& value() const&& { return std::move(mStore.mValue); }

    template<typename U>
    T value_or(U&& defval) const&
    { return bool{*this} ? **this : static_cast<T>(std::forward<U>(defval)); }
    template<typename U>
    T value_or(U&& defval) &&
    { return bool{*this} ? std::move(**this) : static_cast<T>(std::forward<U>(defval)); }

    void reset() noexcept
    {
        if(mStore.mHasValue)
            al::destroy_at(std::addressof(mStore.mValue));
        mStore.mHasValue = false;
    }
};

template<typename T>
inline optional<std::decay_t<T>> make_optional(T&& arg)
{ return optional<std::decay_t<T>>{in_place, std::forward<T>(arg)}; }

template<typename T, typename... Args>
inline optional<T> make_optional(Args&& ...args)
{ return optional<T>{in_place, std::forward<Args>(args)...}; }

template<typename T, typename U, typename... Args>
inline optional<T> make_optional(std::initializer_list<U> il, Args&& ...args)
{ return optional<T>{in_place, il, std::forward<Args>(args)...}; }

} // namespace al

#endif /* AL_OPTIONAL_H */
