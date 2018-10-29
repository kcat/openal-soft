#ifndef AL_ALIGN_H
#define AL_ALIGN_H

#if defined(HAVE_STDALIGN_H) && defined(HAVE_C11_ALIGNAS)
#include <stdalign.h>
#endif

#ifndef alignas
#if defined(HAVE_C11_ALIGNAS)
#define alignas _Alignas
#else
/* NOTE: Our custom ALIGN macro can't take a type name like alignas can. For
 * maximum compatibility, only provide constant integer values to alignas. */
#define alignas(_x) ALIGN(_x)
#endif
#endif

#endif /* AL_ALIGN_H */
