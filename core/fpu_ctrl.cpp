
#include "config.h"

#include "fpu_ctrl.h"

#ifdef HAVE_INTRIN_H
#include <intrin.h>
#endif
#ifdef HAVE_SSE_INTRINSICS
#include <emmintrin.h>
#elif defined(HAVE_SSE)
#include <xmmintrin.h>
#endif

#if defined(HAVE_SSE) && !defined(_MM_DENORMALS_ZERO_MASK)
/* Some headers seem to be missing these? */
#define _MM_DENORMALS_ZERO_MASK 0x0040u
#define _MM_DENORMALS_ZERO_ON 0x0040u
#endif

#include "cpu_caps.h"

namespace {

#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
[[gnu::target("sse")]]
#endif
[[maybe_unused]]
void disable_denormals(unsigned int *state [[maybe_unused]])
{
#if defined(HAVE_SSE_INTRINSICS)
    *state = _mm_getcsr();
    unsigned int sseState{*state};
    sseState &= ~(_MM_FLUSH_ZERO_MASK | _MM_DENORMALS_ZERO_MASK);
    sseState |= _MM_FLUSH_ZERO_ON | _MM_DENORMALS_ZERO_ON;
    _mm_setcsr(sseState);

#elif defined(HAVE_SSE)

    *state = _mm_getcsr();
    unsigned int sseState{*state};
    sseState &= ~_MM_FLUSH_ZERO_MASK;
    sseState |= _MM_FLUSH_ZERO_ON;
    if((CPUCapFlags&CPU_CAP_SSE2))
    {
        sseState &= ~_MM_DENORMALS_ZERO_MASK;
        sseState |= _MM_DENORMALS_ZERO_ON;
    }
    _mm_setcsr(sseState);
#endif
}

#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
[[gnu::target("sse")]]
#endif
[[maybe_unused]]
void reset_fpu(unsigned int state [[maybe_unused]])
{
#if defined(HAVE_SSE_INTRINSICS) || defined(HAVE_SSE)
    _mm_setcsr(state);
#endif
}

} // namespace


void FPUCtl::enter() noexcept
{
    if(this->in_mode) return;

#if defined(HAVE_SSE_INTRINSICS)
    disable_denormals(&this->sse_state);
#elif defined(HAVE_SSE)
    if((CPUCapFlags&CPU_CAP_SSE))
        disable_denormals(&this->sse_state);
#endif

    this->in_mode = true;
}

void FPUCtl::leave() noexcept
{
    if(!this->in_mode) return;

#if defined(HAVE_SSE_INTRINSICS)
    reset_fpu(this->sse_state);
#elif defined(HAVE_SSE)
    if((CPUCapFlags&CPU_CAP_SSE))
        reset_fpu(this->sse_state);
#endif
    this->in_mode = false;
}
