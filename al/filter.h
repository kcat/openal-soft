#ifndef AL_FILTER_H
#define AL_FILTER_H


#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "almalloc.h"

#if ALSOFT_EAX
#include <memory>

#include "eax_utils.h"
#endif // ALSOFT_EAX

#define LOWPASSFREQREF  5000.0f
#define HIGHPASSFREQREF  250.0f


struct ALfilter {
    ALenum type{AL_FILTER_NULL};

    float Gain{1.0f};
    float GainHF{1.0f};
    float HFReference{LOWPASSFREQREF};
    float GainLF{1.0f};
    float LFReference{HIGHPASSFREQREF};

    struct Vtable {
        void (*const setParami )(ALfilter *filter, ALenum param, int val);
        void (*const setParamiv)(ALfilter *filter, ALenum param, const int *vals);
        void (*const setParamf )(ALfilter *filter, ALenum param, float val);
        void (*const setParamfv)(ALfilter *filter, ALenum param, const float *vals);

        void (*const getParami )(const ALfilter *filter, ALenum param, int *val);
        void (*const getParamiv)(const ALfilter *filter, ALenum param, int *vals);
        void (*const getParamf )(const ALfilter *filter, ALenum param, float *val);
        void (*const getParamfv)(const ALfilter *filter, ALenum param, float *vals);
    };
    const Vtable *vtab{nullptr};

    /* Self ID */
    ALuint id{0};

    void setParami(ALenum param, int value) { vtab->setParami(this, param, value); }
    void setParamiv(ALenum param, const int *values) { vtab->setParamiv(this, param, values); }
    void setParamf(ALenum param, float value) { vtab->setParamf(this, param, value); }
    void setParamfv(ALenum param, const float *values) { vtab->setParamfv(this, param, values); }
    void getParami(ALenum param, int *value) const { vtab->getParami(this, param, value); }
    void getParamiv(ALenum param, int *values) const { vtab->getParamiv(this, param, values); }
    void getParamf(ALenum param, float *value) const { vtab->getParamf(this, param, value); }
    void getParamfv(ALenum param, float *values) const { vtab->getParamfv(this, param, values); }

    DISABLE_ALLOC()

#if ALSOFT_EAX
public:
    void eax_set_low_pass_params(
        ALCcontext& context,
        const EaxAlLowPassParam& param);
#endif // ALSOFT_EAX
};

#if ALSOFT_EAX
class EaxAlFilterDeleter
{
public:
    EaxAlFilterDeleter() noexcept = default;

    EaxAlFilterDeleter(
        ALCcontext& context);


    void operator()(
        ALfilter* filter);

private:
    ALCcontext* context_{};
}; // EaxAlFilterDeleter

using EaxAlFilterUPtr = std::unique_ptr<ALfilter, EaxAlFilterDeleter>;

EaxAlFilterUPtr eax_create_al_low_pass_filter(
    ALCcontext& context);

void eax_delete_low_pass_filter(
    ALCcontext& context,
    ALfilter& filter);
#endif // ALSOFT_EAX

#endif
