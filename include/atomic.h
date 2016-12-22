#ifndef AL_ATOMIC_H
#define AL_ATOMIC_H

#include "static_assert.h"
#include "bool.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Atomics using C11 */
#ifdef HAVE_C11_ATOMIC

#include <stdatomic.h>

#define almemory_order memory_order
#define almemory_order_relaxed memory_order_relaxed
#define almemory_order_consume memory_order_consume
#define almemory_order_acquire memory_order_acquire
#define almemory_order_release memory_order_release
#define almemory_order_acq_rel memory_order_acq_rel
#define almemory_order_seq_cst memory_order_seq_cst

#define ATOMIC(T)  T _Atomic
#define ATOMIC_FLAG atomic_flag

#define ATOMIC_INIT(_val, _newval)  atomic_init((_val), (_newval))
#define ATOMIC_INIT_STATIC(_newval) ATOMIC_VAR_INIT(_newval)
/*#define ATOMIC_FLAG_INIT ATOMIC_FLAG_INIT*/

#define ATOMIC_LOAD(_val, _MO)  atomic_load_explicit(_val, _MO)
#define ATOMIC_STORE(_val, _newval, _MO) atomic_store_explicit(_val, _newval, _MO)

#define ATOMIC_ADD(_val, _incr, _MO) atomic_fetch_add_explicit(_val, _incr, _MO)
#define ATOMIC_SUB(_val, _decr, _MO) atomic_fetch_sub_explicit(_val, _decr, _MO)

#define ATOMIC_EXCHANGE(T, _val, _newval, _MO) atomic_exchange_explicit(_val, _newval, _MO)
#define ATOMIC_COMPARE_EXCHANGE_STRONG(T, _val, _orig, _newval, _MO1, _MO2)   \
    atomic_compare_exchange_strong_explicit(_val, _orig, _newval, _MO1, _MO2)
#define ATOMIC_COMPARE_EXCHANGE_WEAK(T, _val, _orig, _newval, _MO1, _MO2)   \
    atomic_compare_exchange_weak_explicit(_val, _orig, _newval, _MO1, _MO2)

#define ATOMIC_FLAG_TEST_AND_SET(_val, _MO) atomic_flag_test_and_set_explicit(_val, _MO)
#define ATOMIC_FLAG_CLEAR(_val, _MO) atomic_flag_clear_explicit(_val, _MO)

#define ATOMIC_THREAD_FENCE atomic_thread_fence

/* Atomics using GCC intrinsics */
#elif defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 1)) && !defined(__QNXNTO__)

enum almemory_order {
    almemory_order_relaxed,
    almemory_order_consume,
    almemory_order_acquire,
    almemory_order_release,
    almemory_order_acq_rel,
    almemory_order_seq_cst
};

#define ATOMIC(T)  struct { T volatile value; }
#define ATOMIC_FLAG  ATOMIC(int)

#define ATOMIC_INIT(_val, _newval)  do { (_val)->value = (_newval); } while(0)
#define ATOMIC_INIT_STATIC(_newval) {(_newval)}
#define ATOMIC_FLAG_INIT            ATOMIC_INIT_STATIC(0)

#define ATOMIC_LOAD(_val, _MO)  __extension__({ \
    __typeof((_val)->value) _r = (_val)->value; \
    __asm__ __volatile__("" ::: "memory");      \
    _r;                                         \
})
#define ATOMIC_STORE(_val, _newval, _MO)  do { \
    __asm__ __volatile__("" ::: "memory");     \
    (_val)->value = (_newval);                 \
} while(0)

#define ATOMIC_ADD(_val, _incr, _MO) __sync_fetch_and_add(&(_val)->value, (_incr))
#define ATOMIC_SUB(_val, _decr, _MO) __sync_fetch_and_sub(&(_val)->value, (_decr))

#define ATOMIC_EXCHANGE(T, _val, _newval, _MO)  __extension__({               \
    static_assert(sizeof(T)==sizeof((_val)->value), "Type "#T" has incorrect size!"); \
    __asm__ __volatile__("" ::: "memory");                                    \
    __sync_lock_test_and_set(&(_val)->value, (_newval));                      \
})
#define ATOMIC_COMPARE_EXCHANGE_STRONG(T, _val, _oldval, _newval, _MO1, _MO2) __extension__({ \
    static_assert(sizeof(T)==sizeof((_val)->value), "Type "#T" has incorrect size!"); \
    T _o = *(_oldval);                                                        \
    *(_oldval) = __sync_val_compare_and_swap(&(_val)->value, _o, (_newval));  \
    *(_oldval) == _o;                                                         \
})

#define ATOMIC_FLAG_TEST_AND_SET(_val, _MO)  __extension__({                  \
    __asm__ __volatile__("" ::: "memory");                                    \
    __sync_lock_test_and_set(&(_val)->value, 1);                              \
})
#define ATOMIC_FLAG_CLEAR(_val, _MO)  __extension__({                         \
    __sync_lock_release(&(_val)->value);                                      \
    __asm__ __volatile__("" ::: "memory");                                    \
})


#define ATOMIC_THREAD_FENCE(order) do {        \
    enum { must_be_constant = (order) };       \
    const int _o = must_be_constant;           \
    if(_o > almemory_order_relaxed)            \
        __asm__ __volatile__("" ::: "memory"); \
} while(0)

/* Atomics using x86/x86-64 GCC inline assembly */
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))

#define WRAP_ADD(S, ret, dest, incr) __asm__ __volatile__(                    \
    "lock; xadd"S" %0,(%1)"                                                   \
    : "=r" (ret)                                                              \
    : "r" (dest), "0" (incr)                                                  \
    : "memory"                                                                \
)
#define WRAP_SUB(S, ret, dest, decr) __asm__ __volatile__(                    \
    "lock; xadd"S" %0,(%1)"                                                   \
    : "=r" (ret)                                                              \
    : "r" (dest), "0" (-(decr))                                               \
    : "memory"                                                                \
)

#define WRAP_XCHG(S, ret, dest, newval) __asm__ __volatile__(                 \
    "lock; xchg"S" %0,(%1)"                                                   \
    : "=r" (ret)                                                              \
    : "r" (dest), "0" (newval)                                                \
    : "memory"                                                                \
)
#define WRAP_CMPXCHG(S, ret, dest, oldval, newval) __asm__ __volatile__(      \
    "lock; cmpxchg"S" %2,(%1)"                                                \
    : "=a" (ret)                                                              \
    : "r" (dest), "r" (newval), "0" (oldval)                                  \
    : "memory"                                                                \
)


enum almemory_order {
    almemory_order_relaxed,
    almemory_order_consume,
    almemory_order_acquire,
    almemory_order_release,
    almemory_order_acq_rel,
    almemory_order_seq_cst
};

#define ATOMIC(T)  struct { T volatile value; }

#define ATOMIC_INIT(_val, _newval)  do { (_val)->value = (_newval); } while(0)
#define ATOMIC_INIT_STATIC(_newval) {(_newval)}

#define ATOMIC_LOAD(_val, _MO)  __extension__({ \
    __typeof((_val)->value) _r = (_val)->value; \
    __asm__ __volatile__("" ::: "memory");      \
    _r;                                         \
})
#define ATOMIC_STORE(_val, _newval, _MO)  do { \
    __asm__ __volatile__("" ::: "memory");     \
    (_val)->value = (_newval);                 \
} while(0)

#define ATOMIC_ADD(_val, _incr, _MO) __extension__({                          \
    static_assert(sizeof((_val)->value)==4 || sizeof((_val)->value)==8, "Unsupported size!"); \
    __typeof((_val)->value) _r;                                               \
    if(sizeof((_val)->value) == 4) WRAP_ADD("l", _r, &(_val)->value, _incr);  \
    else if(sizeof((_val)->value) == 8) WRAP_ADD("q", _r, &(_val)->value, _incr); \
    _r;                                                                       \
})
#define ATOMIC_SUB(_val, _decr, _MO) __extension__({                          \
    static_assert(sizeof((_val)->value)==4 || sizeof((_val)->value)==8, "Unsupported size!"); \
    __typeof((_val)->value) _r;                                               \
    if(sizeof((_val)->value) == 4) WRAP_SUB("l", _r, &(_val)->value, _decr);  \
    else if(sizeof((_val)->value) == 8) WRAP_SUB("q", _r, &(_val)->value, _decr); \
    _r;                                                                       \
})

#define ATOMIC_EXCHANGE(T, _val, _newval, _MO)  __extension__({               \
    static_assert(sizeof(T)==4 || sizeof(T)==8, "Type "#T" has incorrect size!"); \
    static_assert(sizeof(T)==sizeof((_val)->value), "Type "#T" has incorrect size!"); \
    T _r;                                                                     \
    if(sizeof(T) == 4) WRAP_XCHG("l", _r, &(_val)->value, (T)(_newval));      \
    else if(sizeof(T) == 8) WRAP_XCHG("q", _r, &(_val)->value, (T)(_newval)); \
    _r;                                                                       \
})
#define ATOMIC_COMPARE_EXCHANGE_STRONG(T, _val, _oldval, _newval, _MO1, _MO2) __extension__({ \
    static_assert(sizeof(T)==4 || sizeof(T)==8, "Type "#T" has incorrect size!"); \
    static_assert(sizeof(T)==sizeof((_val)->value), "Type "#T" has incorrect size!"); \
    T _old = *(_oldval);                                                      \
    if(sizeof(T) == 4) WRAP_CMPXCHG("l", *(_oldval), &(_val)->value, _old, (T)(_newval)); \
    else if(sizeof(T) == 8) WRAP_CMPXCHG("q", *(_oldval), &(_val)->value, _old, (T)(_newval)); \
    *(_oldval) == _old;                                                       \
})

#define ATOMIC_THREAD_FENCE(order) do {        \
    enum { must_be_constant = (order) };       \
    const int _o = must_be_constant;           \
    if(_o > almemory_order_relaxed)            \
        __asm__ __volatile__("" ::: "memory"); \
} while(0)

/* Atomics using Windows methods */
#elif defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* NOTE: This mess is *extremely* noisy, at least on GCC. It works by wrapping
 * Windows' 32-bit and 64-bit atomic methods, which are then casted to use the
 * given type based on its size (e.g. int and float use 32-bit atomics). This
 * is fine for the swap and compare-and-swap methods, although the add and
 * subtract methods only work properly for integer types.
 *
 * Despite how noisy it is, it's unfortunately the only way that doesn't rely
 * on C99 (damn MSVC).
 */

inline LONG AtomicAdd32(volatile LONG *dest, LONG incr)
{
    return InterlockedExchangeAdd(dest, incr);
}
inline LONGLONG AtomicAdd64(volatile LONGLONG *dest, LONGLONG incr)
{
    return InterlockedExchangeAdd64(dest, incr);
}
inline LONG AtomicSub32(volatile LONG *dest, LONG decr)
{
    return InterlockedExchangeAdd(dest, -decr);
}
inline LONGLONG AtomicSub64(volatile LONGLONG *dest, LONGLONG decr)
{
    return InterlockedExchangeAdd64(dest, -decr);
}

inline LONG AtomicSwap32(volatile LONG *dest, LONG newval)
{
    return InterlockedExchange(dest, newval);
}
inline LONGLONG AtomicSwap64(volatile LONGLONG *dest, LONGLONG newval)
{
    return InterlockedExchange64(dest, newval);
}

inline bool CompareAndSwap32(volatile LONG *dest, LONG newval, LONG *oldval)
{
    LONG old = *oldval;
    *oldval = InterlockedCompareExchange(dest, newval, *oldval);
    return old == *oldval;
}
inline bool CompareAndSwap64(volatile LONGLONG *dest, LONGLONG newval, LONGLONG *oldval)
{
    LONGLONG old = *oldval;
    *oldval = InterlockedCompareExchange64(dest, newval, *oldval);
    return old == *oldval;
}

#define WRAP_ADDSUB(T, _func, _ptr, _amnt)  _func((T volatile*)(_ptr), (_amnt))
#define WRAP_XCHG(T, _func, _ptr, _newval)  ((T(*)(T volatile*,T))_func)((_ptr), (_newval))
#define WRAP_CMPXCHG(T, _func, _ptr, _newval, _oldval) ((bool(*)(T volatile*,T,T*))_func)((_ptr), (_newval), (_oldval))


enum almemory_order {
    almemory_order_relaxed,
    almemory_order_consume,
    almemory_order_acquire,
    almemory_order_release,
    almemory_order_acq_rel,
    almemory_order_seq_cst
};

#define ATOMIC(T)  struct { T volatile value; }

#define ATOMIC_INIT(_val, _newval)  do { (_val)->value = (_newval); } while(0)
#define ATOMIC_INIT_STATIC(_newval) {(_newval)}

#define ATOMIC_LOAD(_val, _MO)  ((_val)->value)
#define ATOMIC_STORE(_val, _newval, _MO)  do {  \
    (_val)->value = (_newval);                  \
} while(0)

int _al_invalid_atomic_size(); /* not defined */

#define ATOMIC_ADD(_val, _incr, _MO)                                          \
    ((sizeof((_val)->value)==4) ? WRAP_ADDSUB(LONG, AtomicAdd32, &(_val)->value, (_incr)) : \
     (sizeof((_val)->value)==8) ? WRAP_ADDSUB(LONGLONG, AtomicAdd64, &(_val)->value, (_incr)) : \
     _al_invalid_atomic_size())
#define ATOMIC_SUB(_val, _decr, _MO)                                          \
    ((sizeof((_val)->value)==4) ? WRAP_ADDSUB(LONG, AtomicSub32, &(_val)->value, (_decr)) : \
     (sizeof((_val)->value)==8) ? WRAP_ADDSUB(LONGLONG, AtomicSub64, &(_val)->value, (_decr)) : \
     _al_invalid_atomic_size())

#define ATOMIC_EXCHANGE(T, _val, _newval, _MO)                                \
    ((sizeof(T)==4) ? WRAP_XCHG(T, AtomicSwap32, &(_val)->value, (_newval)) : \
     (sizeof(T)==8) ? WRAP_XCHG(T, AtomicSwap64, &(_val)->value, (_newval)) : \
     (T)_al_invalid_atomic_size())
#define ATOMIC_COMPARE_EXCHANGE_STRONG(T, _val, _oldval, _newval, _MO1, _MO2) \
    ((sizeof(T)==4) ? WRAP_CMPXCHG(T, CompareAndSwap32, &(_val)->value, (_newval), (_oldval)) : \
     (sizeof(T)==8) ? WRAP_CMPXCHG(T, CompareAndSwap64, &(_val)->value, (_newval), (_oldval)) : \
     (bool)_al_invalid_atomic_size())

#define ATOMIC_THREAD_FENCE(order) do {        \
    enum { must_be_constant = (order) };       \
    const int _o = must_be_constant;           \
    if(_o > almemory_order_relaxed)            \
        _ReadWriteBarrier();                   \
} while(0)

#else

#error "No atomic functions available on this platform!"

#define ATOMIC(T)  T

#define ATOMIC_INIT(_val, _newval)  ((void)0)
#define ATOMIC_INIT_STATIC(_newval) (0)

#define ATOMIC_LOAD(...)   (0)
#define ATOMIC_STORE(...)  ((void)0)

#define ATOMIC_ADD(...) (0)
#define ATOMIC_SUB(...) (0)

#define ATOMIC_EXCHANGE(T, ...) (0)
#define ATOMIC_COMPARE_EXCHANGE_STRONG(T, ...) (0)
#define ATOMIC_COMPARE_EXCHANGE_WEAK(T, ...) (0)

#define ATOMIC_FLAG_TEST_AND_SET(...) (0)
#define ATOMIC_FLAG_CLEAR(...) ((void)0)

#define ATOMIC_THREAD_FENCE(...) ((void)0)
#endif

/* If no weak cmpxchg is provided (not all systems will have one), substitute a
 * strong cmpxchg. */
#ifndef ATOMIC_COMPARE_EXCHANGE_WEAK
#define ATOMIC_COMPARE_EXCHANGE_WEAK ATOMIC_COMPARE_EXCHANGE_STRONG
#endif

/* If no ATOMIC_FLAG is defined, simulate one with an atomic int using exchange
 * and store ops.
 */
#ifndef ATOMIC_FLAG
#define ATOMIC_FLAG      ATOMIC(int)
#define ATOMIC_FLAG_INIT ATOMIC_INIT_STATIC(0)
#define ATOMIC_FLAG_TEST_AND_SET(_val, _MO) ATOMIC_EXCHANGE(int, _val, 1, _MO)
#define ATOMIC_FLAG_CLEAR(_val, _MO)        ATOMIC_STORE(_val, 0, _MO)
#endif


#define ATOMIC_LOAD_SEQ(_val) ATOMIC_LOAD(_val, almemory_order_seq_cst)
#define ATOMIC_STORE_SEQ(_val, _newval) ATOMIC_STORE(_val, _newval, almemory_order_seq_cst)

#define ATOMIC_ADD_SEQ(_val, _incr) ATOMIC_ADD(_val, _incr, almemory_order_seq_cst)
#define ATOMIC_SUB_SEQ(_val, _decr) ATOMIC_SUB(_val, _decr, almemory_order_seq_cst)

#define ATOMIC_EXCHANGE_SEQ(T, _val, _newval) ATOMIC_EXCHANGE(T, _val, _newval, almemory_order_seq_cst)
#define ATOMIC_COMPARE_EXCHANGE_STRONG_SEQ(T, _val, _oldval, _newval) \
    ATOMIC_COMPARE_EXCHANGE_STRONG(T, _val, _oldval, _newval, almemory_order_seq_cst, almemory_order_seq_cst)
#define ATOMIC_COMPARE_EXCHANGE_WEAK_SEQ(T, _val, _oldval, _newval) \
    ATOMIC_COMPARE_EXCHANGE_WEAK(T, _val, _oldval, _newval, almemory_order_seq_cst, almemory_order_seq_cst)


typedef unsigned int uint;
typedef ATOMIC(uint) RefCount;

inline void InitRef(RefCount *ptr, uint value)
{ ATOMIC_INIT(ptr, value); }
inline uint ReadRef(RefCount *ptr)
{ return ATOMIC_LOAD_SEQ(ptr); }
inline uint IncrementRef(RefCount *ptr)
{ return ATOMIC_ADD_SEQ(ptr, 1)+1; }
inline uint DecrementRef(RefCount *ptr)
{ return ATOMIC_SUB_SEQ(ptr, 1)-1; }


/* WARNING: A livelock is theoretically possible if another thread keeps
 * changing the head without giving this a chance to actually swap in the new
 * one (practically impossible with this little code, but...).
 */
#define ATOMIC_REPLACE_HEAD(T, _head, _entry) do {                            \
    T _first = ATOMIC_LOAD(_head, almemory_order_acquire);                    \
    do {                                                                      \
        ATOMIC_STORE(&(_entry)->next, _first, almemory_order_relaxed);        \
    } while(ATOMIC_COMPARE_EXCHANGE_WEAK(T, _head, &_first, _entry,           \
            almemory_order_acq_rel, almemory_order_acquire) == 0);            \
} while(0)

#ifdef __cplusplus
}
#endif

#endif /* AL_ATOMIC_H */
