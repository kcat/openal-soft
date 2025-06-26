#ifndef EFFECTS_BASE_H
#define EFFECTS_BASE_H

#include "core/effects/base.h"
#include "gsl/gsl"


/* This is a user config option for modifying the overall output of the reverb
 * effect.
 */
inline float ReverbBoost{1.0f};


gsl::not_null<EffectStateFactory*> NullStateFactory_getFactory();
gsl::not_null<EffectStateFactory*> ReverbStateFactory_getFactory();
gsl::not_null<EffectStateFactory*> ChorusStateFactory_getFactory();
gsl::not_null<EffectStateFactory*> AutowahStateFactory_getFactory();
gsl::not_null<EffectStateFactory*> CompressorStateFactory_getFactory();
gsl::not_null<EffectStateFactory*> DistortionStateFactory_getFactory();
gsl::not_null<EffectStateFactory*> EchoStateFactory_getFactory();
gsl::not_null<EffectStateFactory*> EqualizerStateFactory_getFactory();
gsl::not_null<EffectStateFactory*> FshifterStateFactory_getFactory();
gsl::not_null<EffectStateFactory*> ModulatorStateFactory_getFactory();
gsl::not_null<EffectStateFactory*> PshifterStateFactory_getFactory();
gsl::not_null<EffectStateFactory*> VmorpherStateFactory_getFactory();

gsl::not_null<EffectStateFactory*> DedicatedStateFactory_getFactory();

gsl::not_null<EffectStateFactory*> ConvolutionStateFactory_getFactory();

#endif /* EFFECTS_BASE_H */
