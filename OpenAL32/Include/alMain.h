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

#include "hrtf.h"

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


typedef ALint64SOFT ALint64;
typedef ALuint64SOFT ALuint64;

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

#define DECLARE_THUNK4(T1, T2, rettype, func, argtype1, argtype2, argtype3, argtype4) \
static rettype T1##_##T2##_##func(T2 *obj, argtype1 a, argtype2 b, argtype3 c, argtype4 d) \
{ return T1##_##func(STATIC_UPCAST(T1, T2, obj), a, b, c, d); }

#define DECLARE_DEFAULT_ALLOCATORS(T)                                         \
static void* T##_New(size_t size) { return al_malloc(16, size); }             \
static void T##_Delete(void *ptr) { al_free(ptr); }

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


#define EXTRACT_NEW_ARGS(...)  __VA_ARGS__);                                  \
    }                                                                         \
} while(0)

#define NEW_OBJ(_res, T) do {                                                 \
    _res = T##_New(sizeof(T));                                                \
    if(_res)                                                                  \
    {                                                                         \
        memset(_res, 0, sizeof(T));                                           \
        T##_Construct(_res, EXTRACT_NEW_ARGS


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
} BackendFuncs;

ALCboolean alc_sndio_init(BackendFuncs *func_list);
void alc_sndio_deinit(void);
void alc_sndio_probe(enum DevProbe type);
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

    BFormatW,
    BFormatX,
    BFormatY,
    BFormatZ,

    InvalidChannel
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

    /* Similar to 5.1, except using rear channels instead of sides */
    DevFmtX51Rear = 0x80000000,

    DevFmtBFormat3D,

    DevFmtChannelsDefault = DevFmtStereo
};
#define MAX_OUTPUT_CHANNELS  (8)

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


enum HrtfMode {
    DisabledHrtf,
    BasicHrtf,
    FullHrtf
};


/* The maximum number of Ambisonics coefficients. For a given order (o), the
 * size needed will be (o+1)**2, thus zero-order has 1, first-order has 4,
 * second-order has 9, and third-order has 16. */
#define MAX_AMBI_COEFFS 16

typedef ALfloat ChannelConfig[MAX_AMBI_COEFFS];


#define HRTF_HISTORY_BITS   (6)
#define HRTF_HISTORY_LENGTH (1<<HRTF_HISTORY_BITS)
#define HRTF_HISTORY_MASK   (HRTF_HISTORY_LENGTH-1)

typedef struct HrtfState {
    alignas(16) ALfloat History[HRTF_HISTORY_LENGTH];
    alignas(16) ALfloat Values[HRIR_LENGTH][2];
} HrtfState;

typedef struct HrtfParams {
    alignas(16) ALfloat Coeffs[HRIR_LENGTH][2];
    alignas(16) ALfloat CoeffStep[HRIR_LENGTH][2];
    ALuint Delay[2];
    ALint DelayStep[2];
} HrtfParams;


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
    ALboolean    IsHeadphones;

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

    /* HRTF filter tables */
    vector_HrtfEntry Hrtf_List;
    al_string Hrtf_Name;
    const struct Hrtf *Hrtf;
    ALCenum Hrtf_Status;
    enum HrtfMode Hrtf_Mode;
    HrtfState Hrtf_State[MAX_OUTPUT_CHANNELS];
    HrtfParams Hrtf_Params[MAX_OUTPUT_CHANNELS];
    ALuint Hrtf_Offset;

    // Stereo-to-binaural filter
    struct bs2b *Bs2b;

    // Device flags
    ALuint Flags;

    enum Channel ChannelName[MAX_OUTPUT_CHANNELS];
    ChannelConfig AmbiCoeffs[MAX_OUTPUT_CHANNELS];
    ALfloat AmbiScale; /* Scale for first-order XYZ inputs using AmbCoeffs. */
    ALuint NumChannels;

    ALuint64 ClockBase;
    ALuint SamplesDone;

    /* Temp storage used for each source when mixing. */
    alignas(16) ALfloat SourceData[BUFFERSIZE];
    alignas(16) ALfloat ResampledData[BUFFERSIZE];
    alignas(16) ALfloat FilteredData[BUFFERSIZE];

    /* Dry path buffer mix. */
    alignas(16) ALfloat (*DryBuffer)[BUFFERSIZE];

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

// Specifies if the DSP is paused at user request
#define DEVICE_PAUSED                            (1<<30)

// Specifies if the device is currently running
#define DEVICE_RUNNING                           (1<<31)


/* Nanosecond resolution for the device clock time. */
#define DEVICE_CLOCK_RES  U64(1000000000)


/* Must be less than 15 characters (16 including terminating null) for
 * compatibility with pthread_setname_np limitations. */
#define MIXER_THREAD_NAME "alsoft-mixer"

#define RECORD_THREAD_NAME "alsoft-record"


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

    struct ALvoice *Voices;
    ALsizei VoiceCount;
    ALsizei MaxVoices;

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

void ALCdevice_Lock(ALCdevice *device);
void ALCdevice_Unlock(ALCdevice *device);

void ALCcontext_DeferUpdates(ALCcontext *context);
void ALCcontext_ProcessUpdates(ALCcontext *context);

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

typedef struct ll_ringbuffer ll_ringbuffer_t;
typedef struct ll_ringbuffer_data {
    char *buf;
    size_t len;
} ll_ringbuffer_data_t;
ll_ringbuffer_t *ll_ringbuffer_create(size_t sz, size_t elem_sz);
void ll_ringbuffer_free(ll_ringbuffer_t *rb);
void ll_ringbuffer_get_read_vector(const ll_ringbuffer_t *rb, ll_ringbuffer_data_t *vec);
void ll_ringbuffer_get_write_vector(const ll_ringbuffer_t *rb, ll_ringbuffer_data_t *vec);
size_t ll_ringbuffer_read(ll_ringbuffer_t *rb, char *dest, size_t cnt);
size_t ll_ringbuffer_peek(ll_ringbuffer_t *rb, char *dest, size_t cnt);
void ll_ringbuffer_read_advance(ll_ringbuffer_t *rb, size_t cnt);
size_t ll_ringbuffer_read_space(const ll_ringbuffer_t *rb);
int ll_ringbuffer_mlock(ll_ringbuffer_t *rb);
void ll_ringbuffer_reset(ll_ringbuffer_t *rb);
size_t ll_ringbuffer_write(ll_ringbuffer_t *rb, const char *src, size_t cnt);
void ll_ringbuffer_write_advance(ll_ringbuffer_t *rb, size_t cnt);
size_t ll_ringbuffer_write_space(const ll_ringbuffer_t *rb);

void ReadALConfig(void);
void FreeALConfig(void);
int ConfigValueExists(const char *devName, const char *blockName, const char *keyName);
const char *GetConfigValue(const char *devName, const char *blockName, const char *keyName, const char *def);
int GetConfigValueBool(const char *devName, const char *blockName, const char *keyName, int def);
int ConfigValueStr(const char *devName, const char *blockName, const char *keyName, const char **ret);
int ConfigValueInt(const char *devName, const char *blockName, const char *keyName, int *ret);
int ConfigValueUInt(const char *devName, const char *blockName, const char *keyName, unsigned int *ret);
int ConfigValueFloat(const char *devName, const char *blockName, const char *keyName, float *ret);
int ConfigValueBool(const char *devName, const char *blockName, const char *keyName, int *ret);

void SetRTPriority(void);

void SetDefaultChannelOrder(ALCdevice *device);
void SetDefaultWFXChannelOrder(ALCdevice *device);

const ALCchar *DevFmtTypeString(enum DevFmtType type) DECL_CONST;
const ALCchar *DevFmtChannelsString(enum DevFmtChannels chans) DECL_CONST;

/**
 * GetChannelIdxByName
 *
 * Returns the device's channel index given a channel name (e.g. FrontCenter),
 * or -1 if it doesn't exist.
 */
inline ALint GetChannelIdxByName(const ALCdevice *device, enum Channel chan)
{
    ALint i = 0;
    for(i = 0;i < MAX_OUTPUT_CHANNELS;i++)
    {
        if(device->ChannelName[i] == chan)
            return i;
    }
    return -1;
}


extern FILE *LogFile;

#if defined(__GNUC__) && !defined(_WIN32) && !defined(IN_IDE_PARSER)
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
    CPU_CAP_SSE3   = 1<<2,
    CPU_CAP_SSE4_1 = 1<<3,
    CPU_CAP_NEON   = 1<<4,
};

void FillCPUCaps(ALuint capfilter);

FILE *OpenDataFile(const char *fname, const char *subdir);

vector_al_string SearchDataFiles(const char *match, const char *subdir);

/* Small hack to use a pointer-to-array type as a normal argument type.
 * Shouldn't be used directly. */
typedef ALfloat ALfloatBUFFERSIZE[BUFFERSIZE];


#ifdef __cplusplus
}
#endif

#endif
