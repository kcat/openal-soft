#ifndef AL_MAIN_H
#define AL_MAIN_H

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef HAVE_FENV_H
#include <fenv.h>
#endif

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#ifndef AL_EXT_sample_buffer_object
#define AL_EXT_sample_buffer_object 1
typedef ptrdiff_t ALintptrEXT;
typedef ptrdiff_t ALsizeiptrEXT;
#define AL_SAMPLE_SOURCE_EXT                     0x1040
#define AL_SAMPLE_SINK_EXT                       0x1041
#define AL_READ_ONLY_EXT                         0x1042
#define AL_WRITE_ONLY_EXT                        0x1043
#define AL_READ_WRITE_EXT                        0x1044
#define AL_STREAM_WRITE_EXT                      0x1045
#define AL_STREAM_READ_EXT                       0x1046
#define AL_STREAM_COPY_EXT                       0x1047
#define AL_STATIC_WRITE_EXT                      0x1048
#define AL_STATIC_READ_EXT                       0x1049
#define AL_STATIC_COPY_EXT                       0x104A
#define AL_DYNAMIC_WRITE_EXT                     0x104B
#define AL_DYNAMIC_READ_EXT                      0x104C
#define AL_DYNAMIC_COPY_EXT                      0x104D
typedef ALvoid (AL_APIENTRY*PFNALGENDATABUFFERSEXTPROC)(ALsizei n,ALuint *puiBuffers);
typedef ALvoid (AL_APIENTRY*PFNALDELETEDATABUFFERSEXTPROC)(ALsizei n, const ALuint *puiBuffers);
typedef ALboolean (AL_APIENTRY*PFNALISDATABUFFEREXTPROC)(ALuint uiBuffer);
typedef ALvoid (AL_APIENTRY*PFNALDATABUFFERDATAEXTPROC)(ALuint buffer,const ALvoid *data,ALsizeiptrEXT size,ALenum usage);
typedef ALvoid (AL_APIENTRY*PFNALDATABUFFERSUBDATAEXTPROC)(ALuint buffer, ALintptrEXT start, ALsizeiptrEXT length, const ALvoid *);
typedef ALvoid (AL_APIENTRY*PFNALGETDATABUFFERSUBDATAEXTPROC)(ALuint buffer, ALintptrEXT start, ALsizeiptrEXT length, ALvoid *);
typedef ALvoid (AL_APIENTRY*PFNALDATABUFFERFEXTPROC)(ALuint buffer, ALenum eParam, ALfloat flValue);
typedef ALvoid (AL_APIENTRY*PFNALDATABUFFERFVEXTPROC)(ALuint buffer, ALenum eParam, const ALfloat* flValues);
typedef ALvoid (AL_APIENTRY*PFNALDATABUFFERIEXTPROC)(ALuint buffer, ALenum eParam, ALint lValue);
typedef ALvoid (AL_APIENTRY*PFNALDATABUFFERIVEXTPROC)(ALuint buffer, ALenum eParam, const ALint* plValues);
typedef ALvoid (AL_APIENTRY*PFNALGETDATABUFFERFEXTPROC)(ALuint buffer, ALenum eParam, ALfloat *pflValue);
typedef ALvoid (AL_APIENTRY*PFNALGETDATABUFFERFVEXTPROC)(ALuint buffer, ALenum eParam, ALfloat* pflValues);
typedef ALvoid (AL_APIENTRY*PFNALGETDATABUFFERIEXTPROC)(ALuint buffer, ALenum eParam, ALint *plValue);
typedef ALvoid (AL_APIENTRY*PFNALGETDATABUFFERIVEXTPROC)(ALuint buffer, ALenum eParam, ALint* plValues);
typedef ALvoid (AL_APIENTRY*PFNALSELECTDATABUFFEREXTPROC)(ALenum target, ALuint uiBuffer);
typedef ALvoid* (AL_APIENTRY*PFNALMAPDATABUFFEREXTPROC)(ALuint uiBuffer, ALintptrEXT start, ALsizeiptrEXT length, ALenum access);
typedef ALvoid (AL_APIENTRY*PFNALUNMAPDATABUFFEREXTPROC)(ALuint uiBuffer);
#ifdef AL_ALEXT_PROTOTYPES
AL_API ALvoid AL_APIENTRY alGenDatabuffersEXT(ALsizei n,ALuint *puiBuffers);
AL_API ALvoid AL_APIENTRY alDeleteDatabuffersEXT(ALsizei n, const ALuint *puiBuffers);
AL_API ALboolean AL_APIENTRY alIsDatabufferEXT(ALuint uiBuffer);
AL_API ALvoid AL_APIENTRY alDatabufferDataEXT(ALuint buffer,const ALvoid *data,ALsizeiptrEXT size,ALenum usage);
AL_API ALvoid AL_APIENTRY alDatabufferSubDataEXT(ALuint buffer, ALintptrEXT start, ALsizeiptrEXT length, const ALvoid *data);
AL_API ALvoid AL_APIENTRY alGetDatabufferSubDataEXT(ALuint buffer, ALintptrEXT start, ALsizeiptrEXT length, ALvoid *data);
AL_API ALvoid AL_APIENTRY alDatabufferfEXT(ALuint buffer, ALenum eParam, ALfloat flValue);
AL_API ALvoid AL_APIENTRY alDatabufferfvEXT(ALuint buffer, ALenum eParam, const ALfloat* flValues);
AL_API ALvoid AL_APIENTRY alDatabufferiEXT(ALuint buffer, ALenum eParam, ALint lValue);
AL_API ALvoid AL_APIENTRY alDatabufferivEXT(ALuint buffer, ALenum eParam, const ALint* plValues);
AL_API ALvoid AL_APIENTRY alGetDatabufferfEXT(ALuint buffer, ALenum eParam, ALfloat *pflValue);
AL_API ALvoid AL_APIENTRY alGetDatabufferfvEXT(ALuint buffer, ALenum eParam, ALfloat* pflValues);
AL_API ALvoid AL_APIENTRY alGetDatabufferiEXT(ALuint buffer, ALenum eParam, ALint *plValue);
AL_API ALvoid AL_APIENTRY alGetDatabufferivEXT(ALuint buffer, ALenum eParam, ALint* plValues);
AL_API ALvoid AL_APIENTRY alSelectDatabufferEXT(ALenum target, ALuint uiBuffer);
AL_API ALvoid* AL_APIENTRY alMapDatabufferEXT(ALuint uiBuffer, ALintptrEXT start, ALsizeiptrEXT length, ALenum access);
AL_API ALvoid AL_APIENTRY alUnmapDatabufferEXT(ALuint uiBuffer);
#endif
#endif

#ifndef AL_EXT_loop_points
#define AL_EXT_loop_points 1
#define AL_LOOP_POINTS                           0x2015
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

#ifdef HAVE_GCC_FORMAT
#define PRINTF_STYLE(x, y) __attribute__((format(printf, (x), (y))))
#else
#define PRINTF_STYLE(x, y)
#endif

#ifdef _WIN32

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif
#include <windows.h>

typedef DWORD tls_type;
#define tls_create(x) (*(x) = TlsAlloc())
#define tls_delete(x) TlsFree((x))
#define tls_get(x) TlsGetValue((x))
#define tls_set(x, a) TlsSetValue((x), (a))

#else

#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif
#include <sys/time.h>
#include <time.h>
#include <errno.h>

#define IsBadWritePtr(a,b) ((a) == NULL && (b) != 0)

typedef pthread_key_t tls_type;
#define tls_create(x) pthread_key_create((x), NULL)
#define tls_delete(x) pthread_key_delete((x))
#define tls_get(x) pthread_getspecific((x))
#define tls_set(x, a) pthread_setspecific((x), (a))

typedef pthread_mutex_t CRITICAL_SECTION;
static __inline void EnterCriticalSection(CRITICAL_SECTION *cs)
{
    int ret;
    ret = pthread_mutex_lock(cs);
    assert(ret == 0);
}
static __inline void LeaveCriticalSection(CRITICAL_SECTION *cs)
{
    int ret;
    ret = pthread_mutex_unlock(cs);
    assert(ret == 0);
}
static __inline void InitializeCriticalSection(CRITICAL_SECTION *cs)
{
    pthread_mutexattr_t attrib;
    int ret;

    ret = pthread_mutexattr_init(&attrib);
    assert(ret == 0);

    ret = pthread_mutexattr_settype(&attrib, PTHREAD_MUTEX_RECURSIVE);
#ifdef HAVE_PTHREAD_NP_H
    if(ret != 0)
        ret = pthread_mutexattr_setkind_np(&attrib, PTHREAD_MUTEX_RECURSIVE);
#endif
    assert(ret == 0);
    ret = pthread_mutex_init(cs, &attrib);
    assert(ret == 0);

    pthread_mutexattr_destroy(&attrib);
}

static __inline void DeleteCriticalSection(CRITICAL_SECTION *cs)
{
    int ret;
    ret = pthread_mutex_destroy(cs);
    assert(ret == 0);
}

/* NOTE: This wrapper isn't quite accurate as it returns an ALuint, as opposed
 * to the expected DWORD. Both are defined as unsigned 32-bit types, however.
 * Additionally, Win32 is supposed to measure the time since Windows started,
 * as opposed to the actual time. */
static __inline ALuint timeGetTime(void)
{
#if _POSIX_TIMERS > 0
    static int hasmono = 0;
    struct timespec ts;
    int ret = -1;

#if defined(_POSIX_MONOTONIC_CLOCK) && (_POSIX_MONOTONIC_CLOCK >= 0)
#if _POSIX_MONOTONIC_CLOCK == 0
    if(hasmono > 0 || (hasmono == 0 &&
                       (hasmono=sysconf(_SC_MONOTONIC_CLOCK)) > 0))
#endif
        ret = clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    if(ret != 0)
        ret = clock_gettime(CLOCK_REALTIME, &ts);
    assert(ret == 0);

    return ts.tv_nsec/1000000 + ts.tv_sec*1000;
#else
    struct timeval tv;
    int ret;

    ret = gettimeofday(&tv, NULL);
    assert(ret == 0);

    return tv.tv_usec/1000 + tv.tv_sec*1000;
#endif
}

static __inline void Sleep(ALuint t)
{
    struct timespec tv, rem;
    tv.tv_nsec = (t*1000000)%1000000000;
    tv.tv_sec = t/1000;

    while(nanosleep(&tv, &rem) == -1 && errno == EINTR)
        tv = rem;
}
#define min(x,y) (((x)<(y))?(x):(y))
#define max(x,y) (((x)>(y))?(x):(y))
#endif

#include "alListener.h"
#include "alu.h"

#ifdef __cplusplus
extern "C" {
#endif


#define SWMIXER_OUTPUT_RATE        44100

#define SPEEDOFSOUNDMETRESPERSEC   (343.3f)
#define AIRABSORBGAINDBHF          (-0.05f)

#define LOWPASSFREQCUTOFF          (5000)

#define DEFAULT_HEAD_DAMPEN        (0.25f)


// Find the next power-of-2 for non-power-of-2 numbers.
static __inline ALuint NextPowerOf2(ALuint value)
{
    ALuint powerOf2 = 1;

    if(value)
    {
        value--;
        while(value)
        {
            value >>= 1;
            powerOf2 <<= 1;
        }
    }
    return powerOf2;
}


typedef struct {
    ALCboolean (*OpenPlayback)(ALCdevice*, const ALCchar*);
    void (*ClosePlayback)(ALCdevice*);
    ALCboolean (*ResetPlayback)(ALCdevice*);
    void (*StopPlayback)(ALCdevice*);

    ALCboolean (*OpenCapture)(ALCdevice*, const ALCchar*);
    void (*CloseCapture)(ALCdevice*);
    void (*StartCapture)(ALCdevice*);
    void (*StopCapture)(ALCdevice*);
    void (*CaptureSamples)(ALCdevice*, void*, ALCuint);
    ALCuint (*AvailableSamples)(ALCdevice*);
} BackendFuncs;

enum {
    DEVICE_PROBE,
    ALL_DEVICE_PROBE,
    CAPTURE_DEVICE_PROBE
};

void alc_alsa_init(BackendFuncs *func_list);
void alc_alsa_deinit(void);
void alc_alsa_probe(int type);
void alc_oss_init(BackendFuncs *func_list);
void alc_oss_deinit(void);
void alc_oss_probe(int type);
void alc_solaris_init(BackendFuncs *func_list);
void alc_solaris_deinit(void);
void alc_solaris_probe(int type);
void alcDSoundInit(BackendFuncs *func_list);
void alcDSoundDeinit(void);
void alcDSoundProbe(int type);
void alcWinMMInit(BackendFuncs *FuncList);
void alcWinMMDeinit(void);
void alcWinMMProbe(int type);
void alc_pa_init(BackendFuncs *func_list);
void alc_pa_deinit(void);
void alc_pa_probe(int type);
void alc_wave_init(BackendFuncs *func_list);
void alc_wave_deinit(void);
void alc_wave_probe(int type);
void alc_pulse_init(BackendFuncs *func_list);
void alc_pulse_deinit(void);
void alc_pulse_probe(int type);
void alc_null_init(BackendFuncs *func_list);
void alc_null_deinit(void);
void alc_null_probe(int type);


typedef struct UIntMap {
    struct {
        ALuint key;
        ALvoid *value;
    } *array;
    ALsizei size;
    ALsizei maxsize;
} UIntMap;

void InitUIntMap(UIntMap *map);
void ResetUIntMap(UIntMap *map);
ALenum InsertUIntMapEntry(UIntMap *map, ALuint key, ALvoid *value);
void RemoveUIntMapKey(UIntMap *map, ALuint key);
ALvoid *LookupUIntMapKey(UIntMap *map, ALuint key);


struct ALCdevice_struct
{
    ALCboolean   Connected;
    ALboolean    IsCaptureDevice;

    ALuint       Frequency;
    ALuint       UpdateSize;
    ALuint       NumUpdates;
    ALenum       Format;

    ALCchar      *szDeviceName;

    ALCenum      LastError;

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

    // Map of Databuffers for this device
    UIntMap DatabufferMap;

    // Stereo-to-binaural filter
    struct bs2b *Bs2b;
    ALCint       Bs2bLevel;

    // Simulated dampening from head occlusion
    ALfloat      HeadDampen;

    // Duplicate stereo sources on the side/rear channels
    ALboolean    DuplicateStereo;

    // Dry path buffer mix
    ALfloat DryBuffer[BUFFERSIZE][MAXCHANNELS];

    ALuint DevChannels[MAXCHANNELS];

    ALfloat ChannelMatrix[MAXCHANNELS][MAXCHANNELS];

    Channel Speaker2Chan[MAXCHANNELS];
    ALfloat PanningLUT[MAXCHANNELS * LUT_NUM];
    ALuint  NumChan;

    ALfloat ClickRemoval[MAXCHANNELS];
    ALfloat PendingClicks[MAXCHANNELS];

    // Contexts created on this device
    ALCcontext  **Contexts;
    ALuint        NumContexts;

    BackendFuncs *Funcs;
    void         *ExtraData; // For the backend's use

    ALCdevice *next;
};

#define ALCdevice_OpenPlayback(a,b)      ((a)->Funcs->OpenPlayback((a), (b)))
#define ALCdevice_ClosePlayback(a)       ((a)->Funcs->ClosePlayback((a)))
#define ALCdevice_ResetPlayback(a)       ((a)->Funcs->ResetPlayback((a)))
#define ALCdevice_StopPlayback(a)        ((a)->Funcs->StopPlayback((a)))
#define ALCdevice_OpenCapture(a,b)       ((a)->Funcs->OpenCapture((a), (b)))
#define ALCdevice_CloseCapture(a)        ((a)->Funcs->CloseCapture((a)))
#define ALCdevice_StartCapture(a)        ((a)->Funcs->StartCapture((a)))
#define ALCdevice_StopCapture(a)         ((a)->Funcs->StopCapture((a)))
#define ALCdevice_CaptureSamples(a,b,c)  ((a)->Funcs->CaptureSamples((a), (b), (c)))
#define ALCdevice_AvailableSamples(a)    ((a)->Funcs->AvailableSamples((a)))

struct ALCcontext_struct
{
    ALlistener  Listener;

    UIntMap SourceMap;
    UIntMap EffectSlotMap;

    struct ALdatabuffer *SampleSource;
    struct ALdatabuffer *SampleSink;

    ALenum      LastError;

    ALboolean   Suspended;

    ALenum      DistanceModel;
    ALboolean   SourceDistanceModel;

    ALfloat     DopplerFactor;
    ALfloat     DopplerVelocity;
    ALfloat     flSpeedOfSound;

    struct ALsource **ActiveSources;
    ALsizei           ActiveSourceCount;
    ALsizei           MaxActiveSources;

    ALCdevice  *Device;
    const ALCchar *ExtensionList;

    ALCcontext *next;
};

void AppendDeviceList(const ALCchar *name);
void AppendAllDeviceList(const ALCchar *name);
void AppendCaptureDeviceList(const ALCchar *name);

ALCvoid alcSetError(ALCdevice *device, ALenum errorCode);

ALCvoid SuspendContext(ALCcontext *context);
ALCvoid ProcessContext(ALCcontext *context);

ALvoid *StartThread(ALuint (*func)(ALvoid*), ALvoid *ptr);
ALuint StopThread(ALvoid *thread);

ALCcontext *GetContextSuspended(void);

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
int GetConfigValueInt(const char *blockName, const char *keyName, int def);
float GetConfigValueFloat(const char *blockName, const char *keyName, float def);
int GetConfigValueBool(const char *blockName, const char *keyName, int def);

void SetRTPriority(void);

void SetDefaultChannelOrder(ALCdevice *device);
void SetDefaultWFXChannelOrder(ALCdevice *device);

void al_print(const char *fname, unsigned int line, const char *fmt, ...)
             PRINTF_STYLE(3,4);
#define AL_PRINT(...) al_print(__FILE__, __LINE__, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
