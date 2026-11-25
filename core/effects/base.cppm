module;

#include "core/effects/base.hpp"

export module core.effects.base;

export {
    using ::ReverbDecayGain;
    using ::ReverbMaxReflectionsDelay;
    using ::ReverbMaxLateReverbDelay;
    using ::ChorusWaveform;
    using ::ChorusMaxDelay;
    using ::FlangerMaxDelay;
    using ::EchoMaxDelay;
    using ::EchoMaxLRDelay;
    using ::FShifterDirection;
    using ::ModulatorWaveform;
    using ::VMorpherPhenome;
    using ::VMorpherWaveform;

    using ::ReverbProps;
    using ::AutowahProps;
    using ::ChorusProps;
    using ::CompressorProps;
    using ::DistortionProps;
    using ::EchoProps;
    using ::EqualizerProps;
    using ::FshifterProps;
    using ::ModulatorProps;
    using ::PshifterProps;
    using ::VmorpherProps;
    using ::DedicatedProps;
    using ::ConvolutionProps;
    using ::EffectProps;

    using ::EffectTarget;

    using ::EffectState;
    using ::EffectStateFactory;
}
