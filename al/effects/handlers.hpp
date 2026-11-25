#ifndef AL_EFFECTS_HANDLERS_HPP
#define AL_EFFECTS_HANDLERS_HPP

#include <variant>

#include "AL/al.h"
#include "AL/efx.h"

#include "core/effects/base.hpp"
#include "opthelper_macros.hpp"

namespace al {
struct Context;
} // namespace al

#define DECL_HANDLER(N, T)                                                    \
struct N {                                                                    \
    using prop_type = T;                                                      \
                                                                              \
    static void SetParami(al::Context& context, prop_type& props, ALenum param, int val);           \
    static void SetParamiv(al::Context& context, prop_type& props, ALenum param, int const* vals);  \
    static void SetParamf(al::Context& context, prop_type& props, ALenum param, float val);         \
    static void SetParamfv(al::Context& context, prop_type& props, ALenum param, float const* vals);\
    static void GetParami(al::Context& context, prop_type const& props, ALenum param, int* val);    \
    static void GetParamiv(al::Context& context, prop_type const& props, ALenum param, int* vals);  \
    static void GetParamf(al::Context& context, prop_type const& props, ALenum param, float* val);  \
    static void GetParamfv(al::Context& context, prop_type const& props, ALenum param, float* vals);\
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

namespace detail {
    [[nodiscard]] consteval
    auto ChorusWaveformFromEnum(ALenum const type) noexcept -> ChorusWaveform
    {
        switch(type)
        {
        case AL_CHORUS_WAVEFORM_SINUSOID: return ChorusWaveform::Sinusoid;
        case AL_CHORUS_WAVEFORM_TRIANGLE: return ChorusWaveform::Triangle;
        }
        ASSUME(false);
    }

    [[nodiscard]] consteval
    auto DirectionFromEmum(ALenum const value) noexcept -> FShifterDirection
    {
        switch(value)
        {
        case AL_FREQUENCY_SHIFTER_DIRECTION_DOWN: return FShifterDirection::Down;
        case AL_FREQUENCY_SHIFTER_DIRECTION_UP: return FShifterDirection::Up;
        case AL_FREQUENCY_SHIFTER_DIRECTION_OFF: return FShifterDirection::Off;
        }
        ASSUME(false);
    }

    [[nodiscard]] consteval
    auto ModulatorWaveformFromEmum(ALenum const value) noexcept -> ModulatorWaveform
    {
        switch(value)
        {
        case AL_RING_MODULATOR_SINUSOID: return ModulatorWaveform::Sinusoid;
        case AL_RING_MODULATOR_SAWTOOTH: return ModulatorWaveform::Sawtooth;
        case AL_RING_MODULATOR_SQUARE: return ModulatorWaveform::Square;
        }
        ASSUME(false);
    }

    [[nodiscard]] consteval
    auto PhenomeFromEnum(ALenum const val) noexcept -> VMorpherPhenome
    {
#define HANDLE_PHENOME(x) case AL_VOCAL_MORPHER_PHONEME_ ## x: return VMorpherPhenome::x
        switch(val)
        {
        HANDLE_PHENOME(A);
        HANDLE_PHENOME(E);
        HANDLE_PHENOME(I);
        HANDLE_PHENOME(O);
        HANDLE_PHENOME(U);
        HANDLE_PHENOME(AA);
        HANDLE_PHENOME(AE);
        HANDLE_PHENOME(AH);
        HANDLE_PHENOME(AO);
        HANDLE_PHENOME(EH);
        HANDLE_PHENOME(ER);
        HANDLE_PHENOME(IH);
        HANDLE_PHENOME(IY);
        HANDLE_PHENOME(UH);
        HANDLE_PHENOME(UW);
        HANDLE_PHENOME(B);
        HANDLE_PHENOME(D);
        HANDLE_PHENOME(F);
        HANDLE_PHENOME(G);
        HANDLE_PHENOME(J);
        HANDLE_PHENOME(K);
        HANDLE_PHENOME(L);
        HANDLE_PHENOME(M);
        HANDLE_PHENOME(N);
        HANDLE_PHENOME(P);
        HANDLE_PHENOME(R);
        HANDLE_PHENOME(S);
        HANDLE_PHENOME(T);
        HANDLE_PHENOME(V);
        HANDLE_PHENOME(Z);
        }
#undef HANDLE_PHENOME
        ASSUME(false);
    }

    [[nodiscard]] consteval
    auto VMorpherWaveformFromEmum(ALenum const value) noexcept -> VMorpherWaveform
    {
        switch(value)
        {
        case AL_VOCAL_MORPHER_WAVEFORM_SINUSOID: return VMorpherWaveform::Sinusoid;
        case AL_VOCAL_MORPHER_WAVEFORM_TRIANGLE: return VMorpherWaveform::Triangle;
        case AL_VOCAL_MORPHER_WAVEFORM_SAWTOOTH: return VMorpherWaveform::Sawtooth;
        }
        ASSUME(false);
    }
}

/* Default properties for the given effect types. */
inline auto constexpr NullEffectProps = std::monostate{};

inline auto constexpr ReverbEffectProps = ReverbProps{
    .Density = AL_EAXREVERB_DEFAULT_DENSITY,
    .Diffusion = AL_EAXREVERB_DEFAULT_DIFFUSION,
    .Gain = AL_EAXREVERB_DEFAULT_GAIN,
    .GainHF = AL_EAXREVERB_DEFAULT_GAINHF,
    .GainLF = AL_EAXREVERB_DEFAULT_GAINLF,
    .DecayTime = AL_EAXREVERB_DEFAULT_DECAY_TIME,
    .DecayHFRatio = AL_EAXREVERB_DEFAULT_DECAY_HFRATIO,
    .DecayLFRatio = AL_EAXREVERB_DEFAULT_DECAY_LFRATIO,
    .ReflectionsGain = AL_EAXREVERB_DEFAULT_REFLECTIONS_GAIN,
    .ReflectionsDelay = AL_EAXREVERB_DEFAULT_REFLECTIONS_DELAY,
    .ReflectionsPan = {AL_EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ,
        AL_EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ, AL_EAXREVERB_DEFAULT_REFLECTIONS_PAN_XYZ},
    .LateReverbGain = AL_EAXREVERB_DEFAULT_LATE_REVERB_GAIN,
    .LateReverbDelay = AL_EAXREVERB_DEFAULT_LATE_REVERB_DELAY,
    .LateReverbPan = {AL_EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ,
        AL_EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ, AL_EAXREVERB_DEFAULT_LATE_REVERB_PAN_XYZ},
    .EchoTime = AL_EAXREVERB_DEFAULT_ECHO_TIME,
    .EchoDepth = AL_EAXREVERB_DEFAULT_ECHO_DEPTH,
    .ModulationTime = AL_EAXREVERB_DEFAULT_MODULATION_TIME,
    .ModulationDepth = AL_EAXREVERB_DEFAULT_MODULATION_DEPTH,
    .AirAbsorptionGainHF = AL_EAXREVERB_DEFAULT_AIR_ABSORPTION_GAINHF,
    .HFReference = AL_EAXREVERB_DEFAULT_HFREFERENCE,
    .LFReference = AL_EAXREVERB_DEFAULT_LFREFERENCE,
    .RoomRolloffFactor = AL_EAXREVERB_DEFAULT_ROOM_ROLLOFF_FACTOR,
    .DecayHFLimit = AL_EAXREVERB_DEFAULT_DECAY_HFLIMIT};

inline auto constexpr StdReverbEffectProps = ReverbProps{
    .Density = AL_REVERB_DEFAULT_DENSITY,
    .Diffusion = AL_REVERB_DEFAULT_DIFFUSION,
    .Gain = AL_REVERB_DEFAULT_GAIN,
    .GainHF = AL_REVERB_DEFAULT_GAINHF,
    .GainLF = 1.0f,
    .DecayTime = AL_REVERB_DEFAULT_DECAY_TIME,
    .DecayHFRatio = AL_REVERB_DEFAULT_DECAY_HFRATIO,
    .DecayLFRatio = 1.0f,
    .ReflectionsGain = AL_REVERB_DEFAULT_REFLECTIONS_GAIN,
    .ReflectionsDelay = AL_REVERB_DEFAULT_REFLECTIONS_DELAY,
    .ReflectionsPan = {0.0f, 0.0f, 0.0f},
    .LateReverbGain = AL_REVERB_DEFAULT_LATE_REVERB_GAIN,
    .LateReverbDelay = AL_REVERB_DEFAULT_LATE_REVERB_DELAY,
    .LateReverbPan = {0.0f, 0.0f, 0.0f},
    .EchoTime = 0.25f,
    .EchoDepth = 0.0f,
    .ModulationTime = 0.25f,
    .ModulationDepth = 0.0f,
    .AirAbsorptionGainHF = AL_REVERB_DEFAULT_AIR_ABSORPTION_GAINHF,
    .HFReference = 5'000.0f,
    .LFReference = 250.0f,
    .RoomRolloffFactor = AL_REVERB_DEFAULT_ROOM_ROLLOFF_FACTOR,
    .DecayHFLimit = AL_REVERB_DEFAULT_DECAY_HFLIMIT};

inline auto constexpr AutowahEffectProps = AutowahProps{
    .AttackTime = AL_AUTOWAH_DEFAULT_ATTACK_TIME,
    .ReleaseTime = AL_AUTOWAH_DEFAULT_RELEASE_TIME,
    .Resonance = AL_AUTOWAH_DEFAULT_RESONANCE,
    .PeakGain = AL_AUTOWAH_DEFAULT_PEAK_GAIN};

inline auto constexpr ChorusEffectProps = ChorusProps{
    .Waveform = detail::ChorusWaveformFromEnum(AL_CHORUS_DEFAULT_WAVEFORM),
    .Phase = AL_CHORUS_DEFAULT_PHASE,
    .Rate = AL_CHORUS_DEFAULT_RATE,
    .Depth = AL_CHORUS_DEFAULT_DEPTH,
    .Feedback = AL_CHORUS_DEFAULT_FEEDBACK,
    .Delay = AL_CHORUS_DEFAULT_DELAY};

inline auto constexpr CompressorEffectProps = CompressorProps{
    .OnOff = AL_COMPRESSOR_DEFAULT_ONOFF};

inline auto constexpr DistortionEffectProps = DistortionProps{
    .Edge = AL_DISTORTION_DEFAULT_EDGE,
    .Gain = AL_DISTORTION_DEFAULT_GAIN,
    .LowpassCutoff = AL_DISTORTION_DEFAULT_LOWPASS_CUTOFF,
    .EQCenter = AL_DISTORTION_DEFAULT_EQCENTER,
    .EQBandwidth = AL_DISTORTION_DEFAULT_EQBANDWIDTH};

inline auto constexpr EchoEffectProps = EchoProps{
    .Delay = AL_ECHO_DEFAULT_DELAY,
    .LRDelay = AL_ECHO_DEFAULT_LRDELAY,
    .Damping = AL_ECHO_DEFAULT_DAMPING,
    .Feedback = AL_ECHO_DEFAULT_FEEDBACK,
    .Spread = AL_ECHO_DEFAULT_SPREAD};

inline auto constexpr EqualizerEffectProps = EqualizerProps{
    .LowCutoff = AL_EQUALIZER_DEFAULT_LOW_CUTOFF,
    .LowGain = AL_EQUALIZER_DEFAULT_LOW_GAIN,
    .Mid1Center = AL_EQUALIZER_DEFAULT_MID1_CENTER,
    .Mid1Gain = AL_EQUALIZER_DEFAULT_MID1_GAIN,
    .Mid1Width = AL_EQUALIZER_DEFAULT_MID1_WIDTH,
    .Mid2Center = AL_EQUALIZER_DEFAULT_MID2_CENTER,
    .Mid2Gain = AL_EQUALIZER_DEFAULT_MID2_GAIN,
    .Mid2Width = AL_EQUALIZER_DEFAULT_MID2_WIDTH,
    .HighCutoff = AL_EQUALIZER_DEFAULT_HIGH_CUTOFF,
    .HighGain = AL_EQUALIZER_DEFAULT_HIGH_GAIN};

inline auto constexpr FlangerEffectProps = ChorusProps{
    .Waveform = detail::ChorusWaveformFromEnum(AL_FLANGER_DEFAULT_WAVEFORM),
    .Phase = AL_FLANGER_DEFAULT_PHASE,
    .Rate = AL_FLANGER_DEFAULT_RATE,
    .Depth = AL_FLANGER_DEFAULT_DEPTH,
    .Feedback = AL_FLANGER_DEFAULT_FEEDBACK,
    .Delay = AL_FLANGER_DEFAULT_DELAY};

inline auto constexpr FshifterEffectProps = FshifterProps{
    .Frequency = AL_FREQUENCY_SHIFTER_DEFAULT_FREQUENCY,
    .LeftDirection = detail::DirectionFromEmum(AL_FREQUENCY_SHIFTER_DEFAULT_LEFT_DIRECTION),
    .RightDirection = detail::DirectionFromEmum(AL_FREQUENCY_SHIFTER_DEFAULT_RIGHT_DIRECTION)};

inline auto constexpr ModulatorEffectProps = ModulatorProps{
    .Frequency = AL_RING_MODULATOR_DEFAULT_FREQUENCY,
    .HighPassCutoff = AL_RING_MODULATOR_DEFAULT_HIGHPASS_CUTOFF,
    .Waveform = detail::ModulatorWaveformFromEmum(AL_RING_MODULATOR_DEFAULT_WAVEFORM)};

inline auto constexpr PshifterEffectProps = PshifterProps{
    .CoarseTune = AL_PITCH_SHIFTER_DEFAULT_COARSE_TUNE,
    .FineTune = AL_PITCH_SHIFTER_DEFAULT_FINE_TUNE};

inline auto constexpr VmorpherEffectProps = VmorpherProps{
    .Rate = AL_VOCAL_MORPHER_DEFAULT_RATE,
    .PhonemeA = detail::PhenomeFromEnum(AL_VOCAL_MORPHER_DEFAULT_PHONEMEA),
    .PhonemeB = detail::PhenomeFromEnum(AL_VOCAL_MORPHER_DEFAULT_PHONEMEB),
    .PhonemeACoarseTuning = AL_VOCAL_MORPHER_DEFAULT_PHONEMEA_COARSE_TUNING,
    .PhonemeBCoarseTuning = AL_VOCAL_MORPHER_DEFAULT_PHONEMEB_COARSE_TUNING,
    .Waveform = detail::VMorpherWaveformFromEmum(AL_VOCAL_MORPHER_DEFAULT_WAVEFORM)};

inline auto constexpr DedicatedDialogEffectProps = DedicatedProps{
    .Target = DedicatedProps::Dialog,
    .Gain = 1.0f};

inline auto constexpr DedicatedLfeEffectProps = DedicatedProps{
    .Target = DedicatedProps::Lfe,
    .Gain = 1.0f};

inline auto constexpr ConvolutionEffectProps = ConvolutionProps{
    .OrientAt = {0.0f,  0.0f, -1.0f},
    .OrientUp = {0.0f,  1.0f,  0.0f}};

#endif /* AL_EFFECTS_HANDLERS_HPP */
