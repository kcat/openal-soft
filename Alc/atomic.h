#ifndef AL_ATOMIC_H
#define AL_ATOMIC_H


typedef void *volatile XchgPtr;

#if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 1)) && !defined(__QNXNTO__)
typedef unsigned int RefCount;
inline RefCount IncrementRef(volatile RefCount *ptr)
{ return __sync_add_and_fetch(ptr, 1); }
inline RefCount DecrementRef(volatile RefCount *ptr)
{ return __sync_sub_and_fetch(ptr, 1); }

inline int ExchangeInt(volatile int *ptr, int newval)
{
    return __sync_lock_test_and_set(ptr, newval);
}
inline void *ExchangePtr(XchgPtr *ptr, void *newval)
{
    return __sync_lock_test_and_set(ptr, newval);
}
inline ALboolean CompExchangeInt(volatile int *ptr, int oldval, int newval)
{
    return __sync_bool_compare_and_swap(ptr, oldval, newval);
}
inline ALboolean CompExchangePtr(XchgPtr *ptr, void *oldval, void *newval)
{
    return __sync_bool_compare_and_swap(ptr, oldval, newval);
}

#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))

inline unsigned int xaddl(volatile unsigned int *dest, int incr)
{
    unsigned int ret;
    __asm__ __volatile__("lock; xaddl %0,(%1)"
                         : "=r" (ret)
                         : "r" (dest), "0" (incr)
                         : "memory");
    return ret;
}

typedef unsigned int RefCount;
inline RefCount IncrementRef(volatile RefCount *ptr)
{ return xaddl(ptr, 1)+1; }
inline RefCount DecrementRef(volatile RefCount *ptr)
{ return xaddl(ptr, -1)-1; }

inline int ExchangeInt(volatile int *dest, int newval)
{
    int ret;
    __asm__ __volatile__("lock; xchgl %0,(%1)"
                         : "=r" (ret)
                         : "r" (dest), "0" (newval)
                         : "memory");
    return ret;
}

inline ALboolean CompExchangeInt(volatile int *dest, int oldval, int newval)
{
    int ret;
    __asm__ __volatile__("lock; cmpxchgl %2,(%1)"
                         : "=a" (ret)
                         : "r" (dest), "r" (newval), "0" (oldval)
                         : "memory");
    return ret == oldval;
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

inline ALboolean CompExchangePtr(XchgPtr *dest, void *oldval, void *newval)
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
    return ret == oldval;
}

#elif defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef LONG RefCount;
inline RefCount IncrementRef(volatile RefCount *ptr)
{ return InterlockedIncrement(ptr); }
inline RefCount DecrementRef(volatile RefCount *ptr)
{ return InterlockedDecrement(ptr); }

extern ALbyte LONG_size_does_not_match_int[(sizeof(LONG)==sizeof(int))?1:-1];

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
inline ALboolean CompExchangeInt(volatile int *ptr, int oldval, int newval)
{
    union {
        volatile int *i;
        volatile LONG *l;
    } u = { ptr };
    return InterlockedCompareExchange(u.l, newval, oldval) == oldval;
}
inline ALboolean CompExchangePtr(XchgPtr *ptr, void *oldval, void *newval)
{
    return InterlockedCompareExchangePointer(ptr, newval, oldval) == oldval;
}

#elif defined(__APPLE__)

#include <libkern/OSAtomic.h>

typedef int32_t RefCount;
inline RefCount IncrementRef(volatile RefCount *ptr)
{ return OSAtomicIncrement32Barrier(ptr); }
inline RefCount DecrementRef(volatile RefCount *ptr)
{ return OSAtomicDecrement32Barrier(ptr); }

inline int ExchangeInt(volatile int *ptr, int newval)
{
    /* Really? No regular old atomic swap? */
    int oldval;
    do {
        oldval = *ptr;
    } while(!OSAtomicCompareAndSwap32Barrier(oldval, newval, ptr));
    return oldval;
}
inline void *ExchangePtr(XchgPtr *ptr, void *newval)
{
    void *oldval;
    do {
        oldval = *ptr;
    } while(!OSAtomicCompareAndSwapPtrBarrier(oldval, newval, ptr));
    return oldval;
}
inline ALboolean CompExchangeInt(volatile int *ptr, int oldval, int newval)
{
    return OSAtomicCompareAndSwap32Barrier(oldval, newval, ptr);
}
inline ALboolean CompExchangePtr(XchgPtr *ptr, void *oldval, void *newval)
{
    return OSAtomicCompareAndSwapPtrBarrier(oldval, newval, ptr);
}

#else
#error "No atomic functions available on this platform!"
typedef ALuint RefCount;
#endif

#endif /* AL_ATOMIC_H */
