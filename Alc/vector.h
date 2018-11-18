#ifndef AL_VECTOR_H
#define AL_VECTOR_H

#include <cstring>
#include <vector>

#include "almalloc.h"


#define TYPEDEF_VECTOR(T, N) typedef struct {                                 \
    std::size_t Capacity;                                                     \
    std::size_t Size;                                                         \
    T Data[];                                                                 \
} _##N;                                                                       \
typedef _##N* N;                                                              \
typedef const _##N* const_##N;

#define VECTOR(T) struct {                                                    \
    std::size_t Capacity;                                                     \
    std::size_t Size;                                                         \
    T Data[];                                                                 \
}*

#define VECTOR_INIT(_x)       do { (_x) = nullptr; } while(0)
#define VECTOR_INIT_STATIC()  nullptr
#define VECTOR_DEINIT(_x)     do { al_free((_x)); (_x) = nullptr; } while(0)

template<typename T>
inline void do_vector_resize(T *&vec, std::size_t size, std::size_t cap)
{
    if(size > cap)
        cap = size;

    if(!vec && cap == 0)
        return;

    if((vec ? vec->Capacity : 0) < cap)
    {
        ptrdiff_t data_offset = vec ? (char*)(vec->Data) - (char*)(vec) : sizeof(*vec);
        size_t old_size = (vec ? vec->Size : 0);

        auto temp = static_cast<T*>(al_calloc(16, data_offset + sizeof(vec->Data[0])*cap));
        if(vec) std::memcpy(temp->Data, vec->Data, sizeof(vec->Data[0])*old_size);

        al_free(vec);
        vec = temp;
        vec->Capacity = cap;
    }
    vec->Size = size;
}
#define VECTOR_RESIZE(_x, _s, _c) do_vector_resize(_x, _s, _c)

#define VECTOR_CAPACITY(_x) ((_x) ? (_x)->Capacity : 0)
#define VECTOR_SIZE(_x)     ((_x) ? (_x)->Size : 0)

#define VECTOR_BEGIN(_x) ((_x) ? (_x)->Data + 0 : NULL)
#define VECTOR_END(_x)   ((_x) ? (_x)->Data + (_x)->Size : NULL)

#define VECTOR_PUSH_BACK(_x, _obj) do {      \
    std::size_t _pbsize = VECTOR_SIZE(_x)+1; \
    VECTOR_RESIZE(_x, _pbsize, _pbsize);     \
    (_x)->Data[(_x)->Size-1] = (_obj);       \
} while(0)
#define VECTOR_POP_BACK(_x) ((void)((_x)->Size--))

#define VECTOR_BACK(_x)  ((_x)->Data[(_x)->Size-1])
#define VECTOR_FRONT(_x) ((_x)->Data[0])

#define VECTOR_ELEM(_x, _o) ((_x)->Data[(_o)])

#define VECTOR_FOR_EACH(_t, _x, _f)  do {                                     \
    _t *_iter = VECTOR_BEGIN((_x));                                           \
    _t *_end = VECTOR_END((_x));                                              \
    for(;_iter != _end;++_iter)                                               \
        _f(_iter);                                                            \
} while(0)

#define VECTOR_FIND_IF(_i, _t, _x, _f)  do {                                  \
    _t *_iter = VECTOR_BEGIN((_x));                                           \
    _t *_end = VECTOR_END((_x));                                              \
    for(;_iter != _end;++_iter)                                               \
    {                                                                         \
        if(_f(_iter))                                                         \
            break;                                                            \
    }                                                                         \
    (_i) = _iter;                                                             \
} while(0)


namespace al {

template<typename T, size_t alignment=DEF_ALIGN>
using vector = std::vector<T, al::allocator<T, alignment>>;

} // namespace al

#endif /* AL_VECTOR_H */
