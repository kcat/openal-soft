#ifndef AL_FILTER_H
#define AL_FILTER_H

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "almalloc.h"


#define LOWPASSFREQREF  5000.0f
#define HIGHPASSFREQREF  250.0f


struct ALfilter;

struct ALfilterVtable {
    void (*const setParami )(ALfilter *filter, ALCcontext *ctx, ALenum param, int val);
    void (*const setParamiv)(ALfilter *filter, ALCcontext *ctx, ALenum param, const int *vals);
    void (*const setParamf )(ALfilter *filter, ALCcontext *ctx, ALenum param, float val);
    void (*const setParamfv)(ALfilter *filter, ALCcontext *ctx, ALenum param, const float *vals);

    void (*const getParami )(const ALfilter *filter, ALCcontext *ctx, ALenum param, int *val);
    void (*const getParamiv)(const ALfilter *filter, ALCcontext *ctx, ALenum param, int *vals);
    void (*const getParamf )(const ALfilter *filter, ALCcontext *ctx, ALenum param, float *val);
    void (*const getParamfv)(const ALfilter *filter, ALCcontext *ctx, ALenum param, float *vals);
};

#define DEFINE_ALFILTER_VTABLE(T)                                  \
const ALfilterVtable T##_vtable = {                                \
    T##_setParami, T##_setParamiv, T##_setParamf, T##_setParamfv,  \
    T##_getParami, T##_getParamiv, T##_getParamf, T##_getParamfv,  \
}

struct ALfilter {
    ALenum type{AL_FILTER_NULL};

    float Gain{1.0f};
    float GainHF{1.0f};
    float HFReference{LOWPASSFREQREF};
    float GainLF{1.0f};
    float LFReference{HIGHPASSFREQREF};

    const ALfilterVtable *vtab{nullptr};

    /* Self ID */
    ALuint id{0};

    inline void setParami(ALCcontext *ctx, ALenum param, int value)
    { vtab->setParami(this, ctx, param, value); }
    inline void setParamiv(ALCcontext *ctx, ALenum param, const int *values)
    { vtab->setParamiv(this, ctx, param, values); }
    inline void setParamf(ALCcontext *ctx, ALenum param, float value)
    { vtab->setParamf(this, ctx, param, value); }
    inline void setParamfv(ALCcontext *ctx, ALenum param, const float *values)
    { vtab->setParamfv(this, ctx, param, values); }
    inline void getParami(ALCcontext *ctx, ALenum param, int *value) const
    { vtab->getParami(this, ctx, param, value); }
    inline void getParamiv(ALCcontext *ctx, ALenum param, int *values) const
    { vtab->getParamiv(this, ctx, param, values); }
    inline void getParamf(ALCcontext *ctx, ALenum param, float *value) const
    { vtab->getParamf(this, ctx, param, value); }
    inline void getParamfv(ALCcontext *ctx, ALenum param, float *values) const
    { vtab->getParamfv(this, ctx, param, values); }

    DISABLE_ALLOC()
};

#endif
