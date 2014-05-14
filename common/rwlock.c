
#include "config.h"

#include "rwlock.h"

#include "bool.h"
#include "atomic.h"
#include "threads.h"


/* A simple spinlock. Yield the thread while the given integer is set by
 * another. Could probably be improved... */
static void Lock(volatile int *l)
{
    while(ExchangeInt(l, true) == true)
        althrd_yield();
}

static void Unlock(volatile int *l)
{
    ExchangeInt(l, false);
}


void RWLockInit(RWLock *lock)
{
    InitRef(&lock->read_count, 0);
    InitRef(&lock->write_count, 0);
    lock->read_lock = false;
    lock->read_entry_lock = false;
    lock->write_lock = false;
}

void ReadLock(RWLock *lock)
{
    Lock(&lock->read_entry_lock);
    Lock(&lock->read_lock);
    if(IncrementRef(&lock->read_count) == 1)
        Lock(&lock->write_lock);
    Unlock(&lock->read_lock);
    Unlock(&lock->read_entry_lock);
}

void ReadUnlock(RWLock *lock)
{
    if(DecrementRef(&lock->read_count) == 0)
        Unlock(&lock->write_lock);
}

void WriteLock(RWLock *lock)
{
    if(IncrementRef(&lock->write_count) == 1)
        Lock(&lock->read_lock);
    Lock(&lock->write_lock);
}

void WriteUnlock(RWLock *lock)
{
    Unlock(&lock->write_lock);
    if(DecrementRef(&lock->write_count) == 0)
        Unlock(&lock->read_lock);
}
