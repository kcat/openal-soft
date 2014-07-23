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
inline int CompExchangeInt(volatile int *ptr, int oldval, int newval)
{ atomic_compare_exchange_strong(ptr, &oldval, newval); return oldval; }
inline void *CompExchangePtr(XchgPtr *ptr, void *oldval, void *newval)
{ atomic_compare_exchange_strong(ptr, &oldval, newval); return oldval; }


#define ATOMIC(T)  struct { T _Atomic value; }

#define ATOMIC_INIT_STATIC(_newval) {ATOMIC_VAR_INIT(_newval)}

#define ATOMIC_LOAD_UNSAFE(_val)           atomic_load_explicit(&(_val).value, memory_order_relaxed)
#define ATOMIC_STORE_UNSAFE(_val, _newval) atomic_store_explicit(&(_val).value, (_newval), memory_order_relaxed)

#define ATOMIC_LOAD(_val)            atomic_load(&(_val).value)
#define ATOMIC_STORE(_val, _newval)  atomic_store(&(_val).value, (_newval))

#define ATOMIC_ADD(T, _val, _incr) atomic_fetch_add(&(_val).value, (_incr))
#define ATOMIC_SUB(T, _val, _decr) atomic_fetch_sub(&(_val).value, (_decr))

#define ATOMIC_EXCHANGE(T, _val, _newval) atomic_exchange(&(_val).value, (_newval))
#define ATOMIC_COMPARE_EXCHANGE(T, _val, _oldval, _newval)                    \
    atomic_compare_exchange_strong(&(_val).value, &(_oldval), (_newval))

/* Atomics using GCC intrinsics */
#elif defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 1)) && !defined(__QNXNTO__)

inline int ExchangeInt(volatile int *ptr, int newval)
{ return __sync_lock_test_and_set(ptr, newval); }
inline void *ExchangePtr(XchgPtr *ptr, void *newval)
{ return __sync_lock_test_and_set(ptr, newval); }
inline int CompExchangeInt(volatile int *ptr, int oldval, int newval)
{ return __sync_val_compare_and_swap(ptr, oldval, newval); }
inline void *CompExchangePtr(XchgPtr *ptr, void *oldval, void *newval)
{ return __sync_val_compare_and_swap(ptr, oldval, newval); }


#define ATOMIC(T)  struct { T volatile value; }

#define ATOMIC_INIT_STATIC(_newval) {(_newval)}

#define ATOMIC_LOAD_UNSAFE(_val)  ((_val).value)
#define ATOMIC_STORE_UNSAFE(_val, _newval)  do {  \
    (_val).value = (_newval);                     \
} while(0)

#define ATOMIC_LOAD(_val)  (__sync_synchronize(),(_val).value)
#define ATOMIC_STORE(_val, _newval)  do {  \
    (_val).value = (_newval);              \
    __sync_synchronize();                  \
} while(0)

#define ATOMIC_ADD(T, _val, _incr)  __extension__({                           \
    static_assert(sizeof(T)==sizeof((_val).value), "Type "#T" has incorrect size!"); \
    __sync_fetch_and_add(&(_val).value, (_incr));                             \
})
#define ATOMIC_SUB(T, _val, _decr)  __extension__({                           \
    static_assert(sizeof(T)==sizeof((_val).value), "Type "#T" has incorrect size!"); \
    __sync_fetch_and_sub(&(_val).value, (_decr));                             \
})

#define ATOMIC_EXCHANGE(T, _val, _newval)  __extension__({                    \
    static_assert(sizeof(T)==sizeof((_val).value), "Type "#T" has incorrect size!"); \
    __sync_lock_test_and_set(&(_val).value, (_newval));                       \
})
#define ATOMIC_COMPARE_EXCHANGE(T, _val, _oldval, _newval) __extension__({    \
    static_assert(sizeof(T)==sizeof((_val).value), "Type "#T" has incorrect size!"); \
    __typeof(_oldval) _old = (_oldval);                                       \
    (_oldval) = __sync_val_compare_and_swap(&(_val).value, (_oldval), (_newval)); \
    (_oldval) == _old;                                                        \
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
inline int CompExchangeInt(volatile int *dest, int oldval, int newval)
{ int ret; WRAP_CMPXCHG("l", ret, dest, oldval, newval); return ret; }

#ifdef __i386__
inline void *ExchangePtr(XchgPtr *dest, void *newval)
{ void *ret; WRAP_XCHG("l", ret, dest, newval); return ret; }
inline void *CompExchangePtr(XchgPtr *dest, void *oldval, void *newval)
{ void *ret; WRAP_CMPXCHG("l", ret, dest, oldval, newval); return ret; }
#else
inline void *ExchangePtr(XchgPtr *dest, void *newval)
{ void *ret; WRAP_XCHG("q", ret, dest, newval); return ret; }
inline void *CompExchangePtr(XchgPtr *dest, void *oldval, void *newval)
{ void *ret; WRAP_CMPXCHG("q", ret, dest, oldval, newval); return ret; }
#endif


#define ATOMIC(T)  struct { T volatile value; }

#define ATOMIC_INIT_STATIC(_newval) {(_newval)}

#define ATOMIC_LOAD_UNSAFE(_val)  ((_val).value)
#define ATOMIC_STORE_UNSAFE(_val, _newval)  do {  \
    (_val).value = (_newval);                     \
} while(0)

inline void _al_mem_barrier(void)
{ __asm__ __volatile__("" ::: "memory"); }

#define ATOMIC_LOAD(_val)  (_al_mem_barrier(),(_val).value)
#define ATOMIC_STORE(_val, _newval)  do {  \
    (_val).value = (_newval);              \
    _al_mem_barrier();                     \
} while(0)

#define ATOMIC_ADD(T, _val, _incr)  __extension__({                           \
    static_assert(sizeof(T)==4, "Type "#T" has incorrect size!");             \
    static_assert(sizeof(T)==sizeof((_val).value), "Type "#T" has incorrect size!"); \
    T _r;                                                                     \
    WRAP_ADD(_r, &(_val).value, (_incr));                                     \
    _r;                                                                       \
})
#define ATOMIC_SUB(T, _val, _decr)  __extension__({                           \
    static_assert(sizeof(T)==4, "Type "#T" has incorrect size!");             \
    static_assert(sizeof(T)==sizeof((_val).value), "Type "#T" has incorrect size!"); \
    T _r;                                                                     \
    WRAP_SUB(_r, &(_val).value, (_decr));                                     \
    _r;                                                                       \
})

#define ATOMIC_EXCHANGE(T, _val, _newval)  __extension__({                    \
    static_assert(sizeof(T)==4 || sizeof(T)==8, "Type "#T" has incorrect size!"); \
    static_assert(sizeof(T)==sizeof((_val).value), "Type "#T" has incorrect size!"); \
    T _r;                                                                     \
    if(sizeof(T) == 4) WRAP_XCHG("l", _r, &(_val).value, (_newval));          \
    else if(sizeof(T) == 8) WRAP_XCHG("q", _r, &(_val).value, (_newval));     \
    _r;                                                                       \
})
#define ATOMIC_COMPARE_EXCHANGE(T, _val, _oldval, _newval) __extension__({    \
    static_assert(sizeof(T)==4 || sizeof(T)==8, "Type "#T" has incorrect size!"); \
    static_assert(sizeof(T)==sizeof((_val).value), "Type "#T" has incorrect size!"); \
    __typeof(_oldval) _old = (_oldval);                                       \
    if(sizeof(T) == 4) WRAP_CMPXCHG("l", (_oldval), &(_val).value, (_oldval), (_newval)); \
    else if(sizeof(T) == 8) WRAP_CMPXCHG("q", (_oldval), &(_val).value, (_oldval), (_newval)); \
    (_oldval) == _old;                                                        \
})

/* Atomics using Windows methods */
#elif defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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

#define RAW_CAST(T1, T2, _val)  (((union{T2 from; T1 to;}){.from=(_val)}).to)
#define WRAP_ADD(T1, T2, _func, _ptr, _incr)  RAW_CAST(T2,T1,_func(RAW_CAST(T1 volatile*,T2 volatile*,(_ptr)), RAW_CAST(T1,T2,(_incr))))
#define WRAP_SUB(T1, T2, _func, _ptr, _decr)  RAW_CAST(T2,T1,_func(RAW_CAST(T1 volatile*,T2 volatile*,(_ptr)), -RAW_CAST(T1,T2,(_decr))))
#define WRAP_XCHG(T1, T2, _func, _ptr, _newval)  RAW_CAST(T2,T1,_func(RAW_CAST(T1 volatile*,T2 volatile*,(_ptr)), RAW_CAST(T1,T2,(_newval))))
#define WRAP_CMPXCHG(T1, T2, _func, _ptr, _newval, _oldval)  _func(RAW_CAST(T1 volatile*,T2 volatile*,(_ptr)), RAW_CAST(T1,T2,(_newval)), RAW_CAST(T1*,T2*,(_oldval)))

inline int ExchangeInt(volatile int *ptr, int newval)
{ return WRAP_XCHG(LONG,int,InterlockedExchange,ptr,newval); }
inline int CompExchangeInt(volatile int *ptr, int oldval, int newval)
{ WRAP_CMPXCHG(LONG,int,CompareAndSwap32,ptr,newval,&oldval); return oldval; }

#ifdef _WIN64
inline void *ExchangePtr(XchgPtr *ptr, void *newval)
{ return WRAP_XCHG(LONGLONG,void*,InterlockedExchange64,ptr,newval); }
inline void *CompExchangePtr(XchgPtr *ptr, void *oldval, void *newval)
{ WRAP_CMPXCHG(LONGLONG,void*,CompareAndSwap64,ptr,newval,&oldval); return oldval; }
#else
inline void *ExchangePtr(XchgPtr *ptr, void *newval)
{ return WRAP_XCHG(LONG,void*,InterlockedExchange,ptr,newval); }
inline void *CompExchangePtr(XchgPtr *ptr, void *oldval, void *newval)
{ WRAP_CMPXCHG(LONG,void*,CompareAndSwap32,ptr,newval,&oldval); return oldval; }
#endif


#define ATOMIC(T)  struct { T volatile value; }

#define ATOMIC_INIT_STATIC(_newval) {(_newval)}

#define ATOMIC_LOAD_UNSAFE(_val)  ((_val).value)
#define ATOMIC_STORE_UNSAFE(_val, _newval)  do {  \
    (_val).value = (_newval);                     \
} while(0)

inline void _al_mem_barrier(void) { _ReadBarrier(); }

#define ATOMIC_LOAD(_val)  (_al_mem_barrier(),(_val).value)
#define ATOMIC_STORE(_val, _newval)  do {  \
    (_val).value = (_newval);              \
    _WriteBarrier();                       \
} while(0)

int _al_invalid_atomic_size(); /* not defined */

#define ATOMIC_ADD(T, _val, _incr)                                            \
    ((sizeof(T)==4) ? WRAP_ADD(LONG, T, InterlockedExchangeAdd, &(_val).value, (_incr)) : \
     (T)_al_invalid_atomic_size())
#define ATOMIC_SUB(T, _val, _decr)                                            \
    ((sizeof(T)==4) ? WRAP_SUB(LONG, T, InterlockedExchangeAdd, &(_val).value, (_decr)) : \
     (T)_al_invalid_atomic_size())

#define ATOMIC_EXCHANGE(T, _val, _newval)                                     \
    ((sizeof(T)==4) ? WRAP_XCHG(LONG, T, InterlockedExchange, &(_val).value, (_newval)) : \
     (sizeof(T)==8) ? WRAP_XCHG(LONGLONG, T, InterlockedExchange64, &(_val).value, (_newval)) : \
     (T)_al_invalid_atomic_size())
#define ATOMIC_COMPARE_EXCHANGE(T, _val, _oldval, _newval)                    \
    ((sizeof(T)==4) ? WRAP_CMPXCHG(LONG, T, CompareAndSwap32, &(_val).value, (_newval), &(_oldval)) : \
     (sizeof(T)==8) ? WRAP_CMPXCHG(LONGLONG, T, CompareAndSwap64, &(_val).value, (_newval), &(_oldval)) : \
     (bool)_al_invalid_atomic_size())

#else
#error "No atomic functions available on this platform!"
#endif


typedef unsigned int uint;
typedef ATOMIC(uint) RefCount;

inline void InitRef(RefCount *ptr, uint value)
{ ATOMIC_STORE_UNSAFE(*ptr, value); }
inline uint ReadRef(RefCount *ptr)
{ return ATOMIC_LOAD(*ptr); }
inline uint IncrementRef(RefCount *ptr)
{ return ATOMIC_ADD(uint, *ptr, 1)+1; }
inline uint DecrementRef(RefCount *ptr)
{ return ATOMIC_SUB(uint, *ptr, 1)-1; }
inline uint ExchangeRef(RefCount *ptr, uint newval)
{ return ATOMIC_EXCHANGE(uint, *ptr, newval); }
inline uint CompExchangeRef(RefCount *ptr, uint oldval, uint newval)
{ (void)ATOMIC_COMPARE_EXCHANGE(uint, *ptr, oldval, newval); return oldval; }

#ifdef __cplusplus
}
#endif

#endif /* AL_ATOMIC_H */
