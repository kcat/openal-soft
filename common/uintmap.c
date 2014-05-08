
#include "config.h"

#include "uintmap.h"

#include <stdlib.h>
#include <string.h>


extern inline void LockUIntMapRead(UIntMap *map);
extern inline void UnlockUIntMapRead(UIntMap *map);
extern inline void LockUIntMapWrite(UIntMap *map);
extern inline void UnlockUIntMapWrite(UIntMap *map);


void InitUIntMap(UIntMap *map, ALsizei limit)
{
    map->array = NULL;
    map->size = 0;
    map->maxsize = 0;
    map->limit = limit;
    RWLockInit(&map->lock);
}

void ResetUIntMap(UIntMap *map)
{
    WriteLock(&map->lock);
    free(map->array);
    map->array = NULL;
    map->size = 0;
    map->maxsize = 0;
    WriteUnlock(&map->lock);
}

ALenum InsertUIntMapEntry(UIntMap *map, ALuint key, ALvoid *value)
{
    ALsizei pos = 0;

    WriteLock(&map->lock);
    if(map->size > 0)
    {
        ALsizei low = 0;
        ALsizei high = map->size - 1;
        while(low < high)
        {
            ALsizei mid = low + (high-low)/2;
            if(map->array[mid].key < key)
                low = mid + 1;
            else
                high = mid;
        }
        if(map->array[low].key < key)
            low++;
        pos = low;
    }

    if(pos == map->size || map->array[pos].key != key)
    {
        if(map->size == map->limit)
        {
            WriteUnlock(&map->lock);
            return AL_OUT_OF_MEMORY;
        }

        if(map->size == map->maxsize)
        {
            ALvoid *temp = NULL;
            ALsizei newsize;

            newsize = (map->maxsize ? (map->maxsize<<1) : 4);
            if(newsize >= map->maxsize)
                temp = realloc(map->array, newsize*sizeof(map->array[0]));
            if(!temp)
            {
                WriteUnlock(&map->lock);
                return AL_OUT_OF_MEMORY;
            }
            map->array = temp;
            map->maxsize = newsize;
        }

        if(pos < map->size)
            memmove(&map->array[pos+1], &map->array[pos],
                    (map->size-pos)*sizeof(map->array[0]));
        map->size++;
    }
    map->array[pos].key = key;
    map->array[pos].value = value;
    WriteUnlock(&map->lock);

    return AL_NO_ERROR;
}

ALvoid *RemoveUIntMapKey(UIntMap *map, ALuint key)
{
    ALvoid *ptr = NULL;
    WriteLock(&map->lock);
    if(map->size > 0)
    {
        ALsizei low = 0;
        ALsizei high = map->size - 1;
        while(low < high)
        {
            ALsizei mid = low + (high-low)/2;
            if(map->array[mid].key < key)
                low = mid + 1;
            else
                high = mid;
        }
        if(map->array[low].key == key)
        {
            ptr = map->array[low].value;
            if(low < map->size-1)
                memmove(&map->array[low], &map->array[low+1],
                        (map->size-1-low)*sizeof(map->array[0]));
            map->size--;
        }
    }
    WriteUnlock(&map->lock);
    return ptr;
}

ALvoid *LookupUIntMapKey(UIntMap *map, ALuint key)
{
    ALvoid *ptr = NULL;
    ReadLock(&map->lock);
    if(map->size > 0)
    {
        ALsizei low = 0;
        ALsizei high = map->size - 1;
        while(low < high)
        {
            ALsizei mid = low + (high-low)/2;
            if(map->array[mid].key < key)
                low = mid + 1;
            else
                high = mid;
        }
        if(map->array[low].key == key)
            ptr = map->array[low].value;
    }
    ReadUnlock(&map->lock);
    return ptr;
}
