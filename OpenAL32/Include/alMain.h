#ifndef AL_MAIN_H
#define AL_MAIN_H

#define AL_MAX_CHANNELS        4
#define AL_MAX_SOURCES        32

#include <string.h>

#include "alu.h"

#ifdef _WIN32
#include <windows.h>
//#define strcasecmp _stricmp

#else

#include <assert.h>
#include <pthread.h>

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

#define min(x,y) (((x)<(y))?(x):(y))
#define max(x,y) (((x)>(y))?(x):(y))
#endif

#include "alBuffer.h"
#include "alError.h"
#include "alExtension.h"
#include "alListener.h"
#include "alSource.h"
#include "alState.h"
#include "alThunk.h"

#ifdef __cplusplus
extern "C"
{
#endif

extern CRITICAL_SECTION g_mutex;

extern char szDebug[256];

#define AL_PRINT(...) do {                       \
    int _al_print_i;                             \
    char *_al_print_fn = strrchr(__FILE__, '/'); \
    if(!_al_print_fn) _al_print_fn  = __FILE__;  \
    else              _al_print_fn += 1;         \
    _al_print_i = snprintf(szDebug, sizeof(szDebug), "AL lib: %s:%d: ", _al_print_fn, __LINE__); \
    snprintf(szDebug+_al_print_i, sizeof(szDebug)-_al_print_i, __VA_ARGS__); \
    fprintf(stderr, "%s", szDebug);              \
} while(0)


#define AL_FORMAT_MONO_IMA4                      0x1300
#define AL_FORMAT_STEREO_IMA4                    0x1301
// These are from AL_EXT_MCFORMATS, which we don't support yet but the mixer
// can use 4-channel formats
#define AL_FORMAT_QUAD8                          0x1204
#define AL_FORMAT_QUAD16                         0x1205

#define SWMIXER_OUTPUT_RATE        44100
//#define OUTPUT_BUFFER_SIZE         (32768*SWMIXER_OUTPUT_RATE/22050)

#define SPEEDOFSOUNDMETRESPERSEC   (343.3f)

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
void alcDSoundInit(BackendFuncs *func_list);
void alcWinMMInit(BackendFuncs *FuncList);


struct ALCdevice_struct
{
    ALboolean    InUse;
    ALboolean    IsCaptureDevice;

    ALuint       Frequency;
    ALuint       UpdateFreq;
    ALuint       FrameSize;
    ALuint       Channels;
    ALenum       Format;

    ALCchar      *szDeviceName;

    // Maximum number of sources that can be created
    ALuint       MaxNoOfSources;

    // Context created on this device
    ALCcontext   *Context;

    BackendFuncs *Funcs;
    void         *ExtraData; // For the backend's use
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

    ALsource   *Source;
    ALuint      SourceCount;

    ALenum      LastError;
    ALboolean   InUse;

    ALuint      Frequency;

    ALenum      DistanceModel;

    ALfloat     DopplerFactor;
    ALfloat     DopplerVelocity;
    ALfloat     flSpeedOfSound;

    ALint       lNumMonoSources;
    ALint       lNumStereoSources;

    ALCdevice  *Device;
    ALCchar     ExtensionList[1024];

    ALCcontext *next;
};

ALCchar *AppendDeviceList(char *name);
ALCchar *AppendAllDeviceList(char *name);
ALCchar *AppendCaptureDeviceList(char *name);

ALCvoid SetALCError(ALenum errorCode);

ALCvoid SuspendContext(ALCcontext *context);
ALCvoid ProcessContext(ALCcontext *context);

ALvoid *StartThread(ALuint (*func)(ALvoid*), ALvoid *ptr);
ALuint StopThread(ALvoid *thread);

void ReadALConfig(void);
void FreeALConfig(void);
const char *GetConfigValue(const char *blockName, const char *keyName, const char *def);
int GetConfigValueInt(const char *blockName, const char *keyName, int def);
float GetConfigValueFloat(const char *blockName, const char *keyName, float def);

#ifdef __cplusplus
}
#endif

#endif
