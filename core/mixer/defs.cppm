module;

#include "core/mixer/defs.hpp"

export module core.mixer.defs;

export {
    using ::f32x2;

    using ::MixerFracBits;
    using ::MixerFracOne;
    using ::MixerFracMask;
    using ::MixerFracHalf;
    using ::GainSilenceThreshold;

    using ::Resampler;
    using ::BsincState;
    using ::CubicState;
    using ::InterpState;
    using ::ResamplerFunc;
    using ::PrepareResampler;

#define DECL_RESAMPLER(T, I) using ::Resample_##T##_##I;
#define DECL_MIXER(I) using ::Mix_##I;
#define DECL_HRTF_MIXER(I) using ::MixHrtf_##I;                               \
    using ::MixHrtfBlend_##I;                                                 \
    using ::MixDirectHrtf_##I;

DECL_RESAMPLER(Point, C)
DECL_RESAMPLER(Linear, C)
DECL_RESAMPLER(Cubic, C)
DECL_RESAMPLER(FastBSinc, C)
DECL_RESAMPLER(BSinc, C)

DECL_MIXER(C)
DECL_HRTF_MIXER(C)

#if HAVE_NEON
DECL_RESAMPLER(Linear, NEON)
DECL_RESAMPLER(Cubic, NEON)
DECL_RESAMPLER(FastBSinc, NEON)
DECL_RESAMPLER(BSinc, NEON)

DECL_MIXER(NEON)
DECL_HRTF_MIXER(NEON)
#endif
#if HAVE_SSE
DECL_RESAMPLER(Cubic, SSE)
DECL_RESAMPLER(FastBSinc, SSE)
DECL_RESAMPLER(BSinc, SSE)

DECL_MIXER(SSE)
DECL_HRTF_MIXER(SSE)
#endif
#if HAVE_SSE2
DECL_RESAMPLER(Linear, SSE2)
DECL_RESAMPLER(Cubic, SSE2)
#endif
#if HAVE_SSE4_1
DECL_RESAMPLER(Linear, SSE4)
DECL_RESAMPLER(Cubic, SSE4)
#endif

#undef DECL_HRTF_MIXER
#undef DECL_MIXER
#undef DECL_RESAMPLER

    using ::InitPosArrays;
}
