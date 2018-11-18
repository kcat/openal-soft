#ifndef AL_ATOMIC_H
#define AL_ATOMIC_H

#include <atomic>

#include "static_assert.h"
#include "bool.h"

#ifdef __GNUC__
/* This helps cast away the const-ness of a pointer without accidentally
 * changing the pointer type. This is necessary due to Clang's inability to use
 * atomic_load on a const _Atomic variable.
 */
#define CONST_CAST(T, V) __extension__({                                      \
    const T _tmp = (V);                                                       \
    (T)_tmp;                                                                  \
})
#else
#define CONST_CAST(T, V) ((T)(V))
#endif

#define _Atomic(T) std::atomic<T>
using std::memory_order;
using std::memory_order_relaxed;
using std::memory_order_consume;
using std::memory_order_acquire;
using std::memory_order_release;
using std::memory_order_acq_rel;
using std::memory_order_seq_cst;

using std::atomic_init;
using std::atomic_load_explicit;
using std::atomic_store_explicit;
using std::atomic_fetch_add_explicit;
using std::atomic_fetch_sub_explicit;
using std::atomic_exchange_explicit;
using std::atomic_compare_exchange_strong_explicit;
using std::atomic_compare_exchange_weak_explicit;
using std::atomic_thread_fence;

#define almemory_order memory_order
#define almemory_order_relaxed memory_order_relaxed
#define almemory_order_consume memory_order_consume
#define almemory_order_acquire memory_order_acquire
#define almemory_order_release memory_order_release
#define almemory_order_acq_rel memory_order_acq_rel
#define almemory_order_seq_cst memory_order_seq_cst

#define ATOMIC(T)  _Atomic(T)

#define ATOMIC_INIT atomic_init
#define ATOMIC_INIT_STATIC ATOMIC_VAR_INIT

#define ATOMIC_LOAD atomic_load_explicit
#define ATOMIC_STORE atomic_store_explicit

#define ATOMIC_ADD atomic_fetch_add_explicit
#define ATOMIC_SUB atomic_fetch_sub_explicit

#define ATOMIC_EXCHANGE atomic_exchange_explicit
#define ATOMIC_COMPARE_EXCHANGE_STRONG atomic_compare_exchange_strong_explicit
#define ATOMIC_COMPARE_EXCHANGE_WEAK atomic_compare_exchange_weak_explicit

#define ATOMIC_THREAD_FENCE atomic_thread_fence


/* If no PTR xchg variants are provided, the normal ones can handle it. */
#ifndef ATOMIC_EXCHANGE_PTR
#define ATOMIC_EXCHANGE_PTR ATOMIC_EXCHANGE
#define ATOMIC_COMPARE_EXCHANGE_PTR_STRONG ATOMIC_COMPARE_EXCHANGE_STRONG
#define ATOMIC_COMPARE_EXCHANGE_PTR_WEAK ATOMIC_COMPARE_EXCHANGE_WEAK
#endif


#define ATOMIC_LOAD_SEQ(_val) ATOMIC_LOAD(_val, almemory_order_seq_cst)
#define ATOMIC_STORE_SEQ(_val, _newval) ATOMIC_STORE(_val, _newval, almemory_order_seq_cst)

#define ATOMIC_ADD_SEQ(_val, _incr) ATOMIC_ADD(_val, _incr, almemory_order_seq_cst)
#define ATOMIC_SUB_SEQ(_val, _decr) ATOMIC_SUB(_val, _decr, almemory_order_seq_cst)

#define ATOMIC_EXCHANGE_SEQ(_val, _newval) ATOMIC_EXCHANGE(_val, _newval, almemory_order_seq_cst)
#define ATOMIC_COMPARE_EXCHANGE_STRONG_SEQ(_val, _oldval, _newval) \
    ATOMIC_COMPARE_EXCHANGE_STRONG(_val, _oldval, _newval, almemory_order_seq_cst, almemory_order_seq_cst)
#define ATOMIC_COMPARE_EXCHANGE_WEAK_SEQ(_val, _oldval, _newval) \
    ATOMIC_COMPARE_EXCHANGE_WEAK(_val, _oldval, _newval, almemory_order_seq_cst, almemory_order_seq_cst)

#define ATOMIC_EXCHANGE_PTR_SEQ(_val, _newval) ATOMIC_EXCHANGE_PTR(_val, _newval, almemory_order_seq_cst)
#define ATOMIC_COMPARE_EXCHANGE_PTR_STRONG_SEQ(_val, _oldval, _newval) \
    ATOMIC_COMPARE_EXCHANGE_PTR_STRONG(_val, _oldval, _newval, almemory_order_seq_cst, almemory_order_seq_cst)
#define ATOMIC_COMPARE_EXCHANGE_PTR_WEAK_SEQ(_val, _oldval, _newval) \
    ATOMIC_COMPARE_EXCHANGE_PTR_WEAK(_val, _oldval, _newval, almemory_order_seq_cst, almemory_order_seq_cst)


typedef unsigned int uint;
typedef ATOMIC(uint) RefCount;

inline void InitRef(RefCount *ptr, uint value)
{ ATOMIC_INIT(ptr, value); }
inline uint ReadRef(RefCount *ptr)
{ return ATOMIC_LOAD(ptr, almemory_order_acquire); }
inline uint IncrementRef(RefCount *ptr)
{ return ATOMIC_ADD(ptr, 1u, almemory_order_acq_rel)+1; }
inline uint DecrementRef(RefCount *ptr)
{ return ATOMIC_SUB(ptr, 1u, almemory_order_acq_rel)-1; }


/* WARNING: A livelock is theoretically possible if another thread keeps
 * changing the head without giving this a chance to actually swap in the new
 * one (practically impossible with this little code, but...).
 */
#define ATOMIC_REPLACE_HEAD(T, _head, _entry) do {                            \
    T _first = ATOMIC_LOAD(_head, almemory_order_acquire);                    \
    do {                                                                      \
        ATOMIC_STORE(&(_entry)->next, _first, almemory_order_relaxed);        \
    } while(ATOMIC_COMPARE_EXCHANGE_PTR_WEAK(_head, &_first, _entry,          \
            almemory_order_acq_rel, almemory_order_acquire) == 0);            \
} while(0)

#endif /* AL_ATOMIC_H */
