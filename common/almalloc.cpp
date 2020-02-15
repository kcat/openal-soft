
#include "config.h"

#include "almalloc.h"

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif


#define ALIGNED_ALLOC_AVAILABLE (__STDC_VERSION__ >= 201112L || __cplusplus >= 201703L)

void *al_malloc(size_t alignment, size_t size)
{
    assert((alignment & (alignment-1)) == 0);
    alignment = std::max(alignment, alignof(std::max_align_t));

#if ALIGNED_ALLOC_AVAILABLE
    size = (size+(alignment-1))&~(alignment-1);
    return aligned_alloc(alignment, size);
#elif defined(HAVE_POSIX_MEMALIGN)
    void *ret;
    if(posix_memalign(&ret, alignment, size) == 0)
        return ret;
    return nullptr;
#elif defined(HAVE__ALIGNED_MALLOC)
    return _aligned_malloc(size, alignment);
#else
    auto *ret = static_cast<char*>(malloc(size+alignment));
    if(ret != nullptr)
    {
        *(ret++) = 0x00;
        while((reinterpret_cast<uintptr_t>(ret)&(alignment-1)) != 0)
            *(ret++) = 0x55;
    }
    return ret;
#endif
}

void *al_calloc(size_t alignment, size_t size)
{
    void *ret = al_malloc(alignment, size);
    if(ret) memset(ret, 0, size);
    return ret;
}

void al_free(void *ptr) noexcept
{
#if ALIGNED_ALLOC_AVAILABLE || defined(HAVE_POSIX_MEMALIGN)
    free(ptr);
#elif defined(HAVE__ALIGNED_MALLOC)
    _aligned_free(ptr);
#else
    if(ptr != nullptr)
    {
        auto *finder = static_cast<char*>(ptr);
        do {
            --finder;
        } while(*finder == 0x55);
        free(finder);
    }
#endif
}
