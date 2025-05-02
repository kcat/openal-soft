#ifndef INTRUSIVE_PTR_H
#define INTRUSIVE_PTR_H

#include <atomic>
#include <cstddef>
#include <utility>

#include "atomic.h"


namespace al {

template<typename T>
class intrusive_ref {
    std::atomic<unsigned int> mRef{1u};

protected:
    ~intrusive_ref() = default;

public:
    unsigned int add_ref() noexcept { return IncrementRef(mRef); }
    unsigned int dec_ref() noexcept
    {
        auto ref = DecrementRef(mRef);
        if(ref == 0) [[unlikely]]
            delete static_cast<T*>(this);
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


template<typename T> /* NOLINTNEXTLINE(clazy-rule-of-three) False positive */
class intrusive_ptr {
    T *mPtr{nullptr};

public:
    constexpr intrusive_ptr() noexcept = default;
    constexpr intrusive_ptr(const intrusive_ptr &rhs) noexcept : mPtr{rhs.mPtr}
    { if(mPtr) mPtr->add_ref(); }
    constexpr intrusive_ptr(intrusive_ptr&& rhs) noexcept : mPtr{rhs.mPtr}
    { rhs.mPtr = nullptr; }
    constexpr intrusive_ptr(std::nullptr_t) noexcept { } /* NOLINT(google-explicit-constructor) */
    explicit constexpr intrusive_ptr(T *ptr) noexcept : mPtr{ptr} { }
    constexpr ~intrusive_ptr() { if(mPtr) mPtr->dec_ref(); }

    /* NOLINTBEGIN(bugprone-unhandled-self-assignment)
     * Self-assignment is handled properly here.
     */
    constexpr intrusive_ptr& operator=(const intrusive_ptr &rhs) noexcept
    {
        static_assert(noexcept(std::declval<T*>()->dec_ref()), "dec_ref must be noexcept");

        if(rhs.mPtr) rhs.mPtr->add_ref();
        if(mPtr) mPtr->dec_ref();
        mPtr = rhs.mPtr;
        return *this;
    }
    /* NOLINTEND(bugprone-unhandled-self-assignment) */
    constexpr intrusive_ptr& operator=(intrusive_ptr&& rhs) noexcept
    {
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
        if(mPtr)
            mPtr->dec_ref();
        mPtr = ptr;
    }

    constexpr T* release() noexcept { return std::exchange(mPtr, nullptr); }

    constexpr void swap(intrusive_ptr &rhs) noexcept { std::swap(mPtr, rhs.mPtr); }
    constexpr void swap(intrusive_ptr&& rhs) noexcept { std::swap(mPtr, rhs.mPtr); }
};

} // namespace al

#endif /* INTRUSIVE_PTR_H */
