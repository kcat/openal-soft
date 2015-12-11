#ifndef AL_VECTOR_H
#define AL_VECTOR_H

#include <stdlib.h>

#include <AL/al.h>

/* "Base" vector type, designed to alias with the actual vector types. */
typedef struct vector__s {
    size_t Capacity;
    size_t Size;
} *vector_;

#define TYPEDEF_VECTOR(T, N) typedef struct {                                 \
    size_t Capacity;                                                          \
    size_t Size;                                                              \
    T Data[];                                                                 \
} _##N;                                                                       \
typedef _##N* N;                                                              \
typedef const _##N* const_##N;

#define VECTOR(T) struct {                                                    \
    size_t Capacity;                                                          \
    size_t Size;                                                              \
    T Data[];                                                                 \
}*

#define VECTOR_INIT(_x)       do { (_x) = NULL; } while(0)
#define VECTOR_INIT_STATIC()  NULL
#define VECTOR_DEINIT(_x)     do { free((_x)); (_x) = NULL; } while(0)

/* Helper to increase a vector's reserve. Do not call directly. */
ALboolean vector_reserve(char *ptr, size_t base_size, size_t obj_size, size_t obj_count, ALboolean exact);
#define VECTOR_RESERVE(_x, _c) (vector_reserve((char*)&(_x), sizeof(*(_x)), sizeof((_x)->Data[0]), (_c), AL_TRUE))

ALboolean vector_resize(char *ptr, size_t base_size, size_t obj_size, size_t obj_count);
#define VECTOR_RESIZE(_x, _c) (vector_resize((char*)&(_x), sizeof(*(_x)), sizeof((_x)->Data[0]), (_c)))

#define VECTOR_CAPACITY(_x) ((_x) ? (_x)->Capacity : 0)
#define VECTOR_SIZE(_x)     ((_x) ? (_x)->Size : 0)

#define VECTOR_ITER_BEGIN(_x) ((_x) ? (_x)->Data + 0 : NULL)
#define VECTOR_ITER_END(_x)   ((_x) ? (_x)->Data + (_x)->Size : NULL)

ALboolean vector_insert(char *ptr, size_t base_size, size_t obj_size, void *ins_pos, const void *datstart, const void *datend);
#ifdef __GNUC__
#define TYPE_CHECK(T1, T2) __builtin_types_compatible_p(T1, T2)
#define VECTOR_INSERT(_x, _i, _s, _e) __extension__({                         \
    ALboolean _r;                                                             \
    static_assert(TYPE_CHECK(__typeof((_x)->Data[0]), __typeof(*(_i))), "Incompatible insertion iterator"); \
    static_assert(TYPE_CHECK(__typeof((_x)->Data[0]), __typeof(*(_s))), "Incompatible insertion source type"); \
    static_assert(TYPE_CHECK(__typeof(*(_s)), __typeof(*(_e))), "Incompatible iterator sources"); \
    _r = vector_insert((char*)&(_x), sizeof(*(_x)), sizeof((_x)->Data[0]), (_i), (_s), (_e)); \
    _r;                                                                       \
})
#else
#define VECTOR_INSERT(_x, _i, _s, _e) (vector_insert((char*)&(_x), sizeof(*(_x)), sizeof((_x)->Data[0]), (_i), (_s), (_e)))
#endif

#define VECTOR_PUSH_BACK(_x, _obj) (vector_reserve((char*)&(_x), sizeof(*(_x)), sizeof((_x)->Data[0]), VECTOR_SIZE(_x)+1, AL_FALSE) && \
                                    (((_x)->Data[(_x)->Size++] = (_obj)),AL_TRUE))
#define VECTOR_POP_BACK(_x) ((void)((_x)->Size--))

#define VECTOR_BACK(_x)  ((_x)->Data[(_x)->Size-1])
#define VECTOR_FRONT(_x) ((_x)->Data[0])

#define VECTOR_ELEM(_x, _o) ((_x)->Data[(_o)])

#define VECTOR_FOR_EACH(_t, _x, _f)  do {                                     \
    _t *_iter = VECTOR_ITER_BEGIN((_x));                                      \
    _t *_end = VECTOR_ITER_END((_x));                                         \
    for(;_iter != _end;++_iter)                                               \
        _f(_iter);                                                            \
} while(0)

#define VECTOR_FOR_EACH_PARAMS(_t, _x, _f, ...)  do {                         \
    _t *_iter = VECTOR_ITER_BEGIN((_x));                                      \
    _t *_end = VECTOR_ITER_END((_x));                                         \
    for(;_iter != _end;++_iter)                                               \
        _f(__VA_ARGS__, _iter);                                               \
} while(0)

#define VECTOR_FIND_IF(_i, _t, _x, _f)  do {                                  \
    _t *_iter = VECTOR_ITER_BEGIN((_x));                                      \
    _t *_end = VECTOR_ITER_END((_x));                                         \
    for(;_iter != _end;++_iter)                                               \
    {                                                                         \
        if(_f(_iter))                                                         \
            break;                                                            \
    }                                                                         \
    (_i) = _iter;                                                             \
} while(0)

#define VECTOR_FIND_IF_PARMS(_i, _t, _x, _f, ...)  do {                       \
    _t *_iter = VECTOR_ITER_BEGIN((_x));                                      \
    _t *_end = VECTOR_ITER_END((_x));                                         \
    for(;_iter != _end;++_iter)                                               \
    {                                                                         \
        if(_f(__VA_ARGS__, _iter))                                            \
            break;                                                            \
    }                                                                         \
    (_i) = _iter;                                                             \
} while(0)

#endif /* AL_VECTOR_H */
