#ifndef AL_ATOMIC_H
#define AL_ATOMIC_H

#include <atomic>
#include <cstddef>
#include <memory>

#include "alnumeric.h"
#include "gsl/gsl"

#ifdef __APPLE__
#include <AvailabilityMacros.h>
#endif

#if defined(MAC_OS_X_VERSION_MIN_REQUIRED) && MAC_OS_X_VERSION_MIN_REQUIRED < 110000
/* For macOS versions before 11, we can't rely on atomic::wait and friends, and
 * need to use custom methods.
 */

#include <mutex>
#include <condition_variable>

/* These could be put into a table where, rather than one mutex/condvar/counter
 * set that all atomic waiters use, individual waiters will use one of a number
 * of different sets dependent on their address, reducing the number of waiters
 * on any given set. However, we use this as a fallback for much older macOS
 * systems and only expect one atomic waiter per device, and one device per
 * process, which itself won't wake the atomic terribly often, so the extra
 * complexity and resources may be best avoided if not necessary.
 */
inline auto gAtomicWaitMutex = std::mutex{};
inline auto gAtomicWaitCondVar = std::condition_variable{};
inline auto gAtomicWaitCounter = std::uint32_t{0};

/* See: https://outerproduct.net/futex-dictionary.html */
#define UL_COMPARE_AND_WAIT          1
#define UL_UNFAIR_LOCK               2
#define UL_COMPARE_AND_WAIT_SHARED   3
#define UL_UNFAIR_LOCK64_SHARED      4
#define UL_COMPARE_AND_WAIT64        5
#define UL_COMPARE_AND_WAIT64_SHARED 6

#define ULF_WAKE_ALL 0x00000100

extern "C" {
auto __attribute__((weak_import)) __ulock_wait(std::uint32_t op, void *addr, std::uint64_t value, std::uint32_t timeout) -> int;
auto __attribute__((weak_import)) __ulock_wake(std::uint32_t op, void *addr, std::uint64_t wake_value) -> int;
} /* extern "C" */
#endif


template<typename T>
auto IncrementRef(std::atomic<T> &ref) noexcept
{ return ref.fetch_add(1u, std::memory_order_acq_rel)+1u; }

template<typename T>
auto DecrementRef(std::atomic<T> &ref) noexcept
{ return ref.fetch_sub(1u, std::memory_order_acq_rel)-1u; }


/* WARNING: A livelock is theoretically possible if another thread keeps
 * changing the head without giving this a chance to actually swap in the new
 * one (practically impossible with this little code, but...).
 */
template<typename T>
void AtomicReplaceHead(std::atomic<T> &head, T newhead)
{
    T first_ = head.load(std::memory_order_acquire);
    do {
        newhead->next.store(first_, std::memory_order_relaxed);
    } while(!head.compare_exchange_weak(first_, newhead,
            std::memory_order_acq_rel, std::memory_order_acquire));
}

namespace al {

template<typename T>
auto atomic_wait(std::atomic<T> &aval, T const value,
    std::memory_order const order = std::memory_order_seq_cst) noexcept -> void
{
#if defined(MAC_OS_X_VERSION_MIN_REQUIRED) && MAC_OS_X_VERSION_MIN_REQUIRED < 110000
    static_assert(sizeof(aval) == sizeof(T));

    if(sizeof(T) == sizeof(std::uint32_t) && __ulock_wait != nullptr)
    {
        while(aval.load(order) == value)
            __ulock_wait(UL_COMPARE_AND_WAIT, &aval, value, 0);
    }
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 101500
    else if(sizeof(T) == sizeof(u64) && __ulock_wait != nullptr)
    {
        while(aval.load(order) == value)
            __ulock_wait(UL_COMPARE_AND_WAIT64, &aval, value, 0);
    }
#endif
    else
    {
        auto lock = std::unique_lock{gAtomicWaitMutex};
        ++gAtomicWaitCounter;
        while(aval.load(order) == value)
            gAtomicWaitCondVar.wait(lock);
        --gAtomicWaitCounter;
    }

#else

    aval.wait(value, order);
#endif
}

template<typename T>
auto atomic_notify_one(std::atomic<T> &aval) noexcept -> void
{
#if defined(MAC_OS_X_VERSION_MIN_REQUIRED) && MAC_OS_X_VERSION_MIN_REQUIRED < 110000
    static_assert(sizeof(aval) == sizeof(T));

    if(sizeof(T) == sizeof(std::uint32_t) && __ulock_wake != nullptr)
        __ulock_wake(UL_COMPARE_AND_WAIT, &aval, 0);
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 101500
    else if(sizeof(T) == sizeof(u64) && __ulock_wake != nullptr)
        __ulock_wake(UL_COMPARE_AND_WAIT64, &aval, 0);
#endif
    else
    {
        auto lock = std::unique_lock{gAtomicWaitMutex};
        auto const numwaits = gAtomicWaitCounter;
        lock.unlock();
        if(numwaits > 0)
        {
            /* notify_all since we can't guarantee notify_one will wake a
             * waiter waiting on this particular object. With notify_all, we
             * just act as if all waiters were spuriously woken up and they'll
             * recheck.
             */
            gAtomicWaitCondVar.notify_all();
        }
    }

#else

    aval.notify_one();
#endif
}

template<typename T>
auto atomic_notify_all(std::atomic<T> &aval) noexcept -> void
{
#if defined(MAC_OS_X_VERSION_MIN_REQUIRED) && MAC_OS_X_VERSION_MIN_REQUIRED < 110000
    static_assert(sizeof(aval) == sizeof(T));

    if(sizeof(T) == sizeof(std::uint32_t) && __ulock_wake != nullptr)
        __ulock_wake(UL_COMPARE_AND_WAIT | ULF_WAKE_ALL, &aval, 0);
#if defined(MAC_OS_X_VERSION_MIN_REQUIRED) && MAC_OS_X_VERSION_MIN_REQUIRED >= 101500
    else if(sizeof(T) == sizeof(u64) && __ulock_wake != nullptr)
        __ulock_wake(UL_COMPARE_AND_WAIT64 | ULF_WAKE_ALL, &aval, 0);
#endif
    else
    {
        auto lock = std::unique_lock{gAtomicWaitMutex};
        auto const numwaits = gAtomicWaitCounter;
        lock.unlock();
        if(numwaits > 0)
            gAtomicWaitCondVar.notify_all();
    }

#else

    aval.notify_all();
#endif
}

template<typename T, typename D=std::default_delete<T>>
class atomic_unique_ptr {
    std::atomic<gsl::owner<T*>> mPointer{};

    using unique_ptr_t = std::unique_ptr<T,D>;

public:
    atomic_unique_ptr() = default;
    atomic_unique_ptr(const atomic_unique_ptr&) = delete;
    explicit atomic_unique_ptr(std::nullptr_t) noexcept { }
    explicit atomic_unique_ptr(gsl::owner<T*> ptr) noexcept : mPointer{ptr} { }
    explicit atomic_unique_ptr(unique_ptr_t&& rhs) noexcept : mPointer{std::move(rhs).release()} {}
    ~atomic_unique_ptr()
    {
        if(auto ptr = mPointer.exchange(nullptr, std::memory_order_relaxed))
            D{}(ptr);
    }

    auto operator=(const atomic_unique_ptr&) -> atomic_unique_ptr& = delete;
    auto operator=(std::nullptr_t) noexcept -> atomic_unique_ptr&
    {
        if(auto ptr = mPointer.exchange(nullptr))
            D{}(ptr);
        return *this;
    }
    auto operator=(unique_ptr_t&& rhs) noexcept -> atomic_unique_ptr&
    {
        if(auto ptr = mPointer.exchange(std::move(rhs).release()))
            D{}(ptr);
        return *this;
    }

    [[nodiscard]]
    auto load(std::memory_order m=std::memory_order_seq_cst) const noexcept -> T*
    { return mPointer.load(m); }
    void store(std::nullptr_t, std::memory_order m=std::memory_order_seq_cst) noexcept
    {
        if(auto oldptr = mPointer.exchange(nullptr, m))
            D{}(oldptr);
    }
    void store(gsl::owner<T*> ptr, std::memory_order m=std::memory_order_seq_cst) noexcept
    {
        if(auto oldptr = mPointer.exchange(ptr, m))
            D{}(oldptr);
    }
    void store(unique_ptr_t&& ptr, std::memory_order m=std::memory_order_seq_cst) noexcept
    {
        if(auto oldptr = mPointer.exchange(std::move(ptr).release(), m))
            D{}(oldptr);
    }

    [[nodiscard]]
    auto exchange(std::nullptr_t, std::memory_order m=std::memory_order_seq_cst) noexcept
        -> unique_ptr_t
    { return unique_ptr_t{mPointer.exchange(nullptr, m)}; }

    [[nodiscard]]
    auto exchange(gsl::owner<T*> ptr, std::memory_order m=std::memory_order_seq_cst) noexcept
        -> unique_ptr_t
    { return unique_ptr_t{mPointer.exchange(ptr, m)}; }

    [[nodiscard]]
    auto exchange(std::unique_ptr<T>&& ptr, std::memory_order m=std::memory_order_seq_cst) noexcept
        -> unique_ptr_t
    { return unique_ptr_t{mPointer.exchange(std::move(ptr).release(), m)}; }

    [[nodiscard]]
    auto is_lock_free() const noexcept -> bool { return mPointer.is_lock_free(); }

    static constexpr auto is_always_lock_free = std::atomic<gsl::owner<T*>>::is_always_lock_free;
};

} // namespace al

#endif /* AL_ATOMIC_H */
