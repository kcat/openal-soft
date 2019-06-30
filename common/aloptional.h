#ifndef AL_OPTIONAL_H
#define AL_OPTIONAL_H

#include <type_traits>

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
            new (&mValue) T{*rhs};
    }
    template<REQUIRES(std::is_move_constructible<T>::value)>
    optional(optional&& rhs) : mHasValue{rhs.mHasValue}
    {
        if(mHasValue)
            new (&mValue) T{std::move(*rhs)};
    }
    template<typename... Args>
    explicit optional(in_place_t, Args&& ...args)
      : mHasValue{true}, mValue{std::forward<Args>(args)...}
    { }
    template<REQUIRES(!std::is_copy_constructible<T>::value)>
    optional(const optional&) noexcept = delete;
    ~optional() { reset(); }

    optional& operator=(nullopt_t) noexcept { reset(); }
    template<REQUIRES(std::is_copy_constructible<T>::value && std::is_copy_assignable<T>::value)>
    optional& operator=(const optional &rhs)
    {
        if(!rhs)
            reset();
        else
        {
            mValue = *rhs;
            mHasValue = true;
        }
        return *this;
    }
    template<REQUIRES(std::is_move_constructible<T>::value && std::is_move_assignable<T>::value)>
    optional& operator=(optional&& rhs)
    {
        if(!rhs)
            reset();
        else
        {
            mValue = std::move(*rhs);
            mHasValue = true;
        }
        return *this;
    }
    template<REQUIRES(!std::is_copy_constructible<T>::value || !std::is_copy_assignable<T>::value)>
    optional& operator=(const optional&) = delete;

    const T* operator->() const { return &mValue; }
    T* operator->() { return &mValue; }
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
            mValue.~T();
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
