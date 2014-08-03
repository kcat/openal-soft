#ifndef AL_ATOMIC_H
#define AL_ATOMIC_H

#include "static_assert.h"
#include "bool.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *volatile XchgPtr;

/* Atomics using C11 */
#ifdef HAVE_C11_ATOMIC

#include <stdatomic.h>

inline int ExchangeInt(volatile int *ptr, int newval)
{ return atomic_exchange(ptr, newval); }
inline void *ExchangePtr(XchgPtr *ptr, void *newval)
{ return atomic_exchange(ptr, newval); }


#define ATOMIC(T)  struct { T _Atomic value; }

#define ATOMIC_INIT(_val, _newval) atomic_init(&(_val)->value, (_newval))
#define ATOMIC_INIT_STATIC(_newval) {ATOMIC_VAR_INIT(_newval)}

#define ATOMIC_LOAD(_val)            atomic_load(&(_val)->value)
#define ATOMIC_STORE(_val, _newval)  atomic_store(&(_val)->value, (_newval))

#define ATOMIC_ADD(T, _val, _incr) atomic_fetch_add(&(_val)->value, (_incr))
#define ATOMIC_SUB(T, _val, _decr) atomic_fetch_sub(&(_val)->value, (_decr))

#define ATOMIC_EXCHANGE(T, _val, _newval) atomic_exchange(&(_val)->value, (_newval))
#define ATOMIC_COMPARE_EXCHANGE_STRONG(T, _val, _oldval, _newval)             \
    atomic_compare_exchange_strong(&(_val)->value, (_oldval), (_newval))
#define ATOMIC_COMPARE_EXCHANGE_WEAK(T, _val, _oldval, _newval)               \
    atomic_compare_exchange_weak(&(_val)->value, (_oldval), (_newval))

/* Atomics using GCC intrinsics */
#elif defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 1)) && !defined(__QNXNTO__)

inline int ExchangeInt(volatile int *ptr, int newval)
{ return __sync_lock_test_and_set(ptr, newval); }
inline void *ExchangePtr(XchgPtr *ptr, void *newval)
{ return __sync_lock_test_and_set(ptr, newval); }


#define ATOMIC(T)  struct { T volatile value; }

#define ATOMIC_INIT(_val, _newval)  do { (_val)->value = (_newval); } while(0)
#define ATOMIC_INIT_STATIC(_newval) {(_newval)}

#define ATOMIC_LOAD(_val)  __extension__({      \
    __typeof((_val)->value) _r = (_val)->value; \
    __asm__ __volatile__("" ::: "memory");      \
    _r;                                         \
})
#define ATOMIC_STORE(_val, _newval)  do {  \
    __asm__ __volatile__("" ::: "memory"); \
    (_val)->value = (_newval);             \
} while(0)

#define ATOMIC_ADD(T, _val, _incr)  __extension__({                           \
    static_assert(sizeof(T)==sizeof((_val)->value), "Type "#T" has incorrect size!"); \
    __sync_fetch_and_add(&(_val)->value, (_incr));                            \
})
#define ATOMIC_SUB(T, _val, _decr)  __extension__({                           \
    static_assert(sizeof(T)==sizeof((_val)->value), "Type "#T" has incorrect size!"); \
    __sync_fetch_and_sub(&(_val)->value, (_decr));                            \
})

#define ATOMIC_EXCHANGE(T, _val, _newval)  __extension__({                    \
    static_assert(sizeof(T)==sizeof((_val)->value), "Type "#T" has incorrect size!"); \
    __sync_lock_test_and_set(&(_val)->value, (_newval));                      \
})
#define ATOMIC_COMPARE_EXCHANGE_STRONG(T, _val, _oldval, _newval) __extension__({ \
    static_assert(sizeof(T)==sizeof((_val)->value), "Type "#T" has incorrect size!"); \
    T _o = *(_oldval);                                                        \
    *(_oldval) = __sync_val_compare_and_swap(&(_val)->value, _o, (_newval));  \
    *(_oldval) == _o;                                                         \
})

/* Atomics using x86/x86-64 GCC inline assembly */
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))

#define WRAP_ADD(ret, dest, incr) __asm__ __volatile__(                       \
    "lock; xaddl %0,(%1)"                                                     \
    : "=r" (ret)                                                              \
    : "r" (dest), "0" (incr)                                                  \
    : "memory"                                                                \
)
#define WRAP_SUB(ret, dest, decr) __asm__ __volatile__(                       \
    "lock; xaddl %0,(%1)"                                                     \
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


inline int ExchangeInt(volatile int *dest, int newval)
{ int ret; WRAP_XCHG("l", ret, dest, newval); return ret; }

#ifdef __i386__
inline void *ExchangePtr(XchgPtr *dest, void *newval)
{ void *ret; WRAP_XCHG("l", ret, dest, newval); return ret; }
#else
inline void *ExchangePtr(XchgPtr *dest, void *newval)
{ void *ret; WRAP_XCHG("q", ret, dest, newval); return ret; }
#endif


#define ATOMIC(T)  struct { T volatile value; }

#define ATOMIC_INIT(_val, _newval)  do { (_val)->value = (_newval); } while(0)
#define ATOMIC_INIT_STATIC(_newval) {(_newval)}

#define ATOMIC_LOAD(_val)  __extension__({      \
    __typeof((_val)->value) _r = (_val)->value; \
    __asm__ __volatile__("" ::: "memory");      \
    _r;                                         \
})
#define ATOMIC_STORE(_val, _newval)  do {  \
    __asm__ __volatile__("" ::: "memory"); \
    (_val)->value = (_newval);             \
} while(0)

#define ATOMIC_ADD(T, _val, _incr)  __extension__({                           \
    static_assert(sizeof(T)==4, "Type "#T" has incorrect size!");             \
    static_assert(sizeof(T)==sizeof((_val)->value), "Type "#T" has incorrect size!"); \
    T _r;                                                                     \
    WRAP_ADD(_r, &(_val)->value, (T)(_incr));                                 \
    _r;                                                                       \
})
#define ATOMIC_SUB(T, _val, _decr)  __extension__({                           \
    static_assert(sizeof(T)==4, "Type "#T" has incorrect size!");             \
    static_assert(sizeof(T)==sizeof((_val)->value), "Type "#T" has incorrect size!"); \
    T _r;                                                                     \
    WRAP_SUB(_r, &(_val)->value, (T)(_decr));                                 \
    _r;                                                                       \
})

#define ATOMIC_EXCHANGE(T, _val, _newval)  __extension__({                    \
    static_assert(sizeof(T)==4 || sizeof(T)==8, "Type "#T" has incorrect size!"); \
    static_assert(sizeof(T)==sizeof((_val)->value), "Type "#T" has incorrect size!"); \
    T _r;                                                                     \
    if(sizeof(T) == 4) WRAP_XCHG("l", _r, &(_val)->value, (T)(_newval));      \
    else if(sizeof(T) == 8) WRAP_XCHG("q", _r, &(_val)->value, (T)(_newval)); \
    _r;                                                                       \
})
#define ATOMIC_COMPARE_EXCHANGE_STRONG(T, _val, _oldval, _newval) __extension__({ \
    static_assert(sizeof(T)==4 || sizeof(T)==8, "Type "#T" has incorrect size!"); \
    static_assert(sizeof(T)==sizeof((_val)->value), "Type "#T" has incorrect size!"); \
    T _old = *(_oldval);                                                      \
    if(sizeof(T) == 4) WRAP_CMPXCHG("l", *(_oldval), &(_val)->value, _old, (T)(_newval)); \
    else if(sizeof(T) == 8) WRAP_CMPXCHG("q", *(_oldval), &(_val)->value, _old, (T)(_newval)); \
    *(_oldval) == _old;                                                       \
})

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
inline LONG AtomicSub32(volatile LONG *dest, LONG decr)
{
    return InterlockedExchangeAdd(dest, -decr);
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

#define WRAP_ADDSUB(T, _func, _ptr, _amnt)  ((T(*)(T volatile*,T))_func)((_ptr), (_amnt))
#define WRAP_XCHG(T, _func, _ptr, _newval)  ((T(*)(T volatile*,T))_func)((_ptr), (_newval))
#define WRAP_CMPXCHG(T, _func, _ptr, _newval, _oldval) ((bool(*)(T volatile*,T,T*))_func)((_ptr), (_newval), (_oldval))

inline int ExchangeInt(volatile int *ptr, int newval)
{ return WRAP_XCHG(int,AtomicSwap32,ptr,newval); }

#ifdef _WIN64
inline void *ExchangePtr(XchgPtr *ptr, void *newval)
{ return WRAP_XCHG(void*,AtomicSwap64,ptr,newval); }
#else
inline void *ExchangePtr(XchgPtr *ptr, void *newval)
{ return WRAP_XCHG(void*,AtomicSwap32,ptr,newval); }
#endif


#define ATOMIC(T)  struct { T volatile value; }

#define ATOMIC_INIT(_val, _newval)  do { (_val)->value = (_newval); } while(0)
#define ATOMIC_INIT_STATIC(_newval) {(_newval)}

#define ATOMIC_LOAD(_val)  ((_val)->value)
#define ATOMIC_STORE(_val, _newval)  do {  \
    (_val)->value = (_newval);             \
} while(0)

int _al_invalid_atomic_size(); /* not defined */

#define ATOMIC_ADD(T, _val, _incr)                                            \
    ((sizeof(T)==4) ? WRAP_ADDSUB(T, AtomicAdd32, &(_val)->value, (_incr)) :  \
     (T)_al_invalid_atomic_size())
#define ATOMIC_SUB(T, _val, _decr)                                            \
    ((sizeof(T)==4) ? WRAP_ADDSUB(T, AtomicSub32, &(_val)->value, (_decr)) :  \
     (T)_al_invalid_atomic_size())

#define ATOMIC_EXCHANGE(T, _val, _newval)                                     \
    ((sizeof(T)==4) ? WRAP_XCHG(T, AtomicSwap32, &(_val)->value, (_newval)) : \
     (sizeof(T)==8) ? WRAP_XCHG(T, AtomicSwap64, &(_val)->value, (_newval)) : \
     (T)_al_invalid_atomic_size())
#define ATOMIC_COMPARE_EXCHANGE_STRONG(T, _val, _oldval, _newval)             \
    ((sizeof(T)==4) ? WRAP_CMPXCHG(T, CompareAndSwap32, &(_val)->value, (_newval), (_oldval)) : \
     (sizeof(T)==8) ? WRAP_CMPXCHG(T, CompareAndSwap64, &(_val)->value, (_newval), (_oldval)) : \
     (bool)_al_invalid_atomic_size())

#else

#error "No atomic functions available on this platform!"

#define ATOMIC(T)  T

#define ATOMIC_INIT_STATIC(_newval) (0)

#define ATOMIC_LOAD_UNSAFE(_val)  (0)
#define ATOMIC_STORE_UNSAFE(_val, _newval)  ((void)0)

#define ATOMIC_LOAD(_val)  (0)
#define ATOMIC_STORE(_val, _newval)  ((void)0)

#define ATOMIC_ADD(T, _val, _incr)  (0)
#define ATOMIC_SUB(T, _val, _decr)  (0)

#define ATOMIC_EXCHANGE(T, _val, _newval)  (0)
#define ATOMIC_COMPARE_EXCHANGE_STRONG(T, _val, _oldval, _newval) (0)
#endif

/* If no weak cmpxchg is provided (not all systems will have one), substitute a
 * strong cmpxchg. */
#ifndef ATOMIC_COMPARE_EXCHANGE_WEAK
#define ATOMIC_COMPARE_EXCHANGE_WEAK(a, b, c, d) ATOMIC_COMPARE_EXCHANGE_STRONG(a, b, c, d)
#endif

/* This is *NOT* atomic, but is a handy utility macro to compare-and-swap non-
 * atomic variables. */
#define COMPARE_EXCHANGE(_val, _oldval, _newval)  ((*(_val) == *(_oldval)) ? ((*(_val)=(_newval)),true) : ((*(_oldval)=*(_val)),false))


typedef unsigned int uint;
typedef ATOMIC(uint) RefCount;

inline void InitRef(RefCount *ptr, uint value)
{ ATOMIC_INIT(ptr, value); }
inline uint ReadRef(RefCount *ptr)
{ return ATOMIC_LOAD(ptr); }
inline uint IncrementRef(RefCount *ptr)
{ return ATOMIC_ADD(uint, ptr, 1)+1; }
inline uint DecrementRef(RefCount *ptr)
{ return ATOMIC_SUB(uint, ptr, 1)-1; }

#ifdef __cplusplus
}
#endif

#endif /* AL_ATOMIC_H */
