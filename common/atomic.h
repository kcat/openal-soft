#ifndef AL_ATOMIC_H
#define AL_ATOMIC_H

#include <atomic>
#include <memory>

#include "almalloc.h"

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
inline void AtomicReplaceHead(std::atomic<T> &head, T newhead)
{
    T first_ = head.load(std::memory_order_acquire);
    do {
        newhead->next.store(first_, std::memory_order_relaxed);
    } while(!head.compare_exchange_weak(first_, newhead,
            std::memory_order_acq_rel, std::memory_order_acquire));
}

namespace al {

template<typename T, typename D=std::default_delete<T>>
class atomic_unique_ptr {
    std::atomic<gsl::owner<T*>> mPointer{};

public:
    atomic_unique_ptr() = default;
    atomic_unique_ptr(const atomic_unique_ptr&) = delete;
    explicit atomic_unique_ptr(std::nullptr_t) noexcept { }
    explicit atomic_unique_ptr(gsl::owner<T*> ptr) noexcept : mPointer{ptr} { }
    explicit atomic_unique_ptr(std::unique_ptr<T>&& rhs) noexcept : mPointer{rhs.release()} { }
    ~atomic_unique_ptr() { if(auto ptr = mPointer.load(std::memory_order_relaxed)) D{}(ptr); }

    atomic_unique_ptr& operator=(const atomic_unique_ptr&) = delete;
    atomic_unique_ptr& operator=(std::nullptr_t) noexcept
    {
        if(auto ptr = mPointer.exchange(nullptr))
            D{}(ptr);
        return *this;
    }
    atomic_unique_ptr& operator=(std::unique_ptr<T>&& rhs) noexcept
    {
        if(auto ptr = mPointer.exchange(rhs.release()))
            D{}(ptr);
        return *this;
    }

    [[nodiscard]]
    T* load(std::memory_order m=std::memory_order_seq_cst) const { return mPointer.load(m); }
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
    void store(std::unique_ptr<T>&& ptr, std::memory_order m=std::memory_order_seq_cst) noexcept
    {
        if(auto oldptr = mPointer.exchange(ptr.release(), m))
            D{}(oldptr);
    }

    [[nodiscard]]
    std::unique_ptr<T> exchange(std::nullptr_t, std::memory_order m=std::memory_order_seq_cst) noexcept
    { return std::unique_ptr<T>{mPointer.exchange(nullptr, m)}; }
    [[nodiscard]]
    std::unique_ptr<T> exchange(gsl::owner<T*> ptr, std::memory_order m=std::memory_order_seq_cst) noexcept
    { return std::unique_ptr<T>{mPointer.exchange(ptr, m)}; }
    [[nodiscard]]
    std::unique_ptr<T> exchange(std::unique_ptr<T>&& ptr, std::memory_order m=std::memory_order_seq_cst) noexcept
    { return std::unique_ptr<T>{mPointer.exchange(ptr.release(), m)}; }

    [[nodiscard]]
    bool is_lock_free() const noexcept { mPointer.is_lock_free(); }

    static constexpr auto is_always_lock_free = std::atomic<gsl::owner<T*>>::is_always_lock_free;
};

} // namespace al

#endif /* AL_ATOMIC_H */
