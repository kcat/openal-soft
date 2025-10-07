
#include "config.h"
#include "config_simd.h"

#include "fpu_ctrl.h"

#ifdef HAVE_INTRIN_H
#include <intrin.h>
#endif
#if HAVE_SSE_INTRINSICS
#include <emmintrin.h>
#elif HAVE_SSE
#include <xmmintrin.h>
#endif

#if HAVE_SSE && !defined(_MM_DENORMALS_ZERO_MASK)
/* Some headers seem to be missing these? */
#define _MM_DENORMALS_ZERO_MASK 0x0040u  /* NOLINT(*-reserved-identifier) */
#define _MM_DENORMALS_ZERO_ON 0x0040u  /* NOLINT(*-reserved-identifier) */
#endif

#if !HAVE_SSE_INTRINSICS && HAVE_SSE
#include "cpu_caps.h"
#endif

namespace {

#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
[[gnu::target("sse")]]
#endif
[[maybe_unused]]
auto disable_denormals() -> unsigned int
{
#if HAVE_SSE_INTRINSICS
    const auto state = _mm_getcsr();
    auto sseState = state;
    sseState &= ~(_MM_FLUSH_ZERO_MASK | _MM_DENORMALS_ZERO_MASK);
    sseState |= _MM_FLUSH_ZERO_ON | _MM_DENORMALS_ZERO_ON;
    _mm_setcsr(sseState);
    return state;

#elif HAVE_SSE

    const auto state = _mm_getcsr();
    auto sseState = state;
    sseState &= ~_MM_FLUSH_ZERO_MASK;
    sseState |= _MM_FLUSH_ZERO_ON;
    if((CPUCapFlags&CPU_CAP_SSE2))
    {
        sseState &= ~_MM_DENORMALS_ZERO_MASK;
        sseState |= _MM_DENORMALS_ZERO_ON;
    }
    _mm_setcsr(sseState);
    return state;

#else

    return 0u;
#endif
}

#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
[[gnu::target("sse")]]
#endif
[[maybe_unused]]
void reset_fpu(unsigned int state [[maybe_unused]])
{
#if HAVE_SSE_INTRINSICS || HAVE_SSE
    _mm_setcsr(state);
#endif
}

} // namespace


auto FPUCtl::Set() noexcept -> unsigned int
{
#if HAVE_SSE_INTRINSICS
    return disable_denormals();

#else

#if HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        return disable_denormals();
#endif
    return 0u;
#endif
}

void FPUCtl::Reset(unsigned int state [[maybe_unused]]) noexcept
{
#if HAVE_SSE_INTRINSICS
    reset_fpu(state);
#elif HAVE_SSE
    if((CPUCapFlags&CPU_CAP_SSE))
        reset_fpu(state);
#endif
}
