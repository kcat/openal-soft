#ifndef AL_RWLOCK_H
#define AL_RWLOCK_H

#include "bool.h"
#include "atomic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    RefCount read_count;
    RefCount write_count;
    volatile int read_lock;
    volatile int read_entry_lock;
    volatile int write_lock;
} RWLock;
#define RWLOCK_STATIC_INITIALIZE { STATIC_REFCOUNT_INIT(0), STATIC_REFCOUNT_INIT(0), false, false, false }

void RWLockInit(RWLock *lock);
void ReadLock(RWLock *lock);
void ReadUnlock(RWLock *lock);
void WriteLock(RWLock *lock);
void WriteUnlock(RWLock *lock);

#ifdef __cplusplus
}
#endif

#endif /* AL_RWLOCK_H */
