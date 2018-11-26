#ifndef AL_ATOMIC_H
#define AL_ATOMIC_H

#include <atomic>


#define almemory_order std::memory_order
#define almemory_order_relaxed std::memory_order_relaxed
#define almemory_order_consume std::memory_order_consume
#define almemory_order_acquire std::memory_order_acquire
#define almemory_order_release std::memory_order_release
#define almemory_order_acq_rel std::memory_order_acq_rel
#define almemory_order_seq_cst std::memory_order_seq_cst

#define ATOMIC_LOAD std::atomic_load_explicit
#define ATOMIC_STORE std::atomic_store_explicit

#define ATOMIC_ADD std::atomic_fetch_add_explicit
#define ATOMIC_SUB std::atomic_fetch_sub_explicit


#define ATOMIC_LOAD_SEQ(_val) ATOMIC_LOAD(_val, almemory_order_seq_cst)
#define ATOMIC_STORE_SEQ(_val, _newval) ATOMIC_STORE(_val, _newval, almemory_order_seq_cst)

#define ATOMIC_ADD_SEQ(_val, _incr) ATOMIC_ADD(_val, _incr, almemory_order_seq_cst)
#define ATOMIC_SUB_SEQ(_val, _decr) ATOMIC_SUB(_val, _decr, almemory_order_seq_cst)


using RefCount = std::atomic<unsigned int>;

inline void InitRef(RefCount *ptr, unsigned int value)
{ ptr->store(value, std::memory_order_relaxed); }
inline unsigned int ReadRef(RefCount *ptr)
{ return ptr->load(std::memory_order_acquire); }
inline unsigned int IncrementRef(RefCount *ptr)
{ return ptr->fetch_add(1u, std::memory_order_acq_rel)+1u; }
inline unsigned int DecrementRef(RefCount *ptr)
{ return ptr->fetch_sub(1u, std::memory_order_acq_rel)-1u; }


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

#endif /* AL_ATOMIC_H */
