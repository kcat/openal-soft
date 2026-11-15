#ifndef EFFECTS_BASE_H
#define EFFECTS_BASE_H

#include "core/effects/base.h"


/* This is a user config option for modifying the overall output of the reverb
 * effect.
 */
inline float ReverbBoost{1.0f};


auto NullStateFactory_getFactory() -> EffectStateFactory&;
auto ReverbStateFactory_getFactory() -> EffectStateFactory&;
auto ChorusStateFactory_getFactory() -> EffectStateFactory&;
auto AutowahStateFactory_getFactory() -> EffectStateFactory&;
auto CompressorStateFactory_getFactory() -> EffectStateFactory&;
auto DistortionStateFactory_getFactory() -> EffectStateFactory&;
auto EchoStateFactory_getFactory() -> EffectStateFactory&;
auto EqualizerStateFactory_getFactory() -> EffectStateFactory&;
auto FshifterStateFactory_getFactory() -> EffectStateFactory&;
auto ModulatorStateFactory_getFactory() -> EffectStateFactory&;
auto PshifterStateFactory_getFactory() -> EffectStateFactory&;
auto VmorpherStateFactory_getFactory() -> EffectStateFactory&;

auto DedicatedStateFactory_getFactory() -> EffectStateFactory&;

auto ConvolutionStateFactory_getFactory() -> EffectStateFactory&;

#endif /* EFFECTS_BASE_H */
