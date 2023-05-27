#ifndef AL_THREADS_H
#define AL_THREADS_H

#if defined(__GNUC__) && defined(__i386__)
/* force_align_arg_pointer may be required for proper stack alignment when SSE
 * code is used. GCC generates code with the assumption the stack pointer is
 * suitably aligned, while some systems (Windows, QNX) do not guarantee non-
 * exported functions will be properly aligned when called externally, and
 * older apps for other systems may have been built with a lower stack
 * alignment than expected by newer builds.
 */
#define FORCE_ALIGN __attribute__((force_align_arg_pointer))
#else
#define FORCE_ALIGN
#endif

#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#elif !defined(_WIN32)
#include <semaphore.h>
#endif

void althrd_setname(const char *name);

namespace al {

class semaphore {
#ifdef _WIN32
    using native_type = void*;
#elif defined(__APPLE__)
    using native_type = dispatch_semaphore_t;
#else
    using native_type = sem_t;
#endif
    native_type mSem;

public:
    semaphore(unsigned int initial=0);
    semaphore(const semaphore&) = delete;
    ~semaphore();

    semaphore& operator=(const semaphore&) = delete;

    void post();
    void wait() noexcept;
    bool try_wait() noexcept;
};

} // namespace al

#endif /* AL_THREADS_H */
