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
typedef void (AL_APIENTRY*LPALBUFFERSTORAGEDIRECTSOFT)(ALCcontext *context, ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq, ALbitfieldSOFT flags);
typedef void* (AL_APIENTRY*LPALMAPBUFFERDIRECTSOFT)(ALCcontext *context, ALuint buffer, ALsizei offset, ALsizei length, ALbitfieldSOFT access);
typedef void (AL_APIENTRY*LPALUNMAPBUFFERDIRECTSOFT)(ALCcontext *context, ALuint buffer);
typedef void (AL_APIENTRY*LPALFLUSHMAPPEDBUFFERDIRECTSOFT)(ALCcontext *context, ALuint buffer, ALsizei offset, ALsizei length);
#ifdef AL_ALEXT_PROTOTYPES
AL_API void AL_APIENTRY alBufferStorageSOFT(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq, ALbitfieldSOFT flags);
AL_API void* AL_APIENTRY alMapBufferSOFT(ALuint buffer, ALsizei offset, ALsizei length, ALbitfieldSOFT access);
AL_API void AL_APIENTRY alUnmapBufferSOFT(ALuint buffer);
AL_API void AL_APIENTRY alFlushMappedBufferSOFT(ALuint buffer, ALsizei offset, ALsizei length);
void AL_APIENTRY alBufferStorageDirectSOFT(ALCcontext *context, ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq, ALbitfieldSOFT flags) noexcept;
void* AL_APIENTRY alMapBufferDirectSOFT(ALCcontext *context, ALuint buffer, ALsizei offset, ALsizei length, ALbitfieldSOFT access) noexcept;
void AL_APIENTRY alUnmapBufferDirectSOFT(ALCcontext *context, ALuint buffer) noexcept;
void AL_APIENTRY alFlushMappedBufferDirectSOFT(ALCcontext *context, ALuint buffer, ALsizei offset, ALsizei length) noexcept;
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
typedef void (AL_APIENTRY*LPALDEBUGMESSAGECALLBACKDIRECTEXT)(ALCcontext *context, ALDEBUGPROCEXT callback, void *userParam);
typedef void (AL_APIENTRY*LPALDEBUGMESSAGEINSERTDIRECTEXT)(ALCcontext *context, ALenum source, ALenum type, ALuint id, ALenum severity, ALsizei length, const ALchar *message);
typedef void (AL_APIENTRY*LPALDEBUGMESSAGECONTROLDIRECTEXT)(ALCcontext *context, ALenum source, ALenum type, ALenum severity, ALsizei count, const ALuint *ids, ALboolean enable);
typedef void (AL_APIENTRY*LPALPUSHDEBUGGROUPDIRECTEXT)(ALCcontext *context, ALenum source, ALuint id, ALsizei length, const ALchar *message);
typedef void (AL_APIENTRY*LPALPOPDEBUGGROUPDIRECTEXT)(ALCcontext *context);
typedef ALuint (AL_APIENTRY*LPALGETDEBUGMESSAGELOGDIRECTEXT)(ALCcontext *context, ALuint count, ALsizei logBufSize, ALenum *sources, ALenum *types, ALuint *ids, ALenum *severities, ALsizei *lengths, ALchar *logBuf);
#ifdef AL_ALEXT_PROTOTYPES
void AL_APIENTRY alDebugMessageCallbackEXT(ALDEBUGPROCEXT callback, void *userParam) noexcept;
void AL_APIENTRY alDebugMessageInsertEXT(ALenum source, ALenum type, ALuint id, ALenum severity, ALsizei length, const ALchar *message) noexcept;
void AL_APIENTRY alDebugMessageControlEXT(ALenum source, ALenum type, ALenum severity, ALsizei count, const ALuint *ids, ALboolean enable) noexcept;
void AL_APIENTRY alPushDebugGroupEXT(ALenum source, ALuint id, ALsizei length, const ALchar *message) noexcept;
void AL_APIENTRY alPopDebugGroupEXT(void) noexcept;
ALuint AL_APIENTRY alGetDebugMessageLogEXT(ALuint count, ALsizei logBufSize, ALenum *sources, ALenum *types, ALuint *ids, ALenum *severities, ALsizei *lengths, ALchar *logBuf) noexcept;
void AL_APIENTRY alDebugMessageCallbackDirectEXT(ALCcontext *context, ALDEBUGPROCEXT callback, void *userParam) noexcept;
void AL_APIENTRY alDebugMessageInsertDirectEXT(ALCcontext *context, ALenum source, ALenum type, ALuint id, ALenum severity, ALsizei length, const ALchar *message) noexcept;
void AL_APIENTRY alDebugMessageControlDirectEXT(ALCcontext *context, ALenum source, ALenum type, ALenum severity, ALsizei count, const ALuint *ids, ALboolean enable) noexcept;
void AL_APIENTRY alPushDebugGroupDirectEXT(ALCcontext *context, ALenum source, ALuint id, ALsizei length, const ALchar *message) noexcept;
void AL_APIENTRY alPopDebugGroupDirectEXT(ALCcontext *context) noexcept;
ALuint AL_APIENTRY alGetDebugMessageLogDirectEXT(ALCcontext *context, ALuint count, ALsizei logBufSize, ALenum *sources, ALenum *types, ALuint *ids, ALenum *severities, ALsizei *lengths, ALchar *logBuf) noexcept;
#endif
#endif


#ifndef AL_EXT_direct_context
#define AL_EXT_direct_context
typedef void (AL_APIENTRY *LPALENABLEDIRECT)(ALCcontext *context, ALenum capability);
typedef void (AL_APIENTRY *LPALDISABLEDIRECT)(ALCcontext *context, ALenum capability);
typedef ALboolean (AL_APIENTRY *LPALISENABLEDDIRECT)(ALCcontext *context, ALenum capability);
typedef void (AL_APIENTRY *LPALDOPPLERFACTORDIRECT)(ALCcontext *context, ALfloat value);
typedef void (AL_APIENTRY *LPALSPEEDOFSOUNDDIRECT)(ALCcontext *context, ALfloat value);
typedef void (AL_APIENTRY *LPALDISTANCEMODELDIRECT)(ALCcontext *context, ALenum distanceModel);
typedef const ALchar* (AL_APIENTRY *LPALGETSTRINGDIRECT)(ALCcontext *context, ALenum param);
typedef void (AL_APIENTRY *LPALGETBOOLEANVDIRECT)(ALCcontext *context, ALenum param, ALboolean *values);
typedef void (AL_APIENTRY *LPALGETINTEGERVDIRECT)(ALCcontext *context, ALenum param, ALint *values);
typedef void (AL_APIENTRY *LPALGETFLOATVDIRECT)(ALCcontext *context, ALenum param, ALfloat *values);
typedef void (AL_APIENTRY *LPALGETDOUBLEVDIRECT)(ALCcontext *context, ALenum param, ALdouble *values);
typedef ALboolean (AL_APIENTRY *LPALGETBOOLEANDIRECT)(ALCcontext *context, ALenum param);
typedef ALint (AL_APIENTRY *LPALGETINTEGERDIRECT)(ALCcontext *context, ALenum param);
typedef ALfloat (AL_APIENTRY *LPALGETFLOATDIRECT)(ALCcontext *context, ALenum param);
typedef ALdouble (AL_APIENTRY *LPALGETDOUBLEDIRECT)(ALCcontext *context, ALenum param);
typedef ALenum (AL_APIENTRY *LPALGETERRORDIRECT)(ALCcontext *context);
typedef ALboolean (AL_APIENTRY *LPALISEXTENSIONPRESENTDIRECT)(ALCcontext *context, const ALchar *extname);
typedef void* (AL_APIENTRY *LPALGETPROCADDRESSDIRECT)(ALCcontext *context, const ALchar *fname);
typedef ALenum (AL_APIENTRY *LPALGETENUMVALUEDIRECT)(ALCcontext *context, const ALchar *ename);
typedef void (AL_APIENTRY *LPALLISTENERFDIRECT)(ALCcontext *context, ALenum param, ALfloat value);
typedef void (AL_APIENTRY *LPALLISTENER3FDIRECT)(ALCcontext *context, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3);
typedef void (AL_APIENTRY *LPALLISTENERFVDIRECT)(ALCcontext *context, ALenum param, const ALfloat *values);
typedef void (AL_APIENTRY *LPALLISTENERIDIRECT)(ALCcontext *context, ALenum param, ALint value);
typedef void (AL_APIENTRY *LPALLISTENER3IDIRECT)(ALCcontext *context, ALenum param, ALint value1, ALint value2, ALint value3);
typedef void (AL_APIENTRY *LPALLISTENERIVDIRECT)(ALCcontext *context, ALenum param, const ALint *values);
typedef void (AL_APIENTRY *LPALGETLISTENERFDIRECT)(ALCcontext *context, ALenum param, ALfloat *value);
typedef void (AL_APIENTRY *LPALGETLISTENER3FDIRECT)(ALCcontext *context, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3);
typedef void (AL_APIENTRY *LPALGETLISTENERFVDIRECT)(ALCcontext *context, ALenum param, ALfloat *values);
typedef void (AL_APIENTRY *LPALGETLISTENERIDIRECT)(ALCcontext *context, ALenum param, ALint *value);
typedef void (AL_APIENTRY *LPALGETLISTENER3IDIRECT)(ALCcontext *context, ALenum param, ALint *value1, ALint *value2, ALint *value3);
typedef void (AL_APIENTRY *LPALGETLISTENERIVDIRECT)(ALCcontext *context, ALenum param, ALint *values);
typedef void (AL_APIENTRY *LPALGENSOURCESDIRECT)(ALCcontext *context, ALsizei n, ALuint *sources);
typedef void (AL_APIENTRY *LPALDELETESOURCESDIRECT)(ALCcontext *context, ALsizei n, const ALuint *sources);
typedef ALboolean (AL_APIENTRY *LPALISSOURCEDIRECT)(ALCcontext *context, ALuint source);
typedef void (AL_APIENTRY *LPALSOURCEFDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALfloat value);
typedef void (AL_APIENTRY *LPALSOURCE3FDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3);
typedef void (AL_APIENTRY *LPALSOURCEFVDIRECT)(ALCcontext *context, ALuint source, ALenum param, const ALfloat *values);
typedef void (AL_APIENTRY *LPALSOURCEIDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALint value);
typedef void (AL_APIENTRY *LPALSOURCE3IDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALint value1, ALint value2, ALint value3);
typedef void (AL_APIENTRY *LPALSOURCEIVDIRECT)(ALCcontext *context, ALuint source, ALenum param, const ALint *values);
typedef void (AL_APIENTRY *LPALGETSOURCEFDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALfloat *value);
typedef void (AL_APIENTRY *LPALGETSOURCE3FDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3);
typedef void (AL_APIENTRY *LPALGETSOURCEFVDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALfloat *values);
typedef void (AL_APIENTRY *LPALGETSOURCEIDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALint *value);
typedef void (AL_APIENTRY *LPALGETSOURCE3IDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALint *value1, ALint *value2, ALint *value3);
typedef void (AL_APIENTRY *LPALGETSOURCEIVDIRECT)(ALCcontext *context, ALuint source, ALenum param, ALint *values);
typedef void (AL_APIENTRY *LPALSOURCEPLAYVDIRECT)(ALCcontext *context, ALsizei n, const ALuint *sources);
typedef void (AL_APIENTRY *LPALSOURCESTOPVDIRECT)(ALCcontext *context, ALsizei n, const ALuint *sources);
typedef void (AL_APIENTRY *LPALSOURCEREWINDVDIRECT)(ALCcontext *context, ALsizei n, const ALuint *sources);
typedef void (AL_APIENTRY *LPALSOURCEPAUSEVDIRECT)(ALCcontext *context, ALsizei n, const ALuint *sources);
typedef void (AL_APIENTRY *LPALSOURCEPLAYDIRECT)(ALCcontext *context, ALuint source);
typedef void (AL_APIENTRY *LPALSOURCESTOPDIRECT)(ALCcontext *context, ALuint source);
typedef void (AL_APIENTRY *LPALSOURCEREWINDDIRECT)(ALCcontext *context, ALuint source);
typedef void (AL_APIENTRY *LPALSOURCEPAUSEDIRECT)(ALCcontext *context, ALuint source);
typedef void (AL_APIENTRY *LPALSOURCEQUEUEBUFFERSDIRECT)(ALCcontext *context, ALuint source, ALsizei nb, const ALuint *buffers);
typedef void (AL_APIENTRY *LPALSOURCEUNQUEUEBUFFERSDIRECT)(ALCcontext *context, ALuint source, ALsizei nb, ALuint *buffers);
typedef void (AL_APIENTRY *LPALGENBUFFERSDIRECT)(ALCcontext *context, ALsizei n, ALuint *buffers);
typedef void (AL_APIENTRY *LPALDELETEBUFFERSDIRECT)(ALCcontext *context, ALsizei n, const ALuint *buffers);
typedef ALboolean (AL_APIENTRY *LPALISBUFFERDIRECT)(ALCcontext *context, ALuint buffer);
typedef void (AL_APIENTRY *LPALBUFFERDATADIRECT)(ALCcontext *context, ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei samplerate);
typedef void (AL_APIENTRY *LPALBUFFERFDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALfloat value);
typedef void (AL_APIENTRY *LPALBUFFER3FDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3);
typedef void (AL_APIENTRY *LPALBUFFERFVDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, const ALfloat *values);
typedef void (AL_APIENTRY *LPALBUFFERIDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALint value);
typedef void (AL_APIENTRY *LPALBUFFER3IDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALint value1, ALint value2, ALint value3);
typedef void (AL_APIENTRY *LPALBUFFERIVDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, const ALint *values);
typedef void (AL_APIENTRY *LPALGETBUFFERFDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALfloat *value);
typedef void (AL_APIENTRY *LPALGETBUFFER3FDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3);
typedef void (AL_APIENTRY *LPALGETBUFFERFVDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALfloat *values);
typedef void (AL_APIENTRY *LPALGETBUFFERIDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALint *value);
typedef void (AL_APIENTRY *LPALGETBUFFER3IDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALint *value1, ALint *value2, ALint *value3);
typedef void (AL_APIENTRY *LPALGETBUFFERIVDIRECT)(ALCcontext *context, ALuint buffer, ALenum param, ALint *values);
/* ALC_EXT_EFX */
typedef void (AL_APIENTRY *LPALGENEFFECTSDIRECT)(ALCcontext *context, ALsizei n, ALuint *effects);
typedef void (AL_APIENTRY *LPALDELETEEFFECTSDIRECT)(ALCcontext *context, ALsizei n, const ALuint *effects);
typedef ALboolean (AL_APIENTRY *LPALISEFFECTDIRECT)(ALCcontext *context, ALuint effect);
typedef void (AL_APIENTRY *LPALEFFECTIDIRECT)(ALCcontext *context, ALuint effect, ALenum param, ALint value);
typedef void (AL_APIENTRY *LPALEFFECTIVDIRECT)(ALCcontext *context, ALuint effect, ALenum param, const ALint *values);
typedef void (AL_APIENTRY *LPALEFFECTFDIRECT)(ALCcontext *context, ALuint effect, ALenum param, ALfloat value);
typedef void (AL_APIENTRY *LPALEFFECTFVDIRECT)(ALCcontext *context, ALuint effect, ALenum param, const ALfloat *values);
typedef void (AL_APIENTRY *LPALGETEFFECTIDIRECT)(ALCcontext *context, ALuint effect, ALenum param, ALint *value);
typedef void (AL_APIENTRY *LPALGETEFFECTIVDIRECT)(ALCcontext *context, ALuint effect, ALenum param, ALint *values);
typedef void (AL_APIENTRY *LPALGETEFFECTFDIRECT)(ALCcontext *context, ALuint effect, ALenum param, ALfloat *value);
typedef void (AL_APIENTRY *LPALGETEFFECTFVDIRECT)(ALCcontext *context, ALuint effect, ALenum param, ALfloat *values);
typedef void (AL_APIENTRY *LPALGENFILTERSDIRECT)(ALCcontext *context, ALsizei n, ALuint *filters);
typedef void (AL_APIENTRY *LPALDELETEFILTERSDIRECT)(ALCcontext *context, ALsizei n, const ALuint *filters);
typedef ALboolean (AL_APIENTRY *LPALISFILTERDIRECT)(ALCcontext *context, ALuint filter);
typedef void (AL_APIENTRY *LPALFILTERIDIRECT)(ALCcontext *context, ALuint filter, ALenum param, ALint value);
typedef void (AL_APIENTRY *LPALFILTERIVDIRECT)(ALCcontext *context, ALuint filter, ALenum param, const ALint *values);
typedef void (AL_APIENTRY *LPALFILTERFDIRECT)(ALCcontext *context, ALuint filter, ALenum param, ALfloat value);
typedef void (AL_APIENTRY *LPALFILTERFVDIRECT)(ALCcontext *context, ALuint filter, ALenum param, const ALfloat *values);
typedef void (AL_APIENTRY *LPALGETFILTERIDIRECT)(ALCcontext *context, ALuint filter, ALenum param, ALint *value);
typedef void (AL_APIENTRY *LPALGETFILTERIVDIRECT)(ALCcontext *context, ALuint filter, ALenum param, ALint *values);
typedef void (AL_APIENTRY *LPALGETFILTERFDIRECT)(ALCcontext *context, ALuint filter, ALenum param, ALfloat *value);
typedef void (AL_APIENTRY *LPALGETFILTERFVDIRECT)(ALCcontext *context, ALuint filter, ALenum param, ALfloat *values);
typedef void (AL_APIENTRY *LPALGENAUXILIARYEFFECTSLOTSDIRECT)(ALCcontext *context, ALsizei n, ALuint *effectslots);
typedef void (AL_APIENTRY *LPALDELETEAUXILIARYEFFECTSLOTSDIRECT)(ALCcontext *context, ALsizei n, const ALuint *effectslots);
typedef ALboolean (AL_APIENTRY *LPALISAUXILIARYEFFECTSLOTDIRECT)(ALCcontext *context, ALuint effectslot);
typedef void (AL_APIENTRY *LPALAUXILIARYEFFECTSLOTIDIRECT)(ALCcontext *context, ALuint effectslot, ALenum param, ALint value);
typedef void (AL_APIENTRY *LPALAUXILIARYEFFECTSLOTIVDIRECT)(ALCcontext *context, ALuint effectslot, ALenum param, const ALint *values);
typedef void (AL_APIENTRY *LPALAUXILIARYEFFECTSLOTFDIRECT)(ALCcontext *context, ALuint effectslot, ALenum param, ALfloat value);
typedef void (AL_APIENTRY *LPALAUXILIARYEFFECTSLOTFVDIRECT)(ALCcontext *context, ALuint effectslot, ALenum param, const ALfloat *values);
typedef void (AL_APIENTRY *LPALGETAUXILIARYEFFECTSLOTIDIRECT)(ALCcontext *context, ALuint effectslot, ALenum param, ALint *value);
typedef void (AL_APIENTRY *LPALGETAUXILIARYEFFECTSLOTIVDIRECT)(ALCcontext *context, ALuint effectslot, ALenum param, ALint *values);
typedef void (AL_APIENTRY *LPALGETAUXILIARYEFFECTSLOTFDIRECT)(ALCcontext *context, ALuint effectslot, ALenum param, ALfloat *value);
typedef void (AL_APIENTRY *LPALGETAUXILIARYEFFECTSLOTFVDIRECT)(ALCcontext *context, ALuint effectslot, ALenum param, ALfloat *values);
/* AL_EXT_BUFFER_DATA_STATIC */
typedef void (AL_APIENTRY *LPALBUFFERDATASTATICDIRECT)(ALCcontext *context, ALuint buffer, ALenum format, ALvoid *data, ALsizei size, ALsizei freq);
/* AL_SOFT_buffer_sub_data */
typedef void (AL_APIENTRY *LPALBUFFERSUBDATADIRECTSOFT)(ALCcontext *context, ALuint buffer, ALenum format, const ALvoid *data, ALsizei offset, ALsizei length);
/* AL_EXT_FOLDBACK */
typedef void (AL_APIENTRY *LPALREQUESTFOLDBACKSTARTDIRECT)(ALCcontext *context, ALenum mode, ALsizei count, ALsizei length, ALfloat *mem, LPALFOLDBACKCALLBACK callback);
typedef void (AL_APIENTRY *LPALREQUESTFOLDBACKSTOPDIRECT)(ALCcontext *context);
/* AL_SOFT_source_latency */
typedef void (AL_APIENTRY *LPALSOURCEDDIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALdouble);
typedef void (AL_APIENTRY *LPALSOURCE3DDIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALdouble,ALdouble,ALdouble);
typedef void (AL_APIENTRY *LPALSOURCEDVDIRECTSOFT)(ALCcontext*,ALuint,ALenum,const ALdouble*);
typedef void (AL_APIENTRY *LPALGETSOURCEDDIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALdouble*);
typedef void (AL_APIENTRY *LPALGETSOURCE3DDIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALdouble*,ALdouble*,ALdouble*);
typedef void (AL_APIENTRY *LPALGETSOURCEDVDIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALdouble*);
typedef void (AL_APIENTRY *LPALSOURCEI64DIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALint64SOFT);
typedef void (AL_APIENTRY *LPALSOURCE3I64DIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALint64SOFT,ALint64SOFT,ALint64SOFT);
typedef void (AL_APIENTRY *LPALSOURCEI64VDIRECTSOFT)(ALCcontext*,ALuint,ALenum,const ALint64SOFT*);
typedef void (AL_APIENTRY *LPALGETSOURCEI64DIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALint64SOFT*);
typedef void (AL_APIENTRY *LPALGETSOURCE3I64DIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALint64SOFT*,ALint64SOFT*,ALint64SOFT*);
typedef void (AL_APIENTRY *LPALGETSOURCEI64VDIRECTSOFT)(ALCcontext*,ALuint,ALenum,ALint64SOFT*);
/* AL_SOFT_deferred_updates */
typedef void (AL_APIENTRY *LPALDEFERUPDATESDIRECTSOFT)(ALCcontext *context);
typedef void (AL_APIENTRY *LPALPROCESSUPDATESDIRECTSOFT)(ALCcontext *context);
/* AL_SOFT_source_resampler */
typedef const ALchar* (AL_APIENTRY *LPALGETSTRINGIDIRECTSOFT)(ALCcontext *context, ALenum pname, ALsizei index);
/* AL_SOFT_events */
typedef void (AL_APIENTRY *LPALEVENTCONTROLDIRECTSOFT)(ALCcontext *context, ALsizei count, const ALenum *types, ALboolean enable);
typedef void (AL_APIENTRY *LPALEVENTCALLBACKDIRECTSOFT)(ALCcontext *context, ALEVENTPROCSOFT callback, void *userParam);
typedef void* (AL_APIENTRY *LPALGETPOINTERDIRECTSOFT)(ALCcontext *context, ALenum pname);
typedef void (AL_APIENTRY *LPALGETPOINTERVDIRECTSOFT)(ALCcontext *context, ALenum pname, void **values);
/* AL_SOFT_callback_buffer */
typedef void (AL_APIENTRY *LPALBUFFERCALLBACKDIRECTSOFT)(ALCcontext *context, ALuint buffer, ALenum format, ALsizei freq, ALBUFFERCALLBACKTYPESOFT callback, ALvoid *userptr);
typedef void (AL_APIENTRY *LPALGETBUFFERPTRDIRECTSOFT)(ALCcontext *context, ALuint buffer, ALenum param, ALvoid **value);
typedef void (AL_APIENTRY *LPALGETBUFFER3PTRDIRECTSOFT)(ALCcontext *context, ALuint buffer, ALenum param, ALvoid **value1, ALvoid **value2, ALvoid **value3);
typedef void (AL_APIENTRY *LPALGETBUFFERPTRVDIRECTSOFT)(ALCcontext *context, ALuint buffer, ALenum param, ALvoid **values);
/* AL_SOFT_source_start_delay */
typedef void (AL_APIENTRY *LPALSOURCEPLAYATTIMEDIRECTSOFT)(ALCcontext *context, ALuint source, ALint64SOFT start_time);
typedef void (AL_APIENTRY *LPALSOURCEPLAYATTIMEVDIRECTSOFT)(ALCcontext *context, ALsizei n, const ALuint *sources, ALint64SOFT start_time);
/* EAX */
typedef ALenum (AL_APIENTRY *LPEAXSETDIRECT)(ALCcontext *context, const struct _GUID *property_set_id, ALuint property_id, ALuint property_source_id, ALvoid *property_buffer, ALuint property_size);
typedef ALenum (AL_APIENTRY *LPEAXGETDIRECT)(ALCcontext *context, const struct _GUID *property_set_id, ALuint property_id, ALuint property_source_id, ALvoid *property_value, ALuint property_value_size);
typedef ALboolean (AL_APIENTRY *LPEAXSETBUFFERMODEDIRECT)(ALCcontext *context, ALsizei n, const ALuint *buffers, ALint value);
typedef ALenum (AL_APIENTRY *LPEAXGETBUFFERMODEDIRECT)(ALCcontext *context, ALuint buffer, ALint *pReserved);
#ifdef AL_ALEXT_PROTOTYPES
void AL_APIENTRY alEnableDirect(ALCcontext *context, ALenum capability) noexcept;
void AL_APIENTRY alDisableDirect(ALCcontext *context, ALenum capability) noexcept;
ALboolean AL_APIENTRY alIsEnabledDirect(ALCcontext *context, ALenum capability) noexcept;

void AL_APIENTRY alDopplerFactorDirect(ALCcontext *context, ALfloat value) noexcept;
void AL_APIENTRY alSpeedOfSoundDirect(ALCcontext *context, ALfloat value) noexcept;
void AL_APIENTRY alDistanceModelDirect(ALCcontext *context, ALenum distanceModel) noexcept;

const ALchar* AL_APIENTRY alGetStringDirect(ALCcontext *context, ALenum param) noexcept;
void AL_APIENTRY alGetBooleanvDirect(ALCcontext *context, ALenum param, ALboolean *values) noexcept;
void AL_APIENTRY alGetIntegervDirect(ALCcontext *context, ALenum param, ALint *values) noexcept;
void AL_APIENTRY alGetFloatvDirect(ALCcontext *context, ALenum param, ALfloat *values) noexcept;
void AL_APIENTRY alGetDoublevDirect(ALCcontext *context, ALenum param, ALdouble *values) noexcept;
ALboolean AL_APIENTRY alGetBooleanDirect(ALCcontext *context, ALenum param) noexcept;
ALint AL_APIENTRY alGetIntegerDirect(ALCcontext *context, ALenum param) noexcept;
ALfloat AL_APIENTRY alGetFloatDirect(ALCcontext *context, ALenum param) noexcept;
ALdouble AL_APIENTRY alGetDoubleDirect(ALCcontext *context, ALenum param) noexcept;

ALenum AL_APIENTRY alGetErrorDirect(ALCcontext *context) noexcept;
ALboolean AL_APIENTRY alIsExtensionPresentDirect(ALCcontext *context, const ALchar *extname) noexcept;
void* AL_APIENTRY alGetProcAddressDirect(ALCcontext *context, const ALchar *fname) noexcept;
ALenum AL_APIENTRY alGetEnumValueDirect(ALCcontext *context, const ALchar *ename) noexcept;

void AL_APIENTRY alListenerfDirect(ALCcontext *context, ALenum param, ALfloat value) noexcept;
void AL_APIENTRY alListener3fDirect(ALCcontext *context, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) noexcept;
void AL_APIENTRY alListenerfvDirect(ALCcontext *context, ALenum param, const ALfloat *values) noexcept;
void AL_APIENTRY alListeneriDirect(ALCcontext *context, ALenum param, ALint value) noexcept;
void AL_APIENTRY alListener3iDirect(ALCcontext *context, ALenum param, ALint value1, ALint value2, ALint value3) noexcept;
void AL_APIENTRY alListenerivDirect(ALCcontext *context, ALenum param, const ALint *values) noexcept;
void AL_APIENTRY alGetListenerfDirect(ALCcontext *context, ALenum param, ALfloat *value) noexcept;
void AL_APIENTRY alGetListener3fDirect(ALCcontext *context, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) noexcept;
void AL_APIENTRY alGetListenerfvDirect(ALCcontext *context, ALenum param, ALfloat *values) noexcept;
void AL_APIENTRY alGetListeneriDirect(ALCcontext *context, ALenum param, ALint *value) noexcept;
void AL_APIENTRY alGetListener3iDirect(ALCcontext *context, ALenum param, ALint *value1, ALint *value2, ALint *value3) noexcept;
void AL_APIENTRY alGetListenerivDirect(ALCcontext *context, ALenum param, ALint *values) noexcept;

void AL_APIENTRY alGenSourcesDirect(ALCcontext *context, ALsizei n, ALuint *sources) noexcept;
void AL_APIENTRY alDeleteSourcesDirect(ALCcontext *context, ALsizei n, const ALuint *sources) noexcept;
ALboolean AL_APIENTRY alIsSourceDirect(ALCcontext *context, ALuint source) noexcept;
void AL_APIENTRY alSourcefDirect(ALCcontext *context, ALuint source, ALenum param, ALfloat value) noexcept;
void AL_APIENTRY alSource3fDirect(ALCcontext *context, ALuint source, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) noexcept;
void AL_APIENTRY alSourcefvDirect(ALCcontext *context, ALuint source, ALenum param, const ALfloat *values) noexcept;
void AL_APIENTRY alSourceiDirect(ALCcontext *context, ALuint source, ALenum param, ALint value) noexcept;
void AL_APIENTRY alSource3iDirect(ALCcontext *context, ALuint source, ALenum param, ALint value1, ALint value2, ALint value3) noexcept;
void AL_APIENTRY alSourceivDirect(ALCcontext *context, ALuint source, ALenum param, const ALint *values) noexcept;
void AL_APIENTRY alGetSourcefDirect(ALCcontext *context, ALuint source, ALenum param, ALfloat *value) noexcept;
void AL_APIENTRY alGetSource3fDirect(ALCcontext *context, ALuint source, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) noexcept;
void AL_APIENTRY alGetSourcefvDirect(ALCcontext *context, ALuint source, ALenum param, ALfloat *values) noexcept;
void AL_APIENTRY alGetSourceiDirect(ALCcontext *context, ALuint source,  ALenum param, ALint *value) noexcept;
void AL_APIENTRY alGetSource3iDirect(ALCcontext *context, ALuint source, ALenum param, ALint *value1, ALint *value2, ALint *value3) noexcept;
void AL_APIENTRY alGetSourceivDirect(ALCcontext *context, ALuint source,  ALenum param, ALint *values) noexcept;
void AL_APIENTRY alSourcePlayDirect(ALCcontext *context, ALuint source) noexcept;
void AL_APIENTRY alSourceStopDirect(ALCcontext *context, ALuint source) noexcept;
void AL_APIENTRY alSourceRewindDirect(ALCcontext *context, ALuint source) noexcept;
void AL_APIENTRY alSourcePauseDirect(ALCcontext *context, ALuint source) noexcept;
void AL_APIENTRY alSourcePlayvDirect(ALCcontext *context, ALsizei n, const ALuint *sources) noexcept;
void AL_APIENTRY alSourceStopvDirect(ALCcontext *context, ALsizei n, const ALuint *sources) noexcept;
void AL_APIENTRY alSourceRewindvDirect(ALCcontext *context, ALsizei n, const ALuint *sources) noexcept;
void AL_APIENTRY alSourcePausevDirect(ALCcontext *context, ALsizei n, const ALuint *sources) noexcept;
void AL_APIENTRY alSourceQueueBuffersDirect(ALCcontext *context, ALuint source, ALsizei nb, const ALuint *buffers) noexcept;
void AL_APIENTRY alSourceUnqueueBuffersDirect(ALCcontext *context, ALuint source, ALsizei nb, ALuint *buffers) noexcept;

void AL_APIENTRY alGenBuffersDirect(ALCcontext *context, ALsizei n, ALuint *buffers) noexcept;
void AL_APIENTRY alDeleteBuffersDirect(ALCcontext *context, ALsizei n, const ALuint *buffers) noexcept;
ALboolean AL_APIENTRY alIsBufferDirect(ALCcontext *context, ALuint buffer) noexcept;
void AL_APIENTRY alBufferDataDirect(ALCcontext *context, ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei samplerate) noexcept;
void AL_APIENTRY alBufferfDirect(ALCcontext *context, ALuint buffer, ALenum param, ALfloat value) noexcept;
void AL_APIENTRY alBuffer3fDirect(ALCcontext *context, ALuint buffer, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3) noexcept;
void AL_APIENTRY alBufferfvDirect(ALCcontext *context, ALuint buffer, ALenum param, const ALfloat *values) noexcept;
void AL_APIENTRY alBufferiDirect(ALCcontext *context, ALuint buffer, ALenum param, ALint value) noexcept;
void AL_APIENTRY alBuffer3iDirect(ALCcontext *context, ALuint buffer, ALenum param, ALint value1, ALint value2, ALint value3) noexcept;
void AL_APIENTRY alBufferivDirect(ALCcontext *context, ALuint buffer, ALenum param, const ALint *values) noexcept;
void AL_APIENTRY alGetBufferfDirect(ALCcontext *context, ALuint buffer, ALenum param, ALfloat *value) noexcept;
void AL_APIENTRY alGetBuffer3fDirect(ALCcontext *context, ALuint buffer, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3) noexcept;
void AL_APIENTRY alGetBufferfvDirect(ALCcontext *context, ALuint buffer, ALenum param, ALfloat *values) noexcept;
void AL_APIENTRY alGetBufferiDirect(ALCcontext *context, ALuint buffer, ALenum param, ALint *value) noexcept;
void AL_APIENTRY alGetBuffer3iDirect(ALCcontext *context, ALuint buffer, ALenum param, ALint *value1, ALint *value2, ALint *value3) noexcept;
void AL_APIENTRY alGetBufferivDirect(ALCcontext *context, ALuint buffer, ALenum param, ALint *values) noexcept;

void AL_APIENTRY alGenEffectsDirect(ALCcontext *context, ALsizei n, ALuint *effects) noexcept;
void AL_APIENTRY alDeleteEffectsDirect(ALCcontext *context, ALsizei n, const ALuint *effects) noexcept;
ALboolean AL_APIENTRY alIsEffectDirect(ALCcontext *context, ALuint effect) noexcept;
void AL_APIENTRY alEffectiDirect(ALCcontext *context, ALuint effect, ALenum param, ALint iValue) noexcept;
void AL_APIENTRY alEffectivDirect(ALCcontext *context, ALuint effect, ALenum param, const ALint *piValues) noexcept;
void AL_APIENTRY alEffectfDirect(ALCcontext *context, ALuint effect, ALenum param, ALfloat flValue) noexcept;
void AL_APIENTRY alEffectfvDirect(ALCcontext *context, ALuint effect, ALenum param, const ALfloat *pflValues) noexcept;
void AL_APIENTRY alGetEffectiDirect(ALCcontext *context, ALuint effect, ALenum param, ALint *piValue) noexcept;
void AL_APIENTRY alGetEffectivDirect(ALCcontext *context, ALuint effect, ALenum param, ALint *piValues) noexcept;
void AL_APIENTRY alGetEffectfDirect(ALCcontext *context, ALuint effect, ALenum param, ALfloat *pflValue) noexcept;
void AL_APIENTRY alGetEffectfvDirect(ALCcontext *context, ALuint effect, ALenum param, ALfloat *pflValues) noexcept;

void AL_APIENTRY alGenFiltersDirect(ALCcontext *context, ALsizei n, ALuint *filters) noexcept;
void AL_APIENTRY alDeleteFiltersDirect(ALCcontext *context, ALsizei n, const ALuint *filters) noexcept;
ALboolean AL_APIENTRY alIsFilterDirect(ALCcontext *context, ALuint filter) noexcept;
void AL_APIENTRY alFilteriDirect(ALCcontext *context, ALuint filter, ALenum param, ALint iValue) noexcept;
void AL_APIENTRY alFilterivDirect(ALCcontext *context, ALuint filter, ALenum param, const ALint *piValues) noexcept;
void AL_APIENTRY alFilterfDirect(ALCcontext *context, ALuint filter, ALenum param, ALfloat flValue) noexcept;
void AL_APIENTRY alFilterfvDirect(ALCcontext *context, ALuint filter, ALenum param, const ALfloat *pflValues) noexcept;
void AL_APIENTRY alGetFilteriDirect(ALCcontext *context, ALuint filter, ALenum param, ALint *piValue) noexcept;
void AL_APIENTRY alGetFilterivDirect(ALCcontext *context, ALuint filter, ALenum param, ALint *piValues) noexcept;
void AL_APIENTRY alGetFilterfDirect(ALCcontext *context, ALuint filter, ALenum param, ALfloat *pflValue) noexcept;
void AL_APIENTRY alGetFilterfvDirect(ALCcontext *context, ALuint filter, ALenum param, ALfloat *pflValues) noexcept;

void AL_APIENTRY alGenAuxiliaryEffectSlotsDirect(ALCcontext *context, ALsizei n, ALuint *effectslots) noexcept;
void AL_APIENTRY alDeleteAuxiliaryEffectSlotsDirect(ALCcontext *context, ALsizei n, const ALuint *effectslots) noexcept;
ALboolean AL_APIENTRY alIsAuxiliaryEffectSlotDirect(ALCcontext *context, ALuint effectslot) noexcept;
void AL_APIENTRY alAuxiliaryEffectSlotiDirect(ALCcontext *context, ALuint effectslot, ALenum param, ALint iValue) noexcept;
void AL_APIENTRY alAuxiliaryEffectSlotivDirect(ALCcontext *context, ALuint effectslot, ALenum param, const ALint *piValues) noexcept;
void AL_APIENTRY alAuxiliaryEffectSlotfDirect(ALCcontext *context, ALuint effectslot, ALenum param, ALfloat flValue) noexcept;
void AL_APIENTRY alAuxiliaryEffectSlotfvDirect(ALCcontext *context, ALuint effectslot, ALenum param, const ALfloat *pflValues) noexcept;
void AL_APIENTRY alGetAuxiliaryEffectSlotiDirect(ALCcontext *context, ALuint effectslot, ALenum param, ALint *piValue) noexcept;
void AL_APIENTRY alGetAuxiliaryEffectSlotivDirect(ALCcontext *context, ALuint effectslot, ALenum param, ALint *piValues) noexcept;
void AL_APIENTRY alGetAuxiliaryEffectSlotfDirect(ALCcontext *context, ALuint effectslot, ALenum param, ALfloat *pflValue) noexcept;
void AL_APIENTRY alGetAuxiliaryEffectSlotfvDirect(ALCcontext *context, ALuint effectslot, ALenum param, ALfloat *pflValues) noexcept;

void AL_APIENTRY alBufferDataStaticDirect(ALCcontext *context, ALuint buffer, ALenum format, ALvoid *data, ALsizei size, ALsizei freq) noexcept;

void AL_APIENTRY alBufferSubDataDirectSOFT(ALCcontext *context, ALuint buffer, ALenum format, const ALvoid *data, ALsizei offset, ALsizei length) noexcept;

void AL_APIENTRY alRequestFoldbackStartDirect(ALCcontext *context, ALenum mode, ALsizei count, ALsizei length, ALfloat *mem, LPALFOLDBACKCALLBACK callback) noexcept;
void AL_APIENTRY alRequestFoldbackStopDirect(ALCcontext *context) noexcept;

void AL_APIENTRY alSourcedDirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALdouble value) noexcept;
void AL_APIENTRY alSource3dDirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALdouble value1, ALdouble value2, ALdouble value3) noexcept;
void AL_APIENTRY alSourcedvDirectSOFT(ALCcontext *context, ALuint source, ALenum param, const ALdouble *values) noexcept;
void AL_APIENTRY alGetSourcedDirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALdouble *value) noexcept;
void AL_APIENTRY alGetSource3dDirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALdouble *value1, ALdouble *value2, ALdouble *value3) noexcept;
void AL_APIENTRY alGetSourcedvDirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALdouble *values) noexcept;
void AL_APIENTRY alSourcei64DirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALint64SOFT value) noexcept;
void AL_APIENTRY alSource3i64DirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALint64SOFT value1, ALint64SOFT value2, ALint64SOFT value3) noexcept;
void AL_APIENTRY alSourcei64vDirectSOFT(ALCcontext *context, ALuint source, ALenum param, const ALint64SOFT *values) noexcept;
void AL_APIENTRY alGetSourcei64DirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALint64SOFT *value) noexcept;
void AL_APIENTRY alGetSource3i64DirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALint64SOFT *value1, ALint64SOFT *value2, ALint64SOFT *value3) noexcept;
void AL_APIENTRY alGetSourcei64vDirectSOFT(ALCcontext *context, ALuint source, ALenum param, ALint64SOFT *values) noexcept;

ALint64SOFT AL_APIENTRY alGetInteger64DirectSOFT(ALCcontext *context, ALenum pname) noexcept;
void AL_APIENTRY alGetInteger64vDirectSOFT(ALCcontext *context, ALenum pname, ALint64SOFT *values) noexcept;

void AL_APIENTRY alDeferUpdatesDirectSOFT(ALCcontext *context) noexcept;
void AL_APIENTRY alProcessUpdatesDirectSOFT(ALCcontext *context) noexcept;

const ALchar* AL_APIENTRY alGetStringiDirectSOFT(ALCcontext *context, ALenum pname, ALsizei index) noexcept;

void AL_APIENTRY alEventControlDirectSOFT(ALCcontext *context, ALsizei count, const ALenum *types, ALboolean enable) noexcept;
void AL_APIENTRY alEventCallbackDirectSOFT(ALCcontext *context, ALEVENTPROCSOFT callback, void *userParam) noexcept;
void* AL_APIENTRY alGetPointerDirectSOFT(ALCcontext *context, ALenum pname) noexcept;
void AL_APIENTRY alGetPointervDirectSOFT(ALCcontext *context, ALenum pname, void **values) noexcept;

void AL_APIENTRY alBufferCallbackDirectSOFT(ALCcontext *context, ALuint buffer, ALenum format, ALsizei freq, ALBUFFERCALLBACKTYPESOFT callback, ALvoid *userptr) noexcept;
void AL_APIENTRY alGetBufferPtrDirectSOFT(ALCcontext *context, ALuint buffer, ALenum param, ALvoid **ptr) noexcept;
void AL_APIENTRY alGetBuffer3PtrDirectSOFT(ALCcontext *context, ALuint buffer, ALenum param, ALvoid **ptr0, ALvoid **ptr1, ALvoid **ptr2) noexcept;
void AL_APIENTRY alGetBufferPtrvDirectSOFT(ALCcontext *context, ALuint buffer, ALenum param, ALvoid **ptr) noexcept;

void AL_APIENTRY alSourcePlayAtTimeDirectSOFT(ALCcontext *context, ALuint source, ALint64SOFT start_time) noexcept;
void AL_APIENTRY alSourcePlayAtTimevDirectSOFT(ALCcontext *context, ALsizei n, const ALuint *sources, ALint64SOFT start_time) noexcept;

ALenum AL_APIENTRY EAXSetDirect(ALCcontext *context, const struct _GUID *property_set_id, ALuint property_id, ALuint property_source_id, ALvoid *property_value, ALuint property_value_size) noexcept;
ALenum AL_APIENTRY EAXGetDirect(ALCcontext *context, const struct _GUID *property_set_id, ALuint property_id, ALuint property_source_id, ALvoid *property_value, ALuint property_value_size) noexcept;
ALboolean AL_APIENTRY EAXSetBufferModeDirect(ALCcontext *context, ALsizei n, const ALuint *buffers, ALint value) noexcept;
ALenum AL_APIENTRY EAXGetBufferModeDirect(ALCcontext *context, ALuint buffer, ALint *pReserved) noexcept;
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
