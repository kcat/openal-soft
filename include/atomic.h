#ifndef AL_ATOMIC_H
#define AL_ATOMIC_H

#include "static_assert.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *volatile XchgPtr;

typedef unsigned int uint;
typedef union {
    uint value;
} RefCount;

#define STATIC_REFCOUNT_INIT(V)  {(V)}

#if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 1)) && !defined(__QNXNTO__)

inline void InitRef(volatile RefCount *ptr, uint value)
{ ptr->value = value; }
inline uint ReadRef(volatile RefCount *ptr)
{ __sync_synchronize(); return ptr->value; }
inline uint IncrementRef(volatile RefCount *ptr)
{ return __sync_add_and_fetch(&ptr->value, 1); }
inline uint DecrementRef(volatile RefCount *ptr)
{ return __sync_sub_and_fetch(&ptr->value, 1); }
inline uint ExchangeRef(volatile RefCount *ptr, uint newval)
{ return __sync_lock_test_and_set(&ptr->value, newval); }
inline uint CompExchangeRef(volatile RefCount *ptr, uint oldval, uint newval)
{ return __sync_val_compare_and_swap(&ptr->value, oldval, newval); }

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

#define ATOMIC_EXCHANGE(T, _val, _newval)  __extension__({                    \
    static_assert(sizeof(T)==sizeof((_val).value), "Type "#T" has incorrect size!"); \
    T _r = __sync_lock_test_and_set(&(_val).value, (_newval));                \
    _r;                                                                       \
})
#define ATOMIC_COMPARE_EXCHANGE(T, _val, _oldval, _newval) __extension__({    \
    static_assert(sizeof(T)==sizeof((_val).value), "Type "#T" has incorrect size!"); \
    T _r = __sync_val_compare_and_swap(&(_val).value, (_oldval), (_newval));  \
    _r;                                                                       \
})

#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))

inline uint xaddl(volatile uint *dest, int incr)
{
    unsigned int ret;
    __asm__ __volatile__("lock; xaddl %0,(%1)"
                         : "=r" (ret)
                         : "r" (dest), "0" (incr)
                         : "memory");
    return ret;
}

inline void InitRef(volatile RefCount *ptr, uint value)
{ ptr->value = value; }
inline uint ReadRef(volatile RefCount *ptr)
{ __asm__ __volatile__("" ::: "memory"); return ptr->value; }
inline uint IncrementRef(volatile RefCount *ptr)
{ return xaddl(&ptr->value, 1)+1; }
inline uint DecrementRef(volatile RefCount *ptr)
{ return xaddl(&ptr->value, -1)-1; }
inline uint ExchangeRef(volatile RefCount *ptr, uint newval)
{
    int ret;
    __asm__ __volatile__("lock; xchgl %0,(%1)"
                         : "=r" (ret)
                         : "r" (&ptr->value), "0" (newval)
                         : "memory");
    return ret;
}
inline uint CompExchangeRef(volatile RefCount *ptr, uint oldval, uint newval)
{
    int ret;
    __asm__ __volatile__("lock; cmpxchgl %2,(%1)"
                         : "=a" (ret)
                         : "r" (&ptr->value), "r" (newval), "0" (oldval)
                         : "memory");
    return ret;
}

#define EXCHANGE(S, ret, dest, newval) __asm__ __volatile__(                  \
    "lock; xchg"S" %0,(%1)"                                                   \
    : "=r" (ret)                                                              \
    : "r" (dest), "0" (newval)                                                \
    : "memory"                                                                \
)
#define COMP_EXCHANGE(S, ret, dest, oldval, newval) __asm__ __volatile__(     \
    "lock; cmpxchg"S" %2,(%1)"                                                \
    : "=a" (ret)                                                              \
    : "r" (dest), "r" (newval), "0" (oldval)                                  \
    : "memory"                                                                \
)


inline int ExchangeInt(volatile int *dest, int newval)
{
    int ret;
    EXCHANGE("l", ret, dest, newval);
    return ret;
}
inline int CompExchangeInt(volatile int *dest, int oldval, int newval)
{
    int ret;
    COMP_EXCHANGE("l", ret, dest, oldval, newval);
    return ret;
}

#ifdef __i386__
inline void *ExchangePtr(XchgPtr *dest, void *newval)
{
    void *ret;
    EXCHANGE("l", ret, dest, newval);
    return ret;
}
inline void *CompExchangePtr(XchgPtr *dest, void *oldval, void *newval)
{
    void *ret;
    COMP_EXCHANGE("l", ret, dest, oldval, newval);
    return ret;
}
#else
inline void *ExchangePtr(XchgPtr *dest, void *newval)
{
    void *ret;
    EXCHANGE("q", ret, dest, newval);
    return ret;
}
inline void *CompExchangePtr(XchgPtr *dest, void *oldval, void *newval)
{
    void *ret;
    COMP_EXCHANGE("q", ret, dest, oldval, newval);
    return ret;
}
#endif


#define ATOMIC(T)  struct { T volatile value; }

#define ATOMIC_INIT_STATIC(_newval) {(_newval)}

#define ATOMIC_LOAD_UNSAFE(_val)  ((_val).value)
#define ATOMIC_STORE_UNSAFE(_val, _newval)  do {  \
    (_val).value = (_newval);                     \
} while(0)

#define ATOMIC_LOAD(_val)  (__asm__ __volatile__("" ::: "memory"),(_val).value)
#define ATOMIC_STORE(_val, _newval)  do {  \
    (_val).value = (_newval);              \
    __asm__ __volatile__("" ::: "memory"); \
} while(0)

#define ATOMIC_EXCHANGE(T, _val, _newval)  __extension__({                    \
    T _r;                                                                     \
    static_assert(sizeof(T)==sizeof((_val).value), "Type "#T" has incorrect size!"); \
    if(sizeof(T) == 4) EXCHANGE("l", _r, &(_val).value, (_newval));           \
    else if(sizeof(T) == 8) EXCHANGE("q", _r, &(_val).value, (_newval));      \
    _r;                                                                       \
})
#define ATOMIC_COMPARE_EXCHANGE(T, _val, _oldval, _newval) __extension__({    \
    T _r;                                                                     \
    static_assert(sizeof(T)==sizeof((_val).value), "Type "#T" has incorrect size!"); \
    if(sizeof(T) == 4) COMP_EXCHANGE("l", _r, &(_val).value, (_oldval), (_newval)); \
    else if(sizeof(T) == 8) COMP_EXCHANGE("q", _r, &(_val).value, (_oldval), (_newval)); \
    _r;                                                                       \
})

#elif defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static_assert(sizeof(LONG)==sizeof(uint), "sizeof LONG does not match sizeof uint");

inline void InitRef(volatile RefCount *ptr, uint value)
{ ptr->value = value; }
inline uint ReadRef(volatile RefCount *ptr)
{ _ReadBarrier(); return ptr->value; }
inline uint IncrementRef(volatile RefCount *ptr)
{
    union {
        volatile uint *u;
        volatile LONG *l;
    } u = { &ptr->value };
    return InterlockedIncrement(u.l);
}
inline uint DecrementRef(volatile RefCount *ptr)
{
    union {
        volatile uint *u;
        volatile LONG *l;
    } u = { &ptr->value };
    return InterlockedDecrement(u.l);
}
inline uint ExchangeRef(volatile RefCount *ptr, uint newval)
{
    union {
        volatile uint *i;
        volatile LONG *l;
    } u = { &ptr->value };
    return InterlockedExchange(u.l, newval);
}
inline uint CompExchangeRef(volatile RefCount *ptr, uint oldval, uint newval)
{
    union {
        volatile uint *i;
        volatile LONG *l;
    } u = { &ptr->value };
    return InterlockedCompareExchange(u.l, newval, oldval);
}

inline int ExchangeInt32(volatile int *ptr, int newval)
{
    union {
        volatile int *i;
        volatile LONG *l;
    } u = { ptr };
    return InterlockedExchange(u.l, newval);
}
inline int CompExchangeInt32(volatile int *ptr, int oldval, int newval)
{
    union {
        volatile int *i;
        volatile LONG *l;
    } u = { ptr };
    return InterlockedCompareExchange(u.l, newval, oldval);
}
inline __int64 ExchangeInt64(volatile __int64 *ptr, __int64 newval)
{
    union {
        volatile __int64 *i;
        volatile LONGLONG *l;
    } u = { ptr };
    return InterlockedExchange64(u.l, newval);
}
inline __int64 CompExchangeInt64(volatile __int64 *ptr, __int64 oldval, __int64 newval)
{
    union {
        volatile __int64 *i;
        volatile LONGLONG *l;
    } u = { ptr };
    return InterlockedCompareExchange64(u.l, newval, oldval);
}

inline int ExchangeInt(volatile int *ptr, int newval)
{ return ExchangeInt32(ptr, newval); }
inline int CompExchangeInt(volatile int *ptr, int oldval, int newval)
{ return CompExchangeInt32(ptr, oldval, newval); }

inline void *ExchangePtr(XchgPtr *ptr, void *newval)
{
    return InterlockedExchangePointer(ptr, newval);
}
inline void *CompExchangePtr(XchgPtr *ptr, void *oldval, void *newval)
{
    return InterlockedCompareExchangePointer(ptr, newval, oldval);
}


#define ATOMIC(T)  struct { T volatile value; }

#define ATOMIC_INIT_STATIC(_newval) {(_newval)}

#define ATOMIC_LOAD_UNSAFE(_val)  ((_val).value)
#define ATOMIC_STORE_UNSAFE(_val, _newval)  do {  \
    (_val).value = (_newval);                     \
} while(0)

#define ATOMIC_LOAD(_val)  (_ReadBarrier(),(_val).value)
#define ATOMIC_STORE(_val, _newval)  do {  \
    (_val).value = (_newval);              \
    _WriteBarrier();                       \
} while(0)

int _al_invalid_atomic_size(); /* not defined */

#define ATOMIC_FUNC_SELECT(T, C, F32, F64)  ((sizeof(T) == 4) ? (C)F32 : ((sizeof(T) == 8) ? (C)F64 : (C)_al_invalid_atomic_size))

#define ATOMIC_EXCHANGE(T, _val, _newval)  (ATOMIC_FUNC_SELECT(T, T(*)(volatile T*,T), ExchangeInt32, ExchangeInt64)(&(_val).value, (_newval)))
#define ATOMIC_COMPARE_EXCHANGE(T, _val, _oldval, _newval) (ATOMIC_FUNC_SELECT(T, T(*)(volatile T*,T,T), CompExchangeInt32, CompExchangeInt64)(&(_val).value, (_oldval), (_newval)))

#else
#error "No atomic functions available on this platform!"
#endif

#ifdef __cplusplus
}
#endif

#endif /* AL_ATOMIC_H */
