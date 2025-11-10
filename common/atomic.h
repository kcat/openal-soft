#ifndef AL_ATOMIC_H
#define AL_ATOMIC_H

#include <atomic>
#include <cstddef>
#include <memory>

#include "alnumeric.h"
#include "gsl/gsl"

#ifdef __APPLE__
/* See: https://outerproduct.net/futex-dictionary.html */
#define UL_COMPARE_AND_WAIT          1
#define UL_UNFAIR_LOCK               2
#define UL_COMPARE_AND_WAIT_SHARED   3
#define UL_UNFAIR_LOCK64_SHARED      4
#define UL_COMPARE_AND_WAIT64        5
#define UL_COMPARE_AND_WAIT64_SHARED 6

#define ULF_WAKE_ALL 0x00000100

extern "C" auto __ulock_wait(u32 op, void *addr, u64 value, u32 timeout) -> int;
extern "C" auto __ulock_wake(u32 op, void *addr, u64 wake_value) -> int;
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
    std::memory_order const order [[maybe_unused]] = std::memory_order_seq_cst) noexcept -> void
{
#ifdef __APPLE__
    static_assert(sizeof(aval) == sizeof(T));

    if constexpr(sizeof(T) == sizeof(u32))
        __ulock_wait(UL_COMPARE_AND_WAIT, &aval, value, 0);
    else if constexpr(sizeof(T) == sizeof(u64))
        __ulock_wait(UL_COMPARE_AND_WAIT64, &aval, value, 0);
    else
        static_assert(sizeof(T) == sizeof(u32) || sizeof(T) == sizeof(u64));

#else

    aval.wait(value, order);
#endif
}

template<typename T>
auto atomic_notify_one(std::atomic<T> &aval) noexcept -> void
{
#ifdef __APPLE__
    static_assert(sizeof(aval) == sizeof(T));

    if constexpr(sizeof(T) == sizeof(u32))
        __ulock_wake(UL_COMPARE_AND_WAIT, &aval, 0);
    else if constexpr(sizeof(T) == sizeof(u64))
        __ulock_wake(UL_COMPARE_AND_WAIT64, &aval, 0);
    else
        static_assert(sizeof(T) == sizeof(u32) || sizeof(T) == sizeof(u64));

#else

    aval.notify_one();
#endif
}

template<typename T>
auto atomic_notify_all(std::atomic<T> &aval) noexcept -> void
{
#ifdef __APPLE__
    static_assert(sizeof(aval) == sizeof(T));

    if constexpr(sizeof(T) == sizeof(u32))
        __ulock_wake(UL_COMPARE_AND_WAIT | ULF_WAKE_ALL, &aval, 0);
    else if constexpr(sizeof(T) == sizeof(u64))
        __ulock_wake(UL_COMPARE_AND_WAIT64 | ULF_WAKE_ALL, &aval, 0);
    else
        static_assert(sizeof(T) == sizeof(u32) || sizeof(T) == sizeof(u64));

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
