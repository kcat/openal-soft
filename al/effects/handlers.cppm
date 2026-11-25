module;

#include "al/effects/handlers.hpp"

export module al.effects.handlers;

export {
    using ::NullEffectHandler;
    using ::ReverbEffectHandler;
    using ::StdReverbEffectHandler;
    using ::AutowahEffectHandler;
    using ::ChorusEffectHandler;
    using ::CompressorEffectHandler;
    using ::DistortionEffectHandler;
    using ::EchoEffectHandler;
    using ::EqualizerEffectHandler;
    using ::FlangerEffectHandler;
    using ::FshifterEffectHandler;
    using ::ModulatorEffectHandler;
    using ::PshifterEffectHandler;
    using ::VmorpherEffectHandler;
    using ::DedicatedDialogEffectHandler;
    using ::DedicatedLfeEffectHandler;
    using ::ConvolutionEffectHandler;

    using ::NullEffectProps;
    using ::ReverbEffectProps;
    using ::StdReverbEffectProps;
    using ::AutowahEffectProps;
    using ::ChorusEffectProps;
    using ::CompressorEffectProps;
    using ::DistortionEffectProps;
    using ::EchoEffectProps;
    using ::EqualizerEffectProps;
    using ::FlangerEffectProps;
    using ::FshifterEffectProps;
    using ::ModulatorEffectProps;
    using ::PshifterEffectProps;
    using ::VmorpherEffectProps;
    using ::DedicatedDialogEffectProps;
    using ::DedicatedLfeEffectProps;
    using ::ConvolutionEffectProps;
}

#ifdef _MSC_VER
export inline auto maybe_an_msvc_workaround = true;
#endif
