#ifndef AL_MAIN_H
#define AL_MAIN_H

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>
#include <limits.h>

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#ifdef HAVE_FENV_H
#include <fenv.h>
#endif

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"


#if defined(_WIN64)
#define SZFMT "%I64u"
#elif defined(_WIN32)
#define SZFMT "%u"
#else
#define SZFMT "%zu"
#endif


#include "static_assert.h"
#include "align.h"
#include "atomic.h"
#include "uintmap.h"
#include "vector.h"
#include "alstring.h"

#ifndef ALC_SOFT_HRTF
#define ALC_SOFT_HRTF 1
#define ALC_HRTF_SOFT                            0x1992
#endif

#ifndef ALC_SOFT_midi_interface
#define ALC_SOFT_midi_interface 1
/* Global properties */
#define AL_MIDI_CLOCK_SOFT                       0x9999
#define AL_MIDI_STATE_SOFT                       0x9986
#define AL_MIDI_GAIN_SOFT                        0x9998
#define AL_SOUNDFONTS_SIZE_SOFT                  0x9995
#define AL_SOUNDFONTS_SOFT                       0x9994

/* Soundfont properties */
#define AL_PRESETS_SIZE_SOFT                     0x9993
#define AL_PRESETS_SOFT                          0x9992

/* Preset properties */
#define AL_MIDI_PRESET_SOFT                      0x9997
#define AL_MIDI_BANK_SOFT                        0x9996
#define AL_FONTSOUNDS_SIZE_SOFT                  0x9991
#define AL_FONTSOUNDS_SOFT                       0x9990

/* Fontsound properties */
/* AL_BUFFER */
#define AL_SAMPLE_START_SOFT                     0x2000
#define AL_SAMPLE_END_SOFT                       0x2001
#define AL_SAMPLE_LOOP_START_SOFT                0x2002
#define AL_SAMPLE_LOOP_END_SOFT                  0x2003
#define AL_SAMPLE_RATE_SOFT                      0x2004
#define AL_BASE_KEY_SOFT                         0x2005
#define AL_KEY_CORRECTION_SOFT                   0x2006
#define AL_SAMPLE_TYPE_SOFT                      0x2007
#define AL_FONTSOUND_LINK_SOFT                   0x2008
#define AL_MOD_LFO_TO_PITCH_SOFT                 0x0005
#define AL_VIBRATO_LFO_TO_PITCH_SOFT             0x0006
#define AL_MOD_ENV_TO_PITCH_SOFT                 0x0007
#define AL_FILTER_CUTOFF_SOFT                    0x0008
#define AL_FILTER_RESONANCE_SOFT                 0x0009
#define AL_MOD_LFO_TO_FILTER_CUTOFF_SOFT         0x000A
#define AL_MOD_ENV_TO_FILTER_CUTOFF_SOFT         0x000B
#define AL_MOD_LFO_TO_VOLUME_SOFT                0x000D
#define AL_CHORUS_SEND_SOFT                      0x000F
#define AL_REVERB_SEND_SOFT                      0x0010
#define AL_PAN_SOFT                              0x0011
#define AL_MOD_LFO_DELAY_SOFT                    0x0015
#define AL_MOD_LFO_FREQUENCY_SOFT                0x0016
#define AL_VIBRATO_LFO_DELAY_SOFT                0x0017
#define AL_VIBRATO_LFO_FREQUENCY_SOFT            0x0018
#define AL_MOD_ENV_DELAYTIME_SOFT                0x0019
#define AL_MOD_ENV_ATTACKTIME_SOFT               0x001A
#define AL_MOD_ENV_HOLDTIME_SOFT                 0x001B
#define AL_MOD_ENV_DECAYTIME_SOFT                0x001C
#define AL_MOD_ENV_SUSTAINVOLUME_SOFT            0x001D
#define AL_MOD_ENV_RELEASETIME_SOFT              0x002E
#define AL_MOD_ENV_KEY_TO_HOLDTIME_SOFT          0x001F
#define AL_MOD_ENV_KEY_TO_DECAYTIME_SOFT         0x0020
#define AL_VOLUME_ENV_DELAYTIME_SOFT             0x0021
#define AL_VOLUME_ENV_ATTACKTIME_SOFT            0x0022
#define AL_VOLUME_ENV_HOLDTIME_SOFT              0x0023
#define AL_VOLUME_ENV_DECAYTIME_SOFT             0x0024
#define AL_VOLUME_ENV_SUSTAINVOLUME_SOFT         0x0025
#define AL_VOLUME_ENV_RELEASETIME_SOFT           0x0026
#define AL_VOLUME_ENV_KEY_TO_HOLDTIME_SOFT       0x0027
#define AL_VOLUME_ENV_KEY_TO_DECAYTIME_SOFT      0x0028
#define AL_KEY_RANGE_SOFT                        0x002B
#define AL_VELOCITY_RANGE_SOFT                   0x002C
#define AL_ATTENUATION_SOFT                      0x0030
#define AL_TUNING_COARSE_SOFT                    0x0033
#define AL_TUNING_FINE_SOFT                      0x0034
#define AL_LOOP_MODE_SOFT                        0x0036
#define AL_TUNING_SCALE_SOFT                     0x0038
#define AL_EXCLUSIVE_CLASS_SOFT                  0x0039

/* Sample Types */
/* AL_MONO_SOFT */
#define AL_RIGHT_SOFT                            0x0002
#define AL_LEFT_SOFT                             0x0004

/* Loop Modes */
/* AL_NONE */
#define AL_LOOP_CONTINUOUS_SOFT                  0x0001
#define AL_LOOP_UNTIL_RELEASE_SOFT               0x0003

/* Fontsound modulator stage properties */
#define AL_SOURCE0_INPUT_SOFT                    0x998F
#define AL_SOURCE0_TYPE_SOFT                     0x998E
#define AL_SOURCE0_FORM_SOFT                     0x998D
#define AL_SOURCE1_INPUT_SOFT                    0x998C
#define AL_SOURCE1_TYPE_SOFT                     0x998B
#define AL_SOURCE1_FORM_SOFT                     0x998A
#define AL_AMOUNT_SOFT                           0x9989
#define AL_TRANSFORM_OP_SOFT                     0x9988
#define AL_DESTINATION_SOFT                      0x9987

/* Sounce Inputs */
#define AL_ONE_SOFT                              0x0080
#define AL_NOTEON_VELOCITY_SOFT                  0x0082
#define AL_NOTEON_KEY_SOFT                       0x0083
/* AL_KEYPRESSURE_SOFT */
/* AL_CHANNELPRESSURE_SOFT */
/* AL_PITCHBEND_SOFT */
#define AL_PITCHBEND_SENSITIVITY_SOFT            0x0090
/* CC 0...127 */

/* Source Types */
#define AL_UNORM_SOFT                            0x0000
#define AL_UNORM_REV_SOFT                        0x0100
#define AL_SNORM_SOFT                            0x0200
#define AL_SNORM_REV_SOFT                        0x0300

/* Source Forms */
#define AL_LINEAR_SOFT                           0x0000
#define AL_CONCAVE_SOFT                          0x0400
#define AL_CONVEX_SOFT                           0x0800
#define AL_SWITCH_SOFT                           0x0C00

/* Transform Ops */
/* AL_LINEAR_SOFT */
#define AL_ABSOLUTE_SOFT                         0x0002

/* Events */
#define AL_NOTEOFF_SOFT                          0x0080
#define AL_NOTEON_SOFT                           0x0090
#define AL_KEYPRESSURE_SOFT                      0x00A0
#define AL_CONTROLLERCHANGE_SOFT                 0x00B0
#define AL_PROGRAMCHANGE_SOFT                    0x00C0
#define AL_CHANNELPRESSURE_SOFT                  0x00D0
#define AL_PITCHBEND_SOFT                        0x00E0

typedef void (AL_APIENTRY*LPALGENSOUNDFONTSSOFT)(ALsizei n, ALuint *ids);
typedef void (AL_APIENTRY*LPALDELETESOUNDFONTSSOFT)(ALsizei n, const ALuint *ids);
typedef ALboolean (AL_APIENTRY*LPALISSOUNDFONTSOFT)(ALuint id);
typedef void (AL_APIENTRY*LPALGETSOUNDFONTIVSOFT)(ALuint id, ALenum param, ALint *values);
typedef void (AL_APIENTRY*LPALSOUNDFONTPRESETSSOFT)(ALuint id, ALsizei count, const ALuint *pids);
typedef void (AL_APIENTRY*LPALGENPRESETSSOFT)(ALsizei n, ALuint *ids);
typedef void (AL_APIENTRY*LPALDELETEPRESETSSOFT)(ALsizei n, const ALuint *ids);
typedef ALboolean (AL_APIENTRY*LPALISPRESETSOFT)(ALuint id);
typedef void (AL_APIENTRY*LPALPRESETISOFT)(ALuint id, ALenum param, ALint value);
typedef void (AL_APIENTRY*LPALPRESETIVSOFT)(ALuint id, ALenum param, const ALint *values);
typedef void (AL_APIENTRY*LPALPRESETFONTSOUNDSSOFT)(ALuint id, ALsizei count, const ALuint *fsids);
typedef void (AL_APIENTRY*LPALGETPRESETIVSOFT)(ALuint id, ALenum param, ALint *values);
typedef void (AL_APIENTRY*LPALGENFONTSOUNDSSOFT)(ALsizei n, ALuint *ids);
typedef void (AL_APIENTRY*LPALDELETEFONTSOUNDSSOFT)(ALsizei n, const ALuint *ids);
typedef ALboolean (AL_APIENTRY*LPALISFONTSOUNDSOFT)(ALuint id);
typedef void (AL_APIENTRY*LPALFONTSOUNDISOFT)(ALuint id, ALenum param, ALint value);
typedef void (AL_APIENTRY*LPALFONTSOUND2ISOFT)(ALuint id, ALenum param, ALint value1, ALint value2);
typedef void (AL_APIENTRY*LPALFONTSOUNDIVSOFT)(ALuint id, ALenum param, const ALint *values);
typedef void (AL_APIENTRY*LPALGETFONTSOUNDIVSOFT)(ALuint id, ALenum param, ALint *values);
typedef void (AL_APIENTRY*LPALFONTSOUNDMOFULATORISOFT)(ALuint id, ALsizei stage, ALenum param, ALint value);
typedef void (AL_APIENTRY*LPALGETFONTSOUNDMODULATORIVSOFT)(ALuint id, ALsizei stage, ALenum param, ALint *values);
typedef void (AL_APIENTRY*LPALMIDISOUNDFONTSOFT)(ALuint id);
typedef void (AL_APIENTRY*LPALMIDISOUNDFONTVSOFT)(ALsizei count, const ALuint *ids);
typedef void (AL_APIENTRY*LPALMIDIEVENTSOFT)(ALuint64SOFT time, ALenum event, ALsizei channel, ALsizei param1, ALsizei param2);
typedef void (AL_APIENTRY*LPALMIDISYSEXSOFT)(ALuint64SOFT time, const ALbyte *data, ALsizei size);
typedef void (AL_APIENTRY*LPALMIDIPLAYSOFT)(void);
typedef void (AL_APIENTRY*LPALMIDIPAUSESOFT)(void);
typedef void (AL_APIENTRY*LPALMIDISTOPSOFT)(void);
typedef void (AL_APIENTRY*LPALMIDIRESETSOFT)(void);
typedef void (AL_APIENTRY*LPALMIDIGAINSOFT)(ALfloat value);
typedef ALint64SOFT (AL_APIENTRY*LPALGETINTEGER64SOFT)(ALenum pname);
typedef void (AL_APIENTRY*LPALGETINTEGER64VSOFT)(ALenum pname, ALint64SOFT *values);
typedef void (AL_APIENTRY*LPALLOADSOUNDFONTSOFT)(ALuint id, size_t(*cb)(ALvoid*,size_t,ALvoid*), ALvoid *user);
#ifdef AL_ALEXT_PROTOTYPES
AL_API void AL_APIENTRY alGenSoundfontsSOFT(ALsizei n, ALuint *ids);
AL_API void AL_APIENTRY alDeleteSoundfontsSOFT(ALsizei n, const ALuint *ids);
AL_API ALboolean AL_APIENTRY alIsSoundfontSOFT(ALuint id);
AL_API void AL_APIENTRY alGetSoundfontivSOFT(ALuint id, ALenum param, ALint *values);
AL_API void AL_APIENTRY alSoundfontPresetsSOFT(ALuint id, ALsizei count, const ALuint *pids);

AL_API void AL_APIENTRY alGenPresetsSOFT(ALsizei n, ALuint *ids);
AL_API void AL_APIENTRY alDeletePresetsSOFT(ALsizei n, const ALuint *ids);
AL_API ALboolean AL_APIENTRY alIsPresetSOFT(ALuint id);
AL_API void AL_APIENTRY alPresetiSOFT(ALuint id, ALenum param, ALint value);
AL_API void AL_APIENTRY alPresetivSOFT(ALuint id, ALenum param, const ALint *values);
AL_API void AL_APIENTRY alGetPresetivSOFT(ALuint id, ALenum param, ALint *values);
AL_API void AL_APIENTRY alPresetFontsoundsSOFT(ALuint id, ALsizei count, const ALuint *fsids);

AL_API void AL_APIENTRY alGenFontsoundsSOFT(ALsizei n, ALuint *ids);
AL_API void AL_APIENTRY alDeleteFontsoundsSOFT(ALsizei n, const ALuint *ids);
AL_API ALboolean AL_APIENTRY alIsFontsoundSOFT(ALuint id);
AL_API void AL_APIENTRY alFontsoundiSOFT(ALuint id, ALenum param, ALint value);
AL_API void AL_APIENTRY alFontsound2iSOFT(ALuint id, ALenum param, ALint value1, ALint value2);
AL_API void AL_APIENTRY alFontsoundivSOFT(ALuint id, ALenum param, const ALint *values);
AL_API void AL_APIENTRY alGetFontsoundivSOFT(ALuint id, ALenum param, ALint *values);
AL_API void AL_APIENTRY alFontsoundModulatoriSOFT(ALuint id, ALsizei stage, ALenum param, ALint value);
AL_API void AL_APIENTRY alGetFontsoundModulatorivSOFT(ALuint id, ALsizei stage, ALenum param, ALint *values);

AL_API void AL_APIENTRY alMidiSoundfontSOFT(ALuint id);
AL_API void AL_APIENTRY alMidiSoundfontvSOFT(ALsizei count, const ALuint *ids);
AL_API void AL_APIENTRY alMidiEventSOFT(ALuint64SOFT time, ALenum event, ALsizei channel, ALsizei param1, ALsizei param2);
AL_API void AL_APIENTRY alMidiSysExSOFT(ALuint64SOFT time, const ALbyte *data, ALsizei size);
AL_API void AL_APIENTRY alMidiPlaySOFT(void);
AL_API void AL_APIENTRY alMidiPauseSOFT(void);
AL_API void AL_APIENTRY alMidiStopSOFT(void);
AL_API void AL_APIENTRY alMidiResetSOFT(void);
AL_API void AL_APIENTRY alMidiGainSOFT(ALfloat value);
AL_API ALint64SOFT AL_APIENTRY alGetInteger64SOFT(ALenum pname);
AL_API void AL_APIENTRY alGetInteger64vSOFT(ALenum pname, ALint64SOFT *values);
AL_API void AL_APIENTRY alLoadSoundfontSOFT(ALuint id, size_t(*cb)(ALvoid*,size_t,ALvoid*), ALvoid *user);
#endif
#endif

#ifndef ALC_SOFT_device_clock
#define ALC_SOFT_device_clock 1
typedef int64_t ALCint64SOFT;
typedef uint64_t ALCuint64SOFT;
#define ALC_DEVICE_CLOCK_SOFT                    0x1600
typedef void (ALC_APIENTRY*LPALCGETINTEGER64VSOFT)(ALCdevice *device, ALCenum pname, ALsizei size, ALCint64SOFT *values);
#ifdef AL_ALEXT_PROTOTYPES
ALC_API void ALC_APIENTRY alcGetInteger64vSOFT(ALCdevice *device, ALCenum pname, ALsizei size, ALCint64SOFT *values);
#endif
#endif


#ifdef IN_IDE_PARSER
/* KDevelop's parser doesn't recognize the C99-standard restrict keyword, but
 * recent versions (at least 4.5.1) do recognize GCC's __restrict. */
#define restrict __restrict
#endif


typedef ALint64SOFT ALint64;
typedef ALuint64SOFT ALuint64;

typedef ptrdiff_t ALintptrEXT;
typedef ptrdiff_t ALsizeiptrEXT;

#ifndef U64
#if defined(_MSC_VER)
#define U64(x) ((ALuint64)(x##ui64))
#elif SIZEOF_LONG == 8
#define U64(x) ((ALuint64)(x##ul))
#elif SIZEOF_LONG_LONG == 8
#define U64(x) ((ALuint64)(x##ull))
#endif
#endif

#ifndef UINT64_MAX
#define UINT64_MAX U64(18446744073709551615)
#endif

#ifndef UNUSED
#if defined(__cplusplus)
#define UNUSED(x)
#elif defined(__GNUC__)
#define UNUSED(x) UNUSED_##x __attribute__((unused))
#elif defined(__LCLINT__)
#define UNUSED(x) /*@unused@*/ x
#else
#define UNUSED(x) x
#endif
#endif

#ifdef __GNUC__
#define DECL_CONST __attribute__((const))
#define DECL_FORMAT(x, y, z) __attribute__((format(x, (y), (z))))
#else
#define DECL_CONST
#define DECL_FORMAT(x, y, z)
#endif

#if defined(__GNUC__) && defined(__i386__)
/* force_align_arg_pointer is required for proper function arguments aligning
 * when SSE code is used. Some systems (Windows, QNX) do not guarantee our
 * thread functions will be properly aligned on the stack, even though GCC may
 * generate code with the assumption that it is. */
#define FORCE_ALIGN __attribute__((force_align_arg_pointer))
#else
#define FORCE_ALIGN
#endif

#ifdef HAVE_C99_VLA
#define DECL_VLA(T, _name, _size)  T _name[(_size)]
#else
#define DECL_VLA(T, _name, _size)  T *_name = alloca((_size) * sizeof(T))
#endif

#ifndef PATH_MAX
#ifdef MAX_PATH
#define PATH_MAX MAX_PATH
#else
#define PATH_MAX 4096
#endif
#endif


static const union {
    ALuint u;
    ALubyte b[sizeof(ALuint)];
} EndianTest = { 1 };
#define IS_LITTLE_ENDIAN (EndianTest.b[0] == 1)

#define COUNTOF(x) (sizeof((x))/sizeof((x)[0]))


#define DERIVE_FROM_TYPE(t)          t t##_parent
#define STATIC_CAST(to, obj)         (&(obj)->to##_parent)
#ifdef __GNUC__
#define STATIC_UPCAST(to, from, obj) __extension__({                          \
    static_assert(__builtin_types_compatible_p(from, __typeof(*(obj))),       \
                  "Invalid upcast object from type");                         \
    (to*)((char*)(obj) - offsetof(to, from##_parent));                        \
})
#else
#define STATIC_UPCAST(to, from, obj) ((to*)((char*)(obj) - offsetof(to, from##_parent)))
#endif

#define DECLARE_FORWARD(T1, T2, rettype, func)                                \
rettype T1##_##func(T1 *obj)                                                  \
{ return T2##_##func(STATIC_CAST(T2, obj)); }

#define DECLARE_FORWARD1(T1, T2, rettype, func, argtype1)                     \
rettype T1##_##func(T1 *obj, argtype1 a)                                      \
{ return T2##_##func(STATIC_CAST(T2, obj), a); }

#define DECLARE_FORWARD2(T1, T2, rettype, func, argtype1, argtype2)           \
rettype T1##_##func(T1 *obj, argtype1 a, argtype2 b)                          \
{ return T2##_##func(STATIC_CAST(T2, obj), a, b); }

#define DECLARE_FORWARD3(T1, T2, rettype, func, argtype1, argtype2, argtype3) \
rettype T1##_##func(T1 *obj, argtype1 a, argtype2 b, argtype3 c)              \
{ return T2##_##func(STATIC_CAST(T2, obj), a, b, c); }


#define GET_VTABLE1(T1)     (&(T1##_vtable))
#define GET_VTABLE2(T1, T2) (&(T1##_##T2##_vtable))

#define SET_VTABLE1(T1, obj)     ((obj)->vtbl = GET_VTABLE1(T1))
#define SET_VTABLE2(T1, T2, obj) (STATIC_CAST(T2, obj)->vtbl = GET_VTABLE2(T1, T2))

#define DECLARE_THUNK(T1, T2, rettype, func)                                  \
static rettype T1##_##T2##_##func(T2 *obj)                                    \
{ return T1##_##func(STATIC_UPCAST(T1, T2, obj)); }

#define DECLARE_THUNK1(T1, T2, rettype, func, argtype1)                       \
static rettype T1##_##T2##_##func(T2 *obj, argtype1 a)                        \
{ return T1##_##func(STATIC_UPCAST(T1, T2, obj), a); }

#define DECLARE_THUNK2(T1, T2, rettype, func, argtype1, argtype2)             \
static rettype T1##_##T2##_##func(T2 *obj, argtype1 a, argtype2 b)            \
{ return T1##_##func(STATIC_UPCAST(T1, T2, obj), a, b); }

#define DECLARE_THUNK3(T1, T2, rettype, func, argtype1, argtype2, argtype3)   \
static rettype T1##_##T2##_##func(T2 *obj, argtype1 a, argtype2 b, argtype3 c) \
{ return T1##_##func(STATIC_UPCAST(T1, T2, obj), a, b, c); }

#define DECLARE_DEFAULT_ALLOCATORS(T)                                         \
static void* T##_New(size_t size) { return malloc(size); }                    \
static void T##_Delete(void *ptr) { free(ptr); }

/* Helper to extract an argument list for VCALL. Not used directly. */
#define EXTRACT_VCALL_ARGS(...)  __VA_ARGS__))

/* Call a "virtual" method on an object, with arguments. */
#define V(obj, func)  ((obj)->vtbl->func((obj), EXTRACT_VCALL_ARGS
/* Call a "virtual" method on an object, with no arguments. */
#define V0(obj, func) ((obj)->vtbl->func((obj) EXTRACT_VCALL_ARGS

#define DELETE_OBJ(obj) do {                                                  \
    if((obj) != NULL)                                                         \
    {                                                                         \
        V0((obj),Destruct)();                                                 \
        V0((obj),Delete)();                                                   \
    }                                                                         \
} while(0)


#ifdef __cplusplus
extern "C" {
#endif

struct Hrtf;


#define DEFAULT_OUTPUT_RATE  (44100)
#define MIN_OUTPUT_RATE      (8000)


/* Find the next power-of-2 for non-power-of-2 numbers. */
inline ALuint NextPowerOf2(ALuint value)
{
    if(value > 0)
    {
        value--;
        value |= value>>1;
        value |= value>>2;
        value |= value>>4;
        value |= value>>8;
        value |= value>>16;
    }
    return value+1;
}

/* Fast float-to-int conversion. Assumes the FPU is already in round-to-zero
 * mode. */
inline ALint fastf2i(ALfloat f)
{
#ifdef HAVE_LRINTF
    return lrintf(f);
#elif defined(_MSC_VER) && defined(_M_IX86)
    ALint i;
    __asm fld f
    __asm fistp i
    return i;
#else
    return (ALint)f;
#endif
}

/* Fast float-to-uint conversion. Assumes the FPU is already in round-to-zero
 * mode. */
inline ALuint fastf2u(ALfloat f)
{ return fastf2i(f); }


enum DevProbe {
    ALL_DEVICE_PROBE,
    CAPTURE_DEVICE_PROBE
};

typedef struct {
    ALCenum (*OpenPlayback)(ALCdevice*, const ALCchar*);
    void (*ClosePlayback)(ALCdevice*);
    ALCboolean (*ResetPlayback)(ALCdevice*);
    ALCboolean (*StartPlayback)(ALCdevice*);
    void (*StopPlayback)(ALCdevice*);

    ALCenum (*OpenCapture)(ALCdevice*, const ALCchar*);
    void (*CloseCapture)(ALCdevice*);
    void (*StartCapture)(ALCdevice*);
    void (*StopCapture)(ALCdevice*);
    ALCenum (*CaptureSamples)(ALCdevice*, void*, ALCuint);
    ALCuint (*AvailableSamples)(ALCdevice*);

    ALint64 (*GetLatency)(ALCdevice*);
} BackendFuncs;

ALCboolean alc_solaris_init(BackendFuncs *func_list);
void alc_solaris_deinit(void);
void alc_solaris_probe(enum DevProbe type);
ALCboolean alc_sndio_init(BackendFuncs *func_list);
void alc_sndio_deinit(void);
void alc_sndio_probe(enum DevProbe type);
ALCboolean alcWinMMInit(BackendFuncs *FuncList);
void alcWinMMDeinit(void);
void alcWinMMProbe(enum DevProbe type);
ALCboolean alc_pa_init(BackendFuncs *func_list);
void alc_pa_deinit(void);
void alc_pa_probe(enum DevProbe type);
ALCboolean alc_wave_init(BackendFuncs *func_list);
void alc_wave_deinit(void);
void alc_wave_probe(enum DevProbe type);
ALCboolean alc_ca_init(BackendFuncs *func_list);
void alc_ca_deinit(void);
void alc_ca_probe(enum DevProbe type);
ALCboolean alc_opensl_init(BackendFuncs *func_list);
void alc_opensl_deinit(void);
void alc_opensl_probe(enum DevProbe type);
ALCboolean alc_qsa_init(BackendFuncs *func_list);
void alc_qsa_deinit(void);
void alc_qsa_probe(enum DevProbe type);

struct ALCbackend;


enum DistanceModel {
    InverseDistanceClamped  = AL_INVERSE_DISTANCE_CLAMPED,
    LinearDistanceClamped   = AL_LINEAR_DISTANCE_CLAMPED,
    ExponentDistanceClamped = AL_EXPONENT_DISTANCE_CLAMPED,
    InverseDistance  = AL_INVERSE_DISTANCE,
    LinearDistance   = AL_LINEAR_DISTANCE,
    ExponentDistance = AL_EXPONENT_DISTANCE,
    DisableDistance  = AL_NONE,

    DefaultDistanceModel = InverseDistanceClamped
};

enum Resampler {
    PointResampler,
    LinearResampler,
    CubicResampler,

    ResamplerMax,
};

enum Channel {
    FrontLeft = 0,
    FrontRight,
    FrontCenter,
    LFE,
    BackLeft,
    BackRight,
    BackCenter,
    SideLeft,
    SideRight,

    MaxChannels,
};


/* Device formats */
enum DevFmtType {
    DevFmtByte   = ALC_BYTE_SOFT,
    DevFmtUByte  = ALC_UNSIGNED_BYTE_SOFT,
    DevFmtShort  = ALC_SHORT_SOFT,
    DevFmtUShort = ALC_UNSIGNED_SHORT_SOFT,
    DevFmtInt    = ALC_INT_SOFT,
    DevFmtUInt   = ALC_UNSIGNED_INT_SOFT,
    DevFmtFloat  = ALC_FLOAT_SOFT,

    DevFmtTypeDefault = DevFmtFloat
};
enum DevFmtChannels {
    DevFmtMono   = ALC_MONO_SOFT,
    DevFmtStereo = ALC_STEREO_SOFT,
    DevFmtQuad   = ALC_QUAD_SOFT,
    DevFmtX51    = ALC_5POINT1_SOFT,
    DevFmtX61    = ALC_6POINT1_SOFT,
    DevFmtX71    = ALC_7POINT1_SOFT,

    /* Similar to 5.1, except using the side channels instead of back */
    DevFmtX51Side = 0x80000000,

    DevFmtChannelsDefault = DevFmtStereo
};

ALuint BytesFromDevFmt(enum DevFmtType type) DECL_CONST;
ALuint ChannelsFromDevFmt(enum DevFmtChannels chans) DECL_CONST;
inline ALuint FrameSizeFromDevFmt(enum DevFmtChannels chans, enum DevFmtType type)
{
    return ChannelsFromDevFmt(chans) * BytesFromDevFmt(type);
}


extern const struct EffectList {
    const char *name;
    int type;
    const char *ename;
    ALenum val;
} EffectList[];


enum DeviceType {
    Playback,
    Capture,
    Loopback
};


/* Size for temporary storage of buffer data, in ALfloats. Larger values need
 * more memory, while smaller values may need more iterations. The value needs
 * to be a sensible size, however, as it constrains the max stepping value used
 * for mixing, as well as the maximum number of samples per mixing iteration.
 */
#define BUFFERSIZE (2048u)


struct ALCdevice_struct
{
    RefCount ref;

    ALCboolean Connected;
    enum DeviceType Type;

    ALuint       Frequency;
    ALuint       UpdateSize;
    ALuint       NumUpdates;
    enum DevFmtChannels FmtChans;
    enum DevFmtType     FmtType;

    al_string DeviceName;

    ATOMIC(ALCenum) LastError;

    // Maximum number of sources that can be created
    ALuint       MaxNoOfSources;
    // Maximum number of slots that can be created
    ALuint       AuxiliaryEffectSlotMax;

    ALCuint      NumMonoSources;
    ALCuint      NumStereoSources;
    ALuint       NumAuxSends;

    // Map of Buffers for this device
    UIntMap BufferMap;

    // Map of Effects for this device
    UIntMap EffectMap;

    // Map of Filters for this device
    UIntMap FilterMap;

    // Map of Soundfonts for this device
    UIntMap SfontMap;

    // Map of Presets for this device
    UIntMap PresetMap;

    // Map of Fontsounds for this device
    UIntMap FontsoundMap;

    /* Default soundfont (accessible as ID 0) */
    struct ALsoundfont *DefaultSfont;

    /* MIDI synth engine */
    struct MidiSynth *Synth;

    /* HRTF filter tables */
    const struct Hrtf *Hrtf;

    // Stereo-to-binaural filter
    struct bs2b *Bs2b;
    ALCint       Bs2bLevel;

    // Device flags
    ALuint       Flags;

    ALuint ChannelOffsets[MaxChannels];

    enum Channel Speaker2Chan[MaxChannels];
    ALfloat SpeakerAngle[MaxChannels];
    ALuint  NumChan;

    ALuint64 ClockBase;
    ALuint SamplesDone;

    /* Temp storage used for each source when mixing. */
    alignas(16) ALfloat SourceData[BUFFERSIZE];
    alignas(16) ALfloat ResampledData[BUFFERSIZE];
    alignas(16) ALfloat FilteredData[BUFFERSIZE];

    // Dry path buffer mix
    alignas(16) ALfloat DryBuffer[MaxChannels][BUFFERSIZE];

    /* Running count of the mixer invocations, in 31.1 fixed point. This
     * actually increments *twice* when mixing, first at the start and then at
     * the end, so the bottom bit indicates if the device is currently mixing
     * and the upper bits indicates how many mixes have been done.
     */
    RefCount MixCount;

    /* Default effect slot */
    struct ALeffectslot *DefaultSlot;

    // Contexts created on this device
    ATOMIC(ALCcontext*) ContextList;

    struct ALCbackend *Backend;

    void *ExtraData; // For the backend's use

    ALCdevice *volatile next;

    /* Memory space used by the default slot (Playback devices only) */
    alignas(16) ALCbyte _slot_mem[];
};

// Frequency was requested by the app or config file
#define DEVICE_FREQUENCY_REQUEST                 (1<<1)
// Channel configuration was requested by the config file
#define DEVICE_CHANNELS_REQUEST                  (1<<2)
// Sample type was requested by the config file
#define DEVICE_SAMPLE_TYPE_REQUEST               (1<<3)
// HRTF was requested by the app
#define DEVICE_HRTF_REQUEST                      (1<<4)

// Stereo sources cover 120-degree angles around +/-90
#define DEVICE_WIDE_STEREO                       (1<<16)

// Specifies if the DSP is paused at user request
#define DEVICE_PAUSED                            (1<<30)

// Specifies if the device is currently running
#define DEVICE_RUNNING                           (1<<31)

/* Invalid channel offset */
#define INVALID_OFFSET                           (~0u)


/* Nanosecond resolution for the device clock time. */
#define DEVICE_CLOCK_RES  U64(1000000000)


/* Must be less than 15 characters (16 including terminating null) for
 * compatibility with pthread_setname_np limitations. */
#define MIXER_THREAD_NAME "alsoft-mixer"


struct ALCcontext_struct
{
    RefCount ref;

    struct ALlistener *Listener;

    UIntMap SourceMap;
    UIntMap EffectSlotMap;

    ATOMIC(ALenum) LastError;

    ATOMIC(ALenum) UpdateSources;

    volatile enum DistanceModel DistanceModel;
    volatile ALboolean SourceDistanceModel;

    volatile ALfloat DopplerFactor;
    volatile ALfloat DopplerVelocity;
    volatile ALfloat SpeedOfSound;
    volatile ALenum  DeferUpdates;

    struct ALactivesource **ActiveSources;
    ALsizei ActiveSourceCount;
    ALsizei MaxActiveSources;

    VECTOR(struct ALeffectslot*) ActiveAuxSlots;

    ALCdevice  *Device;
    const ALCchar *ExtensionList;

    ALCcontext *volatile next;

    /* Memory space used by the listener */
    alignas(16) ALCbyte _listener_mem[];
};

ALCcontext *GetContextRef(void);

void ALCcontext_IncRef(ALCcontext *context);
void ALCcontext_DecRef(ALCcontext *context);

void AppendAllDevicesList(const ALCchar *name);
void AppendCaptureDeviceList(const ALCchar *name);

ALint64 ALCdevice_GetLatencyDefault(ALCdevice *device);

void ALCdevice_Lock(ALCdevice *device);
void ALCdevice_Unlock(ALCdevice *device);
ALint64 ALCdevice_GetLatency(ALCdevice *device);

inline void LockContext(ALCcontext *context)
{ ALCdevice_Lock(context->Device); }

inline void UnlockContext(ALCcontext *context)
{ ALCdevice_Unlock(context->Device); }


void *al_malloc(size_t alignment, size_t size);
void *al_calloc(size_t alignment, size_t size);
void al_free(void *ptr);


typedef struct {
#ifdef HAVE_FENV_H
    DERIVE_FROM_TYPE(fenv_t);
#else
    int state;
#endif
#ifdef HAVE_SSE
    int sse_state;
#endif
} FPUCtl;
void SetMixerFPUMode(FPUCtl *ctl);
void RestoreFPUMode(const FPUCtl *ctl);


typedef struct RingBuffer RingBuffer;
RingBuffer *CreateRingBuffer(ALsizei frame_size, ALsizei length);
void DestroyRingBuffer(RingBuffer *ring);
ALsizei RingBufferSize(RingBuffer *ring);
void WriteRingBuffer(RingBuffer *ring, const ALubyte *data, ALsizei len);
void ReadRingBuffer(RingBuffer *ring, ALubyte *data, ALsizei len);

void ReadALConfig(void);
void FreeALConfig(void);
int ConfigValueExists(const char *blockName, const char *keyName);
const char *GetConfigValue(const char *blockName, const char *keyName, const char *def);
int GetConfigValueBool(const char *blockName, const char *keyName, int def);
int ConfigValueStr(const char *blockName, const char *keyName, const char **ret);
int ConfigValueInt(const char *blockName, const char *keyName, int *ret);
int ConfigValueUInt(const char *blockName, const char *keyName, unsigned int *ret);
int ConfigValueFloat(const char *blockName, const char *keyName, float *ret);

void SetRTPriority(void);

void SetDefaultChannelOrder(ALCdevice *device);
void SetDefaultWFXChannelOrder(ALCdevice *device);

const ALCchar *DevFmtTypeString(enum DevFmtType type) DECL_CONST;
const ALCchar *DevFmtChannelsString(enum DevFmtChannels chans) DECL_CONST;


extern FILE *LogFile;

#if defined(__GNUC__) && !defined(IN_IDE_PARSER)
#define AL_PRINT(T, MSG, ...) fprintf(LogFile, "AL lib: %s %s: "MSG, T, __FUNCTION__ , ## __VA_ARGS__)
#else
void al_print(const char *type, const char *func, const char *fmt, ...) DECL_FORMAT(printf, 3,4);
#define AL_PRINT(T, ...) al_print((T), __FUNCTION__, __VA_ARGS__)
#endif

enum LogLevel {
    NoLog,
    LogError,
    LogWarning,
    LogTrace,
    LogRef
};
extern enum LogLevel LogLevel;

#define TRACEREF(...) do {                                                    \
    if(LogLevel >= LogRef)                                                    \
        AL_PRINT("(--)", __VA_ARGS__);                                        \
} while(0)

#define TRACE(...) do {                                                       \
    if(LogLevel >= LogTrace)                                                  \
        AL_PRINT("(II)", __VA_ARGS__);                                        \
} while(0)

#define WARN(...) do {                                                        \
    if(LogLevel >= LogWarning)                                                \
        AL_PRINT("(WW)", __VA_ARGS__);                                        \
} while(0)

#define ERR(...) do {                                                         \
    if(LogLevel >= LogError)                                                  \
        AL_PRINT("(EE)", __VA_ARGS__);                                        \
} while(0)


extern ALint RTPrioLevel;


extern ALuint CPUCapFlags;
enum {
    CPU_CAP_SSE    = 1<<0,
    CPU_CAP_SSE2   = 1<<1,
    CPU_CAP_SSE4_1 = 1<<2,
    CPU_CAP_NEON   = 1<<3,
};

void FillCPUCaps(ALuint capfilter);

FILE *OpenDataFile(const char *fname, const char *subdir);

/* Small hack to use a pointer-to-array type as a normal argument type.
 * Shouldn't be used directly. */
typedef ALfloat ALfloatBUFFERSIZE[BUFFERSIZE];


#ifdef __cplusplus
}
#endif

#endif
