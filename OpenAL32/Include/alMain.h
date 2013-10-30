#ifndef AL_MAIN_H
#define AL_MAIN_H

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>

#ifdef HAVE_FENV_H
#include <fenv.h>
#endif

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#include "atomic.h"
#include "uintmap.h"

#ifndef ALC_SOFT_HRTF
#define ALC_SOFT_HRTF 1
#define ALC_HRTF_SOFT                            0x1992
#endif


#ifdef IN_IDE_PARSER
/* KDevelop's parser doesn't recognize the C99-standard restrict keyword, but
 * recent versions (at least 4.5.1) do recognize GCC's __restrict. */
#define restrict __restrict
/* KDevelop won't see the ALIGN macro from config.h when viewing files that
 * don't include it directly (e.g. headers). */
#ifndef ALIGN
#define ALIGN(x)
#endif
#endif


#if defined(HAVE_STDINT_H)
#include <stdint.h>
typedef int64_t ALint64;
typedef uint64_t ALuint64;
#elif defined(HAVE___INT64)
typedef __int64 ALint64;
typedef unsigned __int64 ALuint64;
#elif (SIZEOF_LONG == 8)
typedef long ALint64;
typedef unsigned long ALuint64;
#elif (SIZEOF_LONG_LONG == 8)
typedef long long ALint64;
typedef unsigned long long ALuint64;
#endif

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

#ifdef HAVE_GCC_FORMAT
#define PRINTF_STYLE(x, y) __attribute__((format(printf, (x), (y))))
#else
#define PRINTF_STYLE(x, y)
#endif


static const union {
    ALuint u;
    ALubyte b[sizeof(ALuint)];
} EndianTest = { 1 };
#define IS_LITTLE_ENDIAN (EndianTest.b[0] == 1)

#define COUNTOF(x) (sizeof((x))/sizeof((x)[0]))


#define DERIVE_FROM_TYPE(t)          t t##_parent
#define STATIC_CAST(to, obj)         (&(obj)->to##_parent)
#define STATIC_UPCAST(to, from, obj) ((to*)((char*)(obj) - offsetof(to, from##_parent)))


#define DECLARE_FORWARD(T1, T2, rettype, func)                                \
rettype T1##_##func(T1 *obj)                                                  \
{ return T2##_##func(STATIC_CAST(T2, obj)); }

#define DECLARE_FORWARD2(T1, T2, rettype, func, argtype1, argtype2)           \
rettype T1##_##func(T1 *obj, argtype1 a, argtype2 b)                          \
{ return T2##_##func(STATIC_CAST(T2, obj), a, b); }


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


/* Helper to extract an argument list for VCALL. Not used directly. */
#define EXTRACT_VCALL_ARGS(...)  __VA_ARGS__))

/* Call a "virtual" method on an object, with arguments. */
#define VCALL(obj, func)  ((obj)->vtbl->func((obj), EXTRACT_VCALL_ARGS
/* Call a "virtual" method on an object, with no arguments. */
#define VCALL0(obj, func) ((obj)->vtbl->func((obj) EXTRACT_VCALL_ARGS

#define DELETE_OBJ(obj) do {                                                  \
    if((obj) != NULL)                                                         \
    {                                                                         \
        VCALL0((obj),Destruct)();                                             \
        VCALL0((obj),Delete)();                                               \
    }                                                                         \
} while(0)


#ifdef __cplusplus
extern "C" {
#endif

struct Hrtf;


#define DEFAULT_OUTPUT_RATE  (44100)
#define MIN_OUTPUT_RATE      (8000)


// Find the next power-of-2 for non-power-of-2 numbers.
static inline ALuint NextPowerOf2(ALuint value)
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
static inline ALint fastf2i(ALfloat f)
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
static inline ALuint fastf2u(ALfloat f)
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

    void (*Lock)(ALCdevice*);
    void (*Unlock)(ALCdevice*);

    ALint64 (*GetLatency)(ALCdevice*);
} BackendFuncs;

ALCboolean alc_oss_init(BackendFuncs *func_list);
void alc_oss_deinit(void);
void alc_oss_probe(enum DevProbe type);
ALCboolean alc_solaris_init(BackendFuncs *func_list);
void alc_solaris_deinit(void);
void alc_solaris_probe(enum DevProbe type);
ALCboolean alc_sndio_init(BackendFuncs *func_list);
void alc_sndio_deinit(void);
void alc_sndio_probe(enum DevProbe type);
ALCboolean alcMMDevApiInit(BackendFuncs *func_list);
void alcMMDevApiDeinit(void);
void alcMMDevApiProbe(enum DevProbe type);
ALCboolean alcDSoundInit(BackendFuncs *func_list);
void alcDSoundDeinit(void);
void alcDSoundProbe(enum DevProbe type);
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

ALuint BytesFromDevFmt(enum DevFmtType type);
ALuint ChannelsFromDevFmt(enum DevFmtChannels chans);
static inline ALuint FrameSizeFromDevFmt(enum DevFmtChannels chans,
                                           enum DevFmtType type)
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
    volatile RefCount ref;

    ALCboolean Connected;
    enum DeviceType Type;

    ALuint       Frequency;
    ALuint       UpdateSize;
    ALuint       NumUpdates;
    enum DevFmtChannels FmtChans;
    enum DevFmtType     FmtType;

    ALCchar      *DeviceName;

    volatile ALCenum LastError;

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

    /* Temp storage used for mixing. +1 for the predictive sample. */
    ALIGN(16) ALfloat SampleData1[BUFFERSIZE+1];
    ALIGN(16) ALfloat SampleData2[BUFFERSIZE+1];

    // Dry path buffer mix
    ALIGN(16) ALfloat DryBuffer[MaxChannels][BUFFERSIZE];

    ALIGN(16) ALfloat ClickRemoval[MaxChannels];
    ALIGN(16) ALfloat PendingClicks[MaxChannels];

    /* Default effect slot */
    struct ALeffectslot *DefaultSlot;

    // Contexts created on this device
    ALCcontext *volatile ContextList;

    struct ALCbackend *Backend;

    BackendFuncs *Funcs;
    void         *ExtraData; // For the backend's use

    ALCdevice *volatile next;
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

// Specifies if the device is currently running
#define DEVICE_RUNNING                           (1<<31)

/* Invalid channel offset */
#define INVALID_OFFSET                           (~0u)


/* Must be less than 15 characters (16 including terminating null) for
 * compatibility with pthread_setname_np limitations. */
#define MIXER_THREAD_NAME "alsoft-mixer"


ALint64 alcGetLatency(ALCdevice *device);


static inline struct ALbuffer *LookupBuffer(ALCdevice *device, ALuint id)
{ return (struct ALbuffer*)LookupUIntMapKey(&device->BufferMap, id); }
static inline struct ALeffect *LookupEffect(ALCdevice *device, ALuint id)
{ return (struct ALeffect*)LookupUIntMapKey(&device->EffectMap, id); }
static inline struct ALfilter *LookupFilter(ALCdevice *device, ALuint id)
{ return (struct ALfilter*)LookupUIntMapKey(&device->FilterMap, id); }

static inline struct ALbuffer *RemoveBuffer(ALCdevice *device, ALuint id)
{ return (struct ALbuffer*)RemoveUIntMapKey(&device->BufferMap, id); }
static inline struct ALeffect *RemoveEffect(ALCdevice *device, ALuint id)
{ return (struct ALeffect*)RemoveUIntMapKey(&device->EffectMap, id); }
static inline struct ALfilter *RemoveFilter(ALCdevice *device, ALuint id)
{ return (struct ALfilter*)RemoveUIntMapKey(&device->FilterMap, id); }


struct ALCcontext_struct
{
    volatile RefCount ref;

    struct ALlistener *Listener;

    UIntMap SourceMap;
    UIntMap EffectSlotMap;

    volatile ALenum LastError;

    volatile ALenum UpdateSources;

    volatile enum DistanceModel DistanceModel;
    volatile ALboolean SourceDistanceModel;

    volatile ALfloat DopplerFactor;
    volatile ALfloat DopplerVelocity;
    volatile ALfloat SpeedOfSound;
    volatile ALenum  DeferUpdates;

    struct ALsource **ActiveSources;
    ALsizei           ActiveSourceCount;
    ALsizei           MaxActiveSources;

    struct ALeffectslot **ActiveEffectSlots;
    ALsizei               ActiveEffectSlotCount;
    ALsizei               MaxActiveEffectSlots;

    ALCdevice  *Device;
    const ALCchar *ExtensionList;

    ALCcontext *volatile next;
};

static inline struct ALsource *LookupSource(ALCcontext *context, ALuint id)
{ return (struct ALsource*)LookupUIntMapKey(&context->SourceMap, id); }
static inline struct ALeffectslot *LookupEffectSlot(ALCcontext *context, ALuint id)
{ return (struct ALeffectslot*)LookupUIntMapKey(&context->EffectSlotMap, id); }

static inline struct ALsource *RemoveSource(ALCcontext *context, ALuint id)
{ return (struct ALsource*)RemoveUIntMapKey(&context->SourceMap, id); }
static inline struct ALeffectslot *RemoveEffectSlot(ALCcontext *context, ALuint id)
{ return (struct ALeffectslot*)RemoveUIntMapKey(&context->EffectSlotMap, id); }


ALCcontext *GetContextRef(void);

void ALCcontext_IncRef(ALCcontext *context);
void ALCcontext_DecRef(ALCcontext *context);

void AppendAllDevicesList(const ALCchar *name);
void AppendCaptureDeviceList(const ALCchar *name);

void ALCdevice_LockDefault(ALCdevice *device);
void ALCdevice_UnlockDefault(ALCdevice *device);
ALint64 ALCdevice_GetLatencyDefault(ALCdevice *device);

void ALCdevice_Lock(ALCdevice *device);
void ALCdevice_Unlock(ALCdevice *device);
void LockContext(ALCcontext *context);
void UnlockContext(ALCcontext *context);


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

const ALCchar *DevFmtTypeString(enum DevFmtType type);
const ALCchar *DevFmtChannelsString(enum DevFmtChannels chans);

#define HRIR_BITS        (7)
#define HRIR_LENGTH      (1<<HRIR_BITS)
#define HRIR_MASK        (HRIR_LENGTH-1)
#define HRTFDELAY_BITS    (20)
#define HRTFDELAY_FRACONE (1<<HRTFDELAY_BITS)
#define HRTFDELAY_MASK    (HRTFDELAY_FRACONE-1)
const struct Hrtf *GetHrtf(ALCdevice *device);
void FindHrtfFormat(const ALCdevice *device, enum DevFmtChannels *chans, ALCuint *srate);
void FreeHrtfs(void);
ALuint GetHrtfIrSize(const struct Hrtf *Hrtf);
ALfloat CalcHrtfDelta(ALfloat oldGain, ALfloat newGain, const ALfloat olddir[3], const ALfloat newdir[3]);
void GetLerpedHrtfCoeffs(const struct Hrtf *Hrtf, ALfloat elevation, ALfloat azimuth, ALfloat gain, ALfloat (*coeffs)[2], ALuint *delays);
ALuint GetMovingHrtfCoeffs(const struct Hrtf *Hrtf, ALfloat elevation, ALfloat azimuth, ALfloat gain, ALfloat delta, ALint counter, ALfloat (*coeffs)[2], ALuint *delays, ALfloat (*coeffStep)[2], ALint *delayStep);


extern FILE *LogFile;

#ifdef __GNUC__
#define AL_PRINT(T, MSG, ...) fprintf(LogFile, "AL lib: %s %s: "MSG, T, __FUNCTION__ , ## __VA_ARGS__)
#else
void al_print(const char *type, const char *func, const char *fmt, ...) PRINTF_STYLE(3,4);
#define AL_PRINT(T, MSG, ...) al_print((T), __FUNCTION__, MSG, __VA_ARGS__)
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
    CPU_CAP_NEON   = 1<<2,
};

void FillCPUCaps(ALuint capfilter);


#define SET_ERROR_AND_RETURN(ctx, err) do {                                    \
    alSetError((ctx), (err));                                                  \
    return;                                                                    \
} while(0)

#define SET_ERROR_AND_RETURN_VALUE(ctx, err, val) do {                         \
    alSetError((ctx), (err));                                                  \
    return (val);                                                              \
} while(0)

#define SET_ERROR_AND_GOTO(ctx, err, lbl) do {                                 \
    alSetError((ctx), (err));                                                  \
    goto lbl;                                                                  \
} while(0)


#ifdef __cplusplus
}
#endif

#endif
