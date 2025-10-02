#ifndef INTRUSIVE_PTR_H
#define INTRUSIVE_PTR_H

#include <atomic>
#include <concepts>
#include <cstddef>
#include <memory>
#include <utility>

#include "gsl/gsl"

namespace al {

template<typename T, typename D=std::default_delete<T>>
class intrusive_ref : protected D {
    std::atomic<unsigned int> mRef{1u};

    intrusive_ref() = default;
    ~intrusive_ref() = default;

    friend T;

public:
    unsigned int inc_ref() noexcept { return mRef.fetch_add(1, std::memory_order_acq_rel)+1; }
    unsigned int dec_ref() noexcept
    {
        auto ref = mRef.fetch_sub(1, std::memory_order_acq_rel)-1;
        if(ref == 0) [[unlikely]]
            D::operator()(static_cast<gsl::owner<T*>>(this));
        return ref;
    }

    /**
     * Release only if doing so would not bring the object to 0 references and
     * delete it. Returns false if the object could not be released.
     *
     * NOTE: The caller is responsible for handling a failed release, as it
     * means the object has no other references and needs to be be deleted
     * somehow.
     */
    bool releaseIfNoDelete() noexcept
    {
        auto val = mRef.load(std::memory_order_acquire);
        while(val > 1 && !mRef.compare_exchange_strong(val, val-1, std::memory_order_acq_rel))
        {
            /* val was updated with the current value on failure, so just try
             * again.
             */
        }

        return val >= 2;
    }
};


template<typename T>
class intrusive_ptr {
    T *mPtr{nullptr};

public:
    constexpr intrusive_ptr() noexcept = default;
    constexpr intrusive_ptr(const intrusive_ptr &rhs) noexcept : mPtr{rhs.mPtr}
    { if(mPtr) mPtr->inc_ref(); }
    constexpr intrusive_ptr(intrusive_ptr&& rhs) noexcept : mPtr{rhs.mPtr}
    { rhs.mPtr = nullptr; }
    constexpr intrusive_ptr(std::nullptr_t) noexcept { } /* NOLINT(google-explicit-constructor) */
    explicit constexpr intrusive_ptr(T *ptr) noexcept : mPtr{ptr} { }
    constexpr ~intrusive_ptr() { if(mPtr) mPtr->dec_ref(); }

    constexpr auto operator=(const intrusive_ptr &rhs) noexcept -> intrusive_ptr&
    {
        static_assert(noexcept(std::declval<T*>()->dec_ref()), "dec_ref must be noexcept");
        if(&rhs != this) [[likely]]
        {
            if(rhs.mPtr) rhs.mPtr->inc_ref();
            if(mPtr) mPtr->dec_ref();
            mPtr = rhs.mPtr;
        }
        return *this;
    }
    constexpr auto operator=(intrusive_ptr&& rhs) noexcept -> intrusive_ptr&
    {
        static_assert(noexcept(std::declval<T*>()->dec_ref()), "dec_ref must be noexcept");
        if(&rhs != this) [[likely]]
        {
            if(mPtr) mPtr->dec_ref();
            mPtr = std::exchange(rhs.mPtr, nullptr);
        }
        return *this;
    }

    explicit constexpr operator bool() const noexcept { return mPtr != nullptr; }

    [[nodiscard]] constexpr auto operator*() const noexcept -> T& { return *mPtr; }
    [[nodiscard]] constexpr auto operator->() const noexcept -> T* { return mPtr; }
    [[nodiscard]] constexpr auto get() const noexcept -> T* { return mPtr; }

    constexpr void reset(T *ptr=nullptr) noexcept
    {
        static_assert(noexcept(std::declval<T*>()->dec_ref()), "dec_ref must be noexcept");
        if(mPtr)
            mPtr->dec_ref();
        mPtr = ptr;
    }

    constexpr auto release() noexcept -> T* { return std::exchange(mPtr, nullptr); }

    constexpr void swap(intrusive_ptr &rhs) noexcept { std::swap(mPtr, rhs.mPtr); }
};

template<typename T>
void swap(intrusive_ptr<T> &lhs, intrusive_ptr<T> &rhs) noexcept { lhs.swap(rhs); }

template<typename T> requires std::three_way_comparable_with<T*,std::nullptr_t> [[nodiscard]]
constexpr auto operator<=>(const intrusive_ptr<T> &lhs, std::nullptr_t) noexcept
{ return std::compare_three_way{}(lhs.get(), nullptr); }

template<typename T> [[nodiscard]]
constexpr auto operator==(const intrusive_ptr<T> &lhs, std::nullptr_t) noexcept
{ return !lhs; }

template<typename T> [[nodiscard]]
constexpr auto operator!=(const intrusive_ptr<T> &lhs, std::nullptr_t) noexcept
{ return !(lhs == nullptr); }

template<typename T> requires std::three_way_comparable_with<std::nullptr_t,T*> [[nodiscard]]
constexpr auto operator<=>(std::nullptr_t, const intrusive_ptr<T> &rhs) noexcept
{ return std::compare_three_way{}(nullptr, rhs.get()); }

template<typename T> [[nodiscard]]
constexpr auto operator==(std::nullptr_t, const intrusive_ptr<T> &rhs) noexcept
{ return !rhs; }

template<typename T> [[nodiscard]]
constexpr auto operator!=(std::nullptr_t, const intrusive_ptr<T> &rhs) noexcept
{ return !(rhs == nullptr); }


template<typename T, typename U> requires std::three_way_comparable_with<T*,U*> [[nodiscard]]
constexpr auto operator<=>(const intrusive_ptr<T> &lhs, const intrusive_ptr<U>& rhs) noexcept
{ return std::compare_three_way{}(lhs.get(), rhs.get()); }

template<typename T, typename U> requires std::equality_comparable_with<T*,U*> [[nodiscard]]
constexpr auto operator==(const intrusive_ptr<T> &lhs, const intrusive_ptr<U>& rhs) noexcept
{ return lhs.get() == rhs.get(); }

template<typename T, typename U> requires std::equality_comparable_with<T*,U*> [[nodiscard]]
constexpr auto operator!=(const intrusive_ptr<T> &lhs, const intrusive_ptr<U>& rhs) noexcept
{ return !(lhs == rhs); }

} // namespace al

#endif /* INTRUSIVE_PTR_H */
