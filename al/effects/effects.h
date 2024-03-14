#ifndef AL_EFFECTS_EFFECTS_H
#define AL_EFFECTS_EFFECTS_H

#include <variant>

#include "AL/al.h"

#include "al/error.h"
#include "core/effects/base.h"


struct EffectHandler {
#define DECL_HANDLER(T)                                                       \
    static void SetParami(T &props, ALenum param, int val);                   \
    static void SetParamiv(T &props, ALenum param, const int *vals);          \
    static void SetParamf(T &props, ALenum param, float val);                 \
    static void SetParamfv(T &props, ALenum param, const float *vals);        \
    static void GetParami(const T &props, ALenum param, int *val);            \
    static void GetParamiv(const T &props, ALenum param, int *vals);          \
    static void GetParamf(const T &props, ALenum param, float *val);          \
    static void GetParamfv(const T &props, ALenum param, float *vals);

    DECL_HANDLER(std::monostate)
    DECL_HANDLER(ReverbProps)
    DECL_HANDLER(ChorusProps)
    DECL_HANDLER(AutowahProps)
    DECL_HANDLER(CompressorProps)
    DECL_HANDLER(ConvolutionProps)
    DECL_HANDLER(DedicatedDialogProps)
    DECL_HANDLER(DedicatedLfeProps)
    DECL_HANDLER(DistortionProps)
    DECL_HANDLER(EchoProps)
    DECL_HANDLER(EqualizerProps)
    DECL_HANDLER(FlangerProps)
    DECL_HANDLER(FshifterProps)
    DECL_HANDLER(ModulatorProps)
    DECL_HANDLER(PshifterProps)
    DECL_HANDLER(VmorpherProps)
#undef DECL_HANDLER

    static void StdReverbSetParami(ReverbProps &props, ALenum param, int val);
    static void StdReverbSetParamiv(ReverbProps &props, ALenum param, const int *vals);
    static void StdReverbSetParamf(ReverbProps &props, ALenum param, float val);
    static void StdReverbSetParamfv(ReverbProps &props, ALenum param, const float *vals);
    static void StdReverbGetParami(const ReverbProps &props, ALenum param, int *val);
    static void StdReverbGetParamiv(const ReverbProps &props, ALenum param, int *vals);
    static void StdReverbGetParamf(const ReverbProps &props, ALenum param, float *val);
    static void StdReverbGetParamfv(const ReverbProps &props, ALenum param, float *vals);
};

using effect_exception = al::context_error;


/* Default properties for the given effect types. */
extern const EffectProps NullEffectProps;
extern const EffectProps ReverbEffectProps;
extern const EffectProps StdReverbEffectProps;
extern const EffectProps AutowahEffectProps;
extern const EffectProps ChorusEffectProps;
extern const EffectProps CompressorEffectProps;
extern const EffectProps DistortionEffectProps;
extern const EffectProps EchoEffectProps;
extern const EffectProps EqualizerEffectProps;
extern const EffectProps FlangerEffectProps;
extern const EffectProps FshifterEffectProps;
extern const EffectProps ModulatorEffectProps;
extern const EffectProps PshifterEffectProps;
extern const EffectProps VmorpherEffectProps;
extern const EffectProps DedicatedDialogEffectProps;
extern const EffectProps DedicatedLfeEffectProps;
extern const EffectProps ConvolutionEffectProps;

#endif /* AL_EFFECTS_EFFECTS_H */
