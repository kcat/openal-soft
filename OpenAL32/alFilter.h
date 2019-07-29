#ifndef AL_FILTER_H
#define AL_FILTER_H

#include "AL/al.h"
#include "AL/alc.h"


#define LOWPASSFREQREF  (5000.0f)
#define HIGHPASSFREQREF  (250.0f)


struct ALfilter;

struct ALfilterVtable {
    void (*const setParami)(ALfilter *filter, ALCcontext *context, ALenum param, ALint val);
    void (*const setParamiv)(ALfilter *filter, ALCcontext *context, ALenum param, const ALint *vals);
    void (*const setParamf)(ALfilter *filter, ALCcontext *context, ALenum param, ALfloat val);
    void (*const setParamfv)(ALfilter *filter, ALCcontext *context, ALenum param, const ALfloat *vals);

    void (*const getParami)(ALfilter *filter, ALCcontext *context, ALenum param, ALint *val);
    void (*const getParamiv)(ALfilter *filter, ALCcontext *context, ALenum param, ALint *vals);
    void (*const getParamf)(ALfilter *filter, ALCcontext *context, ALenum param, ALfloat *val);
    void (*const getParamfv)(ALfilter *filter, ALCcontext *context, ALenum param, ALfloat *vals);
};

#define DEFINE_ALFILTER_VTABLE(T)                                  \
const ALfilterVtable T##_vtable = {                                \
    T##_setParami, T##_setParamiv, T##_setParamf, T##_setParamfv,  \
    T##_getParami, T##_getParamiv, T##_getParamf, T##_getParamfv,  \
}

struct ALfilter {
    // Filter type (AL_FILTER_NULL, ...)
    ALenum type;

    ALfloat Gain;
    ALfloat GainHF;
    ALfloat HFReference;
    ALfloat GainLF;
    ALfloat LFReference;

    const ALfilterVtable *vtab;

    /* Self ID */
    ALuint id;
};
#define ALfilter_setParami(o, c, p, v)   ((o)->vtab->setParami(o, c, p, v))
#define ALfilter_setParamf(o, c, p, v)   ((o)->vtab->setParamf(o, c, p, v))
#define ALfilter_setParamiv(o, c, p, v)  ((o)->vtab->setParamiv(o, c, p, v))
#define ALfilter_setParamfv(o, c, p, v)  ((o)->vtab->setParamfv(o, c, p, v))
#define ALfilter_getParami(o, c, p, v)   ((o)->vtab->getParami(o, c, p, v))
#define ALfilter_getParamf(o, c, p, v)   ((o)->vtab->getParamf(o, c, p, v))
#define ALfilter_getParamiv(o, c, p, v)  ((o)->vtab->getParamiv(o, c, p, v))
#define ALfilter_getParamfv(o, c, p, v)  ((o)->vtab->getParamfv(o, c, p, v))

#endif
