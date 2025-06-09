#ifndef AL_EFFECTS_EFFECTS_H
#define AL_EFFECTS_EFFECTS_H

#include <variant>

#include "AL/alc.h"
#include "AL/al.h"

#include "core/effects/base.h"
#include "opthelpers.h"

#define DECL_HANDLER(N, T)                                                    \
struct N {                                                                    \
    using prop_type = T;                                                      \
                                                                              \
    static void SetParami(ALCcontext *context, prop_type &props, ALenum param, int val);           \
    static void SetParamiv(ALCcontext *context, prop_type &props, ALenum param, const int *vals);  \
    static void SetParamf(ALCcontext *context, prop_type &props, ALenum param, float val);         \
    static void SetParamfv(ALCcontext *context, prop_type &props, ALenum param, const float *vals);\
    static void GetParami(ALCcontext *context, const prop_type &props, ALenum param, int *val);    \
    static void GetParamiv(ALCcontext *context, const prop_type &props, ALenum param, int *vals);  \
    static void GetParamf(ALCcontext *context, const prop_type &props, ALenum param, float *val);  \
    static void GetParamfv(ALCcontext *context, const prop_type &props, ALenum param, float *vals);\
};
DECL_HANDLER(NullEffectHandler, std::monostate)
DECL_HANDLER(ReverbEffectHandler, ReverbProps)
DECL_HANDLER(StdReverbEffectHandler, ReverbProps)
DECL_HANDLER(AutowahEffectHandler, AutowahProps)
DECL_HANDLER(ChorusEffectHandler, ChorusProps)
DECL_HANDLER(CompressorEffectHandler, CompressorProps)
DECL_HANDLER(DistortionEffectHandler, DistortionProps)
DECL_HANDLER(EchoEffectHandler, EchoProps)
DECL_HANDLER(EqualizerEffectHandler, EqualizerProps)
DECL_HANDLER(FlangerEffectHandler, ChorusProps)
DECL_HANDLER(FshifterEffectHandler, FshifterProps)
DECL_HANDLER(ModulatorEffectHandler, ModulatorProps)
DECL_HANDLER(PshifterEffectHandler, PshifterProps)
DECL_HANDLER(VmorpherEffectHandler, VmorpherProps)
DECL_HANDLER(DedicatedDialogEffectHandler, DedicatedProps)
DECL_HANDLER(DedicatedLfeEffectHandler, DedicatedProps)
DECL_HANDLER(ConvolutionEffectHandler, ConvolutionProps)
#undef DECL_HANDLER


/* Default properties for the given effect types. */
DECL_HIDDEN extern constinit const EffectProps NullEffectProps;
DECL_HIDDEN extern constinit const EffectProps ReverbEffectProps;
DECL_HIDDEN extern constinit const EffectProps StdReverbEffectProps;
DECL_HIDDEN extern constinit const EffectProps AutowahEffectProps;
DECL_HIDDEN extern constinit const EffectProps ChorusEffectProps;
DECL_HIDDEN extern constinit const EffectProps CompressorEffectProps;
DECL_HIDDEN extern constinit const EffectProps DistortionEffectProps;
DECL_HIDDEN extern constinit const EffectProps EchoEffectProps;
DECL_HIDDEN extern constinit const EffectProps EqualizerEffectProps;
DECL_HIDDEN extern constinit const EffectProps FlangerEffectProps;
DECL_HIDDEN extern constinit const EffectProps FshifterEffectProps;
DECL_HIDDEN extern constinit const EffectProps ModulatorEffectProps;
DECL_HIDDEN extern constinit const EffectProps PshifterEffectProps;
DECL_HIDDEN extern constinit const EffectProps VmorpherEffectProps;
DECL_HIDDEN extern constinit const EffectProps DedicatedDialogEffectProps;
DECL_HIDDEN extern constinit const EffectProps DedicatedLfeEffectProps;
DECL_HIDDEN extern constinit const EffectProps ConvolutionEffectProps;

#endif /* AL_EFFECTS_EFFECTS_H */
