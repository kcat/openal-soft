/* The EFX module provides the interfaces for the EFX extension only. */

module;

#include <array>

#ifndef AL_DISABLE_NOEXCEPT
#define AL_API_NOEXCEPT noexcept
#else
#define AL_API_NOEXCEPT
#endif

#ifndef AL_API
 #if defined(AL_LIBTYPE_STATIC)
  #define AL_API
 #elif defined(_WIN32)
  #define AL_API __declspec(dllimport)
 #else
  #define AL_API extern
 #endif
#endif

#ifdef _WIN32
 #define AL_APIENTRY __cdecl
#else
 #define AL_APIENTRY
#endif

export module openal.efx;

import openal;

export extern "C" {

inline constexpr auto ALC_EXT_EFX_NAME =         std::to_array<const ALCchar>("ALC_EXT_EFX");

/* Enumeration values begin at column 50. Do not use tabs. */
#define ENUMDCL inline constexpr auto

ENUMDCL ALC_EFX_MAJOR_VERSION =                  0x20001;
ENUMDCL ALC_EFX_MINOR_VERSION =                  0x20002;
ENUMDCL ALC_MAX_AUXILIARY_SENDS =                0x20003;


/* Listener properties. */
ENUMDCL AL_METERS_PER_UNIT =                     0x20004;

/* Source properties. */
ENUMDCL AL_DIRECT_FILTER =                       0x20005;
ENUMDCL AL_AUXILIARY_SEND_FILTER =               0x20006;
ENUMDCL AL_AIR_ABSORPTION_FACTOR =               0x20007;
ENUMDCL AL_ROOM_ROLLOFF_FACTOR =                 0x20008;
ENUMDCL AL_CONE_OUTER_GAINHF =                   0x20009;
ENUMDCL AL_DIRECT_FILTER_GAINHF_AUTO =           0x2000A;
ENUMDCL AL_AUXILIARY_SEND_FILTER_GAIN_AUTO =     0x2000B;
ENUMDCL AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO =   0x2000C;


/* Effect properties. */

/* Reverb effect parameters */
ENUMDCL AL_REVERB_DENSITY =                      0x0001;
ENUMDCL AL_REVERB_DIFFUSION =                    0x0002;
ENUMDCL AL_REVERB_GAIN =                         0x0003;
ENUMDCL AL_REVERB_GAINHF =                       0x0004;
ENUMDCL AL_REVERB_DECAY_TIME =                   0x0005;
ENUMDCL AL_REVERB_DECAY_HFRATIO =                0x0006;
ENUMDCL AL_REVERB_REFLECTIONS_GAIN =             0x0007;
ENUMDCL AL_REVERB_REFLECTIONS_DELAY =            0x0008;
ENUMDCL AL_REVERB_LATE_REVERB_GAIN =             0x0009;
ENUMDCL AL_REVERB_LATE_REVERB_DELAY =            0x000A;
ENUMDCL AL_REVERB_AIR_ABSORPTION_GAINHF =        0x000B;
ENUMDCL AL_REVERB_ROOM_ROLLOFF_FACTOR =          0x000C;
ENUMDCL AL_REVERB_DECAY_HFLIMIT =                0x000D;

/* EAX Reverb effect parameters */
ENUMDCL AL_EAXREVERB_DENSITY =                   0x0001;
ENUMDCL AL_EAXREVERB_DIFFUSION =                 0x0002;
ENUMDCL AL_EAXREVERB_GAIN =                      0x0003;
ENUMDCL AL_EAXREVERB_GAINHF =                    0x0004;
ENUMDCL AL_EAXREVERB_GAINLF =                    0x0005;
ENUMDCL AL_EAXREVERB_DECAY_TIME =                0x0006;
ENUMDCL AL_EAXREVERB_DECAY_HFRATIO =             0x0007;
ENUMDCL AL_EAXREVERB_DECAY_LFRATIO =             0x0008;
ENUMDCL AL_EAXREVERB_REFLECTIONS_GAIN =          0x0009;
ENUMDCL AL_EAXREVERB_REFLECTIONS_DELAY =         0x000A;
ENUMDCL AL_EAXREVERB_REFLECTIONS_PAN =           0x000B;
ENUMDCL AL_EAXREVERB_LATE_REVERB_GAIN =          0x000C;
ENUMDCL AL_EAXREVERB_LATE_REVERB_DELAY =         0x000D;
ENUMDCL AL_EAXREVERB_LATE_REVERB_PAN =           0x000E;
ENUMDCL AL_EAXREVERB_ECHO_TIME =                 0x000F;
ENUMDCL AL_EAXREVERB_ECHO_DEPTH =                0x0010;
ENUMDCL AL_EAXREVERB_MODULATION_TIME =           0x0011;
ENUMDCL AL_EAXREVERB_MODULATION_DEPTH =          0x0012;
ENUMDCL AL_EAXREVERB_AIR_ABSORPTION_GAINHF =     0x0013;
ENUMDCL AL_EAXREVERB_HFREFERENCE =               0x0014;
ENUMDCL AL_EAXREVERB_LFREFERENCE =               0x0015;
ENUMDCL AL_EAXREVERB_ROOM_ROLLOFF_FACTOR =       0x0016;
ENUMDCL AL_EAXREVERB_DECAY_HFLIMIT =             0x0017;

/* Chorus effect parameters */
ENUMDCL AL_CHORUS_WAVEFORM =                     0x0001;
ENUMDCL AL_CHORUS_PHASE =                        0x0002;
ENUMDCL AL_CHORUS_RATE =                         0x0003;
ENUMDCL AL_CHORUS_DEPTH =                        0x0004;
ENUMDCL AL_CHORUS_FEEDBACK =                     0x0005;
ENUMDCL AL_CHORUS_DELAY =                        0x0006;

/* Distortion effect parameters */
ENUMDCL AL_DISTORTION_EDGE =                     0x0001;
ENUMDCL AL_DISTORTION_GAIN =                     0x0002;
ENUMDCL AL_DISTORTION_LOWPASS_CUTOFF =           0x0003;
ENUMDCL AL_DISTORTION_EQCENTER =                 0x0004;
ENUMDCL AL_DISTORTION_EQBANDWIDTH =              0x0005;

/* Echo effect parameters */
ENUMDCL AL_ECHO_DELAY =                          0x0001;
ENUMDCL AL_ECHO_LRDELAY =                        0x0002;
ENUMDCL AL_ECHO_DAMPING =                        0x0003;
ENUMDCL AL_ECHO_FEEDBACK =                       0x0004;
ENUMDCL AL_ECHO_SPREAD =                         0x0005;

/* Flanger effect parameters */
ENUMDCL AL_FLANGER_WAVEFORM =                    0x0001;
ENUMDCL AL_FLANGER_PHASE =                       0x0002;
ENUMDCL AL_FLANGER_RATE =                        0x0003;
ENUMDCL AL_FLANGER_DEPTH =                       0x0004;
ENUMDCL AL_FLANGER_FEEDBACK =                    0x0005;
ENUMDCL AL_FLANGER_DELAY =                       0x0006;

/* Frequency shifter effect parameters */
ENUMDCL AL_FREQUENCY_SHIFTER_FREQUENCY =         0x0001;
ENUMDCL AL_FREQUENCY_SHIFTER_LEFT_DIRECTION =    0x0002;
ENUMDCL AL_FREQUENCY_SHIFTER_RIGHT_DIRECTION =   0x0003;

/* Vocal morpher effect parameters */
ENUMDCL AL_VOCAL_MORPHER_PHONEMEA =              0x0001;
ENUMDCL AL_VOCAL_MORPHER_PHONEMEA_COARSE_TUNING =0x0002;
ENUMDCL AL_VOCAL_MORPHER_PHONEMEB =              0x0003;
ENUMDCL AL_VOCAL_MORPHER_PHONEMEB_COARSE_TUNING =0x0004;
ENUMDCL AL_VOCAL_MORPHER_WAVEFORM =              0x0005;
ENUMDCL AL_VOCAL_MORPHER_RATE =                  0x0006;

/* Pitchshifter effect parameters */
ENUMDCL AL_PITCH_SHIFTER_COARSE_TUNE =           0x0001;
ENUMDCL AL_PITCH_SHIFTER_FINE_TUNE =             0x0002;

/* Ringmodulator effect parameters */
ENUMDCL AL_RING_MODULATOR_FREQUENCY =            0x0001;
ENUMDCL AL_RING_MODULATOR_HIGHPASS_CUTOFF =      0x0002;
ENUMDCL AL_RING_MODULATOR_WAVEFORM =             0x0003;

/* Autowah effect parameters */
ENUMDCL AL_AUTOWAH_ATTACK_TIME =                 0x0001;
ENUMDCL AL_AUTOWAH_RELEASE_TIME =                0x0002;
ENUMDCL AL_AUTOWAH_RESONANCE =                   0x0003;
ENUMDCL AL_AUTOWAH_PEAK_GAIN =                   0x0004;

/* Compressor effect parameters */
ENUMDCL AL_COMPRESSOR_ONOFF =                    0x0001;

/* Equalizer effect parameters */
ENUMDCL AL_EQUALIZER_LOW_GAIN =                  0x0001;
ENUMDCL AL_EQUALIZER_LOW_CUTOFF =                0x0002;
ENUMDCL AL_EQUALIZER_MID1_GAIN =                 0x0003;
ENUMDCL AL_EQUALIZER_MID1_CENTER =               0x0004;
ENUMDCL AL_EQUALIZER_MID1_WIDTH =                0x0005;
ENUMDCL AL_EQUALIZER_MID2_GAIN =                 0x0006;
ENUMDCL AL_EQUALIZER_MID2_CENTER =               0x0007;
ENUMDCL AL_EQUALIZER_MID2_WIDTH =                0x0008;
ENUMDCL AL_EQUALIZER_HIGH_GAIN =                 0x0009;
ENUMDCL AL_EQUALIZER_HIGH_CUTOFF =               0x000A;

/* Effect type */
ENUMDCL AL_EFFECT_FIRST_PARAMETER =              0x0000;
ENUMDCL AL_EFFECT_LAST_PARAMETER =               0x8000;
ENUMDCL AL_EFFECT_TYPE =                         0x8001;

/* Effect types, used with the AL_EFFECT_TYPE property */
ENUMDCL AL_EFFECT_NULL =                         0x0000;
ENUMDCL AL_EFFECT_REVERB =                       0x0001;
ENUMDCL AL_EFFECT_CHORUS =                       0x0002;
ENUMDCL AL_EFFECT_DISTORTION =                   0x0003;
ENUMDCL AL_EFFECT_ECHO =                         0x0004;
ENUMDCL AL_EFFECT_FLANGER =                      0x0005;
ENUMDCL AL_EFFECT_FREQUENCY_SHIFTER =            0x0006;
ENUMDCL AL_EFFECT_VOCAL_MORPHER =                0x0007;
ENUMDCL AL_EFFECT_PITCH_SHIFTER =                0x0008;
ENUMDCL AL_EFFECT_RING_MODULATOR =               0x0009;
ENUMDCL AL_EFFECT_AUTOWAH =                      0x000A;
ENUMDCL AL_EFFECT_COMPRESSOR =                   0x000B;
ENUMDCL AL_EFFECT_EQUALIZER =                    0x000C;
ENUMDCL AL_EFFECT_EAXREVERB =                    0x8000;

/* Auxiliary Effect Slot properties. */
ENUMDCL AL_EFFECTSLOT_EFFECT =                   0x0001;
ENUMDCL AL_EFFECTSLOT_GAIN =                     0x0002;
ENUMDCL AL_EFFECTSLOT_AUXILIARY_SEND_AUTO =      0x0003;

/* NULL Auxiliary Slot ID to disable a source send. */
ENUMDCL AL_EFFECTSLOT_NULL =                     0x0000;


/* Filter properties. */

/* Lowpass filter parameters */
ENUMDCL AL_LOWPASS_GAIN =                        0x0001;
ENUMDCL AL_LOWPASS_GAINHF =                      0x0002;

/* Highpass filter parameters */
ENUMDCL AL_HIGHPASS_GAIN =                       0x0001;
ENUMDCL AL_HIGHPASS_GAINLF =                     0x0002;

/* Bandpass filter parameters */
ENUMDCL AL_BANDPASS_GAIN =                       0x0001;
ENUMDCL AL_BANDPASS_GAINLF =                     0x0002;
ENUMDCL AL_BANDPASS_GAINHF =                     0x0003;

/* Filter type */
ENUMDCL AL_FILTER_FIRST_PARAMETER =              0x0000;
ENUMDCL AL_FILTER_LAST_PARAMETER =               0x8000;
ENUMDCL AL_FILTER_TYPE =                         0x8001;

/* Filter types, used with the AL_FILTER_TYPE property */
ENUMDCL AL_FILTER_NULL =                         0x0000;
ENUMDCL AL_FILTER_LOWPASS =                      0x0001;
ENUMDCL AL_FILTER_HIGHPASS =                     0x0002;
ENUMDCL AL_FILTER_BANDPASS =                     0x0003;
#undef ENUMDCL


/* Effect object function types. */
using LPALGENEFFECTS =    void (AL_APIENTRY*)(ALsizei, ALuint*) AL_API_NOEXCEPT;
using LPALDELETEEFFECTS = void (AL_APIENTRY*)(ALsizei, const ALuint*) AL_API_NOEXCEPT;
using LPALISEFFECT =      auto (AL_APIENTRY*)(ALuint) AL_API_NOEXCEPT -> ALboolean;
using LPALEFFECTI =       void (AL_APIENTRY*)(ALuint, ALenum, ALint) AL_API_NOEXCEPT;
using LPALEFFECTIV =      void (AL_APIENTRY*)(ALuint, ALenum, const ALint*) AL_API_NOEXCEPT;
using LPALEFFECTF =       void (AL_APIENTRY*)(ALuint, ALenum, ALfloat) AL_API_NOEXCEPT;
using LPALEFFECTFV =      void (AL_APIENTRY*)(ALuint, ALenum, const ALfloat*) AL_API_NOEXCEPT;
using LPALGETEFFECTI =    void (AL_APIENTRY*)(ALuint, ALenum, ALint*) AL_API_NOEXCEPT;
using LPALGETEFFECTIV =   void (AL_APIENTRY*)(ALuint, ALenum, ALint*) AL_API_NOEXCEPT;
using LPALGETEFFECTF =    void (AL_APIENTRY*)(ALuint, ALenum, ALfloat*) AL_API_NOEXCEPT;
using LPALGETEFFECTFV =   void (AL_APIENTRY*)(ALuint, ALenum, ALfloat*) AL_API_NOEXCEPT;

/* Filter object function types. */
using LPALGENFILTERS =    void (AL_APIENTRY*)(ALsizei, ALuint*) AL_API_NOEXCEPT;
using LPALDELETEFILTERS = void (AL_APIENTRY*)(ALsizei, const ALuint*) AL_API_NOEXCEPT;
using LPALISFILTER =      auto (AL_APIENTRY*)(ALuint) AL_API_NOEXCEPT -> ALboolean;
using LPALFILTERI =       void (AL_APIENTRY*)(ALuint, ALenum, ALint) AL_API_NOEXCEPT;
using LPALFILTERIV =      void (AL_APIENTRY*)(ALuint, ALenum, const ALint*) AL_API_NOEXCEPT;
using LPALFILTERF =       void (AL_APIENTRY*)(ALuint, ALenum, ALfloat) AL_API_NOEXCEPT;
using LPALFILTERFV =      void (AL_APIENTRY*)(ALuint, ALenum, const ALfloat*) AL_API_NOEXCEPT;
using LPALGETFILTERI =    void (AL_APIENTRY*)(ALuint, ALenum, ALint*) AL_API_NOEXCEPT;
using LPALGETFILTERIV =   void (AL_APIENTRY*)(ALuint, ALenum, ALint*) AL_API_NOEXCEPT;
using LPALGETFILTERF =    void (AL_APIENTRY*)(ALuint, ALenum, ALfloat*) AL_API_NOEXCEPT;
using LPALGETFILTERFV =   void (AL_APIENTRY*)(ALuint, ALenum, ALfloat*) AL_API_NOEXCEPT;

/* Auxiliary Effect Slot object function types. */
using LPALGENAUXILIARYEFFECTSLOTS =    void (AL_APIENTRY*)(ALsizei, ALuint*) AL_API_NOEXCEPT;
using LPALDELETEAUXILIARYEFFECTSLOTS = void (AL_APIENTRY*)(ALsizei, const ALuint*) AL_API_NOEXCEPT;
using LPALISAUXILIARYEFFECTSLOT =      auto (AL_APIENTRY*)(ALuint) AL_API_NOEXCEPT -> ALboolean;
using LPALAUXILIARYEFFECTSLOTI =       void (AL_APIENTRY*)(ALuint, ALenum, ALint) AL_API_NOEXCEPT;
using LPALAUXILIARYEFFECTSLOTIV =      void (AL_APIENTRY*)(ALuint, ALenum, const ALint*) AL_API_NOEXCEPT;
using LPALAUXILIARYEFFECTSLOTF =       void (AL_APIENTRY*)(ALuint, ALenum, ALfloat) AL_API_NOEXCEPT;
using LPALAUXILIARYEFFECTSLOTFV =      void (AL_APIENTRY*)(ALuint, ALenum, const ALfloat*) AL_API_NOEXCEPT;
using LPALGETAUXILIARYEFFECTSLOTI =    void (AL_APIENTRY*)(ALuint, ALenum, ALint*) AL_API_NOEXCEPT;
using LPALGETAUXILIARYEFFECTSLOTIV =   void (AL_APIENTRY*)(ALuint, ALenum, ALint*) AL_API_NOEXCEPT;
using LPALGETAUXILIARYEFFECTSLOTF =    void (AL_APIENTRY*)(ALuint, ALenum, ALfloat*) AL_API_NOEXCEPT;
using LPALGETAUXILIARYEFFECTSLOTFV =   void (AL_APIENTRY*)(ALuint, ALenum, ALfloat*) AL_API_NOEXCEPT;

#ifdef AL_ALEXT_PROTOTYPES
AL_API void AL_APIENTRY alGenEffects(ALsizei n, ALuint *effects) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alDeleteEffects(ALsizei n, const ALuint *effects) AL_API_NOEXCEPT;
AL_API auto AL_APIENTRY alIsEffect(ALuint effect) AL_API_NOEXCEPT -> ALboolean;
AL_API void AL_APIENTRY alEffecti(ALuint effect, ALenum param, ALint iValue) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alEffectiv(ALuint effect, ALenum param, const ALint *piValues) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alEffectf(ALuint effect, ALenum param, ALfloat flValue) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alEffectfv(ALuint effect, ALenum param, const ALfloat *pflValues) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetEffecti(ALuint effect, ALenum param, ALint *piValue) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetEffectiv(ALuint effect, ALenum param, ALint *piValues) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetEffectf(ALuint effect, ALenum param, ALfloat *pflValue) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetEffectfv(ALuint effect, ALenum param, ALfloat *pflValues) AL_API_NOEXCEPT;

AL_API void AL_APIENTRY alGenFilters(ALsizei n, ALuint *filters) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alDeleteFilters(ALsizei n, const ALuint *filters) AL_API_NOEXCEPT;
AL_API auto AL_APIENTRY alIsFilter(ALuint filter) AL_API_NOEXCEPT -> ALboolean;
AL_API void AL_APIENTRY alFilteri(ALuint filter, ALenum param, ALint iValue) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alFilteriv(ALuint filter, ALenum param, const ALint *piValues) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alFilterf(ALuint filter, ALenum param, ALfloat flValue) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alFilterfv(ALuint filter, ALenum param, const ALfloat *pflValues) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetFilteri(ALuint filter, ALenum param, ALint *piValue) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetFilteriv(ALuint filter, ALenum param, ALint *piValues) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetFilterf(ALuint filter, ALenum param, ALfloat *pflValue) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetFilterfv(ALuint filter, ALenum param, ALfloat *pflValues) AL_API_NOEXCEPT;

AL_API void AL_APIENTRY alGenAuxiliaryEffectSlots(ALsizei n, ALuint *effectslots) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alDeleteAuxiliaryEffectSlots(ALsizei n, const ALuint *effectslots) AL_API_NOEXCEPT;
AL_API auto AL_APIENTRY alIsAuxiliaryEffectSlot(ALuint effectslot) AL_API_NOEXCEPT -> ALboolean;
AL_API void AL_APIENTRY alAuxiliaryEffectSloti(ALuint effectslot, ALenum param, ALint iValue) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alAuxiliaryEffectSlotiv(ALuint effectslot, ALenum param, const ALint *piValues) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alAuxiliaryEffectSlotf(ALuint effectslot, ALenum param, ALfloat flValue) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alAuxiliaryEffectSlotfv(ALuint effectslot, ALenum param, const ALfloat *pflValues) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetAuxiliaryEffectSloti(ALuint effectslot, ALenum param, ALint *piValue) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetAuxiliaryEffectSlotiv(ALuint effectslot, ALenum param, ALint *piValues) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetAuxiliaryEffectSlotf(ALuint effectslot, ALenum param, ALfloat *pflValue) AL_API_NOEXCEPT;
AL_API void AL_APIENTRY alGetAuxiliaryEffectSlotfv(ALuint effectslot, ALenum param, ALfloat *pflValues) AL_API_NOEXCEPT;
#endif

} /* extern "C" */
