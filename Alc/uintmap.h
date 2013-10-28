#ifndef AL_UINTMAP_H
#define AL_UINTMAP_H

#include "AL/al.h"
#include "rwlock.h"

typedef struct UIntMap {
    struct {
        ALuint key;
        ALvoid *value;
    } *array;
    ALsizei size;
    ALsizei maxsize;
    ALsizei limit;
    RWLock lock;
} UIntMap;
extern UIntMap TlsDestructor;

void InitUIntMap(UIntMap *map, ALsizei limit);
void ResetUIntMap(UIntMap *map);
ALenum InsertUIntMapEntry(UIntMap *map, ALuint key, ALvoid *value);
ALvoid *RemoveUIntMapKey(UIntMap *map, ALuint key);
ALvoid *LookupUIntMapKey(UIntMap *map, ALuint key);

static inline void LockUIntMapRead(UIntMap *map)
{ ReadLock(&map->lock); }
static inline void UnlockUIntMapRead(UIntMap *map)
{ ReadUnlock(&map->lock); }
static inline void LockUIntMapWrite(UIntMap *map)
{ WriteLock(&map->lock); }
static inline void UnlockUIntMapWrite(UIntMap *map)
{ WriteUnlock(&map->lock); }

#endif /* AL_UINTMAP_H */
