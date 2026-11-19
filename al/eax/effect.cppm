module;

#include "al/eax/effect.hpp"

export module eax.effect;

export {
    using ::EaxTraceCommits;

    using ::EaxEffectProps;
    using ::EaxEffect;
    using ::EaxReverbCommitter;
    using ::EaxAutowahCommitter;
    using ::EaxChorusCommitter;
    using ::EaxCompressorCommitter;
    using ::EaxDistortionCommitter;
    using ::EaxEchoCommitter;
    using ::EaxEqualizerCommitter;
    using ::EaxFlangerCommitter;
    using ::EaxFrequencyShifterCommitter;
    using ::EaxModulatorCommitter;
    using ::EaxPitchShifterCommitter;
    using ::EaxVocalMorpherCommitter;
    using ::EaxNullCommitter;
}
