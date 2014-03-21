#ifndef AL_VECTOR_H
#define AL_VECTOR_H

#include <stdlib.h>

#include <AL/al.h>

/* "Base" vector type, designed to alias with the actual vector types. */
typedef struct vector__s {
    ALsizei Max;
    ALsizei Size;
} *vector_;

#define DECL_VECTOR(T) typedef struct vector_##T##_s {                        \
    ALsizei Max;                                                              \
    ALsizei Size;                                                             \
    T Data[];                                                                 \
} *vector_##T;

#define VECTOR_INIT(_x)   (((_x) = calloc(1, sizeof(*(_x)))) != NULL)
#define VECTOR_DEINIT(_x) do { free(_x); _x = NULL; } while(0)

/* Helper to increase a vector's reserve. Do not call directly. */
ALboolean vector_reserve(void *ptr, size_t orig_count, size_t base_size, size_t obj_count, size_t obj_size);
#define VECTOR_RESERVE(_x, _c) (vector_reserve(&(_x), (_x)->Max, sizeof(*(_x)), (_c), sizeof((_x)->Data[0])))

#define VECTOR_SIZE(_x) ((const ALsizei)(_x)->Size)
#define VECTOR_MAX(_x) ((const ALsizei)(_x)->Max)

#define VECTOR_ITER_BEGIN(_x) ((_x)->Data)
#define VECTOR_ITER_END(_x)   ((_x)->Data + (_x)->Size)

/* NOTE: The caller must ensure enough space is reserved before pushing in new objects. */
#define VECTOR_PUSH_BACK(_x, _obj) ((void)((_x)->Data[(_x)->Size++] = (_obj)))
#define VECTOR_POP_BACK(_x) ((void)((_x)->Size--))

#define VECTOR_BACK(_x)  ((_x)->Data[(_x)->Size-1])
#define VECTOR_FRONT(_x) ((_x)->Data[0])

#define VECTOR_ELEM(_x, _o) ((_x)->Data[(_o)])

#endif /* AL_VECTOR_H */
