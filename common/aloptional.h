#ifndef AL_OPTIONAL_H
#define AL_OPTIONAL_H

#include <type_traits>

#include "almalloc.h"

namespace al {

#define REQUIRES(...) bool _rt=true, typename std::enable_if<_rt && (__VA_ARGS__),int>::type = 0

struct nullopt_t { };
struct in_place_t { };

constexpr nullopt_t nullopt{};
constexpr in_place_t in_place{};

template<typename T>
class optional {
public:
    using value_type = T;

    optional() noexcept = default;
    optional(nullopt_t) noexcept { }
    template<REQUIRES(std::is_copy_constructible<T>::value)>
    optional(const optional &rhs) : mHasValue{rhs.mHasValue}
    {
        if(mHasValue)
            std::uninitialized_copy_n(std::addressof(*rhs), 1, std::addressof(mValue));
    }
    template<REQUIRES(std::is_move_constructible<T>::value)>
    optional(optional&& rhs) : mHasValue{rhs.mHasValue}
    {
        if(mHasValue)
            al::uninitialized_move_n(std::addressof(*rhs), 1, std::addressof(mValue));
    }
    template<typename... Args>
    explicit optional(in_place_t, Args&& ...args)
      : mHasValue{true}, mValue{std::forward<Args>(args)...}
    { }
    ~optional() { reset(); }

    optional& operator=(nullopt_t) noexcept { reset(); return *this; }
    template<REQUIRES(std::is_copy_constructible<T>::value && std::is_copy_assignable<T>::value)>
    optional& operator=(const optional &rhs)
    {
        if(!rhs)
            reset();
        else if(*this)
            mValue = *rhs;
        else
        {
            std::uninitialized_copy_n(std::addressof(*rhs), 1, std::addressof(mValue));
            mHasValue = true;
        }
        return *this;
    }
    template<REQUIRES(std::is_move_constructible<T>::value && std::is_move_assignable<T>::value)>
    optional& operator=(optional&& rhs)
    {
        if(!rhs)
            reset();
        else if(*this)
            mValue = std::move(*rhs);
        else
        {
            al::uninitialized_move_n(std::addressof(*rhs), 1, std::addressof(mValue));
            mHasValue = true;
        }
        return *this;
    }

    const T* operator->() const { return std::addressof(mValue); }
    T* operator->() { return std::addressof(mValue); }
    const T& operator*() const& { return mValue; }
    T& operator*() & { return mValue; }
    const T&& operator*() const&& { return std::move(mValue); }
    T&& operator*() && { return std::move(mValue); }

    operator bool() const noexcept { return mHasValue; }
    bool has_value() const noexcept { return mHasValue; }

    T& value() & { return mValue; }
    const T& value() const& { return mValue; }
    T&& value() && { return std::move(mValue); }
    const T&& value() const&& { return std::move(mValue); }

    template<typename U>
    T value_or(U&& defval) const&
    { return bool{*this} ? **this : static_cast<T>(std::forward<U>(defval)); }
    template<typename U>
    T value_or(U&& defval) &&
    { return bool{*this} ? std::move(**this) : static_cast<T>(std::forward<U>(defval)); }

    void reset() noexcept
    {
        if(mHasValue)
            al::destroy_at(std::addressof(mValue));
        mHasValue = false;
    }

private:
    bool mHasValue{false};
    union {
        char mDummy[sizeof(T)]{};
        T mValue;
    };
};

#undef REQUIRES

} // namespace al

#endif /* AL_SPAN_H */
