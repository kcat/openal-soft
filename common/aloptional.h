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

#define NOEXCEPT_AS(...)  noexcept(noexcept(__VA_ARGS__))

/* Base storage struct for an optional. Defines a trivial destructor, for types
 * that can be trivially destructed.
 */
template<typename T, bool = std::is_trivially_destructible<T>::value>
struct optstore_base {
    bool mHasValue{false};
    union {
        char mDummy;
        T mValue;
    };

    optstore_base() noexcept { }
    template<typename ...Args>
    explicit optstore_base(in_place_t, Args&& ...args)
        noexcept(std::is_nothrow_constructible<T, Args...>::value)
        : mHasValue{true}, mValue{std::forward<Args>(args)...}
    { }
    ~optstore_base() = default;
};

/* Specialization needing a non-trivial destructor. */
template<typename T>
struct optstore_base<T, false> {
    bool mHasValue{false};
    union {
        char mDummy;
        T mValue;
    };

    optstore_base() noexcept { }
    template<typename ...Args>
    explicit optstore_base(in_place_t, Args&& ...args)
        noexcept(std::is_nothrow_constructible<T, Args...>::value)
        : mHasValue{true}, mValue{std::forward<Args>(args)...}
    { }
    ~optstore_base() { if(mHasValue) al::destroy_at(std::addressof(mValue)); }
};

/* Next level of storage, which defines helpers to construct and destruct the
 * stored object.
 */
template<typename T>
struct optstore_helper : public optstore_base<T> {
    using optstore_base<T>::optstore_base;

    template<typename... Args>
    void construct(Args&& ...args) noexcept(std::is_nothrow_constructible<T, Args...>::value)
    {
        al::construct_at(std::addressof(this->mValue), std::forward<Args>(args)...);
        this->mHasValue = true;
    }

    void reset() noexcept
    {
        if(this->mHasValue)
            al::destroy_at(std::addressof(this->mValue));
        this->mHasValue = false;
    }

    void assign(const optstore_helper &rhs)
        noexcept(std::is_nothrow_copy_constructible<T>::value
            && std::is_nothrow_copy_assignable<T>::value)
    {
        if(!rhs.mHasValue)
            this->reset();
        else if(this->mHasValue)
            this->mValue = rhs.mValue;
        else
            this->construct(rhs.mValue);
    }

    void assign(optstore_helper&& rhs)
        noexcept(std::is_nothrow_move_constructible<T>::value
            && std::is_nothrow_move_assignable<T>::value)
    {
        if(!rhs.mHasValue)
            this->reset();
        else if(this->mHasValue)
            this->mValue = std::move(rhs.mValue);
        else
            this->construct(std::move(rhs.mValue));
    }
};

/* Define copy and move constructors and assignment operators, which may or may
 * not be trivial. Default definition is completely trivial.
 */
template<typename T, bool trivial_copy = std::is_trivially_copy_constructible<T>::value,
    bool trivial_move = std::is_trivially_move_constructible<T>::value,
    /* Trivial assignment is dependent on trivial construction. */
    bool = trivial_copy && std::is_trivially_copy_assignable<T>::value,
    bool = trivial_move && std::is_trivially_move_assignable<T>::value>
struct optional_storage : public optstore_helper<T> {
    using optstore_helper<T>::optstore_helper;
    optional_storage() noexcept = default;
    optional_storage(const optional_storage&) = default;
    optional_storage(optional_storage&&) = default;
    optional_storage& operator=(const optional_storage&) = default;
    optional_storage& operator=(optional_storage&&) = default;
};

/* Non-trivial move assignment. */
template<typename T>
struct optional_storage<T, true, true, true, false> : public optstore_helper<T> {
    using optstore_helper<T>::optstore_helper;
    optional_storage() noexcept = default;
    optional_storage(const optional_storage&) = default;
    optional_storage(optional_storage&&) = default;
    optional_storage& operator=(const optional_storage&) = default;
    optional_storage& operator=(optional_storage&& rhs) NOEXCEPT_AS(this->assign(std::move(rhs)))
    { this->assign(std::move(rhs)); return *this; }
};

/* Non-trivial move construction. */
template<typename T>
struct optional_storage<T, true, false, true, false> : public optstore_helper<T> {
    using optstore_helper<T>::optstore_helper;
    optional_storage() noexcept = default;
    optional_storage(const optional_storage&) = default;
    optional_storage(optional_storage&& rhs) NOEXCEPT_AS(this->construct(std::move(rhs.mValue)))
    { if(rhs.mHasValue) this->construct(std::move(rhs.mValue)); }
    optional_storage& operator=(const optional_storage&) = default;
    optional_storage& operator=(optional_storage&& rhs) NOEXCEPT_AS(this->assign(std::move(rhs)))
    { this->assign(std::move(rhs)); return *this; }
};

/* Non-trivial copy assignment. */
template<typename T>
struct optional_storage<T, true, true, false, true> : public optstore_helper<T> {
    using optstore_helper<T>::optstore_helper;
    optional_storage() noexcept = default;
    optional_storage(const optional_storage&) = default;
    optional_storage(optional_storage&&) = default;
    optional_storage& operator=(const optional_storage &rhs) NOEXCEPT_AS(this->assign(rhs))
    { this->assign(rhs); return *this; }
    optional_storage& operator=(optional_storage&&) = default;
};

/* Non-trivial copy construction. */
template<typename T>
struct optional_storage<T, false, true, false, true> : public optstore_helper<T> {
    using optstore_helper<T>::optstore_helper;
    optional_storage() noexcept = default;
    optional_storage(const optional_storage &rhs) NOEXCEPT_AS(this->construct(rhs.mValue))
    { if(rhs.mHasValue) this->construct(rhs.mValue); }
    optional_storage(optional_storage&&) = default;
    optional_storage& operator=(const optional_storage &rhs) NOEXCEPT_AS(this->assign(rhs))
    { this->assign(rhs); return *this; }
    optional_storage& operator=(optional_storage&&) = default;
};

/* Non-trivial assignment. */
template<typename T>
struct optional_storage<T, true, true, false, false> : public optstore_helper<T> {
    using optstore_helper<T>::optstore_helper;
    optional_storage() noexcept = default;
    optional_storage(const optional_storage&) = default;
    optional_storage(optional_storage&&) = default;
    optional_storage& operator=(const optional_storage &rhs) NOEXCEPT_AS(this->assign(rhs))
    { this->assign(rhs); return *this; }
    optional_storage& operator=(optional_storage&& rhs) NOEXCEPT_AS(this->assign(std::move(rhs)))
    { this->assign(std::move(rhs)); return *this; }
};

/* Non-trivial assignment, non-trivial move construction. */
template<typename T>
struct optional_storage<T, true, false, false, false> : public optstore_helper<T> {
    using optstore_helper<T>::optstore_helper;
    optional_storage() noexcept = default;
    optional_storage(const optional_storage&) = default;
    optional_storage(optional_storage&& rhs) NOEXCEPT_AS(this->construct(std::move(rhs.mValue)))
    { if(rhs.mHasValue) this->construct(std::move(rhs.mValue)); }
    optional_storage& operator=(const optional_storage &rhs) NOEXCEPT_AS(this->assign(rhs))
    { this->assign(rhs); return *this; }
    optional_storage& operator=(optional_storage&& rhs) NOEXCEPT_AS(this->assign(std::move(rhs)))
    { this->assign(std::move(rhs)); return *this; }
};

/* Non-trivial assignment, non-trivial copy construction. */
template<typename T>
struct optional_storage<T, false, true, false, false> : public optstore_helper<T> {
    using optstore_helper<T>::optstore_helper;
    optional_storage() noexcept = default;
    optional_storage(const optional_storage &rhs) NOEXCEPT_AS(this->construct(rhs.mValue))
    { if(rhs.mHasValue) this->construct(rhs.mValue); }
    optional_storage(optional_storage&&) = default;
    optional_storage& operator=(const optional_storage &rhs) NOEXCEPT_AS(this->assign(rhs))
    { this->assign(rhs); return *this; }
    optional_storage& operator=(optional_storage&& rhs) NOEXCEPT_AS(this->assign(std::move(rhs)))
    { this->assign(std::move(rhs)); return *this; }
};

/* Completely non-trivial. */
template<typename T>
struct optional_storage<T, false, false, false, false> : public optstore_helper<T> {
    using optstore_helper<T>::optstore_helper;
    optional_storage() noexcept = default;
    optional_storage(const optional_storage &rhs) NOEXCEPT_AS(this->construct(rhs.mValue))
    { if(rhs.mHasValue) this->construct(rhs.mValue); }
    optional_storage(optional_storage&& rhs) NOEXCEPT_AS(this->construct(std::move(rhs.mValue)))
    { if(rhs.mHasValue) this->construct(std::move(rhs.mValue)); }
    optional_storage& operator=(const optional_storage &rhs) NOEXCEPT_AS(this->assign(rhs))
    { this->assign(rhs); return *this; }
    optional_storage& operator=(optional_storage&& rhs) NOEXCEPT_AS(this->assign(std::move(rhs)))
    { this->assign(std::move(rhs)); return *this; }
};


template<typename T>
class optional {
    using storage_t = optional_storage<T>;

    storage_t mStore{};

public:
    using value_type = T;

    optional() = default;
    optional(const optional&) = default;
    optional(optional&&) = default;
    optional(nullopt_t) noexcept { }
    template<typename ...Args>
    explicit optional(in_place_t, Args&& ...args)
        : mStore{al::in_place, std::forward<Args>(args)...}
    { }
    ~optional() = default;

    optional& operator=(const optional&) = default;
    optional& operator=(optional&&) = default;
    optional& operator=(nullopt_t) noexcept { mStore.reset(); return *this; }
    template<typename U=T>
    std::enable_if_t<std::is_constructible<T, U>::value
        && std::is_assignable<T&, U>::value
        && !std::is_same<std::decay_t<U>, optional<T>>::value
        && (!std::is_same<std::decay_t<U>, T>::value || !std::is_scalar<U>::value),
    optional&> operator=(U&& rhs)
    {
        if(mStore.mHasValue)
            mStore.mValue = std::forward<U>(rhs);
        else
            mStore.construct(std::forward<U>(rhs));
        return *this;
    }

    const T* operator->() const { return std::addressof(mStore.mValue); }
    T* operator->() { return std::addressof(mStore.mValue); }
    const T& operator*() const& { return mStore.mValue; }
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

    void reset() noexcept { mStore.reset(); }
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

#undef NOEXCEPT_AS
} // namespace al

#endif /* AL_OPTIONAL_H */
