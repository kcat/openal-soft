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

inline int ExchangeInt(volatile int *dest, int newval)
{
    int ret;
    __asm__ __volatile__("lock; xchgl %0,(%1)"
                         : "=r" (ret)
                         : "r" (dest), "0" (newval)
                         : "memory");
    return ret;
}
inline void *ExchangePtr(XchgPtr *dest, void *newval)
{
    void *ret;
    __asm__ __volatile__(
#ifdef __i386__
                         "lock; xchgl %0,(%1)"
#else
                         "lock; xchgq %0,(%1)"
#endif
                         : "=r" (ret)
                         : "r" (dest), "0" (newval)
                         : "memory"
    );
    return ret;
}
inline int CompExchangeInt(volatile int *dest, int oldval, int newval)
{
    int ret;
    __asm__ __volatile__("lock; cmpxchgl %2,(%1)"
                         : "=a" (ret)
                         : "r" (dest), "r" (newval), "0" (oldval)
                         : "memory");
    return ret;
}
inline void *CompExchangePtr(XchgPtr *dest, void *oldval, void *newval)
{
    void *ret;
    __asm__ __volatile__(
#ifdef __i386__
                         "lock; cmpxchgl %2,(%1)"
#else
                         "lock; cmpxchgq %2,(%1)"
#endif
                         : "=a" (ret)
                         : "r" (dest), "r" (newval), "0" (oldval)
                         : "memory"
    );
    return ret;
}

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

inline int ExchangeInt(volatile int *ptr, int newval)
{
    union {
        volatile int *i;
        volatile LONG *l;
    } u = { ptr };
    return InterlockedExchange(u.l, newval);
}
inline void *ExchangePtr(XchgPtr *ptr, void *newval)
{
    return InterlockedExchangePointer(ptr, newval);
}
inline int CompExchangeInt(volatile int *ptr, int oldval, int newval)
{
    union {
        volatile int *i;
        volatile LONG *l;
    } u = { ptr };
    return InterlockedCompareExchange(u.l, newval, oldval);
}
inline void *CompExchangePtr(XchgPtr *ptr, void *oldval, void *newval)
{
    return InterlockedCompareExchangePointer(ptr, newval, oldval);
}

#else
#error "No atomic functions available on this platform!"
#endif

#ifdef __cplusplus
}
#endif

#endif /* AL_ATOMIC_H */
