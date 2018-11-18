#ifndef AL_MALLOC_H
#define AL_MALLOC_H

#include <stddef.h>

/* Minimum alignment required by posix_memalign. */
#define DEF_ALIGN sizeof(void*)

void *al_malloc(size_t alignment, size_t size);
void *al_calloc(size_t alignment, size_t size);
void al_free(void *ptr) noexcept;

size_t al_get_page_size(void) noexcept;

/**
 * Returns non-0 if the allocation function has direct alignment handling.
 * Otherwise, the standard malloc is used with an over-allocation and pointer
 * offset strategy.
 */
int al_is_sane_alignment_allocator(void) noexcept;

#define DEF_NEWDEL(T)                                                         \
    void *operator new(size_t size)                                           \
    {                                                                         \
        void *ret = al_malloc(alignof(T), size);                              \
        if(!ret) throw std::bad_alloc();                                      \
        return ret;                                                           \
    }                                                                         \
    void operator delete(void *block) noexcept { al_free(block); }

#endif /* AL_MALLOC_H */
