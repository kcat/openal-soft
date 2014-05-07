#ifndef AL_RWLOCK_H
#define AL_RWLOCK_H

#include "bool.h"
#include "atomic.h"

typedef struct {
    volatile RefCount read_count;
    volatile RefCount write_count;
    volatile int read_lock;
    volatile int read_entry_lock;
    volatile int write_lock;
} RWLock;
#define RWLOCK_STATIC_INITIALIZE { 0, 0, false, false, false }

void RWLockInit(RWLock *lock);
void ReadLock(RWLock *lock);
void ReadUnlock(RWLock *lock);
void WriteLock(RWLock *lock);
void WriteUnlock(RWLock *lock);

#endif /* AL_RWLOCK_H */
