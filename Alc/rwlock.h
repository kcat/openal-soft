#ifndef AL_RWLOCK_H
#define AL_RWLOCK_H

#include "AL/al.h"
#include "atomic.h"

typedef struct {
    volatile RefCount read_count;
    volatile RefCount write_count;
    volatile ALenum read_lock;
    volatile ALenum read_entry_lock;
    volatile ALenum write_lock;
} RWLock;

void RWLockInit(RWLock *lock);
void ReadLock(RWLock *lock);
void ReadUnlock(RWLock *lock);
void WriteLock(RWLock *lock);
void WriteUnlock(RWLock *lock);

#endif /* AL_RWLOCK_H */
