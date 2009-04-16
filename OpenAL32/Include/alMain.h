#ifndef AL_MAIN_H
#define AL_MAIN_H

#include <string.h>
#include <stdio.h>

#include "alu.h"

#ifdef HAVE_FENV_H
#include <fenv.h>
#endif

#ifdef _WIN32

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif
#include <windows.h>

#else

#include <assert.h>
#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif
#include <sys/time.h>
#include <time.h>
#include <errno.h>

#define IsBadWritePtr(a,b) (0)

typedef pthread_mutex_t CRITICAL_SECTION;
static inline void EnterCriticalSection(CRITICAL_SECTION *cs)
{
    int ret;
    ret = pthread_mutex_lock(cs);
    assert(ret == 0);
}
static inline void LeaveCriticalSection(CRITICAL_SECTION *cs)
{
    int ret;
    ret = pthread_mutex_unlock(cs);
    assert(ret == 0);
}
static inline void InitializeCriticalSection(CRITICAL_SECTION *cs)
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

static inline void DeleteCriticalSection(CRITICAL_SECTION *cs)
{
    int ret;
    ret = pthread_mutex_destroy(cs);
    assert(ret == 0);
}

/* NOTE: This wrapper isn't quite accurate as it returns an ALuint, as opposed
 * to the expected DWORD. Both are defined as unsigned 32-bit types, however.
 * Additionally, Win32 is supposed to measure the time since Windows started,
 * as opposed to the actual time. */
static inline ALuint timeGetTime(void)
{
    struct timeval tv;
    int ret;

    ret = gettimeofday(&tv, NULL);
    assert(ret == 0);

    return tv.tv_usec/1000 + tv.tv_sec*1000;
}

static inline void Sleep(ALuint t)
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

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"
#include "alListener.h"

#ifdef __cplusplus
extern "C" {
#endif

extern char _alDebug[256];

#define AL_PRINT(...) do {                             \
    int _al_print_i;                                   \
    const char *_al_print_fn = strrchr(__FILE__, '/'); \
    if(!_al_print_fn) _al_print_fn  = __FILE__;        \
    else              _al_print_fn += 1;               \
    _al_print_i = snprintf(_alDebug, sizeof(_alDebug), "AL lib: %s:%d: ", _al_print_fn, __LINE__); \
    if(_al_print_i < (int)sizeof(_alDebug) && _al_print_i > 0)                     \
        snprintf(_alDebug+_al_print_i, sizeof(_alDebug)-_al_print_i, __VA_ARGS__); \
    _alDebug[sizeof(_alDebug)-1] = 0; \
    fprintf(stderr, "%s", _alDebug);  \
} while(0)


#define SWMIXER_OUTPUT_RATE        44100

#define SPEEDOFSOUNDMETRESPERSEC   (343.3f)
#define AIRABSORBGAINDBHF          (-0.05f)

#define LOWPASSFREQCUTOFF          (5000)

#define QUADRANT_NUM 128
#define LUT_NUM (4 * QUADRANT_NUM)


typedef struct {
    ALCboolean (*OpenPlayback)(ALCdevice*, const ALCchar*);
    void (*ClosePlayback)(ALCdevice*);

    ALCboolean (*OpenCapture)(ALCdevice*, const ALCchar*, ALCuint, ALCenum, ALCsizei);
    void (*CloseCapture)(ALCdevice*);
    void (*StartCapture)(ALCdevice*);
    void (*StopCapture)(ALCdevice*);
    void (*CaptureSamples)(ALCdevice*, void*, ALCuint);
    ALCuint (*AvailableSamples)(ALCdevice*);
} BackendFuncs;

void alc_alsa_init(BackendFuncs *func_list);
void alc_oss_init(BackendFuncs *func_list);
void alc_solaris_init(BackendFuncs *func_list);
void alcDSoundInit(BackendFuncs *func_list);
void alcWinMMInit(BackendFuncs *FuncList);
void alc_pa_init(BackendFuncs *func_list);
void alc_wave_init(BackendFuncs *func_list);
void alc_pulse_init(BackendFuncs *func_list);


struct ALCdevice_struct
{
    ALboolean    IsCaptureDevice;

    ALuint       Frequency;
    ALuint       UpdateSize;
    ALenum       Format;

    ALCchar      *szDeviceName;

    // Maximum number of sources that can be created
    ALuint       MaxNoOfSources;

    // Context created on this device
    ALCcontext   *Context;

    BackendFuncs *Funcs;
    void         *ExtraData; // For the backend's use

    ALCdevice *next;
};

#define ALCdevice_OpenPlayback(a,b)      ((a)->Funcs->OpenPlayback((a), (b)))
#define ALCdevice_ClosePlayback(a)       ((a)->Funcs->ClosePlayback((a)))
#define ALCdevice_OpenCapture(a,b,c,d,e) ((a)->Funcs->OpenCapture((a), (b), (c), (d), (e)))
#define ALCdevice_CloseCapture(a)        ((a)->Funcs->CloseCapture((a)))
#define ALCdevice_StartCapture(a)        ((a)->Funcs->StartCapture((a)))
#define ALCdevice_StopCapture(a)         ((a)->Funcs->StopCapture((a)))
#define ALCdevice_CaptureSamples(a,b,c)  ((a)->Funcs->CaptureSamples((a), (b), (c)))
#define ALCdevice_AvailableSamples(a)    ((a)->Funcs->AvailableSamples((a)))

struct ALCcontext_struct
{
    ALlistener  Listener;

    struct ALsource *Source;
    ALuint           SourceCount;

    struct ALeffectslot *AuxiliaryEffectSlot;
    ALuint               AuxiliaryEffectSlotCount;
    // Maximum number of slots that can be created
    ALuint               AuxiliaryEffectSlotMax;

    ALenum      LastError;
    ALboolean   InUse;

    ALuint      Frequency;

    ALenum      DistanceModel;

    ALfloat     DopplerFactor;
    ALfloat     DopplerVelocity;
    ALfloat     flSpeedOfSound;

    ALint       lNumMonoSources;
    ALint       lNumStereoSources;

    ALuint      NumSends;

    ALfloat     PanningLUT[OUTPUTCHANNELS * LUT_NUM];
    ALint       NumChan;

    ALfloat     ChannelMatrix[OUTPUTCHANNELS][OUTPUTCHANNELS];

    ALCdevice  *Device;
    const ALCchar *ExtensionList;

    struct bs2b *bs2b;

    ALCcontext *next;
};

ALCvoid ReleaseALC(ALCvoid);

ALCchar *AppendDeviceList(char *name);
ALCchar *AppendAllDeviceList(char *name);
ALCchar *AppendCaptureDeviceList(char *name);

ALCvoid SetALCError(ALenum errorCode);

ALCvoid SuspendContext(ALCcontext *context);
ALCvoid ProcessContext(ALCcontext *context);

ALvoid *StartThread(ALuint (*func)(ALvoid*), ALvoid *ptr);
ALuint StopThread(ALvoid *thread);

typedef struct RingBuffer RingBuffer;
RingBuffer *CreateRingBuffer(ALsizei frame_size, ALsizei length);
void DestroyRingBuffer(RingBuffer *ring);
ALsizei RingBufferSize(RingBuffer *ring);
void WriteRingBuffer(RingBuffer *ring, const ALubyte *data, ALsizei len);
void ReadRingBuffer(RingBuffer *ring, ALubyte *data, ALsizei len);

void ReadALConfig(void);
void FreeALConfig(void);
const char *GetConfigValue(const char *blockName, const char *keyName, const char *def);
int GetConfigValueInt(const char *blockName, const char *keyName, int def);
float GetConfigValueFloat(const char *blockName, const char *keyName, float def);

#ifdef __cplusplus
}
#endif

#endif
