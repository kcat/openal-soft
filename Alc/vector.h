#ifndef AL_VECTOR_H
#define AL_VECTOR_H

#include <stdlib.h>

#include <AL/al.h>

/* "Base" vector type, designed to alias with the actual vector types. */
typedef struct vector__s {
    ALsizei Capacity;
    ALsizei Size;
} *vector_;

#define DECL_VECTOR(T) typedef struct vector_##T##_s {                        \
    ALsizei Capacity;                                                         \
    ALsizei Size;                                                             \
    T Data[];                                                                 \
} *vector_##T;                                                                \
typedef const struct vector_##T##_s *const_vector_##T;

#define VECTOR_INIT(_x)   do { (_x) = NULL; } while(0)
#define VECTOR_DEINIT(_x) do { free((_x)); (_x) = NULL; } while(0)

/* Helper to increase a vector's reserve. Do not call directly. */
ALboolean vector_reserve(void *ptr, size_t base_size, size_t obj_count, size_t obj_size, ALboolean exact);
#define VECTOR_RESERVE(_x, _c) (vector_reserve(&(_x), sizeof(*(_x)), (_c), sizeof((_x)->Data[0]), AL_TRUE))

/* Helper to change a vector's size. Do not call directly. */
ALboolean vector_resize(void *ptr, size_t base_size, size_t obj_count, size_t obj_size);
#define VECTOR_RESIZE(_x, _c) (vector_resize(&(_x), sizeof(*(_x)), (_c), sizeof((_x)->Data[0])))

#define VECTOR_CAPACITY(_x) ((const ALsizei)((_x) ? (_x)->Capacity : 0))
#define VECTOR_SIZE(_x)     ((const ALsizei)((_x) ? (_x)->Size : 0))

#define VECTOR_ITER_BEGIN(_x) ((_x)->Data)
#define VECTOR_ITER_END(_x)   ((_x)->Data + VECTOR_SIZE((_x)))

ALboolean vector_insert(void *ptr, size_t base_size, size_t obj_size, ptrdiff_t ins_elem, const void *datstart, ptrdiff_t numins);
#define VECTOR_INSERT(_x, _i, _s, _e) (vector_insert(&(_x), sizeof(*(_x)), sizeof((_x)->Data[0]), (_i)-VECTOR_ITER_BEGIN(_x), (_s), (_e)-(_s)))

#define VECTOR_PUSH_BACK(_x, _obj) (vector_reserve(&(_x), sizeof(*(_x)), VECTOR_SIZE(_x)+1, sizeof((_x)->Data[0]), AL_FALSE) && \
                                    (((_x)->Data[(_x)->Size++] = (_obj)),AL_TRUE))
#define VECTOR_POP_BACK(_x) ((void)((_x)->Size--))

#define VECTOR_BACK(_x)  ((_x)->Data[(_x)->Size-1])
#define VECTOR_FRONT(_x) ((_x)->Data[0])

#define VECTOR_ELEM(_x, _o) ((_x)->Data[(_o)])

#endif /* AL_VECTOR_H */
