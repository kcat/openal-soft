#ifndef EFFECTS_BASE_H
#define EFFECTS_BASE_H

#include "core/effects/base.h"
#include "gsl/gsl"


/* This is a user config option for modifying the overall output of the reverb
 * effect.
 */
inline float ReverbBoost{1.0f};


auto NullStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>;
auto ReverbStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>;
auto ChorusStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>;
auto AutowahStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>;
auto CompressorStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>;
auto DistortionStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>;
auto EchoStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>;
auto EqualizerStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>;
auto FshifterStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>;
auto ModulatorStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>;
auto PshifterStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>;
auto VmorpherStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>;

auto DedicatedStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>;

auto ConvolutionStateFactory_getFactory() -> gsl::not_null<EffectStateFactory*>;

#endif /* EFFECTS_BASE_H */
