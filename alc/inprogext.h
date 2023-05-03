#ifndef INPROGEXT_H
#define INPROGEXT_H

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef AL_SOFT_map_buffer
#define AL_SOFT_map_buffer 1
typedef unsigned int ALbitfieldSOFT;
#define AL_MAP_READ_BIT_SOFT                     0x00000001
#define AL_MAP_WRITE_BIT_SOFT                    0x00000002
#define AL_MAP_PERSISTENT_BIT_SOFT               0x00000004
#define AL_PRESERVE_DATA_BIT_SOFT                0x00000008
typedef void (AL_APIENTRY*LPALBUFFERSTORAGESOFT)(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq, ALbitfieldSOFT flags);
typedef void* (AL_APIENTRY*LPALMAPBUFFERSOFT)(ALuint buffer, ALsizei offset, ALsizei length, ALbitfieldSOFT access);
typedef void (AL_APIENTRY*LPALUNMAPBUFFERSOFT)(ALuint buffer);
typedef void (AL_APIENTRY*LPALFLUSHMAPPEDBUFFERSOFT)(ALuint buffer, ALsizei offset, ALsizei length);
#ifdef AL_ALEXT_PROTOTYPES
AL_API void AL_APIENTRY alBufferStorageSOFT(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq, ALbitfieldSOFT flags);
AL_API void* AL_APIENTRY alMapBufferSOFT(ALuint buffer, ALsizei offset, ALsizei length, ALbitfieldSOFT access);
AL_API void AL_APIENTRY alUnmapBufferSOFT(ALuint buffer);
AL_API void AL_APIENTRY alFlushMappedBufferSOFT(ALuint buffer, ALsizei offset, ALsizei length);
#endif
#endif

#ifndef AL_SOFT_bformat_hoa
#define AL_SOFT_bformat_hoa
#define AL_UNPACK_AMBISONIC_ORDER_SOFT           0x199D
#endif

#ifndef AL_SOFT_convolution_reverb
#define AL_SOFT_convolution_reverb
#define AL_EFFECT_CONVOLUTION_REVERB_SOFT        0xA000
#define AL_EFFECTSLOT_STATE_SOFT                 0x199D
typedef void (AL_APIENTRY*LPALAUXILIARYEFFECTSLOTPLAYSOFT)(ALuint slotid);
typedef void (AL_APIENTRY*LPALAUXILIARYEFFECTSLOTPLAYVSOFT)(ALsizei n, const ALuint *slotids);
typedef void (AL_APIENTRY*LPALAUXILIARYEFFECTSLOTSTOPSOFT)(ALuint slotid);
typedef void (AL_APIENTRY*LPALAUXILIARYEFFECTSLOTSTOPVSOFT)(ALsizei n, const ALuint *slotids);
#ifdef AL_ALEXT_PROTOTYPES
AL_API void AL_APIENTRY alAuxiliaryEffectSlotPlaySOFT(ALuint slotid);
AL_API void AL_APIENTRY alAuxiliaryEffectSlotPlayvSOFT(ALsizei n, const ALuint *slotids);
AL_API void AL_APIENTRY alAuxiliaryEffectSlotStopSOFT(ALuint slotid);
AL_API void AL_APIENTRY alAuxiliaryEffectSlotStopvSOFT(ALsizei n, const ALuint *slotids);
#endif
#endif

#ifndef AL_SOFT_hold_on_disconnect
#define AL_SOFT_hold_on_disconnect
#define AL_STOP_SOURCES_ON_DISCONNECT_SOFT       0x19AB
#endif

#ifndef ALC_EXT_debug
#define ALC_EXT_debug
#define ALC_CONTEXT_FLAGS_EXT                    0x19CE
#define ALC_CONTEXT_DEBUG_BIT_EXT                0x0001
#endif

#ifndef AL_EXT_debug
#define AL_EXT_debug
#define AL_DONT_CARE_EXT                         0x0002
#define AL_DEBUG_OUTPUT_EXT                      0x19B2
#define AL_DEBUG_CALLBACK_FUNCTION_EXT           0x19B3
#define AL_DEBUG_CALLBACK_USER_PARAM_EXT         0x19B4
#define AL_DEBUG_SOURCE_API_EXT                  0x19B5
#define AL_DEBUG_SOURCE_AUDIO_SYSTEM_EXT         0x19B6
#define AL_DEBUG_SOURCE_THIRD_PARTY_EXT          0x19B7
#define AL_DEBUG_SOURCE_APPLICATION_EXT          0x19B8
#define AL_DEBUG_SOURCE_OTHER_EXT                0x19B9
#define AL_DEBUG_TYPE_ERROR_EXT                  0x19BA
#define AL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_EXT    0x19BB
#define AL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_EXT     0x19BC
#define AL_DEBUG_TYPE_PORTABILITY_EXT            0x19BD
#define AL_DEBUG_TYPE_PERFORMANCE_EXT            0x19BE
#define AL_DEBUG_TYPE_MARKER_EXT                 0x19BF
#define AL_DEBUG_TYPE_PUSH_GROUP_EXT             0x19C0
#define AL_DEBUG_TYPE_POP_GROUP_EXT              0x19C1
#define AL_DEBUG_TYPE_OTHER_EXT                  0x19C2
#define AL_DEBUG_SEVERITY_HIGH_EXT               0x19C3
#define AL_DEBUG_SEVERITY_MEDIUM_EXT             0x19C4
#define AL_DEBUG_SEVERITY_LOW_EXT                0x19C5
#define AL_DEBUG_SEVERITY_NOTIFICATION_EXT       0x19C6
#define AL_DEBUG_LOGGED_MESSAGES_EXT             0x19C7
#define AL_DEBUG_NEXT_LOGGED_MESSAGE_LENGTH_EXT  0x19C8
#define AL_MAX_DEBUG_MESSAGE_LENGTH_EXT          0x19C9
#define AL_MAX_DEBUG_LOGGED_MESSAGES_EXT         0x19CA
#define AL_MAX_DEBUG_GROUP_STACK_DEPTH_EXT       0x19CB
#define AL_STACK_OVERFLOW_EXT                    0x19CC
#define AL_STACK_UNDERFLOW_EXT                   0x19CD
#define AL_CONTEXT_FLAGS_EXT                     0x19CE

typedef void (AL_APIENTRY*ALDEBUGPROCEXT)(ALenum source, ALenum type, ALuint id, ALenum severity, ALsizei length, const ALchar *message, void *userParam);
typedef void (AL_APIENTRY*LPALDEBUGMESSAGECALLBACKEXT)(ALDEBUGPROCEXT callback, void *userParam);
typedef void (AL_APIENTRY*LPALDEBUGMESSAGEINSERTEXT)(ALenum source, ALenum type, ALuint id, ALenum severity, ALsizei length, const ALchar *message);
typedef void (AL_APIENTRY*LPALDEBUGMESSAGECONTROLEXT)(ALenum source, ALenum type, ALenum severity, ALsizei count, const ALuint *ids, ALboolean enable);
typedef void (AL_APIENTRY*LPALPUSHDEBUGGROUPEXT)(ALenum source, ALuint id, ALsizei length, const ALchar *message);
typedef void (AL_APIENTRY*LPALPOPDEBUGGROUPEXT)(void);
typedef ALuint (AL_APIENTRY*LPALGETDEBUGMESSAGELOGEXT)(ALuint count, ALsizei logBufSize, ALenum *sources, ALenum *types, ALuint *ids, ALenum *severities, ALsizei *lengths, ALchar *logBuf);
#ifdef AL_ALEXT_PROTOTYPES
void AL_APIENTRY alDebugMessageCallbackEXT(ALDEBUGPROCEXT callback, void *userParam) noexcept;
void AL_APIENTRY alDebugMessageInsertEXT(ALenum source, ALenum type, ALuint id, ALenum severity, ALsizei length, const ALchar *message) noexcept;
void AL_APIENTRY alDebugMessageControlEXT(ALenum source, ALenum type, ALenum severity, ALsizei count, const ALuint *ids, ALboolean enable) noexcept;
void AL_APIENTRY alPushDebugGroupEXT(ALenum source, ALuint id, ALsizei length, const ALchar *message) noexcept;
void AL_APIENTRY alPopDebugGroupEXT(void) noexcept;
ALuint AL_APIENTRY alGetDebugMessageLogEXT(ALuint count, ALsizei logBufSize, ALenum *sources, ALenum *types, ALuint *ids, ALenum *severities, ALsizei *lengths, ALchar *logBuf) noexcept;
#endif
#endif


/* Non-standard export. Not part of any extension. */
AL_API const ALchar* AL_APIENTRY alsoft_get_version(void);


/* Functions from abandoned extensions. Only here for binary compatibility. */
AL_API void AL_APIENTRY alSourceQueueBufferLayersSOFT(ALuint src, ALsizei nb,
    const ALuint *buffers);

AL_API ALint64SOFT AL_APIENTRY alGetInteger64SOFT(ALenum pname);
AL_API void AL_APIENTRY alGetInteger64vSOFT(ALenum pname, ALint64SOFT *values);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* INPROGEXT_H */
